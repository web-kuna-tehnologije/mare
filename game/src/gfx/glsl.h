// Shared GLSL bits. v3 creates a compatibility context on Windows/Linux and a 3.2 core
// context on macOS, so shaders avoid layout qualifiers and bind attributes by name.

#ifndef MARE_GLSL_H
#define MARE_GLSL_H

#define GLSL_VERSION ifdef(V3_MACOS, "#version 150\n", "#version 130\n")

#define GLSL_FOG \
    "vec3 fog_apply(vec3 c, float dist, float mapness, vec3 sky) {\n" \
    "  float f = 1.0 - exp(-max(dist-250.0,0.0)*0.0016);\n" \
    "  return mix(c, sky, f*(1.0-mapness)*0.85);\n" \
    "}\n"

#endif
