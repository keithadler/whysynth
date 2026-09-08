/* WhySynth - LV2 plugin
 *
 * Copyright (C) 2026 Keith Adler.
 * WhySynth is copyright (C) 2004-2017 Sean Bolton and others.
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

/* The LV2 port map is the LADSPA one shifted by two: LV2 port 0 is the MIDI
 * atom input, port 1 the notify output, then audio out left and right, then
 * the 196 control ports in Y_PORT_* order, then polyphony, voice mode and
 * glide mode. Patches arrive as LV2 presets (generated from the factory
 * bank) or as host-saved control values, which is how LV2 likes it. */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#include <lv2/core/lv2.h>
#include <lv2/atom/atom.h>
#include <lv2/atom/util.h>
#include <lv2/midi/midi.h>
#include <lv2/urid/urid.h>

#include "whysynth_types.h"
#include "whysynth.h"
#include "whysynth_ports.h"
#include "dssp_event.h"
#include "whysynth_core.h"
#include "whysynth_lv2_ports.h"

#define WHYSYNTH_URI "https://github.com/keithadler/whysynth"
#define MAX_EVENTS   4096

typedef struct {
    y_synth_t               *synth;
    const LV2_Atom_Sequence *control;
    LV2_Atom_Sequence       *notify;
    const float             *polyphony, *mono_mode, *glide_mode;
    LV2_URID                 midi_event;
    int                      last_polyphony, last_mono, last_glide;
    y_event_t                events[MAX_EVENTS];
} whysynth_lv2_t;

static LV2_Handle
instantiate(const LV2_Descriptor *descriptor, double rate, const char *bundle_path,
            const LV2_Feature *const *features)
{
    whysynth_lv2_t *h = (whysynth_lv2_t *)calloc(1, sizeof(whysynth_lv2_t));
    LV2_URID_Map *map = NULL;
    const LV2_Feature *const *f;

    if (!h) return NULL;
    for (f = features; *f; f++)
        if (!strcmp((*f)->URI, LV2_URID__map)) map = (LV2_URID_Map *)(*f)->data;
    if (!map) {
        fprintf(stderr, "WhySynth.lv2: host does not provide urid:map\n");
        free(h);
        return NULL;
    }
    h->midi_event = map->map(map->handle, LV2_MIDI__MidiEvent);

    y_synth_static_init();
    h->synth = y_synth_new((unsigned long)(rate + 0.5));
    if (!h->synth) {
        fprintf(stderr, "WhySynth.lv2: could not create the synth\n");
        free(h);
        return NULL;
    }
    h->last_polyphony = h->synth->polyphony;
    h->last_mono = h->synth->monophonic;
    h->last_glide = h->synth->glide;
    return (LV2_Handle)h;
}

static void
connect_port(LV2_Handle instance, uint32_t port, void *data)
{
    whysynth_lv2_t *h = (whysynth_lv2_t *)instance;

    switch (port) {
      case LV2_PORT_CONTROL:   h->control = (const LV2_Atom_Sequence *)data; return;
      case LV2_PORT_NOTIFY:    h->notify = (LV2_Atom_Sequence *)data; return;
      case LV2_PORT_OUT_LEFT:  y_synth_connect_port(h->synth, Y_PORT_OUTPUT_LEFT, (float *)data); return;
      case LV2_PORT_OUT_RIGHT: y_synth_connect_port(h->synth, Y_PORT_OUTPUT_RIGHT, (float *)data); return;
      case LV2_PORT_POLYPHONY: h->polyphony = (const float *)data; return;
      case LV2_PORT_MONO_MODE: h->mono_mode = (const float *)data; return;
      case LV2_PORT_GLIDE_MODE: h->glide_mode = (const float *)data; return;
      default: break;
    }
    if (port >= LV2_PORT_FIRST_PARAM && port < LV2_PORT_FIRST_PARAM + WHYSYNTH_LV2_PARAM_COUNT)
        y_synth_connect_port(h->synth, port - LV2_PORT_FIRST_PARAM + Y_PORT_OSC1_MODE, (float *)data);
}

static void
activate(LV2_Handle instance)
{
    y_synth_activate(((whysynth_lv2_t *)instance)->synth);
}

static void
run(LV2_Handle instance, uint32_t nframes)
{
    whysynth_lv2_t *h = (whysynth_lv2_t *)instance;
    uint32_t ne = 0;
    y_event_t ev;

    if (h->notify) {
        /* nothing to say yet; keep the sequence valid and empty */
        h->notify->atom.size = sizeof(LV2_Atom_Sequence_Body);
    }

    if (h->polyphony) {
        int p = (int)lrintf(*h->polyphony);
        if (p != h->last_polyphony && ne < MAX_EVENTS) {
            h->last_polyphony = p;
            memset(&ev, 0, sizeof(ev)); ev.type = Y_EV_POLYPHONY; ev.value = p;
            h->events[ne++] = ev;
        }
    }
    if (h->mono_mode) {
        int m = (int)lrintf(*h->mono_mode);
        if (m != h->last_mono && ne < MAX_EVENTS) {
            h->last_mono = m;
            memset(&ev, 0, sizeof(ev)); ev.type = Y_EV_MONO_MODE; ev.value = m;
            h->events[ne++] = ev;
        }
    }
    if (h->glide_mode) {
        int g = (int)lrintf(*h->glide_mode);
        if (g != h->last_glide && ne < MAX_EVENTS) {
            h->last_glide = g;
            memset(&ev, 0, sizeof(ev)); ev.type = Y_EV_GLIDE_MODE; ev.value = g;
            h->events[ne++] = ev;
        }
    }

    if (h->control) {
        LV2_ATOM_SEQUENCE_FOREACH(h->control, a) {
            if (a->body.type == h->midi_event && ne < MAX_EVENTS) {
                const uint8_t *msg = (const uint8_t *)LV2_ATOM_BODY_CONST(&a->body);
                uint32_t frame = (uint32_t)a->time.frames;
                if (frame > nframes) frame = nframes;
                memset(&ev, 0, sizeof(ev));
                ev.frame = frame;
                if (a->body.size < 2) continue;
                switch (msg[0] & 0xF0) {
                  case 0x80: if (a->body.size < 3) continue; ev.type = Y_EV_NOTE_OFF; ev.a = msg[1] & 0x7F; ev.b = msg[2] & 0x7F; break;
                  case 0x90: if (a->body.size < 3) continue; ev.type = Y_EV_NOTE_ON; ev.a = msg[1] & 0x7F; ev.b = msg[2] & 0x7F; break;
                  case 0xA0: if (a->body.size < 3) continue; ev.type = Y_EV_KEY_PRESSURE; ev.a = msg[1] & 0x7F; ev.b = msg[2] & 0x7F; break;
                  case 0xB0: if (a->body.size < 3) continue; ev.type = Y_EV_CONTROL_CHANGE; ev.a = msg[1] & 0x7F; ev.b = msg[2] & 0x7F; break;
                  case 0xD0: ev.type = Y_EV_CHANNEL_PRESSURE; ev.a = msg[1] & 0x7F; break;
                  case 0xE0: if (a->body.size < 3) continue; ev.type = Y_EV_PITCH_BEND;
                             ev.value = (int32_t)(((msg[2] & 0x7F) << 7) | (msg[1] & 0x7F)) - 8192; break;
                  default: continue;   /* program changes are the host's business in LV2 */
                }
                h->events[ne++] = ev;
            }
        }
    }

    y_synth_run(h->synth, nframes, h->events, ne);
}

static void
deactivate(LV2_Handle instance)
{
    y_synth_deactivate(((whysynth_lv2_t *)instance)->synth);
}

static void
cleanup(LV2_Handle instance)
{
    whysynth_lv2_t *h = (whysynth_lv2_t *)instance;
    if (h->synth) y_synth_free(h->synth);
    free(h);
}

static const void *
extension_data(const char *uri)
{
    return NULL;
}

static const LV2_Descriptor descriptor = {
    WHYSYNTH_URI, instantiate, connect_port, activate, run, deactivate, cleanup, extension_data
};

LV2_SYMBOL_EXPORT const LV2_Descriptor *
lv2_descriptor(uint32_t index)
{
    return index == 0 ? &descriptor : NULL;
}
