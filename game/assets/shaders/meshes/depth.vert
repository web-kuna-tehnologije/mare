in vec3 a_pos; in vec3 a_nrm; in vec3 a_col; in float a_mat; in vec2 a_rm;
in vec4 i_pos; in vec4 i_scol; in vec4 i_misc; in vec4 i_fx; in vec2 i_anchor;
uniform mat4 u_vp; uniform float u_time;
uniform float u_wave_amp; uniform float u_wave_scale; uniform float u_local_depth;
uniform sampler2D u_hm; uniform vec4 u_geo; uniform float u_wk;
out float v_dither;
#include "common/waves.glsl"
void main(){
  v_dither = i_fx.x;
  float h = i_pos.w; float c = cos(h), s = sin(h);
  vec3 p = a_pos * i_scol.x;
  if (i_misc.w > 0.0) { p.x *= i_misc.w; p.z *= i_misc.w; }   // radial slim (gpu.h): thins a mast without shortening it
#include "meshes/cloth.glsl"
// rigid instances may carry a pitch about their own z axis in fx2 (0.22.5): the oars dip their
// blades into the sea with it; cloth instances use fx2/fx3 for the wind and are skipped
  if (i_fx.y < 0.5 && abs(i_fx.z) > 1e-4) { float cq = cos(i_fx.z), sq = sin(i_fx.z); p = vec3(p.x*cq - p.y*sq, p.x*sq + p.y*cq, p.z); }
// Sea motion: roll about the keel plus a slower pitch, two frequencies each so the period 
// never reads as a loop. i_misc.x carries how hard the ship is working (moored, under way, 
// or caught in a storm), so anchored ships barely stir while a storm throws them about.
// A ship is rigid: its masts, yards, sails and oars are offset instances that must move as
// one body with the hull. i_anchor carries the hull's position for those parts; everything
// phase- or wave-dependent below reads the anchor, so every part of one ship samples the
// same swell, the same roll and the same pitch. dl is the part's offset in the hull frame:
// a pitching bow drops its foremast with it (first-order rigid translation).
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
// Floating instances (i_misc.z, the bob flag) ride the Gerstner swell the sea pass draws:
// the hull is lifted to the surface at its own position and sheared by the surface slope, so
// a ship pitches into a trough instead of sailing through it. The shadow pass applies the
// same transform, so the shadow stays under the ship.
  float wamp = u_wave_amp * i_misc.z;
  if (wamp > 0.0) wamp *= wave_depth_fade(u_hm, ap, u_geo, u_wk, u_wave_scale, u_local_depth);
  vec3 wtx, wtz; float wcrest;
  vec3 wdisp = wave_sum(ap, wamp, u_wave_scale, 0.0, wtx, wtz, wcrest);
  vec3 wup = normalize(cross(wtz, wtx));
  wp.y += wdisp.y - (wup.x * (wp.x - ap.x) + wup.z * (wp.z - ap.y)) / max(wup.y, 0.35);
  gl_Position = u_vp * vec4(wp, 1.0);
}
