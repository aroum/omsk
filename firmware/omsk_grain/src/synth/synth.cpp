#include "synth.h"
#include <cstring>
#include <math.h>
#include "pico/multicore.h"
#include "hardware/flash.h"
#include "hardware/sync.h"

#ifndef PICO_FLASH_SIZE_BYTES
#define PICO_FLASH_SIZE_BYTES (4 * 1024 * 1024)
#endif

static uint8_t preset_write_buffer[FLASH_SECTOR_SIZE] __attribute__((aligned(4)));
enum { PRESET_SLOT_COUNT = 16 };
static const uint32_t PRESET_MAGIC = 0x47534552u; // "GRES"
static const uint16_t PRESET_VERSION = 1;

static uint32_t preset_checksum(const PresetData *p) {
    const uint8_t *b = (const uint8_t *)p;
    uint32_t sum = 0;
    for (size_t i = 0; i < sizeof(PresetData); i++) {
        sum += b[i];
    }
    return sum;
}

static uint32_t preset_flash_offset(uint8_t slot) {
    uint32_t base = PICO_FLASH_SIZE_BYTES - (PRESET_SLOT_COUNT * FLASH_SECTOR_SIZE);
    return base + ((uint32_t)slot * FLASH_SECTOR_SIZE);
}

Synth::Synth() {
    fx_wf_gain = 1.0f;
    fx_ds_factor = 1.0f;
    fx_bc_bits = 16.0f;
    fx_mix = 0.0f; // 0% wet by default
}

void Synth::init() {
    // Initialization if required
    global_fx.reset();
}

void Synth::process(int frames, float* out_l, float* out_r) {
    if (frames > 128) frames = 128;

    // Clear main outputs
    memset(out_l, 0, frames * sizeof(float));
    memset(out_r, 0, frames * sizeof(float));

    // Process each voice
    for (int v = 0; v < NUM_VOICES; v++) {
        GranularEngine* voice = &voices[v];
        
        float fm_signal[128] = {0};
        float rm_signal[128] = {0};
        float rm_amt = 0.0f;
        
        // Mod 1 & 2 Routing (25 routes: 0=OFF, 1-12=FM, 13-24=RM)
        int mods[2] = { (int)voice->p.mod1_src, (int)voice->p.mod2_src };
        float amts[2] = { voice->p.mod1_amt, voice->p.mod2_amt };

        for (int m = 0; m < 2; m++) {
            int r = mods[m];
            if (r == 0) continue;
            
            float amt = amts[m];
            if (amt <= 0.001f) continue;

            if (r >= 1 && r <= 12) { // FM
                // Route mapping logic: 1-12 maps to s!=d pairs
                int idx = 1;
                for (int s=0; s<4; s++) {
                    for (int d=0; d<4; d++) {
                        if (s != d) {
                            if (idx == r) {
                                for (int i=0; i<frames; i++) fm_signal[i] += voices[s].last_buffer[i] * amt * 5.0f;
                            }
                            idx++;
                        }
                    }
                }
            } else if (r >= 13 && r <= 24) { // RM
                int idx = 13;
                for (int s=0; s<4; s++) {
                    for (int d=0; d<4; d++) {
                        if (s != d) {
                            if (idx == r) {
                                rm_amt = amt;
                                float env = voices[s].get_last_env(); // 0.0f to 1.0f
                                for (int i=0; i<frames; i++) {
                                    rm_signal[i] = voices[s].last_buffer[i] + (1.0f - env) * 0.2f;
                                }
                            }
                            idx++;
                        }
                    }
                }
            }
        }

        float v_l[128], v_r[128];
        voice->process(frames, v_l, v_r, fm_signal, rm_signal, rm_amt);

        for (int i = 0; i < frames; i++) {
            out_l[i] += v_l[i];
            out_r[i] += v_r[i];
        }
    }

    // Global FX (Using params from voice 0 since FX page is global)
    GranularEngine* v0 = &voices[0];
    if (v0->p.fx_mix > 0.001f) {
        float wf_g = 1.0f + v0->p.fx_wf * 9.0f;
        float ds_f = v0->p.fx_ds;
        float bc_b = v0->p.fx_bc;
        
        // Find peak amplitude across this block for normalization
        float peak = 0.0f;
        for (int i = 0; i < frames; i++) {
            float val_l = fabsf(out_l[i]);
            float val_r = fabsf(out_r[i]);
            if (val_l > peak) peak = val_l;
            if (val_r > peak) peak = val_r;
        }
        
        if (peak > 0.0001f) {
            float inv_peak = 1.0f / peak;
            for (int i = 0; i < frames; i++) {
                // Normalize, process, and scale back
                float norm_l = out_l[i] * inv_peak;
                float norm_r = out_r[i] * inv_peak;
                
                float wet_l = global_fx.process(norm_l, wf_g, ds_f, bc_b, v0->p.fx_mix * 100.0f);
                float wet_r = global_fx.process(norm_r, wf_g, ds_f, bc_b, v0->p.fx_mix * 100.0f);
                
                out_l[i] = wet_l * peak;
                out_r[i] = wet_r * peak;
            }
        }
    }
}

bool Synth::preset_save(uint8_t slot) {
    if (slot >= PRESET_SLOT_COUNT)
        return false;
    PresetSlot ps;
    ps.magic = PRESET_MAGIC;
    ps.version = PRESET_VERSION;
    ps.reserved = 0;
    
    // Copy parameters and mod matrix config from voice 0
    ps.data.params = voices[0].p;
    memcpy(ps.data.mod_src, voices[0].mod_src, sizeof(voices[0].mod_src));
    memcpy(ps.data.mod_amt, voices[0].mod_amt, sizeof(voices[0].mod_amt));
    
    ps.checksum = preset_checksum(&ps.data);

    memset(preset_write_buffer, 0xFF, FLASH_SECTOR_SIZE);
    memcpy(preset_write_buffer, &ps, sizeof(ps));

    uint32_t offset = preset_flash_offset(slot);

    multicore_lockout_start_blocking();
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(offset, FLASH_SECTOR_SIZE);
    flash_range_program(offset, preset_write_buffer, FLASH_SECTOR_SIZE);
    restore_interrupts(ints);
    multicore_lockout_end_blocking();

    return true;
}

bool Synth::preset_load(uint8_t slot) {
    if (slot >= PRESET_SLOT_COUNT)
        return false;
    uint32_t offset = preset_flash_offset(slot);
    const PresetSlot *ps = (const PresetSlot *)(XIP_BASE + offset);
    if (ps->magic != PRESET_MAGIC)
        return false;
    if (ps->version != PRESET_VERSION)
        return false;
    uint32_t sum = preset_checksum(&ps->data);
    if (sum != ps->checksum)
        return false;

    // Apply loaded parameters to all voices
    for (int v = 0; v < NUM_VOICES; v++) {
        voices[v].p = ps->data.params;
        memcpy(voices[v].mod_src, ps->data.mod_src, sizeof(voices[v].mod_src));
        memcpy(voices[v].mod_amt, ps->data.mod_amt, sizeof(voices[v].mod_amt));
    }
    return true;
}

