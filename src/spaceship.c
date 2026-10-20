/* Spaceships (original overlay 18).
 *
 * A civilization's ship is a 15 x 12 grid (unk_far_2786_0740_bytes, 180 bytes per civ) of
 * part numbers, 0xFF for empty.  Parts go into the slots of a fixed template.  Part types:
 * 0-2 structure, 3 fuel, 4 propulsion, 6 habitation, 7 life support, 8 solar panel. */
#include <stdio.h>
#include <string.h>
#include "game.h"
#include "civ.h"
#include "util.h"
#include "spaceship.h"
#include "ui.h"
#include "view.h"

/* far 276e:0000: x, y, part type of every slot, structure first (index 0..32), then the
 * modules (33..66). */
static const signed char slot_template[][3] = {
    {11, 5, 1}, {11, 4, 2}, {11, 6, 1}, {11, 7, 2}, {12, 4, 0}, {13, 4, 0}, {12, 5, 4}, {13, 5, 3},
    {12, 7, 0}, {13, 7, 0}, {12, 6, 4}, {13, 6, 3}, {12, 8, 4}, {13, 8, 3}, {12, 3, 4}, {13, 3, 3},
    {11, 9, 1}, {11, 10, 2}, {12, 10, 0}, {13, 10, 0}, {12, 9, 4}, {13, 9, 3}, {11, 3, 2}, {11, 2, 1},
    {11, 1, 2}, {12, 1, 0}, {13, 1, 0}, {12, 2, 4}, {13, 2, 3}, {12, 11, 4}, {13, 11, 3}, {12, 0, 4},
    {13, 0, 3}, {11, 5, 1}, {11, 4, 2}, {11, 6, 1}, {11, 7, 2}, {9, 6, 6}, {9, 4, 7}, {11, 3, 2},
    {10, 3, 0}, {9, 3, 0}, {8, 1, 8}, {11, 8, 2}, {10, 8, 0}, {9, 8, 0}, {8, 8, 0}, {7, 8, 0},
    {7, 6, 7}, {8, 3, 0}, {7, 3, 0}, {8, 9, 8}, {7, 4, 6}, {6, 3, 0}, {5, 3, 0}, {5, 4, 7},
    {6, 8, 0}, {5, 8, 0}, {4, 9, 8}, {5, 6, 6}, {4, 3, 0}, {3, 3, 0}, {3, 6, 7}, {4, 8, 0},
    {3, 8, 0}, {4, 1, 8}, {3, 4, 6},
};
#define SLOTS ((int)(sizeof slot_template / sizeof slot_template[0]))

int ship_counts[10];             /* g_edc2: connected parts of each type in the last check */
int ship_unavailable[3];         /* g_67fc: per kind, part types the human was told are not needed */
static uint8_t star_x[20], star_y[20], star_on[20];   /* g_67c0 / g_67d4 / g_67e8 */
static int star_frame;           /* g_67fc (word 0) */

/* Part table (DGROUP 0x4b90, 26 bytes per type): mass, picture x, y, width, height, name. */
static int part_word(int t, int k) { return dgroup_word(0x4b90 + t * 26 + 2 * k); }

/* A grid byte; indices off the civ's grid reach neighbouring civs or other far data. */
static int grid_off(int civ, int x, int y)
{
    return civ * 180 + x * 12 + y;
}

static int grid(int civ, int x, int y)
{
    int k = grid_off(civ, x, y);

    if (k >= 0 && k < 1440)
        return game.s.unk_far_2786_0740_bytes[k];
    if (k < 0 && k >= -256)
        return game.s.city_name_y[256 + k];       /* far 2786:0640.. precedes the grids */
    return 0;
}

static void set_grid(int civ, int x, int y, int v)
{
    int k = grid_off(civ, x, y);

    if (k >= 0 && k < 1440)
        game.s.unk_far_2786_0740_bytes[k] = (uint8_t)v;
}

static int slot_part(int civ, int s) { return grid(civ, slot_template[s][0], slot_template[s][1]); }

/* o_18_0c01: is (x, y) structure? */
static int is_structure(int civ, int x, int y)
{
    int v = (int8_t)grid(civ, x, y);

    return v != -1 && v < 3;
}

/* o_18_0000: the spaceship screen, with a field of stars. */
static void stars_begin(int civ)
{
    int i;

    ui_spaceship_screen(civ);
    for (i = 0; i < 20; i++) {
        star_x[i] = (uint8_t)rnd(240);
        star_y[i] = (uint8_t)(rnd(192) + 8);
        star_on[i] = 0;
    }
}

/* o_18_0c39: the stars move (the screen decides which can be seen). */
void spaceship_stars_step(void)
{
    int i;

    for (i = 0; i < 20; i++) {
        switch (i & 3) {
        case 0: star_x[i] += 2; break;
        case 1: star_x[i]++; break;
        default:
            if (star_frame & 1)
                star_x[i]++;
            break;
        }
        if (star_x[i] >= 240) {
            star_x[i] = 1;
            star_y[i] = (uint8_t)(rnd(192) + 8);
            star_on[i] = 0;
        }
        star_on[i] = (uint8_t)ui_spaceship_star(i, star_x[i], star_y[i], (i & 3) == 0 ? 7 : (i & 3) == 1 ? 3 : 8);
    }
    star_frame++;
}

/* o_18_0166: leave the spaceship screen. */
static void stars_end(void)
{
    ui_spaceship_screen_end();
    redraw_map();
}

/* o_18_0249: count the connected parts and work out the ship's figures; shown on screen
 * for the human (and for everybody with option bit 32) for up to `frames` animation frames
 * (-1: until a key).  `offer_launch`: the human may launch.  Returns the chance of success
 * in percent; sets the estimated arrival year unless launched. */
int spaceship_check(int civ, int frames, int offer_launch)
{
    SaveData *s = &game.s;
    int show = civ == s->human_civ || (s->unk_19c0 & 32);
    int i, t, x, y, w, h, connected, dx, dy, mass = 0, success = 100, flight, n;
    SpaceshipFigures f;

    for (t = 0; t < 10; t++)
        ship_counts[t] = 0;
    for (i = 0; i < SLOTS; i++) {
        x = slot_template[i][0];
        y = slot_template[i][1];
        t = (int8_t)grid(civ, x, y);
        if (t == -1)
            continue;
        w = (t >= 6 || t == 3) ? 2 : 1;
        h = t >= 6 ? 2 : 1;
        connected = t < 3;
        for (dx = 0; dx < w; dx++)
            for (dy = 0; dy < h; dy++)
                connected += is_structure(civ, x + dx + 1, y + dy) + is_structure(civ, x + dx - 1, y + dy) +
                             is_structure(civ, x + dx, y + dy + 1) + is_structure(civ, x + dx, y + dy - 1);
        if (connected)
            ship_counts[t]++;
    }
    for (t = 0; t < 10; t++)
        mass += part_word(t, 0) * ship_counts[t];

    f.population = ship_counts[6];
    f.support = ship_counts[7] * 100 / clamp(ship_counts[6], 1, 99);
    success = clamp(f.support, 0, 100) * success / 100;
    f.energy = ship_counts[8] * 200 / clamp(ship_counts[6] + ship_counts[7], 1, 99);
    success = clamp(f.energy, 0, 100) * success / 100;
    f.mass = mass;
    f.fuel = ship_counts[4] * 100 / clamp(ship_counts[3], 1, 99);
    flight = mass * 50 / (clamp(ship_counts[3] * 10, 0, ship_counts[4] * 10) + 1);
    if (flight > 150)
        success -= (flight - 150) / 10;
    f.flight_time = flight;
    success = clamp(success, 0, 100);
    f.success = success;
    if (ship_counts[6] == 0)
        offer_launch = 0;
    if (!(s->unk_4c88 & (1 << civ)))
        s->unk_ed30[civ] = (int16_t)(flight / 10 + s->year);
    if (show) {
        n = ui_spaceship_figures(civ, &f, frames, civ == s->human_civ && frames < 0 && offer_launch);
        for (i = 0; i < n; i++)
            spaceship_stars_step();
        if (civ == s->human_civ && offer_launch && frames < 0 && !(s->unk_4c88 & (1 << s->human_civ))) {
            char msg[160];
            snprintf(msg, sizeof msg,
                     "Confirm spaceship launch.\n(%d%% chance of success)\n No Launch\n Launch CONFIRMED!\n", success);
            if (ui_launch_requested() && ui_ask(msg, 0) == 1)
                spaceship_launch(s->human_civ);
        }
    }
    s->unk_ee0c[civ] = (int16_t)ship_counts[6];
    return success;
}

/* o_18_0d3f: a computer player finishes a part of `kind` (0 structure, 1 component,
 * 2 module).  Returns 0 if placed, 1 or 2 if there is no room for it. */
int spaceship_part_ai(int civ, int kind)
{
    SaveData *s = &game.s;
    int slot = 0, missing = 1, first_free = 999;
    char msg[96];

    s->unk_4c86 = 0;
    if (kind == 2 ||
        (s->unk_ed30[civ] != -1 && s->unk_ed30[civ] - s->year < 15 &&
         (s->unk_ed30[civ] < s->unk_ed30[s->human_civ] || s->unk_ed30[s->human_civ] == -1)))
        slot = 33;
    for (;;) {
        if (slot_part(civ, slot) == NONE8) {
            if (first_free == 999 && slot_template[slot][2] >= kind * 3 + 3)
                first_free = slot;
            if ((int8_t)(slot_template[slot][2] / 3) == kind) {
                missing = 0;
                break;
            }
        }
        slot++;
        if (slot >= SLOTS)
            break;
        if (slot - first_free >= 4 && kind != 2)
            break;
    }
    if (slot >= SLOTS && first_free == 999)
        return 2;
    if (missing)
        return 1;
    snprintf(msg, sizeof msg, "%s build space ship.\n", s->civ_names[civ]);
    ui_spaceship_news(civ, msg);
    stars_begin(civ);
    set_grid(civ, slot_template[slot][0], slot_template[slot][1], slot_template[slot][2]);
    s->unk_4c86 = (int16_t)spaceship_check(civ, -1, 0);
    s->unk_4c88 |= (int16_t)(1 << (civ + 8));
    stars_end();
    return 0;
}

/* o_18_0f55: the human finishes a part of `kind` and says which. */
void spaceship_part_human(int civ, int kind)
{
    SaveData *s = &game.s;
    int sub, slot, i, o, a, b, c, d, choice;
    static const char *const names[9] = {"Structure1", "Structure2", "Propulsion1", "Propulsion", "Fuel", "",
                                         "Habitation", "Life Support", "Solar Panel"};
    char msg[96];

    stars_begin(civ);
    spaceship_check(civ, 15, 0);
    for (;;) {
        sub = 2;
        if (kind == 1) {
            if (ship_unavailable[1] == 3)
                goto check;
            choice = ui_spaceship_choose("Type?\n Propulsion\n Fuel\n", ship_unavailable[1]);
            sub = choice + 3;
            if (sub == 2)
                goto leave;
        }
        if (kind == 2) {
            if (ship_unavailable[2] == 7)
                goto check;
            choice = ui_spaceship_choose("Type?\n Habitation\n Life Support\n Solar Panel\n", ship_unavailable[2]);
            sub = choice + 6;
            if (sub == 5)
                goto leave;
        }
        if (kind == 0) {
            /* Structure goes where it holds the most parts: the components' or the
             * modules' part of the frame. */
            a = 99;
            b = -1;
            for (i = 0; i < 33; i++) {
                if (slot_part(civ, i) == NONE8) {
                    if (slot_template[i][2] < 3 && a == 99)
                        a = i;
                } else if (slot_template[i][2] >= 3)
                    b = i;
            }
            c = 99;
            d = -1;
            for (i = 33; i < SLOTS; i++) {
                if (slot_part(civ, i) == NONE8) {
                    if (slot_template[i][2] < 3 && c == 99)
                        c = i;
                } else if (slot_template[i][2] >= 3)
                    d = i;
            }
            slot = d - c >= b - a ? c : a;
        } else {
            for (slot = 0; slot < SLOTS; slot++)
                if (slot_part(civ, slot) == NONE8 && slot_template[slot][2] == sub)
                    break;
            if (slot >= SLOTS)
                slot = SLOTS;                        /* (the end marker; see below) */
        }
        if (slot == -1) {
            /* (never: the search above ends on the template's end marker instead) */
            ship_unavailable[kind] |= 1 << sub;
            snprintf(msg, sizeof msg, "No further %s required.\n", names[kind * 3 + sub < 9 ? kind * 3 + sub : 0]);
            ui_message(msg, 0);
            continue;
        }
        if (slot < SLOTS)
            set_grid(civ, slot_template[slot][0], slot_template[slot][1], slot_template[slot][2]);
        break;
    }
    if (!(s->unk_4c88 & (1 << (civ + 8)))) {
        for (o = 1; o < MAX_CIVS; o++)
            if (s->unk_4c88 & (1 << (o + 8)))
                s->unk_e204[o] = 0;
        s->unk_4c88 |= (int16_t)(1 << (civ + 8));
    }
check:
    s->unk_4c84 = (int16_t)spaceship_check(civ, -1, 1);
leave:
    stars_end();
}

/* o_18_1595: launch. */
void spaceship_launch(int civ)
{
    SaveData *s = &game.s;
    char msg[96];

    s->unk_4c88 |= (int16_t)(1 << civ);
    snprintf(msg, sizeof msg, "%s LAUNCH space ship!\nEstimate arrival in %d A.D.\n", s->civ_names[civ],
             s->unk_ed30[civ]);
    ui_spaceship_news(civ, msg);
    s->unk_f7c8[civ] = s->year;
    if (civ != s->human_civ)
        stars_begin(civ);
    spaceship_check(civ, -1, 0);
    if (civ != s->human_civ)
        stars_end();
}

/* o_18_14f9: look at the spaceships under construction. */
void spaceship_view(void)
{
    SaveData *s = &game.s;
    int civ;

    if (!(s->unk_4c88 & 0xFE00))
        return;
    civ = ui_choose_spaceship((s->unk_4c88 >> 9) ^ 0xFF) + 1;
    if (civ <= 0)
        return;
    stars_begin(civ);
    spaceship_check(civ, -1, 1);
    stars_end();
}
