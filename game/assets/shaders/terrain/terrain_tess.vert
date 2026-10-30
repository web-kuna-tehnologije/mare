// tessellated terrain (GL 4.0 path): the control points are chunk-local (u, v); the control
// stage picks the subdivision, the evaluation stage runs the same displacement terrain.vert
// does for the plain-grid fallback.
in vec3 a_pos;
out vec2 v_cp;
void main(){ v_cp = a_pos.xy; }
