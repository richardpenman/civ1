/* Advances and research. */
#ifndef SCIENCE_H
#define SCIENCE_H

void give_advance(int civ, int advance, int from);   /* from: civ, 0 itself, -2 Great Library */

int  research_complete(int civ, int from);         /* f_0ad5_1569 */

int  advance_value(int civ, int advance);           /* f_0ad5_22f5 */

#endif
