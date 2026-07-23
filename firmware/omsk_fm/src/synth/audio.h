#ifndef AUDIO_H
#define AUDIO_H

#include <stdint.h>

#define AUDIO_SAMPLE_RATE 48000
#define AUDIO_BUFFER_SIZE 256

void audio_init(void);
void audio_start(void);

#endif // AUDIO_H
