// Coastline data: coarse hand-authored polygons (lon, lat ... END), inland waters and
// strait carvings. To expand the world add polygons here and widen the extents in
// core/types.h. Polygons may extend past the world bounds (they are clipped) and may
// overlap (even-odd fill).

#ifndef MARE_GEO_COAST_H
#define MARE_GEO_COAST_H

#define END 9999.f

// ---------------------------------------------------------------------------
// land polygons (lon, lat ... END). Coarse, ~1-2 degree fidelity.

static const float geo_north_america[] = {
    -130,70, -122,70, -112,68, -102,68, -95,68, -94,62, -94,58.5f, -92,57, -88,56.5f, -85,55.5f,
    -82,53, -80,51.5f, -79,53, -78.5f,55.5f, -77,58, -77.5f,61, -73,62.5f, -70,61.5f, -65,60,
    -62,57.5f, -60,55.5f, -57,53.5f, -56,52, -58.5f,50.5f, -62,50, -66,50, -69,48.5f, -71,47.3f,
    -69.5f,47.5f, -65,48.5f, -64.5f,48, -64,46.5f, -63.5f,46, -61.5f,45.7f, -60,46, -61,45.3f,
    -63.5f,44.5f, -66,44, -65.5f,45.2f, -67,45, -68,44.3f, -70,43.7f, -70.5f,42.5f, -70,42,
    -71.5f,41.3f, -72.5f,41, -74,40.5f, -74,39, -75,38.5f, -76,37, -75.8f,35.3f, -78,34,
    -80,32.5f, -81.5f,31, -81,29, -80,26.5f, -80.5f,25, -81.5f,25, -82.5f,27.5f, -83,29,
    -85,29.5f, -87,30.3f, -89,30, -90,29, -94,29.5f, -97,27.5f, -97.5f,25.5f, -97.5f,22,
    -96,19.5f, -94,18.5f, -91,18.5f, -90.5f,21, -88,21.5f, -87,20, -88,18.5f, -88.5f,16,
    -86,15.8f, -83.5f,15, -83,12, -83.5f,10.5f, -82,9.3f, -79.5f,9.5f, -78,9, -77.5f,8.5f,
    -78,7.5f, -80,7.5f, -81.5f,8.2f, -83,8.5f, -85,10, -86,12, -88,13, -91,14, -94,16,
    -96,15.8f, -100,17, -103,18.5f, -105.5f,20.5f, -105.5f,23, -109,26, -110,28, -113,31,
    -114.5f,31.5f, -112.5f,29, -110.5f,25, -109.5f,23, -112,25.5f, -115,29.5f, -117,32.5f,
    -120.5f,34.5f, -122.5f,37.5f, -124,40.5f, -124,46, -125,49, -128,51, -130,55, -130,70, END };

static const float geo_south_america[] = {
    -77.5f,8.5f, -77,8.2f, -76.8f,9, -75.5f,10.5f, -74.5f,11, -72.5f,11.8f, -72.2f,11.5f, -71.7f,10.95f, -71.3f,11.4f, -71.5f,12.3f,   /* Gulf of Venezuela */
    -70,12, -68,11, -66,10.6f, -64,10.5f, -62.5f,10.5f, -61,9.5f, -60,8.5f, -57,6, -55,5.8f,
    -52,4.5f, -51,1.5f, -50,0, -48,-1, -44,-2.5f, -41,-2.8f, -38.5f,-3.7f, -35.2f,-5.5f,
    -34.8f,-7.5f, -36,-10, -38.5f,-13, -39,-17.5f, -40,-20, -41,-22, -43.2f,-23, -45,-23.5f,
    -48,-25.5f, -48.5f,-28.5f, -50,-30.5f, -52,-32.5f, -54,-34.5f, -56,-34.8f, -58.3f,-34.5f,
    -57,-36.5f, -57.5f,-38, -62,-39, -62.5f,-41, -65,-42.5f, -64,-44.5f, -65.5f,-45.5f,
    -67.5f,-46.5f, -67,-49, -68.5f,-51.5f, -69,-52.5f, -71,-53.5f, -73,-52, -74.5f,-50,
    -75,-47, -74,-44, -73.5f,-41, -73,-37, -71.5f,-33, -71.5f,-30, -70.5f,-26, -70.3f,-23,
    -70,-18.5f, -72,-17, -75,-15, -76.5f,-13, -77,-12, -79,-8, -81,-6, -81,-4, -80,-3,
    -80.5f,-1, -79.5f,1, -78.5f,2.5f, -77.5f,4, -77.5f,7, -78,8, END };

static const float geo_tierra_del_fuego[] = { -68.5f,-52.7f, -65.5f,-54.5f, -68,-55, -71,-54.5f, -72,-53.5f, END };

// Eurasia (west of LON1): Scandinavia, Europe, Anatolia, Levant, Arabia(Red Sea coast)
static const float geo_eurasia[] = {
    46,68, 42,67.5f, 41,66.5f, 40.5f,64.8f, 38,64.5f, 36,65, 35,66.5f, 33,69.5f, 30,70, 25.5f,71, 21,70, 17,69, 14,67.5f,
    12,65.5f, 10,63.5f, 8,62.5f, 5,61.5f, 5,59, 6,58, 8,58, 10.5f,59.2f, 11.5f,58.5f,
    12,56.5f, 12.5f,56, 14.5f,56, 16.5f,57.5f, 18.5f,59.5f, 18,61, 21,62.5f, 21.5f,64,
    25,65.5f, 22.5f,64, 21.5f,62, 21,60.5f, 22.5f,60, 26,60.5f, 30,60, 28,59.5f, 24,59.5f,
    23.5f,58.5f, 24,57, 21,57, 21,55.5f, 19.5f,54.5f, 18.5f,54.5f, 14.5f,54, 12,54.3f,
    10.5f,54.5f, 10,56, 10,57.5f, 8.5f,57, 8,55.5f, 8.5f,54.8f, 9,54, 7,53.5f, 5,53,
    4,51.8f, 3,51.2f, 1.5f,50.9f, 0,49.5f, -1.5f,49.6f, -1.5f,48.6f, -4.5f,48.6f,
    -4.5f,47.8f, -2,47.2f, -1.2f,46, -1.2f,44, -1.8f,43.4f, -3.5f,43.4f, -6,43.5f, -8,43.7f,
    -9.2f,43, -9,42, -8.8f,40, -9.5f,38.7f, -9,37, -7.5f,37, -6.3f,36.5f, -5.6f,36,
    -4.5f,36.7f, -2,36.7f, -0.5f,38, 0,39.5f, 1,41.2f, 2.2f,41.4f, 3,42.5f, 3.5f,43.3f,
    5.5f,43.2f, 7.5f,43.8f, 8.5f,44.4f, 10.5f,43, 11.5f,42.3f, 12.3f,41.8f, 14,40.8f,
    15.7f,40, 16,38, 15.7f,38, 16.5f,38.8f, 17,39.5f, 16.5f,39.8f, 17,40.5f, 18.5f,40.1f,
    16,41.8f, 14,42.5f, 12.5f,44, 12.3f,45.5f, 13.7f,45.6f, 14.5f,45, 16,43.5f, 18.5f,42.5f,
    19.5f,41.5f, 19.5f,40, 20.5f,39, 21,37.5f, 22.5f,36.5f, 23,38, 24,38, 23.5f,40, 25,41,
    26.5f,40.5f, 28,41, 29,41.2f, 28.8f,40.4f, 26.5f,40.3f, 26.2f,39.5f, 26.8f,38.5f,
    27.2f,37, 28.5f,36.7f, 30.5f,36.3f, 32,36.2f, 34,36.2f, 36,36.7f, 36,35, 35.5f,33.5f,
    34.8f,32, 34.2f,31.3f, 32.5f,31, 32.5f,29.8f, 33.5f,28, 34.3f,27.8f, 35,29.5f, 36,27,
    38,24, 39,21.5f, 41,19, 42.5f,15.5f, 43.2f,13, 46,12.8f, END };

static const float geo_africa[] = {
    32.3f,30, 33.5f,28, 35,25, 37,21, 38.5f,18, 39.5f,15.5f, 42,13, 43.2f,12, 45.2f,10.8f,
    45.2f,1, 41.5f,-1.7f,   // Somalia pulled off the map edge: a channel along the border stands in for the Indian Ocean beyond it 39.5f,-5, 39.3f,-6.8f, 40.5f,-10.5f, 40.5f,-15, 36,-18, 35,-23.5f,
    32.5f,-26, 30,-31, 27,-33.5f, 22,-34, 18.5f,-34.3f, 18,-32.5f, 17,-29, 15,-27,
    14.5f,-22.5f, 12,-17, 13.5f,-12, 13.2f,-8.8f, 12.3f,-6, 9.5f,-1, 9.5f,2, 8.5f,4.5f,
    6,4.3f, 3.5f,6.4f, 1,6, -2,5, -4,5.2f, -7.5f,4.4f, -10,6.5f, -13,8.5f, -14.5f,10.5f,
    -16.5f,12.5f, -17.5f,14.7f, -16.5f,16.5f, -16,19.5f, -17,21, -15,24.5f, -13,27.5f,
    -10,29.5f, -9.7f,31.5f, -8,33, -6.8f,34, -6,35.7f, -5.3f,35.8f, -2,35.2f, 0,35.8f,
    3,36.8f, 7,37, 10,37.2f, 11,36.8f, 10.2f,35, 11,34, 10,33.5f, 11,33, 15,32.3f,
    15.5f,31.4f, 19,30.3f, 20,32.3f, 22.5f,32.8f, 25,31.7f, 28,31, 30,31.3f, 32,31.3f, END };

static const float geo_britain[] = {
    -5.7f,50, -3.5f,50.4f, -1.5f,50.7f, 1.4f,51.2f, 1.7f,52.7f, 0.2f,53.5f, -0.5f,54.5f,
    -1.5f,55.5f, -2,56, -2.5f,57.5f, -3.5f,58.6f, -5,58.6f, -6,57.5f, -5.5f,56, -5,55,
    -3,54.8f, -3,54, -3.5f,53.5f, -4.5f,53.2f, -4.7f,52.8f, -4,52.3f, -5.2f,51.7f,
    -3.5f,51.4f, -4.5f,51.2f, END };
static const float geo_ireland[] = {
    -10,51.6f, -8,51.7f, -6.3f,52.3f, -6,53.5f, -6.2f,54.2f, -5.8f,55, -7.5f,55.3f,
    -8.5f,54.7f, -10,54.2f, -10,53.2f, -9.5f,52.5f, -10.2f,52, END };
static const float geo_iceland[]   = { -24,65.5f, -22,66.4f, -16,66.5f, -13.5f,65.2f, -18,63.4f, -22.5f,63.8f, END };
static const float geo_greenland[] = { -45,60, -50,63, -53,66, -54,70, -56,72.5f, -20,72.5f, -22,70, -25,68.5f, -38,65.5f, -42,61.5f, END };
static const float geo_baffin[]    = { -61.5f,66.5f, -65,63.5f, -68.5f,63, -72.5f,65.5f, -78,65, -80,67, -85,68.5f, -88,70, -80,72.5f, -70,72.5f, -63,68, END };
static const float geo_newfoundland[] = { -59.3f,47.6f, -58,49.5f, -56,51.6f, -55.5f,49.5f, -52.7f,47.6f, -53.5f,46.7f, -56,47.5f, END };
static const float geo_cuba[]      = { -85,21.8f, -82,23.2f, -80,23, -77,21.5f, -74.2f,20.2f, -75,19.9f, -77.7f,19.9f, -79,21.5f, -82,22, -84.5f,21.8f, END };
static const float geo_hispaniola[]= { -74.4f,18.4f, -72.8f,19.9f, -70,19.8f, -68.4f,18.6f, -70,18.2f, -72,18, -73.5f,18.2f, END };
static const float geo_jamaica[]   = { -78.3f,18.3f, -76.3f,18.2f, -76.8f,17.9f, -78,18, END };
static const float geo_puertorico[]= { -67.2f,18.5f, -65.6f,18.4f, -65.7f,18, -67.2f,18, END };
static const float geo_trinidad[]  = { -61.9f,10.8f, -61,10.8f, -61,10.1f, -61.9f,10.1f, END };
static const float geo_guadeloupe[]= { -61.8f,16.5f, -61.2f,16.5f, -61.2f,15.95f, -61.8f,15.95f, END };
static const float geo_martinique[]= { -61.25f,14.9f, -60.8f,14.9f, -60.8f,14.4f, -61.25f,14.4f, END };
static const float geo_barbados[]  = { -59.7f,13.35f, -59.4f,13.35f, -59.4f,13.05f, -59.7f,13.05f, END };
static const float geo_antigua[]   = { -62.0f,17.2f, -61.6f,17.2f, -61.6f,16.95f, -62.0f,16.95f, END };
static const float geo_stkitts[]   = { -62.9f,17.45f, -62.5f,17.45f, -62.5f,17.15f, -62.9f,17.15f, END };
static const float geo_curacao[]   = { -69.2f,12.4f, -68.7f,12.4f, -68.7f,12.0f, -69.2f,12.0f, END };
static const float geo_bahamas1[]  = { -78.1f,25.3f, -77.0f,25.3f, -77.0f,24.9f, -78.1f,24.9f, END }; // new providence+andros approx
static const float geo_bahamas2[]  = { -78.6f,26.9f, -77.7f,26.9f, -77.7f,26.4f, -78.6f,26.4f, END }; // grand bahama
static const float geo_bermuda[]   = { -64.95f,32.45f, -64.65f,32.45f, -64.65f,32.2f, -64.95f,32.2f, END };
static const float geo_sicily[]    = { 12.5f,38.2f, 15.6f,38.3f, 15.2f,37, 12.5f,37.5f, END };
static const float geo_sardinia[]  = { 8.2f,41, 9.8f,41.1f, 9.6f,39, 8.5f,39, END };
static const float geo_corsica[]   = { 8.6f,43, 9.5f,42.8f, 9.3f,41.4f, 8.7f,41.6f, END };
static const float geo_crete[]     = { 23.5f,35.4f, 26.3f,35.2f, 25,34.9f, END };
static const float geo_cyprus[]    = { 32.3f,35.1f, 34.5f,35.6f, 33,34.7f, END };
static const float geo_mallorca[]  = { 2.4f,39.6f, 3.4f,39.8f, 3.1f,39.3f, 2.5f,39.4f, END };
static const float geo_zealand[]   = { 11.5f,55, 12.6f,55.7f, 12,56.1f, 11,55.7f, END };
static const float geo_gotland[]   = { 18.2f,57.9f, 18.9f,57.9f, 18.7f,57.1f, 18.2f,57.2f, END };
static const float geo_madeira[]   = { -17.3f,32.9f, -16.7f,32.9f, -16.7f,32.6f, -17.3f,32.6f, END };
static const float geo_tenerife[]  = { -16.9f,28.6f, -16.2f,28.6f, -16.2f,28.0f, -16.9f,28.0f, END };
static const float geo_grancanaria[]={ -15.8f,28.2f, -15.3f,28.2f, -15.3f,27.7f, -15.8f,27.7f, END };
static const float geo_azores1[]   = { -28.9f,38.9f, -28.3f,38.9f, -28.3f,38.5f, -28.9f,38.5f, END };
static const float geo_azores2[]   = { -25.9f,37.95f, -25.4f,37.95f, -25.4f,37.65f, -25.9f,37.65f, END };
static const float geo_azores3[]   = { -27.4f,38.85f, -27.0f,38.85f, -27.0f,38.6f, -27.4f,38.6f, END };
static const float geo_capeverde[] = { -24.2f,15.3f, -23.3f,15.3f, -23.3f,14.8f, -24.2f,14.8f, END };
static const float geo_capeverde2[]= { -25.2f,17.1f, -24.8f,17.1f, -24.8f,16.8f, -25.2f,16.8f, END };
static const float geo_mozisland[] = { 40.6f,-14.9f, 40.9f,-14.9f, 40.9f,-15.2f, 40.6f,-15.2f, END };
static const float geo_longisland[]= { -73.8f,40.8f, -72.0f,41.2f, -72.0f,40.95f, -73.7f,40.55f, END };
static const float geo_capebreton[]= { -61.5f,46.3f, -60.0f,47.0f, -59.8f,46.0f, -60.8f,45.6f, END };
static const float geo_princeedward[]={ -64.2f,46.5f, -62.0f,46.5f, -62.2f,46.1f, -63.8f,46.2f, END };
static const float geo_anticosti[] = { -64.5f,49.9f, -61.8f,49.3f, -62.0f,49.0f, -64.3f,49.6f, END };
static const float geo_vancouver[] = { -128.5f,50.8f, -124.5f,49.5f, -123.5f,48.4f, -125.5f,48.7f, -127.5f,50.2f, END };
static const float geo_marajo[]    = { -50.5f,-0.2f, -48.5f,-0.1f, -48.8f,-1.2f, -50.3f,-1.0f, END };

static const float *geo_land[] = {
    geo_north_america, geo_south_america, geo_tierra_del_fuego, geo_eurasia, geo_africa,
    geo_britain, geo_ireland, geo_iceland, geo_greenland, geo_baffin, geo_newfoundland,
    geo_cuba, geo_hispaniola, geo_jamaica, geo_puertorico, geo_trinidad, geo_guadeloupe,
    geo_martinique, geo_barbados, geo_antigua, geo_stkitts, geo_curacao, geo_bahamas1,
    geo_bahamas2, geo_bermuda, geo_sicily, geo_sardinia, geo_corsica, geo_crete, geo_cyprus,
    geo_mallorca, geo_zealand, geo_gotland, geo_madeira, geo_tenerife, geo_grancanaria,
    geo_azores1, geo_azores2, geo_azores3, geo_capeverde, geo_capeverde2, geo_mozisland,
    geo_longisland, geo_capebreton, geo_princeedward, geo_anticosti, geo_vancouver, geo_marajo,
};

// ---------------------------------------------------------------------------
// inland water polygons (carved out after land)

static const float geo_black_sea[] = {
    28.5f,41.5f, 29,41.3f, 31,41.3f, 34,42, 38,41, 41.5f,41.5f, 41.5f,43, 39,44.5f, 37.5f,45,
    36.5f,45.2f, 35,44.5f, 33.5f,44.5f, 33,45.5f, 32,46.5f, 30.5f,46.5f, 29.5f,45, 28,44, 28,42.3f, END };
static const float geo_lake_superior[] = { -92,46.7f, -89,48, -85,48.5f, -84.5f,46.5f, -87,46.5f, -90,46.5f, END };
static const float geo_lake_michigan[] = { -88,44.2f, -87.6f,46, -85,45.8f, -85.5f,44, -86.5f,42, -87.5f,42, END };
static const float geo_lake_huron[]    = { -84.5f,46.2f, -81.5f,46, -80,45, -80.5f,44, -82,43, -83,44, -84,45.5f, END };
static const float geo_lake_erie[]     = { -83.3f,41.7f, -82,42.5f, -79.5f,42.9f, -79,42.7f, -81,41.5f, -83,41.4f, END };
static const float geo_lake_ontario[]  = { -79.8f,43.3f, -79.5f,44, -76.5f,44.2f, -76.2f,43.5f, -78,43.2f, END };
static const float geo_lake_maracaibo[]= { -72,10.8f, -71.2f,10.8f, -71,9.7f, -71.3f,9.1f, -72,9.3f, END };
static const float geo_lake_nicaragua[]= { -85.9f,11.9f, -85.1f,11.8f, -84.8f,11.2f, -85.5f,11.1f, END };
static const float geo_lake_ladoga[]   = { 30,61.8f, 32.5f,61.5f, 33,60.5f, 31,60, 30,60.5f, END };
static const float geo_lake_vanern[]   = { 12.5f,59.2f, 14,59.3f, 14,58.5f, 13,58.5f, END };

static const float *geo_water[] = {
    geo_black_sea, geo_lake_superior, geo_lake_michigan, geo_lake_huron, geo_lake_erie,
    geo_lake_ontario, geo_lake_maracaibo, geo_lake_nicaragua, geo_lake_ladoga, geo_lake_vanern,
};

// ---------------------------------------------------------------------------
// strait carvings: polylines forced to water with a given half-width (degrees)
typedef struct geo_strait_t { float halfwidth; const float *pts; } geo_strait_t;

static const float strait_gibraltar[] = { -6.3f,36.0f, -5.0f,35.95f, END };
static const float strait_oresund[]   = { 12.7f,56.3f, 12.75f,55.9f, 12.75f,55.5f, 12.7f,55.2f, END };
static const float strait_kattegat[]  = { 10.8f,57.9f, 11.5f,57.2f, 12.0f,56.5f, 12.6f,56.2f, END };
static const float strait_belt[]      = { 11.0f,55.5f, 11.0f,54.6f, END };
static const float strait_messina[]   = { 15.65f,38.5f, 15.65f,37.9f, END };
static const float strait_bonifacio[] = { 8.6f,41.3f, 9.6f,41.3f, END };
static const float strait_babelmandeb[]={ 43.0f,13.0f, 43.6f,12.3f, END };
static const float strait_belleisle[] = { -57.5f,51.4f, -55.2f,52.0f, END };
static const float strait_cabot[]     = { -60.5f,47.3f, -58.5f,47.3f, END };
static const float strait_dardanelles[]={ 26.0f,40.1f, 26.5f,40.3f, 27.5f,40.5f, 28.5f,40.8f, 29.0f,41.1f, 29.2f,41.4f, END };
static const float strait_magellan[]  = { -68.5f,-52.6f, -70.5f,-53.2f, -72.5f,-53.7f, -74.5f,-53.2f, END };
static const float strait_florida[]   = { -80.2f,25.0f, -80.5f,23.8f, END };
static const float strait_windward[]  = { -74.4f,20.2f, -73.8f,19.6f, END };
static const float strait_mona[]      = { -67.5f,18.3f, -67.9f,18.7f, END };
static const float strait_dover[]     = { 1.3f,51.0f, 1.9f,51.3f, END };
static const float strait_bosporus_to_marmara[] = { 28.8f,40.2f, 26.8f,40.0f, END };
static const float strait_st_lawrence[] = { -66,50.3f, -68.5f,49, -70,48, -71,47.3f, END };
static const float strait_hudson[]    = { -70,62, -65,61, END };

static const geo_strait_t geo_straits[] = {
    {0.12f, strait_gibraltar}, {0.12f, strait_oresund}, {0.25f, strait_kattegat}, {0.15f, strait_belt},
    {0.12f, strait_messina}, {0.15f, strait_bonifacio}, {0.25f, strait_babelmandeb}, {0.25f, strait_belleisle},
    {0.35f, strait_cabot}, {0.15f, strait_dardanelles}, {0.2f, strait_magellan}, {0.3f, strait_florida},
    {0.3f, strait_windward}, {0.3f, strait_mona}, {0.25f, strait_dover}, {0.2f, strait_bosporus_to_marmara},
    {0.35f, strait_st_lawrence}, {0.6f, strait_hudson},
};


#endif
