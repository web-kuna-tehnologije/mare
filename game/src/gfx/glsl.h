// Shared GLSL conventions. The shader sources themselves live in game/assets/shaders/**
// (engine/shaders.h loads them, resolving #include lines); what remains here is what the C
// side needs at compile time: the version header and the uniform-name lists.
//
// v3 creates a compatibility context on Windows/Linux and a 3.2 core context on macOS, so
// shaders avoid layout qualifiers and bind attributes by name.
//
// Colour pipeline: author colours (vertex colours, nation colours, palettes) are sRGB. Every
// shader converts them to linear with srgb_to_linear() (common/color.glsl) before any lighting
// math, writes linear radiance into the RGBA16F HDR target, and hdr.c encodes to sRGB after
// tonemapping.

#ifndef MARE_GLSL_H
#define MARE_GLSL_H

#ifdef __cplusplus
extern "C" {
#endif

#define GLSL_VERSION ifdef(V3_MACOS, "#version 150\n", "#version 130\n")

// the wave-set size shared between sea.c and common/waves.glsl (u_wave[SEA_WAVE_SLOTS])
#define SEA_WAVES 5
#define SEA_WAVE_SLOTS 10

// uniform names every pipeline that includes common/waves.glsl must pre-resolve
#define GLSL_WAVE_UNIFORMS "u_wave", "u_wave_phase", "u_wave_blend"

// uniform names declared by common/light.glsl, to append to a pipeline's uniform list
#define GLSL_LIGHT_UNIFORMS \
    "u_sun", "u_suncol", "u_ambsky", "u_ambgnd", "u_shadowmap", \
    "u_shadowvp0", "u_shadowvp1", "u_shadowvp2", "u_shadowsplit", "u_shadow_on", "u_shadow_texel", "u_shadow_wtexel", "u_shadow_debug", "u_fog", "u_cloud"

#ifdef __cplusplus
}
#endif

#endif
