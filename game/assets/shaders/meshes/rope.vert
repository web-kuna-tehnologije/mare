// Cordage vertex stage (gfx/shipyard.cc): each rope is a *line segment* in the rig mesh. This
// is meshes.vert's rigid-body transform without the cloth model; the geometry stage
// (meshes/rope.geom) then expands the segment into a screen-facing quad of physical width
// with a pixel minimum, so shrouds read as hemp at battle scale and still as a line from the
// regional view. Outputs are gv_* so the geometry stage can re-emit them as meshes.frag's v_*.
in vec3 a_pos; in vec3 a_nrm; in vec3 a_col; in float a_mat; in vec2 a_rm;
in vec4 i_pos; in vec4 i_scol; in vec4 i_misc; in vec4 i_fx; in vec4 i_anchor;
uniform mat4 u_vp; uniform float u_time;
uniform float u_wave_amp; uniform float u_wave_scale; uniform float u_local_depth;
uniform sampler2D u_hm; uniform vec4 u_geo; uniform float u_wk;
uniform vec3 u_campos_v; uniform vec2 u_nearfade;   // see meshes.vert (0.22.9)
out vec3 gv_nrm; out vec3 gv_col; out vec3 gv_wpos; out float gv_sel; out vec2 gv_rm; out float gv_dither; out float gv_wet; out float gv_hw; out float gv_q; out float gv_near;
#include "common/waves.glsl"
void main(){
  gv_dither = i_fx.x;
  float h = i_pos.w; float c = cos(h), s = sin(h);
  vec3 p = a_pos * i_scol.x; if (i_misc.w > 0.0) p.x *= i_misc.w;
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
