in vec2 v_uv; out vec4 fragColor;
uniform sampler2D u_depth; uniform mat4 u_inv_vp; uniform mat4 u_vp; uniform vec3 u_campos; uniform vec2 u_texel; uniform float u_on;
vec3 wpos_at(vec2 uv, float z) { vec4 w = u_inv_vp * vec4(uv * 2.0 - 1.0, z * 2.0 - 1.0, 1.0); return w.xyz / w.w; }
float hash(vec2 p) { return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453); }
void main(){
  float z = texture(u_depth, v_uv).r;
  if (u_on < 0.5 || z >= 0.99999) { fragColor = vec4(1.0); return; }
  vec3 P = wpos_at(v_uv, z);
  vec3 N = normalize(cross(dFdx(P), dFdy(P)));
  if (dot(N, u_campos - P) < 0.0) N = -N;
  float dist = distance(P, u_campos);
  if (dist > 220.0) { fragColor = vec4(1.0); return; }
  float R = 0.28 + 0.022 * dist;
  float rot = hash(v_uv * 1000.0) * 6.2831853;
  float occ = 0.0;
  for (int i = 0; i < 8; ++i) {
    float a = rot + float(i) * 0.7853982; float e = 0.35 + 0.6 * fract(float(i) * 0.618034 + hash(v_uv * 300.0 + float(i)));
    vec3 s = vec3(cos(a) * e, 0.0, sin(a) * e);
    vec3 up = abs(N.y) < 0.9 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 tx = normalize(cross(up, N)), tz = cross(N, tx);
    vec3 dir = tx * s.x + tz * s.z + N * (0.25 + 0.5 * e);
    vec3 Q = P + dir * R * (0.3 + 0.7 * float(i + 1) / 8.0);
    vec4 c = u_vp * vec4(Q, 1.0); vec2 uv2 = c.xy / c.w * 0.5 + 0.5;
    if (uv2.x < 0.0 || uv2.x > 1.0 || uv2.y < 0.0 || uv2.y > 1.0) continue;
    float z2 = texture(u_depth, uv2).r; vec3 Pq = wpos_at(uv2, z2);
    float dq = distance(Pq, u_campos), dQ = distance(Q, u_campos);
    float range = smoothstep(0.0, 1.0, R / max(abs(dq - dQ), 1e-4));
    occ += (dq < dQ - 0.03 * R ? 1.0 : 0.0) * range;
  }
  fragColor = vec4(vec3(1.0 - occ / 8.0), 1.0);
}
