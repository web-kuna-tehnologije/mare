// Screen-sized SDF icons placed at world positions (ports, fleets and storms in map view).

#include <stdlib.h>
#include "render.h"
#include "glsl.h"
#include "../engine/shaders.h"

static rhi_pipeline_t icons_prg;
static rhi_geometry_t icons_geo;
static icon_t *icons; static int icon_n, icon_cap;


void icons_init(void) {
    static const rhi_attr_t attrs[] = {
        { "a_corner", 0, 2, 0, 0 },
        { "i_pos", 1, 4, 0, 1 }, { "i_misc", 1, 4, 16, 1 }, { "i_col", 1, 4, 32, 1 },
    };
    static const rhi_layout_t layout = { attrs, 4, { 2*sizeof(float), sizeof(icon_t) } };
    static const char *uniforms[] = { "u_vp", "u_viewport", 0 };
    icons_prg = rhi_pipeline_make(&(rhi_pipeline_desc_t){ .vs = shader_source("icons/icons.vert"), .fs = shader_source("icons/icons.frag"), .layout = &layout, .uniforms = uniforms, .blend = RHI_BLEND_ALPHA });
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
void render_init(void) { rhi_init(); detail_init(); terrain_init(); sea_init(); meshes_init(); icons_init(); hdr_init(); }
