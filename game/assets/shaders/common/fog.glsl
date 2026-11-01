// u_fog: x = density multiplier, y = start distance (game/weather.cc; 1 and 250 in clear air)
uniform vec2 u_fog;
vec3 fog_apply(vec3 c, float dist, float mapness, vec3 sky) {
  float f = 1.0 - exp(-max(dist - u_fog.y, 0.0) * 0.0016 * u_fog.x);
// 0.22.12 (issue9 "boat silhouettes are visible through fog"): the mix used to stop at 85%, so
// fifteen percent of a dark hull or a black rope always survived the fog as an outline - and
// the cordage is drawn at a pixel minimum, so every distant rig stayed etched on the haze
  return mix(c, sky, f*(1.0-mapness));
}
