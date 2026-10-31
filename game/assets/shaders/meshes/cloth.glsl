  float cm = a_rm.y * 32.0; float cwq = floor(cm + 1e-4); float cw = cwq / 31.0; float cu = fract(cm + 1e-4) * 2.0 - 1.0;
  float isc = i_fx.y > 0.5 ? 1.0 : 0.0; vec3 cnrm = a_nrm;
  if (isc > 0.5 && cwq > 0.5) {
    float wxs = i_fx.z * c - i_fx.w * s, wzs = i_fx.z * s + i_fx.w * c;
// the wind in the ship's frame: along (+ = from astern), across (+ = to starboard)
    float wstr = length(i_fx.zw);
    float way = clamp(i_misc.x, 0.0, 1.2) * 0.5;
// how much way she has on: moored 0.12, under sail 0.3-0.5
    float hw = a_rm.x * i_scol.x;
    float ph0 = i_pos.x * 0.7 + i_pos.z * 0.5;
    if (a_mat > 0.5) {
// a flag: this vertex sits cw of the way along a ribbon of length hw that the mesh trails 
// aft; swing it to the apparent wind (her own way adds a head wind), droop it in a light 
// air, ripple it along its length
      vec2 aw = vec2(wxs - way * 0.8, wzs); float al = length(aw);
      vec2 fd = al > 1e-3 ? aw / al : vec2(-1.0, 0.0);
      float L = hw * cw, lift = clamp(al / 0.6, 0.0, 1.0);
      vec3 side = vec3(-fd.y, 0.0, fd.x);
      float fph = u_time * (4.0 + 6.0 * al) - cw * 9.0 + ph0;
      p.x += L * (fd.x + 1.0); p.z += L * fd.y;
      p.y -= L * 0.6 * (1.0 - lift) * cw;
      p += side * (L * 0.10 * sin(fph) * lift) + vec3(0.0, L * 0.05 * sin(fph * 1.3 + 1.0), 0.0);
      cnrm = normalize(side + vec3(0.0, 0.35 * cos(fph), 0.0));
    } else {
// a sail: pressure is the wind through the canvas. A square sail (normal along the hull) 
// also draws from the ship's own way, so a ship under sail is never shown with slack 
// canvas; a lateen is sheeted to one side and so always holds a little shape
      float press = wxs * cnrm.x + wzs * cnrm.z;
      float fill = abs(cnrm.x) > 0.5 ? press + way * 0.9 : (abs(press) < 0.15 ? 0.15 : press);
      float mag = clamp(abs(fill), 0.0, 1.4), dir = fill < 0.0 ? -1.0 : 1.0;
      float fn = 0.55 + 0.55 * min(mag, 1.0);   // 0.22.4: more wind in the canvas
// 0.22: a drawing sail is never flat. Even in a light air the canvas holds its round, which is
// what the reference sheet (assets/fleet_sails.png) shows and what the old 0.3 floor missed.
      float prof = sin(3.14159 * min(cw * 0.82, 1.0));
// zero at the yard, fullest two thirds down, held at the foot
      float across = 0.35 + 0.65 * cos(cu * 1.5708);
// the leeches are held by their bowlines
      float belly = hw * 0.62 * fn * prof * across;
// fuller than the old 0.28: the reference sheet's canvas is round with wind
      if (dir < 0.0) {
// Aback - the wind blows straight onto the ship: the canvas is thrown back against its own
// mast and yard and clings there. A square sail is stopped by the mast down its middle; a
// lateen flattens onto the lantina and the rig. The head (bent to the spar) stays pinned,
// only the free foot keeps a little movement.
        belly *= (abs(cnrm.x) > 0.5 ? 0.22 + 0.45 * abs(cu) : 0.28) * (0.30 + 0.70 * cw);
      }
      belly *= dir * (1.0 + 0.06 * sin(u_time * 1.3 + ph0));
      float luff = 1.0 - clamp(mag / 0.35, 0.0, 1.0);
      float fph = u_time * (3.0 + 4.0 * wstr) + cu * 6.0 + cw * 4.0 + ph0;
      float flut = hw * (0.012 + 0.06 * luff) * cw * sin(fph);
      if (dir < 0.0) flut *= 2.2;
// aback canvas shivers against the rig
      p += cnrm * (belly + flut);
      p.y += hw * 0.01 * cw * sin(fph * 1.31 + 2.2);
      vec3 ct = vec3(-cnrm.z, 0.0, cnrm.x);
// across the panel, the way cu grows
      cnrm = normalize(cnrm - vec3(0.0, 0.45 * dir * fn * cos(3.14159 * cw * 0.82) * across, 0.0) + ct * (0.6 * dir * fn * sin(cu * 1.5708) * prof));
    }
  }
