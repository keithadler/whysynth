/* ZedSynth - host-independent engine API
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

/* ZedSynth as a synthesizer with the plugin API removed. The engine owns
 * the 196 parameter values ("ports", in the LADSPA numbering the synth has
 * always used), the patch bank, and the voice settings. Feed it events with
 * frame offsets and it renders stereo float.
 *
 * Threading follows hexter: render() is the audio thread and only tries the
 * voice lock; everything that changes the bank or the voice count takes the
 * lock and may block, so call those from the main thread. Parameter values
 * are plain floats the audio thread reads each control period.
 *
 * Instances may run at different sample rates; PADsynth samples are shared
 * between instances at the same rate. */

#ifndef _ZEDSYNTH_ENGINE_H
#define _ZEDSYNTH_ENGINE_H

#include <stddef.h>
#include <stdint.h>

#include "zedsynth_core.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ZEDSYNTH_ENGINE_VERSION "2.0.0"

typedef struct _zedsynth_engine_t zedsynth_engine_t;
typedef y_event_t zedsynth_event_t;

/* parameter numbering: ports 2..197 are parameters; 0 and 1 are audio out */
#define ZEDSYNTH_PORT_FIRST_PARAM   2
#define ZEDSYNTH_PORT_COUNT         198
#define ZEDSYNTH_PARAM_COUNT        196

#define ZEDSYNTH_MAX_POLYPHONY      64
#define ZEDSYNTH_DEFAULT_POLYPHONY  12

/* voice assignment modes */
enum { ZEDSYNTH_MONO_OFF = 0, ZEDSYNTH_MONO_ON = 1, ZEDSYNTH_MONO_ONCE = 2, ZEDSYNTH_MONO_BOTH = 3 };
/* glide modes */
enum { ZEDSYNTH_GLIDE_LEGATO = 0, ZEDSYNTH_GLIDE_INITIAL = 1, ZEDSYNTH_GLIDE_ALWAYS = 2,
       ZEDSYNTH_GLIDE_LEFTOVER = 3, ZEDSYNTH_GLIDE_OFF = 4 };

/* parameter kinds, from the port table */
enum {
    ZEDSYNTH_KIND_OUTPUT = 0,
    ZEDSYNTH_KIND_BOOLEAN,
    ZEDSYNTH_KIND_INTEGER,
    ZEDSYNTH_KIND_LINEAR,
    ZEDSYNTH_KIND_LOGARITHMIC,
    ZEDSYNTH_KIND_LOGSCALED,
    ZEDSYNTH_KIND_BPLOGSCALED,
    ZEDSYNTH_KIND_COMBO,
    ZEDSYNTH_KIND_PAN
};

typedef struct {
    const char *name;
    float       min, max, def;
    int         kind;        /* ZEDSYNTH_KIND_* */
    int         combo_type;  /* Y_COMBO_TYPE_* when kind is COMBO, else -1 */
    int         is_integer;  /* stepped */
    int         is_output;
} zedsynth_param_info_t;

/* ---- lifetime ---- */
zedsynth_engine_t *zedsynth_engine_new(float sample_rate);
void  zedsynth_engine_free(zedsynth_engine_t *e);
void  zedsynth_engine_reset(zedsynth_engine_t *e);
float zedsynth_engine_get_sample_rate(const zedsynth_engine_t *e);

/* ---- parameters (port numbering) ---- */
/* static description; valid for 0 <= port < ZEDSYNTH_PORT_COUNT */
const zedsynth_param_info_t *zedsynth_param_info(int port);
/* a display name for a stepped value, or NULL if it is just a number */
const char *zedsynth_param_value_name(int port, int value);
/* symbol for LV2 and file use: lower case, letters digits and underscores */
void  zedsynth_param_symbol(int port, char *buf, size_t size);

float zedsynth_engine_get_param(const zedsynth_engine_t *e, int port);
void  zedsynth_engine_set_param(zedsynth_engine_t *e, int port, float value);   /* clamped */
/* all ZEDSYNTH_PORT_COUNT values; indices 0 and 1 are meaningless */
void  zedsynth_engine_get_params(const zedsynth_engine_t *e, float *out);

/* ---- voice settings (main thread) ---- */
int   zedsynth_engine_set_polyphony(zedsynth_engine_t *e, int voices);
int   zedsynth_engine_get_polyphony(const zedsynth_engine_t *e);
int   zedsynth_engine_set_mono_mode(zedsynth_engine_t *e, int mode);
int   zedsynth_engine_get_mono_mode(const zedsynth_engine_t *e);
int   zedsynth_engine_set_glide_mode(zedsynth_engine_t *e, int mode);
int   zedsynth_engine_get_glide_mode(const zedsynth_engine_t *e);
/* cancel sounding notes on program change (default on, as the DSSI plugin) */
void  zedsynth_engine_set_program_cancel(zedsynth_engine_t *e, int on);
int   zedsynth_engine_get_program_cancel(const zedsynth_engine_t *e);
int   zedsynth_engine_get_active_voices(const zedsynth_engine_t *e);

/* ---- the patch bank ---- */
int   zedsynth_engine_patch_count(const zedsynth_engine_t *e);
/* NULL when index is out of range */
const char *zedsynth_engine_patch_name(const zedsynth_engine_t *e, int index);
/* index of the patch most recently selected, or -1 */
int   zedsynth_engine_get_program(const zedsynth_engine_t *e);
/* any thread: loads the patch into the parameters, immediately if the bank
 * is free, else at the next render */
void  zedsynth_engine_select_program(zedsynth_engine_t *e, int index);
/* read ZedSynth patch files (text) into the bank starting at slot 0;
 * returns patches read, 0 on error with *errmsg malloc'd */
int   zedsynth_engine_load_patches_file(zedsynth_engine_t *e, const char *path, char **errmsg);
int   zedsynth_engine_load_patches_memory(zedsynth_engine_t *e, const char *data, size_t size, char **errmsg);
/* the patch as text, for saving; returns length or -1 if it does not fit */
int   zedsynth_engine_patch_text(const zedsynth_engine_t *e, int index, char *buf, size_t size);
/* store the current parameter values into bank slot index (extends the
 * bank when index == count) with the given name; returns the slot or -1 */
int   zedsynth_engine_store_patch(zedsynth_engine_t *e, int index, const char *name);
/* honor ZEDSYNTH_DEFAULT_BANK if set; returns patches loaded */
int   zedsynth_engine_load_patches_from_env(zedsynth_engine_t *e);

/* ---- events and rendering (audio thread) ---- */
int   zedsynth_event_from_midi(const uint8_t *msg, size_t len, uint32_t frame, zedsynth_event_t *ev);
void  zedsynth_engine_render(zedsynth_engine_t *e, float *left, float *right, uint32_t nframes,
                             const zedsynth_event_t *events, uint32_t nevents);

/* ---- state: a text block with settings, parameters and the bank ---- */
/* an upper bound on the state size for this engine */
size_t zedsynth_engine_state_size(const zedsynth_engine_t *e);
/* writes a NUL-terminated text block; returns its length (without the NUL)
 * or 0 if it did not fit */
size_t zedsynth_engine_state_save(const zedsynth_engine_t *e, char *buf, size_t size);
int    zedsynth_engine_state_load(zedsynth_engine_t *e, const char *buf, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* _ZEDSYNTH_ENGINE_H */
