in vec2 a_pos;
uniform vec3 u_center;
// xz = centre, z component unused
uniform vec3 u_detail;
// x = radius, y = warp, z = grid step in a_pos units (2/cells)
#include "sea/vs_common.glsl"
#include "common/waves.glsl"
void main(){
  vec2 w = sign(a_pos) * (exp(u_detail.y * abs(a_pos)) - 1.0) / (exp(u_detail.y) - 1.0) * u_detail.x;
  vec2 p = u_center.xz + w;
// World size of one cell here: the grid is warped, so cells are ~0.14 units under the
// camera and ~7.6 at the edge. d|w|/da * the step in a_pos. Waves shorter than a few of
// these are faded out by wave_sum instead of aliasing into a sliding pattern.
  vec2 dwda = u_detail.y * exp(u_detail.y * abs(a_pos)) / (exp(u_detail.y) - 1.0) * u_detail.x;
  float cell = max(dwda.x, dwda.y) * u_detail.z;
  float h = height_at(p); float dep = max(-h, 0.0);
// amplitude: full in open water, fading into the shallows and towards the grid edge so the
// detail grid always meets the flat world grid (and the beach) on a flat surface
  float edge = 1.0 - smoothstep(0.55, 0.96, max(abs(a_pos.x), abs(a_pos.y)));
  v_edge = edge;
  float shal = smoothstep(0.0, u_shallow * u_wave_scale, dep);
  float amp = u_wave_amp * edge * shal * shoal_boost(dep, u_wave_scale);
  v_amp = amp;
  vec3 tx, tz; float crest;
  vec3 d = wave_sum(p, amp, u_wave_scale, cell, tx, tz, crest);
  v_jac = tx.x * tz.z - tx.z * tz.x; v_crest = crest;
  v_slope = 1.0 - normalize(cross(tz, tx)).y;
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
