// Sea: one mesh at y=0 shaded from the heightmap (depth colour, foam, distance-faded waves);
// parchment blue with a graticule in map view.

#include <stdlib.h>
#include "render.h"
#include "glsl.h"
#include "../core/types.h"
#include "../core/util.h"

const rhi_texture_t *terrain_heightmap_texture(void);

static rhi_pipeline_t sea_prg;
static rhi_geometry_t sea_geo;

static const char *sea_vs =
    "in vec2 a_pos;\n"
    "uniform mat4 u_vp;\n"
    "out vec3 v_wpos;\n"
    "void main(){ v_wpos = vec3(a_pos.x, 0.0, a_pos.y); gl_Position = u_vp * vec4(v_wpos, 1.0); }\n";

static const char *sea_fs =
    "in vec3 v_wpos;\n"
    "uniform sampler2D u_hm; uniform sampler2D u_waves; uniform vec4 u_geo; uniform float u_k; uniform float u_time;\n"
    "uniform vec3 u_campos; uniform float u_mapness; uniform vec3 u_sky; uniform float u_daylight;\n"
    "out vec4 fragColor;\n"
    GLSL_COLOR GLSL_PBR GLSL_LIGHT GLSL_NMAP GLSL_FOG
    "void main(){\n"
    "  float lon = v_wpos.x / u_k, lat = -v_wpos.z / u_k;\n"
    "  vec2 uv = vec2((lon - u_geo.x)/(u_geo.y-u_geo.x), (u_geo.w - lat)/(u_geo.w-u_geo.z));\n"
    "  float h = texture(u_hm, uv).r;\n"
    "  if (h > 0.35) discard;\n"
    "  float depth = max(-h, 0.0);\n"
    "  float dist = distance(v_wpos, u_campos);\n"
    "  float detail = 1.0 - smoothstep(60.0, 400.0, dist);\n"
    "  vec2 p = v_wpos.xz; float t = u_time;\n"
    // two scrolling samples of the tileable wave normal map, large swell + fine chop; the
    // tangent frame of a flat y-up plane is trivial (T=+x, B=+z)
    "  vec3 n1 = nmap_decode(texture(u_waves, p * 0.055 + vec2(t*0.009, t*0.013)).rgb);\n"
    "  vec3 n2 = nmap_decode(texture(u_waves, p * 0.21 - vec2(t*0.022, t*0.016)).rgb);\n"
    "  vec2 slope = (n1.xy / max(n1.z, 0.3) + n2.xy / max(n2.z, 0.3) * 0.6) * 0.42 * detail;\n"
    "  vec3 n = normalize(vec3(slope.x, 1.0, slope.y));\n"
    "  float w1 = sin(p.x*3.1 + p.y*1.3 + t*1.7);\n"   // keeps the foam band animated
    "  vec3 deep = vec3(0.02,0.10,0.24), shallow = vec3(0.12,0.46,0.52);\n"
    "  vec3 c = srgb_to_linear(mix(shallow, deep, 1.0 - exp(-depth*0.9)));\n"
    "  vec3 vdir = normalize(u_campos - v_wpos);\n"
    // water: smooth dielectric, so the sun glint comes from the GGX lobe and the sky from Fresnel
    "  float rough = mix(0.30, 0.13, detail);\n"
    "  vec3 col3d = sun_shade(c, n, vdir, v_wpos, rough, 0.0, dist);\n"
    "  float fres = 0.02 + 0.98 * pow(1.0 - max(dot(n, vdir), 0.0), 5.0);\n"
    "  col3d += srgb_to_linear(u_sky) * u_ambsky * fres * 1.4;\n"
    "  float foam = (1.0 - smoothstep(0.0, 0.45, depth)) * (0.55 + 0.45*sin(depth*30.0 - t*2.0 + w1)) * detail;\n"
    "  col3d = mix(col3d, srgb_to_linear(vec3(0.85,0.9,0.92)) * (u_ambsky + u_suncol * 0.35), foam*0.7);\n"
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
    "  float alpha = mix(0.42, 1.0, smoothstep(0.0, 1.6, depth));\n"
    "  alpha = max(alpha, foam * 0.85);\n"           // surf stays visible over the seabed
    "  alpha = mix(alpha, 1.0, u_mapness);\n"
    "  fragColor = vec4(col, alpha);\n"
    "}\n";

void sea_init(void) {
    static const rhi_attr_t attrs[] = { { "a_pos", 0, 2, 0, 0 } };
    static const rhi_layout_t layout = { attrs, 1, { 2*sizeof(float), 0 } };
    static const char *uniforms[] = { "u_vp", "u_hm", "u_waves", "u_geo", "u_k", "u_time", "u_campos", "u_mapness", "u_sky", "u_daylight", GLSL_LIGHT_UNIFORMS, 0 };
    sea_prg = rhi_pipeline_make(&(rhi_pipeline_desc_t){ .vs = sea_vs, .fs = sea_fs, .layout = &layout, .uniforms = uniforms, .depth_test = 1, .depth_write = 1, .blend = RHI_BLEND_ALPHA });
    int nx = 88, ny = 65;
    float *v = malloc((nx+1)*(ny+1)*2*sizeof(float));
    float x0 = lon2x(LON0), x1 = lon2x(LON1), z0 = lat2z(LAT1), z1 = lat2z(LAT0);
    for (int y = 0; y <= ny; ++y) for (int x = 0; x <= nx; ++x) { v[(y*(nx+1)+x)*2] = lerpf(x0,x1,x/(float)nx); v[(y*(nx+1)+x)*2+1] = lerpf(z0,z1,y/(float)ny); }
    unsigned *idx = malloc(nx*ny*6*sizeof(unsigned)); int m = 0;
    for (int y = 0; y < ny; ++y) for (int x = 0; x < nx; ++x) { unsigned a = y*(nx+1)+x, b = a+1, c = a+nx+1, d = c+1; idx[m++]=a; idx[m++]=c; idx[m++]=b; idx[m++]=b; idx[m++]=c; idx[m++]=d; }
    sea_geo = rhi_geometry_make(&layout, v, (nx+1)*(ny+1), idx, m);
    free(v); free(idx);
}

void sea_draw(void) {
    rhi_pipeline_bind(&sea_prg);
    rhi_set_mat4(UNI(sea_prg,"u_vp"), &RF.vp.x.x);
    rhi_set_i1(UNI(sea_prg,"u_hm"), 0); rhi_texture_bind(0, terrain_heightmap_texture());
    rhi_set_i1(UNI(sea_prg,"u_waves"), 1); rhi_texture_bind(1, detail_waves());
    rhi_set_f4(UNI(sea_prg,"u_geo"), LON0, LON1, LAT0, LAT1);
    rhi_set_f1(UNI(sea_prg,"u_k"), WORLD_K);
    rhi_set_f1(UNI(sea_prg,"u_time"), RF.time);
    rhi_set_f3(UNI(sea_prg,"u_campos"), RF.campos.x, RF.campos.y, RF.campos.z);
    rhi_set_f3(UNI(sea_prg,"u_sky"), RF.sky.x, RF.sky.y, RF.sky.z);
    light_bind(&sea_prg);
    rhi_set_f1(UNI(sea_prg,"u_mapness"), RF.mapness);
    rhi_set_f1(UNI(sea_prg,"u_daylight"), RF.daylight);
    rhi_draw(&sea_geo, 0);
}
