// Tangent-space normal mapping (detail.c textures). The procedural meshes carry no UVs, so
// nmap_triplanar perturbs the flat normal by sampling the grain map along the three world axes,
// weighted by the normal; the sea has a trivial tangent frame (a flat y-up plane).
vec3 nmap_decode(vec3 t) { return t * 2.0 - 1.0; }
// UDN-style triplanar: add each projection's tangent-space xy, swizzled onto its world 
// plane and weighted by the normal, then renormalize
vec3 nmap_triplanar(sampler2D tex, vec3 wpos, vec3 N, float tiling, float strength) {
  vec3 w = abs(N); w = w*w*w*w; w /= (w.x + w.y + w.z);
  vec3 tx = nmap_decode(texture(tex, wpos.zy * tiling).rgb);
  vec3 ty = nmap_decode(texture(tex, wpos.xz * tiling).rgb);
  vec3 tz = nmap_decode(texture(tex, wpos.xy * tiling).rgb);
  vec3 d = vec3(0.0, tx.y, tx.x) * w.x + vec3(ty.x, 0.0, ty.y) * w.y + vec3(tz.x, tz.y, 0.0) * w.z;
  return normalize(N + d * strength);
}
