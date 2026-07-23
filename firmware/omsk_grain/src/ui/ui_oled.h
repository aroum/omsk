#ifndef UI_OLED_H
#define UI_OLED_H

#include <stdint.h>
#include "ui_state.h"
#include "../synth/audio_engine.h"

// Forward declarations for u8g2
extern "C" {
#include "u8g2.h"
}

void oled_init(u8g2_t* u8g2);
void oled_render(u8g2_t* u8g2, const UIState* state, GranularEngine* voices, int num_voices);

// Call when any encoder moves — switches display from grain view back to param view
void oled_notify_encoder_activity();
// Call when any trigger/note is pressed — switches display to grain view
void oled_notify_trigger_activity();

#endif // UI_OLED_H
