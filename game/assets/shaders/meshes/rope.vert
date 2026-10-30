// Cordage vertex stage (gfx/shipyard.cc): each rope is a *line segment* in the rig mesh. This
// is meshes.vert's rigid-body transform without the cloth model; the geometry stage
// (meshes/rope.geom) then expands the segment into a screen-facing quad of physical width
// with a pixel minimum, so shrouds read as hemp at battle scale and still as a line from the
// regional view. Outputs are gv_* so the geometry stage can re-emit them as meshes.frag's v_*.
in vec3 a_pos; in vec3 a_nrm; in vec3 a_col; in float a_mat; in vec2 a_rm;
in vec4 i_pos; in vec4 i_scol; in vec4 i_misc; in vec4 i_fx; in vec2 i_anchor;
uniform mat4 u_vp; uniform float u_time;
uniform float u_wave_amp; uniform float u_wave_scale; uniform float u_local_depth;
uniform sampler2D u_hm; uniform vec4 u_geo; uniform float u_wk;
out vec3 gv_nrm; out vec3 gv_col; out vec3 gv_wpos; out float gv_sel; out vec2 gv_rm; out float gv_dither; out float gv_wet; out float gv_hw;
#include "common/waves.glsl"
void main(){
  gv_dither = i_fx.x;
  float h = i_pos.w; float c = cos(h), s = sin(h);
  vec3 p = a_pos * i_scol.x; if (i_misc.w > 0.0) p.x *= i_misc.w;
// the rope's physical half-width rides in the roughness channel (model units: fractions of
// the hull length), scaled to world units by the instance
  gv_hw = a_rm.x * i_scol.x;
  vec2 ap = i_pos.xz; vec2 dl = vec2(0.0);
  if (abs(i_anchor.x) + abs(i_anchor.y) > 1e-6) {
    ap = i_anchor;
    vec2 d = i_pos.xz - ap;
    dl = vec2(d.x*c - d.y*s, d.x*s + d.y*c);
  }
  float ph = ap.x*3.0 + ap.y*2.0;
  float roll  = i_misc.x * (sin(u_time*1.30 + ph) + 0.4*sin(u_time*2.10 + ph*1.7)) * 0.045;
  float pitch = i_misc.x * (sin(u_time*0.90 + ph*0.6) + 0.3*sin(u_time*1.70 + ph)) * 0.022;
  float cr = cos(roll), sr = sin(roll), cp = cos(pitch), sp = sin(pitch);
  p = vec3(p.x, p.y*cr - p.z*sr, p.y*sr + p.z*cr);
  p = vec3(p.x*cp - p.y*sp, p.x*sp + p.y*cp, p.z);
  vec3 wp = vec3(p.x*c + p.z*s, p.y, -p.x*s + p.z*c) + i_pos.xyz;
  wp.y += i_misc.z * (sin(u_time*1.10 + ap.x*2.0) * 0.02 + sin(u_time*0.47 + ap.y*1.3) * 0.012);
  wp.y += i_misc.x * (dl.x * sp - dl.y * sr);
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
  gl_Position = u_vp * vec4(wp, 1.0);
}
