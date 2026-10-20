/* Game state.  Every field here is saved in the .SVE file, in the order listed in save.c
 * (which mirrors the original serializer, overlay 11 o_11_09e4).  Names of the form
 * unk_XXXX are blocks not yet identified; XXXX is the original DGROUP address. */
#ifndef GAME_H
#define GAME_H

#include <stdint.h>
#include "rules.h"

#define MAP_W        80
#define MAP_H        50
#define MAX_CIVS     8
#define MAX_CITIES   128
#define MAX_UNITS    128      /* per civ */
#define NUM_CITY_NAMES 256
#define CITY_NAME_LEN  13

#define NONE8        0xFF

/* rules_flags bits (the game always sets 0x0F). */
#define RULE_REAL_IMPROVEMENTS 0x02   /* clear: yields assume every square irrigated/mined/roaded */
#define RULE_GROWTH_LIMITS     0x04   /* aqueduct limit, AI tax/science adjustment */

/* City status bits. */
#define CITY_DISORDER    0x01
#define CITY_COASTAL     0x02
#define CITY_PACK_UNITS  0x04   /* fortified units may be packed into the city record */
#define CITY_HYDRO_OK    0x08   /* near mountains or a river */
#define CITY_AUTOBUILD   0x10   /* pick the next build automatically */
#define CITY_CELEBRATING 0x40   /* "We Love the King Day" */

typedef struct {
    uint32_t buildings;       /* bit (b-1) set => building b present */
    uint8_t  x, y;
    uint8_t  status;          /* 0xFF => slot unused */
    uint8_t  size;
    uint8_t  visible_size;    /* size as last seen by other civs */
    int8_t   producing;       /* >= 0 unit type, < 0 building -id */
    uint8_t  trade;
    uint8_t  owner;
    int16_t  food;
    int16_t  shields;
    uint32_t worked;          /* worked-tile bits; top bits hold specialist counts */
    uint16_t specialists;     /* 2 bits each for the first 8: SPEC_* */
    uint8_t  name;            /* index into city_names */
    uint8_t  trade_routes[3]; /* city ids, 0xFF => none */
    uint8_t  fortified[2];    /* packed fortified units: type | 0x40 veteran, 0xFF none */
} City;

typedef struct {
    uint8_t status;
    uint8_t x, y;
    uint8_t type;             /* 0xFF => slot unused */
    uint8_t moves;            /* remaining, in thirds */
    uint8_t special;          /* fuel / work counter */
    uint8_t goto_x, goto_y;
    uint8_t unk8;
    uint8_t visible;          /* civ mask that can see the unit */
    uint8_t next;             /* next unit in the same stack, 0xFF => none */
    uint8_t home_city;        /* 0xFF => none */
} Unit;

typedef struct {
    int16_t  turn;
    int16_t  human_civ;
    int16_t  human_mask;
    uint16_t world_seed;
    int16_t  year;            /* negative => BC */
    int16_t  difficulty;
    int16_t  civs_alive;      /* bit mask */
    int16_t  human_research;               /* advance the human is researching, -1 none */
    char     leader_names[MAX_CIVS][14];
    char     civ_names[MAX_CIVS][12];
    char     civ_adjectives[MAX_CIVS][11];
    int16_t  treasury[MAX_CIVS];
    int16_t  research_progress[MAX_CIVS];
    int16_t  units_active[MAX_CIVS][NUM_UNIT_TYPES];
    int16_t  units_in_production[MAX_CIVS][NUM_UNIT_TYPES];
    int16_t  advances_known[MAX_CIVS];
    uint16_t advances[MAX_CIVS][5];        /* bit set, 72 advances */
    int16_t  government[MAX_CIVS];
    int16_t  unk_f5b8[MAX_CIVS][16];
    int16_t  diplomacy[MAX_CIVS][MAX_CIVS];
    int16_t  city_count[MAX_CIVS];
    int16_t  unit_count[MAX_CIVS];
    int16_t  land_count[MAX_CIVS];
    int16_t  settler_count[MAX_CIVS];
    int16_t  citizen_count[MAX_CIVS];
    int16_t  military_power[MAX_CIVS];
    int16_t  civ_rank[MAX_CIVS];
    int16_t  tax_rate[MAX_CIVS];
    int16_t  score[MAX_CIVS];
    int16_t  unk_e204[MAX_CIVS];           /* turn of last contact with the human (-1/-2 special) */
    int16_t  capital_x[MAX_CIVS];          /* x of the palace city (set when it is built) */
    int16_t  civ_type[MAX_CIVS];           /* index into civ_types */
    int16_t  unk_6c2c[MAX_CIVS][16];
    int16_t  unk_8f9a[MAX_CIVS][16];
    int16_t  unk_6b16[MAX_CIVS][16];
    int16_t  land_area[64];                /* squares per continent */
    int16_t  sea_area[64];                 /* squares per ocean */
    int16_t  unk_c10e[16];
    uint8_t  unk_far_2786_0000[1200];
    uint8_t  unk_far_2786_0fe8[1200];
    City     cities[MAX_CITIES];
    UnitType unit_types[NUM_UNIT_TYPES];   /* the game saves its (mutable) unit table */
    char     unit_type_names[NUM_UNIT_TYPES][12];
    Unit     units[MAX_CIVS][MAX_UNITS];
    uint8_t  visibility[MAP_W][MAP_H];     /* bit c: square known to civ c; bit 0: hut entered */
    int8_t   ai_spot_type[MAX_CIVS][16];   /* AI points of interest; -1 none */
    uint8_t  ai_spot_value[MAX_CIVS][16];
    int8_t   ai_spot_x[MAX_CIVS][16];
    int8_t   ai_spot_y[MAX_CIVS][16];
    int16_t  first_discoverer[NUM_ADVANCES];   /* low 3 bits: civ that discovered it first */
    int16_t  kills[MAX_CIVS][MAX_CIVS];    /* units of [victim] destroyed by [killer] */
    char     city_names[NUM_CITY_NAMES][CITY_NAME_LEN];
    int16_t  replay_len;                   /* bytes of replay used */
    uint8_t  replay[4096];
    int16_t  wonder_city[NUM_WONDERS + 1];  /* city holding wonder w, -1 => not built */
    int16_t  units_lost[MAX_CIVS][NUM_UNIT_TYPES];
    uint8_t  advance_source[MAX_CIVS][NUM_ADVANCES];  /* who gave it: civ, 0 self, 0xFE Great Library */
    int16_t  pollution_count;
    int16_t  unk_c10a;
    int16_t  unk_ea56;
    int16_t  unk_19c0;
    uint8_t  land_links[260];              /* block connectivity for goto, 20 x 13 */
    int16_t  unk_7fcc;
    int16_t  future_tech_count;
    int16_t  rules_flags;                  /* always 0x0F; see RULE_* */
    int16_t  science_rate[MAX_CIVS];       /* tenths; luxury = 10 - tax - science */
    int16_t  unk_1c26;
    int16_t  unk_6c1c[MAX_CIVS];
    uint8_t  unk_far_2786_0740_bytes[1440]; /* spaceships: [8] x 15 x 12 */
    int16_t  unk_4c88;
    int16_t  unk_4c84;
    int16_t  unk_4c86;
    int16_t  unk_ed30[MAX_CIVS];
    int16_t  unk_e83e[12];
    int16_t  unk_c142[12];
    uint8_t  city_name_x[NUM_CITY_NAMES];  /* where each name is in use; 0xFF free */
    uint8_t  city_name_y[NUM_CITY_NAMES];
    int16_t  unk_1c20;
    int16_t  peace_turns;                  /* turns since civs other than barbarians fought */
    int16_t  unk_e214;
    int16_t  unk_ee0c[MAX_CIVS];
    int16_t  unk_f7c8[MAX_CIVS];
    int16_t  unk_ee3e;
} SaveData;

/* The .MAP file is a 320x200 picture; the world lives in 80x50 blocks of it. */
/* Layers (pixel coordinates of the square's top-left in the page):
 *   (x,     y)        terrain colour, see terrain_types[].map_colour
 *   (x,     y+50)     continent / ocean id
 *   (x+80,  y+50)     unknown
 *   (x+160, y)        bit 3: units present, bits 0-2: their owner
 *   (x,     y+100/150) improvements, low / high nibble (IMP_*)
 *   (x+80,  y+100/150) improvements as last seen by the human player
 * The rest of the page is used as a sprite cache. */
#define MAPL_PAGE_W 320
#define MAPL_PAGE_H 200

enum {
    IMP_CITY       = 0x01,
    IMP_IRRIGATION = 0x02,
    IMP_MINE       = 0x04,
    IMP_ROAD       = 0x08,
    IMP_RAILROAD   = 0x10,
    IMP_FORTRESS   = 0x20,
    IMP_POLLUTION  = 0x40,    /* not yet confirmed */
    IMP_UNKNOWN80  = 0x80
};

typedef struct {
    uint8_t pixels[MAPL_PAGE_H][MAPL_PAGE_W];
} MapPage;

typedef struct {
    SaveData s;
    MapPage  map;
} Game;

extern Game game;
extern const char *data_dir;    /* the original game's files (PIC, TXT, ...) */

/* Unit status bits. */
#define UNIT_SENTRY      0x01
#define UNIT_ROAD        0x02   /* building a road (with IRRIGATE+MINE: fortress) */
#define UNIT_FORTIFYING  0x04
#define UNIT_FORTIFIED   0x08
#define UNIT_GOTO        0x10
#define UNIT_VETERAN     0x20
#define UNIT_IRRIGATE    0x40
#define UNIT_MINE        0x80

/* Diplomacy flags, game.s.diplomacy[civ][other]. */
#define DIP_CONTACT      0x01
#define DIP_PEACE        0x02
#define DIP_MET          0x10
#define DIP_EMBASSY      0x40

enum { UNIT_SETTLERS = 0, UNIT_TRIREME = 16, UNIT_SUBMARINE = 22, UNIT_CARRIER = 23,
       UNIT_NUCLEAR = 25, UNIT_DIPLOMAT = 26, UNIT_CARAVAN = 27 };

/* State the original keeps in non-saved globals (DGROUP BSS); see game.c. */
extern int cur_player;        /* g_6b10: civ whose units are moving */
extern int stack_owner;       /* g_e70e: owner found by the last unit_at() */
extern int quiet;             /* g_20f4: create/delete units without display updates */
extern int count_losses;      /* g_7058: delete_unit() counts units lost */
extern int show_all;          /* g_e726: debug "see everything" */
extern int replay_suppress;   /* g_38e6: -1 normally */
extern int cur_civ;           /* g_64ca: owner of the city being processed */

/* Ported functions not finished yet: fail loudly if a game reaches them. */
void not_ported(const char *what);
void reset_transient_state(void);
#define NOT_PORTED(what) not_ported(what)

#endif
