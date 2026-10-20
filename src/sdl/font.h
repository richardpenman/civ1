/* The original's fonts (FONTS.CV). */
#ifndef FONT_H
#define FONT_H

int  font_load(const char *path);
int  font_height(int font);
int  text_width(int font, const char *s);
/* Draws up to the end of the line; returns the x after the text. */
int  draw_text(int font, int x, int y, int colour, const char *s);
void draw_text_centered(int font, int cx, int y, int colour, const char *s);
int  text_wrapped(char *t, int font, int chars, int x, int y, int colour);   /* f_1f65_008e */

#endif
