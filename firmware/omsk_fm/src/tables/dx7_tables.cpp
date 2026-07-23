#include "dx7_tables.h"
#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

uint16_t g_logsin_lut[FM_LOGSIN_SIZE];
int16_t g_exp_lut[FM_EXP_LUT_SIZE];
int32_t g_dx7_sin_lut[FM_DX7_SIN_N_SAMPLES << 1];
int32_t g_dx7_exp2_lut[FM_DX7_EXP2_N_SAMPLES << 1];
uint32_t g_note_phase_step[FM_NOTE_COUNT];

int32_t k_dx7_freqlut[1025];
int32_t g_porta_rates[128];
int32_t g_porta_rates_glissando[128];

const int k_dx7_level_lut[20] = {
    0,  5,  9,  13, 17, 20, 23, 25, 27, 29,
    31, 33, 35, 37, 39, 41, 42, 43, 45, 46,
};

const uint8_t k_dx7_velocity_lut[64] = {
    0,   70,  86,  97,  106, 114, 121, 126, 132, 138, 142, 148, 152,
    156, 160, 163, 166, 170, 173, 174, 178, 181, 184, 186, 189, 190,
    194, 196, 198, 200, 202, 205, 206, 209, 211, 214, 216, 218, 220,
    222, 224, 225, 227, 229, 230, 232, 233, 235, 237, 238, 240, 241,
    242, 243, 244, 246, 246, 248, 249, 250, 251, 252, 253, 254,
};

const uint8_t k_dx7_exp_scale_lut[33] = {
    0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  11, 14, 16, 19, 23, 27, 33,
    39, 47, 56, 66, 80, 94, 110, 126, 142, 158, 174, 190, 206, 222, 238,
    250,
};

const int32_t k_dx7_statics_lut[77] = {
    1764000, 1764000, 1411200, 1411200, 1190700, 1014300, 992250,
    882000, 705600, 705600, 584325, 507150, 502740, 441000, 418950,
    352800, 308700, 286650, 253575, 220500, 220500, 176400, 145530,
    145530, 125685, 110250, 110250, 88200, 88200, 74970, 61740,
    61740, 55125, 48510, 44100, 37485, 31311, 30870, 27562, 27562,
    22050, 18522, 17640, 15435, 14112, 13230, 11025, 9261, 9261, 7717,
    6615, 6615, 5512, 5512, 4410, 3969, 3969, 3439, 2866, 2690, 2249,
    1984, 1896, 1808, 1411, 1367, 1234, 1146, 926, 837, 837, 705,
    573, 573, 529, 441, 441
};

const int32_t k_coarsemul_lut[32] = {
    -16777216, 0, 16777216, 26591258, 33554432, 38955489, 43368474, 47099600,
    50331648, 53182516, 55732705, 58039632, 60145690, 62083076, 63876816,
    65546747, 67108864, 68576247, 69959732, 71268397, 72509921, 73690858,
    74816848, 75892776, 76922906, 77910978, 78860292, 79773775, 80654032,
    81503396, 82323963, 83117622
};

const uint8_t k_pitchenv_rate[100] = {
  1, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12,
  12, 13, 13, 14, 14, 15, 16, 16, 17, 18, 18, 19, 20, 21, 22, 23, 24,
  25, 26, 27, 28, 30, 31, 33, 34, 36, 37, 38, 39, 41, 42, 44, 46, 47,
  49, 51, 53, 54, 56, 58, 60, 62, 64, 66, 68, 70, 72, 74, 76, 79, 82,
  85, 88, 91, 94, 98, 102, 106, 110, 115, 120, 125, 130, 135, 141, 147,
  153, 159, 165, 171, 178, 185, 193, 202, 211, 232, 243, 254, 255
};

const int8_t k_pitchenv_tab[100] = {
  -128, -116, -104, -95, -85, -76, -68, -61, -56, -52, -49, -46, -43,
  -41, -39, -37, -35, -33, -32, -31, -30, -29, -28, -27, -26, -25, -24,
  -23, -22, -21, -20, -19, -18, -17, -16, -15, -14, -13, -12, -11, -10,
  -9, -8, -7, -6, -5, -4, -3, -2, -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10,
  11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27,
  28, 29, 30, 31, 32, 33, 34, 35, 38, 40, 43, 46, 49, 53, 58, 65, 73,
  82, 92, 103, 115, 127
};

const float k_lfo_source_lut[100] = {
    0.062541f, 0.125031f, 0.312393f, 0.437120f, 0.624610f,
    0.750694f, 0.936330f, 1.125302f, 1.249609f, 1.436782f,
    1.560915f, 1.752081f, 1.875117f, 2.062494f, 2.247191f,
    2.374451f, 2.560492f, 2.686728f, 2.873976f, 2.998950f,
    3.188013f, 3.369840f, 3.500175f, 3.682224f, 3.812065f,
    4.000800f, 4.186202f, 4.310716f, 4.501260f, 4.623209f,
    4.814636f, 4.930480f, 5.121901f, 5.315191f, 5.434783f,
    5.617346f, 5.750431f, 5.946717f, 6.062811f, 6.248438f,
    6.431695f, 6.564264f, 6.749460f, 6.868132f, 7.052186f,
    7.250580f, 7.375719f, 7.556294f, 7.687577f, 7.877738f,
    7.993605f, 8.181967f, 8.372405f, 8.504848f, 8.685079f,
    8.810573f, 8.986341f, 9.122423f, 9.300595f, 9.500285f,
    9.607994f, 9.798158f, 9.950249f, 10.117361f, 11.251125f,
    11.384335f, 12.562814f, 13.676149f, 13.904338f, 15.092062f,
    16.366612f, 16.638935f, 17.869907f, 19.193858f, 19.425019f,
    20.833333f, 21.034918f, 22.502250f, 24.003841f, 24.260068f,
    25.746653f, 27.173913f, 27.578599f, 29.052876f, 30.693677f,
    31.191516f, 32.658393f, 34.317090f, 34.674064f, 36.416606f,
    38.197097f, 38.550501f, 40.387722f, 40.749796f, 42.625746f,
    44.326241f, 44.883303f, 46.772685f, 48.590865f, 49.261084f
};

void dx7_tables_init(double sample_rate) {
  // Generate Luts
  for (uint32_t i = 0; i < FM_LOGSIN_SIZE; ++i) {
    if (i == 0u) {
      g_logsin_lut[i] = FM_MAX_ATTENUATION;
      continue;
    }
    const double angle = ((double)i * (M_PI * 0.5)) / (double)FM_LOGSIN_SIZE;
    double attenuation = -256.0 * log2(sin(angle));
    if (attenuation > (double)FM_MAX_ATTENUATION) attenuation = (double)FM_MAX_ATTENUATION;
    g_logsin_lut[i] = (uint16_t)lround(attenuation);
  }

  for (uint32_t i = 0; i < FM_EXP_LUT_SIZE; ++i) {
    g_exp_lut[i] = (int16_t)lround(32767.0 * exp2(-(double)i / 256.0));
  }

  for (uint32_t i = 0; i < (FM_DX7_SIN_N_SAMPLES / 2u); ++i) {
    double val = sin(((double)i * (2.0 * M_PI)) / (double)FM_DX7_SIN_N_SAMPLES);
    g_dx7_sin_lut[(i << 1) + 1] = (int32_t)floor(val * (double)(1 << 24) + 0.5);
    g_dx7_sin_lut[((i + (FM_DX7_SIN_N_SAMPLES / 2u)) << 1) + 1] = -g_dx7_sin_lut[(i << 1) + 1];
  }
  for (uint32_t i = 0; i < FM_DX7_SIN_N_SAMPLES - 1u; ++i) {
    g_dx7_sin_lut[i << 1] = g_dx7_sin_lut[(i << 1) + 3] - g_dx7_sin_lut[(i << 1) + 1];
  }
  g_dx7_sin_lut[(FM_DX7_SIN_N_SAMPLES << 1) - 2] = -g_dx7_sin_lut[(FM_DX7_SIN_N_SAMPLES << 1) - 1];

  for (uint32_t i = 0; i < FM_DX7_EXP2_N_SAMPLES; ++i) {
    g_dx7_exp2_lut[(i << 1) + 1] = (int32_t)floor(exp2((double)i / (double)FM_DX7_EXP2_N_SAMPLES) * (double)(1 << 30) + 0.5);
  }
  for (uint32_t i = 0; i < FM_DX7_EXP2_N_SAMPLES - 1u; ++i) {
    g_dx7_exp2_lut[i << 1] = g_dx7_exp2_lut[(i << 1) + 3] - g_dx7_exp2_lut[(i << 1) + 1];
  }
  g_dx7_exp2_lut[(FM_DX7_EXP2_N_SAMPLES << 1) - 2] = INT32_MAX - g_dx7_exp2_lut[(FM_DX7_EXP2_N_SAMPLES << 1) - 1];

  for (uint32_t note = 0; note < FM_NOTE_COUNT; ++note) {
    g_note_phase_step[note] = (uint32_t)llround((440.0 * exp2(((double)((int)note - 69)) / 12.0)) * 16777216.0 / sample_rate);
  }

  // k_dx7_freqlut
  double y = (double)(1LL << 44) / sample_rate;
  double inc = pow(2.0, 1.0 / 1024.0);
  for (int i = 0; i < 1025; i++) {
    k_dx7_freqlut[i] = (int32_t)floor(y + 0.5);
    y *= inc;
  }

  // g_porta_rates
  const int32_t step = (1 << 24) / 12; // 1398101
  for (int i = 0; i < 128; ++i) {
    double sps = 2100.0 * pow(2.0, -0.062 * i);
    double spf = sps / sample_rate;
    double spp = spf * 32.0;
    g_porta_rates[i] = (int32_t)(0.5 + step * spp);

    sps = 1300.0 * pow(2.0, -0.062 * i);
    spf = sps / sample_rate;
    spp = spf * 32.0;
    g_porta_rates_glissando[i] = (int32_t)(0.5 + step * spp);
  }
}

//------------------------------------------------------------------------------
// The YM21280 steps data is a derivative of data reverse engineered from die
// photographs of the YM21280 published in Ken Shirriff's blog. That publicly
// available document does not contain a copyright notice or any mention of
// permissions or restrictions that apply to the use of the data. However,
// according to law that does not necessarily mean that the data is free from
// restrictions on its use. At the time of publication, the copyright holder
// is probably Yamaha. If the copyright holder wishes to retrospectively declare
// reasonable and legal restrictions on the data, then either those restrictions
// must be obeyed or this file shouled be deleted.
//
// The statements above shall be included in all copies or substantial portions
// of the data.
//
// THE DATA IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE DATA OR THE USE OR OTHER DEALINGS IN THE
// DATA.
//------------------------------------------------------------------------------

const OpStep k_ym21280_steps[32][6] = {
  // OP6, OP5, OP4, OP3, OP2, OP1
  // Alg 1
  {{1, true, false, false, 0x00}, {1, false, false, false, 0x00}, {1, false, false, false, 0x00}, {0, false, false, true, 0x08}, {1, false, true, false, 0x00}, {5, false, true, true, 0x08}},
  // Alg 2
  {{1, false, false, false, 0x00}, {1, false, false, false, 0x00}, {1, false, false, false, 0x00}, {5, false, false, true, 0x08}, {1, true, true, false, 0x00}, {0, false, true, true, 0x08}},
  // Alg 3
  {{1, true, false, false, 0x00}, {1, false, false, false, 0x00}, {0, false, false, true, 0x08}, {1, false, true, false, 0x00}, {1, false, true, false, 0x00}, {5, false, true, true, 0x08}},
  // Alg 4
  {{1, false, false, false, 0x00}, {1, false, false, false, 0x00}, {0, true, false, true, 0x08}, {1, false, true, false, 0x00}, {1, false, true, false, 0x00}, {5, false, true, true, 0x08}},
  // Alg 5
  {{1, true, false, false, 0x00}, {0, false, false, true, 0x0d}, {1, false, true, false, 0x00}, {0, false, true, true, 0x0d}, {1, false, true, false, 0x00}, {5, false, true, true, 0x0d}},
  // Alg 6
  {{1, false, false, false, 0x00}, {0, true, false, true, 0x0d}, {1, false, true, false, 0x00}, {0, false, true, true, 0x0d}, {1, false, true, false, 0x00}, {5, false, true, true, 0x0d}},
  // Alg 7
  {{1, true, false, false, 0x00}, {0, false, false, true, 0x00}, {2, false, true, true, 0x00}, {0, false, false, true, 0x08}, {1, false, true, false, 0x00}, {5, false, true, true, 0x08}},
  // Alg 8
  {{1, false, false, false, 0x00}, {5, false, false, true, 0x00}, {2, true, true, true, 0x00}, {0, false, false, true, 0x08}, {1, false, true, false, 0x00}, {0, false, true, true, 0x08}},
  // Alg 9
  {{1, false, false, false, 0x00}, {0, false, false, true, 0x00}, {2, false, true, true, 0x00}, {5, false, false, true, 0x08}, {1, true, true, false, 0x00}, {0, false, true, true, 0x08}},
  // Alg 10
  {{0, false, false, true, 0x00}, {2, false, true, true, 0x00}, {5, false, false, true, 0x08}, {1, true, true, false, 0x00}, {1, false, true, false, 0x00}, {0, false, true, true, 0x08}},
  // Alg 11
  {{0, true, false, true, 0x00}, {2, false, true, true, 0x00}, {0, false, false, true, 0x08}, {1, false, true, false, 0x00}, {1, false, true, false, 0x00}, {5, false, true, true, 0x08}},
  // Alg 12
  {{0, false, false, true, 0x00}, {0, false, true, true, 0x00}, {2, false, true, true, 0x00}, {5, false, false, true, 0x08}, {1, true, true, false, 0x00}, {0, false, true, true, 0x08}},
  // Alg 13
  {{0, true, false, true, 0x00}, {0, false, true, true, 0x00}, {2, false, true, true, 0x00}, {0, false, false, true, 0x08}, {1, false, true, false, 0x00}, {5, false, true, true, 0x08}},
  // Alg 14
  {{0, true, false, true, 0x00}, {2, false, true, true, 0x00}, {1, false, false, false, 0x00}, {0, false, false, true, 0x08}, {1, false, true, false, 0x00}, {5, false, true, true, 0x08}},
  // Alg 15
  {{0, false, false, true, 0x00}, {2, false, true, true, 0x00}, {1, false, false, false, 0x00}, {5, false, false, true, 0x08}, {1, true, true, false, 0x00}, {0, false, true, true, 0x08}},
  // Alg 16
  {{1, true, false, false, 0x00}, {0, false, false, true, 0x00}, {1, false, true, false, 0x00}, {0, false, true, true, 0x00}, {2, false, true, true, 0x00}, {5, false, false, true, 0x00}},
  // Alg 17
  {{1, false, false, false, 0x00}, {0, false, false, true, 0x00}, {1, false, true, false, 0x00}, {5, false, true, true, 0x00}, {2, true, true, true, 0x00}, {0, false, false, true, 0x00}},
  // Alg 18
  {{1, false, false, false, 0x00}, {1, false, false, false, 0x00}, {5, false, false, true, 0x00}, {0, true, true, true, 0x00}, {2, false, true, true, 0x00}, {0, false, false, true, 0x00}},
  // Alg 19
  {{1, true, false, false, 0x00}, {4, false, false, true, 0x0d}, {0, false, true, true, 0x0d}, {1, false, true, false, 0x00}, {1, false, true, false, 0x00}, {5, false, true, true, 0x0d}},
  // Alg 20
  {{0, false, false, true, 0x00}, {2, false, true, true, 0x00}, {5, false, false, true, 0x0d}, {1, true, true, false, 0x00}, {4, false, true, true, 0x0d}, {0, false, true, true, 0x0d}},
  // Alg 21
  {{1, false, false, true, 0x00}, {3, false, false, true, 0x10}, {5, false, true, true, 0x10}, {1, true, true, false, 0x00}, {4, false, true, true, 0x10}, {0, false, true, true, 0x10}},
  // Alg 22
  {{1, true, false, false, 0x00}, {4, false, false, true, 0x10}, {4, false, true, true, 0x10}, {0, false, true, true, 0x10}, {1, false, true, false, 0x00}, {5, false, true, true, 0x10}},
  // Alg 23
  {{1, true, false, false, 0x00}, {4, false, false, true, 0x10}, {0, false, true, true, 0x10}, {1, false, true, false, 0x00}, {0, false, true, true, 0x10}, {5, false, true, true, 0x10}},
  // Alg 24
  {{1, true, false, false, 0x00}, {4, false, false, true, 0x13}, {4, false, true, true, 0x13}, {0, false, true, true, 0x13}, {0, false, true, true, 0x13}, {5, false, true, true, 0x13}},
  // Alg 25
  {{1, true, false, false, 0x00}, {4, false, false, true, 0x13}, {0, false, true, true, 0x13}, {0, false, true, true, 0x13}, {0, false, true, true, 0x13}, {5, false, true, true, 0x13}},
  // Alg 26
  {{0, true, false, true, 0x00}, {2, false, true, true, 0x00}, {0, false, false, true, 0x0d}, {1, false, true, false, 0x00}, {0, false, true, true, 0x0d}, {5, false, true, true, 0x0d}},
  // Alg 27
  {{0, false, false, true, 0x00}, {2, false, true, true, 0x00}, {5, false, false, true, 0x0d}, {1, true, true, false, 0x00}, {0, false, true, true, 0x0d}, {0, false, true, true, 0x0d}},
  // Alg 28
  {{5, false, false, true, 0x0d}, {1, true, true, false, 0x00}, {1, false, true, false, 0x00}, {0, false, true, true, 0x0d}, {1, false, true, false, 0x00}, {0, false, true, true, 0x0d}},
  // Alg 29
  {{1, true, false, false, 0x00}, {0, false, false, true, 0x10}, {1, false, true, false, 0x00}, {0, false, true, true, 0x10}, {0, false, true, true, 0x10}, {5, false, true, true, 0x10}},
  // Alg 30
  {{5, false, false, true, 0x10}, {1, true, true, false, 0x00}, {1, false, true, false, 0x00}, {0, false, true, true, 0x10}, {0, false, true, true, 0x10}, {0, false, true, true, 0x10}},
  // Alg 31
  {{1, true, false, false, 0x00}, {0, false, false, true, 0x13}, {0, false, true, true, 0x13}, {0, false, true, true, 0x13}, {0, false, true, true, 0x13}, {5, false, true, true, 0x13}},
  // Alg 32
  {{0, true, false, true, 0x15}, {0, false, true, true, 0x15}, {0, false, true, true, 0x15}, {0, false, true, true, 0x15}, {0, false, true, true, 0x15}, {5, false, true, true, 0x15}},
};

const uint16_t k_frac_mul[8] = {16384, 15024, 13781, 12640, 11593, 10633, 9752, 8944};
