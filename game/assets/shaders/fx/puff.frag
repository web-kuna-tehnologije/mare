// Particle shading (gfx/particles.cc, 0.22.7). Three kinds share the pipeline:
//   0 smoke  - a disc eroded by a 3D value noise that scrolls with age, lit as a sphere (sun side
//              bright, lee side in the ambient), a back-lit rim where the sun shines through
//              the thin edge, faded where it meets the opaque scene (soft particles)
//   1 flash  - the muzzle flash: a hot core and a halo, bright enough to bloom
//   2 ember  - a burning point
in vec2 v_uv; in vec3 v_col; in vec4 v_misc; in vec3 v_wpos; in float v_size; in float v_soft;
uniform vec3 u_campos; uniform vec3 u_sky; uniform float u_mapness; uniform float u_time;
uniform vec3 u_cam_right; uniform vec3 u_cam_up;
uniform sampler2D u_scene_depth; uniform vec2 u_zrange; uniform vec2 u_scene_size; uniform float u_soft_on;
out vec4 fragColor;
#include "common/color.glsl"
#include "common/pbr.glsl"
#include "common/light.glsl"
#include "common/fog.glsl"
float h3(vec3 p) { return fract(sin(dot(p, vec3(127.1, 311.7, 74.7))) * 43758.5453); }
float vnoise3(vec3 p) {
  vec3 i = floor(p), f = fract(p); f = f * f * (3.0 - 2.0 * f);
  float a = mix(mix(h3(i), h3(i + vec3(1,0,0)), f.x), mix(h3(i + vec3(0,1,0)), h3(i + vec3(1,1,0)), f.x), f.y);
  float b = mix(mix(h3(i + vec3(0,0,1)), h3(i + vec3(1,0,1)), f.x), mix(h3(i + vec3(0,1,1)), h3(i + vec3(1,1,1)), f.x), f.y);
  return mix(a, b, f.z);
}
float fbm3(vec3 p) { float s = 0.0, a = 0.5; for (int i = 0; i < 4; ++i) { s += a * vnoise3(p); p = p * 2.07 + vec3(1.3, 2.1, 0.7); a *= 0.5; } return s; }
float lin_depth(float zw, vec2 nf) { float ndc = zw * 2.0 - 1.0; return 2.0 * nf.x * nf.y / (nf.y + nf.x - ndc * (nf.y - nf.x)); }
void main(){
  float r = length(v_uv);
  if (r > 1.0) discard;
  float alpha = v_misc.x, seed = v_misc.y, kind = v_misc.z, age = v_misc.w;
  float dist = distance(v_wpos, u_campos);
  vec3 skyl = srgb_to_linear(u_sky);
  if (kind > 1.5) {                                   // ember
    float a = smoothstep(1.0, 0.15, r) * alpha;
    fragColor = vec4(srgb_to_linear(v_col) * 6.0, a); return;
  }
  if (kind > 0.5) {                                   // muzzle flash
    float n = vnoise3(vec3(v_uv * 3.0, seed * 9.0));
    float core = exp(-r * r * 7.0) * (0.8 + 0.4 * n), halo = exp(-r * r * 1.8) * 0.35;
    vec3 c = srgb_to_linear(v_col) * (core * 9.0 + halo * 2.5);
    fragColor = vec4(fog_apply(c, dist, u_mapness, skyl), clamp((core + halo) * alpha, 0.0, 1.0)); return;
  }
  // smoke. The noise is sampled in the billboard plane plus the seed as a third axis, and it
  // scrolls upward with age so the puff churns instead of sliding as a stamp.
  vec3 np = vec3(v_uv * 1.7, seed * 11.0 + age * 0.9) + vec3(0.0, age * 0.6, 0.0);
  float n = fbm3(np * 1.35);
  float disc = smoothstep(1.0, 0.05, r) * (1.0 - 0.35 * r);
  float thr = 0.30 + 0.45 * age;                      // erosion: ragged from birth, dissolving as the puff ages
  float dens = clamp((disc * (0.30 + 1.35 * n) - thr) * 2.2, 0.0, 1.0);
  if (dens <= 0.002) discard;
  // sphere normal in world space: the sun side is lit, the lee side falls to the ambient
  float nz = sqrt(max(1.0 - r * r, 0.0));
  vec3 tocam = normalize(u_campos - v_wpos);
  vec3 N = normalize(u_cam_right * v_uv.x + u_cam_up * v_uv.y + tocam * nz);
  float wrap = dot(N, u_sun) * 0.5 + 0.5;
  float sh = shadow_at(v_wpos, N, max(dot(N, u_sun), 0.0), dist);
  vec3 albedo = srgb_to_linear(v_col);
  vec3 c = albedo * (u_ambsky * 0.85 + u_ambgnd * 0.35 + u_suncol * 0.65 * wrap * wrap * (0.4 + 0.6 * sh) * g_sun_occ);
  // back-lit rim: the sun through the thin edge of the cloud
  float back = pow(max(dot(-tocam, u_sun), 0.0), 6.0);
  c += u_suncol * albedo * back * (1.0 - dens) * 0.5 * smoothstep(0.4, 1.0, r);
  // self-shadow of the thick core: the noise's dark pockets read as depth
  c *= 0.58 + 0.42 * smoothstep(0.0, 1.0, n * 1.4);
  // soft particles: fade where the disc cuts through a hull or the shore
  float soft = 1.0;
  if (u_soft_on > 0.5) {
    float sd = texture(u_scene_depth, gl_FragCoord.xy / u_scene_size).r;
    float thick = lin_depth(sd, u_zrange) - lin_depth(gl_FragCoord.z, u_zrange);
    soft = clamp(thick / max(0.6 * v_soft, 0.05), 0.0, 1.0);
  }
  c = fog_apply(c, dist, u_mapness, skyl);
  fragColor = vec4(c, dens * alpha * soft);
}
