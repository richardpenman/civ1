/* The original's sprite sheets (SP257.PIC, TER257.PIC, SP299.PIC) and palette. */
#ifndef ASSETS_H
#define ASSETS_H

#include "../pic.h"

typedef struct {
    const Picture *pic;
    short x, y, w, h;
} Sprite;

extern Picture sp257, ter257, sp299;
extern uint8_t game_palette[256][3];
extern Picture unit_sheet[8];        /* SP257 rows 160..191 recoloured for each civ */

extern const short civ_colour[16];   /* g_1946: main colour of each civ */
extern const short civ_colour2[16];  /* g_1956: second colour */

int  assets_load(void);
void era_palette(void);                /* f_0237_1be7 */
void sprite_draw(Sprite s, int x, int y);          /* colour 0 is transparent */
void sprite_copy(Sprite s, int x, int y);          /* opaque */
Sprite spr(const Picture *p, int x, int y, int w, int h);
Sprite unit_sprite(int civ, int type);
Sprite terrain_sprite(int terrain, int mask);
Sprite icon_sprite(int n);                         /* f_1dde_0098's table (g_e3ee) */
int  load_picture(const char *name, Picture *p);   /* from data_dir */

#endif
