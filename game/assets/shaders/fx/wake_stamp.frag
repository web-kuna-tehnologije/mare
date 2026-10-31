// What a hull leaves on the water, painted once a frame at the place she is now (gfx/wake.cc).
// The map holds the history, so this only has to describe the *fresh* disturbance:
//   R  foam - the churn boiling up astern, the two Kelvin arm roots, the cut-water
//   G  disturbance - the water she has broken, which the sea's vertex stage flattens
// Both are painted with alpha blending towards white, at a rate the caller scales by dt, so a
// ship sitting still saturates her own patch and a ship under way lays a continuous band.
in vec2 v_local;        // along (+ forward), across, in hull lengths from the box centre
uniform vec4 u_ship;    // hull length, speed 0..1, paint rate this frame, unused
out vec4 fragColor;
float h2(vec2 p){ return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float n2(vec2 p){ vec2 i = floor(p), f = fract(p); f = f * f * (3.0 - 2.0 * f);
  return mix(mix(h2(i), h2(i + vec2(1,0)), f.x), mix(h2(i + vec2(0,1)), h2(i + vec2(1,1)), f.x), f.y); }
void main(){
// the box centre sits 0.45 L abaft the stem, so the hull runs from along = +0.45 .. -0.55
  float along = v_local.x + 0.45, across = abs(v_local.y);
  float spd = u_ship.y;
  float astern = -along;                       // 0 at the stem, 1 a hull length behind it
  float foam = 0.0, dist = 0.0;
// the cut-water: a tight wedge of white at the stem, where the hull is opening the sea
  float stem = exp(-pow((along - 0.45) / 0.09, 2.0) - pow(across / 0.10, 2.0));
  foam += stem * 0.85;
// along the forward shoulders: the water pushed aside breaks on the planking
  float shoulder = smoothstep(-0.05, 0.10, along) * (1.0 - smoothstep(0.30, 0.46, along))
                 * exp(-pow((across - 0.17) / 0.06, 2.0));
  foam += shoulder * 0.8;
  if (astern > 0.0) {
// the screw-less churn under the counter: a wide boil right astern that is the brightest
// part of the whole wake, fading over about half a hull length
    float boil = exp(-astern / 0.42) * (1.0 - smoothstep(0.0, 0.11 + 0.12 * astern, across));
    foam += boil * 0.95;
// the two diverging arms, at 19.5 degrees, started here and left behind by the map
    float arm = abs(across - astern * 0.354), armw = 0.055 + 0.05 * astern;
    foam += exp(-(arm * arm) / (armw * armw)) * exp(-astern / 0.7) * 0.55;
// the flattened lane between them: not foam, but water she has already broken
    dist += (1.0 - smoothstep(0.0, 0.16 + 0.18 * astern, across)) * exp(-astern / 0.8);
  }
// broken up so the trail is never a smooth airbrushed band
  vec2 np = vec2(along * 7.0, v_local.y * 7.0);
  float brk = 0.55 + 0.75 * n2(np * 1.7) * (0.6 + 0.4 * n2(np * 5.3));
  foam *= brk;
  float a = clamp(max(foam, dist * 0.8) * spd, 0.0, 1.0) * u_ship.z;
  if (a < 0.002) discard;
  fragColor = vec4(clamp(foam * spd, 0.0, 1.0), clamp(dist * spd, 0.0, 1.0), 0.0, a);
}
