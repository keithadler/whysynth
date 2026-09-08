/* DSSI Plugin Framework
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

#include <ladspa.h>
#include <dssi.h>

#include "whysynth_types.h"
#include "whysynth.h"
#include "whysynth_ports.h"
#include "dssp_event.h"
#include "common_data.h"
#include "whysynth_voice.h"
#include "wave_tables.h"
#include "whysynth_core.h"

/* This file is the DSSI plugin. Everything it does is one call into
 * whysynth_core.c, which the CLAP and LV2 plugins share. */

static LADSPA_Descriptor *y_LADSPA_descriptor = NULL;
static DSSI_Descriptor   *y_DSSI_descriptor = NULL;

/* ---- LADSPA interface ---- */

static LADSPA_Handle
y_instantiate(const LADSPA_Descriptor *descriptor, unsigned long sample_rate)
{
    return (LADSPA_Handle)y_synth_new(sample_rate);
}

static void
y_connect_port(LADSPA_Handle instance, unsigned long port, LADSPA_Data *data)
{
    y_synth_connect_port((y_synth_t *)instance, port, data);
}

static void
y_activate(LADSPA_Handle instance)
{
    y_synth_activate((y_synth_t *)instance);
}

static void
y_run_synth(LADSPA_Handle instance, unsigned long sample_count,
            snd_seq_event_t *events, unsigned long event_count);

static void
y_ladspa_run_wrapper(LADSPA_Handle instance, unsigned long sample_count)
{
    y_run_synth(instance, sample_count, NULL, 0);
}

static void
y_deactivate(LADSPA_Handle instance)
{
    y_synth_deactivate((y_synth_t *)instance);
}

static void
y_cleanup(LADSPA_Handle instance)
{
    y_synth_free((y_synth_t *)instance);
}

/* ---- DSSI interface ---- */

static char *
y_configure(LADSPA_Handle instance, const char *key, const char *value)
{
    YDB_MESSAGE(YDB_DSSI, " y_configure called with '%s' and '%s'\n", key, value);

    if (!strcmp(key, "load")) {
        return y_synth_handle_load((y_synth_t *)instance, value);
    } else if (!strcmp(key, "polyphony")) {
        return y_synth_handle_polyphony((y_synth_t *)instance, value);
    } else if (!strcmp(key, "monophonic")) {
        return y_synth_handle_monophonic((y_synth_t *)instance, value);
    } else if (!strcmp(key, "glide")) {
        return y_synth_handle_glide((y_synth_t *)instance, value);
    } else if (!strcmp(key, "program_cancel")) {
        return y_synth_handle_program_cancel((y_synth_t *)instance, value);
    } else if (!strcmp(key, DSSI_PROJECT_DIRECTORY_KEY)) {
        return y_synth_handle_project_dir((y_synth_t *)instance, value);
    }
    return strdup("error: unrecognized configure key");
}

static const DSSI_Program_Descriptor *
y_get_program(LADSPA_Handle instance, unsigned long index)
{
    y_synth_t *synth = (y_synth_t *)instance;
    static DSSI_Program_Descriptor pd;
    const char *name = y_synth_get_patch_name(synth, index);

    if (!name) return NULL;
    pd.Bank = index / 128;
    pd.Program = index % 128;
    pd.Name = name;
    return &pd;
}

static void
y_select_program(LADSPA_Handle handle, unsigned long bank, unsigned long program)
{
    if (program >= 128) return;
    y_synth_request_patch((y_synth_t *)handle, bank * 128 + program);
}

static int
y_get_midi_controller(LADSPA_Handle instance, unsigned long port)
{
    switch (port) {
      case Y_PORT_GLIDE_TIME:
        return DSSI_CC(MIDI_CTL_MSB_PORTAMENTO_TIME);
      default:
        break;
    }
    return DSSI_NONE;
}

#define Y_DSSI_MAX_EVENTS 1024

static void
y_run_synth(LADSPA_Handle instance, unsigned long sample_count,
            snd_seq_event_t *events, unsigned long event_count)
{
    y_event_t ev[Y_DSSI_MAX_EVENTS];
    unsigned long i, n = 0;

    for (i = 0; i < event_count && n < Y_DSSI_MAX_EVENTS; i++) {
        snd_seq_event_t *e = &events[i];
        y_event_t *y = &ev[n];
        y->frame = e->time.tick;
        y->a = y->b = 0;
        y->value = 0;
        switch (e->type) {
          case SND_SEQ_EVENT_NOTEOFF:
            y->type = Y_EV_NOTE_OFF; y->a = e->data.note.note; y->b = e->data.note.velocity; break;
          case SND_SEQ_EVENT_NOTEON:
            y->type = Y_EV_NOTE_ON; y->a = e->data.note.note; y->b = e->data.note.velocity; break;
          case SND_SEQ_EVENT_KEYPRESS:
            y->type = Y_EV_KEY_PRESSURE; y->a = e->data.note.note; y->b = e->data.note.velocity; break;
          case SND_SEQ_EVENT_CONTROLLER:
            y->type = Y_EV_CONTROL_CHANGE; y->a = e->data.control.param; y->b = e->data.control.value; break;
          case SND_SEQ_EVENT_CHANPRESS:
            y->type = Y_EV_CHANNEL_PRESSURE; y->a = e->data.control.value; break;
          case SND_SEQ_EVENT_PITCHBEND:
            y->type = Y_EV_PITCH_BEND; y->value = e->data.control.value; break;
          default:
            continue;
        }
        n++;
    }
    y_synth_run((y_synth_t *)instance, sample_count, ev, n);
}

/* ---- export ---- */

const LADSPA_Descriptor *ladspa_descriptor(unsigned long index)
{
    return index == 0 ? y_LADSPA_descriptor : NULL;
}

const DSSI_Descriptor *dssi_descriptor(unsigned long index)
{
    return index == 0 ? y_DSSI_descriptor : NULL;
}

#ifdef __GNUC__
__attribute__((constructor)) void init()
#else
void _init()
#endif
{
    int i;
    char **port_names;
    LADSPA_PortDescriptor *port_descriptors;
    LADSPA_PortRangeHint *port_range_hints;

    y_synth_static_init();

    y_LADSPA_descriptor = (LADSPA_Descriptor *) malloc(sizeof(LADSPA_Descriptor));
    if (y_LADSPA_descriptor) {
        y_LADSPA_descriptor->UniqueID = 2187;
        y_LADSPA_descriptor->Label = "WhySynth";
        y_LADSPA_descriptor->Properties = 0;
        y_LADSPA_descriptor->Name = "WhySynth " VERSION " DSSI plugin";
        y_LADSPA_descriptor->Maker = "Sean Bolton <whysynth AT smbolton DOT com>";
        y_LADSPA_descriptor->Copyright = "GNU General Public License version 2 or later";
        y_LADSPA_descriptor->PortCount = Y_PORTS_COUNT;

        port_descriptors = (LADSPA_PortDescriptor *) calloc(Y_PORTS_COUNT, sizeof(LADSPA_PortDescriptor));
        y_LADSPA_descriptor->PortDescriptors = (const LADSPA_PortDescriptor *) port_descriptors;
        port_range_hints = (LADSPA_PortRangeHint *) calloc(Y_PORTS_COUNT, sizeof(LADSPA_PortRangeHint));
        y_LADSPA_descriptor->PortRangeHints = (const LADSPA_PortRangeHint *) port_range_hints;
        port_names = (char **) calloc(Y_PORTS_COUNT, sizeof(char *));
        y_LADSPA_descriptor->PortNames = (const char **) port_names;

        for (i = 0; i < Y_PORTS_COUNT; i++) {
            port_descriptors[i] = y_port_description[i].port_descriptor;
            port_names[i]       = y_port_description[i].name;
            port_range_hints[i].HintDescriptor = y_port_description[i].hint_descriptor;
            port_range_hints[i].LowerBound     = y_port_description[i].lower_bound;
            if (y_port_description[i].type == Y_PORT_TYPE_COMBO &&
                (y_port_description[i].subtype == Y_COMBO_TYPE_OSC_WAVEFORM ||
                 y_port_description[i].subtype == Y_COMBO_TYPE_WT_WAVEFORM)) {
                port_range_hints[i].UpperBound = (float)wavetables_count - 1;
            } else {
                port_range_hints[i].UpperBound = y_port_description[i].upper_bound;
            }
        }

        y_LADSPA_descriptor->instantiate = y_instantiate;
        y_LADSPA_descriptor->connect_port = y_connect_port;
        y_LADSPA_descriptor->activate = y_activate;
        y_LADSPA_descriptor->run = y_ladspa_run_wrapper;
        y_LADSPA_descriptor->run_adding = NULL;
        y_LADSPA_descriptor->set_run_adding_gain = NULL;
        y_LADSPA_descriptor->deactivate = y_deactivate;
        y_LADSPA_descriptor->cleanup = y_cleanup;
    }

    y_DSSI_descriptor = (DSSI_Descriptor *) malloc(sizeof(DSSI_Descriptor));
    if (y_DSSI_descriptor) {
        y_DSSI_descriptor->DSSI_API_Version = 1;
        y_DSSI_descriptor->LADSPA_Plugin = y_LADSPA_descriptor;
        y_DSSI_descriptor->configure = y_configure;
        y_DSSI_descriptor->get_program = y_get_program;
        y_DSSI_descriptor->select_program = y_select_program;
        y_DSSI_descriptor->get_midi_controller_for_port = y_get_midi_controller;
        y_DSSI_descriptor->run_synth = y_run_synth;
        y_DSSI_descriptor->run_synth_adding = NULL;
        y_DSSI_descriptor->run_multiple_synths = NULL;
        y_DSSI_descriptor->run_multiple_synths_adding = NULL;
    }
}

#ifdef __GNUC__
__attribute__((destructor)) void fini()
#else
void _fini()
#endif
{
    if (y_LADSPA_descriptor) {
        free((LADSPA_PortDescriptor *) y_LADSPA_descriptor->PortDescriptors);
        free((char **) y_LADSPA_descriptor->PortNames);
        free((LADSPA_PortRangeHint *) y_LADSPA_descriptor->PortRangeHints);
        free(y_LADSPA_descriptor);
    }
    if (y_DSSI_descriptor) free(y_DSSI_descriptor);
}
