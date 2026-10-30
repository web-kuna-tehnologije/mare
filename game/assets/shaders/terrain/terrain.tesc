layout(vertices = 4) out;
in vec2 v_cp[]; out vec2 tc_uv[];
uniform vec3 u_campos; uniform vec2 u_origin; uniform float u_size; uniform vec2 u_tess;
// u_tess: x = pixels per world unit at distance 1 / target px per cell, y = max level.
// Levels are a pure function of the edge's world position and the camera, so the shared edge
// of two patches - and of two neighbouring chunks - always gets the same level: watertight
// without the fallback grid's skirts.
float lvl(vec2 a, vec2 b) {
  vec2 m = u_origin + (a + b) * 0.5 * u_size;
  float ew = distance(a, b) * u_size;
  float d = max(distance(vec3(m.x, 0.0, m.y), u_campos), 1.0);
  return clamp(u_tess.x * ew / d, 1.0, u_tess.y);
}
void main(){ tc_uv[gl_InvocationID] = v_cp[gl_InvocationID];
  if (gl_InvocationID == 0) {
    float L0 = lvl(v_cp[3], v_cp[0]), L1 = lvl(v_cp[0], v_cp[1]), L2 = lvl(v_cp[1], v_cp[2]), L3 = lvl(v_cp[2], v_cp[3]);
    gl_TessLevelOuter[0] = L0; gl_TessLevelOuter[1] = L1; gl_TessLevelOuter[2] = L2; gl_TessLevelOuter[3] = L3;
    gl_TessLevelInner[0] = max(L1, L3); gl_TessLevelInner[1] = max(L0, L2); } }
