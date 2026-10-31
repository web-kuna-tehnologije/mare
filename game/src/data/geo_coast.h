// Coastline data: coarse hand-authored polygons (lon, lat ... END), inland waters and
// strait carvings. To expand the world add polygons here and widen the extents in
// core/types.h. Polygons may extend past the world bounds (they are clipped) and may
// overlap (even-odd fill).

#ifndef MARE_GEO_COAST_H
#define MARE_GEO_COAST_H

#ifdef __cplusplus
extern "C" {
#endif

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
    16,41.8f, 14,42.5f, 12.5f,44, 12.3f,45.5f, 13.7f,45.6f,   /* Trieste */
    13.58f,45.40f, 13.56f,45.12f, 13.72f,44.92f, 13.88f,44.81f,   /* Istria: Porec, Rovinj, Pula */
    14.03f,45.02f, 14.22f,45.22f, 14.42f,45.33f,                  /* ... and back up its eastern shore to Rijeka */
    14.6f,44.9f, 15.3f,44.2f, 16,43.5f, 16.45f,43.52f, 16.70f,43.44f,          /* Split, Omis */
    16.95f,43.30f, 17.35f,43.10f, 17.48f,43.045f, 17.56f,43.00f, 17.63f,42.955f, 17.705f,42.87f,   /* Makarska, Gradac, Ploce, Komarna, Mali Ston: the mainland shore of the bay */
    17.60f,42.975f, 17.50f,42.985f, 17.33f,42.985f, 17.26f,43.04f, 17.04f,43.045f, 16.99f,42.99f,   /* Peljesac (0.22.3): the north shore west to Loviste. Drawn two cells thick - the */
    17.10f,42.905f, 17.30f,42.905f, 17.45f,42.87f, 17.58f,42.85f, 17.68f,42.805f,                   /* rings round a one-cell strip away to islets - with the bay behind it cut by a strait */
    17.72f,42.83f, 17.86f,42.76f, 18.10f,42.64f, 18.5f,42.5f,                                       /* Ston, Slano, Ragusa */
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
static const float geo_trinidad[]  = { -60.989f,10.546f, -61.113f,10.712f, -61.327f,10.808f, -61.573f,10.808f, -61.787f,10.712f, -61.911f,10.546f, -61.911f,10.354f, -61.787f,10.188f, -61.573f,10.092f, -61.327f,10.092f, -61.113f,10.188f, -60.989f,10.354f, END };
static const float geo_guadeloupe[]= { -61.193f,16.3f, -61.275f,16.431f, -61.418f,16.507f, -61.582f,16.507f, -61.725f,16.431f, -61.807f,16.3f, -61.807f,16.15f, -61.725f,16.019f, -61.582f,15.943f, -61.418f,15.943f, -61.275f,16.019f, -61.193f,16.15f, END };
static const float geo_martinique[]= { -60.795f,14.719f, -60.856f,14.837f, -60.963f,14.906f, -61.087f,14.906f, -61.194f,14.837f, -61.255f,14.719f, -61.255f,14.581f, -61.194f,14.463f, -61.087f,14.394f, -60.963f,14.394f, -60.856f,14.463f, -60.795f,14.581f, END };
static const float geo_barbados[]  = { -59.396f,13.241f, -59.438f,13.312f, -59.509f,13.354f, -59.591f,13.354f, -59.662f,13.312f, -59.704f,13.241f, -59.704f,13.159f, -59.662f,13.088f, -59.591f,13.046f, -59.509f,13.046f, -59.438f,13.088f, -59.396f,13.159f, END };
static const float geo_antigua[]   = { -61.595f,17.109f, -61.65f,17.169f, -61.745f,17.203f, -61.855f,17.203f, -61.95f,17.169f, -62.005f,17.109f, -62.005f,17.041f, -61.95f,16.981f, -61.855f,16.947f, -61.745f,16.947f, -61.65f,16.981f, -61.595f,17.041f, END };
static const float geo_stkitts[]   = { -62.495f,17.341f, -62.55f,17.412f, -62.645f,17.454f, -62.755f,17.454f, -62.85f,17.412f, -62.905f,17.341f, -62.905f,17.259f, -62.85f,17.188f, -62.755f,17.146f, -62.645f,17.146f, -62.55f,17.188f, -62.495f,17.259f, END };
static const float geo_curacao[]   = { -68.694f,12.255f, -68.763f,12.35f, -68.881f,12.405f, -69.019f,12.405f, -69.137f,12.35f, -69.206f,12.255f, -69.206f,12.145f, -69.137f,12.05f, -69.019f,11.995f, -68.881f,11.995f, -68.763f,12.05f, -68.694f,12.145f, END };
static const float geo_bahamas1[]  = { -76.987f,25.155f, -77.138f,25.25f, -77.399f,25.305f, -77.701f,25.305f, -77.962f,25.25f, -78.113f,25.155f, -78.113f,25.045f, -77.962f,24.95f, -77.701f,24.895f, -77.399f,24.895f, -77.138f,24.95f, -76.987f,25.045f, END }; // new providence+andros approx
static const float geo_bahamas2[]  = { -77.689f,26.719f, -77.813f,26.837f, -78.027f,26.906f, -78.273f,26.906f, -78.487f,26.837f, -78.611f,26.719f, -78.611f,26.581f, -78.487f,26.463f, -78.273f,26.394f, -78.027f,26.394f, -77.813f,26.463f, -77.689f,26.581f, END }; // grand bahama
static const float geo_bermuda[]   = { -64.646f,32.359f, -64.688f,32.419f, -64.759f,32.453f, -64.841f,32.453f, -64.912f,32.419f, -64.954f,32.359f, -64.954f,32.291f, -64.912f,32.231f, -64.841f,32.197f, -64.759f,32.197f, -64.688f,32.231f, -64.646f,32.291f, END };
static const float geo_sicily[]    = { 12.5f,38.2f, 15.6f,38.3f, 15.2f,37, 12.5f,37.5f, END };
static const float geo_sardinia[]  = { 8.2f,41, 9.8f,41.1f, 9.6f,39, 8.5f,39, END };
static const float geo_corsica[]   = { 8.6f,43, 9.5f,42.8f, 9.3f,41.4f, 8.7f,41.6f, END };
static const float geo_crete[]     = { 23.5f,35.55f, 26.3f,35.35f, 26.0f,35.0f, 23.8f,35.05f, END };
static const float geo_cyprus[]    = { 32.3f,35.1f, 34.5f,35.6f, 33,34.7f, END };
static const float geo_mallorca[]  = { 2.4f,39.6f, 3.4f,39.8f, 3.1f,39.3f, 2.5f,39.4f, END };
static const float geo_zealand[]   = { 11.5f,55, 12.6f,55.7f, 12,56.1f, 11,55.7f, END };
static const float geo_gotland[]   = { 18.2f,57.9f, 18.9f,57.9f, 18.7f,57.1f, 18.2f,57.2f, END };
static const float geo_madeira[]   = { -16.693f,32.791f, -16.775f,32.862f, -16.918f,32.904f, -17.082f,32.904f, -17.225f,32.862f, -17.307f,32.791f, -17.307f,32.709f, -17.225f,32.638f, -17.082f,32.596f, -16.918f,32.596f, -16.775f,32.638f, -16.693f,32.709f, END };
static const float geo_tenerife[]  = { -16.192f,28.382f, -16.288f,28.525f, -16.454f,28.607f, -16.646f,28.607f, -16.812f,28.525f, -16.908f,28.382f, -16.908f,28.218f, -16.812f,28.075f, -16.646f,27.993f, -16.454f,27.993f, -16.288f,28.075f, -16.192f,28.218f, END };
static const float geo_grancanaria[]={ -15.294f,28.019f, -15.363f,28.137f, -15.481f,28.206f, -15.619f,28.206f, -15.737f,28.137f, -15.806f,28.019f, -15.806f,27.881f, -15.737f,27.763f, -15.619f,27.694f, -15.481f,27.694f, -15.363f,27.763f, -15.294f,27.881f, END };
static const float geo_azores1[]   = { -28.293f,38.755f, -28.375f,38.85f, -28.518f,38.905f, -28.682f,38.905f, -28.825f,38.85f, -28.907f,38.755f, -28.907f,38.645f, -28.825f,38.55f, -28.682f,38.495f, -28.518f,38.495f, -28.375f,38.55f, -28.293f,38.645f, END };
static const float geo_azores2[]   = { -25.394f,37.841f, -25.463f,37.912f, -25.581f,37.954f, -25.719f,37.954f, -25.837f,37.912f, -25.906f,37.841f, -25.906f,37.759f, -25.837f,37.688f, -25.719f,37.646f, -25.581f,37.646f, -25.463f,37.688f, -25.394f,37.759f, END };
static const float geo_azores3[]   = { -26.995f,38.759f, -27.05f,38.819f, -27.145f,38.853f, -27.255f,38.853f, -27.35f,38.819f, -27.405f,38.759f, -27.405f,38.691f, -27.35f,38.631f, -27.255f,38.597f, -27.145f,38.597f, -27.05f,38.631f, -26.995f,38.691f, END };
static const float geo_capeverde[] = { -23.289f,15.119f, -23.413f,15.237f, -23.627f,15.306f, -23.873f,15.306f, -24.087f,15.237f, -24.211f,15.119f, -24.211f,14.981f, -24.087f,14.863f, -23.873f,14.794f, -23.627f,14.794f, -23.413f,14.863f, -23.289f,14.981f, END };
static const float geo_capeverde2[]= { -24.795f,16.991f, -24.85f,17.062f, -24.945f,17.104f, -25.055f,17.104f, -25.15f,17.062f, -25.205f,16.991f, -25.205f,16.909f, -25.15f,16.838f, -25.055f,16.796f, -24.945f,16.796f, -24.85f,16.838f, -24.795f,16.909f, END };
static const float geo_mozisland[] = { 40.904f,-15.009f, 40.862f,-14.938f, 40.791f,-14.896f, 40.709f,-14.896f, 40.638f,-14.938f, 40.596f,-15.009f, 40.596f,-15.091f, 40.638f,-15.162f, 40.709f,-15.204f, 40.791f,-15.204f, 40.862f,-15.162f, 40.904f,-15.091f, END };
static const float geo_longisland[]= { -73.8f,40.8f, -72.0f,41.2f, -72.0f,40.95f, -73.7f,40.55f, END };
static const float geo_capebreton[]= { -61.5f,46.3f, -60.0f,47.0f, -59.8f,46.0f, -60.8f,45.6f, END };
static const float geo_princeedward[]={ -64.2f,46.5f, -62.0f,46.5f, -62.2f,46.1f, -63.8f,46.2f, END };
static const float geo_anticosti[] = { -64.5f,49.9f, -61.8f,49.3f, -62.0f,49.0f, -64.3f,49.6f, END };
static const float geo_vancouver[] = { -128.5f,50.8f, -124.5f,49.5f, -123.5f,48.4f, -125.5f,48.7f, -127.5f,50.2f, END };
static const float geo_marajo[]    = { -50.5f,-0.2f, -48.5f,-0.1f, -48.8f,-1.2f, -50.3f,-1.0f, END };

// The Mediterranean island world (one polygon per island or tight group). Authored for the
// 1/8-degree raster *and* the quarter-degree navigation grid: every island spans at least two
// cell-centre rows and columns, so its core navigation cell goes majority-land and routes bend
// around it (a one-cell strip stays sailable under the nav grid's majority rule and puts
// fleets visually on the rocks), and neighbouring chains keep a full water row between them so
// the channels survive. The Dalmatian chain and the Aegean are where the campaign lives, so
// they are authored as vector data rather than hoped for from a heightmap.
static const float geo_dugi_kornati[] = { 15.406f,43.872f, 15.337f,43.936f, 15.219f,43.973f, 15.081f,43.973f, 14.963f,43.936f, 14.894f,43.872f, 14.894f,43.798f, 14.963f,43.734f, 15.081f,43.697f, 15.219f,43.697f, 15.337f,43.734f, 15.406f,43.798f, END };
static const float geo_vis[]       = { 15.953f,43.122f, 15.916f,43.186f, 15.852f,43.223f, 15.778f,43.223f, 15.714f,43.186f, 15.677f,43.122f, 15.677f,43.048f, 15.714f,42.984f, 15.778f,42.947f, 15.852f,42.947f, 15.916f,42.984f, 15.953f,43.048f, END };
// The southern Dalmatian islands (0.22.3): the merged Hvar+Brac and Korcula+Mljet blobs sat
// where Peljesac's channels run, so each island is its own thin polygon now, shifted at most
// a cell from its true place so that one water row survives between every neighbour: the
// Brac channel, the Hvar channel, the Neretva channel north of Peljesac, the Peljesac channel
// off Korcula and the Mljet channel. Thin strips are drawn at a cell, so the nav grid's
// majority rule still lets a fleet sail across one - see KNOWN_ISSUES.
static const float geo_brac[]    = { 16.45f,43.33f, 16.65f,43.36f, 16.80f,43.33f, 16.80f,43.28f, 16.60f,43.265f, 16.45f,43.29f, END };
static const float geo_hvar[]    = { 16.42f,43.17f, 16.70f,43.19f, 17.00f,43.17f, 17.18f,43.13f, 17.00f,43.115f, 16.70f,43.12f, 16.42f,43.13f, END };
static const float geo_korcula[] = { 16.66f,42.845f, 16.90f,42.855f, 17.16f,42.845f, 17.20f,42.82f, 16.95f,42.795f, 16.68f,42.80f, END };
static const float geo_mljet[]   = { 17.33f,42.785f, 17.50f,42.80f, 17.62f,42.775f, 17.60f,42.74f, 17.50f,42.73f, 17.33f,42.745f, END };
static const float geo_lastovo[] = { 16.82f,42.72f, 16.95f,42.725f, 16.95f,42.675f, 16.82f,42.675f, END };
static const float geo_kefalonia[] = { 20.604f,38.241f, 20.562f,38.312f, 20.491f,38.354f, 20.409f,38.354f, 20.338f,38.312f, 20.296f,38.241f, 20.296f,38.159f, 20.338f,38.088f, 20.409f,38.046f, 20.491f,38.046f, 20.562f,38.088f, 20.604f,38.159f, END };
static const float geo_zakynthos[] = { 20.904f,37.772f, 20.862f,37.836f, 20.791f,37.873f, 20.709f,37.873f, 20.638f,37.836f, 20.596f,37.772f, 20.596f,37.698f, 20.638f,37.634f, 20.709f,37.597f, 20.791f,37.597f, 20.862f,37.634f, 20.904f,37.698f, END };
static const float geo_kythira[]   = { 23.154f,36.291f, 23.112f,36.362f, 23.041f,36.404f, 22.959f,36.404f, 22.888f,36.362f, 22.846f,36.291f, 22.846f,36.209f, 22.888f,36.138f, 22.959f,36.096f, 23.041f,36.096f, 23.112f,36.138f, 23.154f,36.209f, END };
static const float geo_rhodes[]    = { 28.105f,36.173f, 28.05f,36.256f, 27.955f,36.304f, 27.845f,36.304f, 27.75f,36.256f, 27.695f,36.173f, 27.695f,36.077f, 27.75f,35.994f, 27.845f,35.946f, 27.955f,35.946f, 28.05f,35.994f, 28.105f,36.077f, END };
static const float geo_karpathos[] = { 27.254f,35.641f, 27.212f,35.712f, 27.141f,35.754f, 27.059f,35.754f, 26.988f,35.712f, 26.946f,35.641f, 26.946f,35.559f, 26.988f,35.488f, 27.059f,35.446f, 27.141f,35.446f, 27.212f,35.488f, 27.254f,35.559f, END };
static const float geo_kos[]       = { 27.154f,36.872f, 27.112f,36.936f, 27.041f,36.973f, 26.959f,36.973f, 26.888f,36.936f, 26.846f,36.872f, 26.846f,36.798f, 26.888f,36.734f, 26.959f,36.697f, 27.041f,36.697f, 27.112f,36.734f, 27.154f,36.798f, END };
static const float geo_samos[]     = { 26.804f,37.772f, 26.762f,37.836f, 26.691f,37.873f, 26.609f,37.873f, 26.538f,37.836f, 26.496f,37.772f, 26.496f,37.698f, 26.538f,37.634f, 26.609f,37.597f, 26.691f,37.597f, 26.762f,37.634f, 26.804f,37.698f, END };
static const float geo_chios[]     = { 26.154f,38.391f, 26.112f,38.462f, 26.041f,38.504f, 25.959f,38.504f, 25.888f,38.462f, 25.846f,38.391f, 25.846f,38.309f, 25.888f,38.238f, 25.959f,38.196f, 26.041f,38.196f, 26.112f,38.238f, 26.154f,38.309f, END };
static const float geo_lesbos[]    = { 26.205f,39.191f, 26.15f,39.262f, 26.055f,39.304f, 25.945f,39.304f, 25.85f,39.262f, 25.795f,39.191f, 25.795f,39.109f, 25.85f,39.038f, 25.945f,38.996f, 26.055f,38.996f, 26.15f,39.038f, 26.205f,39.109f, END };
static const float geo_limnos[]    = { 25.304f,39.922f, 25.262f,39.986f, 25.191f,40.023f, 25.109f,40.023f, 25.038f,39.986f, 24.996f,39.922f, 24.996f,39.848f, 25.038f,39.784f, 25.109f,39.747f, 25.191f,39.747f, 25.262f,39.784f, 25.304f,39.848f, END };
static const float geo_thasos[]    = { 24.783f,40.672f, 24.745f,40.736f, 24.678f,40.773f, 24.602f,40.773f, 24.535f,40.736f, 24.497f,40.672f, 24.497f,40.598f, 24.535f,40.534f, 24.602f,40.497f, 24.678f,40.497f, 24.745f,40.534f, 24.783f,40.598f, END };
static const float geo_skyros[]    = { 24.683f,38.922f, 24.645f,38.986f, 24.578f,39.023f, 24.502f,39.023f, 24.435f,38.986f, 24.397f,38.922f, 24.397f,38.848f, 24.435f,38.784f, 24.502f,38.747f, 24.578f,38.747f, 24.645f,38.784f, 24.683f,38.848f, END };
static const float geo_sporades[]  = { 23.754f,39.222f, 23.712f,39.286f, 23.641f,39.323f, 23.559f,39.323f, 23.488f,39.286f, 23.446f,39.222f, 23.446f,39.148f, 23.488f,39.084f, 23.559f,39.047f, 23.641f,39.047f, 23.712f,39.084f, 23.754f,39.148f, END };
static const float geo_andros_tinos[] = { 25.154f,37.841f, 25.106f,37.912f, 25.023f,37.954f, 24.927f,37.954f, 24.844f,37.912f, 24.796f,37.841f, 24.796f,37.759f, 24.844f,37.688f, 24.927f,37.646f, 25.023f,37.646f, 25.106f,37.688f, 25.154f,37.759f, END };
static const float geo_paros_naxos[]  = { 25.505f,37.191f, 25.45f,37.262f, 25.355f,37.304f, 25.245f,37.304f, 25.15f,37.262f, 25.095f,37.191f, 25.095f,37.109f, 25.15f,37.038f, 25.245f,36.996f, 25.355f,36.996f, 25.45f,37.038f, 25.505f,37.109f, END };
static const float geo_kea_kythnos[]  = { 24.503f,37.491f, 24.469f,37.562f, 24.409f,37.604f, 24.341f,37.604f, 24.281f,37.562f, 24.247f,37.491f, 24.247f,37.409f, 24.281f,37.338f, 24.341f,37.296f, 24.409f,37.296f, 24.469f,37.338f, 24.503f,37.409f, END };
static const float geo_milos[]     = { 24.553f,36.772f, 24.519f,36.836f, 24.459f,36.873f, 24.391f,36.873f, 24.331f,36.836f, 24.297f,36.772f, 24.297f,36.698f, 24.331f,36.634f, 24.391f,36.597f, 24.459f,36.597f, 24.519f,36.634f, 24.553f,36.698f, END };
static const float geo_santorini[] = { 25.553f,36.459f, 25.519f,36.519f, 25.459f,36.553f, 25.391f,36.553f, 25.331f,36.519f, 25.297f,36.459f, 25.297f,36.391f, 25.331f,36.331f, 25.391f,36.297f, 25.459f,36.297f, 25.519f,36.331f, 25.553f,36.391f, END };
static const float geo_menorca[]   = { 4.104f,40.022f, 4.062f,40.086f, 3.991f,40.123f, 3.909f,40.123f, 3.838f,40.086f, 3.796f,40.022f, 3.796f,39.948f, 3.838f,39.884f, 3.909f,39.847f, 3.991f,39.847f, 4.062f,39.884f, 4.104f,39.948f, END };
static const float geo_ibiza[]     = { 1.504f,39.022f, 1.462f,39.086f, 1.391f,39.123f, 1.309f,39.123f, 1.238f,39.086f, 1.196f,39.022f, 1.196f,38.948f, 1.238f,38.884f, 1.309f,38.847f, 1.391f,38.847f, 1.462f,38.884f, 1.504f,38.948f, END };
static const float geo_malta[]     = { 14.504f,35.922f, 14.462f,35.986f, 14.391f,36.023f, 14.309f,36.023f, 14.238f,35.986f, 14.196f,35.922f, 14.196f,35.848f, 14.238f,35.784f, 14.309f,35.747f, 14.391f,35.747f, 14.462f,35.784f, 14.504f,35.848f, END };

static const float *geo_land[] = {
    geo_north_america, geo_south_america, geo_tierra_del_fuego, geo_eurasia, geo_africa,
    geo_britain, geo_ireland, geo_iceland, geo_greenland, geo_baffin, geo_newfoundland,
    geo_cuba, geo_hispaniola, geo_jamaica, geo_puertorico, geo_trinidad, geo_guadeloupe,
    geo_martinique, geo_barbados, geo_antigua, geo_stkitts, geo_curacao, geo_bahamas1,
    geo_bahamas2, geo_bermuda, geo_sicily, geo_sardinia, geo_corsica, geo_crete, geo_cyprus,
    geo_mallorca, geo_zealand, geo_gotland, geo_madeira, geo_tenerife, geo_grancanaria,
    geo_azores1, geo_azores2, geo_azores3, geo_capeverde, geo_capeverde2, geo_mozisland,
    geo_longisland, geo_capebreton, geo_princeedward, geo_anticosti, geo_vancouver, geo_marajo,
    geo_dugi_kornati, geo_vis, geo_brac, geo_hvar, geo_korcula, geo_mljet, geo_lastovo,
    geo_kefalonia, geo_zakynthos, geo_kythira, geo_rhodes, geo_karpathos, geo_kos, geo_samos,
    geo_chios, geo_lesbos, geo_limnos, geo_thasos, geo_skyros, geo_sporades, geo_andros_tinos,
    geo_paros_naxos, geo_kea_kythnos, geo_milos, geo_santorini,
    geo_menorca, geo_ibiza, geo_malta,
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
// Ston (0.22.3): the Mali Ston bay behind Peljesac, open north-west into the Neretva channel,
// and the Ston channel from the south-west - the salt town has the sea on both sides of its
// isthmus, which a 7 km raster could never resolve on its own
static const float strait_malostonski[] = { 17.62f,42.944f, 17.52f,43.006f, 17.32f,43.006f, 17.25f,43.069f, 17.05f,43.069f, 16.95f,43.10f, END };
static const float strait_stonski[]     = { 17.50f,42.80f, 17.62f,42.805f, 17.705f,42.825f, END };

static const geo_strait_t geo_straits[] = {
    {0.12f, strait_gibraltar}, {0.12f, strait_oresund}, {0.25f, strait_kattegat}, {0.15f, strait_belt},
    {0.12f, strait_messina}, {0.15f, strait_bonifacio}, {0.25f, strait_babelmandeb}, {0.25f, strait_belleisle},
    {0.35f, strait_cabot}, {0.15f, strait_dardanelles}, {0.2f, strait_magellan}, {0.3f, strait_florida},
    {0.3f, strait_windward}, {0.3f, strait_mona}, {0.25f, strait_dover}, {0.2f, strait_bosporus_to_marmara},
    {0.35f, strait_st_lawrence}, {0.6f, strait_hudson},
    {0.028f, strait_malostonski}, {0.022f, strait_stonski},
};


#ifdef __cplusplus
}
#endif

#endif
