#ifndef ARTICLE_H
#define ARTICLE_H
#include "assets.h"
/* category: 0 advance, 1 building, 2 unit, 3 terrain, 4 concept (named by `concept`) */
int  advance_icon(int adv);
void icon_centre(const Picture *p, int sx, int sy, int w, int h, int cx, int cy);
void article_show(int category, int item, const char *concept, int style);   /* style: the border */
void article_by_key(const char *key);        /* "*NAME" as in the BLURB files */
#endif
