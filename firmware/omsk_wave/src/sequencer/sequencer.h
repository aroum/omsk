#ifndef SEQUENCER_H
#define SEQUENCER_H

#include <stdint.h>
#include <stdbool.h>
#include "../synth/synth_defs.h"

#define SEQ_MAX_STEPS 64
#define SEQ_MAX_NOTES_PER_STEP 4
#define SEQ_MAX_SEQUENCES 16

typedef enum {
    SEQ_SPEED_DIV_16 = 0, // 1/16
    SEQ_SPEED_DIV_8,      // 1/8
    SEQ_SPEED_DIV_4,      // 1/4
    SEQ_SPEED_DIV_2,      // 1/2
    SEQ_SPEED_1X,         // 1x
    SEQ_SPEED_2X,         // 2x
    SEQ_SPEED_4X          // 4x
} SeqSpeed;

typedef enum {
    SEQ_MODE_FWD = 0,
    SEQ_MODE_BWD,
    SEQ_MODE_PINGPONG,
    SEQ_MODE_SNAKE,
    SEQ_MODE_RANDOM,
    SEQ_MODE_DRUNK,
    SEQ_MODE_COUNT
} SeqMode;

typedef struct {
    uint8_t note;       // MIDI Note Number (0-127)
    uint8_t velocity;   // Velocity (0-127)
    uint8_t probability; // 0-100%
    uint8_t enabled;     // Note on/off
} SeqNote;

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    SeqNote notes[SEQ_MAX_NOTES_PER_STEP];
    bool stop_flag;      // Sequence wraps to 0 if this flag is reached
    uint8_t gate_length; // 0-127 (0-100% of step)
    uint8_t loop_every;  // 1-8 (play every X loops)
    uint8_t loop_count;  // internal counter for loop_every
    uint8_t chord_mode;  // ChordMode enum
} SeqStep;

typedef struct {
    SeqStep steps[SEQ_MAX_STEPS];
    uint8_t bpm;
    SeqSpeed speed;
    SeqMode play_mode;
    uint8_t swing;       // 0-75%
    bool retrigger;      // True = Reset EG
} Sequence;

typedef struct {
    Sequence sequences[SEQ_MAX_SEQUENCES];
    uint8_t current_sequence_idx;
    bool is_playing;
    uint8_t current_step;
    uint8_t current_page; // 0-3 (16 steps per page)
    int8_t direction;    // For Pingpong (1 or -1)
    
    // Playback state
    uint32_t last_tick_time;
    float accumulator;
} SequencerState;

extern SequencerState seq_state;
extern Sequence current_seq;

// Core Functions
void seq_init(void);
void seq_poll(void); 
void seq_process_midi_byte(uint8_t byte);

void seq_start(void);
void seq_stop(void);
void seq_continue(void);
void seq_set_bpm(uint8_t bpm);
uint8_t seq_get_bpm(void);
void seq_set_speed(SeqSpeed speed);
void seq_set_retrigger(bool retrigger);

// Editor Functions
void seq_set_step_note(uint8_t step, uint8_t note_idx, uint8_t note, uint8_t velocity, uint8_t probability, bool enabled);
void seq_set_step_stop(uint8_t step, bool stop);
void seq_set_step_gate(uint8_t step, uint8_t gate);
void seq_select_sequence(uint8_t idx);
void seq_save_current(uint8_t idx);
void seq_load_sequence(uint8_t idx);
void seq_import_preset(uint8_t preset_idx);

// Playback Logic
void seq_process(void);

#ifdef __cplusplus
}
#endif

#endif // SEQUENCER_H
