/* WhySynth DSSI software synthesizer plugin
 *
 * Copyright (C) 2004-2007 Sean Bolton and others.
 *
 * Portions of this file may have come from Peter Hanappe's
 * Fluidsynth, copyright (C) 2003 Peter Hanappe and others.
 * Portions of this file may have come from alsa-lib, copyright
 * and licensed under the LGPL v2.1.
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

#ifndef _DSSP_EVENT_H
#define _DSSP_EVENT_H

#include <stdlib.h>
#include <pthread.h>


#include "whysynth_types.h"
#include "whysynth.h"
#include "whysynth_voice.h"

#define Y_MONO_MODE_OFF  0
#define Y_MONO_MODE_ON   1
#define Y_MONO_MODE_ONCE 2
#define Y_MONO_MODE_BOTH 3

#define Y_GLIDE_MODE_LEGATO   0
#define Y_GLIDE_MODE_INITIAL  1
#define Y_GLIDE_MODE_ALWAYS   2
#define Y_GLIDE_MODE_LEFTOVER 3
#define Y_GLIDE_MODE_OFF      4

/* -PORTS- */
struct _y_sosc_t
{
    float    *mode;
    float    *waveform;
    float    *pitch;
    float    *detune;
    float    *pitch_mod_src;
    float    *pitch_mod_amt;
    float    *mparam1;        /* sync / grain lz / mod freq ratio / ws phase offset / slave freq / noise cutoff freq */
    float    *mparam2;        /* pulsewidth / wave sel bias / grain spread / mod freq detune / mod amount bias */
    float    *mmod_src;       /* (pw | mod index | slave freq ) mod source / grain envelope */          
    float    *mmod_amt;       /* (pw | mod index | slave freq ) mod amount / grain pitch distribution */
    float    *amp_mod_src;
    float    *amp_mod_amt;
    float    *level_a;
    float    *level_b;
    y_sampleset_t  *sampleset;
};

struct _y_svcf_t
{
    float    *mode;
    float    *source;
    float    *frequency;
    float    *freq_mod_src;
    float    *freq_mod_amt;
    float    *qres;
    float    *mparam;
};

struct _y_slfo_t
{
    float    *frequency;
    float    *waveform;
    float    *delay;
    float    *amp_mod_src;
    float    *amp_mod_amt;
};

struct _y_seg_t
{
    float    *mode;
    float    *shape[4];
    float    *time[4];
    float    *level[4];  /* level[3] always points to a 0.0f */
    float    *vel_level_sens;
    float    *vel_time_scale;
    float    *kbd_time_scale;
    float    *amp_mod_src;
    float    *amp_mod_amt;
};

/*
 * y_synth_t
 */
struct _y_synth_t {
    /* output */
    float    *output_left;
    float    *output_right;

    float           sample_rate;
    float           deltat;            /* 1 / sample_rate */
    float           control_rate;
    unsigned long   control_remains;

    /* voice tracking and data */
    unsigned int    note_id;           /* incremented for every new note, used for voice-stealing prioritization */
    int             polyphony;         /* requested polyphony, must be <= Y_MAX_POLYPHONY */
    int             voices;            /* current polyphony, either requested polyphony above or 1 while in monophonic mode */
    int             monophonic;        /* true if operating in monophonic mode */
    int             glide;             /* current glide mode */
    float           last_noteon_pitch; /* glide start pitch for non-legato modes */
    signed char     held_keys[8];      /* for monophonic key tracking, an array of note-ons, most recently received first */
    
    pthread_mutex_t voicelist_mutex;
    int             voicelist_mutex_grab_failed;

    y_voice_t      *voice[Y_MAX_POLYPHONY];

    pthread_mutex_t patches_mutex;
    unsigned int    patch_count;
    unsigned int    patches_allocated;
    y_patch_t      *patches;
    int             pending_patch_change;
    int             program_cancel;    /* if true, cancel any playing notes on recept of program change */
    char           *project_dir;

    grain_envelope_data_t *grain_envelope;   /* grain envelopes for this instance's sample rate */
    grain_t        *grains;                   /* array of all grains */
    grain_t        *free_grain_list;          /* list of available grains */

    /* current non-LADSPA-port-mapped controller values */
    unsigned char   key_pressure[128];
    unsigned char   cc[128];                  /* controller values */
    unsigned char   channel_pressure;
    int             pitch_wheel;              /* range is -8192 - 8191 */

    /* translated controller values */
    float           mod_wheel;                /* 0.0 to 1.0 -FIX- superfluous? */
    float           pressure;                 /* 0.0 to 1.0 */
    float           pitch_bend;               /* frequency multiplier, product of wheel setting and bend range, center = 1.0 */
    float           cc_volume;                /* volume multiplier, 0.0 to 1.0 */
    float           cc_pan;                   /* pan L-R, 0.0 to 1.0 */

    /* global modulators */
    struct vmod     mod[Y_GLOBAL_MODS_COUNT];
    struct vlfo     glfo_vlfo;

    /* LADSPA ports / WhySynth patch parameters */
    y_sosc_t        osc1,
                    osc2,
                    osc3,
                    osc4;
    y_svcf_t        vcf1,
                    vcf2;
    float    *busa_level;
    float    *busa_pan;
    float    *busb_level;
    float    *busb_pan;
    float    *vcf1_level;
    float    *vcf1_pan;
    float    *vcf2_level;
    float    *vcf2_pan;
    float    *volume;
    float    *effect_mode;
    float    *effect_param1;
    float    *effect_param2;
    float    *effect_param3;
    float    *effect_param4;
    float    *effect_param5;
    float    *effect_param6;
    float    *effect_mix;
    float    *glide_time;
    float    *bend_range;
    y_slfo_t        glfo,
                    vlfo,
                    mlfo;
    float    *mlfo_phase_spread;
    float    *mlfo_random_freq;
    y_seg_t         ego,
                    eg1,
                    eg2,
                    eg3,
                    eg4;
    float    *modmix_bias;
    float    *modmix_mod1_src;
    float    *modmix_mod1_amt;
    float    *modmix_mod2_src;
    float    *modmix_mod2_amt;
    float    *tuning;

    /* reusable pre-mixdown voice buffers */
    float           vcf1_out[Y_CONTROL_PERIOD],
                    vcf2_out[Y_CONTROL_PERIOD];

    /* effects */
    float     voice_bus_l[Y_CONTROL_PERIOD],  /* pre-effect voice bus */
                    voice_bus_r[Y_CONTROL_PERIOD];
    int             last_effect_mode;
    float           dc_block_r,
                    dc_block_l_xnm1,
                    dc_block_l_ynm1,
                    dc_block_r_xnm1,
                    dc_block_r_ynm1;
    unsigned int    nonfinite_recoveries;     /* times the run loop had to reset after a non-finite sample */
    char           *effect_buffer;
    size_t          effect_buffer_allocation;
    size_t          effect_buffer_highwater;
    size_t          effect_buffer_silence_count;
};

/*
 * y_global_t
 */
struct _y_global_t {
    int                    initialized;
    int                    instance_count;
    unsigned long          sample_rate;


    pthread_mutex_t        sampleset_mutex;
    pthread_mutex_t        signal_mutex;      /* worker thread wakeup */
    pthread_cond_t         signal_cond;
    int                    signal_pending;
    int                    worker_thread_started;
    volatile int           worker_thread_done;
    pthread_t              worker_thread;
    int                    samplesets_allocated;
    y_sampleset_t         *active_sampleset_list;
    y_sampleset_t         *free_sampleset_list;
    int                    samples_allocated;
    y_sample_t            *active_sample_list;
    y_sample_t            *free_sample_list;
    int                    padsynth_table_size;
    float                 *padsynth_inbuf;
    float                 *padsynth_outfreqs;
    float                 *padsynth_outsamples;
    void                  *padsynth_fft_plan;
    void                  *padsynth_ifft_plan;
};

extern y_global_t global;

void  y_synth_all_voices_off(y_synth_t *synth);
void  y_synth_note_off(y_synth_t *synth, unsigned char key,
                            unsigned char rvelocity);
void  y_synth_all_notes_off(y_synth_t *synth);
void  y_synth_note_on(y_synth_t *synth, unsigned char key,
                           unsigned char velocity);
void  y_synth_key_pressure(y_synth_t *synth, unsigned char key,
                                unsigned char pressure);
void  y_synth_damp_voices(y_synth_t *synth);
void  y_synth_update_wheel_mod(y_synth_t *synth);
void  y_synth_control_change(y_synth_t *synth, unsigned int param,
                                  signed int value);
void  y_synth_channel_pressure(y_synth_t *synth, signed int pressure);
void  y_synth_pitch_bend(y_synth_t *synth, signed int value);
void  y_synth_init_controls(y_synth_t *synth);
void  y_synth_select_patch(y_synth_t *synth, unsigned long patch);
const char *y_synth_get_patch_name(y_synth_t *synth, unsigned long patch);
char *y_synth_handle_load(y_synth_t *synth, const char *value);
char *y_synth_handle_polyphony(y_synth_t *synth, const char *value);
char *y_synth_handle_monophonic(y_synth_t *synth, const char *value);
char *y_synth_handle_glide(y_synth_t *synth, const char *value);
char *y_synth_handle_program_cancel(y_synth_t *synth, const char *value);
char *y_synth_handle_project_dir(y_synth_t *synth, const char *value);
void  y_synth_render_voices(y_synth_t *synth, float *out_left,
                                 float *out_right, unsigned long sample_count,
                                 int do_control_update);

/* these come right out of alsa/asoundef.h */
#define MIDI_CTL_MSB_MODWHEEL           0x01    /**< Modulation */
#define MIDI_CTL_MSB_PORTAMENTO_TIME    0x05    /**< Portamento time */
#define MIDI_CTL_MSB_MAIN_VOLUME        0x07    /**< Main volume */
#define MIDI_CTL_MSB_BALANCE            0x08    /**< Balance */
#define MIDI_CTL_MSB_PAN                0x0a    /**< Panpot */
#define MIDI_CTL_LSB_MODWHEEL           0x21    /**< Modulation */
#define MIDI_CTL_LSB_PORTAMENTO_TIME    0x25    /**< Portamento time */
#define MIDI_CTL_LSB_MAIN_VOLUME        0x27    /**< Main volume */
#define MIDI_CTL_LSB_BALANCE            0x28    /**< Balance */
#define MIDI_CTL_LSB_PAN                0x2a    /**< Panpot */
#define MIDI_CTL_SUSTAIN                0x40    /**< Sustain pedal */
#define MIDI_CTL_ALL_SOUNDS_OFF         0x78    /**< All sounds off */
#define MIDI_CTL_RESET_CONTROLLERS      0x79    /**< Reset Controllers */
#define MIDI_CTL_ALL_NOTES_OFF          0x7b    /**< All notes off */

#define Y_SYNTH_SUSTAINED(_s)  ((_s)->cc[MIDI_CTL_SUSTAIN] >= 64)

#endif /* _DSSP_EVENT_H */

