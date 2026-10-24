// Screen-sized SDF icons placed at world positions (ports, fleets and storms in map view).

#include <stdlib.h>
#include "render.h"
#include "glsl.h"

static rhi_pipeline_t icons_prg;
static rhi_geometry_t icons_geo;
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
    GLSL_COLOR
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
    "  fragColor = vec4(srgb_to_linear(col), alpha);\n"   // icons are authored in sRGB; the HDR target is linear

    "}\n";

void icons_init(void) {
    static const rhi_attr_t attrs[] = {
        { "a_corner", 0, 2, 0, 0 },
        { "i_pos", 1, 4, 0, 1 }, { "i_misc", 1, 4, 16, 1 }, { "i_col", 1, 4, 32, 1 },
    };
    static const rhi_layout_t layout = { attrs, 4, { 2*sizeof(float), sizeof(icon_t) } };
    static const char *uniforms[] = { "u_vp", "u_viewport", 0 };
    icons_prg = rhi_pipeline_make(&(rhi_pipeline_desc_t){ .vs = icons_vs, .fs = icons_fs, .layout = &layout, .uniforms = uniforms, .blend = RHI_BLEND_ALPHA });
    float corners[] = { -1,-1, 1,-1, 1,1, -1,-1, 1,1, -1,1 };
    icons_geo = rhi_geometry_make(&layout, corners, 6, 0, 0);
}

void icons_begin(void) { icon_n = 0; }
icon_t *icon_push(void) {
    if (icon_n >= icon_cap) { icon_cap = icon_cap ? icon_cap*2 : 1024; icons = realloc(icons, icon_cap*sizeof(icon_t)); }
    return &icons[icon_n++];
}
void icons_draw(void) {
    if (icon_n <= 0) return;
    rhi_pipeline_bind(&icons_prg);
    rhi_set_mat4(UNI(icons_prg,"u_vp"), &RF.vp.x.x);
    rhi_set_f2(UNI(icons_prg,"u_viewport"), screen_w, screen_h);
    rhi_geometry_instances(&icons_geo, icons, icon_n);
    rhi_draw(&icons_geo, icon_n);
}

// umbrella
void terrain_init(void); void sea_init(void); void meshes_init(void); void hdr_init(void);
void render_init(void) { rhi_init(); terrain_init(); sea_init(); meshes_init(); icons_init(); hdr_init(); }
