/* WhySynth synthesizer core (formerly the LADSPA half of dssp_synth.c)
 *
 * Copyright (C) 2005-2017 Sean Bolton and others.
 *
 * Portions of this file may have come from Peter Hanappe's
 * Fluidsynth, copyright (C) 2003 Peter Hanappe and others.
 * Portions of this file may have come from Chris Cannam and Steve
 * Harris's public domain DSSI example code.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be
 * useful, but WITHOUT ANY WARRANTY; without even the implied
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 * PURPOSE.  See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the Free
 * Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <pthread.h>

#include "whysynth_types.h"
#include "whysynth.h"
#include "whysynth_ports.h"
#include "dssp_event.h"
#include "common_data.h"
#include "whysynth_voice.h"
#include "agran_oscillator.h"
#include "wave_tables.h"
#include "sampleset.h"
#include "effects.h"
#include "whysynth_core.h"

static pthread_mutex_t global_mutex = PTHREAD_MUTEX_INITIALIZER;
y_global_t             global;
static int             static_initialized = 0;

/* ---- mutual exclusion ---- */

static inline int
dssp_voicelist_mutex_trylock(y_synth_t *synth)
{
    int rc;

    rc = pthread_mutex_trylock(&synth->voicelist_mutex);
    if (rc) {
        synth->voicelist_mutex_grab_failed = 1;
        return rc;
    }
    if (synth->voicelist_mutex_grab_failed) {
        y_synth_all_voices_off(synth);
        synth->voicelist_mutex_grab_failed = 0;
    }
    return 0;
}

int
dssp_voicelist_mutex_lock(y_synth_t *synth)
{
    return pthread_mutex_lock(&synth->voicelist_mutex);
}

int
dssp_voicelist_mutex_unlock(y_synth_t *synth)
{
    return pthread_mutex_unlock(&synth->voicelist_mutex);
}

char *
dssi_configure_message(const char *fmt, ...)
{
    va_list args;
    char buffer[256];

    va_start(args, fmt);
    vsnprintf(buffer, 256, fmt, args);
    va_end(args);
    return strdup(buffer);
}

/* ---- static setup ---- */

void
y_synth_static_init(void)
{
    pthread_mutex_lock(&global_mutex);
    if (!static_initialized) {
        global.initialized = 0;
        y_init_tables();
        wave_tables_set_count();
        static_initialized = 1;
    }
    pthread_mutex_unlock(&global_mutex);
}

/* ---- instance lifetime ---- */

y_synth_t *
y_synth_new(unsigned long sample_rate)
{
    y_synth_t *synth = (y_synth_t *)calloc(1, sizeof(y_synth_t));
    int i;
    static float static_zero = 0.0f;

    if (!synth) return NULL;

    pthread_mutex_lock(&global_mutex);
    if (global.initialized) {
        global.instance_count++;
    } else {
        global.sample_rate = sample_rate;
        if (!sampleset_init()) {
            YDB_MESSAGE(-1, " y_synth_new: sampleset_init() failed!\n");
            pthread_mutex_unlock(&global_mutex);
            free(synth);
            return NULL;
        }
        global.instance_count = 1;
        global.initialized = 1;
    }
    pthread_mutex_unlock(&global_mutex);

    /* grain envelopes depend on the sample rate, so each instance has its own */
    synth->grain_envelope = create_grain_envelopes(sample_rate);
    if (!synth->grain_envelope) {
        YDB_MESSAGE(-1, " y_synth_new: out of memory!\n");
        y_synth_free(synth);
        return NULL;
    }

    /* do any per-instance one-time initialization here */
    for (i = 0; i < Y_MAX_POLYPHONY; i++) {
        synth->voice[i] = y_voice_new(synth);
        if (!synth->voice[i]) {
            // YDB_MESSAGE(-1, " y_instantiate: out of memory!\n");
            y_synth_free(synth);
            return NULL;
        }
    }

    if (!new_grain_array(synth, AG_DEFAULT_GRAIN_COUNT)) {
        YDB_MESSAGE(-1, " y_instantiate: out of memory!\n");
        y_synth_free(synth);
        return NULL;
    }

    if (!sampleset_instantiate(synth)) {
        YDB_MESSAGE(-1, " y_instantiate: out of memory!\n");
        y_synth_free(synth);
        return NULL;
    }

    synth->sample_rate = (float)sample_rate;
    synth->control_rate = (float)sample_rate / (float)Y_CONTROL_PERIOD;
    synth->deltat = 1.0f / synth->sample_rate;

    if (!effects_setup(synth)) {
        YDB_MESSAGE(-1, " y_instantiate: out of memory!\n");
        y_synth_free(synth);
        return NULL;
    }

    synth->polyphony = Y_DEFAULT_POLYPHONY;
    synth->voices = Y_DEFAULT_POLYPHONY;
    synth->monophonic = 0;
    synth->glide = 0;
    synth->last_noteon_pitch = 0.0f;
    pthread_mutex_init(&synth->voicelist_mutex, NULL);
    synth->voicelist_mutex_grab_failed = 0;
    pthread_mutex_init(&synth->patches_mutex, NULL);
    synth->patch_count = 0;
    synth->patches_allocated = 0;
    synth->patches = NULL;
    synth->pending_patch_change = -1;
    synth->program_cancel = 1;
    synth->project_dir = NULL;
    synth->osc1.sampleset = NULL;
    synth->osc2.sampleset = NULL;
    synth->osc3.sampleset = NULL;
    synth->osc4.sampleset = NULL;
    synth->glfo.delay = &static_zero;
    synth->ego.level[3] = &static_zero;
    synth->eg1.level[3] = &static_zero;
    synth->eg2.level[3] = &static_zero;
    synth->eg3.level[3] = &static_zero;
    synth->eg4.level[3] = &static_zero;
    synth->mod[Y_MOD_ONE].value = 1.0f;
    synth->mod[Y_MOD_ONE].next_value = 1.0f;
    synth->mod[Y_MOD_ONE].delta = 0.0f;
    synth->dc_block_r = 1.0f - (2.0f * 3.141593f * 20.0f/* Hz */ / (float)sample_rate); /* DC blocker cutoff */
    y_data_friendly_patches(synth);
    y_synth_init_controls(synth);

    return synth;
}

void
y_synth_free(y_synth_t *synth)
{
    int i;

    for (i = 0; i < Y_MAX_POLYPHONY; i++)
        if (synth->voice[i]) free(synth->voice[i]);
    if (synth->patches) free(synth->patches);
    if (synth->grains) free(synth->grains);
    if (synth->grain_envelope) free_grain_envelopes(synth->grain_envelope);
    if (synth->project_dir) free(synth->project_dir);
    sampleset_cleanup(synth);
    effects_cleanup(synth);
    free(synth);
    pthread_mutex_lock(&global_mutex);
    if (--global.instance_count == 0) {
        sampleset_fini();
        global.initialized = 0;
    }
    pthread_mutex_unlock(&global_mutex);
}


void
y_synth_connect_port(y_synth_t *synth, unsigned long port, float *data)
{

    switch (port) {
      /* -PORTS- */
      case Y_PORT_OUTPUT_LEFT:        synth->output_left        = data;  break;
      case Y_PORT_OUTPUT_RIGHT:       synth->output_right       = data;  break;

      case Y_PORT_OSC1_MODE:          synth->osc1.mode          = data;  break;
      case Y_PORT_OSC1_WAVEFORM:      synth->osc1.waveform      = data;  break;
      case Y_PORT_OSC1_PITCH:         synth->osc1.pitch         = data;  break;
      case Y_PORT_OSC1_DETUNE:        synth->osc1.detune        = data;  break;
      case Y_PORT_OSC1_PITCH_MOD_SRC: synth->osc1.pitch_mod_src = data;  break;
      case Y_PORT_OSC1_PITCH_MOD_AMT: synth->osc1.pitch_mod_amt = data;  break;
      case Y_PORT_OSC1_MPARAM1:       synth->osc1.mparam1       = data;  break;
      case Y_PORT_OSC1_MPARAM2:       synth->osc1.mparam2       = data;  break;
      case Y_PORT_OSC1_MMOD_SRC:      synth->osc1.mmod_src      = data;  break;
      case Y_PORT_OSC1_MMOD_AMT:      synth->osc1.mmod_amt      = data;  break;
      case Y_PORT_OSC1_AMP_MOD_SRC:   synth->osc1.amp_mod_src   = data;  break;
      case Y_PORT_OSC1_AMP_MOD_AMT:   synth->osc1.amp_mod_amt   = data;  break;
      case Y_PORT_OSC1_LEVEL_A:       synth->osc1.level_a       = data;  break;
      case Y_PORT_OSC1_LEVEL_B:       synth->osc1.level_b       = data;  break;

      case Y_PORT_OSC2_MODE:          synth->osc2.mode          = data;  break;
      case Y_PORT_OSC2_WAVEFORM:      synth->osc2.waveform      = data;  break;
      case Y_PORT_OSC2_PITCH:         synth->osc2.pitch         = data;  break;
      case Y_PORT_OSC2_DETUNE:        synth->osc2.detune        = data;  break;
      case Y_PORT_OSC2_PITCH_MOD_SRC: synth->osc2.pitch_mod_src = data;  break;
      case Y_PORT_OSC2_PITCH_MOD_AMT: synth->osc2.pitch_mod_amt = data;  break;
      case Y_PORT_OSC2_MPARAM1:       synth->osc2.mparam1       = data;  break;
      case Y_PORT_OSC2_MPARAM2:       synth->osc2.mparam2       = data;  break;
      case Y_PORT_OSC2_MMOD_SRC:      synth->osc2.mmod_src      = data;  break;
      case Y_PORT_OSC2_MMOD_AMT:      synth->osc2.mmod_amt      = data;  break;
      case Y_PORT_OSC2_AMP_MOD_SRC:   synth->osc2.amp_mod_src   = data;  break;
      case Y_PORT_OSC2_AMP_MOD_AMT:   synth->osc2.amp_mod_amt   = data;  break;
      case Y_PORT_OSC2_LEVEL_A:       synth->osc2.level_a       = data;  break;
      case Y_PORT_OSC2_LEVEL_B:       synth->osc2.level_b       = data;  break;

      case Y_PORT_OSC3_MODE:          synth->osc3.mode          = data;  break;
      case Y_PORT_OSC3_WAVEFORM:      synth->osc3.waveform      = data;  break;
      case Y_PORT_OSC3_PITCH:         synth->osc3.pitch         = data;  break;
      case Y_PORT_OSC3_DETUNE:        synth->osc3.detune        = data;  break;
      case Y_PORT_OSC3_PITCH_MOD_SRC: synth->osc3.pitch_mod_src = data;  break;
      case Y_PORT_OSC3_PITCH_MOD_AMT: synth->osc3.pitch_mod_amt = data;  break;
      case Y_PORT_OSC3_MPARAM1:       synth->osc3.mparam1       = data;  break;
      case Y_PORT_OSC3_MPARAM2:       synth->osc3.mparam2       = data;  break;
      case Y_PORT_OSC3_MMOD_SRC:      synth->osc3.mmod_src      = data;  break;
      case Y_PORT_OSC3_MMOD_AMT:      synth->osc3.mmod_amt      = data;  break;
      case Y_PORT_OSC3_AMP_MOD_SRC:   synth->osc3.amp_mod_src   = data;  break;
      case Y_PORT_OSC3_AMP_MOD_AMT:   synth->osc3.amp_mod_amt   = data;  break;
      case Y_PORT_OSC3_LEVEL_A:       synth->osc3.level_a       = data;  break;
      case Y_PORT_OSC3_LEVEL_B:       synth->osc3.level_b       = data;  break;

      case Y_PORT_OSC4_MODE:          synth->osc4.mode          = data;  break;
      case Y_PORT_OSC4_WAVEFORM:      synth->osc4.waveform      = data;  break;
      case Y_PORT_OSC4_PITCH:         synth->osc4.pitch         = data;  break;
      case Y_PORT_OSC4_DETUNE:        synth->osc4.detune        = data;  break;
      case Y_PORT_OSC4_PITCH_MOD_SRC: synth->osc4.pitch_mod_src = data;  break;
      case Y_PORT_OSC4_PITCH_MOD_AMT: synth->osc4.pitch_mod_amt = data;  break;
      case Y_PORT_OSC4_MPARAM1:       synth->osc4.mparam1       = data;  break;
      case Y_PORT_OSC4_MPARAM2:       synth->osc4.mparam2       = data;  break;
      case Y_PORT_OSC4_MMOD_SRC:      synth->osc4.mmod_src      = data;  break;
      case Y_PORT_OSC4_MMOD_AMT:      synth->osc4.mmod_amt      = data;  break;
      case Y_PORT_OSC4_AMP_MOD_SRC:   synth->osc4.amp_mod_src   = data;  break;
      case Y_PORT_OSC4_AMP_MOD_AMT:   synth->osc4.amp_mod_amt   = data;  break;
      case Y_PORT_OSC4_LEVEL_A:       synth->osc4.level_a       = data;  break;
      case Y_PORT_OSC4_LEVEL_B:       synth->osc4.level_b       = data;  break;

      case Y_PORT_VCF1_MODE:          synth->vcf1.mode          = data;  break;
      case Y_PORT_VCF1_SOURCE:        synth->vcf1.source        = data;  break;
      case Y_PORT_VCF1_FREQUENCY:     synth->vcf1.frequency     = data;  break;
      case Y_PORT_VCF1_FREQ_MOD_SRC:  synth->vcf1.freq_mod_src  = data;  break;
      case Y_PORT_VCF1_FREQ_MOD_AMT:  synth->vcf1.freq_mod_amt  = data;  break;
      case Y_PORT_VCF1_QRES:          synth->vcf1.qres          = data;  break;
      case Y_PORT_VCF1_MPARAM:        synth->vcf1.mparam        = data;  break;

      case Y_PORT_VCF2_MODE:          synth->vcf2.mode          = data;  break;
      case Y_PORT_VCF2_SOURCE:        synth->vcf2.source        = data;  break;
      case Y_PORT_VCF2_FREQUENCY:     synth->vcf2.frequency     = data;  break;
      case Y_PORT_VCF2_FREQ_MOD_SRC:  synth->vcf2.freq_mod_src  = data;  break;
      case Y_PORT_VCF2_FREQ_MOD_AMT:  synth->vcf2.freq_mod_amt  = data;  break;
      case Y_PORT_VCF2_QRES:          synth->vcf2.qres          = data;  break;
      case Y_PORT_VCF2_MPARAM:        synth->vcf2.mparam        = data;  break;

      case Y_PORT_BUSA_LEVEL:         synth->busa_level         = data;  break;
      case Y_PORT_BUSA_PAN:           synth->busa_pan           = data;  break;
      case Y_PORT_BUSB_LEVEL:         synth->busb_level         = data;  break;
      case Y_PORT_BUSB_PAN:           synth->busb_pan           = data;  break;
      case Y_PORT_VCF1_LEVEL:         synth->vcf1_level         = data;  break;
      case Y_PORT_VCF1_PAN:           synth->vcf1_pan           = data;  break;
      case Y_PORT_VCF2_LEVEL:         synth->vcf2_level         = data;  break;
      case Y_PORT_VCF2_PAN:           synth->vcf2_pan           = data;  break;
      case Y_PORT_VOLUME:             synth->volume             = data;  break;

      case Y_PORT_EFFECT_MODE:        synth->effect_mode        = data;  break;
      case Y_PORT_EFFECT_PARAM1:      synth->effect_param1      = data;  break;
      case Y_PORT_EFFECT_PARAM2:      synth->effect_param2      = data;  break;
      case Y_PORT_EFFECT_PARAM3:      synth->effect_param3      = data;  break;
      case Y_PORT_EFFECT_PARAM4:      synth->effect_param4      = data;  break;
      case Y_PORT_EFFECT_PARAM5:      synth->effect_param5      = data;  break;
      case Y_PORT_EFFECT_PARAM6:      synth->effect_param6      = data;  break;
      case Y_PORT_EFFECT_MIX:         synth->effect_mix         = data;  break;

      case Y_PORT_GLIDE_TIME:         synth->glide_time         = data;  break;
      case Y_PORT_BEND_RANGE:         synth->bend_range         = data;  break;

      case Y_PORT_GLFO_FREQUENCY:     synth->glfo.frequency     = data;  break;
      case Y_PORT_GLFO_WAVEFORM:      synth->glfo.waveform      = data;  break;
      /* synth->glfo.delay always points to a 0.0f */
      case Y_PORT_GLFO_AMP_MOD_SRC:   synth->glfo.amp_mod_src   = data;  break;
      case Y_PORT_GLFO_AMP_MOD_AMT:   synth->glfo.amp_mod_amt   = data;  break;

      case Y_PORT_VLFO_FREQUENCY:     synth->vlfo.frequency     = data;  break;
      case Y_PORT_VLFO_WAVEFORM:      synth->vlfo.waveform      = data;  break;
      case Y_PORT_VLFO_DELAY:         synth->vlfo.delay         = data;  break;
      case Y_PORT_VLFO_AMP_MOD_SRC:   synth->vlfo.amp_mod_src   = data;  break;
      case Y_PORT_VLFO_AMP_MOD_AMT:   synth->vlfo.amp_mod_amt   = data;  break;

      case Y_PORT_MLFO_FREQUENCY:     synth->mlfo.frequency     = data;  break;
      case Y_PORT_MLFO_WAVEFORM:      synth->mlfo.waveform      = data;  break;
      case Y_PORT_MLFO_DELAY:         synth->mlfo.delay         = data;  break;
      case Y_PORT_MLFO_AMP_MOD_SRC:   synth->mlfo.amp_mod_src   = data;  break;
      case Y_PORT_MLFO_AMP_MOD_AMT:   synth->mlfo.amp_mod_amt   = data;  break;
      case Y_PORT_MLFO_PHASE_SPREAD:  synth->mlfo_phase_spread  = data;  break;
      case Y_PORT_MLFO_RANDOM_FREQ:   synth->mlfo_random_freq   = data;  break;

      case Y_PORT_EGO_MODE:           synth->ego.mode           = data;  break;
      case Y_PORT_EGO_SHAPE1:         synth->ego.shape[0]       = data;  break;
      case Y_PORT_EGO_TIME1:          synth->ego.time[0]        = data;  break;
      case Y_PORT_EGO_LEVEL1:         synth->ego.level[0]       = data;  break;
      case Y_PORT_EGO_SHAPE2:         synth->ego.shape[1]       = data;  break;
      case Y_PORT_EGO_TIME2:          synth->ego.time[1]        = data;  break;
      case Y_PORT_EGO_LEVEL2:         synth->ego.level[1]       = data;  break;
      case Y_PORT_EGO_SHAPE3:         synth->ego.shape[2]       = data;  break;
      case Y_PORT_EGO_TIME3:          synth->ego.time[2]        = data;  break;
      case Y_PORT_EGO_LEVEL3:         synth->ego.level[2]       = data;  break;
      case Y_PORT_EGO_SHAPE4:         synth->ego.shape[3]       = data;  break;
      case Y_PORT_EGO_TIME4:          synth->ego.time[3]        = data;  break;
      /* synth->ego.level[3] always points to a 0.0f */
      case Y_PORT_EGO_VEL_LEVEL_SENS: synth->ego.vel_level_sens = data;  break;
      case Y_PORT_EGO_VEL_TIME_SCALE: synth->ego.vel_time_scale = data;  break;
      case Y_PORT_EGO_KBD_TIME_SCALE: synth->ego.kbd_time_scale = data;  break;
      case Y_PORT_EGO_AMP_MOD_SRC:    synth->ego.amp_mod_src    = data;  break;
      case Y_PORT_EGO_AMP_MOD_AMT:    synth->ego.amp_mod_amt    = data;  break;

      case Y_PORT_EG1_MODE:           synth->eg1.mode           = data;  break;
      case Y_PORT_EG1_SHAPE1:         synth->eg1.shape[0]       = data;  break;
      case Y_PORT_EG1_TIME1:          synth->eg1.time[0]        = data;  break;
      case Y_PORT_EG1_LEVEL1:         synth->eg1.level[0]       = data;  break;
      case Y_PORT_EG1_SHAPE2:         synth->eg1.shape[1]       = data;  break;
      case Y_PORT_EG1_TIME2:          synth->eg1.time[1]        = data;  break;
      case Y_PORT_EG1_LEVEL2:         synth->eg1.level[1]       = data;  break;
      case Y_PORT_EG1_SHAPE3:         synth->eg1.shape[2]       = data;  break;
      case Y_PORT_EG1_TIME3:          synth->eg1.time[2]        = data;  break;
      case Y_PORT_EG1_LEVEL3:         synth->eg1.level[2]       = data;  break;
      case Y_PORT_EG1_SHAPE4:         synth->eg1.shape[3]       = data;  break;
      case Y_PORT_EG1_TIME4:          synth->eg1.time[3]        = data;  break;
      /* synth->eg1.level[3] always points to a 0.0f */
      case Y_PORT_EG1_VEL_LEVEL_SENS: synth->eg1.vel_level_sens = data;  break;
      case Y_PORT_EG1_VEL_TIME_SCALE: synth->eg1.vel_time_scale = data;  break;
      case Y_PORT_EG1_KBD_TIME_SCALE: synth->eg1.kbd_time_scale = data;  break;
      case Y_PORT_EG1_AMP_MOD_SRC:    synth->eg1.amp_mod_src    = data;  break;
      case Y_PORT_EG1_AMP_MOD_AMT:    synth->eg1.amp_mod_amt    = data;  break;

      case Y_PORT_EG2_MODE:           synth->eg2.mode           = data;  break;
      case Y_PORT_EG2_SHAPE1:         synth->eg2.shape[0]       = data;  break;
      case Y_PORT_EG2_TIME1:          synth->eg2.time[0]        = data;  break;
      case Y_PORT_EG2_LEVEL1:         synth->eg2.level[0]       = data;  break;
      case Y_PORT_EG2_SHAPE2:         synth->eg2.shape[1]       = data;  break;
      case Y_PORT_EG2_TIME2:          synth->eg2.time[1]        = data;  break;
      case Y_PORT_EG2_LEVEL2:         synth->eg2.level[1]       = data;  break;
      case Y_PORT_EG2_SHAPE3:         synth->eg2.shape[2]       = data;  break;
      case Y_PORT_EG2_TIME3:          synth->eg2.time[2]        = data;  break;
      case Y_PORT_EG2_LEVEL3:         synth->eg2.level[2]       = data;  break;
      case Y_PORT_EG2_SHAPE4:         synth->eg2.shape[3]       = data;  break;
      case Y_PORT_EG2_TIME4:          synth->eg2.time[3]        = data;  break;
      /* synth->eg2.level[3] always points to a 0.0f */
      case Y_PORT_EG2_VEL_LEVEL_SENS: synth->eg2.vel_level_sens = data;  break;
      case Y_PORT_EG2_VEL_TIME_SCALE: synth->eg2.vel_time_scale = data;  break;
      case Y_PORT_EG2_KBD_TIME_SCALE: synth->eg2.kbd_time_scale = data;  break;
      case Y_PORT_EG2_AMP_MOD_SRC:    synth->eg2.amp_mod_src    = data;  break;
      case Y_PORT_EG2_AMP_MOD_AMT:    synth->eg2.amp_mod_amt    = data;  break;

      case Y_PORT_EG3_MODE:           synth->eg3.mode           = data;  break;
      case Y_PORT_EG3_SHAPE1:         synth->eg3.shape[0]       = data;  break;
      case Y_PORT_EG3_TIME1:          synth->eg3.time[0]        = data;  break;
      case Y_PORT_EG3_LEVEL1:         synth->eg3.level[0]       = data;  break;
      case Y_PORT_EG3_SHAPE2:         synth->eg3.shape[1]       = data;  break;
      case Y_PORT_EG3_TIME2:          synth->eg3.time[1]        = data;  break;
      case Y_PORT_EG3_LEVEL2:         synth->eg3.level[1]       = data;  break;
      case Y_PORT_EG3_SHAPE3:         synth->eg3.shape[2]       = data;  break;
      case Y_PORT_EG3_TIME3:          synth->eg3.time[2]        = data;  break;
      case Y_PORT_EG3_LEVEL3:         synth->eg3.level[2]       = data;  break;
      case Y_PORT_EG3_SHAPE4:         synth->eg3.shape[3]       = data;  break;
      case Y_PORT_EG3_TIME4:          synth->eg3.time[3]        = data;  break;
      /* synth->eg3.level[3] always points to a 0.0f */
      case Y_PORT_EG3_VEL_LEVEL_SENS: synth->eg3.vel_level_sens = data;  break;
      case Y_PORT_EG3_VEL_TIME_SCALE: synth->eg3.vel_time_scale = data;  break;
      case Y_PORT_EG3_KBD_TIME_SCALE: synth->eg3.kbd_time_scale = data;  break;
      case Y_PORT_EG3_AMP_MOD_SRC:    synth->eg3.amp_mod_src    = data;  break;
      case Y_PORT_EG3_AMP_MOD_AMT:    synth->eg3.amp_mod_amt    = data;  break;

      case Y_PORT_EG4_MODE:           synth->eg4.mode           = data;  break;
      case Y_PORT_EG4_SHAPE1:         synth->eg4.shape[0]       = data;  break;
      case Y_PORT_EG4_TIME1:          synth->eg4.time[0]        = data;  break;
      case Y_PORT_EG4_LEVEL1:         synth->eg4.level[0]       = data;  break;
      case Y_PORT_EG4_SHAPE2:         synth->eg4.shape[1]       = data;  break;
      case Y_PORT_EG4_TIME2:          synth->eg4.time[1]        = data;  break;
      case Y_PORT_EG4_LEVEL2:         synth->eg4.level[1]       = data;  break;
      case Y_PORT_EG4_SHAPE3:         synth->eg4.shape[2]       = data;  break;
      case Y_PORT_EG4_TIME3:          synth->eg4.time[2]        = data;  break;
      case Y_PORT_EG4_LEVEL3:         synth->eg4.level[2]       = data;  break;
      case Y_PORT_EG4_SHAPE4:         synth->eg4.shape[3]       = data;  break;
      case Y_PORT_EG4_TIME4:          synth->eg4.time[3]        = data;  break;
      /* synth->eg4.level[3] always points to a 0.0f */
      case Y_PORT_EG4_VEL_LEVEL_SENS: synth->eg4.vel_level_sens = data;  break;
      case Y_PORT_EG4_VEL_TIME_SCALE: synth->eg4.vel_time_scale = data;  break;
      case Y_PORT_EG4_KBD_TIME_SCALE: synth->eg4.kbd_time_scale = data;  break;
      case Y_PORT_EG4_AMP_MOD_SRC:    synth->eg4.amp_mod_src    = data;  break;
      case Y_PORT_EG4_AMP_MOD_AMT:    synth->eg4.amp_mod_amt    = data;  break;

      case Y_PORT_MODMIX_BIAS:        synth->modmix_bias        = data;  break;
      case Y_PORT_MODMIX_MOD1_SRC:    synth->modmix_mod1_src    = data;  break;
      case Y_PORT_MODMIX_MOD1_AMT:    synth->modmix_mod1_amt    = data;  break;
      case Y_PORT_MODMIX_MOD2_SRC:    synth->modmix_mod2_src    = data;  break;
      case Y_PORT_MODMIX_MOD2_AMT:    synth->modmix_mod2_amt    = data;  break;

      case Y_PORT_TUNING:             synth->tuning             = data;  break;

      default:
        break;
    }
}

void
y_synth_activate(y_synth_t *synth)
{
    synth->control_remains = 0;
    synth->note_id = 0;
    y_voice_setup_lfo(synth, &synth->glfo, &synth->glfo_vlfo, 0.0f, 0.0f,
                      synth->mod, &synth->mod[Y_GLOBAL_MOD_GLFO]);
    y_synth_all_voices_off(synth);
}

void
y_synth_deactivate(y_synth_t *synth)
{
    y_synth_all_voices_off(synth);
}

/* ---- patches ---- */

void
y_synth_request_patch(y_synth_t *synth, unsigned long patch)
{
    if (patch >= synth->patch_count) return;
    if (pthread_mutex_trylock(&synth->patches_mutex)) {
        synth->pending_patch_change = (int)patch;
        return;
    }
    y_synth_select_patch(synth, patch);
    pthread_mutex_unlock(&synth->patches_mutex);
}

static inline void
handle_pending_patch_change(y_synth_t *synth)
{
    if (pthread_mutex_trylock(&synth->patches_mutex))
        return;
    if (synth->pending_patch_change >= 0 &&
        (unsigned int)synth->pending_patch_change < synth->patch_count)
        y_synth_select_patch(synth, synth->pending_patch_change);
    synth->pending_patch_change = -1;
    pthread_mutex_unlock(&synth->patches_mutex);
}

/* ---- events and the run loop ---- */

/* caller holds the voice list lock */
static void
apply_polyphony(y_synth_t *synth, int polyphony)
{
    int i;

    if (polyphony < 1) polyphony = 1;
    if (polyphony > Y_MAX_POLYPHONY) polyphony = Y_MAX_POLYPHONY;
    synth->polyphony = polyphony;
    if (!synth->monophonic) {
        synth->voices = polyphony;
        for (i = polyphony; i < Y_MAX_POLYPHONY; i++) {
            y_voice_t *voice = synth->voice[i];
            if (_PLAYING(voice)) {
                if (synth->held_keys[0] != -1) {
                    int k;
                    for (k = 0; k < 8; k++) synth->held_keys[k] = -1;
                }
                y_voice_off(synth, voice);
            }
        }
    }
}

/* caller holds the voice list lock */
static void
apply_mono_mode(y_synth_t *synth, int mode)
{
    if (mode < Y_MONO_MODE_OFF || mode > Y_MONO_MODE_BOTH) return;
    if (mode == Y_MONO_MODE_OFF) {
        synth->monophonic = 0;
        synth->voices = synth->polyphony;
    } else {
        if (!synth->monophonic) y_synth_all_voices_off(synth);
        synth->monophonic = mode;
        synth->voices = 1;
    }
}

static inline void
handle_event(y_synth_t *synth, const y_event_t *ev)
{
    switch (ev->type) {
      case Y_EV_NOTE_ON:
        if (ev->b > 0)
            y_synth_note_on(synth, ev->a, ev->b);
        else
            y_synth_note_off(synth, ev->a, 64);
        break;
      case Y_EV_NOTE_OFF:
        y_synth_note_off(synth, ev->a, ev->b);
        break;
      case Y_EV_KEY_PRESSURE:
        if (ev->a < 128) y_synth_key_pressure(synth, ev->a, ev->b);
        break;
      case Y_EV_CONTROL_CHANGE:
        if (ev->a < 128) y_synth_control_change(synth, ev->a, ev->b & 0x7F);
        break;
      case Y_EV_CHANNEL_PRESSURE:
        y_synth_channel_pressure(synth, ev->a & 0x7F);
        break;
      case Y_EV_PITCH_BEND:
        y_synth_pitch_bend(synth, ev->value < -8192 ? -8192 : (ev->value > 8191 ? 8191 : ev->value));
        break;
      case Y_EV_PROGRAM_CHANGE:
        if (ev->value >= 0) y_synth_request_patch(synth, (unsigned long)ev->value);
        break;
      case Y_EV_POLYPHONY:
        apply_polyphony(synth, ev->value);
        break;
      case Y_EV_MONO_MODE:
        apply_mono_mode(synth, ev->value);
        break;
      case Y_EV_GLIDE_MODE:
        if (ev->value >= Y_GLIDE_MODE_LEGATO && ev->value <= Y_GLIDE_MODE_OFF)
            synth->glide = ev->value;
        break;
      default:
        break;
    }
}

void
y_synth_run(y_synth_t *synth, unsigned long sample_count,
            const y_event_t *events, unsigned long event_count)
{
    unsigned long samples_done = 0;
    unsigned long event_index = 0;
    unsigned long burst_size;

    if (dssp_voicelist_mutex_trylock(synth)) {
        memset(synth->output_left,  0, sizeof(float) * sample_count);
        memset(synth->output_right, 0, sizeof(float) * sample_count);
        return;
    }

    if (synth->pending_patch_change > -1)
        handle_pending_patch_change(synth);

    while (samples_done < sample_count) {

        if (!synth->control_remains)
            synth->control_remains = Y_CONTROL_PERIOD;

        while (event_index < event_count && events[event_index].frame <= samples_done) {
            handle_event(synth, &events[event_index]);
            event_index++;
        }

        burst_size = Y_CONTROL_PERIOD;
        if (synth->control_remains < burst_size)
            burst_size = synth->control_remains;
        if (event_index < event_count && events[event_index].frame - samples_done < burst_size)
            burst_size = events[event_index].frame - samples_done;
        if (sample_count - samples_done < burst_size)
            burst_size = sample_count - samples_done;

        y_synth_render_voices(synth, synth->output_left + samples_done,
                              synth->output_right + samples_done, burst_size,
                              (burst_size == synth->control_remains));
        samples_done += burst_size;
        synth->control_remains -= burst_size;
    }

    while (event_index < event_count) {
        handle_event(synth, &events[event_index]);
        event_index++;
    }

    /* A non-finite sample would stay in the filter and effect state for
     * good and the instrument would fall silent. Extreme parameter
     * combinations at unusual sample rates can still reach one, so when it
     * happens: silence this block, drop every voice and reset the effect and
     * DC-blocker state. One click instead of a dead plugin. */
    {
        unsigned long i;
        int bad = 0;
        for (i = 0; i < sample_count; i++) {
            if (!(synth->output_left[i] == synth->output_left[i]) ||
                !(synth->output_right[i] == synth->output_right[i]) ||
                synth->output_left[i] > 1e6f || synth->output_left[i] < -1e6f ||
                synth->output_right[i] > 1e6f || synth->output_right[i] < -1e6f) {
                bad = 1;
                break;
            }
        }
        if (bad) {
            memset(synth->output_left,  0, sizeof(float) * sample_count);
            memset(synth->output_right, 0, sizeof(float) * sample_count);
            y_synth_all_voices_off(synth);
            synth->dc_block_l_xnm1 = synth->dc_block_l_ynm1 = 0.0f;
            synth->dc_block_r_xnm1 = synth->dc_block_r_ynm1 = 0.0f;
            synth->last_effect_mode = -1;   /* effects re-initialize their buffers */
            for (i = 1; i < Y_GLOBAL_MODS_COUNT; i++) {
                synth->mod[i].value = synth->mod[i].next_value = synth->mod[i].delta = 0.0f;
            }
            synth->nonfinite_recoveries++;
        }
    }

    dssp_voicelist_mutex_unlock(synth);
}
