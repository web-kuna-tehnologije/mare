in vec3 a_pos; in vec3 a_nrm; in vec3 a_col; in float a_mat; in vec2 a_rm;
in vec4 i_pos; in vec4 i_scol; in vec4 i_misc; in vec4 i_fx;
// xyz+heading | scale+rgb | sway, highlight, bob, x-stretch | dither, reserved
uniform mat4 u_vp; uniform float u_time;
uniform float u_wave_amp; uniform float u_wave_scale; uniform float u_local_depth;
uniform sampler2D u_hm; uniform vec4 u_geo; uniform float u_wk;
out vec3 v_nrm; out vec3 v_col; out vec3 v_wpos; out float v_sel; out vec2 v_rm; out float v_dither;
#include "common/waves.glsl"
void main(){
  v_dither = i_fx.x;
  float h = i_pos.w; float c = cos(h), s = sin(h);
  vec3 p = a_pos * i_scol.x; if (i_misc.w > 0.0) p.x *= i_misc.w;
#include "meshes/cloth.glsl"
// Sea motion: roll about the keel plus a slower pitch, two frequencies each so the period 
// never reads as a loop. i_misc.x carries how hard the ship is working (moored, under way, 
// or caught in a storm), so anchored ships barely stir while a storm throws them about.
  float ph = i_pos.x*3.0 + i_pos.z*2.0;
  float roll  = i_misc.x * (sin(u_time*1.30 + ph) + 0.4*sin(u_time*2.10 + ph*1.7)) * 0.045;
  float pitch = i_misc.x * (sin(u_time*0.90 + ph*0.6) + 0.3*sin(u_time*1.70 + ph)) * 0.022;
  float cr = cos(roll), sr = sin(roll), cp = cos(pitch), sp = sin(pitch);
  p = vec3(p.x, p.y*cr - p.z*sr, p.y*sr + p.z*cr);
  p = vec3(p.x*cp - p.y*sp, p.x*sp + p.y*cp, p.z);
  vec3 n = vec3(cnrm.x, cnrm.y*cr - cnrm.z*sr, cnrm.y*sr + cnrm.z*cr);
  n = vec3(n.x*cp - n.y*sp, n.x*sp + n.y*cp, n.z);
  vec3 wp = vec3(p.x*c + p.z*s, p.y, -p.x*s + p.z*c) + i_pos.xyz;
  wp.y += i_misc.z * (sin(u_time*1.10 + i_pos.x*2.0) * 0.02 + sin(u_time*0.47 + i_pos.z*1.3) * 0.012);
// Floating instances (i_misc.z, the bob flag) ride the Gerstner swell the sea pass draws:
// the hull is lifted to the surface at its own position and sheared by the surface slope, so
// a ship pitches into a trough instead of sailing through it. The shadow pass applies the
// same transform, so the shadow stays under the ship.
  float wamp = u_wave_amp * i_misc.z;
  if (wamp > 0.0) wamp *= wave_depth_fade(u_hm, i_pos.xz, u_geo, u_wk, u_wave_scale, u_local_depth);
  vec3 wtx, wtz; float wcrest;
  vec3 wdisp = wave_sum(i_pos.xz, wamp, u_wave_scale, 0.0, wtx, wtz, wcrest);
  vec3 wup = normalize(cross(wtz, wtx));
  wp.y += wdisp.y - (wup.x * (wp.x - i_pos.x) + wup.z * (wp.z - i_pos.z)) / max(wup.y, 0.35);
  v_nrm = vec3(n.x*c + n.z*s, n.y, -n.x*s + n.z*c);
  v_col = mix(a_col, i_scol.yzw, a_mat);
  v_wpos = wp; v_sel = i_misc.y; v_rm = vec2(mix(a_rm.x, 0.9, isc), mix(a_rm.y, 0.0, isc));
  gl_Position = u_vp * vec4(wp, 1.0);
}
