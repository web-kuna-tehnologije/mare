// Region simulation: rural production into export pools, rural consumption fed by the hubs,
// throughput-limited hub links, development, prosperity, inland settlements, hub shares.
// Ports are the only interface between a region and the world.

#ifndef MARE_REGION_SIM_H
#define MARE_REGION_SIM_H

#include "../core/types.h"

void regions_setup(void);                  // populations/development from the historical hubs, initial inland settlements (game_new)
void regions_tick(float dt);               // daily
float region_attractiveness(int r);        // for colonisation: resources x (1 + development)
int  region_found_inland(int r, int good, int kind, int nation, const char *name);   // -1 if no spot/room
int  region_consumption_bonus(const port_t *p, int g, float *extra_pop);   // hub demand from the region's rural population

#endif
