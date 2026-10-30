// C ABI over mare::engine::ShaderLibrary (shader_library.hh): GLSL sources loaded from
// game/assets/shaders/** with #include support. The returned pointer is valid for the whole
// run; a missing file is fatal with the path in the message.

#ifndef MARE_ENGINE_SHADERS_H
#define MARE_ENGINE_SHADERS_H

#ifdef __cplusplus
extern "C" {
#endif

const char *shader_source(const char *name);   // e.g. shader_source("sea/sea.frag")

#ifdef __cplusplus
}
#endif

#endif
