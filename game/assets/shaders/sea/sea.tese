layout(quads, fractional_odd_spacing, ccw) in;
// no derivatives in an evaluation stage: every lookup is an explicit LOD 0 (an implicit-LOD 
// texture() here is where the Intel driver crashed at link time)
#define texture(s, uv) textureLod(s, uv, 0.0)
in vec2 tc_pos[];
uniform vec3 u_center; uniform vec3 u_detail;
// x = region radius, y = 1 for the inner lattice (no rim fade, seam blend), z = the outer lattice's patch size
uniform vec2 u_tess; uniform float u_patch;
uniform sampler2D u_disp; uniform float u_has_disp; uniform vec2 u_drift1; uniform vec2 u_drift2; uniform vec3 u_campos;
#include "sea/vs_common.glsl"
#include "common/waves.glsl"
void main(){
  float u = gl_TessCoord.x, v = gl_TessCoord.y;
  vec2 p = mix(mix(tc_pos[0], tc_pos[1], u), mix(tc_pos[3], tc_pos[2], u), v);
  float h = height_at(p); float dep = max(-h, 0.0);
  float r = distance(p, u_center.xz);
  float edge = u_detail.y > 0.5 ? 1.0 : 1.0 - smoothstep(0.78 * u_detail.x, 0.97 * u_detail.x, r);
// How big is a cell around here? This drives the Nyquist fade that drops waves and 
// displacement a cell cannot carry, so it has to be *continuous*: taking it from the 
// patch's own tessellation level made it jump at every patch edge, and the sea came out 
// ruled into 12-unit rectangles wherever the level changed. The same formula the control 
// stage uses, evaluated at this vertex, is continuous across the lattice.
  float dcam = max(distance(vec3(p.x, 0.0, p.y), u_campos), 1.0);
  float cell = u_patch / clamp(u_tess.x * u_patch / dcam, 1.0, u_tess.y);
  if (u_detail.y > 0.5) {
// inner lattice: towards its rim, pretend to be as coarse as the outer lattice is there
    float ocell = u_detail.z / clamp(u_tess.x * u_detail.z / dcam, 1.0, u_tess.y);
    cell = mix(cell, max(ocell, cell), smoothstep(0.55 * u_detail.x, 0.95 * u_detail.x, r));
  }
  float shal = smoothstep(0.0, u_shallow * u_wave_scale, dep);
  float amp = u_wave_amp * edge * shal * shoal_boost(dep, u_wave_scale);
  v_amp = amp; v_edge = edge;
  vec3 tx, tz; float crest;
  vec3 d = wave_sum(p, amp, u_wave_scale, cell, tx, tz, crest);
// whitecaps read the big waves, not the chop: slope, Jacobian and crest come from the 
// Gerstner surface before the displacement map is folded into the tangents
  v_slope = 1.0 - normalize(cross(tz, tx)).y;
  v_jac = tx.x * tz.z - tx.z * tz.x; v_crest = crest;
// authored displacement: fine chop on top of the wave sum, two scales drifting downwind, 
// faded where a cell is too big to carry it (the same rule the waves obey)
  if (u_has_disp > 0.5 && amp > 0.0) {
    vec2 q = p / u_wave_scale;
    float fine = smoothstep(2.4 * cell, 5.0 * cell, 1.1 * u_wave_scale);
    float dm = (texture(u_disp, q * 0.19 + u_drift1 * 0.8).r - 0.5) * 0.55 + (texture(u_disp, q * 0.90 + u_drift2 * 1.6).r - 0.5) * 0.25 * fine;   // 0.22.4: the new tile has more contrast
    float da = 0.16 * amp * u_wave_scale * smoothstep(1.4 * cell, 3.0 * cell, 5.3 * u_wave_scale);
    d.y += dm * da;
    float e = 0.6 * u_wave_scale;
// finite-difference slope of the displacement, folded into the tangents
    float dmx = (texture(u_disp, (q + vec2(e, 0.0)) * 0.19 + u_drift1 * 0.8).r - texture(u_disp, (q - vec2(e, 0.0)) * 0.19 + u_drift1 * 0.8).r) * 0.55 * da / (2.0 * e);
    float dmz = (texture(u_disp, (q + vec2(0.0, e)) * 0.19 + u_drift1 * 0.8).r - texture(u_disp, (q - vec2(0.0, e)) * 0.19 + u_drift1 * 0.8).r) * 0.55 * da / (2.0 * e);
    tx.y += dmx; tz.y += dmz;
  }
// the water the ships push aside (GLSL_SEA_VS_COMMON), slope by finite differences
  v_shipw = 0.0;
  if (u_nships > 0) {
    float se = 0.05 * u_wave_scale, s0 = ship_wave(p, u_time);
    d.y += s0; tx.y += (ship_wave(p + vec2(se, 0.0), u_time) - s0) / se; tz.y += (ship_wave(p + vec2(0.0, se), u_time) - s0) / se; v_shipw = s0;
  }
  v_wpos = vec3(p.x + d.x, d.y, p.y + d.z);
  v_nrm = normalize(cross(tz, tx));
  v_h = h;
  gl_Position = u_vp * vec4(v_wpos, 1.0);
}
