in vec3 v_wpos; in vec2 v_uv; in float v_h;
uniform sampler2D u_hm; uniform sampler2D u_terr; uniform vec2 u_texel; uniform float u_texel_world;
uniform vec3 u_campos; uniform float u_mapness; uniform vec3 u_sky; uniform float u_k;
uniform sampler2D u_ao; uniform float u_ao_on;
// baked horizon occlusion (terrain_init): 1 = open sky, lower in valleys
uniform vec3 u_nation_colors[11]; uniform float u_daylight; uniform float u_regions; uniform vec2 u_terr_texel;
uniform float u_flatten;
uniform sampler2D u_atlas; uniform float u_has_atlas;
// ground material atlas (assets/terrain/terrain_textures.png, 6x3 cells)
uniform sampler2D u_grain;
// the procedural surface-grain normal map (detail.c): micro relief on the close ground
#include "terrain/detail.glsl"
#include "terrain/atlas.glsl"
out vec4 fragColor;
// One cell of the atlas, tiled: the uv wraps inside the cell by hand (an atlas cannot use
// the sampler's repeat) and the derivatives come from the *unwrapped* coordinate, so the
// mip level is continuous across the wrap and the seam does not show. Cells are inset a
// little to keep the neighbour's texels out of the bilinear footprint.
vec3 atlas_cell(int cell, vec2 p) {
  vec2 f = fract(p);
  vec2 cuv = (vec2(float(cell - (cell / 6) * 6), float(cell / 6)) + 0.012 + f * 0.976) / vec2(6.0, 3.0);
  vec2 ddx = dFdx(p) * (0.976 / 6.0), ddy = dFdy(p) * (0.976 / 6.0);
  return textureGrad(u_atlas, cuv, ddx, ddy).rgb;
}
float hash(vec2 p){ return fract(sin(dot(p, vec2(127.1,311.7)))*43758.5453); }
float noise(vec2 p){ vec2 i=floor(p), f=fract(p); f=f*f*(3.0-2.0*f);
  return mix(mix(hash(i),hash(i+vec2(1,0)),f.x), mix(hash(i+vec2(0,1)),hash(i+vec2(1,1)),f.x), f.y); }
#include "common/color.glsl"
#include "common/pbr.glsl"
#include "common/light.glsl"
#include "common/fog.glsl"
#include "common/nmap.glsl"
void main(){
  float hl = texture(u_hm, v_uv - vec2(u_texel.x,0)).r, hr = texture(u_hm, v_uv + vec2(u_texel.x,0)).r;
  float hu = texture(u_hm, v_uv - vec2(0,u_texel.y)).r, hd = texture(u_hm, v_uv + vec2(0,u_texel.y)).r;
  float gx = (hr-hl)/(2.0*u_texel_world), gz = (hd-hu)/(2.0*u_texel_world);
// The streamed detail moves the surface, so it has to light it too: without this the relief
// the rings add is shaded by the base map's normals and reads as flat painted ground.
  float dk = 1.0 - u_flatten;
  if (dk > 0.01) {
    float e = det_step(v_wpos.xz);
    gx += dk * (det_at(v_wpos.xz + vec2(e,0.0)) - det_at(v_wpos.xz - vec2(e,0.0))) / (2.0*e);
    gz += dk * (det_at(v_wpos.xz + vec2(0.0,e)) - det_at(v_wpos.xz - vec2(0.0,e))) / (2.0*e);
// and the atlas residual, differentiated at the fine set's own step: without this the shape
// the vertex stage just added would be lit by the ring's smooth normals and read as painted
    if (u_ha_on.x > 0.5) {
      float ea = max(u_ha_on.y * 0.25, 0.004);
// faded out at the waterline exactly as ha_shape stops the displacement there, so the
// lighting follows the surface the vertex stage actually built
      float wk = dk * smoothstep(0.0, 0.22, abs(v_h));
      gx += wk * (ha_detail(v_wpos.xz + vec2(ea,0.0)) - ha_detail(v_wpos.xz - vec2(ea,0.0))) / (2.0*ea);
      gz += wk * (ha_detail(v_wpos.xz + vec2(0.0,ea)) - ha_detail(v_wpos.xz - vec2(0.0,ea))) / (2.0*ea);
    }
  }
  vec3 n = normalize(vec3(-gx, 1.0, -gz));
  float h = v_h; float lat = -v_wpos.z / u_k; float lon = v_wpos.x / u_k;
  float slope = 1.0 - n.y;
  float nz = noise(v_wpos.xz*0.9) * 0.6 + noise(v_wpos.xz*3.7)*0.4;
  vec3 sand  = vec3(0.78,0.72,0.52);
  vec3 grass = mix(vec3(0.33,0.50,0.22), vec3(0.22,0.38,0.16), nz);
  vec3 dry   = mix(vec3(0.55,0.50,0.32), vec3(0.45,0.42,0.28), nz);
  vec3 rock  = vec3(0.46,0.43,0.40);
  vec3 snow  = vec3(0.93,0.94,0.96);
  vec3 desert= mix(vec3(0.86,0.74,0.48), vec3(0.80,0.66,0.42), nz);
  vec3 tundra= mix(vec3(0.55,0.58,0.48), vec3(0.70,0.72,0.68), nz);
// Mediterranean scrub: between the olive belt and the Alps the lowland green goes dry and
// silvery (the reference art's hills are olive, not Irish). Cliffs come in earlier and harder
// than they used to: the ridged relief is steep, and rock is what a steep face shows.
  float med = smoothstep(32.0, 35.5, abs(lat)) * (1.0 - smoothstep(44.5, 47.5, abs(lat))) * smoothstep(-12.0, -6.0, lon) * (1.0 - smoothstep(40.0, 44.0, lon));
  vec3 scrub = mix(vec3(0.42,0.50,0.27), vec3(0.55,0.53,0.33), nz);
  float wgrass = smoothstep(0.15, 0.6, h), wdry = smoothstep(2.5, 5.0, h), wrock = smoothstep(0.17, 0.42, slope), wsnow = smoothstep(6.5, 8.5, h) * (1.0 - smoothstep(0.55, 0.75, slope));
  vec3 c = mix(sand, mix(grass, scrub, med * 0.6), wgrass);
  c = mix(c, vec3(0.30, 0.40, 0.36), (1.0 - smoothstep(-1.4, 0.0, h)) * 0.9);
// the sea floor: pale sand at the beach, dark silt and weed where it drops away
  c = mix(c, dry, wdry);
  c = mix(c, rock, wrock);
  c = mix(c, rock * 0.72, (1.0 - smoothstep(0.0, 0.35, h)) * smoothstep(0.12, 0.3, slope) * 0.8);
// wet, dark rock at the waterline
  c = mix(c, snow, wsnow);
// deserts: sahara, arabia, northern mexico, atacama
  float des = 0.0;
  des = max(des, (1.0-smoothstep(30.0,33.0,lat)) * smoothstep(13.0,16.0,lat) * smoothstep(-18.0,-14.0,lon) * (1.0-smoothstep(36.0,40.0,lon)));
  des = max(des, smoothstep(14.0,18.0,lat)*(1.0-smoothstep(30.0,34.0,lat))*smoothstep(35.0,38.0,lon));
  des = max(des, smoothstep(22.0,25.0,lat)*(1.0-smoothstep(32.0,35.0,lat))*smoothstep(-116.0,-112.0,lon)*(1.0-smoothstep(-103.0,-100.0,lon)));
  des = max(des, smoothstep(-30.0,-26.0,lat)*(1.0-smoothstep(-16.0,-14.0,lat))*smoothstep(-72.0,-70.0,lon)*(1.0-smoothstep(-66.0,-64.0,lon)));
  c = mix(c, desert, des * smoothstep(0.12, 0.5, h) * (1.0 - smoothstep(5.0, 7.0, h)));
  float cold = smoothstep(52.0, 64.0, abs(lat));
  c = mix(c, tundra, cold * smoothstep(0.3,1.0,h));
  c = mix(c, snow, smoothstep(64.0, 70.0, abs(lat)) * 0.8);
// Textured ground (0.17): the atlas materials follow the same weights as the flat colours
// above, at two scales (fine repeats for the city and battle tiers, a coarse repeat that
// survives the regional view), triplanar for the rock so cliff faces are not smeared, and
// faded out with distance so the strategic map keeps its clean biome colours.
  float dist0 = distance(v_wpos, u_campos);
  float tdet = (1.0 - smoothstep(90.0, 420.0, dist0)) * u_has_atlas * (1.0 - u_mapness);
  if (tdet > 0.01) {
    vec3 tw = pow(abs(n), vec3(6.0)); tw /= (tw.x + tw.y + tw.z);
    vec2 pxz = v_wpos.xz * 1.6, mxz = v_wpos.xz * 0.21;
    float far = smoothstep(8.0, 45.0, dist0);
// fine -> coarse repeat with distance
    vec3 grassT  = mix(mix(atlas_cell(1, pxz), atlas_cell(13, pxz), 0.35), atlas_cell(1, mxz), far);
    vec3 scrubT  = mix(mix(atlas_cell(2, pxz), atlas_cell(14, pxz), 0.5), atlas_cell(11, mxz), far);
    vec3 sandT   = mix(atlas_cell(3, pxz), atlas_cell(10, mxz), far);
    vec3 dryT    = mix(mix(atlas_cell(9, pxz), atlas_cell(2, pxz), 0.4), atlas_cell(9, mxz), far);
    vec3 rockT   = atlas_cell(0, mix(pxz, mxz, far)) * tw.y + atlas_cell(0, mix(v_wpos.xy * 1.6, v_wpos.xy * 0.21, far)) * tw.z + atlas_cell(0, mix(v_wpos.zy * 1.6, v_wpos.zy * 0.21, far)) * tw.x;
    vec3 desertT = mix(atlas_cell(6, pxz), atlas_cell(5, mxz), 0.5 + 0.5 * far);
    vec3 tundraT = mix(atlas_cell(14, pxz), atlas_cell(16, mxz), 0.4 + 0.5 * far);
    vec3 t = mix(sandT, mix(grassT, scrubT, med * 0.7), wgrass);
    t = mix(t, dryT, wdry);
    t = mix(t, rockT, wrock);
    t = mix(t, rockT * 0.6, (1.0 - smoothstep(0.0, 0.35, h)) * smoothstep(0.12, 0.3, slope) * 0.8);
    t = mix(t, mix(rockT, vec3(0.9, 0.92, 0.95), 0.8), wsnow);
    t = mix(t, desertT, des * smoothstep(0.12, 0.5, h) * (1.0 - smoothstep(5.0, 7.0, h)));
    t = mix(t, tundraT, cold * smoothstep(0.3, 1.0, h));
    vec3 ts = linear_to_srgb(t);
// the atlas is sRGB-sampled (linear); the colour mix below is in sRGB
    c = mix(c, ts * (0.55 + 0.45 * (c / max(ts, vec3(0.02)))), 0.78 * tdet);
// keep the biome's hue, take the material's grain
  }
// Ground materials (0.19): each surface has its own roughness - grass and scrub matte, dry
// soil a little less, bare rock and shingle tighter, snow a soft gloss, and the rock in the
// splash zone wet and dark-glossy - and, close in, a micro-relief normal from the grain map
// so the beach and the rock faces stop reading as painted planes under a low sun.
  float wet = (1.0 - smoothstep(0.0, 0.35, h)) * smoothstep(0.12, 0.3, slope);
  float rough = 0.95;
  rough = mix(rough, 0.84, wdry);
  rough = mix(rough, 0.72, wrock);
  rough = mix(rough, 0.80, (1.0 - wgrass) * (1.0 - wrock));          // beach sand
  rough = mix(rough, 0.55, wsnow);
  rough = mix(rough, 0.40, wet * 0.8);
  float micro = (1.0 - smoothstep(12.0, 60.0, dist0)) * (1.0 - u_mapness);
  if (micro > 0.01) {
    float mstr = mix(0.25, 0.55, wrock) * micro;
    n = nmap_triplanar(u_grain, v_wpos * 1.3, n, 1.0, mstr);
  }
  float diff = max(dot(n, u_sun), 0.0);
  vec3 vdir = normalize(u_campos - v_wpos);
  float viewdist = distance(v_wpos, u_campos);
  float ao = u_ao_on > 0.5 ? mix(1.0, texture(u_ao, v_uv).r, 0.85) : 1.0;
  vec3 lit = sun_shade_ao(srgb_to_linear(c), n, vdir, v_wpos, rough, 0.0, viewdist, ao);
// map style
  vec4 tt = texture(u_terr, v_uv);
  int nid = int(tt.r * 255.0 + 0.5);
  float dev = tt.g * 255.0 / 80.0;
  float rid = tt.b, border = 0.0;
  if (u_regions > 0.5) { border += float(abs(texture(u_terr, v_uv + vec2(u_terr_texel.x,0)).b - rid) > 0.001); border += float(abs(texture(u_terr, v_uv + vec2(0,u_terr_texel.y)).b - rid) > 0.001); }
  vec3 parch = vec3(0.87,0.80,0.64) * (0.85 + 0.15*nz);
  vec3 owner = nid > 0 ? u_nation_colors[min(nid,10)] : parch;
  vec3 mapc = mix(parch, owner, nid > 0 ? 0.55 : 0.0);
  mapc *= 0.75 + 0.25 * diff;
  mapc = mix(mapc, mapc*0.45, smoothstep(6.0, 9.0, h)*0.6);
  float coastline = 1.0 - smoothstep(0.05, 0.28, h);
  mapc = mix(mapc, vec3(0.35,0.28,0.18), coastline*0.8);
  if (u_regions > 0.5) { vec3 devc = mix(vec3(0.55,0.50,0.40), vec3(0.95,0.85,0.45), clamp(dev/3.0,0.0,1.0)); mapc = mix(mapc, devc, 0.55); mapc = mix(mapc, vec3(0.15,0.10,0.05), min(border,1.0)*0.8); }
  vec3 col = mix(lit, srgb_to_linear(mapc) * u_daylight, u_mapness);
  col = fog_apply(col, viewdist, u_mapness, srgb_to_linear(u_sky));
  fragColor = vec4(col, 1.0);
}
