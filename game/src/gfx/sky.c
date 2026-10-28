// Sky (DESIGN_VISUAL.md step 8): a full-screen pass drawn first into the HDR target, behind
// everything - a gradient from the sky colour at the zenith to a paler, hazier horizon, the sun
// as a disc with a glow, and a cloud layer: value-noise fbm on a plane SKY_CLOUD_H units up,
// drifting with the wind, lit by the sun and thinned towards the horizon. The same noise, in
// GLSL_CLOUDS, darkens the sun term of every lit surface where a cloud sits between it and the
// sun, so the ground is dappled by the sky it is under. Cover comes from the weather: some in
// clear air, most under a storm. MARE_CLOUDS=x pins the cover (0 = none).
#include <math.h>
#include <stdlib.h>
#include "render.h"
#include "glsl.h"
#include "camera.h"
#include "weather.h"
#include "../core/util.h"
#include "../core/diag.h"

#define SKY_CLOUD_H 60.f

float sky_cloud_cover = 0.3f;       // 0..1, what the shaders see this frame
float sky_cloud_off[2];             // accumulated drift of the cloud field (world units * 0.011)
int   sky_enabled = 1;
static float cloud_pin = -1.f;
static rhi_pipeline_t sky_prg; static rhi_geometry_t sky_tri; static int sky_init_done;

static const char *sky_vs = "in vec2 a_pos; out vec2 v_uv; void main(){ v_uv = a_pos * 0.5 + 0.5; gl_Position = vec4(a_pos, 0.0, 1.0); }\n";
static const char *sky_fs =
    "in vec2 v_uv; out vec4 fragColor;\n"
    "uniform mat4 u_inv_vp; uniform vec3 u_campos; uniform vec3 u_sun; uniform vec3 u_sky; uniform vec3 u_suncol;\n"
    "uniform float u_daylight; uniform float u_haze; uniform float u_overcast;\n"
    GLSL_COLOR GLSL_CLOUDS
    "void main(){\n"
    "  vec4 w = u_inv_vp * vec4(v_uv * 2.0 - 1.0, 1.0, 1.0);\n"
    "  vec3 dir = normalize(w.xyz / w.w - u_campos);\n"
    "  float h = clamp(dir.y, -1.0, 1.0);\n"
    "  vec3 sky = srgb_to_linear(u_sky);\n"
    "  vec3 zenith = sky * (0.72 - 0.1 * u_overcast);\n"
    "  vec3 horizon = mix(sky, vec3(0.86, 0.88, 0.92) * max(u_daylight, 0.3), 0.5 + 0.4 * u_haze);\n"
    "  vec3 col = mix(horizon, zenith, pow(clamp(h, 0.0, 1.0), 0.42));\n"
    "  if (h < 0.0) col = horizon * (1.0 + h * 0.6);\n"
    "  float d = dot(dir, u_sun);\n"
    "  float disc = smoothstep(0.99935, 0.99985, d), glow = pow(max(d, 0.0), 72.0) * 0.14 + pow(max(d, 0.0), 9.0) * 0.045;\n"
    "  col += u_suncol * (disc * 1.2 + glow) * (1.0 - 0.7 * u_cloud.z * u_overcast);\n"
    "  if (u_cloud.z > 0.001 && dir.y > 0.004) {\n"
    "    float t = (u_cloud.w - u_campos.y) / dir.y; vec2 cxz = u_campos.xz + dir.xz * t;\n"
    "    float dens = cloud_density(cxz, 5);\n"
    "    float horiz = smoothstep(0.0, 0.10, dir.y);\n"                       // clouds thin into the haze at the horizon
    "    float sunup = clamp(u_sun.y * 1.6, 0.0, 1.0);\n"
    "    vec3 lit = mix(vec3(0.50, 0.53, 0.60), vec3(1.0, 0.99, 0.97), sunup) * max(u_daylight, 0.25);\n"
    "    lit = mix(lit * 0.55, lit, 1.0 - dens * 0.75);\n"                   // thick parts are dark underneath
    "    lit = mix(lit, lit * vec3(0.78, 0.80, 0.86), u_overcast * 0.6);\n"
    "    col = mix(col, lit, dens * horiz);\n"
    "  }\n"
    "  fragColor = vec4(col, 1.0);\n"
    "}\n";

void sky_update(float dt) {
    static int env_read; if (!env_read) { env_read = 1; const char *e = getenv("MARE_CLOUDS"); if (e) { cloud_pin = clampf((float)atof(e), 0.f, 1.f); LOG_INFO("gpu", "cloud cover pinned to %.2f (MARE_CLOUDS)", cloud_pin); } }
    float target = clampf(0.28f + 0.55f * WX.storm + 0.10f * WX.calm - 0.1f * WX.mist, 0.f, 0.95f);
    if (cloud_pin >= 0.f) target = cloud_pin;
    sky_cloud_cover += (target - sky_cloud_cover) * (1.f - expf(-dt / 4.f));
    // the cloud field drifts downwind, faster than the sea does, as an accumulated offset
    sky_cloud_off[0] += sea_wind_dx * 0.011f * 1.8f * dt; sky_cloud_off[1] += sea_wind_dz * 0.011f * 1.8f * dt;
}

// the cloud uniform every lit pipeline shares (GLSL_CLOUDS): drift, cover, plane height
void sky_bind_clouds(const rhi_pipeline_t *p) {
    rhi_set_f4(rhi_uniform(p, "u_cloud"), sky_cloud_off[0], sky_cloud_off[1], sky_enabled ? sky_cloud_cover : 0.f, SKY_CLOUD_H);
}

void sky_draw(void) {
    if (!sky_enabled || RF.mapness > 0.97f) return;
    if (!sky_init_done) {
        static const rhi_attr_t attrs[] = { { "a_pos", 0, 2, 0, 0 } };
        static const rhi_layout_t layout = { attrs, 1, { 2 * sizeof(float), 0 } };
        static const char *uniforms[] = { "u_inv_vp", "u_campos", "u_sun", "u_sky", "u_suncol", "u_daylight", "u_haze", "u_overcast", "u_cloud", 0 };
        sky_prg = rhi_pipeline_make(&(rhi_pipeline_desc_t){ .vs = sky_vs, .fs = sky_fs, .layout = &layout, .uniforms = uniforms });   // no depth test, no depth write: the background
        float tri[6] = { -1, -1, 3, -1, -1, 3 };
        sky_tri = rhi_geometry_make(&layout, tri, 3, 0, 0);
        sky_init_done = 1;
    }
    rhi_pipeline_bind(&sky_prg);
    rhi_set_mat4(rhi_uniform(&sky_prg, "u_inv_vp"), &cam.inv_vp.x.x);
    rhi_set_f3(rhi_uniform(&sky_prg, "u_campos"), cam.pos.x, cam.pos.y, cam.pos.z);
    rhi_set_f3(rhi_uniform(&sky_prg, "u_sun"), RF.sun.x, RF.sun.y, RF.sun.z);
    rhi_set_f3(rhi_uniform(&sky_prg, "u_sky"), RF.sky.x, RF.sky.y, RF.sky.z);
    rhi_set_f3(rhi_uniform(&sky_prg, "u_suncol"), RF.suncol.x, RF.suncol.y, RF.suncol.z);
    rhi_set_f1(rhi_uniform(&sky_prg, "u_daylight"), RF.daylight);
    rhi_set_f1(rhi_uniform(&sky_prg, "u_haze"), WX.haze + WX.mist);
    rhi_set_f1(rhi_uniform(&sky_prg, "u_overcast"), WX.overcast);
    sky_bind_clouds(&sky_prg);
    rhi_draw(&sky_tri, 0);
}
