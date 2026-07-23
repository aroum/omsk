#ifndef GRAIN_H
#define GRAIN_H

#include <stdint.h>
#include <stdbool.h>

class Grain {
public:
    Grain();

    void init(int sample_id, float start_idx, float length, float pitch, float pan, bool reverse, int shape_idx, float amp);
    
    // Process one sample. Returns true if grain is still active, false if finished.
    // out_l and out_r are accumulators (adds to them).
    bool process(float fm_shift, float* out_l, float* out_r);

    bool active;
    int sample_id;
    float start_idx;
    float length;
    float pitch;
    float pan;
    bool reverse;
    float amp;
    int shape_idx;
    float current_frame;
    float current_idx;
};

#endif // GRAIN_H
