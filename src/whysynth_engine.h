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
 * Boston, MA 06110-1301 USA.
 */

/* WhySynth as a synthesizer with the plugin API removed. The engine owns
 * the 196 parameter values ("ports", in the LADSPA numbering the synth has
 * always used), the patch bank, and the voice settings. Feed it events with
 * frame offsets and it renders stereo float.
 *
 * Threading follows hexter: render() is the audio thread and only tries the
 * voice lock; everything that changes the bank or the voice count takes the
 * lock and may block, so call those from the main thread. Parameter values
 * are plain floats the audio thread reads each control period. */

#ifndef _WHYSYNTH_ENGINE_H
#define _WHYSYNTH_ENGINE_H

#include <stddef.h>
#include <stdint.h>

#include "whysynth_core.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WHYSYNTH_ENGINE_VERSION "2.0.0"

typedef struct _whysynth_engine_t whysynth_engine_t;
typedef y_event_t whysynth_event_t;

/* parameter numbering: ports 2..197 are parameters; 0 and 1 are audio out */
#define WHYSYNTH_PORT_FIRST_PARAM   2
#define WHYSYNTH_PORT_COUNT         198
#define WHYSYNTH_PARAM_COUNT        196

#define WHYSYNTH_MAX_POLYPHONY      64
#define WHYSYNTH_DEFAULT_POLYPHONY  12

/* voice assignment modes */
enum { WHYSYNTH_MONO_OFF = 0, WHYSYNTH_MONO_ON = 1, WHYSYNTH_MONO_ONCE = 2, WHYSYNTH_MONO_BOTH = 3 };
/* glide modes */
enum { WHYSYNTH_GLIDE_LEGATO = 0, WHYSYNTH_GLIDE_INITIAL = 1, WHYSYNTH_GLIDE_ALWAYS = 2,
       WHYSYNTH_GLIDE_LEFTOVER = 3, WHYSYNTH_GLIDE_OFF = 4 };

/* parameter kinds, from the port table */
enum {
    WHYSYNTH_KIND_OUTPUT = 0,
    WHYSYNTH_KIND_BOOLEAN,
    WHYSYNTH_KIND_INTEGER,
    WHYSYNTH_KIND_LINEAR,
    WHYSYNTH_KIND_LOGARITHMIC,
    WHYSYNTH_KIND_LOGSCALED,
    WHYSYNTH_KIND_BPLOGSCALED,
    WHYSYNTH_KIND_COMBO,
    WHYSYNTH_KIND_PAN
};

typedef struct {
    const char *name;
    float       min, max, def;
    int         kind;        /* WHYSYNTH_KIND_* */
    int         combo_type;  /* Y_COMBO_TYPE_* when kind is COMBO, else -1 */
    int         is_integer;  /* stepped */
    int         is_output;
} whysynth_param_info_t;

/* ---- lifetime ---- */
whysynth_engine_t *whysynth_engine_new(float sample_rate);
void  whysynth_engine_free(whysynth_engine_t *e);
void  whysynth_engine_reset(whysynth_engine_t *e);
float whysynth_engine_get_sample_rate(const whysynth_engine_t *e);

/* ---- parameters (port numbering) ---- */
/* static description; valid for 0 <= port < WHYSYNTH_PORT_COUNT */
const whysynth_param_info_t *whysynth_param_info(int port);
/* a display name for a stepped value, or NULL if it is just a number */
const char *whysynth_param_value_name(int port, int value);
/* symbol for LV2 and file use: lower case, letters digits and underscores */
void  whysynth_param_symbol(int port, char *buf, size_t size);

float whysynth_engine_get_param(const whysynth_engine_t *e, int port);
void  whysynth_engine_set_param(whysynth_engine_t *e, int port, float value);   /* clamped */
/* all WHYSYNTH_PORT_COUNT values; indices 0 and 1 are meaningless */
void  whysynth_engine_get_params(const whysynth_engine_t *e, float *out);

/* ---- voice settings (main thread) ---- */
int   whysynth_engine_set_polyphony(whysynth_engine_t *e, int voices);
int   whysynth_engine_get_polyphony(const whysynth_engine_t *e);
int   whysynth_engine_set_mono_mode(whysynth_engine_t *e, int mode);
int   whysynth_engine_get_mono_mode(const whysynth_engine_t *e);
int   whysynth_engine_set_glide_mode(whysynth_engine_t *e, int mode);
int   whysynth_engine_get_glide_mode(const whysynth_engine_t *e);
/* cancel sounding notes on program change (default on, as the DSSI plugin) */
void  whysynth_engine_set_program_cancel(whysynth_engine_t *e, int on);
int   whysynth_engine_get_program_cancel(const whysynth_engine_t *e);
int   whysynth_engine_get_active_voices(const whysynth_engine_t *e);

/* ---- the patch bank ---- */
int   whysynth_engine_patch_count(const whysynth_engine_t *e);
/* NULL when index is out of range */
const char *whysynth_engine_patch_name(const whysynth_engine_t *e, int index);
/* index of the patch most recently selected, or -1 */
int   whysynth_engine_get_program(const whysynth_engine_t *e);
/* any thread: loads the patch into the parameters, immediately if the bank
 * is free, else at the next render */
void  whysynth_engine_select_program(whysynth_engine_t *e, int index);
/* read WhySynth patch files (text) into the bank starting at slot 0;
 * returns patches read, 0 on error with *errmsg malloc'd */
int   whysynth_engine_load_patches_file(whysynth_engine_t *e, const char *path, char **errmsg);
int   whysynth_engine_load_patches_memory(whysynth_engine_t *e, const char *data, size_t size, char **errmsg);
/* the patch as text, for saving; returns length or -1 if it does not fit */
int   whysynth_engine_patch_text(const whysynth_engine_t *e, int index, char *buf, size_t size);
/* store the current parameter values into bank slot index (extends the
 * bank when index == count) with the given name; returns the slot or -1 */
int   whysynth_engine_store_patch(whysynth_engine_t *e, int index, const char *name);
/* honor WHYSYNTH_DEFAULT_BANK if set; returns patches loaded */
int   whysynth_engine_load_patches_from_env(whysynth_engine_t *e);

/* ---- events and rendering (audio thread) ---- */
int   whysynth_event_from_midi(const uint8_t *msg, size_t len, uint32_t frame, whysynth_event_t *ev);
void  whysynth_engine_render(whysynth_engine_t *e, float *left, float *right, uint32_t nframes,
                             const whysynth_event_t *events, uint32_t nevents);

/* ---- state: a text block with settings, parameters and the bank ---- */
/* an upper bound on the state size for this engine */
size_t whysynth_engine_state_size(const whysynth_engine_t *e);
/* writes a NUL-terminated text block; returns its length (without the NUL)
 * or 0 if it did not fit */
size_t whysynth_engine_state_save(const whysynth_engine_t *e, char *buf, size_t size);
int    whysynth_engine_state_load(whysynth_engine_t *e, const char *buf, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* _WHYSYNTH_ENGINE_H */
