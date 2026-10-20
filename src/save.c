/* .SVE files: the globals below written back to back, in exactly this order, with no
 * header (overlay 11, o_11_09e4).  All values are little-endian. */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pic.h"
#include "save.h"

enum { K_BYTES, K_WORDS, K_CITIES, K_UNIT_TYPES, K_UNITS };

typedef struct {
    int    kind;
    size_t offset;
    int    count;            /* bytes for K_BYTES, elements otherwise */
    const char *name;
} Block;

#define B(f)  { K_BYTES, offsetof(SaveData, f), sizeof(((SaveData *)0)->f), #f }
#define W(f)  { K_WORDS, offsetof(SaveData, f), sizeof(((SaveData *)0)->f) / 2, #f }

static const Block layout[] = {
    W(turn), W(human_civ), W(human_mask), W(world_seed), W(year), W(difficulty),
    W(civs_alive), W(human_research),
    B(leader_names), B(civ_names), B(civ_adjectives),
    W(treasury), W(research_progress), W(units_active), W(units_in_production),
    W(advances_known), W(advances), W(government), W(unk_f5b8), W(diplomacy),
    W(city_count), W(unit_count), W(land_count), W(settler_count), W(citizen_count),
    W(military_power), W(civ_rank), W(tax_rate), W(score), W(unk_e204), W(capital_x),
    W(civ_type), W(unk_6c2c), W(unk_8f9a), W(unk_6b16), W(land_area), W(sea_area), W(unk_c10e),
    B(unk_far_2786_0000), B(unk_far_2786_0fe8),
    { K_CITIES, offsetof(SaveData, cities), MAX_CITIES, "cities" },
    { K_UNIT_TYPES, offsetof(SaveData, unit_types), NUM_UNIT_TYPES, "unit_types" },
    { K_UNITS, offsetof(SaveData, units), MAX_CIVS * MAX_UNITS, "units" },
    B(visibility), B(ai_spot_type), B(ai_spot_value), B(ai_spot_x), B(ai_spot_y),
    W(first_discoverer), W(kills), B(city_names), W(replay_len), B(replay),
    W(wonder_city), W(units_lost), B(advance_source),
    W(pollution_count), W(unk_c10a), W(unk_ea56), W(unk_19c0), B(land_links), W(unk_7fcc),
    W(future_tech_count), W(rules_flags), W(science_rate), W(unk_1c26), W(unk_6c1c), B(unk_far_2786_0740_bytes),
    W(unk_4c88), W(unk_4c84), W(unk_4c86), W(unk_ed30), W(unk_e83e), W(unk_c142),
    B(city_name_x), B(city_name_y),
    W(unk_1c20), W(peace_turns), W(unk_e214), W(unk_ee0c), W(unk_f7c8), W(unk_ee3e),
};

#define CITY_BYTES 28
#define UNIT_TYPE_BYTES 34
#define UNIT_BYTES 12

static unsigned get16(const uint8_t *p) { return p[0] | p[1] << 8; }
static uint32_t get32(const uint8_t *p) { return get16(p) | (uint32_t)get16(p + 2) << 16; }
static void put16(uint8_t *p, unsigned v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v) { put16(p, v & 0xFFFF); put16(p + 2, v >> 16); }

static void city_get(City *c, const uint8_t *p)
{
    c->buildings = get32(p);
    c->x = p[4];
    c->y = p[5];
    c->status = p[6];
    c->size = p[7];
    c->visible_size = p[8];
    c->producing = (int8_t)p[9];
    c->trade = p[10];
    c->owner = p[11];
    c->food = (int16_t)get16(p + 12);
    c->shields = (int16_t)get16(p + 14);
    c->worked = get32(p + 16);
    c->specialists = get16(p + 20);
    c->name = p[22];
    memcpy(c->trade_routes, p + 23, 3);
    memcpy(c->fortified, p + 26, 2);
}

static void city_put(const City *c, uint8_t *p)
{
    put32(p, c->buildings);
    p[4] = c->x;
    p[5] = c->y;
    p[6] = c->status;
    p[7] = c->size;
    p[8] = c->visible_size;
    p[9] = (uint8_t)c->producing;
    p[10] = c->trade;
    p[11] = c->owner;
    put16(p + 12, (uint16_t)c->food);
    put16(p + 14, (uint16_t)c->shields);
    put32(p + 16, c->worked);
    put16(p + 20, c->specialists);
    p[22] = c->name;
    memcpy(p + 23, c->trade_routes, 3);
    memcpy(p + 26, c->fortified, 2);
}

static void unit_type_get(SaveData *s, int i, const uint8_t *p)
{
    UnitType *u = &s->unit_types[i];
    short *f = &u->obsolete_by;
    int k;

    memcpy(s->unit_type_names[i], p, 12);
    u->name = s->unit_type_names[i];
    for (k = 0; k < 11; k++)
        f[k] = (short)get16(p + 12 + 2 * k);
}

static void unit_type_put(const SaveData *s, int i, uint8_t *p)
{
    const short *f = &s->unit_types[i].obsolete_by;
    int k;

    memcpy(p, s->unit_type_names[i], 12);
    for (k = 0; k < 11; k++)
        put16(p + 12 + 2 * k, (uint16_t)f[k]);
}

static void unit_get(Unit *u, const uint8_t *p) { memcpy(u, p, UNIT_BYTES); }
static void unit_put(const Unit *u, uint8_t *p) { memcpy(p, u, UNIT_BYTES); }

static long block_bytes(const Block *b)
{
    switch (b->kind) {
    case K_BYTES:      return b->count;
    case K_WORDS:      return 2L * b->count;
    case K_CITIES:     return (long)CITY_BYTES * b->count;
    case K_UNIT_TYPES: return (long)UNIT_TYPE_BYTES * b->count;
    default:           return (long)UNIT_BYTES * b->count;
    }
}

/* The field at byte `pos` of a .SVE file, e.g. "units[6][122].x" (tools/lockstep.py). */
void save_field_name(long pos, char *out, size_t n)
{
    static const char *const city_f[28] = { "buildings", "buildings+1", "buildings+2", "buildings+3", "x", "y",
        "status", "size", "visible_size", "producing", "trade", "owner", "food", "food+1", "shields", "shields+1",
        "worked", "worked+1", "worked+2", "worked+3", "specialists", "specialists+1", "name", "trade_routes[0]",
        "trade_routes[1]", "trade_routes[2]", "fortified[0]", "fortified[1]" };
    static const char *const unit_f[12] = { "status", "x", "y", "type", "moves", "special", "goto_x", "goto_y",
        "unk8", "visible", "next", "home_city" };
    size_t i;
    long at = 0, r;

    for (i = 0; i < sizeof layout / sizeof layout[0]; i++) {
        const Block *b = &layout[i];
        long len = block_bytes(b);
        if (pos < at + len) {
            r = pos - at;
            switch (b->kind) {
            case K_CITIES:
                snprintf(out, n, "cities[%ld].%s", r / CITY_BYTES, city_f[r % CITY_BYTES]);
                break;
            case K_UNIT_TYPES:
                snprintf(out, n, "unit_types[%ld]+%ld", r / UNIT_TYPE_BYTES, r % UNIT_TYPE_BYTES);
                break;
            case K_UNITS:
                snprintf(out, n, "units[%ld][%ld].%s", r / UNIT_BYTES / MAX_UNITS, r / UNIT_BYTES % MAX_UNITS,
                         unit_f[r % UNIT_BYTES]);
                break;
            case K_WORDS:
                if (b->count > 1)
                    snprintf(out, n, "%s[%ld]", b->name, r / 2);
                else
                    snprintf(out, n, "%s", b->name);
                break;
            default:
                snprintf(out, n, "%s+%ld", b->name, r);
                break;
            }
            return;
        }
        at += len;
    }
    snprintf(out, n, "@%ld", pos);
}

int save_decode(const uint8_t *buf, long len, SaveData *s)
{
    size_t i;
    long pos = 0;
    int k;

    if (len != SVE_SIZE)
        return -1;
    memset(s, 0, sizeof *s);
    for (i = 0; i < sizeof layout / sizeof layout[0]; i++) {
        const Block *b = &layout[i];
        uint8_t *dst = (uint8_t *)s + b->offset;
        const uint8_t *p = buf + pos;
        for (k = 0; k < b->count; k++) {
            switch (b->kind) {
            case K_BYTES:      dst[k] = p[k]; break;
            case K_WORDS:      ((int16_t *)dst)[k] = (int16_t)get16(p + 2 * k); break;
            case K_CITIES:     city_get(&((City *)dst)[k], p + CITY_BYTES * k); break;
            case K_UNIT_TYPES: unit_type_get(s, k, p + UNIT_TYPE_BYTES * k); break;
            case K_UNITS:      unit_get(&((Unit *)dst)[k], p + UNIT_BYTES * k); break;
            }
        }
        pos += block_bytes(b);
    }
    return pos == SVE_SIZE ? 0 : -1;
}

void save_encode(const SaveData *s, uint8_t *buf)
{
    size_t i;
    long pos = 0;
    int k;

    for (i = 0; i < sizeof layout / sizeof layout[0]; i++) {
        const Block *b = &layout[i];
        const uint8_t *src = (const uint8_t *)s + b->offset;
        uint8_t *p = buf + pos;
        for (k = 0; k < b->count; k++) {
            switch (b->kind) {
            case K_BYTES:      p[k] = src[k]; break;
            case K_WORDS:      put16(p + 2 * k, (uint16_t)((const int16_t *)src)[k]); break;
            case K_CITIES:     city_put(&((const City *)src)[k], p + CITY_BYTES * k); break;
            case K_UNIT_TYPES: unit_type_put(s, k, p + UNIT_TYPE_BYTES * k); break;
            case K_UNITS:      unit_put(&((const Unit *)src)[k], p + UNIT_BYTES * k); break;
            }
        }
        pos += block_bytes(b);
    }
}

static uint8_t *read_file(const char *path, long *len)
{
    FILE *f = fopen(path, "rb");
    uint8_t *buf;

    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    *len = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = malloc(*len ? *len : 1);
    if (buf && fread(buf, 1, *len, f) != (size_t)*len) {
        free(buf);
        buf = NULL;
    }
    fclose(f);
    return buf;
}

int load_game(const char *sve_path, const char *map_path, Game *g)
{
    long len;
    uint8_t *buf = read_file(sve_path, &len);
    Picture pic;
    int r;

    if (!buf)
        return -1;
    r = save_decode(buf, len, &g->s);
    free(buf);
    if (r)
        return r;
    if (pic_load(map_path, &pic) || !pic.pixels || pic.width != MAPL_PAGE_W || pic.height != MAPL_PAGE_H) {
        pic_free(&pic);
        return -1;
    }
    memcpy(g->map.pixels, pic.pixels, sizeof g->map.pixels);
    pic_free(&pic);
    reset_transient_state();
    return 0;
}

int save_game(const char *sve_path, const char *map_path, const Game *g)
{
    static uint8_t buf[SVE_SIZE];
    Picture pic;
    FILE *f;

    save_encode(&g->s, buf);
    f = fopen(sve_path, "wb");
    if (!f)
        return -1;
    if (fwrite(buf, 1, SVE_SIZE, f) != SVE_SIZE) {
        fclose(f);
        return -1;
    }
    if (fclose(f))
        return -1;
    memset(&pic, 0, sizeof pic);
    pic.width = MAPL_PAGE_W;
    pic.height = MAPL_PAGE_H;
    pic.pixels = (uint8_t *)g->map.pixels;
    return pic_save_image(map_path, &pic);
}
