/* Static game-rule tables recovered from CIV.EXE (see tools/extract_rules.py). */
#ifndef RULES_H
#define RULES_H

#define NUM_GOVERNMENTS    6
#define NUM_TRADE_GOODS    8
#define NUM_TERRAIN_TYPES  24   /* 0-11 base terrain, 12-23 the matching special resource */
#define NUM_ADVANCES       72   /* 0-66 real, 67-70 unused placeholders, 71 Future Tech */
#define NUM_BUILDINGS      46   /* 0 NONE, 1-24 improvements, 25-45 wonders */
#define NUM_UNIT_TYPES     28
#define NUM_CIV_TYPES      16   /* player slot i uses civ i or i+8; 0 and 8 are barbarians */

#define FIRST_WONDER       25
#define NUM_WONDERS        21   /* wonder w is building FIRST_WONDER + w - 1 */
#define TERRAIN_SPECIAL    12   /* add to a base terrain to get its special resource */
#define ADVANCE_NONE       (-1)
#define ADVANCE_FUTURE_TECH 71
enum { ADV_MONARCHY = 5, ADV_NAVIGATION = 8, ADV_INVENTION = 20, ADV_WRITING = 22,
       ADV_CEREMONIAL_BURIAL = 25, ADV_MYSTICISM = 26, ADV_RELIGION = 29, ADV_GUNPOWDER = 34,
       ADV_INDUSTRIALIZATION = 35, ADV_COMBUSTION = 37, ADV_FLIGHT = 38, ADV_RAILROAD = 49, ADV_SPACE_FLIGHT = 40, ADV_MASS_PRODUCTION = 41,
       ADV_AUTOMOBILE = 58, ADV_PLASTICS = 60, ADV_FUSION_POWER = 66 };

/* Buildings: city bit (b - 1) of City.buildings; see building_types. */
enum { B_PALACE = 1, B_BARRACKS, B_GRANARY, B_TEMPLE, B_MARKETPLACE, B_LIBRARY, B_COURTHOUSE,
       B_CITY_WALLS, B_AQUEDUCT, B_BANK, B_CATHEDRAL, B_UNIVERSITY, B_MASS_TRANSIT, B_COLOSSEUM,
       B_FACTORY, B_MFG_PLANT, B_SDI, B_RECYCLING, B_POWER_PLANT, B_HYDRO_PLANT, B_NUCLEAR_PLANT,
       B_SS_STRUCTURAL, B_SS_COMPONENT, B_SS_MODULE };
#define BIT(b) (1UL << ((b) - 1))

enum { WONDER_PYRAMIDS = 1, WONDER_HANGING_GARDENS, WONDER_COLOSSUS, WONDER_LIGHTHOUSE,
       WONDER_GREAT_LIBRARY, WONDER_ORACLE, WONDER_GREAT_WALL, WONDER_MAGELLAN, WONDER_MICHELANGELO,
       WONDER_COPERNICUS, WONDER_SHAKESPEARE, WONDER_NEWTON, WONDER_BACH, WONDER_DARWIN,
       WONDER_HOOVER_DAM, WONDER_WOMENS_SUFFRAGE, WONDER_MANHATTAN, WONDER_UNITED_NATIONS,
       WONDER_APOLLO, WONDER_SETI, WONDER_CURE_FOR_CANCER };

enum { GOV_ANARCHY, GOV_DESPOTISM, GOV_MONARCHY, GOV_COMMUNISM, GOV_REPUBLIC, GOV_DEMOCRACY };
#define ADVANCE_NEVER      127

enum { DOMAIN_LAND, DOMAIN_AIR, DOMAIN_SEA };

enum {
    TERRAIN_DESERT, TERRAIN_PLAINS, TERRAIN_GRASSLAND, TERRAIN_FOREST, TERRAIN_HILLS,
    TERRAIN_MOUNTAINS, TERRAIN_TUNDRA, TERRAIN_ARCTIC, TERRAIN_SWAMP, TERRAIN_JUNGLE,
    TERRAIN_OCEAN, TERRAIN_RIVER
};

typedef struct {
    const char *name;
    unsigned char move_cost;
    unsigned char defense;      /* in half steps: 2 = x1.0, 4 = x2.0 */
    unsigned char food, shields, trade;
    unsigned char unk7;
    unsigned char map_colour;   /* pixel value used for this terrain in the .MAP layer */
} TerrainType;

typedef struct {
    const char *name;
    signed char prereq[2];
} AdvanceType;

typedef struct {
    const char *name;
    short cost;                 /* x10 shields */
    short upkeep;               /* gold per turn */
    short prereq;               /* advance, or -1 */
} BuildingType;

typedef struct {
    const char *name;
    short obsolete_by;          /* advance, 127 = never */
    short domain;
    short moves;
    short fuel;                 /* turns aloft for air units */
    short attack, defense;
    short cost;                 /* x10 shields */
    short sight;
    short capacity;             /* units carried */
    short role;
    short prereq;               /* advance, or -1 */
} UnitType;

typedef struct {
    const char *leader, *plural, *adjective;
    short aggression;           /* AI: -1 peaceful .. +1 warlike */
    short unk1;                 /* AI: -1/0/1 */
    short science_bias;         /* AI: added to the science rate */
    short music;                /* leader's theme (sound number) */
    short unk4;
} CivType;

typedef struct {
    short irrigate, irrigate_turns;
    short mine, mine_turns;
    short unk4, unk5;
} TerrainImprove;

extern const char *const government_names[NUM_GOVERNMENTS];
extern const char *const leader_titles[NUM_GOVERNMENTS];
extern const char *const trade_good_names[NUM_TRADE_GOODS];
extern const TerrainType terrain_types_ext[NUM_TERRAIN_TYPES + 1];
#define terrain_types (terrain_types_ext + 1)   /* terrain_types[-1] is valid: see rules_data.c */
extern const AdvanceType advance_types[NUM_ADVANCES];
extern const BuildingType building_types[NUM_BUILDINGS];
extern const UnitType unit_types[NUM_UNIT_TYPES];
extern const CivType civ_types_ext[NUM_CIV_TYPES + 1];
#define civ_types (civ_types_ext + 1)           /* civ_types[-1] is valid: see rules_data.c */
extern const TerrainImprove terrain_improve[12];
extern const signed char wonder_obsolete[NUM_WONDERS + 1];
extern const signed char around_dx[49], around_dy[49];
extern const signed char city_square_dx[21], city_square_dy[21];
extern const signed char terrain_by_colour[16];
extern const signed char earth_start[NUM_CIV_TYPES][2];
extern const signed char advance_ai[NUM_ADVANCES][2];
extern const char *const civ_short_names[8];
extern const char *const civ_plural_names[8];
extern const char *const civ_leader_names[8];

/* DGROUP's initialised data as shipped (offsets as in the original).  Only for reproducing
 * reads past the end of a table; live game data is in `game`. */
#define DGROUP_INIT_SIZE 0x6AB0
extern const unsigned char dgroup_init[DGROUP_INIT_SIZE];
static inline int dgroup_word(unsigned off)
{
    return off + 1 < DGROUP_INIT_SIZE ? (short)(dgroup_init[off] | dgroup_init[off + 1] << 8) : 0;
}

#endif
