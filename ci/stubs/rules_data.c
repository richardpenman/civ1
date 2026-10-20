/* Stand-in for src/rules_data.c (generated from CIV.EXE, not in the repository) so that the
 * sources compile without the original game: every table empty.  A program built with it
 * does not play; it is only for the compile check (.github/workflows/build.yml). */
#include "rules.h"

const char *const government_names[NUM_GOVERNMENTS];
const char *const leader_titles[NUM_GOVERNMENTS];
const char *const trade_good_names[NUM_TRADE_GOODS];
const TerrainType terrain_types_ext[NUM_TERRAIN_TYPES + 1];
const AdvanceType advance_types[NUM_ADVANCES];
const BuildingType building_types[NUM_BUILDINGS];
const UnitType unit_types[NUM_UNIT_TYPES];
const CivType civ_types_ext[NUM_CIV_TYPES + 1];
const TerrainImprove terrain_improve[12];
const signed char wonder_obsolete[NUM_WONDERS + 1];
const signed char around_dx[49], around_dy[49];
const signed char city_square_dx[21], city_square_dy[21];
const signed char terrain_by_colour[16];
const signed char earth_start[NUM_CIV_TYPES][2];
const signed char advance_ai[NUM_ADVANCES][2];
const char *const civ_short_names[8];
const char *const civ_plural_names[8];
const char *const civ_leader_names[8];
const unsigned char dgroup_init[DGROUP_INIT_SIZE];
