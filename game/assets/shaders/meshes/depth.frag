in float v_dither;
out vec4 fragColor;
#include "common/dither.glsl"
void main(){ dither_clip(v_dither); fragColor = vec4(1.0); }
