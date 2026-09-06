// Settlements (colonisation expansion).
//
// * Free placement: any coast with navigable water nearby and MIN_SITE_DIST from other
//   settlements can become a site; its resource potential comes from climate and terrain.
// * Production: each settlement allocates labour shares over the goods it has potential for.
//   produce[g] = TAG_PRODUCTION * size * potential[g] * labor[g] * (#resources), so equal shares
//   reproduce the historical output and concentrating labour trades one good for another.
// * Population: a site-quality capacity; immigration = free room * prosperity * IMMIGRATION_RATE,
//   mostly of the owner's culture plus a share from trading partners.
// * Cultures & unrest: minorities assimilate at a rate set by the owner's tolerance; a foreign
//   majority in an unhappy town secedes to its nation (or independence).
// * Merging: founded settlements within MERGE_DIST merge after MERGE_MIN_AGE days: the larger
//   population absorbs the smaller and the dominant culture takes ownership.

#ifndef MARE_SETTLEMENT_H
#define MARE_SETTLEMENT_H

#include "../core/types.h"

static inline int port_is_gone(const port_t *p) { return p->merged_into >= 0; }          // absorbed by a merge: skip everywhere
static inline int port_is_site(const port_t *p) { return p->size == 0 && p->merged_into < 0; } // unsettled, can be founded

void  site_potential(float lon, float lat, float potential[MAX_GOODS]);   // 0 none, ~1 good, ~1.5 rich per good
int   site_check(float wx, float wz, int nation, char *reason, int reason_len);   // 1 if a colony may be founded here
int   site_create(float wx, float wz, int nation);     // append a site (named for `nation`); port index or -1
void  settlement_update_production(port_t *p);         // derive produce[] from potential/labor/size
void  settlement_set_labor(port_t *p, int good, float share);   // set one share in [0,1], rescale the others
void  settlement_auto_labor(port_t *p);                // AI: drift labour toward goods that are dear locally
void  settlement_init_from_tags(port_t *p);            // historical ports: potentials/labour/culture/capacity from their tags
void  settlement_found(port_t *p, int nation, float settlers_thousands);   // turn a site into a size-1 colony
void  settlements_tick(float dt);                      // daily: immigration, assimilation, unrest, secession, merging (once per day)
int   settlement_dominant_culture(const port_t *p);    // nation id with the largest culture share
float settlement_foreign_share(const port_t *p);       // 0..1 share of population not of the owner's culture
const char *settlement_generate_name(int nation, float lon, float lat);   // "<prefix> <suffix>", deterministic per location

#endif
