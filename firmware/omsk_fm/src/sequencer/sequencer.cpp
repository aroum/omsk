#include "sequencer.h"
#include <string.h>

SequencerState seq_state;
Sequence current_seq;

void seq_init(void) {
  memset(&seq_state, 0, sizeof(seq_state));
  memset(&current_seq, 0, sizeof(current_seq));
}

void seq_poll(void) {} 
void seq_process_midi_byte(uint8_t byte) { (void)byte; }

void seq_start(void) { seq_state.is_playing = true; }
void seq_stop(void) { seq_state.is_playing = false; }
void seq_continue(void) { seq_state.is_playing = true; }
void seq_set_bpm(uint8_t bpm) { seq_state.sequences[seq_state.current_sequence_idx].bpm = bpm; }
uint8_t seq_get_bpm(void) { return seq_state.sequences[seq_state.current_sequence_idx].bpm; }
void seq_set_speed(SeqSpeed speed) { seq_state.sequences[seq_state.current_sequence_idx].speed = speed; }
void seq_set_retrigger(bool retrigger) { seq_state.sequences[seq_state.current_sequence_idx].retrigger = retrigger; }

void seq_set_step_note(uint8_t step, uint8_t note_idx, uint8_t note, uint8_t velocity, uint8_t probability, bool enabled) {
  if (step < SEQ_MAX_STEPS && note_idx < SEQ_MAX_NOTES_PER_STEP) {
    current_seq.steps[step].notes[note_idx].note = note;
    current_seq.steps[step].notes[note_idx].velocity = velocity;
    current_seq.steps[step].notes[note_idx].probability = probability;
    current_seq.steps[step].notes[note_idx].enabled = enabled;
  }
}
void seq_set_step_stop(uint8_t step, bool stop) {
  if (step < SEQ_MAX_STEPS) current_seq.steps[step].stop_flag = stop;
}
void seq_set_step_gate(uint8_t step, uint8_t gate) {
  if (step < SEQ_MAX_STEPS) current_seq.steps[step].gate_length = gate;
}
void seq_select_sequence(uint8_t idx) { if (idx < SEQ_MAX_SEQUENCES) seq_state.current_sequence_idx = idx; }
void seq_save_current(uint8_t idx) { (void)idx; }
void seq_load_sequence(uint8_t idx) { (void)idx; }
void seq_import_preset(uint8_t preset_idx) { (void)preset_idx; }
void seq_process(void) {}
