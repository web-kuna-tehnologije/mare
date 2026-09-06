// Screen-sized SDF icons placed at world positions (ports, fleets and storms in map view).

#include <stdlib.h>
#include "render.h"
#include "glsl.h"

static program_t icons_prg;
static unsigned icons_vao, icons_ibo; static int gpu_cap;
static icon_t *icons; static int icon_n, icon_cap;

static const char *icons_vs =
    "in vec2 a_corner; in vec4 i_pos; in vec4 i_misc; in vec4 i_col;\n"
    "uniform mat4 u_vp; uniform vec2 u_viewport;\n"
    "out vec2 v_uv; out vec4 v_col; out float v_shape; out float v_outline;\n"
    "void main(){\n"
    "  vec4 clip = u_vp * vec4(i_pos.xyz, 1.0);\n"
    "  float h = i_pos.w; float c = cos(h), s = sin(h);\n"
    "  vec2 cr = vec2(a_corner.x*c - a_corner.y*s, a_corner.x*s + a_corner.y*c);\n"
    "  clip.xy += cr * (i_misc.x / u_viewport) * 2.0 * clip.w;\n"
    "  gl_Position = clip; v_uv = a_corner; v_col = vec4(i_col.rgb, i_misc.z); v_shape = i_misc.y; v_outline = i_misc.w;\n"
    "}\n";
static const char *icons_fs =
    "in vec2 v_uv; in vec4 v_col; in float v_shape; in float v_outline;\n"
    "out vec4 fragColor;\n"
    "void main(){\n"
    "  vec2 p = v_uv; float d; int sh = int(v_shape + 0.5);\n"
    "  if (sh == 0) d = length(p) - 0.8;\n"
    "  else if (sh == 1) d = abs(length(p) - 0.7) - 0.14;\n"
    "  else if (sh == 2) { vec2 q = vec2(abs(p.y), p.x); d = max(q.x*0.9 + q.y*0.5 - 0.45, -q.y - 0.5); }\n"
    "  else if (sh == 3) d = abs(p.x) + abs(p.y) - 0.8;\n"
    "  else if (sh == 4) d = max(abs(p.x), abs(p.y)) - 0.7;\n"
    "  else if (sh == 5) { float a = atan(p.y, p.x); d = abs(length(p) - 0.6 - 0.15*sin(a*3.0)) - 0.12; }\n"
    "  else { d = max(abs(p.x) - 0.12, abs(p.y) - 0.8); d = min(d, max(abs(p.x)-0.7, abs(p.y)-0.12)); }\n"
    "  float aa = 0.08;\n"
    "  float inside = 1.0 - smoothstep(-aa, aa, d);\n"
    "  float outline = 1.0 - smoothstep(0.10, 0.10+aa, abs(d));\n"
    "  vec3 col = mix(v_col.rgb, vec3(0.05), outline * v_outline);\n"
    "  float alpha = max(inside, outline*v_outline) * v_col.a;\n"
    "  if (alpha < 0.02) discard;\n"
    "  fragColor = vec4(col, alpha);\n"
    "}\n";

void icons_init(void) {
    static const char *attribs[] = { "a_corner", "i_pos", "i_misc", "i_col", 0 };
    static const char *uniforms[] = { "u_vp", "u_viewport", 0 };
    icons_prg = gpu_program(icons_vs, icons_fs, attribs, uniforms);
    float corners[] = { -1,-1, 1,-1, 1,1, -1,-1, 1,1, -1,1 };
    icons_vao = vao_begin();
    vbo_buffer(0, "2f", corners, sizeof corners);
    glGenBuffers(1, &icons_ibo); glBindBuffer(GL_ARRAY_BUFFER, icons_ibo);
    gpu_cap = 1024; glBufferData(GL_ARRAY_BUFFER, gpu_cap*sizeof(icon_t), 0, GL_STREAM_DRAW);
    for (int a = 0; a < 3; ++a) { glEnableVertexAttribArray(1+a); glVertexAttribPointer(1+a, 4, GL_FLOAT, 0, sizeof(icon_t), (void*)(size_t)(a*4*sizeof(float))); glVertexAttribDivisor(1+a, 1); }
    vao_end();
}

void icons_begin(void) { icon_n = 0; }
icon_t *icon_push(void) {
    if (icon_n >= icon_cap) { icon_cap = icon_cap ? icon_cap*2 : 1024; icons = realloc(icons, icon_cap*sizeof(icon_t)); }
    return &icons[icon_n++];
}
void icons_draw(void) {
    if (icon_n <= 0) return;
    glUseProgram(icons_prg.id);
    glUniformMatrix4fv(UNI(icons_prg,"u_vp"), 1, GL_FALSE, &RF.vp.x.x);
    glUniform2f(UNI(icons_prg,"u_viewport"), screen_w, screen_h);
    glBindVertexArray(icons_vao);
    glBindBuffer(GL_ARRAY_BUFFER, icons_ibo);
    if (icon_n > gpu_cap) { gpu_cap = icon_n*2; glBufferData(GL_ARRAY_BUFFER, gpu_cap*sizeof(icon_t), 0, GL_STREAM_DRAW); }
    glBufferSubData(GL_ARRAY_BUFFER, 0, icon_n*sizeof(icon_t), icons);
    glDisable(GL_DEPTH_TEST); glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDrawArraysInstanced(GL_TRIANGLES, 0, 6, icon_n);
    glDisable(GL_BLEND); glEnable(GL_DEPTH_TEST);
    glBindVertexArray(0);
}

// umbrella
void terrain_init(void); void sea_init(void); void meshes_init(void);
void render_init(void) { terrain_init(); sea_init(); meshes_init(); icons_init(); }
