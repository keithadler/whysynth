/* WhySynth - display names for stepped parameter values
 *
 * Copyright (C) 2026 Keith Adler. GPL-2.0-or-later.
 * The names match the GTK editor's, so users see the same words everywhere.
 */

#include <stdlib.h>
#include <string.h>

#include "whysynth_types.h"
#include "whysynth.h"
#include "whysynth_ports.h"
#include "wave_tables.h"
#include "whysynth_names.h"

static const char *osc_modes[] = {
    "Off", "minBLEP", "Wavecycle", "Async Granular", "FM Wave->Sine", "FM Sine->Wave",
    "Waveshaper", "Noise", "PADsynth", "Phase Distortion", "FM Wave->LF Sine", "Wavecycle Chorus"
};
static const char *filter_modes[] = {
    "Off", "Low Pass: Xsynth 2-pole", "Low Pass: Xsynth 4-pole", "Low Pass: Fons' MVC LPF-3",
    "Low Pass: Clipping 4-pole", "Band Pass: 4-pole", "Low Pass: amSynth 4-pole",
    "Band Pass: Csound resonz", "High Pass: 2-pole", "High Pass: 4-pole", "Band Reject: 4-pole"
};
static const char *filter_sources[] = { "Bus A", "Bus B", "Filter 1 Out" };
static const char *effect_modes[] = { "Off", "Plate Reverb", "Dual Delay", "SC Reverb" };
static const char *mod_sources[] = {
    "Constant On", "Mod Wheel", "Pressure", "Key", "Velocity",
    "GLFO Bipolar", "GLFO Unipolar", "VLFO Bipolar", "VLFO Unipolar",
    "MLFO 0 Bipolar", "MLFO 0 Unipolar", "MLFO 1 Bipolar", "MLFO 1 Unipolar",
    "MLFO 2 Bipolar", "MLFO 2 Unipolar", "MLFO 3 Bipolar", "MLFO 3 Unipolar",
    "EGO", "EG1", "EG2", "EG3", "EG4", "ModMix"
};
static const char *eg_modes[] = { "Off", "ADSR", "AAASR", "AASRR", "ASRRR", "One-Shot" };
static const char *eg_shapes[] = {
    "Lead +3", "Lead +2", "Lead +1", "Linear", "Lag -1", "Lag -2", "Lag -3",
    "\"S\" Lead", "\"S\" Mid", "\"S\" Lag", "Jump", "Hold"
};

#define PICK(table) ((value >= 0 && value < (int)(sizeof(table) / sizeof(table[0]))) ? table[value] : NULL)

const char *
whysynth_combo_value_name(int combo_type, int value)
{
    switch (combo_type) {
      case Y_COMBO_TYPE_OSC_MODE:    return PICK(osc_modes);
      case Y_COMBO_TYPE_OSC_WAVEFORM:
      case Y_COMBO_TYPE_WT_WAVEFORM: {
        const char *name, *bar;
        if (value < 0 || value >= wavetables_count) return NULL;
        name = wavetable[value].name;
        if (!name) return NULL;
        bar = strrchr(name, '|');   /* "Sines|Sine 1" -> "Sine 1" */
        return bar ? bar + 1 : name;
      }
      case Y_COMBO_TYPE_MOD_SRC:     return PICK(mod_sources);
      case Y_COMBO_TYPE_MMOD_SRC:    return PICK(mod_sources);   /* grain envelopes past 22 stay numeric */
      case Y_COMBO_TYPE_FILTER_MODE: return PICK(filter_modes);
      case Y_COMBO_TYPE_FILTER_SRC:  return PICK(filter_sources);
      case Y_COMBO_TYPE_EFFECT_MODE: return PICK(effect_modes);
      case Y_COMBO_TYPE_EG_MODE:     return PICK(eg_modes);
      case Y_COMBO_TYPE_EG_SHAPE:    return PICK(eg_shapes);
      default:                       return NULL;
    }
}
