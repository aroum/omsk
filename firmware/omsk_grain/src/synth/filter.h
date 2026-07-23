#ifndef FILTER_H
#define FILTER_H

class StateVariableFilter {
public:
    StateVariableFilter();

    // mode_idx: 0=LP, 1=HP, 2=BP, 3=Off
    float process(float v0, float cutoff, float res, int mode_idx, float sample_rate);

private:
    float ic1eq;
    float ic2eq;

    // Cached parameters to avoid recalculating when unchanged
    float cached_cutoff;
    float cached_res;
    float cached_sample_rate;
    float a1, a2, a3, k;

    void update_coefs(float cutoff, float res, float sample_rate);
};

#endif // FILTER_H
