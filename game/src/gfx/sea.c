// Sea: a camera-centred detail grid with Gerstner waves over a world-wide flat plane, shaded
// from the heightmap (depth colour, foam, authored normal/foam textures); parchment blue with a
// graticule in map view.
//
// Geometry. The world grid (coarse, static, covering the whole map) is flat: at map and
// continental zoom that is all the sea needs to be. Inside a square of SEA_DETAIL_R units
// around the camera's look-at point the *detail grid* takes over - a grid whose cells grow
// exponentially with distance from the centre (SEA_WARP), so cells are ~0.25 units under the
// camera and ~13 units at the edge, which is what puts enough vertices under a ship to displace.
// The world pass discards inside that square, so the two never blend over each other.
//
// Waves. Four Gerstner waves (SEA_WAVES) displace the detail grid: crests sharpen, troughs
// flatten and the surface moves horizontally as well as vertically, which is what makes water
// read as water rather than as a wobbling sheet. The analytic surface derivatives give the
// normal, so no extra geometry is needed for shading; the authored normal map (game/assets/sea,
// cut seamless by tools/sea_textures.py) adds the ripple detail below the wave scale. Amplitude
// fades to zero towards the edge of the detail square, in shallow water (so waves never break
// through a beach) and in map view, so the two grids always meet on a flat surface.
//
// Scale. `u_wave_scale` stretches the whole wave field: the world uses 1 (ships are ~2 units
// long, waves ~2-6 units), the detail view passes DV_SHIP_SCALE so the same sea reads correctly
// at 1 unit ~ 4 m. sea_draw_local() draws the detail grid alone at a constant depth for the
// staged battle, which is why that scene gets the same water as the map.

#include <stdlib.h>
#include <stdio.h>
#include "render.h"
#include "glsl.h"
#include "../core/types.h"
#include "../core/util.h"
#include "../core/diag.h"
#include "../world/wind.h"
#include "../assets/asset.h"

const rhi_texture_t *terrain_heightmap_texture(void);

static rhi_pipeline_t sea_prg, sea_detail_prg;
static rhi_geometry_t sea_geo, sea_detail_geo;
static int sea_tex_normal = -1, sea_tex_albedo = -1;   // textures.c handles

#define SEA_DETAIL_N  192        // cells per side of the detail grid (74k triangles)
#define SEA_DETAIL_R  180.f      // world units from the centre to its edge
#define SEA_WARP      4.0f       // cell growth: exp(SEA_WARP * |u|); 0.14 units at the centre, 7.6 at the edge

// The wave set is derived from the wind every frame (sea_update_waves below), not authored.
// shared varyings and the depth lookup both vertex shaders use
#define GLSL_SEA_VS_COMMON \
    "uniform mat4 u_vp; uniform sampler2D u_hm; uniform vec4 u_geo; uniform float u_k;\n" \
    "uniform float u_time; uniform float u_wave_amp; uniform float u_wave_scale; uniform float u_local_depth;\n" \
    "out vec3 v_wpos; out vec3 v_nrm; out float v_slope; out float v_h; out float v_amp; out float v_edge;\n" \
    /* raw heightmap value: negative below sea level; > 0.35 is land, which the sea discards */ \
    "float height_at(vec2 p) {\n" \
    "  if (u_local_depth > 0.0) return -u_local_depth;\n" \
    "  float lon = p.x / u_k, lat = -p.y / u_k;\n" \
    "  vec2 uv = vec2((lon - u_geo.x)/(u_geo.y-u_geo.x), (u_geo.w - lat)/(u_geo.w-u_geo.z));\n" \
    "  return texture(u_hm, uv).r;\n" \
    "}\n"

// world pass: the static grid, flat
static const char *sea_vs =
    "in vec2 a_pos;\n"
    GLSL_SEA_VS_COMMON
    "void main(){\n"
    "  v_wpos = vec3(a_pos.x, 0.0, a_pos.y);\n"
    "  v_nrm = vec3(0.0, 1.0, 0.0); v_slope = 0.0; v_h = height_at(a_pos); v_amp = 0.0; v_edge = 0.0;\n"
    "  gl_Position = u_vp * vec4(v_wpos, 1.0);\n"
    "}\n";

// detail pass: a_pos is (u,v) in [-1,1]; the grid is warped around u_center and displaced
static const char *sea_detail_vs =
    "in vec2 a_pos;\n"
    "uniform vec3 u_center;\n"                   // xz = centre, z component unused
    "uniform vec3 u_detail;\n"                   // x = radius, y = warp, z = grid step in a_pos units (2/cells)
    GLSL_SEA_VS_COMMON
    GLSL_WAVES
    "void main(){\n"
    "  vec2 w = sign(a_pos) * (exp(u_detail.y * abs(a_pos)) - 1.0) / (exp(u_detail.y) - 1.0) * u_detail.x;\n"
    "  vec2 p = u_center.xz + w;\n"
    // World size of one cell here: the grid is warped, so cells are ~0.14 units under the
    // camera and ~7.6 at the edge. d|w|/da * the step in a_pos. Waves shorter than a few of
    // these are faded out by wave_sum instead of aliasing into a sliding pattern.
    "  vec2 dwda = u_detail.y * exp(u_detail.y * abs(a_pos)) / (exp(u_detail.y) - 1.0) * u_detail.x;\n"
    "  float cell = max(dwda.x, dwda.y) * u_detail.z;\n"
    "  float h = height_at(p); float dep = max(-h, 0.0);\n"
    // amplitude: full in open water, fading into the shallows and towards the grid edge so the
    // detail grid always meets the flat world grid (and the beach) on a flat surface
    "  float edge = 1.0 - smoothstep(0.55, 0.96, max(abs(a_pos.x), abs(a_pos.y)));\n"
    "  v_edge = edge;\n"
    "  float shal = smoothstep(0.0, 1.1 * u_wave_scale, dep);\n"
    "  float amp = u_wave_amp * edge * shal;\n"
    "  v_amp = amp;\n"
    "  vec3 tx, tz; float crest;\n"
    "  vec3 d = wave_sum(p, amp, u_wave_scale, cell, tx, tz, crest);\n"
    "  v_slope = 1.0 - normalize(cross(tz, tx)).y;\n"
    "  v_wpos = vec3(p.x + d.x, d.y, p.y + d.z);\n"
    "  v_nrm = normalize(cross(tz, tx));\n"
    "  v_h = h;\n"
    "  gl_Position = u_vp * vec4(v_wpos, 1.0);\n"
    "}\n";

static const char *sea_fs =
    "in vec3 v_wpos; in vec3 v_nrm; in float v_slope; in float v_h; in float v_amp; in float v_edge;\n"
    "uniform sampler2D u_waves; uniform sampler2D u_albedo; uniform float u_has_albedo;\n"
    "uniform float u_time; uniform float u_wave_scale; uniform float u_local_depth; uniform vec4 u_geo; uniform float u_k;\n"
    "uniform vec3 u_campos; uniform float u_mapness; uniform vec3 u_sky; uniform float u_daylight;\n"
    "uniform vec2 u_wind;\n"                    // wind direction: the surface detail drifts along it
    "uniform vec4 u_hole;\n"                     // xy = centre, z = half size, w = 1 when the world pass must cut the detail square out
    "uniform vec2 u_clip;\n"                     // x = world z the water stops at, y = 1 when that clip is on (staged city: the shore)
    "out vec4 fragColor;\n"
    GLSL_COLOR GLSL_PBR GLSL_LIGHT GLSL_NMAP GLSL_FOG
    "void main(){\n"
    "  if (u_hole.w > 0.5 && abs(v_wpos.x - u_hole.x) < u_hole.z && abs(v_wpos.z - u_hole.y) < u_hole.z) discard;\n"
    "  if (u_local_depth <= 0.0 && v_h > 0.35) discard;\n"              // land (the same threshold the sea has always used)
    "  if (u_clip.y > 0.5 && v_wpos.z > u_clip.x) discard;\n"           // staged scenes have no heightmap: the shore is a line
    "  float lon = v_wpos.x / u_k, lat = -v_wpos.z / u_k;\n"
    "  float depth = max(-v_h, 0.0);\n"
    "  float dist = distance(v_wpos, u_campos);\n"
    "  float detail = (1.0 - smoothstep(60.0 * u_wave_scale, 400.0 * u_wave_scale, dist)) * v_edge;\n"
    "  vec2 p = v_wpos.xz / u_wave_scale; float t = u_time;\n"
    // two scrolling samples of the tileable wave normal map, large swell + fine chop, riding on
    // top of the Gerstner normal (which carries the shape, this carries the ripple)
    // the ripple detail drifts downwind, like the swell it sits on
    "  vec2 wdrift = u_wind * t;\n"
    "  vec2 wskew = vec2(u_wind.x * 0.91 - u_wind.y * 0.42, u_wind.x * 0.42 + u_wind.y * 0.91) * t;\n"   // 25 degrees off the wind
    "  vec3 n1 = nmap_decode(texture(u_waves, p * 0.33 + wdrift * 0.030).rgb);\n"
    "  vec3 n2 = nmap_decode(texture(u_waves, p * 1.15 + wskew * 0.058 + vec2(0.37, 0.11)).rgb);\n"
    "  vec2 slope = (n1.xy / max(n1.z, 0.3) + n2.xy / max(n2.z, 0.3) * 0.6) * 0.42 * detail;\n"
    "  vec3 wn = normalize(v_nrm);\n"
    "  vec3 n = normalize(wn + vec3(slope.x, 0.0, slope.y));\n"
    "  float w1 = sin(p.x*3.1 + p.y*1.3 + t*1.7);\n"   // keeps the foam band animated
    "  vec3 deep = vec3(0.02,0.10,0.24), shallow = vec3(0.16,0.56,0.60);\n"
    "  vec3 c = srgb_to_linear(mix(shallow, deep, 1.0 - exp(-depth*0.9)));\n"
    // authored surface: the foam albedo rides the swell layer (and a finer copy the chop), its
    // bright streaks become whitecaps over the depth colour and its tonal variation breaks up
    // the flat gradient; everything fades out with `detail` like the normals do
    "  vec3 alb1 = texture(u_albedo, p * 0.33 + wdrift * 0.030).rgb;\n"
    "  vec3 alb2 = texture(u_albedo, p * 1.15 + wskew * 0.058 + vec2(0.37, 0.11)).rgb;\n"
    "  float lum1 = dot(alb1, vec3(0.3, 0.5, 0.2)), lum2 = dot(alb2, vec3(0.3, 0.5, 0.2));\n"
    "  float streak = max(smoothstep(0.22, 0.58, lum1), 0.5 * smoothstep(0.24, 0.62, lum2)) * u_has_albedo * detail;\n"
    "  c *= mix(1.0, clamp(0.80 + lum1 * 3.0, 0.7, 1.35), 0.30 * u_has_albedo * detail);\n"
    "  vec3 vdir = normalize(u_campos - v_wpos);\n"
    // water: smooth dielectric, so the sun glint comes from the GGX lobe and the sky from Fresnel
    // Specular antialiasing. One pixel covers many waves once the camera pulls back, and a
    // sharp GGX lobe sampled at one point per pixel then turns into a field of fireflies.
    // Measuring how much the normal actually swings per pixel and folding that variance into
    // roughness fixes it at every distance, instead of guessing with a distance curve.
    "  float rough = mix(0.45, 0.13, detail);\n"
    "  vec3 ndx = dFdx(n), ndy = dFdy(n);\n"
    "  float nvar = dot(ndx, ndx) + dot(ndy, ndy);\n"
    "  rough = clamp(sqrt(rough*rough + nvar * 2.0), 0.0, 1.0);\n"
    "  vec3 col3d = sun_shade(c, n, vdir, v_wpos, rough, 0.0, dist);\n"
    "  float fres = 0.02 + 0.98 * pow(1.0 - max(dot(n, vdir), 0.0), 5.0);\n"
    "  col3d += srgb_to_linear(u_sky) * u_ambsky * fres * 1.4;\n"
    "  float foam = (1.0 - smoothstep(0.0, 0.45 * u_wave_scale, depth)) * (0.55 + 0.45*sin(depth*30.0/u_wave_scale - t*2.0 + w1)) * detail;\n"
    "  col3d = mix(col3d, srgb_to_linear(vec3(0.85,0.9,0.92)) * (u_ambsky + u_suncol * 0.35), foam*0.7);\n"
    "  col3d = mix(col3d, srgb_to_linear(vec3(0.90,0.93,0.95)) * (u_ambsky * 0.9 + u_suncol * 0.30), streak * 0.10);\n"
    // whitecaps where the Gerstner crests sharpen: the one cue that sells moving water
    // Whitecaps come from the surface the fragment actually has, not from a value interpolated
    // across a grid cell (which paints the cells themselves as diamonds): the steep faces near
    // a crest foam, the flat troughs do not. The authored streaks ride on top of that.
    "  float caps = smoothstep(0.022, 0.075, v_slope) * clamp(v_amp, 0.0, 1.0) * detail;\n"
    "  col3d = mix(col3d, srgb_to_linear(vec3(0.93,0.96,0.98)) * (u_ambsky * 1.0 + u_suncol * 0.35), caps * 0.55);\n"
    "  vec3 mapc = mix(vec3(0.62,0.72,0.80), vec3(0.50,0.62,0.74), 1.0 - exp(-depth*0.8));\n"
    "  float gx = abs(fract(lon/10.0 + 0.5) - 0.5) * 10.0, gy = abs(fract(lat/10.0 + 0.5) - 0.5) * 10.0;\n"
    "  float grid = 1.0 - smoothstep(0.0, 0.06, min(gx, gy));\n"
    "  float gx5 = abs(fract(lon/5.0 + 0.5) - 0.5) * 5.0, gy5 = abs(fract(lat/5.0 + 0.5) - 0.5) * 5.0;\n"
    "  float grid5 = (1.0 - smoothstep(0.0, 0.035, min(gx5, gy5))) * 0.5;\n"
    "  mapc = mix(mapc, vec3(0.35,0.42,0.52), max(grid, grid5) * 0.8);\n"
    "  mapc = mix(mapc, vec3(0.30,0.40,0.52), (1.0 - smoothstep(0.0, 0.6, depth))*0.55);\n"
    "  vec3 col = fog_apply(mix(col3d, srgb_to_linear(mapc) * u_daylight, u_mapness), dist, u_mapness, srgb_to_linear(u_sky));\n"
    // transparency: the terrain below has already been drawn, so shallow water reveals the
    // seabed and deep water goes opaque; the map view stays solid parchment
    "  float alpha = mix(0.38, 0.92, smoothstep(0.0, 1.6 * u_wave_scale, depth));\n"   // clearer shallows, seabed reads through a little further out
    "  alpha = max(alpha, max(foam, caps * 0.9) * 0.85);\n"        // surf and whitecaps stay visible over the seabed
    "  alpha = mix(alpha, 1.0, u_mapness);\n"
    "  fragColor = vec4(col, alpha);\n"
    "}\n";

static const char *sea_uniforms[] = { GLSL_WAVE_UNIFORMS, "u_vp", "u_hm", "u_waves", "u_albedo", "u_has_albedo", "u_geo", "u_k", "u_time",
                                      "u_campos", "u_mapness", "u_sky", "u_daylight", "u_center", "u_detail",
                                      "u_wave_amp", "u_wave_scale", "u_local_depth", "u_hole", "u_clip", GLSL_LIGHT_UNIFORMS, 0 };

void sea_init(void) {
    static const rhi_attr_t attrs[] = { { "a_pos", 0, 2, 0, 0 } };
    static const rhi_layout_t layout = { attrs, 1, { 2*sizeof(float), 0 } };
    sea_prg        = rhi_pipeline_make(&(rhi_pipeline_desc_t){ .vs = sea_vs,        .fs = sea_fs, .layout = &layout, .uniforms = sea_uniforms, .depth_test = 1, .depth_write = 1, .blend = RHI_BLEND_ALPHA });
    sea_detail_prg = rhi_pipeline_make(&(rhi_pipeline_desc_t){ .vs = sea_detail_vs, .fs = sea_fs, .layout = &layout, .uniforms = sea_uniforms, .depth_test = 1, .depth_write = 1, .blend = RHI_BLEND_ALPHA });
    {   // world grid: static, covers the whole map
        int nx = 88, ny = 65;
        float *v = malloc((nx+1)*(ny+1)*2*sizeof(float));
        float x0 = lon2x(LON0), x1 = lon2x(LON1), z0 = lat2z(LAT1), z1 = lat2z(LAT0);
        for (int y = 0; y <= ny; ++y) for (int x = 0; x <= nx; ++x) { v[(y*(nx+1)+x)*2] = lerpf(x0,x1,x/(float)nx); v[(y*(nx+1)+x)*2+1] = lerpf(z0,z1,y/(float)ny); }
        unsigned *idx = malloc(nx*ny*6*sizeof(unsigned)); int m = 0;
        for (int y = 0; y < ny; ++y) for (int x = 0; x < nx; ++x) { unsigned a = y*(nx+1)+x, b = a+1, c = a+nx+1, d = c+1; idx[m++]=a; idx[m++]=c; idx[m++]=b; idx[m++]=b; idx[m++]=c; idx[m++]=d; }
        sea_geo = rhi_geometry_make(&layout, v, (nx+1)*(ny+1), idx, m);
        free(v); free(idx);
    }
    {   // detail grid: parameter space [-1,1]^2, warped and placed by the vertex shader
        int n = SEA_DETAIL_N, vn = n + 1;
        float *v = malloc((size_t)vn*vn*2*sizeof(float));
        for (int y = 0; y < vn; ++y) for (int x = 0; x < vn; ++x) { v[(y*vn+x)*2] = x * 2.f / n - 1.f; v[(y*vn+x)*2+1] = y * 2.f / n - 1.f; }
        unsigned *idx = malloc((size_t)n*n*6*sizeof(unsigned)); int m = 0;
        for (int y = 0; y < n; ++y) for (int x = 0; x < n; ++x) { unsigned a = y*vn+x, b = a+1, c = a+vn, d = c+1; idx[m++]=a; idx[m++]=c; idx[m++]=b; idx[m++]=b; idx[m++]=c; idx[m++]=d; }
        sea_detail_geo = rhi_geometry_make(&layout, v, vn*vn, idx, m);
        free(v); free(idx);
    }
    // authored tiles, streamed (no stall at start-up; the procedural map covers until they land)
    char path[600];
    snprintf(path, sizeof path, "%s/sea/sea_normal.png", asset_root()); sea_tex_normal = texture_request(path, RHI_FMT_RGB8, RHI_FILTER_MIPMAP, RHI_WRAP_REPEAT);
    snprintf(path, sizeof path, "%s/sea/sea_albedo.png", asset_root()); sea_tex_albedo = texture_request(path, RHI_FMT_SRGB8, RHI_FILTER_MIPMAP, RHI_WRAP_REPEAT);
}

// ---------------------------------------------------------------------------
// The wave set (DESIGN_VISUAL.md step 4)
//
// Five Gerstner waves generated from the wind at the sea's centre (world/wind.c), not authored:
//
//   direction   spread around the wind direction; the spread narrows as the wind strengthens,
//               so a gale runs in near-parallel lines and a light air is confused and crossed
//   wavelength  a geometric series whose longest member grows with the wind, so a fresh breeze
//               makes a longer, slower swell as well as a taller one
//   height      amplitude follows wavelength (WAVE_STEEP caps the steepness) and the whole
//               field scales with the wind: at the edge of the doldrums the sea is nearly glassy
//   phase       accumulated per wave (see GLSL_WAVES), so changing a wavelength never makes the
//               crests jump; each wave also gets a fixed random offset from the game seed, so
//               two games do not get the same sea
//
// The wind is sampled at the look-at point and smoothed over about two seconds: crossing from
// one wind band into the next must turn the swell, not snap it.
#define SEA_WIND_TAU   2.0f     // seconds the wave set takes to follow a change in the wind
#define SEA_SPREAD_MAX 0.95f    // radians either side of the wind in a light air
#define SEA_SPREAD_MIN 0.28f    // ... and in a strong one
#define SEA_LEN0       3.6f     // longest wavelength at wind strength 1, world units
#define SEA_LEN_RATIO  0.64f    // each wave shorter than the last by this factor

typedef struct sea_wave_t { float dir[2], len, steep; } sea_wave_t;
static sea_wave_t sea_wave[SEA_WAVES];
static float sea_wave_phase[SEA_WAVES];        // accumulated, radians
static float sea_wave_jit[SEA_WAVES][3];       // per-wave randomness: angle, length, steepness
static int   sea_wave_seeded;
float sea_wind_dx = 1.f, sea_wind_dz = 0.f, sea_wind_str = 1.f;   // smoothed, for the shaders and the dashboard
float sea_wind_amp = 1.f;                                         // wave height factor from the wind

static float sea_hash01(unsigned h) {
    h ^= h >> 16; h *= 0x7FEB352Du; h ^= h >> 15; h *= 0x846CA68Bu; h ^= h >> 16;
    return (h & 0xFFFFFF) / (float)0x1000000;
}

// Once per frame, before anything draws water. `day` drives the wind model, `dt` the smoothing.
void sea_update_waves(float day, float dt) {
    if (!sea_wave_seeded) {
        sea_wave_seeded = 1;
        for (int i = 0; i < SEA_WAVES; ++i) {
            unsigned h = G.seed * 0x9E3779B1u + (unsigned)i * 0x85EBCA6Bu;
            sea_wave_jit[i][0] = sea_hash01(h) - 0.5f;             // angle offset, +-0.5 of the band
            sea_wave_jit[i][1] = 0.78f + 0.44f * sea_hash01(h ^ 0x51u);   // wavelength multiplier
            sea_wave_jit[i][2] = 0.80f + 0.40f * sea_hash01(h ^ 0xA3u);   // steepness multiplier
            sea_wave_phase[i]  = sea_hash01(h ^ 0xF7u) * 6.2831853f;      // so they do not all crest together at t=0
        }
    }
    float wdx, wdz, str;
    wind_at(RF.sea_center.x, RF.sea_center.z, day, &wdx, &wdz, &str);
    float k = dt > 0 ? 1.f - expf(-dt / SEA_WIND_TAU) : 1.f;
    sea_wind_dx += (wdx - sea_wind_dx) * k;
    sea_wind_dz += (wdz - sea_wind_dz) * k;
    sea_wind_str += (str - sea_wind_str) * k;
    float l = sqrtf(sea_wind_dx * sea_wind_dx + sea_wind_dz * sea_wind_dz);
    if (l > 1e-4f) { sea_wind_dx /= l; sea_wind_dz /= l; } else { sea_wind_dx = 1; sea_wind_dz = 0; }

    float s = clampf(sea_wind_str, 0.f, 1.6f);
    // Height: the doldrums (strength ~0.35) leave a nearly flat sea, a strong westerly a heavy one.
    sea_wind_amp = clampf(0.10f + 0.95f * (s - 0.18f), 0.06f, 1.55f);
    float spread = lerpf(SEA_SPREAD_MAX, SEA_SPREAD_MIN, clampf((s - 0.3f) / 0.9f, 0.f, 1.f));
    float base = atan2f(sea_wind_dz, sea_wind_dx);
    float len0 = SEA_LEN0 * (0.70f + 0.45f * s);
    for (int i = 0; i < SEA_WAVES; ++i) {
        float t = SEA_WAVES > 1 ? (float)i / (SEA_WAVES - 1) * 2.f - 1.f : 0.f;   // -1 .. +1 across the fan
        float ang = base + (t * 0.8f + sea_wave_jit[i][0] * 0.7f) * spread;
        sea_wave[i].dir[0] = cosf(ang); sea_wave[i].dir[1] = sinf(ang);
        sea_wave[i].len = len0 * powf(SEA_LEN_RATIO, (float)i) * sea_wave_jit[i][1];
        // the long leading swell carries the most height; the short cross seas ride on it
        sea_wave[i].steep = (0.62f - 0.07f * i) * sea_wave_jit[i][2];
        // deep-water dispersion: long waves run faster. Phase is integrated, never speed * time.
        float kw = 6.2831853f / maxf(sea_wave[i].len, 1e-4f);
        float spd = sqrtf(9.81f / kw) * 0.42f;
        sea_wave_phase[i] += kw * spd * dt;
        if (sea_wave_phase[i] > 6.2831853f) sea_wave_phase[i] -= 6.2831853f * floorf(sea_wave_phase[i] / 6.2831853f);
    }
}

// Upload the wave set to any pipeline built with GLSL_WAVES (the sea and the instanced meshes,
// so ships ride exactly the water they float on).
void sea_bind_waves(const rhi_pipeline_t *p) {
    float w[SEA_WAVES * 4];
    for (int i = 0; i < SEA_WAVES; ++i) {
        w[i*4+0] = sea_wave[i].dir[0]; w[i*4+1] = sea_wave[i].dir[1];
        w[i*4+2] = sea_wave[i].len;    w[i*4+3] = sea_wave[i].steep;
    }
    rhi_set_f4v(rhi_uniform(p, "u_wave"), w, SEA_WAVES);
    rhi_set_f1v(rhi_uniform(p, "u_wave_phase"), sea_wave_phase, SEA_WAVES);
    rhi_set_f2(rhi_uniform(p, "u_wind"), sea_wind_dx, sea_wind_dz);
}

int sea_nyquist = 1;         // fade waves the detail grid cannot sample (MARE_SEA_NYQUIST=0 shows the aliasing it removes)
float sea_wave_amp = 1.f;    // global wave height knob (dashboard / MARE_WAVES; 0 = flat water)
float sea_wave_scale = 1.f;  // set per scene; the instanced meshes read it too, so ships ride the same swell
float sea_local_depth = 0.f;
float sea_clip_z; int sea_clip_on;   // staged scenes: water only on the seaward side of this world z

// uniforms every sea draw shares
static void sea_bind_common(const rhi_pipeline_t *p, float wave_scale, float local_depth) {
    sea_bind_waves(p);
    rhi_set_mat4(rhi_uniform(p,"u_vp"), &RF.vp.x.x);
    rhi_set_i1(rhi_uniform(p,"u_hm"), 0); rhi_texture_bind(0, terrain_heightmap_texture());
    const rhi_texture_t *nrm = texture_get(sea_tex_normal), *alb = texture_get(sea_tex_albedo);
    rhi_set_i1(rhi_uniform(p,"u_waves"), 1); rhi_texture_bind(1, nrm ? nrm : detail_waves());
    rhi_set_i1(rhi_uniform(p,"u_albedo"), 2); rhi_texture_bind(2, alb ? alb : detail_waves());
    rhi_set_f1(rhi_uniform(p,"u_has_albedo"), alb ? 1.f : 0.f);
    rhi_set_f4(rhi_uniform(p,"u_geo"), LON0, LON1, LAT0, LAT1);
    rhi_set_f1(rhi_uniform(p,"u_k"), WORLD_K);
    rhi_set_f1(rhi_uniform(p,"u_time"), RF.time);
    rhi_set_f3(rhi_uniform(p,"u_campos"), RF.campos.x, RF.campos.y, RF.campos.z);
    rhi_set_f3(rhi_uniform(p,"u_sky"), RF.sky.x, RF.sky.y, RF.sky.z);
    rhi_set_f1(rhi_uniform(p,"u_mapness"), RF.mapness);
    rhi_set_f1(rhi_uniform(p,"u_daylight"), RF.daylight);
    rhi_set_f1(rhi_uniform(p,"u_wave_scale"), wave_scale);
    rhi_set_f1(rhi_uniform(p,"u_local_depth"), local_depth);
    rhi_set_f2(rhi_uniform(p,"u_clip"), sea_clip_z, sea_clip_on ? 1.f : 0.f);
    light_bind(p);
}

// how tall the waves may be right now: they flatten as the view turns into a map, and the
// detail grid only exists while the camera is close enough for it to matter
static float sea_amp_now(void) { return sea_wave_amp * sea_wind_amp * (1.f - RF.mapness); }

void sea_draw(void) {
    float amp = sea_amp_now();
    int detail = amp > 0.001f && RF.mapness < 0.995f;
    float cx = RF.sea_center.x, cz = RF.sea_center.z;
    rhi_pipeline_bind(&sea_prg);
    sea_bind_common(&sea_prg, 1.f, 0.f);
    rhi_set_f1(rhi_uniform(&sea_prg,"u_wave_amp"), 0.f);
    rhi_set_f4(rhi_uniform(&sea_prg,"u_hole"), cx, cz, detail ? SEA_DETAIL_R * 0.98f : 0.f, detail ? 1.f : 1.f);
    rhi_draw(&sea_geo, 0);
    if (!detail) return;
    rhi_pipeline_bind(&sea_detail_prg);
    sea_bind_common(&sea_detail_prg, 1.f, 0.f);
    rhi_set_f3(rhi_uniform(&sea_detail_prg,"u_center"), cx, 0, cz);
    rhi_set_f3(rhi_uniform(&sea_detail_prg,"u_detail"), SEA_DETAIL_R, SEA_WARP, sea_nyquist ? 2.f / SEA_DETAIL_N : 0.f);
    rhi_set_f1(rhi_uniform(&sea_detail_prg,"u_wave_amp"), amp);
    rhi_set_f4(rhi_uniform(&sea_detail_prg,"u_hole"), 0, 0, 0, 0);
    rhi_draw(&sea_detail_geo, 0);
}

// Staged scenes (detailview.c): the detail grid alone, centred on the camera, at a constant
// depth and with no land to discard. `scale` stretches the wave field to the scene's units.
// `clip_z` (when `clip_on`) is the shoreline: a staged scene has no heightmap, so the water's
// landward limit is given as a plane rather than discovered from the terrain.
void sea_draw_local(float depth, float scale, float radius) {
    rhi_pipeline_bind(&sea_detail_prg);
    sea_bind_common(&sea_detail_prg, scale, depth);
    rhi_set_f3(rhi_uniform(&sea_detail_prg,"u_center"), RF.sea_center.x, 0, RF.sea_center.z);
    rhi_set_f3(rhi_uniform(&sea_detail_prg,"u_detail"), radius, SEA_WARP, sea_nyquist ? 2.f / SEA_DETAIL_N : 0.f);
    rhi_set_f1(rhi_uniform(&sea_detail_prg,"u_wave_amp"), sea_wave_amp * sea_wind_amp);
    rhi_set_f4(rhi_uniform(&sea_detail_prg,"u_hole"), 0, 0, 0, 0);
    rhi_draw(&sea_detail_geo, 0);
}
