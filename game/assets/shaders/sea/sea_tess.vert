in vec2 a_pos;
// lattice index (i, j) in [0, N]
uniform vec3 u_origin;
// xz of lattice corner (snapped), z component = patch size
out vec2 v_lat;
void main(){ v_lat = u_origin.xy + a_pos * u_origin.z; }
