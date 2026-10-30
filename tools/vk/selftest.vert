#version 450
// Vulkan self test (gfx/rhi_vk.cc): one triangle covering the whole target, its colour a
// gradient so the readback can tell orientation and interpolation apart from a flat clear.
layout(location = 0) out vec3 v_col;
void main() {
    vec2 p = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
    v_col = vec3(p, 1.0);
}
