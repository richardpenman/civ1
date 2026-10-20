#ifndef SOUND_H
#define SOUND_H
int  sound_init(void);          /* 0 if audio is available */
void sound_play(int n);
int  sound_playing(void);        /* 1 while a sound is still playing */         /* f_01a7_046e: sound n of the driver; 0 stops */
#endif
