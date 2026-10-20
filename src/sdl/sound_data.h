#ifndef SOUND_DATA_H
#define SOUND_DATA_H
#define NUM_SOUNDS 45
typedef struct { unsigned short divisor, length; } SoundRun;
extern const SoundRun *const speaker_sounds[NUM_SOUNDS];
#endif
