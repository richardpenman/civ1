/* Founding, capturing and splitting cities; diplomats (overlays 15, 20, 22; seg 1439). */
#include <stdio.h>
#include <string.h>
#include "game.h"
#include "civ.h"
#include "city.h"
#include "map.h"
#include "unit.h"
#include "util.h"
#include "ai.h"
#include "replay.h"
#include "science.h"
#include "diplomacy.h"
#include "combat.h"
#include "move.h"
#include "ui.h"
#include "meeting.h"
#include "turn.h"
#include "view.h"

#define DIP(a, b) game.s.diplomacy[a][b]
#define CITY(i) (&game.s.cities[i])

/* o_20_0000: a new city of `civ` at (x, y).  Returns the city or -1. */
int found_city(int civ, int unused, int x, int y, int size)
{
    SaveData *s = &game.s;
    City *c;
    int id, name, start, done, i, first, t, sx, sy, v;
    char *n;

    (void)unused;
    ai_forget_spot(civ, 0, x, y, 4);
    for (id = 0; id < MAX_CITIES; id++)
        if (s->cities[id].status == NONE8)
            break;
    if (id == MAX_CITIES) {
        if (civ == s->human_civ)
            ui_help_message("*PMAX");
        return -1;
    }
    c = CITY(id);

    /* The civilization's next unused name (16 each, then a shared spare block). */
    name = s->civ_type[civ] - 1;
    if (name >= 7)
        name--;
    name <<= 4;
    start = name;
    do {
        done = 1;
        if (s->city_name_x[name] != NONE8) {
            done = 0;
            name++;
            if (name == start + 16)
                name = 224;
            if (name == 256) {
                name = 0;
                if (start == 999)
                    done = 1;
                start = 999;
            }
        }
    } while (!done);
    c->name = (uint8_t)name;
    s->city_name_x[name] = (uint8_t)x;
    s->city_name_y[name] = (uint8_t)y;
    if (s->human_mask & (1 << civ)) {
        if (!ui_name_city(id))
            return -1;
        s->city_names[name][12] = 0;
    }
    n = s->city_names[name];
    for (i = 11; i > 0 && n[i] == ' '; i--)
        n[i] = 0;

    replay_log(1, 4, civ, name, x, y);
    map_clear_improvements(IMP_MINE | IMP_FORTRESS, x, y);
    map_set_improvements(IMP_CITY | IMP_ROAD, x, y);
    if (terrain_improve[map_terrain(x, y)].irrigate < -1)
        map_set_improvements(IMP_IRRIGATION, x, y);

    c->status = 0;
    c->x = (uint8_t)x;
    c->y = (uint8_t)y;
    c->size = (uint8_t)size;
    c->visible_size = 0;
    c->worked = 0;
    c->trade = 0;
    c->food = 0;
    c->shields = 0;
    c->buildings = 0;
    c->producing = 1;                                 /* Militia */
    if (civ_has_advance(civ, ADV_GUNPOWDER))
        c->producing = 4;
    if (civ_has_advance(civ, 64))                     /* Conscription */
        c->producing = 5;
    c->fortified[0] = c->fortified[1] = NONE8;
    first = 1;
    for (i = 0; i < MAX_CITIES; i++)                  /* (status is not checked) */
        if ((int8_t)s->cities[i].owner == civ && i != id)
            first = 0;
    if (first)
        c->buildings = BIT(B_PALACE);
    c->trade = 0;
    c->owner = (uint8_t)civ;
    memset(c->trade_routes, NONE8, 3);
    c->specialists = 0;
    for (i = 1; i <= 8; i++) {
        t = map_terrain(x + around_dx[i], y + around_dy[i]);
        if (t == TERRAIN_OCEAN && (body_area(x + around_dx[i], y + around_dy[i]) > 256 || civ == s->human_civ))
            c->status |= CITY_COASTAL;
        if (t == TERRAIN_RIVER || t == TERRAIN_MOUNTAINS)
            c->status |= CITY_HYDRO_OK;
    }
    if (first && civ == s->human_civ)
        ui_help_message("*FIRSTPRODUCT");
    for (i = 0; i < 45; i++) {
        sx = wrap_x(x + around_dx[i]);
        sy = y + around_dy[i];
        if (i <= 20) {
            map_put_pixel(sx + 80, sy, civ);          /* the land is ours */
        } else {
            v = map_pixel(sx + 80, sy);
            if (v >= 8)
                map_put_pixel(sx + 80, sy, 8);
        }
        if (civ != s->human_civ && i <= 24 && (map_improvements(sx, sy) & IMP_CITY) &&
            map_unit_layer_owner(sx, sy) == s->human_civ) {
            c->visible_size = 1;
            map_refresh_known(x, y);
        }
    }
    if (civ == s->human_civ) {
        ui_city_founded(id);
        city_screen(civ, id);
        redraw_map();
    } else {
        c->producing = (int8_t)choose_production(civ, id);
        if (civ_has_wonder(s->human_civ, WONDER_APOLLO)) {
            s->visibility[c->x][c->y] |= 1 << s->human_civ;
            c->visible_size = 1;
            map_refresh_known(c->x, c->y);
        }
    }
    return id;
}

/* f_1439_067f: what a conqueror finds in the treasury. */
int plunder(int city)
{
    int civ = (int8_t)CITY(city)->owner, size = (int8_t)CITY(city)->size;

    if (32000 / size > game.s.treasury[civ])
        return game.s.treasury[civ] * size / (game.s.citizen_count[civ] + 1);
    return game.s.treasury[civ] / (game.s.citizen_count[civ] + 1) * size;
}

/* f_1439_06ea: the winner takes an advance from the loser (the human chooses). */
int steal_advance(int civ, int from)
{
    int list[NUM_ADVANCES], n = 0, best = -1, best_v = -1, a, k;
    char msg[1600];

    if (civ == 0 || from == 0)
        return 0;
    strcpy(msg, "Select one...\n ");
    for (a = NUM_ADVANCES - 1; a >= 0; a--) {
        if (civ_has_advance(civ, a) || !civ_has_advance(from, a))
            continue;
        strncat(msg, advance_types[a].name, sizeof msg - strlen(msg) - 1);
        strncat(msg, "\n ", sizeof msg - strlen(msg) - 1);
        list[n++] = a;
        if (advance_value(civ, a) > best_v) {
            best_v = advance_value(civ, a);
            best = a;
        }
    }
    if (n == 0)
        return 0;
    if (civ == game.s.human_civ) {
        do {
            k = message_lock == -1 ? ui_ask(msg, 0) : ui_ask_emissary(msg);
        } while (k == -1);
        give_advance(civ, list[k], from);
        return 1;
    }
    if (from == game.s.human_civ) {
        snprintf(msg, sizeof msg, "%s take\n%s.\n", game.s.civ_names[civ], advance_types[best].name);
        if (message_lock == -1)
            ui_ask(msg, 0);
        else
            ui_ask_emissary(msg);
    }
    give_advance(civ, best, from);
    ai_rechoose_production(civ, -1);
    return 1;
}

/* o_05_1d03: a civilization's spaceship is lost. */
void spaceship_lost(int civ)
{
    int i, k;

    game.s.unk_ed30[civ] = -1;
    for (i = 0; i < 15; i++)
        for (k = 0; k < 12; k++)
            game.s.unk_far_2786_0740_bytes[civ * 180 + i * 12 + k] = NONE8;
}

/* g_eb8a: never written by the code (only read here and in the Hall of Fame), so 0. */
static const int unk_eb8a = 0;

/* Owner of a unit's home city.  A unit without one (0xFF) makes the original read past
 * the city table; the port answers "nobody" (-1). */
static int home_owner_u(int home)
{
    return home < MAX_CITIES ? (int8_t)CITY(home)->owner : -1;
}

static int home_owner_s(int home)
{
    return home_owner_u((uint8_t)home);
}

/* o_15_08ba: the most central city of `civ` (smallest total distance to the others). */
static int central_city(int civ)
{
    int best = -1, best_sum = 32767, i, k, sum;

    for (i = 0; i < MAX_CITIES; i++) {
        if ((int8_t)CITY(i)->owner != civ || CITY(i)->status == NONE8)
            continue;
        sum = 0;
        for (k = 0; k < MAX_CITIES; k++)
            if ((int8_t)CITY(k)->owner == civ && CITY(k)->status != NONE8)
                sum += map_distance(CITY(i)->x, CITY(i)->y, CITY(k)->x, CITY(k)->y);
        if (sum < best_sum) {
            best_sum = sum;
            best = i;
        }
    }
    return best;
}

/* o_15_087f */
static int switch_city(int city, int from, int to)
{
    (void)from;
    CITY(city)->owner = (uint8_t)to;
    map_set_unit_layer_owner(to, CITY(city)->x, CITY(city)->y);
    game.s.citizen_count[to] += (int8_t)CITY(city)->size;
    return (int8_t)CITY(city)->size;
}

/* o_15_0000: losing its capital splits a large civilization in two.  Returns 1 if it did. */
int civil_war(int civ)
{
    int ring_steps = 0;
    SaveData *s = &game.s;
    int rebel = -1, pop[16], cap_cont = -1, cap_x = 0, cap_y = 0, cap = 0, far, t, i, k, taken, total;
    int u, x, y, owner, nxt, type, nu, home_owner;
    char msg[400];

    for (i = 1; i < MAX_CIVS; i++)
        if (s->unit_count[i] == 0 && s->city_count[i] == 0) {
            rebel = i;
            break;
        }
    if (rebel == -1)
        return 0;
    for (i = 0; i < 16; i++)
        pop[i] = 0;
    for (i = 0; i < MAX_CITIES; i++) {
        City *c = CITY(i);
        if (c->status == NONE8 || (int8_t)c->owner != civ)
            continue;
        pop[map_continent(c->x, c->y)] += (int8_t)c->size;
        if ((c->buildings & BIT(B_PALACE)) && (cap_cont == -1 || s->capital_x[civ] == c->x)) {
            cap_cont = map_continent(c->x, c->y);
            cap_x = c->x;
            cap_y = c->y;
            cap = i;
        }
    }
    if (cap_cont == -1)
        return 0;
    far = rebel > s->unk_e214 + 1;
    s->civs_alive &= ~(1 << rebel);
    if (civ_restart(rebel) == 0)
        return 0;
    s->civs_alive |= 1 << rebel;
    t = s->civ_type[rebel];
    if (far || (t & (rebel != 7 ? 1 : 0)) || t > 15) {
        s->civ_type[rebel] = (int16_t)(rnd(2) ? rebel : rebel + 8);
        if (s->civ_type[rebel] >= 8)
            s->unk_ee3e |= 1 << rebel;
    } else {
        s->civ_type[rebel] ^= 8;
    }
    snprintf(s->leader_names[rebel], sizeof s->leader_names[rebel], "%s", civ_types[s->civ_type[rebel]].leader);
    snprintf(s->civ_adjectives[rebel], sizeof s->civ_adjectives[rebel], "%s", civ_types[s->civ_type[rebel]].adjective);
    if (civ_types[s->civ_type[rebel]].plural[0])
        snprintf(s->civ_names[rebel], sizeof s->civ_names[rebel], "%s", civ_types[s->civ_type[rebel]].plural);
    else
        snprintf(s->civ_names[rebel], sizeof s->civ_names[rebel], "%ss", civ_types[s->civ_type[rebel]].adjective);
    ui_text_section(civ == s->human_civ ? "*SCHISM" : "*ESCHISM", s->civ_adjectives[civ], s->civ_adjectives[rebel], msg, sizeof msg);
    ui_message(msg, 5);
    s->treasury[rebel] = s->treasury[civ] / 2;
    s->treasury[civ] -= s->treasury[civ] / 2;
    s->military_power[rebel] = s->military_power[civ] / 2;
    s->research_progress[rebel] = s->research_progress[civ];
    s->advances_known[rebel] = s->advances_known[civ];
    s->government[rebel] = s->government[civ];
    for (i = 0; i < 5; i++)
        s->advances[rebel][i] = s->advances[civ][i];
    s->unk_e204[rebel] = s->turn - 8;
    DIP(civ, rebel) |= DIP_CONTACT;
    DIP(rebel, civ) |= 9;

    /* The rebels take whole continents other than the capital's, smallest first... */
    total = pop[cap_cont];
    taken = 0;
    for (i = 0; i < 16; i++) {
        if (pop[i] == 0)
            continue;
        if (taken <= total && i != cap_cont) {
            taken += pop[i];
            pop[i] = 2;
        } else {
            if (i != cap_cont)
                total += pop[i];
            pop[i] = 1;
        }
    }
    if (taken * 2 > total && taken <= total) {
        for (i = 0; i < MAX_CITIES; i++)
            if (CITY(i)->status != NONE8 && (int8_t)CITY(i)->owner == civ &&
                pop[map_continent(CITY(i)->x, CITY(i)->y)] == 2)
                switch_city(i, civ, rebel);
    } else {
        /* ...or else the cities farthest from the capital, until they have their share. */
        taken = 0;
        while (taken * 9 / (unk_eb8a + 3) < s->citizen_count[civ]) {
            int far_d = 1, far_c = 0;
            for (i = 0; i < MAX_CITIES; i++) {
                if (CITY(i)->status == NONE8 || (int8_t)CITY(i)->owner != civ)
                    continue;
                k = map_distance(CITY(i)->x, CITY(i)->y, cap_x, cap_y);
                if (k > far_d) {
                    far_d = k;
                    far_c = i;
                }
            }
            taken += (int8_t)CITY(far_c)->size;
            switch_city(far_c, civ, rebel);
        }
    }
    s->citizen_count[civ] -= s->citizen_count[rebel];

    /* Units follow their home cities (units inside a rebel city join the rebels). */
    for (u = 0; u < MAX_UNITS; u++) {
        Unit *p = &s->units[civ][u];
        if (p->type == NONE8)
            continue;
        type = (int8_t)p->type;
        x = p->x;
        y = p->y;
        if (map_improvements(x, y) & IMP_CITY) {
            int c = city_at(x, y);
            owner = (int8_t)CITY(c)->owner;
            if (home_owner_u(p->home_city) != (int8_t)CITY(c)->owner) {
                delete_unit(civ, u);
                map_set_units_present(x, y);
                create_unit(owner, type, x, y);
                continue;
            }
            if (owner != rebel)
                continue;
            delete_unit(civ, u);
            map_set_units_present(x, y);
            nu = create_unit(rebel, type, x, y);
            s->units[rebel][nu].status = p->status & ~UNIT_SENTRY;
            continue;
        }
        home_owner = home_owner_s(p->home_city);
        nxt = (int8_t)p->next;
        ring_steps = 0;              /* (broken stack rings: see city_happiness) */
        if (home_owner == rebel) {
            delete_unit(civ, u);
            map_set_unit_layer_owner(rebel, x, y);
            map_set_units_present(x, y);
            nu = create_unit(rebel, type, x, y);
            s->units[rebel][nu].status = p->status & ~UNIT_SENTRY;
            s->units[rebel][nu].home_city = p->home_city;
            /* ...and take whatever they carry. */
            while (nxt != -1 && nxt != u && ++ring_steps < 1000) {
                Unit *q = &s->units[civ][nxt];
                int nn = (int8_t)q->next, qt = (int8_t)q->type;
                delete_unit(civ, nxt);
                map_set_unit_layer_owner(rebel, x, y);
                if (qt != -1) {
                    map_set_units_present(x, y);
                    nu = create_unit(rebel, qt, x, y);
                    s->units[rebel][nu].status = q->status & ~UNIT_SENTRY;
                    if (home_owner_s(q->home_city) != home_owner)
                        s->units[rebel][nu].home_city = q->home_city;
                }
                nxt = nn;
            }
        } else {
            while (nxt != -1 && nxt != u && ++ring_steps < 1000) {
                Unit *q = &s->units[civ][nxt];
                int nn = (int8_t)q->next;
                if (home_owner_s(q->home_city) != home_owner)
                    q->home_city = p->home_city;
                nxt = nn;
            }
        }
    }
    k = central_city(rebel);
    CITY(k)->buildings |= BIT(B_PALACE);
    s->capital_x[rebel] = CITY(k)->x;
    if (civ == s->human_civ) {
        view_center(s->human_civ, view_x, view_y);
    } else {
        CITY(cap)->owner = NONE8;
        k = central_city(civ);
        CITY(k)->buildings |= BIT(B_PALACE);
        s->capital_x[civ] = CITY(k)->x;
        CITY(cap)->owner = (uint8_t)civ;
    }
    return 1;
}

/* f_1439_0004: `civ` takes `city` (incited: by a diplomat's revolt). */
void city_capture(int civ, int city, int incited)
{
    SaveData *s = &game.s;
    City *c = CITY(city);
    int old = (int8_t)c->owner, x = c->x, y = c->y, human = s->human_civ, loot, i, k;
    char msg[256];

    if (civ == human)
        s->unk_e204[old] = -2;
    if (old != human && ((c->buildings & BIT(B_PALACE)) || c->producing == -1) && !incited &&
        (c->buildings & BIT(B_PALACE)) && s->city_count[old] > 4 && s->civ_rank[human] < s->civ_rank[old] &&
        !civil_war(old)) {
        c->owner = (uint8_t)civ;
        k = central_city(old);
        CITY(k)->producing = -1;                      /* they start a new Palace */
        c->owner = (uint8_t)old;
    }
    if (c->buildings & BIT(B_PALACE)) {
        if (s->unk_4c88 & (1 << (old + 8))) {
            snprintf(msg, sizeof msg, "%s spaceship\n%s", s->civ_adjectives[old],
                     (s->unk_4c88 & (1 << old)) ? "returns to Earth.\n" : "construction cancelled.\n");
            ui_message(msg, 0);
        }
        s->unk_4c88 &= ~(1 << old);
        s->unk_4c88 &= ~(1 << (old + 8));
        spaceship_lost(old);
    }
    if (old == human)
        s->unk_e204[civ] = -1;
    loot = clamp(plunder(city), 0, s->treasury[old]);
    if (civ == 0 && game.s.unit_types[s->units[0][unit_at(x, y)].type].attack <= 2 && !inciting) {
        destroy_city(city, x, y);                     /* weak barbarians just burn it */
        c->size = 0;
    } else {
        c->shields = 0;
        c->status &= 0xAE;
        c->buildings &= ~(BIT(B_PALACE) | BIT(B_TEMPLE) | BIT(B_COURTHOUSE) | BIT(B_CATHEDRAL));
        if (!incited) {
            c->buildings &= (uint16_t)(0xAAAA >> rnd(2));   /* half the rest, and nothing modern */
        }
        if (!incited || c->size > 1) {
            if (--c->size == 0)
                destroy_city(city, x, y);
        }
    }
    s->treasury[old] -= loot;
    if (civ != 0)
        s->treasury[civ] += loot;
    if (c->size != 0) {
        replay_log(9, 4, civ, c->name, x, y);
        ai_forget_spot(civ, 0, x, y, 4);
        c->status &= 0x9B;
        for (i = 0; i < 20; i++)
            map_put_pixel(wrap_x(x + around_dx[i]) + 80, y + around_dy[i], civ);
    }
    c->owner = (uint8_t)civ;
    if (civ == human || old == human) {
        map_refresh_known(x, y);
        if (old == human)
            view_follow(human, x, y);
        terrain_redraw(x, y);
        snprintf(msg, sizeof msg, "%s capture\n%s. %d gold\npieces plundered.\n", s->civ_names[civ],
                 s->city_names[c->name], loot);
        ui_sound(civ_types[s->civ_type[civ]].music);
        if (s->unk_19c0 & 8) {
            city_view(city, -2);
            ui_city_conquered(city, civ, msg);
            redraw_map();
        } else
            newspaper(city, msg);
        c->visible_size = civ == human ? 0 : c->size;
    } else if ((DIP(human, civ) & DIP_EMBASSY) || (DIP(human, old) & DIP_EMBASSY)) {
        snprintf(msg, sizeof msg, "Spies report:\n%s%s%s city\nof %s.\n", s->civ_names[civ],
                 c->size ? " capture\nthe " : " destroy\nthe ", s->civ_adjectives[old], s->city_names[c->name]);
        ui_message(msg, 0);
    }
    steal_advance(civ, old);
    map_set_unit_layer_owner(civ, x, y);
    for (i = 0; i < MAX_UNITS; i++) {
        Unit *p = &s->units[old][i];
        if (p->type == NONE8 || (int8_t)p->home_city != city)
            continue;
        if (p->x == c->x && p->y == c->y && c->size != 0)
            continue;
        delete_unit(old, i);
    }
    if (civ == human && s->government[human] < GOV_REPUBLIC)
        c->status |= CITY_DISORDER;
    if (c->size != 0) {
        if (c->producing >= 0)
            s->units_in_production[old][c->producing]--;
        if (civ == human) {
            city_screen(civ, city);          /* (city_process(city, 1)) */
            redraw_map();
        } else {
            c->producing = 0;
            c->producing = (int8_t)choose_production(civ, city);
            if (c->producing >= 0)
                s->units_in_production[civ][c->producing]++;
        }
    }
    civ_check_destroyed(old, civ);
}

/* f_14f6_0d9f: distance from (x, y) to the nearest palace of `civ` (at most 16). */
int capital_distance(int civ, int x, int y)
{
    int best = 16, i, d;

    for (i = 0; i < MAX_CITIES; i++) {
        const City *c = CITY(i);
        if (c->status == NONE8 || (int8_t)c->owner != civ || !(c->buildings & BIT(B_PALACE)))
            continue;
        d = map_distance(c->x, c->y, x, y);
        if (d < best)
            best = d;
    }
    return best;
}

/* o_22_0af5: `city` revolts and joins `to` (-1: whoever its unhappiest neighbour suggests). */
void city_revolt(int city, int to)
{
    SaveData *s = &game.s;
    City *c = CITY(city);
    int old = (int8_t)c->owner, x = c->x, y = c->y, i, best = 0, best_c = -1, d, v, u, t, nu;
    char msg[192];

    if (to == -1) {
        for (i = 0; i < MAX_CITIES && i != city; i++) {
            if (CITY(i)->status == NONE8 || (int8_t)CITY(i)->owner == old)
                break;
            d = map_distance(c->x, c->y, CITY(i)->x, CITY(i)->y);
            city_process(i, -1);
            v = ((calc.happy - calc.unhappy) << 5) / d;
            if (v > best) {
                best = v;
                best_c = i;
            }
        }
        if (best != 0)
            to = (int8_t)CITY(best_c)->owner;
    }
    if (to == -1)
        return;
    s->visibility[x][y] |= 1 << to;
    for (i = 0; i < 20; i++) {
        /* (x is not wrapped in the original; off-map squares write into the neighbouring rows) */
        int k = (x + city_square_dx[i]) * MAP_H + y + city_square_dy[i];
        if (k >= 0 && k < MAP_W * MAP_H)
            (&s->visibility[0][0])[k] |= 1 << to;
    }
    if (old == s->human_civ || to == s->human_civ || (DIP(s->human_civ, old) & DIP_EMBASSY) ||
        (DIP(s->human_civ, to) & DIP_EMBASSY)) {
        if (s->visibility[x][y] & (1 << s->human_civ))
            view_center(s->human_civ, x - 8, y - 5);
        snprintf(msg, sizeof msg, "%s%s%s%s%s", s->civ_names[old],
                 to ? " rebel!\nCivil War in\n" : " declare\nindependence in\n", s->city_names[c->name],
                 to ? ".\n" : "\n", "");
        if (to) {
            strncat(msg, s->civ_adjectives[to], sizeof msg - strlen(msg) - 1);
            strncat(msg, " influence\nsuspected.\n", sizeof msg - strlen(msg) - 1);
        }
        ui_message(msg, 0);
    }
    for (u = 0; u < MAX_UNITS; u++) {
        Unit *p = &s->units[old][u];
        if (p->type == NONE8)
            continue;
        d = map_distance(x, y, p->x, p->y);
        if (d > 1 || (d != 0 && (map_improvements(p->x, p->y) & IMP_CITY)))
            continue;
        t = (int8_t)p->type;
        delete_unit(old, u);
        map_set_unit_layer_owner(to, p->x, p->y);
        map_set_units_present(p->x, p->y);
        nu = create_unit(to, t, p->x, p->y);
        if (nu == -1)
            continue;
        s->units[to][nu].visible |= 1 << old;
        s->units[to][nu].home_city = (uint8_t)city;
    }
    inciting = 1;
    city_capture(to, city, 1);
    inciting = 0;
    map_set_unit_layer_owner(to, c->x, c->y);
}

/* o_22_0639: a diplomat of `civ` offers money to the lone unit (owner, u).  */
void diplomat_bribe(int owner, int u, int civ)
{
    SaveData *s = &game.s;
    Unit *p = &s->units[owner][u];
    int cost, d, t, nu;
    char msg[192];

    if (stack_sum_of(owner, u, 2) > 1)
        return;
    d = capital_distance(owner, p->x, p->y);
    cost = (s->treasury[owner] + 750) / (d + 2) * game.s.unit_types[p->type].cost;
    if (p->type != UNIT_SETTLERS)
        cost /= 2;
    if (civ != s->human_civ && s->treasury[civ] / 2 < cost)
        return;
    snprintf(msg, sizeof msg, "%s %s\nwill desert for %d$.\nTreasury: %d$\n%s", s->civ_adjectives[owner],
             s->unit_type_names[p->type], cost, s->treasury[civ], cost <= s->treasury[civ] ? " Forget it.\n Pay\n" : "");
    if (civ == s->human_civ && ui_ask(msg, 0) != 1)
        return;
    if (owner == s->human_civ) {
        view_center(owner, p->x - 8, p->y - 6);
        snprintf(msg, sizeof msg, "%s %s unit\nbribed by %s!\n", s->civ_adjectives[owner],
                 s->unit_type_names[p->type], s->civ_names[civ]);
        ui_message(msg, 0);
    }
    t = (int8_t)p->type;
    delete_unit(owner, u);
    s->units_active[owner][t]--;
    nu = create_unit(civ, t, p->x, p->y);
    if (nu != -1) {
        s->units_active[civ][t]++;
        s->units[civ][nu].visible |= 1 << owner;
    }
    s->treasury[civ] -= cost;
    if (owner == s->human_civ) {
        square_redraw(p->x, p->y);
        status_report();
    }
}

/* o_22_0000: a diplomat of `civ` enters foreign `city`. */
void diplomat_enters_city(int city, int civ, int u)
{
    SaveData *s = &game.s;
    City *c = CITY(city);
    int owner = (int8_t)c->owner, human = s->human_civ, choice, i, a, start, b, tries, d, cost, disabled = 0;
    char msg[256];

    s->units[civ][u].goto_x = NONE8;
    if (civ == human) {
        if ((DIP(human, owner) & DIP_EMBASSY) || owner == 0)
            disabled = 1;                             /* already have an embassy */
        if (c->status & 0x20)
            disabled |= 4;                            /* already robbed */
        if (c->buildings & BIT(B_PALACE))
            disabled |= 16;                           /* capitals do not revolt */
        if (owner == 0)
            disabled |= 32;
        snprintf(msg, sizeof msg, "%s diplomat arrives\nin %s...\n Establish Embassy\n Investigate City\n Steal Technology\n"
                 " Industrial Sabotage\n Incite a Revolt\n Meet with King\n", s->civ_adjectives[civ], s->city_names[c->name]);
        choice = ui_menu(msg, disabled);
    } else {
        choice = 2;
        if (owner == human && (DIP(human, civ) & DIP_PEACE))
            goto gone;                                /* at peace with us: no mischief */
    }
    switch (choice) {
    case 0:                                           /* establish embassy */
        DIP(human, owner) |= DIP_EMBASSY;
        delete_unit(civ, u);
        ui_intelligence_report_civ(owner);
        return;
    case 1:                                           /* investigate */
        city_screen(owner, city);
        goto gone;
    case 2:                                           /* steal technology */
        if (civ == human) {
            if (c->status & 0x20)
                return;
            if (confirm_break_treaty(owner) + 1 == 0)
                return;
            diplomacy_clear(human, owner, DIP_PEACE);
        } else if (c->status & 0x20) {
            goto sabotage;
        }
        msg[0] = 0;
        start = rnd(NUM_ADVANCES);
        for (i = 0; i < NUM_ADVANCES; i++) {
            a = (start + i) % NUM_ADVANCES;
            if (civ == 0 || !civ_has_advance(owner, a) || civ_has_advance(civ, a))
                continue;
            give_advance(civ, a, owner);
            snprintf(msg, sizeof msg, "%s steal\n%s.\n", s->civ_names[civ], advance_types[a].name);
            ui_message(msg, 0);
            c->status |= 0x20;
            break;
        }
        if (msg[0])
            goto gone;
        if (civ == human) {
            ui_message("No new technology found.\n", 0);
            return;
        }
        /* fall through */
    case 3:                                           /* industrial sabotage */
    sabotage:
        tries = 0;
        if (rnd(2) != 0 || c->shields == 0) {
            do {
                b = rnd(24);
                tries++;
            } while (tries < 99 && (b + 1 == 1 || !(c->buildings & (1UL << b))));
            if (tries < 99) {
                c->buildings &= ~(1UL << b);
                snprintf(msg, sizeof msg, "%s destroyed\nin %s.\n", building_types[b + 1].name, s->city_names[c->name]);
            } else {
                tries = 0;
            }
        } else {
            tries = 0;
        }
        if (tries == 0) {
            c->shields = 0;
            snprintf(msg, sizeof msg, "%s production sabotaged\nin %s.\n",
                     c->producing >= 0 ? s->unit_type_names[c->producing] : building_types[-c->producing].name,
                     s->city_names[c->name]);
        }
        ui_message(msg, 0);
        goto gone;
    case 4:                                           /* incite a revolt */
        d = capital_distance(owner, c->x, c->y);
        if (d == 0)
            return;
        cost = (s->treasury[owner] + 1000) / (d + 3) * (int8_t)c->size;
        if (c->status & CITY_DISORDER)
            cost /= 2;
        snprintf(msg, sizeof msg, "Dissidents in %s\nwill revolt for $%d.\n%s%s", s->city_names[c->name], cost,
                 cost <= s->treasury[civ] ? " Forget it.\n Incite revolt\n" : "",
                 cost <= s->treasury[civ] && cost * 2 <= s->treasury[civ] && (DIP(civ, owner) & DIP_PEACE)
                     ? " Subvert city ($x2)\n" : "");
        choice = ui_ask(msg, 0);
        if (choice < 1)
            return;
        if (choice == 1 && (DIP(owner, civ) & DIP_PEACE)) {
            diplomacy_clear(owner, civ, DIP_PEACE);
            DIP(owner, civ) |= 8;
        }
        s->treasury[civ] -= cost * choice;
        city_revolt(city, civ);
        square_redraw(c->x, c->y);
        delete_unit(civ, u);
        status_report();
        return;
    case 5:                                           /* meet with the king */
        if (owner != 0)
            diplomacy_meeting(owner, s->units[civ][u].x, s->units[civ][u].y, 1);
        s->units[civ][u].moves = 0;
        return;
    default:
        return;
    }
gone:
    delete_unit(civ, u);
}
