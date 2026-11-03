// Particle shading (gfx/particles.cc, 0.22.7). Three kinds share the pipeline:
//   0 smoke  - a disc eroded by a 3D value noise that scrolls with age, lit as a sphere (sun side
//              bright, lee side in the ambient), a back-lit rim where the sun shines through
//              the thin edge, faded where it meets the opaque scene (soft particles)
//   1 flash  - the muzzle flash: a hot core and a halo, bright enough to bloom
//   2 ember  - a burning point
in vec2 v_uv; in vec3 v_col; in vec4 v_misc; in vec3 v_wpos; in float v_size; in float v_soft;
uniform vec3 u_campos; uniform vec3 u_sky; uniform float u_mapness; uniform float u_time;
uniform vec3 u_cam_right; uniform vec3 u_cam_up;
uniform sampler2D u_scene_depth; uniform vec2 u_zrange; uniform vec2 u_scene_size; uniform float u_soft_on;
// 0.22.13 (issue10): the Meshy sheets as sprite atlases - smoke 2x2, fire 4x2, foam 2x2 frames (tools/fx_sprites.py)
uniform sampler2D u_fx_smoke; uniform sampler2D u_fx_fire; uniform sampler2D u_fx_foam; uniform vec3 u_has_fx;
vec2 atlas_uv(vec2 uv, float seed, float cols, float rows) { float n = cols * rows; float f = floor(fract(seed * 7.31) * n); vec2 cell = vec2(mod(f, cols), floor(f / cols)); return (uv * 0.5 + 0.5 + cell) / vec2(cols, rows); }
out vec4 fragColor;
#include "common/color.glsl"
#include "common/pbr.glsl"
#include "common/light.glsl"
#include "common/fog.glsl"
float h3(vec3 p) { return fract(sin(dot(p, vec3(127.1, 311.7, 74.7))) * 43758.5453); }
float vnoise3(vec3 p) {
  vec3 i = floor(p), f = fract(p); f = f * f * (3.0 - 2.0 * f);
  float a = mix(mix(h3(i), h3(i + vec3(1,0,0)), f.x), mix(h3(i + vec3(0,1,0)), h3(i + vec3(1,1,0)), f.x), f.y);
  float b = mix(mix(h3(i + vec3(0,0,1)), h3(i + vec3(1,0,1)), f.x), mix(h3(i + vec3(0,1,1)), h3(i + vec3(1,1,1)), f.x), f.y);
  return mix(a, b, f.z);
}
float fbm3(vec3 p) { float s = 0.0, a = 0.5; for (int i = 0; i < 4; ++i) { s += a * vnoise3(p); p = p * 2.07 + vec3(1.3, 2.1, 0.7); a *= 0.5; } return s; }
float lin_depth(float zw, vec2 nf) { float ndc = zw * 2.0 - 1.0; return 2.0 * nf.x * nf.y / (nf.y + nf.x - ndc * (nf.y - nf.x)); }
void main(){
  float r = length(v_uv);
  if (r > 1.0) discard;
  float alpha = v_misc.x, seed = v_misc.y, kind = v_misc.z, age = v_misc.w;
  float dist = distance(v_wpos, u_campos);
  vec3 skyl = srgb_to_linear(u_sky);
  if (kind > 7.5) {                                   // a flame tongue (0.22.20, issue16): fire, not a glowing disc
    // the tongue: narrow at the root, waving at the tip, eaten by noise that scrolls *down*
    // through the sprite as the flame climbs through it
// 0.22.23 (issue19 "fire more detail not streams but realistic fire"). The 0.22.21 tongue was
// one smooth exponential lobe wobbled by a single noise - at a distance that is a *stream*, a
// licking ribbon, which is exactly the complaint. A real flame is not one tongue: it is a
// cluster of them, splitting and rejoining, each with a hard bright root and a torn, lacy top
// that breaks into separate licks before it goes to smoke. So: three lobes at different
// widths and offsets, each wobbling on its own phase, combined by max (they overlap and
// separate as the noise moves); the top third is eaten by a second, finer noise, which is
// what makes the flame *break up* instead of tapering; and the root is held solid.
    vec2 uv = v_uv; uv.x *= 1.0 + 0.7 * (uv.y * 0.5 + 0.5);                      // wider at the top, where it tears
    float hgt0 = uv.y * 0.5 + 0.5;                                               // 0 root, 1 tip
    float tongue = 0.0;
    for (int L = 0; L < 3; ++L) {
      float fl = float(L);
      float ph = seed * 17.0 + fl * 4.7 + age * 6.0;
      float wob = (vnoise3(vec3(uv * (2.2 + fl * 1.1), ph)) - 0.5) * (0.4 + 0.9 * hgt0);
      float off = (fl - 1.0) * 0.30 * hgt0;                                      // the licks fan apart as they rise
      float w   = 2.9 + fl * 0.8;
      tongue = max(tongue, exp(-pow((uv.x + off + wob * 0.45) * w, 2.0)) * (L == 1 ? 1.0 : 0.82));
    }
    tongue *= smoothstep(1.0, -0.6, uv.y * (0.8 - 0.5 * age));
    float n = fbm3(vec3(uv * 2.6, seed * 9.0 - age * 4.0));
    // the lacy top: a finer noise eats the flame where it is thin and high, so it ends in
    // separate licks and embers rather than a drawn-out point
    float lace = fbm3(vec3(uv * 7.5, seed * 23.0 - age * 9.0));
    tongue *= 1.0 - smoothstep(0.35, 1.0, hgt0) * smoothstep(0.62, 0.30, lace);
    float body = clamp(tongue * (0.55 + 0.9 * n) - 0.18 * age, 0.0, 1.0);
    body = max(body, tongue * smoothstep(0.30, 0.0, hgt0) * 0.9);                // the root stays solid and bright
    if (u_has_fx.y > 0.5) {                                                      // the fire sheet gives it structure
      vec4 fx = texture(u_fx_fire, atlas_uv(vec2(uv.x, -uv.y), seed + floor(age * 3.0) * 0.137, 4.0, 2.0));
      body = clamp(body * (0.35 + 1.3 * fx.a), 0.0, 1.0);
    }
    // white-hot at the root through orange to deep red at the tip, bright enough to bloom
    float hgt = uv.y * 0.5 + 0.5;
    vec3 c = mix(vec3(1.0, 0.93, 0.70), mix(vec3(1.0, 0.55, 0.12), vec3(0.75, 0.12, 0.03), hgt), clamp(hgt * 1.6, 0.0, 1.0));
    float a = clamp(body * alpha, 0.0, 1.0);
    fragColor = vec4(srgb_to_linear(c) * (4.5 + 4.0 * body), a); return;         // fire lights itself: no fog, no sun
  }
  if (kind > 6.5) {                                   // a splinter: a dark chip, hard-edged, tumbling
    float a = smoothstep(0.9, 0.3, abs(v_uv.x) * 2.2 + abs(v_uv.y) * 0.6) * alpha;
    vec3 c = srgb_to_linear(v_col) * (u_ambsky * 0.8 + u_suncol * 0.5 * max(u_sun.y, 0.0));
    fragColor = vec4(fog_apply(c, dist, u_mapness, skyl), a); return;
  }
  if (kind > 5.5) {                                   // the ring the blast flattens on the water: an annulus of foam, breaking up as it spreads
    float n = vnoise3(vec3(v_uv * 4.0, seed * 7.0 + age * 2.0));
    float ring = exp(-pow((r - (0.55 + 0.3 * age)) / (0.16 + 0.1 * age), 2.0));
    float a = ring * alpha * (0.55 + 0.45 * n) * (1.0 - smoothstep(0.85, 1.0, r));
    if (u_has_fx.z > 0.5) a *= 0.35 + 1.1 * texture(u_fx_foam, atlas_uv(v_uv * 0.9, seed, 2.0, 2.0)).a;   // the foam sheet's torn structure
    vec3 c = srgb_to_linear(v_col) * (u_ambsky * 0.8 + u_ambgnd * 0.3 + u_suncol * 0.5 * max(u_sun.y, 0.0));
    fragColor = vec4(fog_apply(c, dist, u_mapness, skyl), a); return;
  }
  if (kind > 4.5) {                                   // spray: a soft white droplet cloud
    float n = vnoise3(vec3(v_uv * 3.0, seed * 5.0 + age * 3.0));
    float a = smoothstep(1.0, 0.1, r) * (0.5 + 0.5 * n) * alpha;
    if (u_has_fx.z > 0.5) a *= 0.4 + 1.0 * texture(u_fx_foam, atlas_uv(v_uv, seed + 0.37, 2.0, 2.0)).a;
    vec3 c = srgb_to_linear(v_col) * (u_ambsky * 0.9 + u_suncol * 0.55 * max(u_sun.y, 0.0));
    fragColor = vec4(fog_apply(c, dist, u_mapness, skyl), a * 0.8); return;
  }
  if (kind > 1.5 && kind < 2.5) {                     // ember
    float a = smoothstep(1.0, 0.15, r) * alpha;
    fragColor = vec4(srgb_to_linear(v_col) * 6.0, a); return;
  }
  if (kind > 0.5 && kind < 1.5) {                     // muzzle flash
    float n = vnoise3(vec3(v_uv * 3.0, seed * 9.0));
    float core = exp(-r * r * 7.0) * (0.8 + 0.4 * n), halo = exp(-r * r * 1.8) * 0.35;
    vec3 c = srgb_to_linear(v_col) * (core * 9.0 + halo * 2.5);
    float a = clamp((core + halo) * alpha, 0.0, 1.0);
    if (u_has_fx.y > 0.5) {   // the fire sheet: flame and sparks over the procedural core, so a flash has tongues and embers in it
      vec4 fx = texture(u_fx_fire, atlas_uv(v_uv, seed, 4.0, 2.0));
      fx.a *= smoothstep(1.0, 0.25, r) * (1.0 - age);   // 0.22.14: held inside the flash's own disc and gone with it - the sheet's smoke was reading as an orange cloud a hull long
      vec3 fc = srgb_to_linear(fx.rgb) * fx.a * 4.0;
      c = c * 0.7 + fc; a = clamp(a * 0.75 + fx.a * alpha * 0.6, 0.0, 1.0);
    }
    fragColor = vec4(fog_apply(c, dist, u_mapness, skyl), a); return;
  }
  // smoke, in three layers (kind 3 core, 0 plume, 4 haze). The noise is sampled in the
  // billboard plane plus the seed as a third axis, and it scrolls upward with age so the puff
  // churns instead of sliding as a stamp. The core is a cumulus: a lower threshold and a
  // coarser noise keep it a solid rolling ball; the haze is the opposite, a veil with no edge.
  float core = kind > 2.5 && kind < 3.5 ? 1.0 : 0.0, haze = kind > 3.5 ? 1.0 : 0.0;
  vec3 np = vec3(v_uv * (1.7 - 0.5 * core + 0.4 * haze), seed * 11.0 + age * 0.9) + vec3(0.0, age * 0.6, 0.0);
  float n = fbm3(np * 1.35);
// 0.22.9 (issue6 "not perfect shape"): a second, coarser noise on its own drift warps the
// *silhouette* rather than the shading, so no two puffs are the same lump and none of them
// is a circle. The warp is strongest at the edge, where the shape is read.
  float warp = vnoise3(vec3(v_uv * 0.9 + vec2(seed * 3.1), seed * 7.0 + age * 0.35)) - 0.5;
  n = clamp(n + warp * 0.55, 0.0, 1.6);
  float rr = r * (1.0 + 0.30 * warp);                 // the disc itself is dented in and out
  float disc = smoothstep(1.0, 0.05 + 0.25 * haze, rr) * (1.0 - 0.35 * rr);
  float spr = 1.0, sprlum = 0.5; vec2 sgrad = vec2(0.0);
  if (u_has_fx.x > 0.5 && haze < 0.5) {   // 0.22.13: the smoke sheet's cover is the puff's shape, its shading the volume
    vec2 suv = atlas_uv(v_uv * (1.0 + 0.25 * core), seed, 2.0, 2.0);
    vec4 sx = texture(u_fx_smoke, suv);
    spr = sx.a; sprlum = dot(sx.rgb, vec3(0.33));
    float du = 0.012;
    sgrad = vec2(texture(u_fx_smoke, suv + vec2(du, 0.0)).a - texture(u_fx_smoke, suv - vec2(du, 0.0)).a,
                 texture(u_fx_smoke, suv + vec2(0.0, du)).a - texture(u_fx_smoke, suv - vec2(0.0, du)).a);
    disc = disc * (0.25 + 0.95 * spr);
  }
  float thr = (0.30 - 0.12 * core + 0.05 * haze) + (0.45 - 0.2 * core) * age;   // erosion: ragged from birth, dissolving as the puff ages
  float dens = clamp((disc * (0.30 + 0.2 * core + 1.35 * n) - thr) * (2.2 + 1.0 * core - 1.2 * haze), 0.0, 1.0);
  if (dens <= 0.002) discard;
  // sphere normal in world space: the sun side is lit, the lee side falls to the ambient
  float nz = sqrt(max(1.0 - r * r, 0.0));
  vec3 tocam = normalize(u_campos - v_wpos);
  vec3 N = normalize(u_cam_right * (v_uv.x - sgrad.x * 2.5) + u_cam_up * (v_uv.y - sgrad.y * 2.5) + tocam * nz);   // the sheet's cover gradient bends the sphere normal: the billows of the sprite are lit as billows
  float wrap = dot(N, u_sun) * 0.5 + 0.5;
  float sh = shadow_at(v_wpos, N, max(dot(N, u_sun), 0.0), dist);
  vec3 albedo = srgb_to_linear(v_col);
  vec3 c = albedo * (u_ambsky * 0.85 + u_ambgnd * 0.35 + u_suncol * 0.65 * wrap * wrap * (0.4 + 0.6 * sh) * g_sun_occ);
  // back-lit rim: the sun through the thin edge of the cloud
  float back = pow(max(dot(-tocam, u_sun), 0.0), 6.0);
  c += u_suncol * albedo * back * (1.0 - dens) * 0.5 * smoothstep(0.4, 1.0, r);
  // self-shadow of the thick core: the noise's dark pockets read as depth (the haze has none)
  c *= mix(0.58 + 0.42 * smoothstep(0.0, 1.0, n * 1.4), 0.95, haze);
  if (u_has_fx.x > 0.5 && haze < 0.5) c *= 0.70 + 0.75 * sprlum;   // the sheet's own light and shade
  // the core is lit hard: a white cumulus in the sun with a bright crown (the reference frame)
  c += u_suncol * albedo * core * 0.45 * pow(wrap, 3.0) * sh * (0.6 + 0.4 * n);
  // soft particles: fade where the disc cuts through a hull or the shore
  float soft = 1.0;
  if (u_soft_on > 0.5) {
    float sd = texture(u_scene_depth, gl_FragCoord.xy / u_scene_size).r;
    float thick = lin_depth(sd, u_zrange) - lin_depth(gl_FragCoord.z, u_zrange);
    soft = clamp(thick / max(0.6 * v_soft, 0.05), 0.0, 1.0);
  }
  c = fog_apply(c, dist, u_mapness, skyl);
  fragColor = vec4(c, dens * alpha * soft);
}
