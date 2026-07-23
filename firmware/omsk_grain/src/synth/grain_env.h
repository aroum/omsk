#ifndef GRAIN_ENV_H
#define GRAIN_ENV_H

#include <stdint.h>
#include "../tables/grain_env_lut.h"

class GrainEnvelope {
public:
    // val: 0.0 to (NUM_GRAIN_SHAPES - 1)
    // x: 0.0 to 1.0 (normalized time)
    // Returns amplitude 0.0 to 1.0
    static float get_morphed_envelope(float val, float x);
};

#endif // GRAIN_ENV_H
