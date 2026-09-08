/* WhySynth - CLAP plugin
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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdbool.h>

#include <clap/clap.h>

#include "whysynth_engine.h"

#define WHYSYNTH_CLAP_ID  "com.github.keithadler.whysynth"
#define MAX_EVENTS        4096
#define DEFAULT_RATE      48000.0f

/* parameter ids: the port number for the 196 synth parameters, then the
 * plugin-level settings */
enum {
    P_POLYPHONY = 1000,
    P_MONO_MODE,
    P_GLIDE_MODE,
    P_PROGRAM,
    P_COUNT_EXTRA = 4
};
#define P_PROGRAM_MAX 4095

static const char *mono_mode_names[4] = { "Poly", "Mono", "Mono legato", "Mono both" };
static const char *glide_mode_names[5] = { "Legato", "Initial", "Always", "Leftover", "Off" };

typedef struct {
    clap_plugin_t                  plugin;
    const clap_host_t             *host;
    const clap_host_params_t      *host_params;
    const clap_host_state_t       *host_state;
    const clap_host_log_t         *host_log;
    const clap_host_preset_load_t *host_preset_load;

    whysynth_engine_t *engine;
    float              sample_rate;
    uint32_t           max_frames;
    float             *left, *right;
    whysynth_event_t   events[MAX_EVENTS];

    float              reported[WHYSYNTH_PORT_COUNT];  /* last values told to the host */
    int                reported_program;
    bool               rescan_requested;
    bool               dirty_requested;
} whysynth_clap_t;

static void
log_msg(whysynth_clap_t *h, clap_log_severity sev, const char *msg)
{
    if (h->host_log) h->host_log->log(h->host, sev, msg);
    else fprintf(stderr, "WhySynth: %s\n", msg);
}

/* ---- descriptor ---- */

static const char *whysynth_features[] = {
    CLAP_PLUGIN_FEATURE_INSTRUMENT,
    CLAP_PLUGIN_FEATURE_SYNTHESIZER,
    CLAP_PLUGIN_FEATURE_STEREO,
    NULL
};

static const clap_plugin_descriptor_t whysynth_desc = {
    .clap_version = CLAP_VERSION_INIT,
    .id           = WHYSYNTH_CLAP_ID,
    .name         = "WhySynth",
    .vendor       = "Sean Bolton and Keith Adler",
    .url          = "https://github.com/keithadler/whysynth",
    .manual_url   = "https://github.com/keithadler/whysynth#readme",
    .support_url  = "https://github.com/keithadler/whysynth/issues",
    .version      = WHYSYNTH_ENGINE_VERSION,
    .description  = "Versatile multi-mode synthesizer: 4 oscillators with minBLEP, wavecycle, granular, FM, PADsynth and phase distortion modes, 2 filters, 3 LFOs, 5 envelopes.",
    .features     = whysynth_features,
};

/* ---- engine lifetime ---- */

static bool
recreate_engine(whysynth_clap_t *h, float sample_rate)
{
    char *state = NULL;
    size_t n = 0;
    whysynth_engine_t *e;

    if (h->engine) {
        size_t cap = whysynth_engine_state_size(h->engine);
        state = (char *)malloc(cap);
        if (state) n = whysynth_engine_state_save(h->engine, state, cap);
    }
    e = whysynth_engine_new(sample_rate);
    if (!e) {
        free(state);
        return false;
    }
    if (state && n) whysynth_engine_state_load(e, state, n);
    free(state);
    if (h->engine) whysynth_engine_free(h->engine);
    h->engine = e;
    h->sample_rate = sample_rate;
    return true;
}

/* ---- audio and note ports ---- */

static uint32_t audio_ports_count(const clap_plugin_t *p, bool is_input) { return is_input ? 0 : 1; }

static bool
audio_ports_get(const clap_plugin_t *p, uint32_t index, bool is_input, clap_audio_port_info_t *info)
{
    if (is_input || index != 0) return false;
    info->id = 0;
    snprintf(info->name, sizeof(info->name), "%s", "Output");
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = 2;
    info->port_type = CLAP_PORT_STEREO;
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
}

static const clap_plugin_audio_ports_t ext_audio_ports = { audio_ports_count, audio_ports_get };

static uint32_t note_ports_count(const clap_plugin_t *p, bool is_input) { return is_input ? 1 : 0; }

static bool
note_ports_get(const clap_plugin_t *p, uint32_t index, bool is_input, clap_note_port_info_t *info)
{
    if (!is_input || index != 0) return false;
    info->id = 0;
    info->supported_dialects = CLAP_NOTE_DIALECT_CLAP | CLAP_NOTE_DIALECT_MIDI;
    info->preferred_dialect = CLAP_NOTE_DIALECT_MIDI;
    snprintf(info->name, sizeof(info->name), "%s", "MIDI In");
    return true;
}

static const clap_plugin_note_ports_t ext_note_ports = { note_ports_count, note_ports_get };

/* ---- params ---- */

static uint32_t
params_count(const clap_plugin_t *p)
{
    return WHYSYNTH_PARAM_COUNT + P_COUNT_EXTRA;
}

static const char *
module_for(const char *name, char *buf, size_t size)
{
    /* "Osc1 Pitch Mod Amt" -> "Osc1"; "EG1 Time1" -> "EG1"; "Tuning" -> "" */
    const char *sp = strchr(name, ' ');
    if (!sp) { buf[0] = 0; return buf; }
    if (!strncmp(name, "ModMix", 6)) { snprintf(buf, size, "ModMix"); return buf; }
    if (!strncmp(name, "Osc", 3) && strstr(name, "->")) { snprintf(buf, size, "%.4s", name); return buf; }
    snprintf(buf, size, "%.*s", (int)(sp - name), name);
    return buf;
}

static bool
params_get_info(const clap_plugin_t *plugin, uint32_t index, clap_param_info_t *info)
{
    if (index >= WHYSYNTH_PARAM_COUNT + P_COUNT_EXTRA) return false;
    memset(info, 0, sizeof(*info));
    if (index < WHYSYNTH_PARAM_COUNT) {
        int port = (int)index + WHYSYNTH_PORT_FIRST_PARAM;
        const whysynth_param_info_t *pi = whysynth_param_info(port);
        info->id = (clap_id)port;
        snprintf(info->name, sizeof(info->name), "%s", pi->name);
        module_for(pi->name, info->module, sizeof(info->module));
        info->flags = CLAP_PARAM_IS_AUTOMATABLE;
        if (pi->is_integer) info->flags |= CLAP_PARAM_IS_STEPPED;
        if (pi->kind == WHYSYNTH_KIND_COMBO) info->flags |= CLAP_PARAM_IS_ENUM;
        info->min_value = pi->min;
        info->max_value = pi->max;
        info->default_value = pi->def;
        return true;
    }
    switch (index - WHYSYNTH_PARAM_COUNT) {
      case 0:
        info->id = P_POLYPHONY;
        snprintf(info->name, sizeof(info->name), "Polyphony");
        snprintf(info->module, sizeof(info->module), "Voice");
        info->flags = CLAP_PARAM_IS_STEPPED;
        info->min_value = 1; info->max_value = WHYSYNTH_MAX_POLYPHONY; info->default_value = WHYSYNTH_DEFAULT_POLYPHONY;
        return true;
      case 1:
        info->id = P_MONO_MODE;
        snprintf(info->name, sizeof(info->name), "Voice mode");
        snprintf(info->module, sizeof(info->module), "Voice");
        info->flags = CLAP_PARAM_IS_STEPPED | CLAP_PARAM_IS_ENUM;
        info->min_value = 0; info->max_value = 3; info->default_value = 0;
        return true;
      case 2:
        info->id = P_GLIDE_MODE;
        snprintf(info->name, sizeof(info->name), "Glide mode");
        snprintf(info->module, sizeof(info->module), "Voice");
        info->flags = CLAP_PARAM_IS_STEPPED | CLAP_PARAM_IS_ENUM;
        info->min_value = 0; info->max_value = 4; info->default_value = 0;
        return true;
      case 3:
        info->id = P_PROGRAM;
        snprintf(info->name, sizeof(info->name), "Program");
        snprintf(info->module, sizeof(info->module), "Patch");
        info->flags = CLAP_PARAM_IS_STEPPED | CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_ENUM;
        info->min_value = 0; info->max_value = P_PROGRAM_MAX; info->default_value = 0;
        return true;
      default:
        return false;
    }
}

static bool
params_get_value(const clap_plugin_t *plugin, clap_id id, double *value)
{
    whysynth_clap_t *h = (whysynth_clap_t *)plugin->plugin_data;

    if (id >= WHYSYNTH_PORT_FIRST_PARAM && id < WHYSYNTH_PORT_COUNT) {
        *value = whysynth_engine_get_param(h->engine, (int)id);
        return true;
    }
    switch (id) {
      case P_POLYPHONY:  *value = whysynth_engine_get_polyphony(h->engine); return true;
      case P_MONO_MODE:  *value = whysynth_engine_get_mono_mode(h->engine); return true;
      case P_GLIDE_MODE: *value = whysynth_engine_get_glide_mode(h->engine); return true;
      case P_PROGRAM: {
        int p = whysynth_engine_get_program(h->engine);
        *value = p < 0 ? 0 : p;
        return true;
      }
      default: return false;
    }
}

static bool
params_value_to_text(const clap_plugin_t *plugin, clap_id id, double value, char *out, uint32_t out_size)
{
    whysynth_clap_t *h = (whysynth_clap_t *)plugin->plugin_data;
    int v = (int)lrint(value);

    if (id >= WHYSYNTH_PORT_FIRST_PARAM && id < WHYSYNTH_PORT_COUNT) {
        const whysynth_param_info_t *pi = whysynth_param_info((int)id);
        const char *name = whysynth_param_value_name((int)id, v);
        if (name) snprintf(out, out_size, "%s", name);
        else if (pi->is_integer) snprintf(out, out_size, "%d", v);
        else if (id == 197) snprintf(out, out_size, "%.1f Hz", value);
        else snprintf(out, out_size, "%.3f", value);
        return true;
    }
    switch (id) {
      case P_POLYPHONY:
        snprintf(out, out_size, "%d voice%s", v, v == 1 ? "" : "s");
        return true;
      case P_MONO_MODE:
        snprintf(out, out_size, "%s", mono_mode_names[v < 0 ? 0 : (v > 3 ? 3 : v)]);
        return true;
      case P_GLIDE_MODE:
        snprintf(out, out_size, "%s", glide_mode_names[v < 0 ? 0 : (v > 4 ? 4 : v)]);
        return true;
      case P_PROGRAM: {
        const char *name = whysynth_engine_patch_name(h->engine, v);
        if (name) snprintf(out, out_size, "%d: %s", v + 1, name);
        else snprintf(out, out_size, "%d", v + 1);
        return true;
      }
      default:
        return false;
    }
}

static bool
params_text_to_value(const clap_plugin_t *plugin, clap_id id, const char *text, double *value)
{
    char *end;
    double d;
    int i;

    if (id == P_MONO_MODE || id == P_GLIDE_MODE) {
        const char **names = id == P_MONO_MODE ? mono_mode_names : glide_mode_names;
        int n = id == P_MONO_MODE ? 4 : 5, best = -1;
        size_t best_len = 0;
        for (i = 0; i < n; i++) {
            size_t len = strlen(names[i]);
            if (!strncmp(text, names[i], len) && len > best_len) { best = i; best_len = len; }
        }
        if (best >= 0) { *value = best; return true; }
    }
    if (id >= WHYSYNTH_PORT_FIRST_PARAM && id < WHYSYNTH_PORT_COUNT) {
        const whysynth_param_info_t *pi = whysynth_param_info((int)id);
        if (pi->kind == WHYSYNTH_KIND_COMBO) {
            for (i = (int)pi->min; i <= (int)pi->max; i++) {
                const char *name = whysynth_param_value_name((int)id, i);
                if (name && !strcmp(name, text)) { *value = i; return true; }
            }
        }
    }
    d = strtod(text, &end);
    if (end == text) return false;
    *value = id == P_PROGRAM ? d - 1.0 : d;
    return true;
}

static void
apply_param_now(whysynth_clap_t *h, clap_id id, double value)
{
    if (id >= WHYSYNTH_PORT_FIRST_PARAM && id < WHYSYNTH_PORT_COUNT) {
        whysynth_engine_set_param(h->engine, (int)id, (float)value);
        h->reported[id] = whysynth_engine_get_param(h->engine, (int)id);
        return;
    }
    switch (id) {
      case P_POLYPHONY:  whysynth_engine_set_polyphony(h->engine, (int)lrint(value)); break;
      case P_MONO_MODE:  whysynth_engine_set_mono_mode(h->engine, (int)lrint(value)); break;
      case P_GLIDE_MODE: whysynth_engine_set_glide_mode(h->engine, (int)lrint(value)); break;
      case P_PROGRAM:    whysynth_engine_select_program(h->engine, (int)lrint(value)); break;
      default: break;
    }
}

static void
params_flush(const clap_plugin_t *plugin, const clap_input_events_t *in, const clap_output_events_t *out)
{
    whysynth_clap_t *h = (whysynth_clap_t *)plugin->plugin_data;
    uint32_t n = in->size(in), i;

    for (i = 0; i < n; i++) {
        const clap_event_header_t *hdr = in->get(in, i);
        if (hdr->space_id == CLAP_CORE_EVENT_SPACE_ID && hdr->type == CLAP_EVENT_PARAM_VALUE) {
            const clap_event_param_value_t *ev = (const clap_event_param_value_t *)hdr;
            apply_param_now(h, ev->param_id, ev->value);
        }
    }
}

static const clap_plugin_params_t ext_params = {
    params_count, params_get_info, params_get_value, params_value_to_text, params_text_to_value, params_flush
};

/* ---- state ---- */

static bool
state_save(const clap_plugin_t *plugin, const clap_ostream_t *stream)
{
    whysynth_clap_t *h = (whysynth_clap_t *)plugin->plugin_data;
    size_t cap = whysynth_engine_state_size(h->engine);
    char *buf = (char *)malloc(cap);
    size_t n, done = 0;

    if (!buf) return false;
    n = whysynth_engine_state_save(h->engine, buf, cap);
    if (!n) { free(buf); return false; }
    while (done < n) {
        int64_t w = stream->write(stream, buf + done, n - done);
        if (w <= 0) { free(buf); return false; }
        done += (size_t)w;
    }
    free(buf);
    return true;
}

static bool
state_load(const clap_plugin_t *plugin, const clap_istream_t *stream)
{
    whysynth_clap_t *h = (whysynth_clap_t *)plugin->plugin_data;
    size_t cap = 65536, size = 0;
    char *buf = (char *)malloc(cap);
    bool ok;

    if (!buf) return false;
    while (1) {
        int64_t r;
        if (size + 4096 > cap) {
            char *nb;
            if (cap > 64u * 1024 * 1024) { free(buf); return false; }
            cap *= 2;
            nb = (char *)realloc(buf, cap);
            if (!nb) { free(buf); return false; }
            buf = nb;
        }
        r = stream->read(stream, buf + size, cap - size - 1);
        if (r < 0) { free(buf); return false; }
        if (r == 0) break;
        size += (size_t)r;
    }
    buf[size] = 0;
    ok = whysynth_engine_state_load(h->engine, buf, size) != 0;
    free(buf);
    if (!ok) {
        log_msg(h, CLAP_LOG_WARNING, "state did not load: not a WhySynth state block");
        return false;
    }
    whysynth_engine_get_params(h->engine, h->reported);
    h->reported_program = whysynth_engine_get_program(h->engine);
    h->rescan_requested = true;
    h->host->request_callback(h->host);
    return true;
}

static const clap_plugin_state_t ext_state = { state_save, state_load };

/* ---- preset load: a WhySynth patch file ---- */

static bool
preset_from_location(const clap_plugin_t *plugin, uint32_t location_kind, const char *location, const char *load_key)
{
    whysynth_clap_t *h = (whysynth_clap_t *)plugin->plugin_data;
    char *err = NULL;
    int count;

    if (location_kind != CLAP_PRESET_DISCOVERY_LOCATION_FILE || !location) {
        if (h->host_preset_load)
            h->host_preset_load->on_error(h->host, location_kind, location, load_key, -1, "WhySynth loads patch files only");
        return false;
    }
    count = whysynth_engine_load_patches_file(h->engine, location, &err);
    if (!count) {
        if (h->host_preset_load)
            h->host_preset_load->on_error(h->host, location_kind, location, load_key, -1, err ? err : "could not load patches");
        free(err);
        return false;
    }
    free(err);
    if (load_key && *load_key) {
        int p = atoi(load_key);
        if (p >= 1) whysynth_engine_select_program(h->engine, p - 1);
    } else {
        whysynth_engine_select_program(h->engine, 0);
    }
    if (h->host_preset_load) h->host_preset_load->loaded(h->host, location_kind, location, load_key);
    h->rescan_requested = true;
    h->dirty_requested = true;
    h->host->request_callback(h->host);
    return true;
}

static const clap_plugin_preset_load_t ext_preset_load = { preset_from_location };

/* ---- plugin ---- */

static bool
plugin_init(const clap_plugin_t *plugin)
{
    whysynth_clap_t *h = (whysynth_clap_t *)plugin->plugin_data;

    h->host_params = (const clap_host_params_t *)h->host->get_extension(h->host, CLAP_EXT_PARAMS);
    h->host_state  = (const clap_host_state_t *)h->host->get_extension(h->host, CLAP_EXT_STATE);
    h->host_log    = (const clap_host_log_t *)h->host->get_extension(h->host, CLAP_EXT_LOG);
    h->host_preset_load = (const clap_host_preset_load_t *)h->host->get_extension(h->host, CLAP_EXT_PRESET_LOAD);
    if (!h->host_preset_load)
        h->host_preset_load = (const clap_host_preset_load_t *)h->host->get_extension(h->host, CLAP_EXT_PRESET_LOAD_COMPAT);

    if (!recreate_engine(h, DEFAULT_RATE)) return false;
    whysynth_engine_load_patches_from_env(h->engine);
    whysynth_engine_get_params(h->engine, h->reported);
    h->reported_program = whysynth_engine_get_program(h->engine);
    return true;
}

static void
plugin_destroy(const clap_plugin_t *plugin)
{
    whysynth_clap_t *h = (whysynth_clap_t *)plugin->plugin_data;
    if (h->engine) whysynth_engine_free(h->engine);
    free(h->left);
    free(h->right);
    free(h);
}

static bool
plugin_activate(const clap_plugin_t *plugin, double sample_rate, uint32_t min_frames, uint32_t max_frames)
{
    whysynth_clap_t *h = (whysynth_clap_t *)plugin->plugin_data;

    if ((float)sample_rate != h->sample_rate) {
        if (!recreate_engine(h, (float)sample_rate)) {
            log_msg(h, CLAP_LOG_ERROR, "could not create the engine at this sample rate");
            return false;
        }
        whysynth_engine_get_params(h->engine, h->reported);
    }
    free(h->left); free(h->right);
    h->max_frames = max_frames ? max_frames : 1;
    h->left = (float *)calloc(h->max_frames, sizeof(float));
    h->right = (float *)calloc(h->max_frames, sizeof(float));
    if (!h->left || !h->right) return false;
    whysynth_engine_reset(h->engine);
    return true;
}

static void plugin_deactivate(const clap_plugin_t *plugin) {}
static bool plugin_start_processing(const clap_plugin_t *plugin) { return true; }
static void plugin_stop_processing(const clap_plugin_t *plugin) {}

static void
plugin_reset(const clap_plugin_t *plugin)
{
    whysynth_clap_t *h = (whysynth_clap_t *)plugin->plugin_data;
    whysynth_engine_reset(h->engine);
}

static inline uint8_t
vel7(double v)
{
    int i = (int)lrint(v * 127.0);
    return (uint8_t)(i < 0 ? 0 : (i > 127 ? 127 : i));
}

static inline uint32_t
midi_length(uint8_t status)
{
    switch (status & 0xF0) {
      case 0xC0: case 0xD0: return 2;
      case 0xF0: return 1;
      default: return 3;
    }
}

static void
push_param_out(const clap_process_t *process, uint32_t time, clap_id id, double value)
{
    clap_event_param_value_t pv;

    if (!process->out_events) return;
    memset(&pv, 0, sizeof(pv));
    pv.header.size = sizeof(pv);
    pv.header.time = time;
    pv.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    pv.header.type = CLAP_EVENT_PARAM_VALUE;
    pv.param_id = id;
    pv.note_id = -1; pv.port_index = -1; pv.channel = -1; pv.key = -1;
    pv.value = value;
    process->out_events->try_push(process->out_events, &pv.header);
}

static clap_process_status
plugin_process(const clap_plugin_t *plugin, const clap_process_t *process)
{
    whysynth_clap_t *h = (whysynth_clap_t *)plugin->plugin_data;
    const clap_input_events_t *in = process->in_events;
    uint32_t nframes = process->frames_count;
    uint32_t nin = in ? in->size(in) : 0;
    uint32_t ne = 0, i;
    int program_now;

    if (nframes > h->max_frames) nframes = h->max_frames;

    for (i = 0; i < nin && ne < MAX_EVENTS; i++) {
        const clap_event_header_t *hdr = in->get(in, i);
        whysynth_event_t ev;
        uint32_t t = hdr->time > nframes ? nframes : hdr->time;

        if (hdr->space_id != CLAP_CORE_EVENT_SPACE_ID) continue;
        memset(&ev, 0, sizeof(ev));
        ev.frame = t;

        switch (hdr->type) {
          case CLAP_EVENT_NOTE_ON: {
            const clap_event_note_t *n = (const clap_event_note_t *)hdr;
            if (n->key < 0) break;
            ev.type = Y_EV_NOTE_ON; ev.a = (uint8_t)n->key; ev.b = vel7(n->velocity);
            if (ev.b == 0) ev.b = 1;
            h->events[ne++] = ev;
            break;
          }
          case CLAP_EVENT_NOTE_OFF:
          case CLAP_EVENT_NOTE_CHOKE: {
            const clap_event_note_t *n = (const clap_event_note_t *)hdr;
            if (n->key < 0) {
                ev.type = Y_EV_CONTROL_CHANGE; ev.a = hdr->type == CLAP_EVENT_NOTE_CHOKE ? 120 : 123; ev.b = 0;
            } else {
                ev.type = Y_EV_NOTE_OFF; ev.a = (uint8_t)n->key;
                ev.b = hdr->type == CLAP_EVENT_NOTE_CHOKE ? 127 : vel7(n->velocity);
            }
            h->events[ne++] = ev;
            break;
          }
          case CLAP_EVENT_NOTE_EXPRESSION: {
            const clap_event_note_expression_t *x = (const clap_event_note_expression_t *)hdr;
            if (x->expression_id == CLAP_NOTE_EXPRESSION_PRESSURE) {
                if (x->key >= 0) { ev.type = Y_EV_KEY_PRESSURE; ev.a = (uint8_t)x->key; ev.b = vel7(x->value); }
                else { ev.type = Y_EV_CHANNEL_PRESSURE; ev.a = vel7(x->value); }
                h->events[ne++] = ev;
            }
            break;
          }
          case CLAP_EVENT_PARAM_VALUE: {
            const clap_event_param_value_t *p = (const clap_event_param_value_t *)hdr;
            if (p->param_id >= WHYSYNTH_PORT_FIRST_PARAM && p->param_id < WHYSYNTH_PORT_COUNT) {
                whysynth_engine_set_param(h->engine, (int)p->param_id, (float)p->value);
                h->reported[p->param_id] = whysynth_engine_get_param(h->engine, (int)p->param_id);
            } else if (p->param_id == P_POLYPHONY) {
                ev.type = Y_EV_POLYPHONY; ev.value = (int32_t)lrint(p->value); h->events[ne++] = ev;
            } else if (p->param_id == P_MONO_MODE) {
                ev.type = Y_EV_MONO_MODE; ev.value = (int32_t)lrint(p->value); h->events[ne++] = ev;
            } else if (p->param_id == P_GLIDE_MODE) {
                ev.type = Y_EV_GLIDE_MODE; ev.value = (int32_t)lrint(p->value); h->events[ne++] = ev;
            } else if (p->param_id == P_PROGRAM) {
                ev.type = Y_EV_PROGRAM_CHANGE; ev.value = (int32_t)lrint(p->value); h->events[ne++] = ev;
            }
            break;
          }
          case CLAP_EVENT_MIDI: {
            const clap_event_midi_t *m = (const clap_event_midi_t *)hdr;
            if (whysynth_event_from_midi(m->data, midi_length(m->data[0]), t, &ev))
                h->events[ne++] = ev;
            break;
          }
          default:
            break;
        }
    }

    whysynth_engine_render(h->engine, h->left, h->right, nframes, h->events, ne);

    if (process->audio_outputs_count >= 1) {
        const clap_audio_buffer_t *out = &process->audio_outputs[0];
        uint32_t c;
        for (c = 0; c < out->channel_count; c++) {
            const float *src = c == 0 ? h->left : h->right;
            if (out->data32 && out->data32[c]) memcpy(out->data32[c], src, nframes * sizeof(float));
            else if (out->data64 && out->data64[c]) {
                uint32_t k;
                for (k = 0; k < nframes; k++) out->data64[c][k] = src[k];
            }
        }
    }

    /* a program change rewrites every parameter: tell the host */
    program_now = whysynth_engine_get_program(h->engine);
    if (program_now != h->reported_program) {
        push_param_out(process, nframes ? nframes - 1 : 0, P_PROGRAM, program_now < 0 ? 0 : program_now);
        h->reported_program = program_now;
    }
    for (i = WHYSYNTH_PORT_FIRST_PARAM; i < WHYSYNTH_PORT_COUNT; i++) {
        float v = whysynth_engine_get_param(h->engine, (int)i);
        if (v != h->reported[i]) {
            push_param_out(process, nframes ? nframes - 1 : 0, (clap_id)i, v);
            h->reported[i] = v;
        }
    }

    return CLAP_PROCESS_CONTINUE;
}

static const void *
plugin_get_extension(const clap_plugin_t *plugin, const char *id)
{
    if (!strcmp(id, CLAP_EXT_AUDIO_PORTS)) return &ext_audio_ports;
    if (!strcmp(id, CLAP_EXT_NOTE_PORTS))  return &ext_note_ports;
    if (!strcmp(id, CLAP_EXT_PARAMS))      return &ext_params;
    if (!strcmp(id, CLAP_EXT_STATE))       return &ext_state;
    if (!strcmp(id, CLAP_EXT_PRESET_LOAD) || !strcmp(id, CLAP_EXT_PRESET_LOAD_COMPAT)) return &ext_preset_load;
    return NULL;
}

static void
plugin_on_main_thread(const clap_plugin_t *plugin)
{
    whysynth_clap_t *h = (whysynth_clap_t *)plugin->plugin_data;

    if (h->rescan_requested) {
        h->rescan_requested = false;
        if (h->host_params) h->host_params->rescan(h->host, CLAP_PARAM_RESCAN_VALUES | CLAP_PARAM_RESCAN_TEXT);
    }
    if (h->dirty_requested) {
        h->dirty_requested = false;
        if (h->host_state) h->host_state->mark_dirty(h->host);
    }
}

static const clap_plugin_t *
create_plugin(const clap_host_t *host)
{
    whysynth_clap_t *h = (whysynth_clap_t *)calloc(1, sizeof(whysynth_clap_t));
    if (!h) return NULL;
    h->host = host;
    h->plugin.desc = &whysynth_desc;
    h->plugin.plugin_data = h;
    h->plugin.init = plugin_init;
    h->plugin.destroy = plugin_destroy;
    h->plugin.activate = plugin_activate;
    h->plugin.deactivate = plugin_deactivate;
    h->plugin.start_processing = plugin_start_processing;
    h->plugin.stop_processing = plugin_stop_processing;
    h->plugin.reset = plugin_reset;
    h->plugin.process = plugin_process;
    h->plugin.get_extension = plugin_get_extension;
    h->plugin.on_main_thread = plugin_on_main_thread;
    h->reported_program = -1;
    return &h->plugin;
}

/* ---- factory and entry ---- */

static uint32_t factory_get_plugin_count(const clap_plugin_factory_t *f) { return 1; }
static const clap_plugin_descriptor_t *factory_get_plugin_descriptor(const clap_plugin_factory_t *f, uint32_t i) { return i == 0 ? &whysynth_desc : NULL; }

static const clap_plugin_t *
factory_create_plugin(const clap_plugin_factory_t *f, const clap_host_t *host, const char *plugin_id)
{
    if (!clap_version_is_compatible(host->clap_version)) return NULL;
    if (strcmp(plugin_id, WHYSYNTH_CLAP_ID)) return NULL;
    return create_plugin(host);
}

static const clap_plugin_factory_t whysynth_factory = {
    factory_get_plugin_count, factory_get_plugin_descriptor, factory_create_plugin
};

static bool entry_init(const char *plugin_path) { return true; }
static void entry_deinit(void) {}
static const void *entry_get_factory(const char *factory_id)
{
    return strcmp(factory_id, CLAP_PLUGIN_FACTORY_ID) ? NULL : &whysynth_factory;
}

CLAP_EXPORT const clap_plugin_entry_t clap_entry = {
    .clap_version = CLAP_VERSION_INIT,
    .init = entry_init,
    .deinit = entry_deinit,
    .get_factory = entry_get_factory,
};
