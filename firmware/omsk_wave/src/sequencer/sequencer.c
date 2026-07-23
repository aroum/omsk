#include "sequencer.h"
#include "../synth/synth.h" // For synth_note_on/off, synth_preset_load
#include "pico/stdlib.h"
#include "pico/time.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Standard MIDI Clock is 24 PPQN (Pulses Per Quarter Note)
#define PPQN 24

// Default BPM if no MIDI clock
#define SEQ_DEFAULT_BPM 120
#define SEQ_DEFAULT_SPEED SEQ_SPEED_1X

// Internal state
SequencerState seq_state; // Made non-static for UI access
static Sequence sequences[SEQ_MAX_SEQUENCES];
Sequence current_seq;

// External BPM from MIDI clock (readable globally)
volatile float g_midi_bpm = 120.0f;

// Clock state
static uint32_t last_midi_clock_us = 0;
static uint32_t last_internal_tick_us = 0;
static bool external_clock_active = false;

// Step timing logic
static uint32_t ticks_per_step = 6; // Default 1/16th note (24 / 4)
static uint32_t step_tick_counter = 0;

// Gate length logic
static uint32_t gate_off_tick = 0;
static bool gate_active = false;

// Note tracking for Note Offs
static int8_t active_step_notes[SEQ_MAX_NOTES_PER_STEP]; 

// Snake pattern mapping (0-15 local index within a page)
static const uint8_t snake_map[16] = {0, 1, 2, 3, 7, 6, 5, 4, 8, 9, 10, 11, 15, 14, 13, 12};

static void update_ticks_per_step(void) {
  // Base is 1/16th note = 6 ticks (at 24 PPQN)
  switch (current_seq.speed) {
  case SEQ_SPEED_DIV_16: ticks_per_step = 96; break; // 1 bar
  case SEQ_SPEED_DIV_8:  ticks_per_step = 48; break; // 1/2 note
  case SEQ_SPEED_DIV_4:  ticks_per_step = 24; break; // 1/4 note
  case SEQ_SPEED_DIV_2:  ticks_per_step = 12; break; // 1/8 note
  case SEQ_SPEED_1X:     ticks_per_step = 6;  break; // 1/16 note
  case SEQ_SPEED_2X:     ticks_per_step = 3;  break; // 1/32 note
  case SEQ_SPEED_4X:     ticks_per_step = 1;  break; // 1/96 note (Fastest)
  default: ticks_per_step = 6; break;
  }
}

void seq_init(void) {
  memset(&seq_state, 0, sizeof(SequencerState));
  memset(&sequences, 0, sizeof(sequences));
  memset(&current_seq, 0, sizeof(current_seq));

  current_seq.bpm = SEQ_DEFAULT_BPM;
  current_seq.speed = SEQ_DEFAULT_SPEED;
  current_seq.play_mode = SEQ_MODE_FWD;
  seq_state.current_sequence_idx = 0;
  seq_state.is_playing = false;
  seq_state.current_step = 0;
  seq_state.current_page = 0;
  seq_state.direction = 1;

  update_ticks_per_step();

  current_seq.retrigger = true;
  for (int i = 0; i < SEQ_MAX_STEPS; i++) {
    current_seq.steps[i].stop_flag = false;
    current_seq.steps[i].gate_length = 64; // ~50%
    current_seq.steps[i].loop_every = 1;  // play every loop
    current_seq.steps[i].loop_count = 0;
    current_seq.steps[i].chord_mode = 0;
    for (int j = 0; j < SEQ_MAX_NOTES_PER_STEP; j++) {
      current_seq.steps[i].notes[j].enabled = false;
      current_seq.steps[i].notes[j].probability = 100;
      current_seq.steps[i].notes[j].velocity = 100;
    }
  }

  for (int i = 0; i < SEQ_MAX_NOTES_PER_STEP; i++) {
    active_step_notes[i] = -1;
  }
}

static void all_notes_off(void) {
  for (int i = 0; i < SEQ_MAX_NOTES_PER_STEP; i++) {
    if (active_step_notes[i] != -1) {
      synth_note_off(active_step_notes[i]);
      active_step_notes[i] = -1;
    }
  }
  gate_active = false;
}

static void trigger_step(void) {
  SeqStep *step = &current_seq.steps[seq_state.current_step];

  // 1. Loop Count Check (Every X)
  if (step->loop_every > 1) {
    if (step->loop_count < (step->loop_every - 1)) {
        step->loop_count++;
        return; // Skip this step
    } else {
        step->loop_count = 0;
    }
  }

  uint32_t gate_ticks = (step->gate_length * ticks_per_step) / 127;
  if (gate_ticks == 0 && step->gate_length > 0)
    gate_ticks = 1;

  if (current_seq.retrigger || gate_active) {
    all_notes_off();
  }

  int8_t prev_notes[SEQ_MAX_NOTES_PER_STEP];
  memcpy(prev_notes, active_step_notes, sizeof(prev_notes));

  for (int i = 0; i < SEQ_MAX_NOTES_PER_STEP; i++)
    active_step_notes[i] = -1;

  int note_slot = 0;
  bool any_note_triggered = false;

  for (int i = 0; i < SEQ_MAX_NOTES_PER_STEP; i++) {
    SeqNote *n = &step->notes[i];
    if (n->enabled) {
      bool trigger = false;
      if (n->probability >= 100)
        trigger = true;
      else if (n->probability == 0)
        trigger = false;
      else {
        if ((rand() % 100) < n->probability)
          trigger = true;
      }

      if (trigger) {
        // Handle Chord Mode if set
        if (step->chord_mode > 0) {
            // Internal chord notes usually handled by the synth.
        }
        synth_note_on(n->note, 127);
        active_step_notes[note_slot++] = n->note;
        any_note_triggered = true;
      }
    }
  }

  if (!current_seq.retrigger) {
    for (int i = 0; i < SEQ_MAX_NOTES_PER_STEP; i++) {
      if (prev_notes[i] != -1) {
        synth_note_off(prev_notes[i]);
      }
    }
  }

  if (any_note_triggered) {
    gate_active = true;
    gate_off_tick = step_tick_counter + gate_ticks;
    if (gate_ticks >= ticks_per_step) {
      gate_off_tick = ticks_per_step + 1;
    }
  } else {
    gate_active = false;
  }
}

static uint8_t get_next_step(uint8_t current, int8_t dir) {
    int next = (int)current + dir;
    if (next < 0) return SEQ_MAX_STEPS - 1;
    if (next >= SEQ_MAX_STEPS) return 0;
    return (uint8_t)next;
}

static void advance_step(void) {
  if (current_seq.retrigger && gate_active) {
    all_notes_off();
  }

  uint8_t prev_step = seq_state.current_step;
  uint8_t next_step = prev_step;

  switch (current_seq.play_mode) {
    case SEQ_MODE_FWD:
        next_step = get_next_step(prev_step, 1);
        break;
    case SEQ_MODE_BWD:
        next_step = get_next_step(prev_step, -1);
        break;
    case SEQ_MODE_PINGPONG:
        next_step = (uint8_t)((int)prev_step + seq_state.direction);
        if (next_step >= SEQ_MAX_STEPS || current_seq.steps[next_step].stop_flag) {
            seq_state.direction = -1;
            next_step = (uint8_t)((int)prev_step - 1);
        } else if ((int)prev_step + seq_state.direction < 0) {
            seq_state.direction = 1;
            next_step = (uint8_t)((int)prev_step + 1);
        }
        break;
    case SEQ_MODE_SNAKE: {
        uint8_t page = prev_step / 16;
        uint8_t local = prev_step % 16;
        uint8_t snake_idx = 0;
        for (int i = 0; i < 16; i++) {
            if (snake_map[i] == local) {
                snake_idx = i;
                break;
            }
        }
        snake_idx = (snake_idx + 1) % 16;
        next_step = page * 16 + snake_map[snake_idx];
        break;
    }
    case SEQ_MODE_RANDOM:
        next_step = rand() % SEQ_MAX_STEPS;
        break;
    case SEQ_MODE_DRUNK: {
        int r = rand() % 3; 
        int8_t d = (r == 0) ? -1 : (r == 1 ? 0 : 1);
        next_step = get_next_step(prev_step, d);
        break;
    }
    default:
        next_step = get_next_step(prev_step, 1);
        break;
  }

  if (current_seq.steps[next_step].stop_flag && current_seq.play_mode != SEQ_MODE_PINGPONG) {
    next_step = 0;
  }

  seq_state.current_step = next_step;
  seq_state.current_page = next_step / 16;
  
  trigger_step();
}

static void handle_tick(void) {
  if (!seq_state.is_playing) return;
  step_tick_counter++;

  if (gate_active && step_tick_counter >= gate_off_tick) {
    all_notes_off(); 
  }

  if (step_tick_counter >= ticks_per_step) {
    step_tick_counter = 0;
    advance_step();
  }
}

void seq_process_midi_byte(uint8_t b) {
  if (b == 0xF8) { // Clock
    uint32_t now = time_us_32();
    if (last_midi_clock_us > 0) {
      uint32_t delta = now - last_midi_clock_us;
      if (delta > 5000) {
        float inst_bpm = 2500000.0f / (float)delta;
        float smooth_bpm = (current_seq.bpm * 0.9f) + (inst_bpm * 0.1f);
        if (smooth_bpm < 10.0f) smooth_bpm = 10.0f;
        if (smooth_bpm > 300.0f) smooth_bpm = 300.0f;
        current_seq.bpm = (uint8_t)smooth_bpm;
        g_midi_bpm = smooth_bpm;
      }
    }
    last_midi_clock_us = now;
    external_clock_active = true;
    handle_tick();
  } else if (b == 0xFA) { seq_start(); } 
    else if (b == 0xFC) { seq_stop(); }
    else if (b == 0xFB) { seq_continue(); }
}

void seq_poll(void) {
  uint32_t now = time_us_32();
  if (external_clock_active && (now - last_midi_clock_us > 500000)) {
    external_clock_active = false;
  }
  if (!external_clock_active && seq_state.is_playing) {
    uint32_t bpm = current_seq.bpm;
    if (bpm < 10) bpm = 10;
    uint32_t tick_interval_us = 2500000 / bpm;
    if (now - last_internal_tick_us >= tick_interval_us) {
      last_internal_tick_us += tick_interval_us;
      if (now - last_internal_tick_us > tick_interval_us) {
        last_internal_tick_us = now;
      }
      handle_tick();
    }
  }
}

void seq_start(void) {
  last_internal_tick_us = time_us_32();
  seq_state.is_playing = true;
  seq_state.current_step = 0;
  seq_state.direction = 1;
  step_tick_counter = 0;
  gate_active = false;
  trigger_step();
}

void seq_stop(void) {
  seq_state.is_playing = false;
  all_notes_off();
}

void seq_continue(void) {
  seq_state.is_playing = true;
}

void seq_set_bpm(uint8_t bpm) {
  if (bpm < 10) bpm = 10;
  if (bpm > 240) bpm = 240;
  current_seq.bpm = bpm;
}

uint8_t seq_get_bpm(void) { return current_seq.bpm; }

void seq_set_speed(SeqSpeed speed) {
  current_seq.speed = speed;
  update_ticks_per_step();
}

void seq_set_retrigger(bool retrigger) { current_seq.retrigger = retrigger; }

void seq_set_step_note(uint8_t step, uint8_t note_idx, uint8_t note,
                       uint8_t velocity, uint8_t probability, bool enabled) {
  if (step >= SEQ_MAX_STEPS || note_idx >= SEQ_MAX_NOTES_PER_STEP)
    return;
  SeqNote *n = &current_seq.steps[step].notes[note_idx];
  n->note = note;
  n->velocity = velocity;
  n->probability = probability;
  n->enabled = enabled;
}

void seq_set_step_stop(uint8_t step, bool stop) {
  if (step >= SEQ_MAX_STEPS) return;
  current_seq.steps[step].stop_flag = stop;
}

void seq_set_step_gate(uint8_t step, uint8_t gate) {
  if (step >= SEQ_MAX_STEPS) return;
  current_seq.steps[step].gate_length = gate;
}

void seq_select_sequence(uint8_t idx) {
  if (idx >= SEQ_MAX_SEQUENCES) return;
  seq_state.current_sequence_idx = idx;
  current_seq = sequences[idx];
}

void seq_save_current(uint8_t idx) {
  if (idx >= SEQ_MAX_SEQUENCES) return;
  sequences[idx] = current_seq;
}

void seq_load_sequence(uint8_t idx) { seq_select_sequence(idx); }

void seq_import_preset(uint8_t preset_idx) {
  if (synth_preset_load(preset_idx)) {
    // Success
  }
}
void seq_process(void) {}
