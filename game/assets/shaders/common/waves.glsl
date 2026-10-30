// Gerstner wave field (gfx/sea.c, DESIGN_VISUAL.md step 4). Shared by the sea pass, which
// displaces its grid by them, and by the instanced mesh pass, which lifts and tilts every
// floating instance onto the same surface - otherwise ships would sail through the swell
// instead of riding it.
//
// The wave set is not authored here: sea.c derives it from the wind at the sea's centre -
// directions spread around the wind direction, wavelengths and heights growing with its
// strength - and uploads it as u_wave/u_wave_phase. Phase is *accumulated on the CPU* rather
// than computed as speed*time.
//
// Two sets, cross-faded by u_wave_blend. A wave set must never change continuously while it is
// on screen: a direction that rotates by even a degree sweeps its crests sideways by (distance
// from the phase origin) x (angle), which hundreds of units from the origin is faster than the
// waves travel - the "swell speeds up when I pan the camera" artefact. So when the wind at the
// look-at point has changed enough, sea.c builds a *new* static set and fades the old one out
// over several seconds; two static fields blending by amplitude move no crest anywhere.
// Pipelines using this append GLSL_WAVE_UNIFORMS (gfx/glsl.h) to their uniform list.
uniform vec4 u_wave[10];
// xy = unit direction, z = wavelength, w = steepness weight; 0-4 set A, 5-9 set B
uniform float u_wave_phase[10];
// accumulated phase, radians
uniform float u_wave_blend;
// 0 = set A only .. 1 = set B only
const float WAVE_STEEP = 0.026;
const float WAVE_PINCH = 0.55;
// Gerstner sum at world point p: returns the displacement, the two surface tangents (so 
// the caller gets an exact normal with no finite differences) and a crest factor. 
// `cell` is the world size of one grid cell at this vertex, 0 where the surface is not a 
// grid (a ship riding the swell). A wave shorter than a few cells cannot be represented by 
// that grid: sampling it anyway aliases it into a pattern that slides as the grid moves 
// with the camera. Those waves are faded out instead, so what is drawn is only what is 
// sampled.
vec3 wave_sum(vec2 p, float amp, float scale, float cell, out vec3 tan_x, out vec3 tan_z, out float crest) {
  vec3 d = vec3(0.0); tan_x = vec3(1.0, 0.0, 0.0); tan_z = vec3(0.0, 0.0, 1.0); crest = 0.0;
  if (amp <= 0.0) return d;
  float wsum = 1e-4;
  for (int i = 0; i < 10; ++i) {
    float setw = i < 5 ? 1.0 - u_wave_blend : u_wave_blend;
    if (setw <= 0.001) continue;
    vec2 dir = u_wave[i].xy;
    float len = u_wave[i].z * scale;
    float fade = cell > 0.0 ? smoothstep(2.0 * cell, 4.0 * cell, len) : 1.0;
    if (fade <= 0.001) continue;
    float k = 6.28318 / max(len, 1e-4);
    float w = u_wave[i].w * fade * setw;
    float a = w * amp * len * WAVE_STEEP;
    float ph = k * dot(dir, p) - u_wave_phase[i];
    float s = sin(ph), c = cos(ph);
// Gerstner pinch. q = 1 is the textbook trochoid, whose crests come to a point - which at 
// the amplitudes a running sea uses reads as rows of spikes rather than water. Holding it 
// well below 1 rounds the crests while keeping the forward lean that makes a swell look 
// like it is travelling.
    float q = min(u_wave[i].w, 1.0) * WAVE_PINCH;
    d.xz += dir * (q * a * c);
    d.y  += a * s;
    tan_x += vec3(-dir.x * dir.x * q * a * k * s, dir.x * a * k * c, -dir.x * dir.y * q * a * k * s);
    tan_z += vec3(-dir.x * dir.y * q * a * k * s, dir.y * a * k * c, -dir.y * dir.y * q * a * k * s);
    crest += max(s, 0.0) * w;
    wsum += w;
  }
  crest = clamp((crest / wsum - 0.58) * 3.7, 0.0, 1.0);
  return d;
}
float wave_depth_fade(sampler2D hm, vec2 p, vec4 geo, float wk, float scale, float local_depth) {
  float dep = local_depth;
  if (local_depth <= 0.0) {
    float lon = p.x / wk, lat = -p.y / wk;
    vec2 uv = vec2((lon - geo.x)/(geo.y-geo.x), (geo.w - lat)/(geo.w-geo.z));
    dep = max(-texture(hm, uv).r, 0.0);
  }
  float f = smoothstep(0.05 * scale, 1.6 * scale, dep);
  return f * f;
// squared: the real-bathymetry shelves (a few tenths of a unit) stay nearly calm, full swell
// needs genuinely deep water
}
