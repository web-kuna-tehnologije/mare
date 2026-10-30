// Cook-Torrance GGX + Smith + Schlick, Lambert diffuse. Roughness and metalness are linear
// 0..1 inputs; albedo must already be linear.
const float PBR_PI = 3.14159265;
vec3 pbr_direct(vec3 albedo, vec3 N, vec3 V, vec3 L, float rough, float metal, vec3 lightcol) {
  float NdotL = max(dot(N, L), 0.0); if (NdotL <= 0.0) return vec3(0.0);
  vec3 H = normalize(V + L);
  float NdotV = max(dot(N, V), 1e-4), NdotH = max(dot(N, H), 0.0), VdotH = max(dot(V, H), 0.0);
  float a = max(rough * rough, 1e-3), a2 = a * a;
  float den = NdotH * NdotH * (a2 - 1.0) + 1.0;
  float D = a2 / max(PBR_PI * den * den, 1e-7);
  float k = a * 0.5;
  float G = (NdotV / (NdotV * (1.0 - k) + k)) * (NdotL / (NdotL * (1.0 - k) + k));
  vec3 F0 = mix(vec3(0.04), albedo, metal);
  vec3 F = F0 + (1.0 - F0) * pow(1.0 - VdotH, 5.0);
  vec3 spec = (D * G) * F / max(4.0 * NdotV * NdotL, 1e-5);
  vec3 diff = (1.0 - F) * (1.0 - metal) * albedo / PBR_PI;
  return (diff + spec) * lightcol * NdotL;
}
vec3 pbr_ambient(vec3 albedo, vec3 N, float metal, vec3 sky, vec3 ground) {
  vec3 amb = mix(ground, sky, N.y * 0.5 + 0.5);
  return albedo * amb * (1.0 - 0.5 * metal);
}
