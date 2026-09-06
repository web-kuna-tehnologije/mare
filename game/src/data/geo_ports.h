// Historical ports and colony sites of 1620. Production tags: one letter per unit of
// output (see geo_tag_good). Sites (size 0) carry the historical settler and year.

#ifndef MARE_GEO_PORTS_H
#define MARE_GEO_PORTS_H

#include "../core/types.h"

// ---------------------------------------------------------------------------
// ports & colony sites. Production tags: string of good initials (see geo_tag_good).
//   G grain, F fish, T timber, S salt, W wine, C cloth, A arms, U sugar, O tobacco,
//   P spices, B bullion, R furs
typedef struct geo_port_t {
    const char *name;
    float lon, lat;
    int nation;          // owner in 1620 (NATION_NONE for independent cities)
    int size;            // 0 = unsettled site
    int region;
    const char *tags;    // production
    int pref_nation;     // historical settler (sites) or 0
    int settle_year;     // year of historical settlement (sites)
    int haven;           // pirate haven
} geo_port_t;

static const geo_port_t geo_ports[] = {
    // --- Northern Europe / Baltic
    { "Amsterdam",      4.9f, 52.4f, NATION_NETHERLANDS, 3, REG_NORTH, "CFA" },
    { "Middelburg",     3.6f, 51.5f, NATION_NETHERLANDS, 2, REG_NORTH, "CF" },
    { "Antwerp",        4.4f, 51.25f,NATION_SPAIN,       2, REG_NORTH, "CA" },
    { "London",         0.0f, 51.5f, NATION_ENGLAND,     3, REG_NORTH, "CAG" },
    { "Bristol",       -2.6f, 51.45f,NATION_ENGLAND,     2, REG_ATLANTIC, "CG" },
    { "Plymouth",      -4.1f, 50.4f, NATION_ENGLAND,     1, REG_ATLANTIC, "F" },
    { "Leith",         -3.2f, 56.0f, NATION_ENGLAND,     1, REG_NORTH, "FG" },
    { "Dublin",        -6.3f, 53.3f, NATION_ENGLAND,     1, REG_ATLANTIC, "G" },
    { "Dunkirk",        2.4f, 51.05f,NATION_SPAIN,       1, REG_NORTH, "F", 0, 0, 0 },
    { "Le Havre",       0.1f, 49.5f, NATION_FRANCE,      2, REG_NORTH, "CG" },
    { "Brest",         -4.5f, 48.4f, NATION_FRANCE,      1, REG_ATLANTIC, "FT" },
    { "Nantes",        -1.6f, 47.2f, NATION_FRANCE,      2, REG_ATLANTIC, "WS" },
    { "La Rochelle",   -1.15f,46.15f,NATION_FRANCE,      2, REG_ATLANTIC, "SW" },
    { "Bordeaux",      -0.6f, 44.85f,NATION_FRANCE,      2, REG_ATLANTIC, "WW" },
    { "Hamburg",       10.0f, 53.55f,NATION_NONE,        2, REG_NORTH, "GC" },
    { "Lubeck",        10.7f, 53.87f,NATION_NONE,        2, REG_NORTH, "SF" },
    { "Copenhagen",    12.6f, 55.7f, NATION_DENMARK,     2, REG_NORTH, "GF" },
    { "Bergen",         5.3f, 60.4f, NATION_DENMARK,     1, REG_NORTH, "FT" },
    { "Stockholm",     18.1f, 59.3f, NATION_NONE,        2, REG_NORTH, "AT" },
    { "Danzig",        18.65f,54.35f,NATION_NONE,        2, REG_NORTH, "GT" },
    { "Riga",          24.1f, 57.0f, NATION_NONE,        1, REG_NORTH, "TR" },
    { "Archangel",     40.5f, 64.5f, NATION_NONE,        1, REG_ARCTIC, "RT" },
    // --- Iberia
    { "Lisbon",        -9.1f, 38.7f, NATION_PORTUGAL,    3, REG_ATLANTIC, "WSP" },
    { "Porto",         -8.6f, 41.15f,NATION_PORTUGAL,    1, REG_ATLANTIC, "W" },
    { "Seville",       -6.0f, 37.4f, NATION_SPAIN,       3, REG_ATLANTIC, "WS" },
    { "Cadiz",         -6.3f, 36.5f, NATION_SPAIN,       2, REG_ATLANTIC, "SW" },
    { "Malaga",        -4.4f, 36.7f, NATION_SPAIN,       1, REG_MED, "W" },
    { "Valencia",      -0.4f, 39.5f, NATION_SPAIN,       2, REG_MED, "CW" },
    { "Barcelona",      2.2f, 41.4f, NATION_SPAIN,       2, REG_MED, "CA" },
    { "Bilbao",        -2.9f, 43.3f, NATION_SPAIN,       2, REG_ATLANTIC, "AA" },
    { "La Coruna",     -8.4f, 43.4f, NATION_SPAIN,       1, REG_ATLANTIC, "F" },
    { "Palma",          2.6f, 39.6f, NATION_SPAIN,       1, REG_MED, "SW" },
    // --- Mediterranean
    { "Marseille",      5.4f, 43.3f, NATION_FRANCE,      3, REG_MED, "CW" },
    { "Genoa",          8.9f, 44.4f, NATION_NONE,        2, REG_MED, "CA" },
    { "Livorno",       10.3f, 43.55f,NATION_NONE,        2, REG_MED, "C" },
    { "Naples",        14.25f,40.85f,NATION_SPAIN,       3, REG_MED, "GW" },
    { "Palermo",       13.4f, 38.1f, NATION_SPAIN,       2, REG_MED, "GG" },
    { "Messina",       15.55f,38.2f, NATION_SPAIN,       1, REG_MED, "S" },
    { "Venice",        12.3f, 45.4f, NATION_VENICE,      3, REG_MED, "CP" },
    { "Ragusa",        18.1f, 42.6f, NATION_NONE,        1, REG_MED, "S" },
    { "Candia",        25.1f, 35.3f, NATION_VENICE,      1, REG_MED, "W" },
    { "Constantinople",29.0f, 41.0f, NATION_OTTOMAN,     3, REG_MED, "PC" },
    { "Smyrna",        27.1f, 38.4f, NATION_OTTOMAN,     2, REG_MED, "CW" },
    { "Salonica",      22.9f, 40.6f, NATION_OTTOMAN,     1, REG_MED, "G" },
    { "Famagusta",     33.95f,35.1f, NATION_OTTOMAN,     1, REG_MED, "SW" },
    { "Acre",          35.1f, 32.9f, NATION_OTTOMAN,     1, REG_MED, "PC" },
    { "Alexandria",    29.9f, 31.2f, NATION_OTTOMAN,     2, REG_MED, "GP" },
    { "Tripoli",       13.2f, 32.9f, NATION_OTTOMAN,     1, REG_MED, "S" },
    { "Tunis",         10.3f, 36.8f, NATION_OTTOMAN,     1, REG_MED, "G" },
    { "Algiers",        3.05f,36.75f,NATION_OTTOMAN,     2, REG_MED, "G", 0, 0, 1 },
    // --- Africa & Atlantic islands
    { "Tangier",       -5.8f, 35.8f, NATION_PORTUGAL,    1, REG_ATLANTIC, "G" },
    { "Sale",          -6.8f, 34.05f,NATION_PIRATES,     1, REG_ATLANTIC, "G", 0, 0, 1 },
    { "Funchal",      -16.9f, 32.65f,NATION_PORTUGAL,    1, REG_ATLANTIC, "WU" },
    { "Las Palmas",   -15.4f, 28.1f, NATION_SPAIN,       1, REG_ATLANTIC, "WU" },
    { "Angra",        -27.2f, 38.65f,NATION_PORTUGAL,    1, REG_ATLANTIC, "G" },
    { "Ribeira Grande",-23.6f,15.0f, NATION_PORTUGAL,    1, REG_AFRICA, "S" },
    { "Goree",        -17.4f, 14.67f,NATION_NETHERLANDS, 1, REG_AFRICA, "S" },
    { "Elmina",        -1.35f, 5.08f,NATION_PORTUGAL,    2, REG_AFRICA, "BB" },
    { "Luanda",        13.2f, -8.8f, NATION_PORTUGAL,    1, REG_AFRICA, "S" },
    { "Cape Town",     18.4f,-33.9f, NATION_NONE,        0, REG_AFRICA, "GW", NATION_NETHERLANDS, 1652 },
    { "Mozambique",    40.7f,-15.0f, NATION_PORTUGAL,    1, REG_INDIAN, "PB" },
    { "Mombasa",       39.7f, -4.05f,NATION_PORTUGAL,    1, REG_INDIAN, "P" },
    { "Jeddah",        39.2f, 21.5f, NATION_OTTOMAN,     1, REG_INDIAN, "P" },
    { "Mocha",         43.25f,13.3f, NATION_OTTOMAN,     1, REG_INDIAN, "PP" },
    // --- Caribbean & Spanish Main
    { "Havana",       -82.4f, 23.1f, NATION_SPAIN,       3, REG_CARIB, "UO" },
    { "Santiago de Cuba",-75.8f,20.0f,NATION_SPAIN,      1, REG_CARIB, "U" },
    { "Santo Domingo",-69.9f, 18.5f, NATION_SPAIN,       2, REG_CARIB, "UU" },
    { "San Juan",     -66.1f, 18.5f, NATION_SPAIN,       1, REG_CARIB, "U" },
    { "Veracruz",     -96.1f, 19.2f, NATION_SPAIN,       2, REG_CARIB, "BB" },
    { "Campeche",     -90.5f, 19.85f,NATION_SPAIN,       1, REG_CARIB, "T" },
    { "Cartagena",    -75.5f, 10.4f, NATION_SPAIN,       2, REG_CARIB, "BB" },
    { "Portobelo",    -79.65f, 9.55f,NATION_SPAIN,       1, REG_CARIB, "B" },
    { "Maracaibo",    -71.65f,11.05f,NATION_SPAIN,       1, REG_CARIB, "O" },   // at the gulf, not inside the lake (which is not navigable)
    { "Cumana",       -64.2f, 10.45f,NATION_SPAIN,       1, REG_CARIB, "S" },
    { "Trinidad",     -61.5f, 10.65f,NATION_SPAIN,       1, REG_CARIB, "O" },
    { "Santiago de la Vega",-76.85f,17.95f,NATION_SPAIN, 1, REG_CARIB, "U" },
    { "Tortuga",      -72.8f, 20.05f,NATION_PIRATES,     1, REG_CARIB, "O", 0, 0, 1 },
    { "St Augustine", -81.3f, 29.9f, NATION_SPAIN,       1, REG_CARIB, "T" },
    { "Curacao",      -68.95f,12.1f, NATION_NONE,        0, REG_CARIB, "S", NATION_NETHERLANDS, 1634 },
    { "Barbados",     -59.6f, 13.1f, NATION_NONE,        0, REG_CARIB, "UU", NATION_ENGLAND, 1627 },
    { "St Kitts",     -62.7f, 17.3f, NATION_NONE,        0, REG_CARIB, "UO", NATION_ENGLAND, 1624 },
    { "Antigua",      -61.8f, 17.1f, NATION_NONE,        0, REG_CARIB, "U", NATION_ENGLAND, 1632 },
    { "Martinique",   -61.0f, 14.6f, NATION_NONE,        0, REG_CARIB, "UU", NATION_FRANCE, 1635 },
    { "Guadeloupe",   -61.6f, 16.2f, NATION_NONE,        0, REG_CARIB, "U", NATION_FRANCE, 1635 },
    { "Nassau",       -77.35f,25.05f,NATION_NONE,        0, REG_CARIB, "S", NATION_ENGLAND, 1650, 1 },
    { "St Eustatius", -62.98f,17.48f,NATION_NONE,        0, REG_CARIB, "S", NATION_NETHERLANDS, 1636 },
    { "St Thomas",    -64.93f,18.34f,NATION_NONE,        0, REG_CARIB, "U", NATION_DENMARK, 1672 },
    { "Cayenne",      -52.3f,  4.9f, NATION_NONE,        0, REG_SAMER, "U", NATION_FRANCE, 1643 },
    { "Paramaribo",   -55.2f,  5.85f,NATION_NONE,        0, REG_SAMER, "UU", NATION_NETHERLANDS, 1651 },
    { "Essequibo",    -58.4f,  6.8f, NATION_NETHERLANDS, 1, REG_SAMER, "U" },
    // --- North America
    { "Jamestown",    -76.8f, 37.2f, NATION_ENGLAND,     1, REG_NAMER, "OO" },
    { "Plymouth Colony",-70.7f,41.95f,NATION_ENGLAND,    1, REG_NAMER, "FR" },
    { "Bermuda",      -64.8f, 32.3f, NATION_ENGLAND,     1, REG_NAMER, "O" },
    { "St Johns",     -52.7f, 47.55f,NATION_ENGLAND,     1, REG_NAMER, "FF" },
    { "Quebec",       -71.2f, 46.8f, NATION_FRANCE,      1, REG_NAMER, "RR" },
    { "Port Royal",   -65.6f, 44.7f, NATION_FRANCE,      1, REG_NAMER, "RF" },
    { "New Amsterdam",-74.0f, 40.7f, NATION_NONE,        0, REG_NAMER, "RG", NATION_NETHERLANDS, 1624 },
    { "Boston",       -71.05f,42.35f,NATION_NONE,        0, REG_NAMER, "FT", NATION_ENGLAND, 1630 },
    { "Charleston",   -79.9f, 32.8f, NATION_NONE,        0, REG_NAMER, "G", NATION_ENGLAND, 1670 },
    { "New Sweden",   -75.5f, 39.7f, NATION_NONE,        0, REG_NAMER, "RT", NATION_NONE, 1638 },
    { "Port Nelson",  -92.8f, 57.0f, NATION_NONE,        0, REG_ARCTIC, "RR", NATION_ENGLAND, 1670 },
    // --- Pacific & South America
    { "Salvador",     -38.5f,-13.0f, NATION_PORTUGAL,    2, REG_SAMER, "UO" },
    { "Recife",       -34.9f, -8.05f,NATION_PORTUGAL,    2, REG_SAMER, "UU" },
    { "Rio de Janeiro",-43.2f,-22.9f,NATION_PORTUGAL,    1, REG_SAMER, "U" },
    { "Sao Luis",     -44.3f, -2.5f, NATION_PORTUGAL,    1, REG_SAMER, "T" },
    { "Belem",        -48.5f, -1.45f,NATION_PORTUGAL,    1, REG_SAMER, "P" },
    { "Buenos Aires", -58.4f,-34.6f, NATION_SPAIN,       1, REG_SAMER, "RG" },
    { "Callao",       -77.1f,-12.05f,NATION_SPAIN,       2, REG_PACIFIC, "BB" },
    { "Panama",       -79.5f,  8.95f,NATION_SPAIN,       1, REG_PACIFIC, "B" },
    { "Acapulco",     -99.9f, 16.85f,NATION_SPAIN,       1, REG_PACIFIC, "PP" },
    { "Valparaiso",   -71.6f,-33.05f,NATION_SPAIN,       1, REG_PACIFIC, "GW" },
};

static inline int geo_tag_good(char c) {
    switch(c) {
        case 'G': return G_GRAIN;  case 'F': return G_FISH;   case 'T': return G_TIMBER;
        case 'S': return G_SALT;   case 'W': return G_WINE;   case 'C': return G_CLOTH;
        case 'A': return G_ARMS;   case 'U': return G_SUGAR;  case 'O': return G_TOBACCO;
        case 'P': return G_SPICES; case 'B': return G_BULLION;case 'R': return G_FURS;
    }
    return -1;
}


#define GEO_NPORTS ((int)(sizeof(geo_ports)/sizeof(geo_ports[0])))

#endif
