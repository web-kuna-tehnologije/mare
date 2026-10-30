// mare::engine::render::Icons (see render.h): screen-sized SDF icons placed at world positions
// (ports, fleets and storms in map view). Also holds render_init, the umbrella that builds every
// render module in order.

#include "../platform/v3cpp.hh"
#include <cstdlib>

extern "C" {
#include "render.h"
#include "glsl.h"
#include "../engine/shaders.h"
}

namespace mare::engine::render {

class Icons {
public:
    void init();
    void begin();
    icon_t *push();
    void draw();

private:
    rhi_pipeline_t icons_prg;
    rhi_geometry_t icons_geo;
    icon_t *icons = nullptr; int icon_n = 0, icon_cap = 0;
};

Icons &icons() { static Icons i; return i; }


void Icons::init() {
    static const rhi_attr_t attrs[] = {
        { "a_corner", 0, 2, 0, 0 },
        { "i_pos", 1, 4, 0, 1 }, { "i_misc", 1, 4, 16, 1 }, { "i_col", 1, 4, 32, 1 },
    };
    static const rhi_layout_t layout = { attrs, 4, { 2*sizeof(float), sizeof(icon_t) } };
    static const char *uniforms[] = { "u_vp", "u_viewport", 0 };
    const rhi_pipeline_desc_t desc{ .vs = shader_source("icons/icons.vert"), .fs = shader_source("icons/icons.frag"), .layout = &layout, .uniforms = uniforms, .blend = RHI_BLEND_ALPHA };   // (C compound literal -> named: C++ cannot take &T{...})
    icons_prg = rhi_pipeline_make(&desc);
    float corners[] = { -1,-1, 1,-1, 1,1, -1,-1, 1,1, -1,1 };
    icons_geo = rhi_geometry_make(&layout, corners, 6, 0, 0);
}

void Icons::begin() { icon_n = 0; }
icon_t *Icons::push() {
    if (icon_n >= icon_cap) { icon_cap = icon_cap ? icon_cap*2 : 1024; icons = (icon_t *)realloc(icons, icon_cap*sizeof(icon_t)); }
    return &icons[icon_n++];
}
void Icons::draw() {
    if (icon_n <= 0) return;
    rhi_pipeline_bind(&icons_prg);
    rhi_set_mat4(UNI(icons_prg,"u_vp"), &RF.vp.x.x);
    rhi_set_f2(UNI(icons_prg,"u_viewport"), screen_w, screen_h);
    rhi_geometry_instances(&icons_geo, icons, icon_n);
    rhi_draw(&icons_geo, icon_n);
}

} // namespace mare::engine::render

// C ABI (render.h) for the unported callers
extern "C" {
void icons_init(void) { mare::engine::render::icons().init(); }
void icons_begin(void) { mare::engine::render::icons().begin(); }
icon_t *icon_push(void) { return mare::engine::render::icons().push(); }
void icons_draw(void) { mare::engine::render::icons().draw(); }

// umbrella
void terrain_init(void); void sea_init(void); void meshes_init(void); void hdr_init(void);
void render_init(void) { rhi_init(); detail_init(); terrain_init(); sea_init(); meshes_init(); icons_init(); hdr_init(); }
}
