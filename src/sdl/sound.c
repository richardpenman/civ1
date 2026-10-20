/* The PC speaker: the sounds of ISOUND.CVL (recorded by tools/extract_sound.py) played
 * back as the square wave the speaker made. */
#include <SDL2/SDL.h>
#include "sound.h"
#include "sound_data.h"

#define RATE 44100
#define PIT 1193182.0

static SDL_AudioDeviceID dev;
static const SoundRun *cur;     /* the run being played */
static long run_left;           /* samples left in it */
static double phase;

static void start_run(void)
{
    run_left = cur && cur->length ? (long)cur->length * RATE / 300 : 0;
}

static void callback(void *user, Uint8 *stream, int len)
{
    Sint16 *out = (Sint16 *)stream;
    int i, n = len / 2;

    (void)user;
    for (i = 0; i < n; i++) {
        while (cur && cur->length && run_left <= 0) {
            cur++;
            start_run();
        }
        if (!cur || !cur->length) {
            out[i] = 0;
            continue;
        }
        if (cur->divisor) {
            phase += (PIT / cur->divisor) / RATE;
            if (phase >= 1.0)
                phase -= (int)phase;
            out[i] = phase < 0.5 ? 2500 : -2500;
        } else
            out[i] = 0;
        run_left--;
    }
}

int sound_init(void)
{
    SDL_AudioSpec want, have;

    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0)
        return -1;
    SDL_zero(want);
    want.freq = RATE;
    want.format = AUDIO_S16SYS;
    want.channels = 1;
    want.samples = 1024;
    want.callback = callback;
    dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (!dev)
        return -1;
    SDL_PauseAudioDevice(dev, 0);
    return 0;
}

void sound_play(int n)
{
    if (!dev)
        return;
    SDL_LockAudioDevice(dev);
    cur = n > 0 && n < NUM_SOUNDS ? speaker_sounds[n] : NULL;
    phase = 0;
    start_run();
    SDL_UnlockAudioDevice(dev);
}

int sound_playing(void)
{
    int r;

    if (!dev)
        return 0;
    SDL_LockAudioDevice(dev);
    r = cur && cur->length;
    SDL_UnlockAudioDevice(dev);
    return r;
}
