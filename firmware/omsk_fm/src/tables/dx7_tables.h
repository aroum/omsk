#ifndef DX7_TABLES_H
#define DX7_TABLES_H

#include <stdint.h>
#include <stddef.h>

#define FM_LOGSIN_BITS 10u
#define FM_LOGSIN_SIZE (1u << FM_LOGSIN_BITS)
#define FM_EXP_LUT_SIZE 4096u
#define FM_MAX_ATTENUATION (FM_EXP_LUT_SIZE - 1u)
#define FM_DX7_SIN_LG_N_SAMPLES 10u
#define FM_DX7_SIN_N_SAMPLES (1u << FM_DX7_SIN_LG_N_SAMPLES)
#define FM_DX7_EXP2_LG_N_SAMPLES 10u
#define FM_DX7_EXP2_N_SAMPLES (1u << FM_DX7_EXP2_LG_N_SAMPLES)
#define FM_NOTE_COUNT 128u

extern uint16_t g_logsin_lut[FM_LOGSIN_SIZE];
extern int16_t g_exp_lut[FM_EXP_LUT_SIZE];
extern int32_t g_dx7_sin_lut[FM_DX7_SIN_N_SAMPLES << 1];
extern int32_t g_dx7_exp2_lut[FM_DX7_EXP2_N_SAMPLES << 1];
extern uint32_t g_note_phase_step[FM_NOTE_COUNT];

extern int32_t k_dx7_freqlut[1025];
extern int32_t g_porta_rates[128];
extern int32_t g_porta_rates_glissando[128];

extern const int k_dx7_level_lut[20];
extern const uint8_t k_dx7_velocity_lut[64];
extern const uint8_t k_dx7_exp_scale_lut[33];
extern const int32_t k_dx7_statics_lut[77];
extern const int32_t k_coarsemul_lut[32];
extern const uint8_t k_pitchenv_rate[100];
extern const int8_t k_pitchenv_tab[100];
extern const float k_lfo_source_lut[100];

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

struct OpStep {
  uint8_t sel;
  bool a;
  bool c;
  bool d;
  uint8_t log2_com;
};

extern const OpStep k_ym21280_steps[32][6];
extern const uint16_t k_frac_mul[8];

void dx7_tables_init(double sample_rate);

#endif // DX7_TABLES_H
