/* WhySynth - the synthesizer core, with no plugin API attached
 *
 * Copyright (C) 2004-2017 Sean Bolton and others.
 * Copyright (C) 2026 Keith Adler.
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

/* What used to be the LADSPA half of dssp_synth.c: instance lifetime, port
 * connection, and the run loop, expressed without LADSPA. The DSSI plugin,
 * the CLAP and LV2 plugins and the tools all sit on this. */

#ifndef _WHYSYNTH_CORE_H
#define _WHYSYNTH_CORE_H

#include <stdint.h>

#include "whysynth_types.h"

/* events, sorted by frame, handed to y_synth_run() */
enum {
    Y_EV_NONE = 0,
    Y_EV_NOTE_ON,           /* a = key, b = velocity (0 acts as note off) */
    Y_EV_NOTE_OFF,          /* a = key, b = release velocity */
    Y_EV_KEY_PRESSURE,      /* a = key, b = pressure */
    Y_EV_CONTROL_CHANGE,    /* a = controller, b = value */
    Y_EV_CHANNEL_PRESSURE,  /* a = pressure */
    Y_EV_PITCH_BEND,        /* value = -8192 .. 8191 */
    Y_EV_PROGRAM_CHANGE,    /* value = patch index */
    Y_EV_POLYPHONY,         /* value = 1..64 */
    Y_EV_MONO_MODE,         /* value = Y_MONO_MODE_* */
    Y_EV_GLIDE_MODE         /* value = Y_GLIDE_MODE_* */
};

typedef struct {
    uint32_t frame;
    uint16_t type;
    uint8_t  a, b;
    int32_t  value;
} y_event_t;

/* once per process, before anything else (idempotent) */
void       y_synth_static_init(void);

/* all live instances share one sample rate; returns NULL if a different
 * rate is asked for while others exist, or on allocation failure */
y_synth_t *y_synth_new(unsigned long sample_rate);
void       y_synth_free(y_synth_t *synth);

/* audio outputs and the 196 control ports are host-owned memory, LADSPA
 * style; port indices are the Y_PORT_* values */
void       y_synth_connect_port(y_synth_t *synth, unsigned long port, float *data);

void       y_synth_activate(y_synth_t *synth);
void       y_synth_deactivate(y_synth_t *synth);

/* render into the connected output ports, applying events at their frames;
 * silence if the voice list is busy (a main-thread change in progress) */
void       y_synth_run(y_synth_t *synth, unsigned long sample_count,
                       const y_event_t *events, unsigned long event_count);

/* the mutex helpers and configure-message formatter the rest of the
 * engine already uses */
int        dssp_voicelist_mutex_lock(y_synth_t *synth);
int        dssp_voicelist_mutex_unlock(y_synth_t *synth);
char      *dssi_configure_message(const char *fmt, ...);

/* select a patch from any thread: immediate if the bank is free, else at
 * the next run() */
void       y_synth_request_patch(y_synth_t *synth, unsigned long patch);

#endif /* _WHYSYNTH_CORE_H */
