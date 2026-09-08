/* WhySynth - host-independent engine API
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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include <pthread.h>

#include "whysynth_types.h"
#include "whysynth.h"
#include "whysynth_ports.h"
#include "dssp_event.h"
#include "common_data.h"
#include "whysynth_voice.h"
#include "wave_tables.h"
#include "whysynth_core.h"
#include "whysynth_names.h"
#include "whysynth_engine.h"

struct _whysynth_engine_t {
    y_synth_t *synth;
    float      ports[WHYSYNTH_PORT_COUNT];
    int        program;
};

/* ---- parameter descriptions ---- */

static whysynth_param_info_t param_info[WHYSYNTH_PORT_COUNT];
static int param_info_ready = 0;

static float
default_for(const struct y_port_descriptor *d, float lo, float hi)
{
    switch (d->hint_descriptor & LADSPA_HINT_DEFAULT_MASK) {
      case LADSPA_HINT_DEFAULT_MINIMUM: return lo;
      case LADSPA_HINT_DEFAULT_LOW:     return (d->hint_descriptor & LADSPA_HINT_LOGARITHMIC) ? expf(logf(lo) * 0.75f + logf(hi) * 0.25f) : lo * 0.75f + hi * 0.25f;
      case LADSPA_HINT_DEFAULT_MIDDLE:  return (d->hint_descriptor & LADSPA_HINT_LOGARITHMIC) ? sqrtf(lo * hi) : (lo + hi) * 0.5f;
      case LADSPA_HINT_DEFAULT_HIGH:    return (d->hint_descriptor & LADSPA_HINT_LOGARITHMIC) ? expf(logf(lo) * 0.25f + logf(hi) * 0.75f) : lo * 0.25f + hi * 0.75f;
      case LADSPA_HINT_DEFAULT_MAXIMUM: return hi;
      case LADSPA_HINT_DEFAULT_0:       return 0.0f;
      case LADSPA_HINT_DEFAULT_1:       return 1.0f;
      case LADSPA_HINT_DEFAULT_100:     return 100.0f;
      case LADSPA_HINT_DEFAULT_440:     return 440.0f;
      default:                          return lo;
    }
}

static void
build_param_info(void)
{
    int i;

    if (param_info_ready) return;
    y_synth_static_init();
    for (i = 0; i < WHYSYNTH_PORT_COUNT; i++) {
        const struct y_port_descriptor *d = &y_port_description[i];
        whysynth_param_info_t *p = &param_info[i];
        p->name = d->name;
        p->min = d->lower_bound;
        if (d->type == Y_PORT_TYPE_COMBO &&
            (d->subtype == Y_COMBO_TYPE_OSC_WAVEFORM || d->subtype == Y_COMBO_TYPE_WT_WAVEFORM))
            p->max = (float)wavetables_count - 1;
        else
            p->max = d->upper_bound;
        p->kind = d->type;
        p->combo_type = d->type == Y_PORT_TYPE_COMBO ? d->subtype : -1;
        p->is_integer = (d->type == Y_PORT_TYPE_COMBO || d->type == Y_PORT_TYPE_INTEGER ||
                         d->type == Y_PORT_TYPE_BOOLEAN || (d->hint_descriptor & LADSPA_HINT_INTEGER)) ? 1 : 0;
        p->is_output = (d->port_descriptor & LADSPA_PORT_OUTPUT) ? 1 : 0;
        p->def = p->is_output ? 0.0f : default_for(d, p->min, p->max);
    }
    param_info_ready = 1;
}

const whysynth_param_info_t *
whysynth_param_info(int port)
{
    build_param_info();
    if (port < 0 || port >= WHYSYNTH_PORT_COUNT) return NULL;
    return &param_info[port];
}

const char *
whysynth_param_value_name(int port, int value)
{
    const whysynth_param_info_t *p = whysynth_param_info(port);
    if (!p || p->kind != WHYSYNTH_KIND_COMBO) return NULL;
    return whysynth_combo_value_name(p->combo_type, value);
}

void
whysynth_param_symbol(int port, char *buf, size_t size)
{
    const whysynth_param_info_t *p = whysynth_param_info(port);
    size_t n = 0, i;
    int last_us = 1;

    if (!p || size == 0) { if (size) buf[0] = 0; return; }
    for (i = 0; p->name[i] && n + 1 < size; i++) {
        unsigned char c = (unsigned char)p->name[i];
        if (isalnum(c)) {
            buf[n++] = (char)tolower(c);
            last_us = 0;
        } else if (c == '-' && p->name[i + 1] == '>') {
            if (!last_us && n + 1 < size) buf[n++] = '_';
            if (n + 3 < size) { memcpy(buf + n, "to", 2); n += 2; }
            last_us = 0;
            i++;
        } else if (!last_us) {
            buf[n++] = '_';
            last_us = 1;
        }
    }
    while (n > 0 && buf[n - 1] == '_') n--;
    buf[n] = 0;
}

/* ---- lifetime ---- */

whysynth_engine_t *
whysynth_engine_new(float sample_rate)
{
    whysynth_engine_t *e;
    int i;

    if (sample_rate < 1000.0f) return NULL;
    build_param_info();

    e = (whysynth_engine_t *)calloc(1, sizeof(whysynth_engine_t));
    if (!e) return NULL;
    e->synth = y_synth_new((unsigned long)lrintf(sample_rate));
    if (!e->synth) {
        free(e);
        return NULL;
    }
    for (i = WHYSYNTH_PORT_FIRST_PARAM; i < WHYSYNTH_PORT_COUNT; i++) {
        e->ports[i] = param_info[i].def;
        y_synth_connect_port(e->synth, i, &e->ports[i]);
    }
    e->program = -1;
    /* the DSSI plugin started on patch 0 through the host; do the same */
    if (e->synth->patch_count > 0) {
        y_synth_select_patch(e->synth, 0);
        e->program = 0;
    }
    y_synth_activate(e->synth);
    return e;
}

void
whysynth_engine_free(whysynth_engine_t *e)
{
    if (!e) return;
    if (e->synth) y_synth_free(e->synth);
    free(e);
}

void
whysynth_engine_reset(whysynth_engine_t *e)
{
    y_synth_activate(e->synth);
}

float
whysynth_engine_get_sample_rate(const whysynth_engine_t *e)
{
    return e->synth->sample_rate;
}

/* ---- parameters ---- */

float
whysynth_engine_get_param(const whysynth_engine_t *e, int port)
{
    if (port < 0 || port >= WHYSYNTH_PORT_COUNT) return 0.0f;
    return e->ports[port];
}

void
whysynth_engine_set_param(whysynth_engine_t *e, int port, float value)
{
    const whysynth_param_info_t *p;

    if (port < WHYSYNTH_PORT_FIRST_PARAM || port >= WHYSYNTH_PORT_COUNT) return;
    p = &param_info[port];
    if (value != value) value = p->def;
    if (value < p->min) value = p->min;
    if (value > p->max) value = p->max;
    if (p->is_integer) value = (float)lrintf(value);
    e->ports[port] = value;
}

void
whysynth_engine_get_params(const whysynth_engine_t *e, float *out)
{
    memcpy(out, e->ports, sizeof(e->ports));
}

/* ---- voice settings ---- */

int
whysynth_engine_set_polyphony(whysynth_engine_t *e, int voices)
{
    char buf[16];
    char *err;

    if (voices < 1) voices = 1;
    if (voices > WHYSYNTH_MAX_POLYPHONY) voices = WHYSYNTH_MAX_POLYPHONY;
    snprintf(buf, sizeof(buf), "%d", voices);
    err = y_synth_handle_polyphony(e->synth, buf);
    free(err);
    return e->synth->polyphony;
}

int
whysynth_engine_get_polyphony(const whysynth_engine_t *e)
{
    return e->synth->polyphony;
}

int
whysynth_engine_set_mono_mode(whysynth_engine_t *e, int mode)
{
    static const char *names[4] = { "off", "on", "once", "both" };
    char *err;

    if (mode < 0) mode = 0;
    if (mode > 3) mode = 3;
    err = y_synth_handle_monophonic(e->synth, names[mode]);
    free(err);
    return e->synth->monophonic;
}

int
whysynth_engine_get_mono_mode(const whysynth_engine_t *e)
{
    return e->synth->monophonic;
}

int
whysynth_engine_set_glide_mode(whysynth_engine_t *e, int mode)
{
    static const char *names[5] = { "legato", "initial", "always", "leftover", "off" };
    char *err;

    if (mode < 0) mode = 0;
    if (mode > 4) mode = 4;
    err = y_synth_handle_glide(e->synth, names[mode]);
    free(err);
    return e->synth->glide;
}

int
whysynth_engine_get_glide_mode(const whysynth_engine_t *e)
{
    return e->synth->glide;
}

void
whysynth_engine_set_program_cancel(whysynth_engine_t *e, int on)
{
    e->synth->program_cancel = on ? 1 : 0;
}

int
whysynth_engine_get_program_cancel(const whysynth_engine_t *e)
{
    return e->synth->program_cancel;
}

int
whysynth_engine_get_active_voices(const whysynth_engine_t *e)
{
    int i, n = 0;
    for (i = 0; i < e->synth->voices; i++)
        if (_PLAYING(e->synth->voice[i])) n++;
    return n;
}

/* ---- the patch bank ---- */

int
whysynth_engine_patch_count(const whysynth_engine_t *e)
{
    return (int)e->synth->patch_count;
}

const char *
whysynth_engine_patch_name(const whysynth_engine_t *e, int index)
{
    if (index < 0 || (unsigned int)index >= e->synth->patch_count) return NULL;
    return e->synth->patches[index].name;
}

int
whysynth_engine_get_program(const whysynth_engine_t *e)
{
    return e->program;
}

void
whysynth_engine_select_program(whysynth_engine_t *e, int index)
{
    if (index < 0 || (unsigned int)index >= e->synth->patch_count) return;
    e->program = index;
    y_synth_request_patch(e->synth, (unsigned long)index);
}

/* read patches from a reader into the bank at slot 0 (the DSSI 'load'
 * semantics); returns the count read */
static int
load_patches(whysynth_engine_t *e, y_reader_t *r, char **errmsg)
{
    y_synth_t *synth = e->synth;
    int count = 0;

    pthread_mutex_lock(&synth->patches_mutex);
    while (1) {
        y_data_check_patches_allocation(synth, count);
        if (!y_data_read_patch_r(r, &synth->patches[count]))
            break;
        count++;
    }
    if (count > (int)synth->patch_count)
        synth->patch_count = count;
    pthread_mutex_unlock(&synth->patches_mutex);

    if (!count && errmsg)
        *errmsg = strdup("no patches recognized in patch data");
    return count;
}

int
whysynth_engine_load_patches_file(whysynth_engine_t *e, const char *path, char **errmsg)
{
    FILE *fh;
    y_reader_t r;
    int count;

    if (errmsg) *errmsg = NULL;
    if ((fh = fopen(path, "rb")) == NULL) {
        if (errmsg) *errmsg = dssi_configure_message("could not open file '%s'", path);
        return 0;
    }
    y_reader_init_file(&r, fh);
    count = load_patches(e, &r, errmsg);
    fclose(fh);
    return count;
}

int
whysynth_engine_load_patches_memory(whysynth_engine_t *e, const char *data, size_t size, char **errmsg)
{
    y_reader_t r;
    y_memreader_t m;

    if (errmsg) *errmsg = NULL;
    y_reader_init_memory(&r, &m, data, size);
    return load_patches(e, &r, errmsg);
}

int
whysynth_engine_patch_text(const whysynth_engine_t *e, int index, char *buf, size_t size)
{
    if (index < 0 || (unsigned int)index >= e->synth->patch_count) return -1;
    return y_data_patch_to_text(&e->synth->patches[index], buf, size);
}

/* the inverse of y_voice_set_ports(): the current parameters as a patch */
static void
patch_from_ports(const whysynth_engine_t *e, y_patch_t *patch)
{
    const float *v = e->ports;
#define I(port) ((int)lrintf(v[port]))
#define F(port) (v[port])
#define OSC(o, base) do { \
        (o).mode = I(base + 0); (o).waveform = I(base + 1); (o).pitch = I(base + 2); \
        (o).detune = F(base + 3); (o).pitch_mod_src = I(base + 4); (o).pitch_mod_amt = F(base + 5); \
        (o).mparam1 = F(base + 6); (o).mparam2 = F(base + 7); (o).mmod_src = I(base + 8); \
        (o).mmod_amt = F(base + 9); (o).amp_mod_src = I(base + 10); (o).amp_mod_amt = F(base + 11); \
        (o).level_a = F(base + 12); (o).level_b = F(base + 13); } while (0)
#define VCF(f, base) do { \
        (f).mode = I(base + 0); (f).source = I(base + 1); (f).frequency = F(base + 2); \
        (f).freq_mod_src = I(base + 3); (f).freq_mod_amt = F(base + 4); (f).qres = F(base + 5); \
        (f).mparam = F(base + 6); } while (0)
#define EG(g, base) do { \
        (g).mode = I(base + 0); \
        (g).shape1 = I(base + 1); (g).time1 = F(base + 2); (g).level1 = F(base + 3); \
        (g).shape2 = I(base + 4); (g).time2 = F(base + 5); (g).level2 = F(base + 6); \
        (g).shape3 = I(base + 7); (g).time3 = F(base + 8); (g).level3 = F(base + 9); \
        (g).shape4 = I(base + 10); (g).time4 = F(base + 11); \
        (g).vel_level_sens = F(base + 12); (g).vel_time_scale = F(base + 13); (g).kbd_time_scale = F(base + 14); \
        (g).amp_mod_src = I(base + 15); (g).amp_mod_amt = F(base + 16); } while (0)

    memcpy(patch, &y_init_voice, sizeof(y_patch_t));
    OSC(patch->osc1, Y_PORT_OSC1_MODE);
    OSC(patch->osc2, Y_PORT_OSC2_MODE);
    OSC(patch->osc3, Y_PORT_OSC3_MODE);
    OSC(patch->osc4, Y_PORT_OSC4_MODE);
    VCF(patch->vcf1, Y_PORT_VCF1_MODE);
    VCF(patch->vcf2, Y_PORT_VCF2_MODE);
    patch->busa_level = F(Y_PORT_BUSA_LEVEL); patch->busa_pan = F(Y_PORT_BUSA_PAN);
    patch->busb_level = F(Y_PORT_BUSB_LEVEL); patch->busb_pan = F(Y_PORT_BUSB_PAN);
    patch->vcf1_level = F(Y_PORT_VCF1_LEVEL); patch->vcf1_pan = F(Y_PORT_VCF1_PAN);
    patch->vcf2_level = F(Y_PORT_VCF2_LEVEL); patch->vcf2_pan = F(Y_PORT_VCF2_PAN);
    patch->volume = F(Y_PORT_VOLUME);
    patch->effect_mode = I(Y_PORT_EFFECT_MODE);
    patch->effect_param1 = F(Y_PORT_EFFECT_PARAM1); patch->effect_param2 = F(Y_PORT_EFFECT_PARAM2);
    patch->effect_param3 = F(Y_PORT_EFFECT_PARAM3); patch->effect_param4 = F(Y_PORT_EFFECT_PARAM4);
    patch->effect_param5 = F(Y_PORT_EFFECT_PARAM5); patch->effect_param6 = F(Y_PORT_EFFECT_PARAM6);
    patch->effect_mix = F(Y_PORT_EFFECT_MIX);
    patch->glide_time = F(Y_PORT_GLIDE_TIME);
    patch->bend_range = I(Y_PORT_BEND_RANGE);
    patch->glfo.frequency = F(Y_PORT_GLFO_FREQUENCY); patch->glfo.waveform = I(Y_PORT_GLFO_WAVEFORM);
    patch->glfo.delay = 0.0f;
    patch->glfo.amp_mod_src = I(Y_PORT_GLFO_AMP_MOD_SRC); patch->glfo.amp_mod_amt = F(Y_PORT_GLFO_AMP_MOD_AMT);
    patch->vlfo.frequency = F(Y_PORT_VLFO_FREQUENCY); patch->vlfo.waveform = I(Y_PORT_VLFO_WAVEFORM);
    patch->vlfo.delay = F(Y_PORT_VLFO_DELAY);
    patch->vlfo.amp_mod_src = I(Y_PORT_VLFO_AMP_MOD_SRC); patch->vlfo.amp_mod_amt = F(Y_PORT_VLFO_AMP_MOD_AMT);
    patch->mlfo.frequency = F(Y_PORT_MLFO_FREQUENCY); patch->mlfo.waveform = I(Y_PORT_MLFO_WAVEFORM);
    patch->mlfo.delay = F(Y_PORT_MLFO_DELAY);
    patch->mlfo.amp_mod_src = I(Y_PORT_MLFO_AMP_MOD_SRC); patch->mlfo.amp_mod_amt = F(Y_PORT_MLFO_AMP_MOD_AMT);
    patch->mlfo_phase_spread = F(Y_PORT_MLFO_PHASE_SPREAD);
    patch->mlfo_random_freq = F(Y_PORT_MLFO_RANDOM_FREQ);
    EG(patch->ego, Y_PORT_EGO_MODE);
    EG(patch->eg1, Y_PORT_EG1_MODE);
    EG(patch->eg2, Y_PORT_EG2_MODE);
    EG(patch->eg3, Y_PORT_EG3_MODE);
    EG(patch->eg4, Y_PORT_EG4_MODE);
    patch->modmix_bias = F(Y_PORT_MODMIX_BIAS);
    patch->modmix_mod1_src = I(Y_PORT_MODMIX_MOD1_SRC); patch->modmix_mod1_amt = F(Y_PORT_MODMIX_MOD1_AMT);
    patch->modmix_mod2_src = I(Y_PORT_MODMIX_MOD2_SRC); patch->modmix_mod2_amt = F(Y_PORT_MODMIX_MOD2_AMT);
#undef I
#undef F
#undef OSC
#undef VCF
#undef EG
}

int
whysynth_engine_store_patch(whysynth_engine_t *e, int index, const char *name)
{
    y_synth_t *synth = e->synth;
    y_patch_t patch;

    if (index < 0 || (unsigned int)index > synth->patch_count) return -1;
    patch_from_ports(e, &patch);
    if (name) {
        strncpy(patch.name, name, 30);
        patch.name[30] = 0;
    } else if (e->program >= 0 && (unsigned int)e->program < synth->patch_count) {
        memcpy(patch.name, synth->patches[e->program].name, sizeof(patch.name));
        memcpy(patch.category, synth->patches[e->program].category, sizeof(patch.category));
        memcpy(patch.comment, synth->patches[e->program].comment, sizeof(patch.comment));
    }
    pthread_mutex_lock(&synth->patches_mutex);
    y_data_check_patches_allocation(synth, index);
    memcpy(&synth->patches[index], &patch, sizeof(y_patch_t));
    if ((unsigned int)index == synth->patch_count) synth->patch_count++;
    pthread_mutex_unlock(&synth->patches_mutex);
    return index;
}

int
whysynth_engine_load_patches_from_env(whysynth_engine_t *e)
{
    const char *path = getenv("WHYSYNTH_DEFAULT_BANK");
    char *err = NULL;
    int n;

    if (!path || !*path) return 0;
    n = whysynth_engine_load_patches_file(e, path, &err);
    if (!n) fprintf(stderr, "WhySynth: could not load default bank '%s': %s\n", path, err ? err : "unknown error");
    free(err);
    return n;
}

/* ---- events and rendering ---- */

int
whysynth_event_from_midi(const uint8_t *msg, size_t len, uint32_t frame, whysynth_event_t *ev)
{
    memset(ev, 0, sizeof(*ev));
    ev->frame = frame;
    if (len == 0) return 0;

    switch (msg[0] & 0xF0) {
      case 0x80:
        if (len < 3) return 0;
        ev->type = Y_EV_NOTE_OFF; ev->a = msg[1] & 0x7F; ev->b = msg[2] & 0x7F;
        return 1;
      case 0x90:
        if (len < 3) return 0;
        ev->type = Y_EV_NOTE_ON; ev->a = msg[1] & 0x7F; ev->b = msg[2] & 0x7F;
        return 1;
      case 0xA0:
        if (len < 3) return 0;
        ev->type = Y_EV_KEY_PRESSURE; ev->a = msg[1] & 0x7F; ev->b = msg[2] & 0x7F;
        return 1;
      case 0xB0:
        if (len < 3) return 0;
        ev->type = Y_EV_CONTROL_CHANGE; ev->a = msg[1] & 0x7F; ev->b = msg[2] & 0x7F;
        return 1;
      case 0xC0:
        if (len < 2) return 0;
        ev->type = Y_EV_PROGRAM_CHANGE; ev->value = msg[1] & 0x7F;
        return 1;
      case 0xD0:
        if (len < 2) return 0;
        ev->type = Y_EV_CHANNEL_PRESSURE; ev->a = msg[1] & 0x7F;
        return 1;
      case 0xE0:
        if (len < 3) return 0;
        ev->type = Y_EV_PITCH_BEND;
        ev->value = (int32_t)(((msg[2] & 0x7F) << 7) | (msg[1] & 0x7F)) - 8192;
        return 1;
      default:
        return 0;
    }
}

void
whysynth_engine_render(whysynth_engine_t *e, float *left, float *right, uint32_t nframes,
                       const whysynth_event_t *events, uint32_t nevents)
{
    uint32_t i;

    if (nframes == 0) return;
    /* a program change through MIDI must show in get_program() */
    for (i = 0; i < nevents; i++)
        if (events[i].type == Y_EV_PROGRAM_CHANGE && events[i].value >= 0 &&
            (unsigned int)events[i].value < e->synth->patch_count)
            e->program = events[i].value;

    y_synth_connect_port(e->synth, Y_PORT_OUTPUT_LEFT, left);
    y_synth_connect_port(e->synth, Y_PORT_OUTPUT_RIGHT, right);
    y_synth_run(e->synth, nframes, events, nevents);
}

/* ---- state ---- */

#define STATE_HEADER "WhySynth state 1\n"

size_t
whysynth_engine_state_size(const whysynth_engine_t *e)
{
    /* header and settings, 196 numbers, and a generous 4 KB per patch */
    return 512 + WHYSYNTH_PORT_COUNT * 16 + (size_t)e->synth->patch_count * 4096;
}

size_t
whysynth_engine_state_save(const whysynth_engine_t *e, char *buf, size_t size)
{
    y_synth_t *synth = e->synth;
    size_t pos = 0;
    int n, i;
    unsigned int p;

    if (size == 0) return 0;
    buf[0] = 0;
#define APPEND(...) do { n = snprintf(buf + pos, size - pos, __VA_ARGS__); \
        if (n < 0 || (size_t)n >= size - pos) { buf[0] = 0; return 0; } pos += (size_t)n; } while (0)

    APPEND(STATE_HEADER);
    APPEND("polyphony %d\nmonophonic %d\nglide %d\nprogram_cancel %d\nprogram %d\n",
           synth->polyphony, synth->monophonic, synth->glide, synth->program_cancel, e->program);
    APPEND("ports %d", WHYSYNTH_PARAM_COUNT);
    for (i = WHYSYNTH_PORT_FIRST_PARAM; i < WHYSYNTH_PORT_COUNT; i++) {
        char num[32];
        int k;
        snprintf(num, sizeof(num), "%.9g", (double)e->ports[i]);
        for (k = 0; num[k]; k++) if (num[k] == ',') num[k] = '.';
        APPEND(" %s", num);
    }
    APPEND("\npatches %u\n", synth->patch_count);
    for (p = 0; p < synth->patch_count; p++) {
        int len = y_data_patch_to_text(&synth->patches[p], buf + pos, size - pos);
        if (len < 0) { buf[0] = 0; return 0; }
        pos += (size_t)len;
    }
    APPEND("WhySynth state end\n");
#undef APPEND
    return pos;
}

int
whysynth_engine_state_load(whysynth_engine_t *e, const char *buf, size_t size)
{
    y_synth_t *synth = e->synth;
    y_reader_t r;
    y_memreader_t m;
    char line[8192];
    int polyphony = -1, mono = -1, glide = -1, cancel = -1, program = -1, nports = 0, npatches = -1;
    float ports[WHYSYNTH_PORT_COUNT];
    y_patch_t *patches = NULL;
    int i, count = 0;

    if (size < sizeof(STATE_HEADER) - 1 || memcmp(buf, STATE_HEADER, sizeof(STATE_HEADER) - 1) != 0)
        { if (getenv("WHYSYNTH_STATE_DEBUG")) fprintf(stderr, "state_load: failed at line %d (count=%d npatches=%d nports=%d)\n", __LINE__, count, npatches, nports); return 0; }
    y_reader_init_memory(&r, &m, buf, size);
    r.gets(r.ctx, line, sizeof(line));   /* header */
    memcpy(ports, e->ports, sizeof(ports));

    while (r.gets(r.ctx, line, sizeof(line))) {
        if (sscanf(line, "polyphony %d", &polyphony) == 1) continue;
        if (sscanf(line, "monophonic %d", &mono) == 1) continue;
        if (sscanf(line, "glide %d", &glide) == 1) continue;
        if (sscanf(line, "program_cancel %d", &cancel) == 1) continue;
        if (sscanf(line, "program %d", &program) == 1) continue;
        if (sscanf(line, "ports %d", &nports) == 1) {
            const char *p = strchr(line, ' ');
            p = p ? strchr(p + 1, ' ') : NULL;
            for (i = 0; i < nports && i + WHYSYNTH_PORT_FIRST_PARAM < WHYSYNTH_PORT_COUNT && p; i++) {
                double d;
                if (!y_sscanf(p, " %lf", &d)) break;
                ports[i + WHYSYNTH_PORT_FIRST_PARAM] = (float)d;
                p++;
                while (*p && *p != ' ') p++;
            }
            continue;
        }
        if (sscanf(line, "patches %d", &npatches) == 1) {
            if (npatches < 0 || npatches > 100000) { free(patches); { if (getenv("WHYSYNTH_STATE_DEBUG")) fprintf(stderr, "state_load: failed at line %d (count=%d npatches=%d nports=%d)\n", __LINE__, count, npatches, nports); return 0; } }
            if (npatches > 0) {
                patches = (y_patch_t *)malloc((size_t)npatches * sizeof(y_patch_t));
                if (!patches) { if (getenv("WHYSYNTH_STATE_DEBUG")) fprintf(stderr, "state_load: failed at line %d (count=%d npatches=%d nports=%d)\n", __LINE__, count, npatches, nports); return 0; }
            }
            for (count = 0; count < npatches; count++)
                if (!y_data_read_patch_r(&r, &patches[count])) break;
            continue;
        }
        if (!strncmp(line, "WhySynth state end", 18)) break;
    }

    if (npatches < 0 || count != npatches) { free(patches); { if (getenv("WHYSYNTH_STATE_DEBUG")) fprintf(stderr, "state_load: failed at line %d (count=%d npatches=%d nports=%d)\n", __LINE__, count, npatches, nports); return 0; } }

    /* apply, bank first so a program index means something */
    pthread_mutex_lock(&synth->patches_mutex);
    if (npatches > 0) {
        y_data_check_patches_allocation(synth, npatches - 1);
        memcpy(synth->patches, patches, (size_t)npatches * sizeof(y_patch_t));
        synth->patch_count = (unsigned int)npatches;
    }
    synth->pending_patch_change = -1;
    pthread_mutex_unlock(&synth->patches_mutex);
    free(patches);

    if (cancel >= 0) synth->program_cancel = cancel ? 1 : 0;
    if (polyphony > 0) whysynth_engine_set_polyphony(e, polyphony);
    whysynth_engine_set_mono_mode(e, mono < 0 ? 0 : mono);
    if (glide >= 0) whysynth_engine_set_glide_mode(e, glide);
    e->program = (program >= 0 && (unsigned int)program < synth->patch_count) ? program : -1;
    for (i = WHYSYNTH_PORT_FIRST_PARAM; i < WHYSYNTH_PORT_COUNT; i++)
        whysynth_engine_set_param(e, i, ports[i]);
    return 1;
}
