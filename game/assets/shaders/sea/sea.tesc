layout(vertices = 4) out;
in vec2 v_lat[]; out vec2 tc_pos[];
uniform vec3 u_campos; uniform vec2 u_tess; uniform float u_patch;
// u_tess: x = pixels per world unit at distance 1 / target px, y = max level
float lvl(vec2 a, vec2 b) { vec2 m = (a + b) * 0.5; float d = max(distance(vec3(m.x, 0.0, m.y), u_campos), 1.0); return clamp(u_tess.x * u_patch / d, 1.0, u_tess.y); }
void main(){ tc_pos[gl_InvocationID] = v_lat[gl_InvocationID];
  if (gl_InvocationID == 0) {
    float L0 = lvl(v_lat[3], v_lat[0]), L1 = lvl(v_lat[0], v_lat[1]), L2 = lvl(v_lat[1], v_lat[2]), L3 = lvl(v_lat[2], v_lat[3]);
    float I0 = max(L1, L3), I1 = max(L0, L2);
    gl_TessLevelOuter[0] = L0; gl_TessLevelOuter[1] = L1; gl_TessLevelOuter[2] = L2; gl_TessLevelOuter[3] = L3;
    gl_TessLevelInner[0] = I0; gl_TessLevelInner[1] = I1; } }
