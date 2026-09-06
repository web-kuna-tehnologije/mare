// Static gameplay tables: goods, ship classes, buildings, nations, regions.

#ifndef MARE_TABLES_H
#define MARE_TABLES_H

#include "../core/types.h"
#include "../core/util.h"

// ---------------------------------------------------------------------------
// static tables

static const good_info_t goods[MAX_GOODS] = {
    { "Grain",     5, 0.180f, 0, RGB_(0xd8,0xc2,0x7a) },
    { "Fish",      6, 0.120f, 0, RGB_(0x9f,0xb8,0xc8) },
    { "Timber",    4, 0.090f, 0, RGB_(0x6b,0x8e,0x4e) },
    { "Salt",      8, 0.060f, 0, RGB_(0xe8,0xe8,0xf0) },
    { "Wine",     15, 0.060f, 1, RGB_(0x8e,0x1b,0x3a) },
    { "Cloth",    22, 0.045f, 1, RGB_(0x4a,0x6f,0xd8) },
    { "Arms",     30, 0.018f, 1, RGB_(0x55,0x55,0x60) },
    { "Sugar",    25, 0.030f, 2, RGB_(0xf4,0xf0,0xe0) },
    { "Tobacco",  35, 0.024f, 2, RGB_(0x8c,0x5a,0x2b) },
    { "Spices",   60, 0.012f, 2, RGB_(0xe0,0x7b,0x20) },
    { "Bullion", 100, 0.009f, 2, RGB_(0xf0,0xc0,0x20) },
    { "Furs",     40, 0.012f, 2, RGB_(0x6e,0x4a,0x2a) },
    // regional rare goods (0.7): produced by inland settlements, consumed mostly in Europe
    { "Iron",     18, 0.010f, 1, RGB_(0x8a,0x8a,0x90) },
    { "Coal",     10, 0.020f, 0, RGB_(0x30,0x30,0x34) },
    { "Copper",   40, 0.004f, 1, RGB_(0xc8,0x70,0x40) },
    { "Silver",  120, 0.004f, 2, RGB_(0xd8,0xd8,0xe8) },
    { "Gold",    250, 0.0015f,2, RGB_(0xff,0xd0,0x30) },
};

static const ship_info_t ship_types[SHIP_COUNT] = {
    //  name          cargo hull  speed guns crew  cost
    { "Sloop",          60,  40, 1.30f,  8,  40,  2500 },
    { "Fluyt",         400,  60, 1.00f,  6,  30,  4000 },
    { "Merchantman",   600, 120, 0.85f, 20,  80,  9000 },
    { "Frigate",        80, 130, 1.20f, 32, 180, 12000 },
    { "Galleon",       500, 180, 0.80f, 40, 220, 16000 },
    { "Ship of the Line",60,260, 0.90f, 74, 500, 28000 },
};

static const building_info_t buildings[MAX_BUILDINGS] = {
    { "Warehouse",  "Larger stocks, more stable prices",       6000 },
    { "Docks",      "Shipyard, faster loading & repairs",      8000 },
    { "Fort",       "Garrison guns and walls",                 9000 },
    { "Market",     "Population growth and tariff income",     5000 },
    { "Plantation", "Boosts sugar/tobacco output (colonies)",  7000 },
    { "Mine",       "Boosts bullion output (where found)",    12000 },
};

static const nation_info_t nations_info[MAX_NATIONS] = {
    { "Free Cities",     "Independent", RGB_(0xb0,0xb0,0xb0),  3, 1, 0.7f, "Free Port" },
    { "Spain",           "Spanish",     RGB_(0xf0,0xc8,0x20), 10, 6, 0.3f, "San" },
    { "England",         "English",     RGB_(0xe0,0x30,0x30),  8, 5, 0.45f, "New" },
    { "France",          "French",      RGB_(0x40,0x80,0xf0),  7, 4, 0.5f, "Port" },
    { "United Provinces","Dutch",       RGB_(0xff,0x90,0x10), 11, 4, 0.8f, "Fort" },
    { "Portugal",        "Portuguese",  RGB_(0x30,0xb0,0x40),  7, 3, 0.4f, "Santa" },
    { "Venice",          "Venetian",    RGB_(0xc0,0x40,0xb0),  4, 2, 0.6f, "Porto" },
    { "Ottoman Empire",  "Ottoman",     RGB_(0x20,0xb0,0xa0),  5, 3, 0.35f, "Yeni" },
    { "Denmark-Norway",  "Danish",      RGB_(0xf0,0x90,0xc0),  3, 2, 0.6f, "Frederiks" },
    { "Brethren of the Coast","Pirate", RGB_(0x30,0x30,0x30),  0, 0, 0.9f, "Freetown" },
};

static const char *const region_names[REG_COUNT] = {   // (referenced by the UI only)
    "North Sea & Baltic", "Atlantic Europe", "Mediterranean", "Caribbean", "North America",
    "South America", "Africa", "Arctic", "Pacific", "Indian Ocean",
};


#endif
