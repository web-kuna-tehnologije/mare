// Volumetric gunsmoke (gfx/particles.cc, 0.22.15, issue12 "add 3D volumetric smoke with shaders").
// A fullscreen pass after the billboards: the broadsides' smoke is a set of spherical density
// blobs (u_blob: centre + radius, u_blobp: density, age, seed, shade) that grow, drift with the
// wind and thin out. Each pixel's ray is clipped to the union of the blobs it crosses and to the
// opaque scene (the captured depth), then marched: the density is the blobs' falloff eroded by a
// 3D value noise that scrolls with age, lit by the sun through a short second march towards it
// (self-shadowing), with the sky's ambient and a back-lit rim. Front-to-back accumulation.
in vec2 v_uv; out vec4 fragColor;
uniform mat4 u_inv_vp; uniform vec3 u_campos; uniform sampler2D u_scene_depth; uniform float u_soft_on;
uniform vec4 u_blob[128]; uniform vec4 u_blobp[128]; uniform int u_nblob;
uniform float u_time; uniform vec3 u_sky; uniform float u_mapness; uniform vec3 u_wind; uniform float u_debug;
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
float fbm3(vec3 p) { float s = 0.0, a = 0.5; for (int i = 0; i < 3; ++i) { s += a * vnoise3(p); p = p * 2.11 + vec3(1.3, 2.1, 0.7); a *= 0.5; } return s; }
vec3 unproject(vec2 uv, float z) { vec4 h = u_inv_vp * vec4(uv * 2.0 - 1.0, z, 1.0); return h.xyz / h.w; }
// one blob's density at p (its falloff eroded by a scrolling 3D noise when `fine`)
float age_of(int i) { return u_blobp[i].y; }
float density_one(int i, vec3 p, float fine) {
  vec3 c = u_blob[i].xyz; float R = u_blob[i].w;
  vec3 q = p - c; float r2 = dot(q, q);
  if (r2 > R * R) return 0.0;
  float r = sqrt(r2) / R;
  float age = u_blobp[i].y, seed = u_blobp[i].z;
  float e = 1.0;
  if (fine > 0.5) {
// 0.22.19 (issue15 "less round"): the dent was 0.45 of a single noise field, which leaves a
// ball with a rough coat. A second, much lower-frequency lobe field pulls the whole silhouette
// out of round - that is what makes one puff tall and ragged and the next one squat - and the
// fine dent is half as deep again on top of it.
    vec3 np = q / R * 1.6 + vec3(seed * 13.0, age * 0.7 + seed * 3.0, seed * 7.0) + u_wind * (u_time * 0.06);
    float n = fbm3(np) * 0.75 + fbm3(np * 2.6 + 5.0) * 0.25;
    float lobe = fbm3(q / R * 0.55 + vec3(seed * 31.0, seed * 17.0, age * 0.20));
    r *= 1.0 + 0.70 * (n - 0.45) + 0.55 * (lobe - 0.5);             // never a sphere, and never the same shape twice
    e = clamp((n * 1.6 + 0.25 - 0.45 * age) * (1.5 - 0.5 * age), 0.0, 1.0);
  }
// 0.22.19 (issue15 "fade from end to middle"): a puff used to thin everywhere at once, so it
// kept its size and went transparent - a ghost of the original ball. Smoke does the opposite:
// the thin outside is carried off and mixed away first and the dense middle is the last of it
// to go. `core` is the fraction of the ball still holding smoke, shrinking with age, and the
// radius is measured against *that*, so the cloud is eaten from its rim inwards and the last
// of it is a small dense knot where the heart of the puff was.
  float core = 1.0 - 0.62 * age * age;
  r /= max(core, 0.22);
  if (r >= 1.0) return 0.0;
  float shape = (1.0 - r * r); shape *= shape;
  return u_blobp[i].x * shape * e;
}
void main(){
  vec3 p0 = unproject(v_uv, -1.0), p1 = unproject(v_uv, 1.0);
  vec3 dir = normalize(p1 - p0);
  float tmax = 1e9;
  if (u_soft_on > 0.5) { float sd = texture(u_scene_depth, v_uv).r; if (sd < 0.9999) tmax = distance(u_campos, unproject(v_uv, sd * 2.0 - 1.0)); }
  vec3 skyl = srgb_to_linear(u_sky);
  float jitter = fract(sin(dot(gl_FragCoord.xy, vec2(12.9898, 78.233))) * 43758.5453);
// Every blob the ray crosses is marched along its own chord (six samples), and the samples are
// accumulated order-free: the optical depths add, the colour is the extinction-weighted mean.
// Powder smoke is one colour, so the order the puffs come in hardly shows, and nothing needs
// sorting. The sun reaches a sample through two more taps of its own blob towards the sun.
  const int M = 6; const float sigma = 0.30;   // extinction per unit density and local unit: a fresh core is ~80% opaque through its middle, the haze a veil
  float tau = 0.0; vec3 csum = vec3(0.0); float wsum = 0.0; int hit = 0;
  for (int i = 0; i < u_nblob; ++i) {
    vec3 oc = u_campos - u_blob[i].xyz; float R = u_blob[i].w;
    float b = dot(oc, dir), c = dot(oc, oc) - R * R, h = b * b - c;
    if (h < 0.0) continue;
    h = sqrt(h); float ta = max(-b - h, 0.0), tb = min(-b + h, tmax);
    if (tb <= ta) continue;
    hit = 1;
    float dt = (tb - ta) / float(M), t = ta + dt * jitter;
    for (int k = 0; k < M; ++k) {
      vec3 p = u_campos + dir * t;
      float d = density_one(i, p, 1.0);
      if (d > 0.002) {
        float occ = density_one(i, p + u_sun * (0.6 * R), 0.0) + density_one(i, p + u_sun * (1.2 * R), 0.0);
        float tsun = exp(-occ * 0.9 * R) * cloud_shadow(p, u_sun);
        float shade = u_blobp[i].w;
        vec3 albedo = vec3(shade, shade * 0.99, shade * 0.98);
        vec3 col = albedo * (u_ambsky * 0.85 + u_ambgnd * 0.25 + u_suncol * 0.38 * tsun * (0.55 + 0.45 * (1.0 - age_of(i))));   // the sun at a bit over a third: the billboards' wrap lighting landed about here, and the tonemap keeps the white
        float back = pow(max(dot(-dir, u_sun), 0.0), 5.0);
        col += albedo * u_suncol * back * 0.18 * exp(-d * 1.5);
        col = fog_apply(col, t, u_mapness, skyl);
        float w = d * sigma * dt;
        tau += w; csum += col * w; wsum += w;
      }
      t += dt;
    }
  }
  if (u_debug > 0.5 && u_debug < 1.5) { if (hit == 0) discard; fragColor = vec4(1.0, 0.1, 0.1, 0.5); return; }   // MARE_VOLSMOKE_DEBUG=1: the blobs' spheres
  if (hit == 0) discard;
  float alpha = 1.0 - exp(-tau);
  if (u_debug > 3.5) { fragColor = vec4(alpha, 0.0, 1.0, 1.0); return; }   // =4: the marched alpha, opaque
  if (alpha < 0.004 || wsum <= 0.0) discard;
  fragColor = vec4(csum / wsum, alpha);
}
