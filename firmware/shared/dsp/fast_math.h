#ifndef SHARED_FAST_MATH_H
#define SHARED_FAST_MATH_H

#ifdef __cplusplus
extern "C" {
#endif

// Fast approximation of 2^x (equivalent to powf(2.0f, x) or exp2f(x))
// Extremely fast, uses a 513-point lookup table with linear interpolation and float bit manipulation.
float fast_exp2(float x);

#ifdef __cplusplus
}
#endif

#endif // SHARED_FAST_MATH_H
