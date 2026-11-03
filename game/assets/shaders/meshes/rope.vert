// Cordage vertex stage (gfx/shipyard.cc): each rope is a *line segment* in the rig mesh. This
// is meshes.vert's rigid-body transform without the cloth model; the geometry stage
// (meshes/rope.geom) then expands the segment into a screen-facing quad of physical width
// with a pixel minimum, so shrouds read as hemp at battle scale and still as a line from the
// regional view. Outputs are gv_* so the geometry stage can re-emit them as meshes.frag's v_*.
in vec3 a_pos; in vec3 a_nrm; in vec3 a_col; in float a_mat; in vec2 a_rm;
in vec4 i_pos; in vec4 i_scol; in vec4 i_misc; in vec4 i_fx; in vec4 i_anchor; in vec4 i_rig;
uniform mat4 u_vp; uniform float u_time;
uniform float u_wave_amp; uniform float u_wave_scale; uniform float u_local_depth;
uniform sampler2D u_hm; uniform vec4 u_geo; uniform float u_wk;
uniform vec3 u_campos_v; uniform vec2 u_nearfade;   // see meshes.vert (0.22.9)
out vec3 gv_nrm; out vec3 gv_col; out vec3 gv_wpos; out float gv_sel; out vec2 gv_rm; out float gv_dither; out float gv_wet; out float gv_hw; out float gv_q; out float gv_near;
out vec3 gv_mpos;
#include "common/waves.glsl"
void main(){
  gv_dither = i_fx.x;
  float h = i_pos.w; float c = cos(h), s = sin(h);
  vec3 p = a_pos * i_scol.x; if (i_misc.w > 0.0) p.x *= i_misc.w;
// 0.22.15 (issue12 "rope behaviour flexible and physics based"): a rope hangs. a_nrm carries
// (t along the rope, slack, kind) from gpu.cc line_rope; the sag is a parabola 4t(1-t) of the
// slack, pulled down by gravity and blown to leeward by the wind in the ship's frame, and it
// lags the heel: as the hull rolls to starboard the bight swings a little the other way, as a
// hanging rope does. Static instances (kind 0) are straight.
  if (a_nrm.z > 0.5) {
// 0.22.17 (issue14 "add more elasticity to rope for sails"): a_nrm.z is the kind - 2 is running
// rigging, a sheet or a brace made fast to the canvas, which comes up taut as the sail fills and
// hangs again in a calm; 1 is standing rigging, which keeps most of its sag. On top of that the
// bight *breathes*: two slow terms out of phase, so a rope is never a frozen curve.
// 0.22.23 (todo 121b, issue19 "then the verlet rope"): the bight's *direction* is no longer a
// pair of sines - it comes from the four-node verlet chain the CPU solves for this ship
// (gfx/shipyard.cc rig_sim_step), handed over as i_rig.x, the chain's swing angle in her own
// frame, and i_rig.y, how far it has been drawn out towards straight. So the rigging lags her
// roll, overshoots it and damps down on its own, which no closed form here could do. The
// small breathing terms stay: they are the fine chatter a four-node chain does not resolve.
    float pull = clamp(length(i_fx.zw) * 1.2, 0.0, 1.0);
    float taut = clamp(i_rig.y, 0.0, 1.0);
    float t = a_nrm.x;
    float bight = 4.0 * t * (1.0 - t) * a_nrm.y * i_scol.x
                * (1.0 - (a_nrm.z > 1.5 ? 0.78 : 0.30) * pull) * (1.0 - 0.45 * taut);
    float sw = i_rig.x;                                   // + = the bight is out to starboard
    float ph = u_time * 1.9 + a_pos.x * 7.0 + a_pos.z * 5.0;
    p.y -= bight * (cos(sw) * 0.90 + 0.10 * sin(ph * 0.41 + 1.7));      // it rises as it swings out
    p.x += bight * 0.22 * sin(sw) + bight * 0.05 * sin(ph * 0.73);      // a little fore-and-aft with her
    p.z += bight * (sin(sw) * 0.95 - 0.9 * i_anchor.z) + bight * 0.07 * sin(ph * 1.3);
  }
// the rope's physical half-width rides in the roughness channel (model units: fractions of
// the hull length), scaled to world units by the instance
  gv_hw = a_rm.x * i_scol.x;
  gv_q = i_fx.y;   // the cordage's condition (1 tarred and worn, 2 new hemp): meshes.frag picks the rope texture (0.22.8)
  vec2 ap = i_pos.xz;
  if (abs(i_anchor.x) + abs(i_anchor.y) > 1e-6) ap = i_anchor.xy;
  float ph = ap.x*3.0 + ap.y*2.0;
  float roll  = i_misc.x * (sin(u_time*1.30 + ph) + 0.4*sin(u_time*2.10 + ph*1.7)) * 0.045;
  float pitch = i_misc.x * (sin(u_time*0.90 + ph*0.6) + 0.3*sin(u_time*1.70 + ph)) * 0.022;
// the body's own attitude (0.22.12): the five-point buoyancy and the recoil heel from
// sim/naval_physics, the same angles on every part of the ship, about the same pivot
  roll += i_anchor.z; pitch += i_anchor.w;
  float cr = cos(roll), sr = sin(roll), cp = cos(pitch), sp = sin(pitch);
// 0.22.10 (issue7 "the lantina and the sail should act with the ship as if tied to it, which
// they are"). The roll and the pitch are applied to the whole ship as ONE rigid body, about
// one pivot - the hull's anchor at the waterline - instead of to each part about its own
// origin. Before, a sail ten metres over the deck rolled about its own foot while the rail
// under it swung sideways, and the two slid against each other every swell.
  vec3 fwd = vec3(c, 0.0, -s), stb = vec3(s, 0.0, c);
  vec3 piv = vec3(ap.x, 0.0, ap.y);
  gv_mpos = p;
  vec3 wp = vec3(p.x*c + p.z*s, p.y, -p.x*s + p.z*c) + i_pos.xyz;
  { vec3 d = wp - piv; float lx = dot(d, fwd), ly = d.y, lz = dot(d, stb);
    float ly1 = ly*cr - lz*sr, lz1 = ly*sr + lz*cr;
    float lx2 = lx*cp - ly1*sp, ly2 = lx*sp + ly1*cp;
    wp = piv + fwd*lx2 + vec3(0.0, ly2, 0.0) + stb*lz1; }
  wp.y += i_misc.z * (sin(u_time*1.10 + ap.x*2.0) * 0.02 + sin(u_time*0.47 + ap.y*1.3) * 0.012);
  float wamp = u_wave_amp * i_misc.z;
  if (wamp > 0.0) wamp *= wave_depth_fade(u_hm, ap, u_geo, u_wk, u_wave_scale, u_local_depth);
  vec3 wtx, wtz; float wcrest;
  vec3 wdisp = wave_sum(ap, wamp, u_wave_scale, 0.0, wtx, wtz, wcrest);
  vec3 wup = normalize(cross(wtz, wtx));
  wp.y += wdisp.y - (wup.x * (wp.x - ap.x) + wup.z * (wp.z - ap.y)) / max(wup.y, 0.35);
  gv_wet = 0.0;
  gv_nrm = vec3(0.0, 1.0, 0.0);
  gv_col = mix(a_col, i_scol.yzw, a_mat);
  gv_wpos = wp; gv_sel = i_misc.y; gv_rm = vec2(0.80, 0.0);
  gv_near = u_nearfade.y > u_nearfade.x ? clamp((u_nearfade.y - distance(wp, u_campos_v)) / (u_nearfade.y - u_nearfade.x), 0.0, 1.0) : 0.0;
  gl_Position = u_vp * vec4(wp, 1.0);
}
