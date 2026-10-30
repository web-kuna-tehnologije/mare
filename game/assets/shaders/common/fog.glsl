// u_fog: x = density multiplier, y = start distance (game/weather.cc; 1 and 250 in clear air)
uniform vec2 u_fog;
vec3 fog_apply(vec3 c, float dist, float mapness, vec3 sky) {
  float f = 1.0 - exp(-max(dist - u_fog.y, 0.0) * 0.0016 * u_fog.x);
  return mix(c, sky, f*(1.0-mapness)*0.85);
}
