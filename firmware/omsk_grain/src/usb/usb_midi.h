#ifndef USB_MIDI_H
#define USB_MIDI_H

#ifdef __cplusplus
extern "C" {
#endif

void usb_midi_init(void);
void usb_midi_task(void);

#ifdef __cplusplus
}
#endif

#endif // USB_MIDI_H
