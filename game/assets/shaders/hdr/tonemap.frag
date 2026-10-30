in vec2 v_uv;
uniform sampler2D u_scene; uniform float u_exposure; uniform sampler2D u_ao; uniform float u_ao_mix; uniform vec2 u_ao_texel;
uniform sampler2D u_bloom; uniform float u_bloom_mix;
uniform sampler2D u_depth; uniform mat4 u_inv_vp; uniform vec3 u_campos; uniform vec3 u_sun; uniform vec3 u_suncol;
uniform vec4 u_vfog;
// x = density, y = scale height, z = sun in-scatter strength, w = max distance
uniform vec3 u_vfogcol;
out vec4 fragColor;
#include "common/color.glsl"
void main(){
  vec3 c = texture(u_scene, v_uv).rgb * u_exposure;
// --- volumetric fog. Density falls off exponentially with height, so the integral along the
// view ray has a closed form: no marching, and a valley fills while a hilltop stays clear.
// Looking towards the sun through it, the fog scatters his light back - haze glows, a storm
// does not. Driven by the weather, so clear air is left alone.
  if (u_vfog.x > 0.0001) {
    float z = texture(u_depth, v_uv).r;
    vec4 wp = u_inv_vp * vec4(v_uv * 2.0 - 1.0, z * 2.0 - 1.0, 1.0);
    vec3 P = wp.xyz / wp.w;
    vec3 V = P - u_campos; float L = min(length(V), u_vfog.w);
    vec3 dir = L > 1e-4 ? V / max(length(V), 1e-4) : vec3(0.0, 0.0, 1.0);
    float H = max(u_vfog.y, 1.0);
    float d0 = u_vfog.x * exp(-max(u_campos.y, 0.0) / H);
    float opt = abs(dir.y) > 1e-3 ? d0 * H / dir.y * (1.0 - exp(-dir.y * L / H)) : d0 * L;
    float f = 1.0 - exp(-max(opt, 0.0));
    float cs = max(dot(dir, u_sun), 0.0);
    vec3 fogc = u_vfogcol + u_suncol * pow(cs, 7.0) * u_vfog.z;
    c = mix(c, fogc, clamp(f, 0.0, 1.0));
  }
  if (u_bloom_mix > 0.001) c += texture(u_bloom, v_uv).rgb * u_bloom_mix;
// ambient occlusion (hdr_ao_pass, half resolution): four bilinear taps blur the noise
  if (u_ao_mix > 0.001) {
    float ao = (texture(u_ao, v_uv + u_ao_texel * vec2(-0.5, -0.5)).r + texture(u_ao, v_uv + u_ao_texel * vec2(0.5, -0.5)).r
              + texture(u_ao, v_uv + u_ao_texel * vec2(-0.5, 0.5)).r + texture(u_ao, v_uv + u_ao_texel * vec2(0.5, 0.5)).r) * 0.25;
    c *= mix(1.0, ao, u_ao_mix);
  }
// Reinhard-style shoulder on luminance: keeps midtones, rolls highlights off smoothly
  float l = dot(c, vec3(0.2126, 0.7152, 0.0722));
  const float W = 3.0;  float lt = l * (1.0 + l / (W * W)) / (1.0 + l);
  c *= l > 1e-5 ? lt / l : 1.0;
  fragColor = vec4(linear_to_srgb(c), 1.0);
// the only place linear becomes sRGB
}
