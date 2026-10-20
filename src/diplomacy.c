/* Relations between civilizations (original seg 14f6, overlay 6). */
#include <stdio.h>
#include "game.h"
#include "civ.h"
#include "diplomacy.h"
#include "replay.h"
#include "ui.h"
#include "meeting.h"
#include "city.h"
#include "science.h"
#include "util.h"
#include "ai.h"

#define DIP(a, b) game.s.diplomacy[a][b]

/* f_14f6_0a38: set a flag both ways.  Peace treaties go in the replay. */
void diplomacy_set(int civ, int other, int flag)
{
    if (flag == DIP_PEACE && !(DIP(civ, other) & flag))
        replay_log(3, 1, other + (civ << 4), 0, 0, 0);
    DIP(civ, other) |= flag;
    DIP(other, civ) |= flag;
    if (flag == DIP_PEACE) {
        DIP(civ, other) &= ~0x20;
        DIP(other, civ) &= ~0x20;
    }
}

/* f_14f6_0aa9: clear a flag both ways.  Ending peace is a declaration of war. */
void diplomacy_clear(int civ, int other, int flag)
{
    if (flag == DIP_PEACE && (DIP(civ, other) & flag))
        replay_log(2, 1, other + (civ << 4), 0, 0, 0);
    DIP(civ, other) &= ~flag;
    DIP(other, civ) &= ~flag;
    if (flag & DIP_PEACE) {
        DIP(civ, other) &= ~0x04;
        DIP(other, civ) &= ~0x04;
    }
}

/* f_14f6_0e3b */
int civ_nuclear_count(int civ)
{
    return game.s.units_active[civ][UNIT_NUCLEAR];
}

/* f_14f6_0b25: would `civ` go to war with `other`? */
int ai_wants_war(int civ, int other)
{
    int human = game.s.human_civ, n, k, power = 0, weight = 1, any = 0, cont;

    if (!(game.s.rules_flags & 1))
        return 1;
    /* Do not gang up on a stronger human. */
    if ((DIP(civ, human) & 3) == 1 && game.s.military_power[other] < game.s.military_power[human] &&
        !(DIP(other, human) & 4))
        return 0;
    if ((DIP(other, human) & 3) == 1 && game.s.military_power[civ] < game.s.military_power[human]) {
        if (!(DIP(civ, human) & 4))
            return 0;
    }
    if (game.s.units_active[civ][UNIT_NUCLEAR] < civ_nuclear_count(other))
        return 0;
    n = 0;
    for (k = 1; k < MAX_CIVS; k++)
        if (k != civ && k != other && (DIP(civ, k) & 3) == 1)
            n++;
    if (DIP(civ, other) & DIP_PEACE)
        n++;
    n -= civ_types[game.s.civ_type[civ]].aggression;
    for (cont = 1; cont < 15; cont++) {
        if (game.s.unk_6b16[civ][cont] != 0 &&
            game.s.unk_8f9a[civ][cont] / 2 + game.s.unk_6c2c[civ][cont] < game.s.unk_8f9a[other][cont])
            return 0;
        if (game.s.unk_8f9a[civ][cont] != 0 && game.s.unk_6c2c[other][cont] != 0) {
            power += game.s.unk_8f9a[civ][cont];
            weight += game.s.unk_8f9a[other][cont] / 2 + game.s.unk_6c2c[other][cont];
            any += game.s.unk_6b16[civ][cont];
        }
    }
    if (power * 4 / weight >= n + 4)
        return 1;
    return any == 0;
}

/* f_14f6_0cf7: if each side knows something the other does not, they trade one advance each. */
int exchange_advances(int civ, int other)
{
    int a, mine = 0, theirs = 0, which = 0;

    for (a = 0; a < NUM_ADVANCES; a++) {
        if (!civ_has_advance(civ, a) && civ_has_advance(other, a)) {
            which |= 1;
            theirs = a;
        }
        if (!civ_has_advance(other, a) && civ_has_advance(civ, a)) {
            which |= 2;
            mine = a;
        }
    }
    if (which != 3)
        return 0;
    give_advance(civ, theirs, other);
    give_advance(other, mine, civ);
    return 1;
}

/* f_14f6_073f: units of two civilizations have met near (x, y). */
void civ_contact(int civ, int other, int x, int y)
{
    int human = game.s.human_civ, old;

    if (civ == 0 || other == 0)
        return;
    if (civ == human && (!(DIP(civ, other) & DIP_CONTACT) || game.s.turn - game.s.unk_e204[other] >= 16))
        diplomacy_meeting(other, x, y, 0);
    if (other == human && (!(DIP(other, civ) & DIP_CONTACT) || game.s.turn - game.s.unk_e204[civ] >= 16))
        diplomacy_meeting(civ, x, y, 0);
    old = DIP(civ, other);
    DIP(civ, other) |= DIP_CONTACT | DIP_MET;
    DIP(other, civ) |= DIP_CONTACT | DIP_MET;
    if (civ == human || other == human)
        return;
    if (((civ + other + game.s.turn) & 3) && (old & DIP_CONTACT))
        return;
    if ((DIP(civ, other) & 8) || (DIP(other, civ) & 8))
        return;
    if ((ai_wants_war(civ, other) && !civ_has_wonder(other, 7) && game.s.government[civ] < 4) ||
        (ai_wants_war(other, civ) && !civ_has_wonder(civ, 7) && game.s.government[other] < 4)) {
        if (!(DIP(civ, other) & DIP_PEACE) && (old & DIP_CONTACT))
            return;
        if ((DIP(human, civ) & DIP_EMBASSY) || (DIP(human, other) & DIP_EMBASSY)) {
            char msg[160];
            snprintf(msg, sizeof msg, "%s and\n%s%s", game.s.civ_names[civ], game.s.civ_names[other],
                     (DIP(civ, other) & DIP_PEACE) ? " cancel\npeace treaty.\n" : " declare\nwar on each other.\n");
            ui_message(msg, 1);
        }
        diplomacy_clear(civ, other, DIP_PEACE);
    } else {
        exchange_advances(civ, other);
        if (DIP(civ, other) & DIP_PEACE)
            return;
        if ((DIP(human, civ) & DIP_EMBASSY) || (DIP(human, other) & DIP_EMBASSY)) {
            char msg[160];
            snprintf(msg, sizeof msg, "%s and\n%s sign\na peace treaty.\n", game.s.civ_names[civ], game.s.civ_names[other]);
            ui_message(msg, 1);
        }
        diplomacy_set(civ, other, DIP_PEACE);
    }
}

/* ai_diplomacy (f_14f6_000c): a civilization's relations at the start of its turn: the end of
 * anarchy, ceasefires running out, contacts lapsing, and envoys asking the human for help. */
void ai_diplomacy(int civ)
{
    SaveData *s = &game.s;
    int human = s->human_civ, o, f, gold;
    char msg[160], amount[16];

    if (s->government[civ] == GOV_ANARCHY) {
        if (civ == human && (s->turn & 3) == 0) {
            if (DIP(civ, 0) & 4)                       /* (bit 4 of the barbarian entry: revolution) */
                DIP(civ, 0) &= ~4;
            else
                set_government(civ, GOV_DESPOTISM);
        } else if ((s->turn & 3) == 0 || civ_has_wonder(civ, WONDER_PYRAMIDS)) {
            set_government(civ, GOV_DESPOTISM);
            if (civ == human)
                DIP(civ, 0) &= ~4;
        }
    }
    if (civ != human && (DIP(civ, 0) & 4))
        set_government(civ, s->government[civ]);

    for (o = 1; o < MAX_CIVS; o++) {
        if ((DIP(civ, o) & 0x100) && rnd(3) == 0) {
            /* A ceasefire breaks down. */
            DIP(civ, o) &= ~0x100;
            DIP(civ, o) &= ~DIP_PEACE;
            DIP(civ, o) |= 0x200;
        }
        if ((s->turn & 15) != 0)
            continue;
        f = (uint8_t)DIP(civ, o);
        if (!(f & DIP_MET)) {
            if ((f & DIP_CONTACT) && !(f & DIP_PEACE) &&
                ((DIP(human, civ) & DIP_EMBASSY) || (DIP(human, o) & DIP_EMBASSY)) && civ != human && o != human) {
                snprintf(msg, sizeof msg, "The war between\nthe %s and the\n%s has ended.\n", s->civ_names[civ],
                         s->civ_names[o]);
                ui_message(msg, 1);
            }
            DIP(o, civ) &= ~DIP_CONTACT;
            DIP(civ, o) &= ~DIP_CONTACT;
        }
        DIP(o, civ) &= ~DIP_MET;
        DIP(civ, o) &= ~DIP_MET;
    }

    if (civ == 0 || ((uint8_t)s->turn & 31) != civ * 4 || civ == human)
        return;
    for (o = 1; o < MAX_CIVS; o++) {
        if ((DIP(civ, o) & 0x20) && (DIP(human, o) & DIP_PEACE) && !(DIP(civ, human) & 0x20)) {
            if (s->military_power[civ] * 2 < s->military_power[o] && diplomacy_audience(civ, -1, 0) == 0) {
                /* Losing a war: pay the human to join in. */
                gold = clamp(s->treasury[civ] / 100, 1, 10) * 50;
                snprintf(amount, sizeof amount, "%d", gold);
                if (ui_envoy_proposal(civ, "*HELPME", s->civ_names[o], amount) == 1) {
                    s->treasury[human] += (int16_t)gold;
                    s->treasury[civ] += (int16_t)gold;   /* (sic: both get the money) */
                    diplomacy_set(civ, human, DIP_PEACE);
                    diplomacy_clear(o, human, DIP_PEACE);
                    *(uint8_t *)&DIP(o, human) |= 8;
                }
                diplomacy_audience_end();
            }
            if (s->military_power[o] > s->military_power[civ] && s->military_power[civ] * 2 >= s->military_power[o] &&
                diplomacy_audience(civ, -1, 0) == 0) {
                /* An even fight: ask the human to join a crusade. */
                if (ui_envoy_proposal(civ, "*CRUSADE", s->civ_names[o],
                                      (DIP(civ, human) & DIP_PEACE) ? "extend our" : "agree to a") == 1) {
                    diplomacy_set(civ, human, DIP_PEACE);
                    diplomacy_clear(o, human, DIP_PEACE);
                    *(uint8_t *)&DIP(o, human) |= 8;
                    s->unk_e204[civ] = (int16_t)(s->turn + 16);
                }
                diplomacy_audience_end();
            }
        }
        *(uint8_t *)&DIP(civ, o) &= ~0x20;
    }
}
