#ifndef DIALOG_H
#define DIALOG_H

#include <stddef.h>
#include "../ui.h"

enum { DLG_ESCAPE = 1, DLG_DARK = 2, DLG_KEEP = 4, DLG_NOSEL = 8, DLG_DRAW = 16 };   /* DRAW (g_ea58): drawn,
                                                         * not waited on */   /* KEEP: leave it drawn; NOSEL
                                                         * (g_e126): no highlight, a key ends it */

extern char text_us[32], text_them[32], text_bucks[16];
extern int dialog_font;
extern int dialog_default;            /* g_2f4a */
extern int dialog_checks;             /* g_e712 */
extern int dialog_texture;            /* g_2f48 */
extern int dialog_advisor;            /* g_2f4e */
extern int dialog_help;               /* g_2f50 */

int  text_section_raw(const char *key, char *out, size_t n);
int  text_section_lines(const char *name, const char *key, char *out, size_t n);   /* f_1f65_01b3 */
const char *text_file_name(int f);
void text_substitute(char *s, size_t n, const char *a, const char *b);
void text_get(const char *text, char *out, size_t n, const char *a, const char *b);
int  dialog(const char *text, int x, int y, int disabled, int flags);
void dialog_box_at(int x, int y, int w, int h, int colour);   /* f_1d1e_097a */
int  text_input(const char *prompt, char *buf, int max);
int  text_field(int x, int y, int len, char *buf);       /* o_23_040d */
int  city_name_prompt(char *buf, int len);              /* o_23_0000 */
int  name_prompt(const char *prompt, char *buf, int len);  /* o_23_00d6, o_23_0173 */
void screen_save(void);
void screen_restore(void);

#endif
