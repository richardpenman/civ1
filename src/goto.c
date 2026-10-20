/* "Go to" pathfinding (original seg 1e4a).
 *
 * Two levels: the map is cut into 20 x 13 blocks of 4 x 4 squares, each with a byte of
 * directions to connected neighbour blocks (land_links is saved; sea_links is only built
 * with a new world, so after loading a game it is empty, as in the original).  A
 * breadth-first search over blocks gives an intermediate target; then a cost-weighted
 * search in a 16 x 16 window around the target picks the actual step. */
#include <stdlib.h>
#include <string.h>
#include "game.h"
#include "map.h"
#include "unit.h"
#include "util.h"
#include "move.h"

#define BW 20
#define BH 13

uint8_t sea_links[BW * BH];               /* g_7eb8 */
static uint8_t block_dist[BW * BH];       /* g_e736 */
static int8_t queue_x[256], queue_y[256]; /* g_6516 / g_6616 */
static uint8_t fine_dist[256];            /* g_c6a0 */
static int fine_cx = 0, fine_cy = 0;      /* g_6718 / g_671a: target the window was built for */
static int fine_len;                      /* g_6716 */
static int target_x, target_y;            /* g_6512 / g_6514 */

static uint8_t *links(int sea)
{
    return sea ? sea_links : game.s.land_links;
}

static int fine_at(int dx, int dy)
{
    int k = dx * 16 + dy;

    return k >= 0 && k < 256 ? fine_dist[k] : 0;    /* (the original reads out of range) */
}

static int is_sea(int x, int y)
{
    return map_terrain(x, y) == TERRAIN_OCEAN;
}

/* f_1e4a_0c13: step from unit (civ, u) toward (target_x, target_y), searching outward from
 * the target up to `limit` cost.  Returns a direction or -1. */
static int fine_path(int civ, int u, int limit)
{
    Unit *p = &game.s.units[civ][u];
    int ux = p->x, uy = p->y, ox = target_x - 8, oy = target_y - 8;
    int sea, one_move, head, tail, qx, qy, d, k, nx, ny, wnx, t, cost, old, road, best = -1, best_d, sc, sc2, top;

    fine_len = 0;
    if (!(target_x == fine_cx && target_y == fine_cy && fine_at(ux - ox, uy - oy) != 0)) {
        fine_cx = target_x;
        fine_cy = target_y;
        memset(fine_dist, 0, sizeof fine_dist);
        head = tail = 0;
        queue_x[tail] = (int8_t)target_x;
        queue_y[tail++] = (int8_t)target_y;
        fine_dist[(target_x - ox) * 16 + target_y - oy] = 1;
        fine_len = limit;
        sea = game.s.unit_types[p->type].domain == DOMAIN_SEA;
        one_move = game.s.unit_types[p->type].moves == 1;
        for (;;) {
            qx = queue_x[head];
            qy = queue_y[head];
            head = (head + 1) & 0xFF;
            d = fine_at(qx - ox, qy - oy);
            if (d <= fine_len) {
                if (wrap_x(qx) == ux && qy == uy) {
                    fine_len = d;
                } else {
                    road = map_improvements(wrap_x(qx), qy) & IMP_ROAD;
                    for (k = 1; k <= 8; k++) {
                        nx = qx + around_dx[k];
                        if (abs(nx - target_x) >= 8)
                            continue;
                        wnx = wrap_x(nx);
                        ny = qy + around_dy[k];
                        if (abs(ny - target_y) >= 8 || !map_in_bounds(wnx, ny))
                            continue;
                        t = map_terrain(wnx, ny);
                        if ((t == TERRAIN_OCEAN) != sea && !(map_improvements(wnx, ny) & IMP_CITY))
                            continue;
                        if (road && (map_improvements(wnx, ny) & IMP_ROAD))
                            cost = d + 1;
                        else if (one_move)
                            cost = d + 3;
                        else
                            cost = terrain_types[t].move_cost * 3 + d;
                        old = fine_dist[(nx - ox) * 16 + ny - oy];
                        if (old != 0 && old <= cost)
                            continue;
                        fine_dist[(nx - ox) * 16 + ny - oy] = (uint8_t)cost;
                        queue_x[tail] = (int8_t)nx;
                        queue_y[tail] = (int8_t)ny;
                        tail = (tail + 1) & 0xFF;
                    }
                }
            }
            if (head == tail || head >= 225)
                break;
        }
    }

    if (limit > fine_len) {
        best_d = 99;
        sea = game.s.unit_types[p->type].domain == DOMAIN_SEA;
        sc = 0;
        for (k = 1; k <= 8; k++) {
            nx = ux + around_dx[k];
            if (abs(nx - target_x) >= 72)
                nx += nx > target_x ? -80 : 80;
            if (abs(nx - target_x) >= 8)
                continue;
            wnx = wrap_x(nx);
            ny = uy + around_dy[k];
            if (abs(ny - target_y) >= 8)
                continue;
            if (is_sea(wnx, ny) != sea && !(map_improvements(wnx, ny) & IMP_CITY))
                continue;
            d = fine_at(nx - ox, ny - oy);
            if (d == 0)
                continue;
            if (d < best_d) {
                best_d = d;
                best = k;
                top = unit_at(wnx, ny);
                sc = (top != -1 ? stack_sum_of(civ, top, 2) * 4 : 0) + map_distance(target_x, target_y, wnx, ny);
            }
            if (d == best_d) {
                top = unit_at(wnx, ny);
                sc2 = (top != -1 ? stack_sum_of(civ, top, 2) * 4 : 0) + map_distance(target_x, target_y, wnx, ny);
                if (sc2 < sc) {
                    best = k;
                    sc = sc2;
                }
            }
        }
        if (best != -1)
            return best;
    }
    target_x = p->goto_x;
    target_y = p->goto_y;
    return -1;
}

/* f_1e4a_1112: can a militia (or trireme) get from (x1, y1) to (x2, y2) within `limit`?
 * Uses barbarian unit slot 127 as a scratch unit.  Returns the path cost or -1. */
int reachable(int x1, int y1, int x2, int y2, int sea, int limit)
{
    Unit *scratch = &game.s.units[0][127];
    int r;

    if (abs(x1 - x2) > 7 && abs(y1 - y2) > 7)
        return -1;
    scratch->x = (uint8_t)x1;
    scratch->y = (uint8_t)y1;
    scratch->type = (uint8_t)(sea ? UNIT_TRIREME : 1);
    target_x = x2;
    target_y = y2;
    r = fine_path(0, 127, limit);
    scratch->type = NONE8;
    return r == -1 ? -1 : fine_len;
}

/* f_1e4a_0a22: the block for square (x, y), or a reachable neighbour block if it has no
 * links.  Leaves the block in target_x/target_y; returns 0 if none. */
static int block_of(int x, int y, int sea)
{
    int bx = x / 4, by = y / 4, dir = -1, best = 99, k, nbx, nby, d, px, py;

    if (links(sea)[bx * BH + by] != 0)
        dir = 0;
    if (dir == -1) {
        for (k = 1; k <= 8; k++) {
            nbx = bx + around_dx[k];
            nby = by + around_dy[k];
            if (links(sea)[nbx * BH + nby] == 0)
                continue;
            d = distance(x - (nbx << 2) - 1, y - (nby << 2) - 1);
            if (d >= best)
                continue;
            px = nbx * 4 + 1;
            py = nby * 4 + 1;
            if (is_sea(px, py) != sea) {
                px++;
                if (is_sea(px, py) != sea) {
                    py++;
                    if (is_sea(px, py) != sea) {
                        px--;
                        if (is_sea(px, py) != sea)
                            continue;
                    }
                }
            }
            if (reachable(px, py, x, y, sea, 18) + 1 != 0) {
                best = d;
                dir = k;
            }
        }
    }
    if (dir == -1)
        return 0;
    target_x = around_dx[dir] + bx;
    target_y = around_dy[dir] + by;
    return 1;
}

/* f_1e4a_05dc: an intermediate target for a long trip, from the block graph. */
static int coarse_path(int civ, int u)
{
    Unit *p = &game.s.units[civ][u];
    int sea = game.s.unit_types[p->type].domain == DOMAIN_SEA;
    int sbx, sby, head = 0, tail = 0, found = 0, bx, by, d, conn, k, nbx, nby, best_d, best_k, near = 0, r;

    target_x = p->goto_x;
    target_y = p->goto_y;
    if (!block_of(p->x, p->y, sea)) {
        target_x = p->goto_x;
        target_y = p->goto_y;
        return 0;
    }
    sbx = target_x;
    sby = target_y;
    block_of(p->goto_x, p->goto_y, sea);
    memset(block_dist, 0, sizeof block_dist);
    queue_x[tail] = (int8_t)target_x;
    queue_y[tail++] = (int8_t)target_y;
    block_dist[target_x * BH + target_y] = 1;
    for (;;) {
        bx = queue_x[head];
        by = queue_y[head];
        if (bx == sbx && by == sby) {
            found = 1;
            break;
        }
        d = (int8_t)block_dist[by + bx * BH];
        head = (head + 1) & 0xFF;
        conn = (int8_t)links(sea)[by + bx * BH];
        for (k = 1; k <= 8; k++) {
            if (!(conn & (1 << (k - 1))))
                continue;
            nbx = around_dx[k] + bx;
            if (nbx == BW)
                nbx = 0;
            if (nbx == -1)
                nbx = BW - 1;
            nby = around_dy[k] + by;
            if (block_dist[nbx * BH + nby] != 0)
                continue;
            block_dist[nbx * BH + nby] = (uint8_t)(d + 1);
            queue_x[tail] = (int8_t)nbx;
            queue_y[tail] = (int8_t)nby;
            tail = (tail + 1) & 0xFF;
        }
        if (head == tail)
            break;
    }
    target_x = -1;
    if (found) {
        best_d = 99;
        best_k = -1;
        conn = (int8_t)links(sea)[sby + sbx * BH];
        for (k = 1; k <= 8; k++) {
            if (!(conn & (1 << (k - 1))))
                continue;
            nbx = around_dx[k] + sbx;
            nby = around_dy[k] + sby;
            if (nbx == BW)
                nbx = 0;
            if (nbx == -1)
                nbx = BW - 1;
            d = (int8_t)block_dist[nby + nbx * BH];
            if (d == 0)
                continue;
            if (d < best_d) {
                best_d = d;
                best_k = k;
                near = map_distance(p->goto_x, p->goto_y, nbx * 4 + 1, nby * 4 + 1);
            } else if (d == best_d) {
                r = map_distance(p->goto_x, p->goto_y, nbx * 4 + 1, nby * 4 + 1);
                if (r < near) {
                    best_k = k;
                    near = r;
                }
            }
        }
        if (best_k != -1) {
            target_x = wrap_x((around_dx[best_k] + sbx) * 4 + 1);
            target_y = (around_dy[best_k] + sby) * 4 + 1;
            if (is_sea(target_x, target_y) != sea) {
                target_x++;
                if (is_sea(target_x, target_y) != sea)
                    target_y++;
            }
        }
    }
    if (target_x == -1) {
        target_x = p->goto_x;
        target_y = p->goto_y;
    }
    return found;
}

/* f_1e4a_0004: the next step for a unit on its way to (goto_x, goto_y): a direction,
 * 0 for nothing, or -1 when it is there (or stuck). */
int goto_step(int civ, int u)
{
    Unit *p = &game.s.units[civ][u];
    int dx, dy, k, near = 0, r, best, best_cost, cost, metric, rx, ry, nx, ny, t, o, zoc_here, road_here, d;
    const UnitType *ut = &game.s.unit_types[p->type];

    if (civ == game.s.human_civ) {
        dx = p->goto_x - p->x;
        dy = p->goto_y - p->y;
        if (abs(dx) <= 1 && abs(dy) <= 1) {
            dx = abs(dx) < 40 ? sign(dx) : -sign(dx);
            dy = sign(dy);
            p->goto_x = NONE8;
            for (k = 1; k <= 8; k++)
                if (around_dx[k] == dx && around_dy[k] == dy)
                    return k;
            return 0;
        }
    }
    if (ut->domain == DOMAIN_AIR) {
        target_x = p->goto_x;
        target_y = p->goto_y;
    } else {
        if ((abs(p->goto_x - p->x) < 7 || abs(p->goto_x - p->x) > 73) && abs(p->goto_y - p->y) < 7) {
            target_x = p->goto_x;
            target_y = p->goto_y;
            r = fine_path(civ, u, 999);
            if (r != -1)
                return r;
            near = 1;
        }
        if (coarse_path(civ, u) || !near) {
            r = fine_path(civ, u, 999);
            if (r != -1)
                return r;
        }
    }

    /* Head straight for it. */
    dx = target_x - p->x;
    dy = target_y - p->y;
    metric = (abs(dx) > abs(dy) ? abs(dx) : abs(dy)) + abs(dx) + abs(dy);
    if (dx == 0 && dy == 0) {
        p->unk8 = NONE8;
        p->goto_x = NONE8;
        p->moves = 0;
        return -1;
    }
    best_cost = 9999;
    best = 0;
    road_here = map_improvements(p->x, p->y) & IMP_ROAD;
    zoc_here = in_enemy_zone(civ, p->x, p->y);
    for (k = 1; k < 9; k++) {
        nx = p->x + around_dx[k];
        ny = p->y + around_dy[k];
        rx = dx - around_dx[k];
        ry = dy - around_dy[k];
        cost = (abs(rx) > abs(ry) ? abs(rx) : abs(ry)) + abs(rx) + abs(ry);
        if (civ == game.s.human_civ && cost > metric)
            continue;
        t = map_terrain(nx, ny);
        o = map_unit_owner(nx, ny);
        if (o == -1 || o == civ) {
            if ((t == TERRAIN_OCEAN) == (ut->domain == DOMAIN_SEA)) {
                if (zoc_here && in_enemy_zone(civ, nx, ny))
                    goto check_air;
                goto ok;
            }
        check_air:
            if (ut->domain == DOMAIN_AIR)
                goto ok;
        }
        if (!(map_improvements(nx, ny) & IMP_CITY) || map_unit_layer_owner(nx, ny) != civ)
            continue;
    ok:
        if (t == TERRAIN_OCEAN && body_area(nx, ny) < 5)
            continue;
        if (road_here && (map_improvements(nx, ny) & IMP_ROAD))
            d = 1;
        else
            d = ut->moves > 1 ? terrain_types[t].move_cost * 3 : 3;
        d += cost * 4 + abs(rx) + abs(ry);
        if (p->unk8 != NONE8) {
            r = abs((int8_t)p->unk8 - k);
            if (r > 4)
                r = 8 - r;
            d += r * r;
        }
        if (d < best_cost) {
            best = k;
            best_cost = d;
        }
    }
    if (p->unk8 != NONE8 && ((int8_t)p->unk8 ^ 4) == best) {
        p->moves = 0;
        best = 0;
    }
    if (best == 0) {
        p->unk8 = NONE8;
        p->goto_x = NONE8;
        return -1;
    }
    p->unk8 = (uint8_t)best;
    return best;
}

void goto_reset(void)
{
    memset(sea_links, 0, sizeof sea_links);
    memset(fine_dist, 0, sizeof fine_dist);
    memset(block_dist, 0, sizeof block_dist);
    fine_cx = fine_cy = 0;
    fine_len = 0;
    target_x = target_y = 0;
}
