// Cloud layer (gfx/sky.c, DESIGN_VISUAL.md step 8): value-noise fbm on a plane u_cloud.w units
// up, drifting by u_cloud.xy; u_cloud.z is the cover. The sky pass draws it, and sun_shade
// darkens the direct term where a cloud sits between the surface and the sun.
uniform vec4 u_cloud;
float cl_hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
// Gradient noise, not value noise. Value noise interpolates one number per lattice corner, 
// so a threshold through it follows the lattice and paints the ground in soft rectangles - 
// which is exactly what the cloud shadows did. Gradient noise interpolates a dot product 
// with a random direction per corner: it is zero at every corner, so no lattice structure 
// survives. Quintic fade for a continuous second derivative.
vec2 cl_grad(vec2 i) { float a = cl_hash(i) * 6.2831853; return vec2(cos(a), sin(a)); }
float cl_noise(vec2 p) {
  vec2 i = floor(p), f = fract(p);
  vec2 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
  float a = dot(cl_grad(i), f);
  float b = dot(cl_grad(i + vec2(1.0, 0.0)), f - vec2(1.0, 0.0));
  float c = dot(cl_grad(i + vec2(0.0, 1.0)), f - vec2(0.0, 1.0));
  float d = dot(cl_grad(i + vec2(1.0, 1.0)), f - vec2(1.0, 1.0));
  return mix(mix(a, b, u.x), mix(c, d, u.x), u.y) * 0.78 + 0.5;
}
// Each octave is rotated as well as scaled, and the sum is normalised by its own amplitude. 
// Without the rotation the lattice of the coarsest octave stays axis-aligned and paints the 
// ground in soft rectangles; without the normalisation the 3-octave shadow field and the 
// 5-octave visible one cover different fractions of the sky and the shadows stop matching 
// the clouds casting them.
float cl_fbm(vec2 p, int octaves) {
  float f = 0.0, a = 0.55, norm = 0.0;
  for (int i = 0; i < octaves; ++i) {
    f += a * cl_noise(p); norm += a;
    p = mat2(0.80, 0.60, -0.60, 0.80) * (p * 2.07) + vec2(13.1, 7.3); a *= 0.62;
  }
  return f / max(norm, 1e-4);
}
float cloud_density(vec2 xz, int octaves) {
// the base lattice is rotated off the world axes as well, so even the coarsest feature 
// edges do not run north-south
  vec2 q = mat2(0.86, 0.51, -0.51, 0.86) * (xz * 0.017) + u_cloud.xy;
  float f = cl_fbm(q, octaves);
  float thr = 0.74 - u_cloud.z * 0.52;
  return smoothstep(thr, thr + 0.26, f);
}
float cloud_shadow(vec3 wpos, vec3 sun) {
  if (u_cloud.z <= 0.001) return 1.0;
  vec2 xz = wpos.xz + sun.xz / max(sun.y, 0.15) * (u_cloud.w - wpos.y);
// where the sun's ray through this point meets the cloud plane
  return 1.0 - 0.62 * cloud_density(xz, 4);
}
