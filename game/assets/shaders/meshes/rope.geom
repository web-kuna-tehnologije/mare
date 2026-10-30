// Cordage geometry stage (gfx/shipyard.cc, meshes/rope.vert): a rope arrives as a line
// segment and leaves as a screen-facing quad. Its half-width is the *physical* rope projected
// to pixels - a 4 cm shroud is a hair at map zoom and a visible line in the battle view - but
// never under u_rope.y pixels, so the rig stays legible instead of flickering out. Shaded as
// a cylinder seen side-on: the normal faces the viewer and tilts towards each edge.
layout(lines) in;
layout(triangle_strip, max_vertices = 4) out;
in vec3 gv_nrm[]; in vec3 gv_col[]; in vec3 gv_wpos[]; in float gv_sel[]; in vec2 gv_rm[]; in float gv_dither[]; in float gv_wet[]; in float gv_hw[];
out vec3 v_nrm; out vec3 v_col; out vec3 v_wpos; out float v_sel; out vec2 v_rm; out float v_dither; out float v_wet; out vec3 v_cloth;
uniform vec2 u_viewport;   // framebuffer size, pixels
uniform vec2 u_rope;       // x = pixels per world unit at distance 1, y = minimum half-width in pixels
uniform vec3 u_campos;
void emit_v(int i, vec2 off_ndc, vec3 n) {
  vec4 cp = gl_in[i].gl_Position;
  gl_Position = cp + vec4(off_ndc * cp.w, 0.0, 0.0);
  v_nrm = n; v_col = gv_col[i]; v_wpos = gv_wpos[i]; v_sel = gv_sel[i]; v_rm = gv_rm[i]; v_dither = gv_dither[i]; v_wet = gv_wet[i];
  v_cloth = vec3(0.0);   // rope, not canvas
  EmitVertex();
}
void main(){
  vec4 c0 = gl_in[0].gl_Position, c1 = gl_in[1].gl_Position;
  if (c0.w <= 0.0 && c1.w <= 0.0) return;             // wholly behind the camera
  float w0 = max(c0.w, 1e-4), w1 = max(c1.w, 1e-4);
  vec2 dpx = (c1.xy / w1 - c0.xy / w0) * u_viewport * 0.5;
  float l = length(dpx);
  vec2 dir = l > 1e-4 ? dpx / l : vec2(1.0, 0.0);
  vec2 perp = vec2(-dir.y, dir.x);
  float h0 = max(gv_hw[0] * u_rope.x / w0, u_rope.y);
  float h1 = max(gv_hw[1] * u_rope.x / w1, u_rope.y);
  vec2 o0 = perp * h0 / (u_viewport * 0.5), o1 = perp * h1 / (u_viewport * 0.5);
  vec3 view0 = normalize(u_campos - gv_wpos[0]), view1 = normalize(u_campos - gv_wpos[1]);
  vec3 axis = normalize(gv_wpos[1] - gv_wpos[0] + vec3(1e-6));
  vec3 side0 = normalize(cross(axis, view0) + vec3(1e-6)), side1 = normalize(cross(axis, view1) + vec3(1e-6));
  emit_v(0, -o0, normalize(view0 - side0 * 0.6));
  emit_v(0,  o0, normalize(view0 + side0 * 0.6));
  emit_v(1, -o1, normalize(view1 - side1 * 0.6));
  emit_v(1,  o1, normalize(view1 + side1 * 0.6));
  EndPrimitive();
}
