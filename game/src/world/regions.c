// Region generation (see regions.h). Sections: coast segmentation · land assignment (multi-source
// BFS) · naming · resources (climate potentials + rare deposits + pins) · port attachment.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "regions.h"
#include "world.h"
#include "../core/util.h"
#include "../core/balance.h"
#include "../core/diag.h"
#include "../sim/settlement.h"

unsigned char *regionmap, *devmap;

static const struct { const char *name; float lon, lat; } region_name_pins[] = {
    {"Flanders",4,51.5f},{"Holland",5,52.7f},{"Thames Valley",0,51.5f},{"Cornwall",-4.5f,50.5f},{"Brittany",-3.5f,48},{"Aquitaine",-1,45},{"Andalusia",-6,37},{"Catalonia",2,41.5f},
    {"Provence",5.5f,43.5f},{"Liguria",9,44.3f},{"Campania",14.5f,40.8f},{"Veneto",12.5f,45.4f},{"Thrace",28.5f,41},{"Nile Delta",30.5f,31},{"Barbary Coast",3,36.8f},{"Algarve",-8.5f,37.5f},
    {"Galicia",-8.5f,43},{"Jutland",10,56},{"Pomerania",16,54.2f},{"Svealand",18,59.5f},{"Livonia",24,57},{"Norway",6,60},{"Muscovy Coast",40,64.5f},{"Scotland",-3,56.5f},{"Ireland",-7,53},
    {"Tidewater",-76.5f,37.2f},{"New England",-71,42.2f},{"Acadia",-65,44.7f},{"St Lawrence",-71,46.8f},{"Newfoundland",-53,47.5f},{"Carolinas",-79.5f,33},{"Florida",-81,29},{"Gulf Coast",-90,29.5f},
    {"Mexico",-96,19.5f},{"Yucatan",-90,20},{"Honduras",-86,15.5f},{"Darien",-79.5f,9},{"Spanish Main",-75,10.5f},{"Cuba",-80,22.5f},{"Hispaniola",-70.5f,19},{"Guiana",-55,6},{"Pernambuco",-35,-8},
    {"Bahia",-38.5f,-13},{"Rio Coast",-43,-23},{"Plate Estuary",-57,-34.5f},{"Peru",-77,-12},{"Chile",-71.5f,-33},{"Gold Coast",-1,5},{"Senegambia",-17,14.5f},{"Angola",13,-9},{"Cape",18.5f,-34},
    {"Swahili Coast",39.5f,-5},{"Hejaz",39,21.5f},{"Yemen",43.5f,13},{"Levant",35,33},{"Anatolia",27,38.5f},{"Morocco",-6.5f,34.5f},{"Canaries",-15.5f,28},{"Hudson Bay",-92,57},{"Labrador",-60,55},
};
static const struct { float lon, lat; int good; float amount; } deposits[] = {   // historical pins
    {-65.7f,-19.6f,G_SILVER,2.0f},{-102.6f,22.8f,G_SILVER,1.6f},{-43.5f,-20.5f,G_GOLD,1.5f},{-1.5f,6.0f,G_GOLD,1.0f},{15.6f,60.6f,G_COPPER,1.5f},
    {-2.5f,43.2f,G_IRON,1.2f},{16,60,G_IRON,1.0f},{-1.6f,55,G_COAL,1.8f},{4.4f,50.5f,G_COAL,1.2f},{8.5f,49,G_IRON,0.8f},{-99,23.5f,G_SILVER,1.0f},{-76,40.5f,G_IRON,0.6f},{-80,40,G_COAL,1.0f},
};

int region_at(float wx, float wz) {
    int cx = lon2cx(x2lon(wx)) / TERR_DIV, cy = lat2cy(z2lat(wz)) / TERR_DIV;
    if (cx < 0 || cy < 0 || cx >= TERR_W || cy >= TERR_H) return -1;
    return (int)regionmap[cy*TERR_W+cx] - 1;
}
const char *dev_tier_name(float d) { return d >= 3 ? "developed" : d >= 2 ? "settled" : d >= 1 ? "frontier" : "wilderness"; }

static int is_coast(int x, int y) {
    if (!landmask[(y*TERR_DIV+TERR_DIV/2)*LM_W + x*TERR_DIV+TERR_DIV/2]) return 0;
    for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) { int xx = x+dx, yy = y+dy; if (xx < 0 || yy < 0 || xx >= TERR_W || yy >= TERR_H) continue;
        if (!landmask[(yy*TERR_DIV+TERR_DIV/2)*LM_W + xx*TERR_DIV+TERR_DIV/2]) return 1; }
    return 0;
}

static region_t tmp_regions[MAX_REGIONS];
void regions_generate(unsigned seed, int write_state) {
    double t0 = diag_now();
    region_t *saved = 0; static region_t backup[MAX_REGIONS]; if (!write_state) { memcpy(backup, G.regions, sizeof backup); saved = backup; }
    free(regionmap); free(devmap); regionmap = calloc(TERR_W*TERR_H, 1); devmap = calloc(TERR_W*TERR_H, 1);
    int N = TERR_W*TERR_H; int *queue = malloc(N * sizeof(int));
    unsigned char *land = calloc(N, 1); for (int i = 0; i < N; ++i) land[i] = landmask[((i/TERR_W)*TERR_DIV+TERR_DIV/2)*LM_W + (i%TERR_W)*TERR_DIV+TERR_DIV/2];
    // 1. coast segments: flood along the coast until the segment spans REGION_SEGMENT_DEG
    float seg = REGION_SEGMENT_DEG; int nreg;
    for (int attempt = 0; attempt < 6; ++attempt, seg *= 1.3f) {
        memset(regionmap, 0, N); nreg = 0; int span = (int)(seg * CELLS_PER_DEG / TERR_DIV);
        for (int start = 0; start < N && nreg < MAX_REGIONS; ++start) {
            if (regionmap[start] || !is_coast(start % TERR_W, start / TERR_W)) continue;
            int id = ++nreg, qh = 0, qt = 0, minx = start % TERR_W, maxx = minx, miny = start / TERR_W, maxy = miny;
            queue[qt++] = start; regionmap[start] = id;
            while (qh < qt) { int c = queue[qh++], x = c % TERR_W, y = c / TERR_W;
                for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) { int xx = x+dx, yy = y+dy;
                    if (xx < 0 || yy < 0 || xx >= TERR_W || yy >= TERR_H) continue;
                    int n = yy*TERR_W+xx;
                    if (regionmap[n] || !is_coast(xx, yy)) continue;
                    if (maxi(maxx, xx) - mini(minx, xx) > span || maxi(maxy, yy) - mini(miny, yy) > span) continue;
                    regionmap[n] = id; queue[qt++] = n; minx = mini(minx, xx); maxx = maxi(maxx, xx); miny = mini(miny, yy); maxy = maxi(maxy, yy); } }
        }
        if (nreg < MAX_REGIONS) break;
    }
    // 2. every land cell joins the nearest segment by land distance (multi-source BFS)
    { int qh = 0, qt = 0; for (int i = 0; i < N; ++i) if (regionmap[i]) queue[qt++] = i;
      while (qh < qt) { int c = queue[qh++], x = c % TERR_W, y = c / TERR_W;
        for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) { int xx = x+dx, yy = y+dy; if (xx < 0 || yy < 0 || xx >= TERR_W || yy >= TERR_H) continue; int n = yy*TERR_W+xx;
            if (land[n] && !regionmap[n]) { regionmap[n] = regionmap[c]; queue[qt++] = n; } } } }
    // 3. region records: centroid, cells, resources
    G.nregions = nreg;
    for (int r = 0; r < nreg; ++r) { region_t *R = &G.regions[r]; memset(R, 0, sizeof *R); R->alive = 1; R->prosperity = 1; R->nhub = 0; }
    float *acc = calloc((size_t)nreg * MAX_GOODS, sizeof(float));
    for (int i = 0; i < N; ++i) { int r = (int)regionmap[i] - 1; if (r < 0) continue; region_t *R = &G.regions[r];
        float lon = cx2lon((i%TERR_W)*TERR_DIV + TERR_DIV/2), lat = cy2lat((i/TERR_W)*TERR_DIV + TERR_DIV/2);
        R->ncells++; R->cx += lon2x(lon); R->cz += lat2z(lat);
        if (R->ncells % 3 == 1) {   // sample every third cell: climate potentials
            float pot[MAX_GOODS]; site_potential(lon, lat, pot); for (int g = 0; g < MAX_GOODS; ++g) acc[r*MAX_GOODS+g] += pot[g];
            // rare deposits from noise: iron (temperate uplands), coal (basins), copper (hills), silver/gold (mountains)
            float h = world_height(lon2x(lon), lat2z(lat)), a = fabsf(lat);
            float n1 = world_fbm(lon*0.21f+3, lat*0.21f+11, 3, seed+501), n2 = world_fbm(lon*0.09f+40, lat*0.09f+7, 3, seed+502), n3 = world_fbm(lon*0.3f+9, lat*0.3f+21, 3, seed+503);
            if (h > 1.5f && h < 6.5f && a > 38 && a < 66 && n1 > 0.62f) acc[r*MAX_GOODS+G_IRON] += (n1 - 0.62f) * 6;
            if (h < 3.5f && a > 35 && a < 60 && n2 > 0.64f) acc[r*MAX_GOODS+G_COAL] += (n2 - 0.64f) * 8;
            if (h > 2 && n3 > 0.7f) acc[r*MAX_GOODS+G_COPPER] += (n3 - 0.7f) * 5;
            if (h > 5 && a < 40 && n1 > 0.58f) acc[r*MAX_GOODS+G_SILVER] += (n1 - 0.58f) * 4;
            if (h > 5 && a < 25 && n3 > 0.74f) acc[r*MAX_GOODS+G_GOLD] += (n3 - 0.74f) * 4;
        } }
    for (int r = 0; r < nreg; ++r) { region_t *R = &G.regions[r]; if (!R->ncells) { R->alive = 0; continue; }
        R->cx /= R->ncells; R->cz /= R->ncells; float samples = (R->ncells + 2) / 3.f, sizef = clampf(R->ncells / 40.f, 0.4f, 2.5f);
        for (int g = 0; g < MAX_GOODS; ++g) { float v = acc[r*MAX_GOODS+g] / samples * sizef; R->potential[g] = v < 0.25f ? 0 : minf(v, 2.5f); }
        snprintf(R->name, sizeof R->name, "Coast %d", r + 1); }
    for (unsigned k = 0; k < sizeof deposits / sizeof deposits[0]; ++k) { int r = region_at(lon2x(deposits[k].lon), lat2z(deposits[k].lat)); if (r >= 0) G.regions[r].potential[deposits[k].good] = minf(2.5f, G.regions[r].potential[deposits[k].good] + deposits[k].amount); }
    // 4. names: pins first, then "<port> Hinterland" in regions_attach_ports
    for (unsigned k = 0; k < sizeof region_name_pins / sizeof region_name_pins[0]; ++k) { int r = region_at(lon2x(region_name_pins[k].lon), lat2z(region_name_pins[k].lat)); if (r >= 0 && !strncmp(G.regions[r].name, "Coast", 5)) snprintf(G.regions[r].name, NAME_LEN, "%s", region_name_pins[k].name); }
    free(acc); free(queue); free(land);
    int rare = 0; for (int r = 0; r < nreg; ++r) for (int g = G_IRON; g <= G_GOLD; ++g) rare += G.regions[r].potential[g] > 0;
    if (saved) memcpy(G.regions, saved, sizeof backup);   // loaded state wins; only the maps were rebuilt
    (void)tmp_regions;
    LOG_INFO("regions", "%d land regions generated in %.1f ms (segment %.1f deg, %d rare deposits)", nreg, (diag_now()-t0)*1000, seg, rare);
}

void regions_attach_ports(void) {
    for (int r = 0; r < G.nregions; ++r) G.regions[r].nhub = 0;
    for (int i = 0; i < G.nports; ++i) { port_t *p = &G.ports[i]; p->hinterland = -1; p->hub_share = 0;
        if (port_is_gone(p)) continue;
        int r = region_at(p->lx, p->lz); if (r < 0) r = region_at(p->x, p->z);
        if (r < 0) continue;
        p->hinterland = r; region_t *R = &G.regions[r];
        if (p->size > 0 && R->nhub < MAX_REGION_PORTS) { R->hub[R->nhub++] = i; }
        else if (p->size > 0) { int w = 0; for (int k = 1; k < R->nhub; ++k) if (G.ports[R->hub[k]].pop < G.ports[R->hub[w]].pop) w = k; if (G.ports[R->hub[w]].pop < p->pop) R->hub[w] = i; }
        if (!strncmp(R->name, "Coast", 5) && p->size > 0) snprintf(R->name, NAME_LEN, "%.14s Hinterland", p->name); }
    for (int r = 0; r < G.nregions; ++r) { region_t *R = &G.regions[r]; for (int k = 0; k < R->nhub; ++k) G.ports[R->hub[k]].hub_share = 1.f / R->nhub; }
}

void regions_refresh_devmap(void) {
    for (int i = 0; i < TERR_W*TERR_H; ++i) { int r = (int)regionmap[i] - 1; devmap[i] = r >= 0 ? (unsigned char)clampf(G.regions[r].development * 80, 0, 255) : 0; }
    if (territory_dirty < 2) territory_dirty = 1;
}
