#ifndef UI_OLED_H
#define UI_OLED_H

#include <stdint.h>
#include <stdbool.h>

typedef struct
{
    const char *title;
    uint8_t value;
    const char *mod_label;
    uint8_t mod_amount;
    char value_str[16];
} OledKnob;

typedef struct
{
    OledKnob knobs[4];
    uint8_t layout_id;
} OledPage;

void ui_oled_init(void);
void ui_oled_draw(const OledPage *page);
void ui_oled_set_power(bool on);
void ui_oled_set_brightness(uint8_t percentage);

#endif // UI_OLED_H