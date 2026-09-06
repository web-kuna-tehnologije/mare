// Sea: one mesh at y=0 shaded from the heightmap (depth colour, foam, distance-faded waves);
// parchment blue with a graticule in map view.

#include <stdlib.h>
#include "render.h"
#include "glsl.h"
#include "../core/types.h"
#include "../core/util.h"

unsigned terrain_heightmap_texture(void);

static program_t sea_prg;
static unsigned sea_vao, sea_ntris;

static const char *sea_vs =
    "in vec2 a_pos;\n"
    "uniform mat4 u_vp;\n"
    "out vec3 v_wpos;\n"
    "void main(){ v_wpos = vec3(a_pos.x, 0.0, a_pos.y); gl_Position = u_vp * vec4(v_wpos, 1.0); }\n";

static const char *sea_fs =
    "in vec3 v_wpos;\n"
    "uniform sampler2D u_hm; uniform vec4 u_geo; uniform float u_k; uniform float u_time;\n"
    "uniform vec3 u_campos; uniform vec3 u_sun; uniform float u_mapness; uniform vec3 u_sky; uniform float u_daylight;\n"
    "out vec4 fragColor;\n"
    GLSL_FOG
    "void main(){\n"
    "  float lon = v_wpos.x / u_k, lat = -v_wpos.z / u_k;\n"
    "  vec2 uv = vec2((lon - u_geo.x)/(u_geo.y-u_geo.x), (u_geo.w - lat)/(u_geo.w-u_geo.z));\n"
    "  float h = texture(u_hm, uv).r;\n"
    "  if (h > 0.35) discard;\n"
    "  float depth = max(-h, 0.0);\n"
    "  float dist = distance(v_wpos, u_campos);\n"
    "  float detail = 1.0 - smoothstep(60.0, 400.0, dist);\n"
    "  vec2 p = v_wpos.xz; float t = u_time;\n"
    "  float w1 = sin(p.x*3.1 + p.y*1.3 + t*1.7), w2 = sin(p.x*-1.7 + p.y*2.9 + t*1.3), w3 = sin(p.x*6.3 + p.y*-4.1 + t*2.9);\n"
    "  vec3 n = normalize(vec3((w1*0.6+w3*0.2)*0.09*detail, 1.0, (w2*0.6+w3*0.2)*0.09*detail));\n"
    "  vec3 deep = vec3(0.02,0.10,0.24), shallow = vec3(0.12,0.46,0.52);\n"
    "  vec3 c = mix(shallow, deep, 1.0 - exp(-depth*0.9));\n"
    "  vec3 vdir = normalize(u_campos - v_wpos);\n"
    "  float spec = pow(max(dot(reflect(-u_sun, n), vdir), 0.0), 90.0) * 0.7 * detail;\n"
    "  float diff = 0.55 + 0.45*max(dot(n, u_sun), 0.0);\n"
    "  vec3 col3d = c * diff + spec * vec3(1.0,0.95,0.8);\n"
    "  float foam = (1.0 - smoothstep(0.0, 0.45, depth)) * (0.55 + 0.45*sin(depth*30.0 - t*2.0 + w1)) * detail;\n"
    "  col3d = mix(col3d, vec3(0.85,0.9,0.92), foam*0.7) * u_daylight;\n"
    "  vec3 mapc = mix(vec3(0.62,0.72,0.80), vec3(0.50,0.62,0.74), 1.0 - exp(-depth*0.8));\n"
    "  float gx = abs(fract(lon/10.0 + 0.5) - 0.5) * 10.0, gy = abs(fract(lat/10.0 + 0.5) - 0.5) * 10.0;\n"
    "  float grid = 1.0 - smoothstep(0.0, 0.06, min(gx, gy));\n"
    "  float gx5 = abs(fract(lon/5.0 + 0.5) - 0.5) * 5.0, gy5 = abs(fract(lat/5.0 + 0.5) - 0.5) * 5.0;\n"
    "  float grid5 = (1.0 - smoothstep(0.0, 0.035, min(gx5, gy5))) * 0.5;\n"
    "  mapc = mix(mapc, vec3(0.35,0.42,0.52), max(grid, grid5) * 0.8);\n"
    "  mapc = mix(mapc, vec3(0.30,0.40,0.52), (1.0 - smoothstep(0.0, 0.6, depth))*0.55);\n"
    "  vec3 col = fog_apply(mix(col3d, mapc, u_mapness), dist, u_mapness, u_sky);\n"
    "  float alpha = mix(1.0, 0.75, (1.0 - smoothstep(0.0, 0.5, depth)) * (1.0-u_mapness));\n"
    "  fragColor = vec4(col, alpha);\n"
    "}\n";

void sea_init(void) {
    static const char *attribs[] = { "a_pos", 0 };
    static const char *uniforms[] = { "u_vp", "u_hm", "u_geo", "u_k", "u_time", "u_campos", "u_sun", "u_mapness", "u_sky", "u_daylight", 0 };
    sea_prg = gpu_program(sea_vs, sea_fs, attribs, uniforms);
    int nx = 88, ny = 65;
    float *v = malloc((nx+1)*(ny+1)*2*sizeof(float));
    float x0 = lon2x(LON0), x1 = lon2x(LON1), z0 = lat2z(LAT1), z1 = lat2z(LAT0);
    for (int y = 0; y <= ny; ++y) for (int x = 0; x <= nx; ++x) { v[(y*(nx+1)+x)*2] = lerpf(x0,x1,x/(float)nx); v[(y*(nx+1)+x)*2+1] = lerpf(z0,z1,y/(float)ny); }
    unsigned *idx = malloc(nx*ny*6*sizeof(unsigned)); int m = 0;
    for (int y = 0; y < ny; ++y) for (int x = 0; x < nx; ++x) { unsigned a = y*(nx+1)+x, b = a+1, c = a+nx+1, d = c+1; idx[m++]=a; idx[m++]=c; idx[m++]=b; idx[m++]=b; idx[m++]=c; idx[m++]=d; }
    sea_vao = vao_begin(); vbo_buffer(0, "2f", v, (nx+1)*(ny+1)*2*sizeof(float)); ebo_buffer("", idx, m*sizeof(unsigned)); vao_end();
    sea_ntris = m/3; free(v); free(idx);
}

void sea_draw(void) {
    glUseProgram(sea_prg.id);
    glUniformMatrix4fv(UNI(sea_prg,"u_vp"), 1, GL_FALSE, &RF.vp.x.x);
    glUniform1i(UNI(sea_prg,"u_hm"), 0); glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, terrain_heightmap_texture());
    glUniform4f(UNI(sea_prg,"u_geo"), LON0, LON1, LAT0, LAT1);
    glUniform1f(UNI(sea_prg,"u_k"), WORLD_K);
    glUniform1f(UNI(sea_prg,"u_time"), RF.time);
    glUniform3f(UNI(sea_prg,"u_campos"), RF.campos.x, RF.campos.y, RF.campos.z);
    glUniform3f(UNI(sea_prg,"u_sun"), RF.sun.x, RF.sun.y, RF.sun.z);
    glUniform3f(UNI(sea_prg,"u_sky"), RF.sky.x, RF.sky.y, RF.sky.z);
    glUniform1f(UNI(sea_prg,"u_mapness"), RF.mapness);
    glUniform1f(UNI(sea_prg,"u_daylight"), RF.daylight);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBindVertexArray(sea_vao);
    glDrawElements(GL_TRIANGLES, sea_ntris*3, GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);
    glDisable(GL_BLEND);
}
