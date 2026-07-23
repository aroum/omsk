#include "synth.h"
#include "../sw_config.h"
#include "../midi/midi_map.h"
#include "../tables/omsk_wavetables.h"
#include "../ui/ui_state.h"
#include "audio.h"
#include "hardware/flash.h"
#include "hardware/regs/addressmap.h"
#include "hardware/sync.h"
#include "pico/stdlib.h"
#include "pra_synth.h"
#include "synth_defs.h"
#include <math.h>
#include <string.h>


SynthParams params;

enum { PRESET_SLOT_COUNT = 16 };

typedef struct {
  uint32_t magic;
  uint16_t version;
  uint16_t reserved;
  uint32_t checksum;
  SynthParams data;
} PresetSlot;

static const uint32_t PRESET_MAGIC = 0x54534552u;
static const uint16_t PRESET_VERSION = 1;
