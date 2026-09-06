// Land regions (hinterlands): generated once from the coastline and the climate/deposit model.
// Coast cells are cut into ~REGION_SEGMENT_DEG segments; every land cell joins the nearest segment
// by land distance. Rare-good deposits come from noise plus historical pins. Deterministic per seed.

#ifndef MARE_REGIONS_H
#define MARE_REGIONS_H

#include "../core/types.h"

extern unsigned char *regionmap;   // TERR_W*TERR_H: region id + 1 (0 = water)
extern unsigned char *devmap;      // TERR_W*TERR_H: development * 80 (for the map view; refreshed by the sim)

void regions_generate(unsigned seed, int write_state);   // builds regionmap (+ G.regions when write_state; 0 after a load)
int  region_at(float wx, float wz);             // region index or -1
void regions_attach_ports(void);                // set port.region and region hubs (after ports exist)
void regions_refresh_devmap(void);              // copy development into devmap and flag the GPU upload
const char *dev_tier_name(float development);

#endif
