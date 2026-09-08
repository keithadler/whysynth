/* WhySynth engine tests
 *
 * Copyright (C) 2026 Keith Adler. GPL-2.0-or-later.
 *
 * Run with the path to the directory holding the .WhySynth patch files.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#ifdef _WIN32
#include <windows.h>
static void sleep_ms(int ms) { Sleep(ms); }
#else
#include <unistd.h>
static void sleep_ms(int ms) { usleep(ms * 1000); }
#endif

#include "whysynth_engine.h"

static int failures = 0, checks = 0;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static const char *patch_dir = ".";

static char *
patch_path(const char *name)
{
    static char buf[1024];
    snprintf(buf, sizeof(buf), "%s/%s", patch_dir, name);
    return buf;
}

static void
render_frames(whysynth_engine_t *e, float *l, float *r, uint32_t frames, const whysynth_event_t *evs, uint32_t nev)
{
    uint32_t done = 0, next = 0;
    while (done < frames) {
        uint32_t n = frames - done < 256 ? frames - done : 256;
        whysynth_event_t local[64];
        uint32_t nl = 0;
        while (next < nev && nl < 64 && evs[next].frame < done + n) {
            local[nl] = evs[next];
            local[nl].frame = evs[next].frame < done ? 0 : evs[next].frame - done;
            nl++; next++;
        }
        whysynth_engine_render(e, l + done, r + done, n, local, nl);
        done += n;
    }
}

static double
rms(const float *x, uint32_t n)
{
    double s = 0.0;
    uint32_t i;
    for (i = 0; i < n; i++) s += (double)x[i] * x[i];
    return sqrt(s / (n ? n : 1));
}

static int
has_nan(const float *x, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++) if (x[i] != x[i]) return 1;
    return 0;
}

/* ---- parameters ---- */

static void
test_params(void)
{
    int port, named = 0, symbols_ok = 1;
    char sym[64], prev[64] = "";

    for (port = WHYSYNTH_PORT_FIRST_PARAM; port < WHYSYNTH_PORT_COUNT; port++) {
        const whysynth_param_info_t *p = whysynth_param_info(port);
        CHECK(p && p->name && p->name[0], "port %d has a name", port);
        CHECK(p->min <= p->def && p->def <= p->max, "port %d (%s) default %g in [%g, %g]", port, p->name, p->def, p->min, p->max);
        CHECK(!p->is_output, "port %d is an input", port);
        whysynth_param_symbol(port, sym, sizeof(sym));
        if (!sym[0] || !strcmp(sym, prev)) symbols_ok = 0;
        strcpy(prev, sym);
        if (p->kind == WHYSYNTH_KIND_COMBO && whysynth_param_value_name(port, (int)p->min)) named++;
    }
    CHECK(symbols_ok, "every parameter has a distinct symbol");
    CHECK(named > 30, "%d combo parameters have value names", named);
    CHECK(whysynth_param_info(0)->is_output && whysynth_param_info(1)->is_output, "ports 0 and 1 are audio outputs");
    CHECK(whysynth_param_info(197) && !strcmp(whysynth_param_info(197)->name, "Tuning"), "port 197 is Tuning");
    CHECK(whysynth_param_info(198) == NULL && whysynth_param_info(-1) == NULL, "out of range ports are NULL");
    {
        const char *n = whysynth_param_value_name(2, 1);   /* Osc1 Mode = 1 */
        CHECK(n && !strcmp(n, "minBLEP"), "Osc1 mode 1 is minBLEP (%s)", n ? n : "NULL");
        n = whysynth_param_value_name(58, 0);
        CHECK(n && !strcmp(n, "Off"), "VCF1 mode 0 is Off (%s)", n ? n : "NULL");
        whysynth_param_symbol(14, sym, sizeof(sym));
        CHECK(!strcmp(sym, "osc1_tobusa_level"), "symbol for 'Osc1->BusA Level' is %s", sym);
    }
}

/* ---- patch files ---- */

static void
test_patch_files(void)
{
    struct { const char *name; int expect; } files[] = {
        { "current_default_patches.WhySynth", 397 },
        { "more_K4_interpretations.WhySynth", 454 },
        { "version_20120903_patches.WhySynth", 289 },
        { "version_20051005_patches.WhySynth", 232 },
    };
    size_t i;
    whysynth_engine_t *e = whysynth_engine_new(48000.0f);
    char *err = NULL;
    int factory;

    CHECK(e != NULL, "engine");
    factory = whysynth_engine_patch_count(e);
    CHECK(factory > 50, "factory bank has %d patches", factory);
    CHECK(whysynth_engine_patch_name(e, 0) != NULL && whysynth_engine_patch_name(e, factory) == NULL, "patch name bounds");
    CHECK(whysynth_engine_get_program(e) == 0, "starts on patch 1");

    for (i = 0; i < sizeof(files) / sizeof(files[0]); i++) {
        whysynth_engine_t *f = whysynth_engine_new(48000.0f);
        int n = whysynth_engine_load_patches_file(f, patch_path(files[i].name), &err);
        CHECK(n == files[i].expect, "%s: %d patches, expected %d (%s)", files[i].name, n, files[i].expect, err ? err : "");
        free(err); err = NULL;
        whysynth_engine_free(f);
    }

    /* text round trip: every factory patch survives write then read */
    {
        int p, bad = 0;
        for (p = 0; p < factory; p++) {
            char text[4096], text2[4096];
            whysynth_engine_t *f = whysynth_engine_new(48000.0f);
            int len = whysynth_engine_patch_text(e, p, text, sizeof(text));
            int n = whysynth_engine_load_patches_memory(f, text, (size_t)len, &err);
            free(err); err = NULL;
            if (len <= 0 || n != 1) { bad++; whysynth_engine_free(f); continue; }
            whysynth_engine_patch_text(f, 0, text2, sizeof(text2));
            if (strcmp(text, text2)) { bad++; if (bad == 1) printf("first mismatch, patch %d:\n%s\n---\n%s\n", p, text, text2); }
            whysynth_engine_free(f);
        }
        CHECK(bad == 0, "%d factory patches did not survive a text round trip", bad);
    }

    /* memory loader agrees with the file loader */
    {
        FILE *fp = fopen(patch_path("version_20051005_patches.WhySynth"), "rb");
        long size;
        char *data;
        whysynth_engine_t *a = whysynth_engine_new(48000.0f), *b = whysynth_engine_new(48000.0f);
        int na, nb, p, same = 1;
        CHECK(fp != NULL, "open patch file");
        fseek(fp, 0, SEEK_END); size = ftell(fp); fseek(fp, 0, SEEK_SET);
        data = (char *)malloc(size);
        CHECK(fread(data, 1, size, fp) == (size_t)size, "read patch file");
        fclose(fp);
        na = whysynth_engine_load_patches_file(a, patch_path("version_20051005_patches.WhySynth"), &err); free(err); err = NULL;
        nb = whysynth_engine_load_patches_memory(b, data, size, &err); free(err); err = NULL;
        CHECK(na == nb && na == 232, "file %d and memory %d loaders agree", na, nb);
        na = whysynth_engine_patch_count(a);
        for (p = 0; p < na; p++) {
            char ta[4096], tb[4096];
            whysynth_engine_patch_text(a, p, ta, sizeof(ta));
            whysynth_engine_patch_text(b, p, tb, sizeof(tb));
            if (strcmp(ta, tb)) same = 0;
        }
        CHECK(same, "loaded patches are identical");
        free(data);
        whysynth_engine_free(a); whysynth_engine_free(b);
    }

    /* garbage and missing files */
    CHECK(whysynth_engine_load_patches_memory(e, "not a patch\n", 12, &err) == 0 && err != NULL, "junk rejected: %s", err ? err : "");
    free(err); err = NULL;
    CHECK(whysynth_engine_load_patches_file(e, patch_path("nope.WhySynth"), &err) == 0 && err != NULL, "missing file rejected: %s", err ? err : "");
    free(err); err = NULL;

    /* store: parameters back into the bank */
    {
        int count = whysynth_engine_patch_count(e), slot;
        whysynth_engine_select_program(e, 3);
        whysynth_engine_set_param(e, 4, 12.0f);   /* Osc1 Pitch */
        slot = whysynth_engine_store_patch(e, count, "Stored One");
        CHECK(slot == count && whysynth_engine_patch_count(e) == count + 1, "stored as a new patch %d", slot);
        CHECK(!strcmp(whysynth_engine_patch_name(e, slot), "Stored One"), "stored name '%s'", whysynth_engine_patch_name(e, slot));
        whysynth_engine_select_program(e, 0);
        whysynth_engine_select_program(e, slot);
        CHECK(fabsf(whysynth_engine_get_param(e, 4) - 12.0f) < 1e-6, "stored patch recalls Osc1 pitch %g", whysynth_engine_get_param(e, 4));
        CHECK(whysynth_engine_store_patch(e, count + 5, "gap") == -1, "cannot store past the end");
    }
    whysynth_engine_free(e);
}

/* ---- rendering ---- */

static void
test_render(void)
{
    const uint32_t rate = 48000;
    whysynth_engine_t *e = whysynth_engine_new((float)rate);
    float *l = (float *)calloc(rate * 4, sizeof(float)), *r = (float *)calloc(rate * 4, sizeof(float));
    whysynth_event_t evs[4];
    double x;
    int p, count, silent = 0, nan = 0, loud = 0;

    memset(evs, 0, sizeof(evs));
    evs[0].type = Y_EV_NOTE_ON; evs[0].a = 60; evs[0].b = 100; evs[0].frame = 0;
    evs[1].type = Y_EV_NOTE_OFF; evs[1].a = 60; evs[1].b = 64; evs[1].frame = rate;
    render_frames(e, l, r, rate * 4, evs, 2);
    x = rms(l, rate);
    CHECK(x > 0.005, "patch 1 middle C audible: rms %.4f", x);
    CHECK(rms(l + rate * 3, rate) < 1e-3, "quiet 2 s after release: rms %.6f", rms(l + rate * 3, rate));
    CHECK(!has_nan(l, rate * 4) && !has_nan(r, rate * 4), "no NaN");

    /* every factory patch: short note, no NaN, sane level; PADsynth and
     * wavetable patches may be silent until the worker thread has rendered
     * their tables, so give it a moment first */
    count = whysynth_engine_patch_count(e);
    for (p = 0; p < count; p++) {
        double lv;
        whysynth_engine_select_program(e, p);
        whysynth_engine_reset(e);
        render_frames(e, l, r, 512, NULL, 0);   /* lets the sampleset request go out */
        sleep_ms(p < 8 ? 300 : 20);
        render_frames(e, l, r, rate / 2, evs, 1);
        if (has_nan(l, rate / 2) || has_nan(r, rate / 2)) nan++;
        lv = rms(l, rate / 2);
        if (lv < 1e-5) silent++;
        if (lv > 2.0) loud++;
        evs[1].frame = 0;
        render_frames(e, l, r, rate / 4, &evs[1], 1);
        evs[1].frame = rate;
    }
    CHECK(nan == 0, "%d patches produced NaN", nan);
    CHECK(loud == 0, "%d patches exceeded rms 2.0", loud);
    CHECK(silent < count / 4, "%d of %d patches were silent on middle C", silent, count);
    printf("  factory patches: %d, silent on a quick note: %d\n", count, silent);

    /* a PADsynth patch renders its table on the worker thread and then sounds */
    {
        int i, found = -1;
        for (i = 0; i < count; i++) {
            whysynth_engine_select_program(e, i);
            if ((int)lrintf(whysynth_engine_get_param(e, 2)) == 8) { found = i; break; }   /* Osc1 mode PADsynth */
        }
        if (found >= 0) {
            double lv = 0.0;
            int tries;
            whysynth_engine_reset(e);
            for (tries = 0; tries < 50 && lv < 1e-4; tries++) {
                render_frames(e, l, r, 512, NULL, 0);
                sleep_ms(100);
                render_frames(e, l, r, rate / 4, evs, 1);
                lv = rms(l, rate / 4);
                evs[1].frame = 0; render_frames(e, l, r, rate / 8, &evs[1], 1); evs[1].frame = rate;
            }
            CHECK(lv > 1e-4, "PADsynth patch %d ('%s') sounds after its table renders: rms %.5f", found + 1, whysynth_engine_patch_name(e, found), lv);
        } else {
            printf("  no PADsynth patch in the factory bank\n");
        }
    }

    /* determinism: same patch, same events, same output */
    {
        whysynth_engine_t *f = whysynth_engine_new((float)rate);
        float *l2 = (float *)calloc(rate, sizeof(float)), *r2 = (float *)calloc(rate, sizeof(float));
        whysynth_engine_select_program(e, 1); whysynth_engine_reset(e);
        whysynth_engine_select_program(f, 1); whysynth_engine_reset(f);
        render_frames(e, l, r, rate, evs, 1);
        render_frames(f, l2, r2, rate, evs, 1);
        /* the mode LFO can be randomized per voice; compare levels rather than samples */
        CHECK(fabs(rms(l, rate) - rms(l2, rate)) < 0.05 * rms(l, rate) + 1e-6, "two engines render the same level (%.4f vs %.4f)", rms(l, rate), rms(l2, rate));
        free(l2); free(r2);
        whysynth_engine_free(f);
    }

    /* controllers: mod wheel, pitch bend, sustain, all sound off */
    {
        whysynth_event_t ev2[6];
        memset(ev2, 0, sizeof(ev2));
        whysynth_engine_select_program(e, 0); whysynth_engine_reset(e);
        ev2[0].type = Y_EV_CONTROL_CHANGE; ev2[0].a = 64; ev2[0].b = 127; ev2[0].frame = 0;
        ev2[1].type = Y_EV_NOTE_ON; ev2[1].a = 60; ev2[1].b = 100; ev2[1].frame = 1;
        ev2[2].type = Y_EV_NOTE_OFF; ev2[2].a = 60; ev2[2].b = 64; ev2[2].frame = 2;
        ev2[3].type = Y_EV_CONTROL_CHANGE; ev2[3].a = 1; ev2[3].b = 127; ev2[3].frame = 3;
        ev2[4].type = Y_EV_PITCH_BEND; ev2[4].value = 8191; ev2[4].frame = 4;
        ev2[5].type = Y_EV_CHANNEL_PRESSURE; ev2[5].a = 100; ev2[5].frame = 5;
        render_frames(e, l, r, 4096, ev2, 6);
        CHECK(whysynth_engine_get_active_voices(e) >= 1, "sustain holds the voice");
        ev2[0].type = Y_EV_CONTROL_CHANGE; ev2[0].a = 120; ev2[0].b = 0; ev2[0].frame = 0;
        render_frames(e, l, r, 512, ev2, 1);
        CHECK(whysynth_engine_get_active_voices(e) == 0, "all sound off leaves %d voices", whysynth_engine_get_active_voices(e));
    }

    free(l); free(r);
    whysynth_engine_free(e);
}

/* ---- voices and settings ---- */

static void
test_voices(void)
{
    whysynth_engine_t *e = whysynth_engine_new(48000.0f);
    whysynth_event_t evs[70];
    float l[512], r[512];
    int i, n = 0;

    whysynth_engine_set_polyphony(e, 16);
    memset(evs, 0, sizeof(evs));
    for (i = 0; i < 64; i++) { evs[n].type = Y_EV_NOTE_ON; evs[n].a = (uint8_t)(30 + i); evs[n].b = 100; evs[n].frame = (uint32_t)i; n++; }
    whysynth_engine_render(e, l, r, 512, evs, n);
    CHECK(whysynth_engine_get_active_voices(e) == 16, "64 notes at polyphony 16 leave %d", whysynth_engine_get_active_voices(e));

    n = 0; evs[n].type = Y_EV_POLYPHONY; evs[n].value = 4; evs[n].frame = 0; n++;
    whysynth_engine_render(e, l, r, 512, evs, n);
    CHECK(whysynth_engine_get_active_voices(e) <= 4 && whysynth_engine_get_polyphony(e) == 4, "polyphony event to 4: %d voices", whysynth_engine_get_active_voices(e));

    n = 0;
    evs[n].type = Y_EV_MONO_MODE; evs[n].value = WHYSYNTH_MONO_ON; evs[n].frame = 0; n++;
    evs[n].type = Y_EV_NOTE_ON; evs[n].a = 60; evs[n].b = 100; evs[n].frame = 10; n++;
    evs[n].type = Y_EV_NOTE_ON; evs[n].a = 64; evs[n].b = 100; evs[n].frame = 20; n++;
    whysynth_engine_render(e, l, r, 512, evs, n);
    CHECK(whysynth_engine_get_active_voices(e) == 1 && whysynth_engine_get_mono_mode(e) == 1, "mono mode: %d voice", whysynth_engine_get_active_voices(e));
    n = 0; evs[n].type = Y_EV_GLIDE_MODE; evs[n].value = WHYSYNTH_GLIDE_ALWAYS; evs[n].frame = 0; n++;
    whysynth_engine_render(e, l, r, 512, evs, n);
    CHECK(whysynth_engine_get_glide_mode(e) == WHYSYNTH_GLIDE_ALWAYS, "glide mode event");

    CHECK(whysynth_engine_set_polyphony(e, 500) == 64 && whysynth_engine_set_polyphony(e, 0) == 1, "polyphony clamps");
    CHECK(whysynth_engine_set_mono_mode(e, 9) == 3 && whysynth_engine_set_mono_mode(e, 0) == 0, "mono clamps");
    CHECK(whysynth_engine_set_glide_mode(e, 9) == 4, "glide clamps");
    whysynth_engine_set_param(e, 197, 100.0f);
    CHECK(fabsf(whysynth_engine_get_param(e, 197) - 415.3f) < 1e-3, "tuning clamps to %g", whysynth_engine_get_param(e, 197));
    whysynth_engine_set_param(e, 2, 3.4f);
    CHECK(whysynth_engine_get_param(e, 2) == 3.0f, "integer parameter rounds");
    whysynth_engine_set_param(e, 0, 1.0f); whysynth_engine_set_param(e, 999, 1.0f);
    whysynth_engine_select_program(e, -1); whysynth_engine_select_program(e, 100000);
    whysynth_engine_render(e, l, r, 0, NULL, 0);

    /* MIDI parsing */
    {
        whysynth_event_t ev;
        uint8_t m[3] = { 0x93, 60, 100 };
        CHECK(whysynth_event_from_midi(m, 3, 7, &ev) && ev.type == Y_EV_NOTE_ON && ev.a == 60 && ev.frame == 7, "note on");
        m[0] = 0xE0; m[1] = 0x7F; m[2] = 0x7F;
        CHECK(whysynth_event_from_midi(m, 3, 0, &ev) && ev.type == Y_EV_PITCH_BEND && ev.value == 8191, "bend max");
        m[0] = 0xC0; m[1] = 5;
        CHECK(whysynth_event_from_midi(m, 2, 0, &ev) && ev.type == Y_EV_PROGRAM_CHANGE && ev.value == 5, "program change");
        m[0] = 0xF8;
        CHECK(!whysynth_event_from_midi(m, 1, 0, &ev), "clock ignored");
        /* a program change event updates get_program() */
        ev.type = Y_EV_PROGRAM_CHANGE; ev.value = 2; ev.frame = 0;
        whysynth_engine_render(e, l, r, 64, &ev, 1);
        CHECK(whysynth_engine_get_program(e) == 2, "program change through events: %d", whysynth_engine_get_program(e));
    }
    whysynth_engine_free(e);
}

/* ---- state ---- */

static void
test_state(void)
{
    whysynth_engine_t *a = whysynth_engine_new(48000.0f), *b;
    char *state;
    size_t cap, n;
    char *err = NULL;
    int i, same = 1;

    whysynth_engine_load_patches_file(a, patch_path("version_20051005_patches.WhySynth"), &err); free(err); err = NULL;
    whysynth_engine_set_polyphony(a, 7);
    whysynth_engine_set_mono_mode(a, WHYSYNTH_MONO_ONCE);
    whysynth_engine_set_glide_mode(a, WHYSYNTH_GLIDE_ALWAYS);
    whysynth_engine_set_program_cancel(a, 0);
    whysynth_engine_select_program(a, 11);
    whysynth_engine_set_param(a, 60, 23.5f);   /* VCF1 frequency, 0..50 */
    whysynth_engine_set_param(a, 197, 432.0f);

    cap = whysynth_engine_state_size(a);
    state = (char *)malloc(cap);
    n = whysynth_engine_state_save(a, state, cap);
    CHECK(n > 1000 && n < cap, "state saved, %zu bytes", n);
    { char tiny[100]; CHECK(whysynth_engine_state_save(a, tiny, sizeof(tiny)) == 0, "small buffer refused"); }

    b = whysynth_engine_new(48000.0f);
    whysynth_engine_free(a);
    CHECK(whysynth_engine_state_load(b, state, n), "state loaded");
    CHECK(whysynth_engine_get_polyphony(b) == 7, "polyphony %d", whysynth_engine_get_polyphony(b));
    CHECK(whysynth_engine_get_mono_mode(b) == WHYSYNTH_MONO_ONCE, "mono mode %d", whysynth_engine_get_mono_mode(b));
    CHECK(whysynth_engine_get_glide_mode(b) == WHYSYNTH_GLIDE_ALWAYS, "glide mode %d", whysynth_engine_get_glide_mode(b));
    CHECK(whysynth_engine_get_program_cancel(b) == 0, "program cancel");
    CHECK(whysynth_engine_get_program(b) == 11, "program %d", whysynth_engine_get_program(b));
    /* loading a 232-patch file over the 397 factory patches keeps the rest, as the DSSI plugin did */
    CHECK(whysynth_engine_patch_count(b) == 397, "bank restored: %d patches", whysynth_engine_patch_count(b));
    CHECK(fabsf(whysynth_engine_get_param(b, 60) - 23.5f) < 1e-3, "VCF1 frequency %g", whysynth_engine_get_param(b, 60));
    CHECK(fabsf(whysynth_engine_get_param(b, 197) - 432.0f) < 1e-3, "tuning %g", whysynth_engine_get_param(b, 197));
    {
        char *state2 = (char *)malloc(cap);
        size_t n2 = whysynth_engine_state_save(b, state2, cap);
        CHECK(n2 == n && !memcmp(state, state2, n), "state is stable across a round trip");
        free(state2);
    }
    for (i = WHYSYNTH_PORT_FIRST_PARAM; i < WHYSYNTH_PORT_COUNT; i++) {
        const whysynth_param_info_t *p = whysynth_param_info(i);
        float v = whysynth_engine_get_param(b, i);
        if (v < p->min || v > p->max) same = 0;
    }
    CHECK(same, "restored parameters are all in range");
    state[0] = 'X';
    CHECK(!whysynth_engine_state_load(b, state, n), "bad header rejected");
    CHECK(!whysynth_engine_state_load(b, state, 5), "short state rejected");
    free(state);
    whysynth_engine_free(b);
}

/* ---- sample rates ---- */

static void
test_sample_rates(void)
{
    const float rates[] = { 8000.0f, 22050.0f, 44100.0f, 48000.0f, 88200.0f, 96000.0f, 192000.0f, 1234.5678f, 45678.901f };
    size_t r;

    for (r = 0; r < sizeof(rates) / sizeof(rates[0]); r++) {
        whysynth_engine_t *e = whysynth_engine_new(rates[r]);
        float l[512], rr[512];
        whysynth_event_t ev[2];
        int i, k, nan = 0;
        double energy = 0.0;

        CHECK(e != NULL, "engine at %g", rates[r]);
        if (!e) continue;
        memset(ev, 0, sizeof(ev));
        for (k = 0; k < whysynth_engine_patch_count(e); k += 9) {
            whysynth_engine_select_program(e, k);
            ev[0].type = Y_EV_NOTE_ON; ev[0].a = (uint8_t)(30 + (k % 60)); ev[0].b = 100;
            ev[1].type = Y_EV_PITCH_BEND; ev[1].value = 5000; ev[1].frame = 3;
            whysynth_engine_render(e, l, rr, 512, ev, 2);
            for (i = 0; i < 20; i++) {
                int j;
                whysynth_engine_render(e, l, rr, 512, NULL, 0);
                for (j = 0; j < 512; j++) { if (l[j] != l[j]) nan++; energy += (double)l[j] * l[j]; }
            }
            ev[0].type = Y_EV_NOTE_OFF;
            whysynth_engine_render(e, l, rr, 512, ev, 1);
            for (i = 0; i < 5; i++) whysynth_engine_render(e, l, rr, 512, NULL, 0);
        }
        CHECK(nan == 0, "%g Hz: %d NaN", rates[r], nan);
        CHECK(energy > 0.0, "%g Hz: sound", rates[r]);
        whysynth_engine_free(e);
    }
    /* instances at different rates live side by side */
    {
        whysynth_engine_t *a = whysynth_engine_new(48000.0f);
        whysynth_engine_t *b = whysynth_engine_new(44100.0f);
        whysynth_engine_t *c = whysynth_engine_new(96000.0f);
        float l[256], r[256];
        whysynth_event_t ev; memset(&ev, 0, sizeof(ev)); ev.type = Y_EV_NOTE_ON; ev.a = 60; ev.b = 100;
        CHECK(a && b && c, "three instances at three rates");
        if (a && b && c) {
            int i;
            for (i = 0; i < 20; i++) {
                whysynth_engine_render(a, l, r, 256, i ? NULL : &ev, i ? 0 : 1);
                whysynth_engine_render(b, l, r, 256, i ? NULL : &ev, i ? 0 : 1);
                whysynth_engine_render(c, l, r, 256, i ? NULL : &ev, i ? 0 : 1);
            }
            CHECK(fabsf(whysynth_engine_get_sample_rate(b) - 44100.0f) < 1.0f, "rate kept per instance");
        }
        whysynth_engine_free(a); whysynth_engine_free(b); whysynth_engine_free(c);
    }
}

int
main(int argc, char **argv)
{
    if (argc > 1) patch_dir = argv[1];
    test_params();
    test_patch_files();
    test_render();
    test_voices();
    test_state();
    test_sample_rates();
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
