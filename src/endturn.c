/* What happens once all civilizations have moved (original seg 0237 and overlays 7, 12, 20):
 * barbarians, disasters, the computer players' wonders, pollution and global warming,
 * the power ranking and the historians. */
#include <stdio.h>
#include <string.h>
#include "game.h"
#include "civ.h"
#include "city.h"
#include "map.h"
#include "unit.h"
#include "util.h"
#include "ai.h"
#include "move.h"
#include "replay.h"
#include "science.h"
#include "endturn.h"
#include "ui.h"
#include "view.h"

#define CITY(i) (&game.s.cities[i])
#define DIP(a, b) game.s.diplomacy[a][b]
#define FIRST(adv) (game.s.first_discoverer[adv] & 7)   /* civ that discovered it first, 0 none */

/* f_0237_0982: barbarians land from the sea, or rise up near a rich city of the human. */
void barbarians_appear(void)
{
    SaveData *s = &game.s;
    int x, y, n, i, t, c, p, d, tries, u;
    char msg[160];

    if ((5 - s->difficulty) * 30 > s->turn)
        return;
    if (((s->turn + 1) & 7) == 0 && FIRST(58) == 0) {
        do {
            x = rnd(80);
            y = rnd(44) + 3;
        } while (map_terrain(x, y) != TERRAIN_OCEAN || map_unit_owner(x, y) != -1 || body_area(x, y) < 16);
        n = clamp(s->turn / 150 + 1, 1, 3);
        create_unit(0, n >= 3 ? 18 : 17, x, y);         /* a Frigate, early on a Sail */
        for (i = 0; i <= n; i++) {
            t = FIRST(56) ? 7 : 3;                       /* Knights, or Legions */
            if (i == n)
                t = UNIT_DIPLOMAT;                       /* their leader */
            u = create_unit(0, t, x, y);
            s->units[0][u].status |= UNIT_SENTRY;
        }
    }

    c = rnd(MAX_CITIES);
    if (CITY(c)->status == NONE8 || (int8_t)CITY(c)->owner != s->human_civ)
        return;
    p = plunder(c);
    if (rnd(100) >= p)
        return;
    tries = 0;
    for (;;) {
        x = CITY(c)->x + rnd(13) - 6;
        y = CITY(c)->y + rnd(13) - 6;
        c = nearest_city(x, y, NULL);
        d = map_distance(x, y, CITY(c)->x, CITY(c)->y);
        if (++tries >= 99)
            return;
        if (d < 6)
            continue;
        /* The original passes an uninitialised local as the civilization; it holds a stack
         * address, which is no civilization: any unit next to the square counts. */
        if (enemy_in_zone(-1, x, y))
            continue;
        if (map_terrain(x, y) == TERRAIN_OCEAN || map_unit_owner(x, y) != -1)
            continue;
        if (map_continent(x, y) == map_continent(CITY(c)->x, CITY(c)->y))
            break;
    }
    if ((int8_t)CITY(c)->owner == s->human_civ && ((1 << s->human_civ) & (int8_t)s->visibility[x][y])) {
        view_center(s->human_civ, CITY(c)->x - 8, CITY(c)->y - 6);
        snprintf(msg, sizeof msg, "Travellers report:\n%snear %s!\n",
                 FIRST(35) ? "Guerrilla uprising\n" : FIRST(34) ? "Native unrest\n" : "Barbarian uprising\n",
                 s->city_names[CITY(c)->name]);
        ui_message(msg, 2);
    }
    n = clamp(rnd((int8_t)((int8_t)CITY(c)->size / 2)), 2, 99);
    for (i = 0; i <= n; i++) {
        if (FIRST(35))
            t = (i & 4) ? 9 : 4;                         /* Cannons and Musketeers */
        else
            t = (i & 4) ? 10 : 6;                        /* Chariots and Cavalry */
        if (i == n)
            t = UNIT_DIPLOMAT;
        u = create_unit(0, t, x, y);
        s->units[0][u].visible |= s->visibility[x][y];
        if ((int8_t)CITY(c)->owner == s->human_civ)
            square_redraw(x, y);
    }
}

/* o_07_1b82: the `n`th global warming: coasts flood into swamp, inland turns to desert. */
void global_warming(int n)
{
    int x, y, t, k, sea;

    newspaper(-1, "Global temperature\nrises! Icecaps melt.\nSevere Drought.\n");
    for (x = 0; x < MAP_W; x++) {
        for (y = 0; y < MAP_H; y++) {
            t = map_terrain(x, y);
            if (t > TERRAIN_FOREST)
                continue;
            sea = 0;
            for (k = 1; k <= 8; k++)
                if (map_terrain(around_dx[k] + x, around_dy[k] + y) == TERRAIN_OCEAN)
                    sea++;
            if (sea >= 7 - n) {
                game.s.visibility[x][y] |= 1;
                map_put_pixel(x, y, t == TERRAIN_FOREST ? 11 : 3);
                map_clear_improvements(IMP_IRRIGATION | IMP_MINE, x, y);
            } else if (((x * 11 + y * 13) & 7) == n) {
                game.s.visibility[x][y] |= 1;
                map_put_pixel(x, y, t > TERRAIN_PLAINS ? 6 : 14);
            } else
                continue;
            if ((int8_t)game.s.visibility[x][y] & (1 << game.s.human_civ))
                square_redraw(x, y);
        }
    }
}

/* f_0237_1763: pollution drives the warming meter; a computer player may complete a wonder. */
void world_events(void)
{
    SaveData *s = &game.s;
    int k, w, c, o, i, cost;
    char msg[160];

    s->unk_c10a = (int16_t)clamp(sign(s->pollution_count * 2 - s->unk_ea56 * 4 - s->unk_c10a) + s->unk_c10a, 0, 99);
    if (s->unk_c10a == 12 && s->pollution_count > 6)
        newspaper(-1, "Scientists alarmed by\nrising temperatures.\nGlobal Warming feared!\n");
    if (s->unk_c10a > 16) {
        s->unk_ea56++;
        global_warming(s->unk_ea56);
        s->unk_c10a = 0;
    }

    for (k = 0; k <= s->difficulty; k++) {
        w = rnd(21) + 1;
        /* (sic: the prerequisite of improvement w, not of the wonder) */
        if (s->difficulty == 0 && !civ_has_advance(s->human_civ, building_types[w].prereq))
            continue;
        if (s->difficulty < 2) {
            /* Not the wonder the human is building: the Manhattan Project instead. */
            for (c = 0; c < MAX_CITIES; c++)
                if ((int8_t)CITY(c)->owner == s->human_civ && CITY(c)->status != NONE8 &&
                    -(w + FIRST_WONDER - 1) == CITY(c)->producing)
                    w = WONDER_MANHATTAN;
        }
        c = rnd(MAX_CITIES);
        if (CITY(c)->status == NONE8)
            continue;
        o = (int8_t)CITY(c)->owner;
        if (DIP(o, 0) & 4)                             /* in revolution */
            continue;
        if (w == WONDER_MANHATTAN && !civ_has_advance(o, 46))
            continue;
        if (k == 0 && civ_has_advance(o, 46) && !civ_has_advance(s->human_civ, 46) &&
            s->wonder_city[WONDER_MANHATTAN] == -1)
            w = WONDER_MANHATTAN;
        if (w == WONDER_MANHATTAN && civ_has_advance(s->human_civ, 46) &&
            s->military_power[o] > s->military_power[s->human_civ])
            continue;
        if (o == -1 || o == s->human_civ || s->wonder_city[w] != -1)
            continue;
        cost = building_types[w + FIRST_WONDER - 1].cost;
        if ((int8_t)CITY(c)->size <= cost / 10)
            continue;
        if (!civ_has_advance(o, building_types[w + FIRST_WONDER - 1].prereq))
            continue;
        s->wonder_city[w] = (int16_t)c;
        CITY(c)->shields = 0;
        replay_log(10, 2, o, w, 0, 0);
        snprintf(msg, sizeof msg, "%s (%s)\nbuilds %s.\n", s->city_names[CITY(c)->name], s->civ_adjectives[o],
                 building_types[w + FIRST_WONDER - 1].name);
        s->treasury[o] -= (int16_t)clamp(s->treasury[o] / 3, 0, cost * 10 - CITY(c)->shields);
        if (fast_view != 1) {
            ui_play_theme(civ_types[s->civ_type[o]].music);
            ui_message(msg, (DIP(s->human_civ, o) & DIP_EMBASSY) ? 5 : 2);
            ui_wonder_view(w + FIRST_WONDER - 1, rnd(2));   /* (o_08_062a's border) */
            redraw_map();
            ui_play_theme(1);
        }
        if (w == WONDER_DARWIN) {
            research_complete(o, 0);
            research_complete(o, 0);
        }
        for (i = 0; i < MAX_CITIES; i++)
            if (CITY(i)->status != NONE8 && (int8_t)CITY(i)->owner == s->human_civ &&
                -(w + FIRST_WONDER - 1) == CITY(i)->producing)
                city_screen(s->human_civ, i);
    }
}

/* f_0237_0d9d: the power ranking (civ_rank), the history graphs, and everybody against a
 * human who leads by too much. */
void civ_ranking(void)
{
    SaveData *s = &game.s;
    int16_t power[MAX_CIVS];
    int c, o, t, k, best, pick = 0, wars, slot = (s->turn / 4) % 150 * 8;

    for (c = 1; c < MAX_CIVS; c++) {
        power[c] = (int16_t)(s->treasury[c] / 32 + (s->citizen_count[c] << 3) + s->advances_known[c]);
        for (t = 0; t < NUM_UNIT_TYPES; t++)
            power[c] = (int16_t)(power[c] + s->unit_types[t].cost * s->units_active[c][t]);
        if (!(s->civs_alive & (1 << c)))
            power[c] = 0;
        if (s->turn < 600)
            s->unk_far_2786_0000[slot + c] = (uint8_t)clamp(power[c] / 8, 0, 255);
        wars = 0;
        for (o = 0; o < MAX_CIVS; o++)
            if ((DIP(c, o) & 3) == 1)
                wars |= 1 << o;
        if (s->turn < 600)
            s->unk_far_2786_0fe8[slot + c] = (uint8_t)wars;
    }
    for (k = 1; k < MAX_CIVS; k++) {
        best = -1;
        for (c = 1; c < MAX_CIVS; c++)
            if (best < power[c]) {
                pick = c;
                best = power[c];
            }
        s->civ_rank[pick] = (int16_t)(8 - k);
        power[pick] = -1;
    }
    ai_flag_8f98 = 0;
    if (s->civ_rank[s->human_civ] == 7 && s->city_count[s->human_civ] > 4 &&
        s->units_active[s->human_civ][UNIT_NUCLEAR] == 0 && s->turn > 200) {
        ai_flag_8f98 = 1;
        for (o = 1; o < MAX_CIVS; o++) {
            if (o == s->human_civ || s->city_count[o] <= 1)
                continue;
            if (!(DIP(o, s->human_civ) & DIP_PEACE))
                DIP(o, s->human_civ) |= DIP_CONTACT;
            else
                DIP(o, s->human_civ) |= 0x100;       /* the treaty will be broken */
        }
    }
}

/* f_0237_16e3: score needed for palace improvement `level`. */
int palace_threshold(int level)
{
    int sum = 0, k;

    for (k = 0; k <= level; k++)
        sum += k * (7 - game.s.difficulty);
    return sum / 2 + 1;
}

/* Remove a random improvement (not the Palace) for an earthquake or a fire.  The mask is
 * built in a 16-bit register and sign-extended (sic): improvements 17 and up are never
 * picked, and picking the 16th clears all of the upper ones too.  Returns its number. */
static int lose_improvement(City *c)
{
    int b, guard = 0;
    long mask;

    for (;;) {
        b = rnd(24);
        if (b == 0)
            continue;
        mask = (int16_t)(b < 16 ? 1 << b : 0);
        if ((long)c->buildings & mask)
            break;
        if (++guard > 100000)                         /* the original would loop for ever */
            return -1;
    }
    c->buildings &= (uint32_t)(long)(int16_t)~mask;
    return b + 1;
}

/* o_20_053d: a disaster may strike `city` (plague, flood, fire, ...), or its people riot
 * and perhaps go over to a happier neighbour. */
void city_disaster(int city)
{
    SaveData *s = &game.s;
    City *c = CITY(city);
    int owner = (int8_t)c->owner, near = 0, k, b, best, pick, d, v;
    char msg[256] = "", name[16];

    for (k = 0; k <= 8; k++)
        near |= 1 << map_terrain(c->x + around_dx[k], c->y + around_dy[k]);
    snprintf(name, sizeof name, "%s", s->city_names[c->name]);

    switch (rnd(10)) {
    case 0:                                           /* earthquake (hills) */
        if (!(near & (1 << TERRAIN_HILLS)) || !(c->buildings & ~1UL))
            break;
        b = lose_improvement(c);
        if (b > 0)
            snprintf(msg, sizeof msg, "Earthquake in %s!\n%s destroyed.\n", name, building_types[b].name);
        break;
    case 1:                                           /* plague */
        if (civ_has_advance(owner, 10) || (c->buildings & BIT(B_AQUEDUCT)) ||
            !civ_has_advance(owner, building_types[B_AQUEDUCT].prereq))
            break;
        ui_text_section("*D1", name, "", msg, sizeof msg);
        if (c->size != 0)
            c->size -= (int8_t)c->size / 4;
        break;
    case 2:                                           /* flood (river) */
        if (!(near & (1 << TERRAIN_RIVER)) || (c->buildings & BIT(B_CITY_WALLS)) ||
            !civ_has_advance(owner, building_types[B_CITY_WALLS].prereq))
            break;
        ui_text_section("*D2", name, "", msg, sizeof msg);
        if ((int8_t)c->size > 1)
            c->size -= (int8_t)c->size / 4;
        break;
    case 3:                                           /* volcano (mountains) */
        if (!(near & (1 << TERRAIN_MOUNTAINS)) || (c->buildings & BIT(B_TEMPLE)) ||
            !civ_has_advance(owner, building_types[B_TEMPLE].prereq))
            break;
        ui_text_section("*D3", name, "", msg, sizeof msg);
        if ((int8_t)c->size > 1)
            c->size -= (int8_t)c->size / 3;
        break;
    case 4:                                           /* famine */
        if (c->size == 0 || (c->buildings & BIT(B_GRANARY)) ||
            !civ_has_advance(owner, building_types[B_GRANARY].prereq))
            break;
        ui_text_section("*D4", name, "", msg, sizeof msg);
        c->food = 0;
        c->size -= (int8_t)c->size / 3;
        strncat(msg, "Citizens demand GRANARY.\n", sizeof msg - strlen(msg) - 1);
        break;
    case 5:                                           /* fire */
        if (!(c->buildings & ~1UL) || (c->buildings & BIT(B_AQUEDUCT)) ||
            !civ_has_advance(owner, building_types[B_AQUEDUCT].prereq))
            break;
        b = lose_improvement(c);
        if (b > 0)
            ui_text_section("*D5", name, building_types[b].name, msg, sizeof msg);
        break;
    case 6:                                           /* pirates (ocean) */
        if (!(near & (1 << TERRAIN_OCEAN)) || (c->buildings & BIT(B_BARRACKS)))
            break;
        ui_text_section("*D6", name, "", msg, sizeof msg);
        c->food = 0;
        c->shields = 0;
        break;
    default:                                          /* unrest */
        city_process(city, -1);
        if (calc.happy >= calc.unhappy)
            break;
        {
            static const char *const what[3] = {"Rioting in ", "Corruption in ", "Scandal in "};
            const char *demand;
            k = rnd(3);
            if (!(c->buildings & BIT(B_TEMPLE)))
                demand = "TEMPLE\n";
            else if (!(c->buildings & BIT(B_COURTHOUSE)))
                demand = "COURTHOUSE\n";
            else if (!(c->buildings & BIT(B_MARKETPLACE)))
                demand = "MARKETPLACE\n";
            else if (civ_has_advance(owner, ADV_RELIGION) && !(c->buildings & BIT(B_CATHEDRAL)))
                demand = "CATHEDRAL\n";
            else
                demand = "lower taxes.\n";
            snprintf(msg, sizeof msg, "%s%s!\nCitizens demand %s", what[k], name, demand);
        }
        if (s->human_mask & (1 << c->owner)) {
            ui_message(msg, 0);
            redraw_if_seen(c->x, c->y);
            msg[0] = 0;
        }
        c->food = 0;
        c->shields = 0;
        if (calc.happy > calc.unhappy || (c->buildings & BIT(B_PALACE)) || s->city_count[owner] <= 3)
            break;
        /* Is there a happier city around?  (The search stops at this city or the first
         * empty slot, sic.) */
        best = 4;
        pick = -1;
        for (k = 0; k < MAX_CITIES && k != city && CITY(k)->status != NONE8; k++) {
            d = map_distance(c->x, c->y, CITY(k)->x, CITY(k)->y);
            city_process(k, -1);
            v = ((calc.happy - calc.unhappy) << 5) / d;
            if (v > best) {
                best = v;
                pick = k;
            }
        }
        if (pick == -1 || CITY(pick)->owner == c->owner)
            break;
        if ((1 << s->human_civ) & (int8_t)s->visibility[c->x][c->y]) {
            char m2[160];
            snprintf(m2, sizeof m2, "Residents of %s admire\nthe prosperity of %s.\n", name,
                     s->city_names[CITY(pick)->name]);
            ui_message(m2, 0);
        }
        city_revolt(city, (int8_t)CITY(pick)->owner);
        msg[0] = 0;
        break;
    }
    if (msg[0] == 0)
        return;
    if (s->human_mask & (1 << c->owner)) {
        ui_message(msg, 4);
        redraw_if_seen(c->x, c->y);
    } else if ((1 << s->human_civ) & (int8_t)s->visibility[c->x][c->y]) {
        char *nl = strchr(msg, '\n');                 /* only the first line */
        if (nl)
            nl[1] = 0;
        ui_message(msg, 2);
        view_center(s->human_civ, c->x - 10, c->y - 6);
    }
}

/* o_12_09d6: a historian ranks the civilizations by one measure; places score points. */
void historian_report(void)
{
    static const char *const historian[4] = {"Herodotus", "Pliny", "Toynbee", "Gibbon"};
    static const char *const measure[5] = {"WEALTHIEST", "MOST POWERFUL", "MOST ADVANCED", "HAPPIEST", "LARGEST"};
    static const char *const grade[7] = {"Glorious", "Great", "Fine", "Mediocre", "Puny", "Pathetic", "Hopeless"};
    SaveData *s = &game.s;
    int16_t v[MAX_CIVS] = {0};
    int kind, who, k, c, o, place = 0, best, pick;
    char title[96], line[96];

    kind = rnd(5);
    who = rnd(4);
    snprintf(title, sizeof title, "%s completes his epic history:\n'The %s Civilizations in the World'",
             historian[who], measure[kind]);
    ui_historian_begin(title);
    for (c = 0; c < MAX_CITIES; c++) {
        const City *cy = CITY(c);
        if (cy->owner == NONE8 || cy->status == NONE8)
            continue;
        city_process(c, -1);
        if (kind == 3)
            v[(int8_t)cy->owner] += (int16_t)((int8_t)cy->size + calc.happy - calc.unhappy);
        if (kind == 4)
            v[(int8_t)cy->owner] += (int8_t)cy->size;
    }
    for (o = 0; o < MAX_CIVS; o++) {
        if (kind == 0)
            v[o] = s->treasury[o];
        else if (kind == 1)
            v[o] = s->military_power[o];
        else if (kind == 2) {
            v[o] = 0;
            for (k = 0; k < NUM_ADVANCES; k++)
                if (civ_has_advance(o, k))
                    v[o]++;
        }
    }
    for (k = 1; k < MAX_CIVS; k++) {
        if (!(s->civs_alive & (1 << k)))
            continue;
        best = 0;
        pick = -1;
        for (o = 1; o < MAX_CIVS; o++)
            if (v[o] >= best && (s->civs_alive & (1 << o))) {
                best = v[o];
                pick = o;
            }
        if (pick == -1)
            continue;
        if ((DIP(s->human_civ, pick) & DIP_EMBASSY) || pick == s->human_civ || s->difficulty == 0) {
            snprintf(line, sizeof line, "%d. The %s Civilization of the %s.", place + 1, grade[place],
                     s->civ_names[pick]);
            ui_historian_line(pick, line);
        }
        s->unk_6c1c[pick] += (int16_t)(7 - place);
        place++;
        v[pick] = -1;
    }
    s->unk_6c1c[0]++;
    ui_historian_end();
}
