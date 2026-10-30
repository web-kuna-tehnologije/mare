in vec2 v_uv; out vec4 fragColor;
uniform mat4 u_inv_vp; uniform vec3 u_campos; uniform vec3 u_sun; uniform vec3 u_sky; uniform vec3 u_suncol;
uniform float u_daylight; uniform float u_haze; uniform float u_overcast;
#include "common/color.glsl"
#include "common/clouds.glsl"
void main(){
  vec4 w = u_inv_vp * vec4(v_uv * 2.0 - 1.0, 1.0, 1.0);
  vec3 dir = normalize(w.xyz / w.w - u_campos);
  float h = clamp(dir.y, -1.0, 1.0);
  vec3 sky = srgb_to_linear(u_sky);
  vec3 zenith = sky * (0.72 - 0.1 * u_overcast);
  vec3 horizon = mix(sky, vec3(0.86, 0.88, 0.92) * max(u_daylight, 0.3), 0.5 + 0.4 * u_haze);
  vec3 col = mix(horizon, zenith, pow(clamp(h, 0.0, 1.0), 0.42));
  if (h < 0.0) col = horizon * (1.0 + h * 0.6);
  float d = dot(dir, u_sun);
  float disc = smoothstep(0.99935, 0.99985, d), glow = pow(max(d, 0.0), 72.0) * 0.14 + pow(max(d, 0.0), 9.0) * 0.045;
  col += u_suncol * (disc * 1.2 + glow) * (1.0 - 0.7 * u_cloud.z * u_overcast);
  if (u_cloud.z > 0.001 && dir.y > 0.004 && u_campos.y < u_cloud.w) {
    float t = (u_cloud.w - u_campos.y) / dir.y; vec2 cxz = u_campos.xz + dir.xz * t;
    float dens = cloud_density(cxz, 5);
    float horiz = smoothstep(0.0, 0.10, dir.y);
// clouds thin into the haze at the horizon
    float sunup = clamp(u_sun.y * 1.6, 0.0, 1.0);
    vec3 lit = mix(vec3(0.50, 0.53, 0.60), vec3(1.0, 0.99, 0.97), sunup) * max(u_daylight, 0.25);
    lit = mix(lit * 0.55, lit, 1.0 - dens * 0.75);
// thick parts are dark underneath
    lit = mix(lit, lit * vec3(0.78, 0.80, 0.86), u_overcast * 0.6);
    col = mix(col, lit, dens * horiz);
  }
  fragColor = vec4(col, 1.0);
}
