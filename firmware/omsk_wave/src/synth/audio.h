#ifndef AUDIO_H
#define AUDIO_H

#include <stdint.h>

// Audio settings
#define AUDIO_SAMPLE_RATE 48000
#define AUDIO_BUFFER_SIZE 256

void audio_init(void);
void usb_audio_init(void);
void audio_start(void);
void audio_usb_task(void);

#endif
