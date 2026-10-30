in vec3 v_nrm; in vec3 v_col; in vec3 v_wpos; in float v_sel; in vec2 v_rm; in float v_dither; in float v_wet;
in vec3 v_cloth;
uniform vec3 u_campos; uniform vec3 u_sky; uniform float u_mapness; uniform sampler2D u_grain;
out vec4 fragColor;
#include "common/color.glsl"
#include "common/pbr.glsl"
#include "common/light.glsl"
#include "common/nmap.glsl"
#include "common/fog.glsl"
#include "common/dither.glsl"
void main(){
  dither_clip(v_dither);
  vec3 n = normalize(v_nrm);
  vec3 albedo = srgb_to_linear(v_col);
  vec3 vdir = normalize(u_campos - v_wpos);
// Two-sided shading: sails, flags and roof slopes are single-quad surfaces drawn without
// backface culling, so flip the flat normal towards the viewer instead of letting whole
// faces fall into the ambient-only floor.
  if (dot(n, vdir) < 0.0) n = -n;
// triplanar surface grain, fading with distance so far meshes keep their clean silhouette;
// rougher materials (wood, cloth, stone) get more relief than painted ones
  float dist0 = distance(v_wpos, u_campos);
  float grain = v_rm.x * 0.42 * (1.0 - smoothstep(30.0, 120.0, dist0));
  if (grain > 0.01) n = nmap_triplanar(u_grain, v_wpos * 1.6, n, 1.0, grain);
  float dist = dist0;
  float rough = v_rm.x;
// Wood (ship hulls, spars, brown roofs): the baked vertex colours are flat per plank, so the
// grain map also modulates the *albedo* - one broad sample for tonal plank variation and one
// stretched along y for the grain streaks that read as planking on a hull side (the vertical
// projection squeezes y, so the streaks run with the strakes). Varnished ridges catch a bit
// more gloss. Gated on brown-and-rough so painted trim, sails and stone keep their look.
  float wood = clamp((v_col.r - v_col.b) * 4.0, 0.0, 1.0) * smoothstep(0.45, 0.70, v_rm.x)
             * (1.0 - smoothstep(25.0, 90.0, dist0));
  if (wood > 0.01) {
    vec2 guv = abs(n.y) > 0.6 ? v_wpos.xz : (abs(n.x) > abs(n.z) ? v_wpos.zy : v_wpos.xy);
    float planks = texture(u_grain, guv * 0.55).g;
    float streaks = texture(u_grain, guv * vec2(0.9, 3.4)).g;
    albedo *= mix(1.0, 0.78 + 0.38 * planks + 0.14 * streaks, wood);
    rough = mix(rough, rough * (1.0 - 0.22 * streaks), wood);
  }
// The wet band above the waterline (meshes.vert): soaked wood is darker and glossier, which
// is what seats a hull *in* the water instead of on it.
  albedo *= 1.0 - 0.38 * v_wet;
  rough *= 1.0 - 0.45 * v_wet;
// Canvas (0.19). A sail is sewn from vertical cloths about 60 cm wide with a tabling at the
// head and foot: the seams read as faint lines across the panel, the weave as a fine
// triplanar grain, the foot is grimier than the head, and the cloth is *translucent* - the
// sun behind a sail lights it from within. A little fuzz sheen at grazing angles instead of
// a hard specular, which is what keeps it reading as cloth rather than plastic.
  float canvas = v_cloth.x > 0.75 ? 1.0 : 0.0;          // flags (0.5) keep their plain colour
  vec3 trans = vec3(0.0);
  if (canvas > 0.5) {
    float near = 1.0 - smoothstep(20.0, 80.0, dist0);
    float across = v_cloth.y * 0.5 + 0.5, down = v_cloth.z;
    float panels = 7.0;
    float seam = 1.0 - smoothstep(0.0, 0.035, abs(fract(across * panels + 0.5) - 0.5));
    float tabling = (1.0 - smoothstep(0.0, 0.06, down)) + smoothstep(0.93, 0.985, down);
    float stain = texture(u_grain, v_wpos.xz * 0.23 + v_wpos.yy * 0.11).g;
    albedo *= 1.0 - 0.10 * seam * near - 0.06 * clamp(tabling, 0.0, 1.0) * near;
    albedo *= (0.90 + 0.10 * stain) * (1.0 - 0.09 * down);
    if (near > 0.01) n = nmap_triplanar(u_grain, v_wpos * 9.0, n, 1.0, 0.18 * near);
    rough = 0.86 + 0.06 * stain;
// transmission: the sun on the far side of the canvas (n faces the viewer), shadowed like the lit side
    float back = max(dot(-n, u_sun), 0.0);
    float sh = shadow_at(v_wpos, -n, back, dist0);
    trans = albedo * u_suncol * back * 0.28 * sh;
    trans += albedo * u_ambsky * 0.10;
// fuzz: the loose fibres catch light at a grazing view
    trans += albedo * (u_ambsky * 0.5 + u_suncol * 0.12) * pow(1.0 - max(dot(n, vdir), 0.0), 3.0) * 0.10;
  }
  vec3 c = sun_shade(albedo, n, vdir, v_wpos, rough, v_rm.y, dist) + trans;
  c = mix(c, srgb_to_linear(vec3(1.0,0.95,0.6)) * 1.6, v_sel * 0.35);
// selection tint
  c = fog_apply(c, dist, u_mapness, srgb_to_linear(u_sky));
  fragColor = vec4(c, 1.0);
}
