// Static gameplay tables: goods, ship classes, buildings, nations, regions.

#ifndef MARE_TABLES_H
#define MARE_TABLES_H

#include "../core/types.h"
#include "../core/util.h"

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// static tables

// The tables themselves live in tables.cc (one definition, not one per translation unit);
// these are the declarations every call site has always used.
extern const good_info_t     goods[MAX_GOODS];
extern const ship_info_t     ship_types[SHIP_COUNT];
extern const building_info_t buildings[MAX_BUILDINGS];
extern const nation_info_t   nations_info[MAX_NATIONS];
extern const char *const     region_names[REG_COUNT];
#ifdef __cplusplus
}

// Bounds-checked access to the same tables, for code that would rather not index raw arrays.
namespace mare::data {
class Tables {
public:
    static const good_info_t     *good(int i);
    static const ship_info_t     *ship(int i);
    static const building_info_t *building(int i);
    static const nation_info_t   *nation(int i);
    static const char            *region(int i);
};
} // namespace mare::data
#endif

#endif
