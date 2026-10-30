// mare::game::Weather - the first module ported onto the entity lifecycle (entity.hh).
// Same model as the old gfx/weather.c, hook by hook:
//
//   on_load      read MARE_WEATHER, build the rain pipeline and its fullscreen triangle
//                (the old code built them lazily on the first rainy frame)
//   on_update    Phase::FrameSetup - the smoothed weather state from the wind, the storms
//                and the hour; writes RF.fog_* / RF.vfog_* / RF.sky for the shaders
//   on_render    Phase::Overlay - rain streaks over the finished, tonemapped frame
//   on_unload    release the rain geometry
//
// The smoothed state stays exported as the C globals WX / weather_enabled (weather.h):
// sky.c and the dashboard read them and are not ported yet.

#include "../core/math.hh"
#include "../engine/entity.hh"

extern "C" {
#include "../core/diag.h"
#include "../core/types.h"
#include "../core/util.h"
#include "../world/world.h"
#include "../gfx/rhi.h"
#include "../gfx/render.h"
#include "../gfx/camera.h"
#include "../gfx/weather.h"
}

#include <cstdlib>
#include <cstring>
#include <memory>

// the C globals the unported readers (sky.c, dashboard.c) keep using
weather_t WX;
int weather_enabled = 1;

namespace mare::game {
namespace {

constexpr float WX_TAU = 3.0f;         // seconds the state takes to follow a change

float towards(float cur, float target, float dt) { return cur + (target - cur) * (1.f - std::exp(-dt / WX_TAU)); }

// --- rain: three layers of streaks falling with the wind's slant, drawn over the finished frame
const char *rain_vs = "in vec2 a_pos; out vec2 v_uv; void main(){ v_uv = a_pos * 0.5 + 0.5; gl_Position = vec4(a_pos, 0.0, 1.0); }\n";
const char *rain_fs =
    "in vec2 v_uv; uniform float u_rain, u_time, u_aspect, u_slant; out vec4 fragColor;\n"
    "float hash(vec2 p){ return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }\n"
    "void main(){\n"
    "  if (u_rain <= 0.001) discard;\n"
    "  vec2 p = vec2(v_uv.x * u_aspect, v_uv.y);\n"
    "  float a = 0.0;\n"
    "  for (int i = 0; i < 3; ++i) {\n"
    "    float sc = 70.0 + 45.0 * float(i);\n"
    "    vec2 q = p * sc; q.x += q.y * u_slant * 0.45; q.y += u_time * (16.0 + 7.0 * float(i));\n"
    "    vec2 cell = floor(q); float h = hash(cell);\n"
    "    float on = step(0.86 - 0.3 * u_rain, hash(cell + vec2(7.0, 3.0)));\n"
    "    float fx = fract(q.x), fy = fract(q.y);\n"
    "    float streak = smoothstep(0.09, 0.0, abs(fx - 0.5 - (h - 0.5) * 0.5)) * smoothstep(0.0, 0.25, fy) * smoothstep(1.0, 0.7, fy);\n"
    "    a += streak * on * (0.32 - 0.07 * float(i));\n"
    "  }\n"
    "  fragColor = vec4(vec3(0.78, 0.82, 0.90), clamp(a, 0.0, 1.0) * u_rain * 0.6);\n"
    "}\n";

} // namespace

class Weather final : public engine::Entity {
public:
    Weather() : Entity("weather", engine::Phase::FrameSetup) {}

    void on_load() override {
        force_ = getenv("MARE_WEATHER");
        if (force_ && !strcmp(force_, "0")) { weather_enabled = 0; force_ = nullptr; LOG_INFO("gpu", "weather off (MARE_WEATHER=0)"); }
        else if (force_) LOG_INFO("gpu", "weather pinned to '%s' (MARE_WEATHER)", force_);

        static const rhi_attr_t attrs[] = { { "a_pos", 0, 2, 0, 0 } };
        static const rhi_layout_t layout = { attrs, 1, { 2 * sizeof(float), 0 } };
        static const char *uniforms[] = { "u_rain", "u_time", "u_aspect", "u_slant", nullptr };
        rhi_pipeline_desc_t desc = {};
        desc.vs = rain_vs; desc.fs = rain_fs; desc.layout = &layout; desc.uniforms = uniforms; desc.blend = RHI_BLEND_ALPHA;
        rain_prg_ = rhi_pipeline_make(&desc);
        float tri[6] = { -1, -1, 3, -1, -1, 3 };
        rain_geo_ = rhi_geometry_make(&layout, tri, 3, nullptr, 0);
    }

    void on_unload() override {
        rhi_geometry_free(&rain_geo_);   // pipelines have no release path in the RHI (yet)
    }

    void on_update(const engine::UpdateContext &ctx) override {
        float dt = ctx.dt;
        weather_t t = {};
        if (weather_enabled) {
            // the wind at the look-at point: below ~0.45 the air is calm
            float wind = sea_wind_now_str;
            t.calm = clampf((0.50f - wind) / 0.25f, 0.f, 1.f);
            // storms: inside one fully, within 40 units of its edge fading in
            float storm = 0;
            for (int i = 0; i < G.nevents; ++i) { const event_t *e = &G.events[i]; if (e->type != EV_STORM) continue;
                float d = dist2d(e->x, e->z, cam.tx, cam.tz) - e->radius;
                storm = maxf(storm, clampf(e->strength, 0.3f, 1.f) * clampf(1.f - d / 40.f, 0.f, 1.f)); }
            t.storm = storm;
            // dawn mist in the bays: calm air, the hour after sunrise, water close to a shore
            float dawn = std::exp(-powf((RF.tod - 0.29f) / 0.05f, 2.f));
            float h = world_height(cam.tx, cam.tz);
            float shore = clampf(1.f - fabsf(h) / 2.0f, 0.f, 1.f);
            t.mist = t.calm * dawn * shore;
            t.haze = clampf(0.55f * t.calm + 0.6f * storm + 0.5f * t.mist, 0.f, 1.f);
            t.overcast = storm;
            t.rain = clampf((storm - 0.25f) / 0.6f, 0.f, 1.f);
            if (force_) {
                memset(&t, 0, sizeof t);
                if (!strcmp(force_, "calm"))  { t.calm = 1; t.haze = 0.55f; }
                if (!strcmp(force_, "mist"))  { t.calm = 1; t.mist = 1; t.haze = 1; }
                if (!strcmp(force_, "storm")) { t.storm = 1; t.overcast = 1; t.rain = 1; t.haze = 0.6f; }
            }
        }
        WX.calm = towards(WX.calm, t.calm, dt); WX.haze = towards(WX.haze, t.haze, dt); WX.mist = towards(WX.mist, t.mist, dt);
        WX.overcast = towards(WX.overcast, t.overcast, dt); WX.rain = towards(WX.rain, t.rain, dt); WX.storm = towards(WX.storm, t.storm, dt);
        // what the shaders read
        RF.fog_density = 1.f + 1.6f * WX.haze + 2.5f * WX.mist;
        RF.fog_start = lerpf(250.f, 40.f, clampf(WX.haze + WX.mist, 0.f, 1.f));
        RF.calm = WX.calm; RF.overcast = WX.overcast;
        // Volumetric fog: nearly nothing in clear air, a shallow dense layer in dawn mist, a deep
        // one under a storm. The sun in-scatter is what makes haze glow towards the sun and a
        // storm stay flat.
        RF.vfog_density = 0.00035f + 0.0042f * WX.mist + 0.0022f * WX.haze + 0.0016f * WX.storm;
        RF.vfog_height  = lerpf(26.f, 11.f, clampf(WX.mist, 0.f, 1.f));
        RF.vfog_sun     = (0.55f + 0.5f * WX.haze) * (1.f - 0.8f * WX.overcast);
        RF.vfog_color   = lerp3(lerp3(vec3(0.62f, 0.68f, 0.78f), vec3(0.88f, 0.90f, 0.93f), WX.mist),
                                vec3(0.45f, 0.47f, 0.52f), WX.overcast);
        // the sky: grey under a storm, whitened by mist (mare.c blends the map parchment after this)
        RF.sky = lerp3(RF.sky, vec3(0.52f, 0.54f, 0.58f), WX.overcast * 0.75f);
        RF.sky = lerp3(RF.sky, vec3(0.86f, 0.88f, 0.90f), WX.mist * 0.7f);
    }

    void on_render(const engine::RenderContext &ctx) override {
        if (WX.rain <= 0.002f || ctx.mapness > 0.9f) return;
        // the wind's screen-space slant: its component along the camera's right vector
        float slant = sea_wind_dx * cam.right.x + sea_wind_dz * cam.right.z;
        rhi_pipeline_bind(&rain_prg_);
        rhi_set_f1(rhi_uniform(&rain_prg_, "u_rain"), WX.rain * (1.f - ctx.mapness));
        rhi_set_f1(rhi_uniform(&rain_prg_, "u_time"), RF.time);
        rhi_set_f1(rhi_uniform(&rain_prg_, "u_aspect"), screen_w / maxf(1.f, (float)screen_h));
        rhi_set_f1(rhi_uniform(&rain_prg_, "u_slant"), slant * 0.8f);
        rhi_draw(&rain_geo_, 0);
    }

private:
    const char    *force_ = nullptr;      // MARE_WEATHER pin, NULL when live
    rhi_pipeline_t rain_prg_ = {};
    rhi_geometry_t rain_geo_ = {};
};

std::unique_ptr<engine::Entity> make_weather() { return std::make_unique<Weather>(); }

} // namespace mare::game
