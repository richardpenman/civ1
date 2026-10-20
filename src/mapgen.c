/* Making a new world (original overlay 7). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "game.h"
#include "map.h"
#include "move.h"
#include "util.h"
#include "civ.h"
#include "mapgen.h"
#include "ui.h"
#include "pic.h"

/* The original builds the world on screen page 1 and copies it to the map page (2) at the
 * end; the port builds it on the map page directly.  The one routine that reads page 2 in
 * between (build_land_links, o_07_1142) therefore sees the world being made; in the
 * original page 2 holds whatever the intro animation drew there (see NOTES.md). */
#define P(x, y)        map_pixel(x, y)
#define PUT(x, y, c)   map_put_pixel(x, y, c)
#define TERR(x, y)     terrain_by_colour[map_pixel(x, y) & 15]   /* o_07_170c */
#define CONT(x, y)     map_pixel(x, y + 50)                      /* o_07_172d */

WorldParams world = {1, 1, 1, 1};
int land_mass;                   /* g_6780: squares raised so far (never reset, sic) */
uint8_t gen_counts[12];          /* (o_07_0a33 counts terrain, unused) */

static void fill(int x, int y, int w, int h, int c)
{
    int i, j;

    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            PUT(x + i, y + j, c);
}

static void blit(int sx, int sy, int w, int h, int dx, int dy)
{
    static uint8_t tmp[200][320];
    int i, j;

    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            tmp[j][i] = (uint8_t)P(sx + i, sy + j);
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            PUT(dx + i, dy + j, tmp[j][i]);
}

/* f_1d1e_0a13: a rectangle outline from (x, y) to (x + w, y + h). */
static void outline(int x, int y, int w, int h, int c)
{
    int i;

    for (i = 0; i <= w; i++) {
        PUT(x + i, y, c);
        PUT(x + i, y + h, c);
    }
    for (i = 0; i <= h; i++) {
        PUT(x + w, y + i, c);
        PUT(x, y + i, c);
    }
}

/* o_07_0988: a random walk drawn at (x + 160, y). */
static void blob_walk(int x, int y)
{
    int n = rnd(64) + 1, d;

    do {
        PUT(x + 160, y, 15);
        PUT(x + 161, y, 15);
        PUT(x + 160, y + 1, 15);
        d = rnd(4) * 2 + 1;
        x += around_dx[d];
        y += around_dy[d];
    } while (--n != 0 && x > 2 && x < 77 && y > 2 && y < 46);
}

/* o_07_08be: raise one blob of land by a step. */
static void raise_land(void)
{
    int x, y;

    fill(160, 0, 80, 50, 0);
    x = rnd(72) + 4;
    y = rnd(34) + 8;
    blob_walk(x, y);
    for (x = 0; x < 80; x++)
        for (y = 0; y < 50; y++)
            if (P(x + 160, y) != 0) {
                PUT(x, y, P(x, y) + 1);
                land_mass++;
            }
}

/* o_07_05d4 */
static void erode(int x, int y)
{
    if (P(x - 1, y - 1) == 1 || P(x - 1, y + 1) == 1 || P(x + 1, y - 1) == 1 || P(x + 1, y + 1) == 1)
        return;
    PUT(x, y, 1);
}

/* o_07_065c: rivers from hills down to the sea. */
static void make_rivers(void)
{
    int rivers = 0, tries = 0, len, x, y, sx, sy, dir, old, coast, k, v;

    do {
        blit(0, 0, 160, 100, 160, 0);                /* to undo a failed river */
        len = 0;
        do {
            x = rnd(80);
            y = rnd(50);
        } while (P(x, y) != 12);
        sx = x;
        sy = y;
        dir = rnd(4) * 2;
        for (;;) {
            PUT(x, y, 9);
            coast = 0;
            for (k = 1; k <= 8; k += 2)
                if (P(around_dx[k] + x, around_dy[k] + y) == 1)
                    coast = 1;
            old = dir;
            dir = ((rnd(2) - (len & 1)) * 2 + dir) & 7;
            if ((old ^ 4) > dir)
                PUT(x, y + 50, 8);
            x += around_dx[dir + 1];
            y += around_dy[dir + 1];
            v = P(x, y);
            len++;
            if (coast || v == 13 || v == 1 || v == 9)
                break;
        }
        if ((coast || v == 9) && len >= 5) {
            rivers++;
            for (k = 1; k <= 21; k++)
                if (P(around_dx[k] + sx, around_dy[k] + sy) == 2)
                    PUT(around_dx[k] + sx, around_dy[k] + sy, 11);
        } else
            blit(160, 0, 160, 100, 0, 0);
        ui_world_progress();
    } while (++tries < 256 && (world.land + world.climate) * 2 + 6 > rivers);
    blit(0, 50, 80, 50, 0, 150);
    fill(0, 50, 80, 50, 0);
}

/* o_07_0a33 part: number the land (pass 0) or sea (pass 1) bodies by size, 1..14 for the
 * biggest and 15 for the rest; visibility is borrowed as scratch. */
static void number_bodies(int pass)
{
    int16_t *area = pass ? game.s.sea_area : game.s.land_area;
    int x, y, k, i, cur, run, id = 0, above, rank_of[64], order[16], r;

    memset(game.s.visibility, 0, sizeof game.s.visibility);
    for (y = 0; y < 50; y++) {
        cur = -1;
        run = 0;
        for (x = 0; x < 80; x++) {
            int t = TERR(x, y), land = t != TERRAIN_OCEAN;
            if (pass == 0)
                gen_counts[t]++;
            if (land != (pass == 0)) {
                run = 0;
                cur = -1;
                continue;
            }
            if (y > 0) {
                for (k = x == 0 ? 0 : -1; k <= (x < 79 ? 1 : 0); k++) {
                    above = (int8_t)game.s.visibility[x + k][y - 1];
                    if (above == 0)
                        continue;
                    if (cur != -1 && cur != above) {
                        int ax, ay;
                        for (ay = 0; ay <= y; ay++)
                            for (ax = 0; ax < 80; ax++) {
                                if ((int8_t)game.s.visibility[ax][ay] == cur)
                                    game.s.visibility[ax][ay] = (uint8_t)above;
                                area[above] += area[cur];
                                area[cur] = 0;
                            }
                    }
                    cur = above;
                    ui_world_progress();
                }
            }
            if (cur == -1) {
                if (run == 0) {
                    id = 0;
                    do
                        id++;
                    while (area[id] != 0);
                }
                cur = id;
            }
            game.s.visibility[x][y] = (uint8_t)cur;
            area[cur]++;
            run = 1;
        }
        ui_world_progress();
    }

    /* Rank the 14 largest. */
    memset(order, 0, sizeof order);
    rank_of[0] = 0;
    for (i = 1; i < 64; i++) {
        rank_of[i] = 15;
        for (r = 1; r < 15; r++) {
            if (area[order[r]] >= area[i])
                continue;
            for (k = 15; k > r; k--) {
                rank_of[order[k - 1]] = k;
                order[k] = order[k - 1];
            }
            rank_of[i] = r;
            order[r] = i;
            break;
        }
    }
    rank_of[0] = 0;                                  /* (the shifts above wrote it) */
    for (x = 0; x < 80; x++)
        for (y = 0; y < 50; y++) {
            r = rank_of[(int8_t)game.s.visibility[x][y]];
            if (r != 0)
                PUT(x, y + 50, r);
        }
    for (i = 1; i < 15; i++)
        order[i] = area[order[i]];
    for (i = 1; i < 15; i++)
        area[i] = order[i];
    area[0] = 0;
    area[15] = 1;
}

/* o_07_13df: which neighbouring blocks a ship can sail to (sea_links, not saved). */
void build_sea_links(void)
{
    static const int ox[4] = {0, 1, 0, 1}, oy[4] = {0, 0, 1, 1};
    int bx, by, k, j, x, y, c, c2 = -1, step, n, ok, nx, ny, q;

    memset(sea_links, 0, 260);
    for (bx = 0; bx < 20; bx++) {
        for (by = 0; by < 12; by++) {
            x = bx * 4 + 1;
            y = by * 4 + 1;
            c = -1;
            for (j = 0; j < 4; j++)
                if (TERR(x + ox[j], y + oy[j]) == TERRAIN_OCEAN) {
                    c = CONT(x + ox[j], y + oy[j]);
                    break;
                }
            if (c == -1)
                continue;
            for (k = 1; k <= 8; k++) {
                nx = around_dx[k] * 4 + x;
                ny = around_dy[k] * 4 + y;
                /* (no ocean there: the previous neighbour's body is compared, sic) */
                for (j = 0; j < 4; j++)
                    if (TERR(nx + ox[j], ny + oy[j]) == TERRAIN_OCEAN) {
                        c2 = CONT(nx + ox[j], ny + oy[j]);
                        break;
                    }
                if (c != c2)
                    continue;
                ok = 1;
                nx = x;
                ny = y;
                for (step = 0; step <= 4; step++) {
                    nx += around_dx[k];
                    ny += around_dy[k];
                    n = 4;
                    for (j = 0; j < 4; j++)
                        if (!(TERR(nx + ox[j], ny + oy[j]) == TERRAIN_OCEAN && map_in_bounds(nx + ox[j], ny + oy[j])))
                            n--;
                    if (n < 2) {
                        ok = 0;
                        break;
                    }
                }
                if (!ok && !(by == 11 && k == 3))
                    continue;
                if (around_dy[k] + by >= 12)
                    continue;
                sea_links[bx * 13 + by] |= (uint8_t)(1 << (k - 1));
                nx = around_dx[k] + bx;
                ny = around_dy[k] + by;
                if (nx >= 0 && nx < 20 && ny >= 0 && ny <= 12) {
                    q = nx * 13 + ny;
                    sea_links[q] |= (uint8_t)(1 << ((k + 3) & 7));
                }
            }
        }
    }
    for (by = 0; by < 12; by++) {
        sea_links[by] |= 0xE0;                       /* the world wraps east-west */
        sea_links[19 * 13 + by] |= 0x0E;
    }
    sea_links[0] &= 0x7F;
    sea_links[11] &= 0xDF;
    sea_links[19 * 13] &= 0xFD;
    sea_links[19 * 13 + 11] &= 0xF7;
}

/* o_07_0ec4: the value of each square as a city site (layer x + 80, both halves), and
 * good sites per continent (unk_c10e). */
static void rate_sites(void)
{
    int val[24], t, tt, x, y, k, sx, sy, v, sum, r;

    for (k = 0; k < 16; k++)
        game.s.unk_c10e[k] = 0;
    for (t = 0; t < 24; t++) {
        const TerrainType *tp = &terrain_types[t];
        val[t] = (signed char)tp->food * 3 + (signed char)tp->trade;
        tt = t % 12;
        if (tt != TERRAIN_GRASSLAND && tt != TERRAIN_RIVER)
            val[t] += (signed char)tp->shields * 2;
        if (terrain_improve[tt].mine < 0)
            val[t] += -1 - terrain_improve[tt].mine;
        else if (terrain_improve[tt].irrigate < 0)
            val[t] += (-1 - terrain_improve[tt].irrigate) * 2;
    }
    for (x = 2; x < 78; x++) {
        for (y = 2; y < 48; y++) {
            t = TERR(x, y);
            if (t != TERRAIN_RIVER && t != TERRAIN_GRASSLAND && t != TERRAIN_PLAINS) {
                PUT(x + 80, y, 0);
                PUT(x + 80, y + 50, 0);
                continue;
            }
            sum = 0;
            for (k = 0; k <= 20; k++) {
                sx = city_square_dx[k] + x;
                sy = city_square_dy[k] + y;
                v = 0;
                t = TERR(sx, sy);
                if ((t == TERRAIN_GRASSLAND || t == TERRAIN_RIVER) && !((sx * 7 + sy * 11) & 2))
                    v += 2;
                if (has_special_resource(sx, sy))
                    t += 12;
                v += val[t];
                if (k < 9)
                    v += v;
                if (k == 0)
                    v += v;
                sum += v;
            }
            if (TERR(x, y) != TERRAIN_PLAINS && ((x * 7 + y * 11) & 2))
                sum -= 16;
            r = clamp((sum - 120) / 8, 1, 15);
            v = r / 2 + 8;
            PUT(x + 80, y, v);
            PUT(x + 80, y + 50, v);
            game.s.unk_c10e[CONT(x, y)]++;
        }
    }
}

/* o_07_0a33: bodies of land and water, block links, site values, polar ice. */
static void finish_world(void)
{
    int k;

    memset(gen_counts, 0, sizeof gen_counts);
    number_bodies(0);
    number_bodies(1);
    ui_world_progress();
    build_land_links();
    build_sea_links();
    ui_world_progress();
    rate_sites();
    for (k = 0; k < 80; k++) {
        PUT(k, 0, 15);
        PUT(k, 49, 15);
    }
    for (k = 0; k < 20; k++) {
        PUT(rnd(80), 0, 7);
        PUT(rnd(80), 1, 7);
        PUT(rnd(80), 48, 7);
        PUT(rnd(80), 49, 7);
    }
}

/* The Earth: map.pic (f_1fb4_0224 into page 1). */
static void load_earth_map(void)
{
    char path[512];
    Picture pic;

    snprintf(path, sizeof path, "%s/MAP.PIC", data_dir);
    if (pic_load(path, &pic) || !pic.pixels || pic.width != MAPL_PAGE_W || pic.height != MAPL_PAGE_H) {
        fprintf(stderr, "cannot load %s\n", path);
        exit(1);
    }
    memcpy(game.map.pixels, pic.pixels, sizeof game.map.pixels);
    pic_free(&pic);
}

/* o_07_0012: a new world from the parameters in `world` (0..2 each), or the Earth. */
void generate_world(void)
{
    int x, y, b, v, t, i, d;

    if (earth_map) {
        load_earth_map();
    } else {
        ui_world_progress();
        fill(0, 0, 320, 200, 0);
        do {
            raise_land();
            ui_world_progress();
        } while (world.land * 8 * 40 + 640 > land_mass);
        outline(0, 0, 79, 49, 0);
        outline(1, 1, 77, 47, 0);
        /* No land touching only at a corner. */
        for (x = 1; x < 79; x++) {
            for (y = 1; y < 49; y++) {
                b = (P(x, y) != 0) | (P(x + 1, y) != 0) << 1 | (P(x, y + 1) != 0) << 2 | (P(x + 1, y + 1) != 0) << 3;
                if (b == 6 || b == 9) {
                    PUT(x + 1, y, 1);
                    PUT(x, y + 1, 1);
                    PUT(x + 1, y + 1, 1);
                    if (x != 0)
                        x--;
                    if (y != 0)
                        y--;
                }
            }
            ui_world_progress();
        }
        /* Heights become terrain, by latitude and temperature. */
        for (x = 0; x < 80; x++) {
            for (y = 0; y < 50; y++) {
                v = P(x, y);
                if (v == 0)
                    PUT(x, y, 1);
                else if (v == 1) {
                    t = abs(rnd(8) + y - 29) + 1 - world.temperature;
                    switch (t / 6 + 1) {
                    case 0: case 1: PUT(x, y, 14); break;
                    case 2: case 3: PUT(x, y, 6); break;
                    case 4: case 5: PUT(x, y, 7); break;
                    case 6: case 7: PUT(x, y, 15); break;
                    default: break;
                    }
                } else if (v == 2)
                    PUT(x, y, 12);
                else
                    PUT(x, y, 13);
            }
            ui_world_progress();
        }
        /* Rain: wet winds from the west, then from the east. */
        for (y = 0; y < 50; y++) {
            int lat = abs(25 - y), wet = 0;
            for (x = 0; x < 80; x++) {
                v = P(x, y);
                if (v == 1) {
                    if (abs(12 - lat) + world.climate * 4 > wet)
                        wet++;
                    continue;
                }
                if (wet <= 0)
                    continue;
                wet -= rnd(7 - world.climate * 2);
                switch (v) {
                case 6: PUT(x, y, 10); break;
                case 7: PUT(x, y, 15); break;
                case 12: PUT(x, y, 2); break;
                case 13: wet -= 3; break;
                case 14: PUT(x, y, 6); break;
                default: break;
                }
            }
            wet = 0;
            for (x = 79; x >= 0; x--) {
                v = P(x, y);
                if (v == 1) {
                    if (lat / 2 + world.climate > wet)
                        wet++;
                    continue;
                }
                if (wet <= 0)
                    continue;
                wet -= rnd(7 - world.climate * 2);
                switch (v) {
                case 3: case 12: PUT(x, y, 2); break;
                case 6: PUT(x, y, 10); break;
                case 10: PUT(x, y, lat < 10 ? 11 : 3); wet = -2; break;
                case 13: wet -= 3; PUT(x, y, 2); break;
                case 14: PUT(x, y, 6); break;
                default: break;
                }
            }
            ui_world_progress();
        }
        /* Age: erosion. */
        for (i = 0; i < world.age * 800 + 800; i++) {
            if (!(i & 1)) {
                x = rnd(80);
                y = rnd(50);
            } else {
                d = rnd(8) + 1;
                x += around_dx[d];
                y += around_dy[d];
            }
            switch (P(x, y)) {
            case 2: PUT(x, y, 11); break;
            case 3: PUT(x, y, 10); break;
            case 6: case 7: PUT(x, y, 12); break;
            case 10: PUT(x, y, 2); break;
            case 11: PUT(x, y, 3); break;
            case 12: case 15: PUT(x, y, 13); break;
            case 13: erode(x, y); break;
            case 14: PUT(x, y, 6); break;
            default: break;
            }
        }
        make_rivers();
    }
    fill(160, 0, 160, 200, 0);
    finish_world();
    ui_world_progress_end();
}

/* The first land square of the 2x2 at (x, y): its continent, or -1. */
static int land_in_2x2(int x, int y, int *lx, int *ly)
{
    static const int ox[4] = {0, 1, 0, 1}, oy[4] = {0, 0, 1, 1};
    int k;

    for (k = 0; k < 4; k++) {
        if (map_terrain(x + ox[k], y + oy[k]) != TERRAIN_OCEAN) {
            *lx = x + ox[k];
            *ly = y + oy[k];
            return map_continent(*lx, *ly);
        }
    }
    return -1;
}

/* o_07_1142: which neighbouring 4x4 blocks a land unit can reach (land_links). */
void build_land_links(void)
{
    uint8_t *links = game.s.land_links;
    int bx, by, k, x, y, c, c2, px = 0, py = 0, qx = 0, qy = 0, d, n;

    memset(links, 0, 260);
    for (bx = 0; bx < 20; bx++) {
        for (by = 0; by < 13; by++) {
            x = bx * 4 + 1;
            y = by * 4 + 1;
            c = land_in_2x2(x, y, &px, &py);
            if (c == -1)
                continue;
            for (k = 1; k <= 4; k++) {
                c2 = land_in_2x2(around_dx[k] * 4 + x, around_dy[k] * 4 + y, &qx, &qy);
                if (c != c2)
                    continue;
                d = reachable(px, py, qx, qy, 0, 20);
                if (d == -1 || d >= 20)
                    continue;
                links[bx * 13 + by] |= (uint8_t)(1 << (k - 1));
                n = (around_dx[k] + bx) * 13 + by + around_dy[k];
                if (n >= 0 && n < 260)                 /* (the original writes past the end) */
                    links[n] |= (uint8_t)(1 << ((k + 3) & 7));
            }
        }
    }
}
