/* Combat (original seg 1a19). */
#include <stdio.h>
#include "game.h"
#include "civ.h"
#include "city.h"
#include "map.h"
#include "unit.h"
#include "util.h"
#include "ai.h"
#include "diplomacy.h"
#include "combat.h"
#include "ui.h"
#include "view.h"
#include "move.h"

#define DIP(a, b) game.s.diplomacy[a][b]

/* f_1a19_0c9e: the human is about to attack someone with whom there is peace.
 * Returns 0 if not at peace, 1 to go ahead, -1 if called off (or overruled). */
int confirm_break_treaty(int civ)
{
    int human = game.s.human_civ;
    char msg[160];

    if (!(DIP(human, civ) & DIP_PEACE))
        return 0;
    snprintf(msg, sizeof msg, "We have signed a\npeace treaty with\nthe %s!\n Cancel action.\n Break treaty.\n",
             game.s.civ_names[civ]);
    if (ui_ask(msg, 5) != 1)
        return -1;
    if (game.s.government[human] >= GOV_REPUBLIC) {
        ui_message("Overruled by\nthe Senate.\nAction canceled.\n", 4);
        return -1;
    }
    return 1;
}

/* f_1a19_0d4d: a nuclear attack on (x, y). */
void nuclear_attack(int civ, int x, int y)
{
    int i, sx, sy, top, c, n, owner;
    char msg[160];

    if (civ != game.s.human_civ) {
        snprintf(msg, sizeof msg, "%s use\nnuclear weapons!\n", game.s.civ_names[civ]);
        ui_message(msg, 3);
    }
    ui_nuclear_explosion(x, y);
    for (i = 8; i >= 0; i--) {
        sx = x + around_dx[i];
        sy = y + around_dy[i];
        top = unit_at(sx, sy);
        if (map_improvements(sx, sy) & IMP_CITY) {
            c = city_at(sx, sy);
            if (game.s.cities[c].buildings & BIT(B_SDI)) {
                snprintf(msg, sizeof msg, "SDI protects %s.\n", game.s.city_names[game.s.cities[c].name]);
                ui_message(msg, 0);
                top = -1;
            }
        }
        if (top == -1)
            continue;
        owner = stack_owner;
        n = stack_sum_of(owner, top, 2);
        game.s.kills[civ][owner] += (int16_t)n;
        stack_foreach(owner, top, delete_unit);
        if (civ != owner)
            *(uint8_t *)&DIP(owner, civ) |= 0x88;
    }
    nuclear_fallout(x, y);
}

/* f_1a19_000e: unit (civ, u) attacks the stack (dciv, du).  With `fight` 0 it only
 * returns the odds (attack * 8 / defence).  Otherwise 1 = attacker won, 0 = lost,
 * -1 = called off. */
int unit_attack(int civ, int u, int dciv, int du, int fight)
{
    Unit *a = &game.s.units[civ][u], *d = &game.s.units[dciv][du];
    int at = a->type, dt = d->type, human = game.s.human_civ;
    const UnitType *aut = &game.s.unit_types[at], *dut = &game.s.unit_types[dt];
    int terrain = map_terrain(d->x, d->y), tdef = terrain_types[terrain].defense;
    int att = aut->attack << 3, def, city = -1, barbarian_city = 0, win, n, i, sx, sy, top;
    char msg[160];

    if (map_improvements(d->x, d->y) & IMP_FORTRESS)
        def = tdef * dut->defense << 3;
    else if (d->status & UNIT_FORTIFIED)
        def = tdef * dut->defense * 12 / 2;
    else
        def = tdef * dut->defense * 4;
    if (civ == 0)
        att = dciv == human ? att * (game.s.difficulty + 1) / 4 : att / 2;
    if (dut->domain == DOMAIN_AIR || dut->domain == DOMAIN_SEA)
        def = dut->defense << 3;
    if (map_improvements(d->x, d->y) & IMP_CITY) {
        city = city_at(d->x, d->y);
        if ((game.s.cities[city].buildings & BIT(B_CITY_WALLS)) && aut->attack != 12 && dut->domain != DOMAIN_AIR)
            def = tdef * dut->defense * 12;
        if (civ == 0) {
            /* Barbarians are kind to small civilizations and to capitals. */
            if (game.s.city_count[dciv] == 1)
                att = 0;
            if (game.s.cities[city].buildings & BIT(B_PALACE))
                att /= 2;
            barbarian_city = 1;
        }
    }
    if (a->status & UNIT_VETERAN)
        att += att / 2;
    if (d->status & UNIT_VETERAN)
        def += def / 2;
    if (a->moves < 3)
        att = (int8_t)a->moves * att / 3;
    if (!fight)
        return (att << 3) / (def + 1);

    if (civ != 0) {
        if (game.s.difficulty <= 1 && dciv == human)
            att /= 2;
        if (game.s.difficulty == 0 && civ == human)
            att <<= 1;
    }
    if (civ == human && confirm_break_treaty(dciv) == -1) {
        a->goto_x = NONE8;
        return -1;
    }
    win = rnd(def) < rnd(att);
    if (barbarian_city && win && rnd(att) < rnd(def))
        win = 0;
    if ((civ == human || dciv == human || (a->visible & (1 << human)) || (d->visible & (1 << human)) || show_all) &&
        !fast_view) {
        combat_display(civ, u, dciv, du, win);
    }

    *(uint8_t *)&DIP(civ, dciv) |= 0x20;
    *(uint8_t *)&DIP(dciv, civ) |= 0x20;
    if (civ != 0 && dciv != 0)
        game.s.peace_turns = 0;
    if ((dciv == human && (DIP(dciv, civ) & DIP_PEACE)) || (DIP(civ, dciv) & 0x200)) {
        snprintf(msg, sizeof msg, "Sneak attack by\n%s forces!\n", game.s.civ_adjectives[civ]);
        ui_message(msg, 3);
        DIP(civ, dciv) &= ~0x200;
    }
    if ((DIP(civ, dciv) & DIP_PEACE) || (DIP(dciv, civ) & DIP_PEACE)) {
        if (civ == human || dciv == human) {
            snprintf(msg, sizeof msg, "%s cancel\npeace treaty\nwith %s.\n", game.s.civ_names[dciv], game.s.civ_names[civ]);
            ui_message(msg, 5);
        }
        if (civ == human && (DIP(dciv, civ) & DIP_PEACE))
            DIP(dciv, civ) |= 8;                     /* betrayed */
        diplomacy_clear(civ, dciv, DIP_PEACE);
    }
    if (civ == human)
        DIP(dciv, civ) &= ~0x200;

    if (at == UNIT_NUCLEAR) {
        nuclear_attack(civ, d->x, d->y);
        return 1;
    }
    if (!win) {
        game.s.kills[dciv][civ]++;
        delete_unit(civ, u);
        if (rnd(2))
            d->status |= UNIT_VETERAN;
        return 0;
    }

    if (dciv == 0 && dt == UNIT_DIPLOMAT) {          /* a barbarian leader, for ransom */
        game.s.treasury[civ] += 100;
        if (civ == human)
            ui_help_message("*LEADER");
    }
    if (city == -1 && !(map_improvements(d->x, d->y) & IMP_FORTRESS)) {
        /* In the open the whole stack goes down with its defender. */
        n = stack_sum_of(dciv, du, 2);
        game.s.kills[civ][dciv] += (int16_t)n;
        stack_foreach(dciv, du, delete_unit);
        if (n > 1 && (civ == human || dciv == human)) {
            snprintf(msg, sizeof msg, "%d units destroyed.\n", n);
            ui_message(msg, 0);
        }
    } else {
        if (city != -1 && d->next == NONE8) {
            /* The city's last defender falls: the neighbours stop what they are doing. */
            for (i = 8; i >= 0; i--) {
                sx = d->x + around_dx[i];
                sy = d->y + around_dy[i];
                top = unit_at(sx, sy);
                if (top != -1 && stack_owner != human)
                    stack_foreach(stack_owner, top, cb_reset_orders);
            }
            if ((int8_t)game.s.cities[city].owner == human)
                ai_go_for_undefended(city);
        }
        delete_unit(dciv, du);
        game.s.kills[civ][dciv]++;
    }
    if (rnd(2))
        a->status |= UNIT_VETERAN;
    return 1;
}
