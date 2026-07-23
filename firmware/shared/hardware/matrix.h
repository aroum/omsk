#ifndef SHARED_MATRIX_H
#define SHARED_MATRIX_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Array containing current state of 16 matrix buttons
extern bool matrix_curr[16];
extern bool matrix_debounced[16];

#include "../hw_config.h"

// Initializes GPIO pins for the matrix
void init_matrix(void);

// Scans the matrix. Should be called frequently in the main loop.
void scan_matrix(void);

#ifdef __cplusplus
}
#endif

#endif // SHARED_MATRIX_H
