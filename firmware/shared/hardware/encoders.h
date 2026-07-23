#ifndef SHARED_ENCODERS_H
#define SHARED_ENCODERS_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Array holding current relative position of encoders
extern int8_t enc_positions[4];

// Initializes GPIO pins and states for the encoders
void init_encoders(void);

// Scans the encoders. Should be called frequently (e.g. 1ms timer).
void scan_encoders(void);

// Gets the accumulated delta for an encoder and clears it
int encoders_get_delta(int index);

#ifdef __cplusplus
}
#endif

#endif // SHARED_ENCODERS_H
