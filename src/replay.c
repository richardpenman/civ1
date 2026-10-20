/* The replay log shown at the end of the game (f_0860_24cf).  Each entry is
 * (kind << 4 | turn >> 8), turn & 0xFF, then up to four argument bytes; replay_len is
 * the number of bytes used. */
#include "game.h"
#include "replay.h"

void replay_log(int kind, int nargs, int a, int b, int c, int d)
{
    SaveData *s = &game.s;
    int args[4] = { a, b, c, d }, i;

    if (s->replay_len + nargs + 2 >= (int)sizeof s->replay)
        return;
    s->replay[s->replay_len++] = (uint8_t)((kind << 4) + (s->turn >> 8));
    s->replay[s->replay_len++] = (uint8_t)s->turn;
    for (i = 0; i < nargs && i < 4; i++)
        s->replay[s->replay_len++] = (uint8_t)args[i];
}
