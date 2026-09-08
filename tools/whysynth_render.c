/* whysynth-render: render DX7 notes or a MIDI file to a WAV file, no host needed
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
#include <stdint.h>

#include "whysynth_engine.h"

#define BLOCK 256

/* ---- a timed event list ---- */

typedef struct {
    double   time;      /* seconds */
    uint8_t  msg[3];
    uint8_t  len;
    uint8_t *sysex;     /* malloc'd complete message when len == 0 */
    uint32_t sysex_len;
    uint32_t order;
} timed_event_t;

typedef struct {
    timed_event_t *v;
    size_t n, cap;
} event_list_t;

static void
push_event(event_list_t *l, double time, const uint8_t *msg, uint8_t len,
           uint8_t *sysex, uint32_t sysex_len)
{
    if (l->n == l->cap) {
        l->cap = l->cap ? l->cap * 2 : 256;
        l->v = (timed_event_t *)realloc(l->v, l->cap * sizeof(timed_event_t));
        if (!l->v) { fprintf(stderr, "out of memory\n"); exit(1); }
    }
    timed_event_t *e = &l->v[l->n];
    e->time = time;
    memset(e->msg, 0, 3);
    if (len) memcpy(e->msg, msg, len);
    e->len = len;
    e->sysex = sysex;
    e->sysex_len = sysex_len;
    e->order = (uint32_t)l->n;
    l->n++;
}

static int
cmp_event(const void *a, const void *b)
{
    const timed_event_t *x = (const timed_event_t *)a, *y = (const timed_event_t *)b;
    if (x->time < y->time) return -1;
    if (x->time > y->time) return 1;
    return x->order < y->order ? -1 : (x->order > y->order ? 1 : 0);
}

/* ---- Standard MIDI File reader ---- */

typedef struct {
    const uint8_t *p, *end;
} reader_t;

static uint32_t
rd_be32(reader_t *r) { uint32_t v = 0; int i; for (i = 0; i < 4 && r->p < r->end; i++) v = (v << 8) | *r->p++; return v; }
static uint16_t
rd_be16(reader_t *r) { uint16_t v = 0; int i; for (i = 0; i < 2 && r->p < r->end; i++) v = (uint16_t)((v << 8) | *r->p++); return v; }
static uint32_t
rd_vlq(reader_t *r)
{
    uint32_t v = 0;
    int i;
    for (i = 0; i < 4 && r->p < r->end; i++) {
        uint8_t b = *r->p++;
        v = (v << 7) | (b & 0x7F);
        if (!(b & 0x80)) break;
    }
    return v;
}

typedef struct { uint64_t tick; double us_per_quarter; uint32_t order; } tempo_t;

static int
cmp_tempo(const void *a, const void *b)
{
    const tempo_t *x = (const tempo_t *)a, *y = (const tempo_t *)b;
    if (x->tick < y->tick) return -1;
    if (x->tick > y->tick) return 1;
    return x->order < y->order ? -1 : (x->order > y->order ? 1 : 0);
}

/* events collected with absolute ticks, converted to seconds afterwards */
typedef struct { uint64_t tick; timed_event_t ev; } tick_event_t;

static int
load_midi_file(const char *path, event_list_t *out, double *end_time)
{
    FILE *fp = fopen(path, "rb");
    long size;
    uint8_t *data;
    reader_t r;
    uint16_t format, ntracks, division;
    tempo_t *tempos = NULL;
    size_t ntempos = 0, tempocap = 0;
    tick_event_t *tev = NULL;
    size_t ntev = 0, tevcap = 0;
    int t;
    size_t i;
    double ticks_per_second_smpte = 0.0;

    if (!fp) { fprintf(stderr, "cannot open %s\n", path); return 0; }
    fseek(fp, 0, SEEK_END); size = ftell(fp); fseek(fp, 0, SEEK_SET);
    if (size <= 14) { fclose(fp); fprintf(stderr, "%s: not a MIDI file\n", path); return 0; }
    data = (uint8_t *)malloc(size);
    if (!data || fread(data, 1, size, fp) != (size_t)size) { fclose(fp); free(data); return 0; }
    fclose(fp);

    r.p = data; r.end = data + size;
    if (memcmp(r.p, "MThd", 4)) { fprintf(stderr, "%s: not a MIDI file\n", path); free(data); return 0; }
    r.p += 4;
    {
        uint32_t hlen = rd_be32(&r);
        const uint8_t *hend = r.p + hlen;
        format = rd_be16(&r); ntracks = rd_be16(&r); division = rd_be16(&r);
        r.p = hend;
    }
    (void)format;
    if (division & 0x8000) {
        int fps = -(int8_t)(division >> 8);
        int tpf = division & 0xFF;
        ticks_per_second_smpte = (double)fps * tpf;
    }

    /* default tempo */
    tempos = (tempo_t *)malloc(sizeof(tempo_t) * 16); tempocap = 16;
    tempos[0].tick = 0; tempos[0].us_per_quarter = 500000.0; tempos[0].order = 0; ntempos = 1;

    for (t = 0; t < ntracks && r.p + 8 <= r.end; t++) {
        uint32_t tlen;
        const uint8_t *tend;
        uint64_t tick = 0;
        uint8_t running = 0;

        if (memcmp(r.p, "MTrk", 4)) break;
        r.p += 4;
        tlen = rd_be32(&r);
        tend = r.p + tlen;
        if (tend > r.end) tend = r.end;

        while (r.p < tend) {
            uint8_t status;
            tick += rd_vlq(&r);
            if (r.p >= tend) break;
            status = *r.p;
            if (status & 0x80) { r.p++; if (status < 0xF0) running = status; }
            else status = running;

            if (status == 0xFF) {
                uint8_t type = *r.p++;
                uint32_t len = rd_vlq(&r);
                if (type == 0x51 && len == 3) {
                    double us = (double)((r.p[0] << 16) | (r.p[1] << 8) | r.p[2]);
                    if (ntempos == tempocap) { tempocap *= 2; tempos = (tempo_t *)realloc(tempos, sizeof(tempo_t) * tempocap); }
                    tempos[ntempos].tick = tick; tempos[ntempos].us_per_quarter = us; tempos[ntempos].order = (uint32_t)ntempos; ntempos++;
                }
                r.p += len;
            } else if (status == 0xF0 || status == 0xF7) {
                uint32_t len = rd_vlq(&r);
                if (status == 0xF0 && len > 0 && r.p + len <= tend) {
                    uint8_t *sx = (uint8_t *)malloc(len + 1);
                    sx[0] = 0xF0;
                    memcpy(sx + 1, r.p, len);
                    if (ntev == tevcap) { tevcap = tevcap ? tevcap * 2 : 1024; tev = (tick_event_t *)realloc(tev, sizeof(tick_event_t) * tevcap); }
                    memset(&tev[ntev].ev, 0, sizeof(timed_event_t));
                    tev[ntev].tick = tick; tev[ntev].ev.sysex = sx; tev[ntev].ev.sysex_len = len + 1; tev[ntev].ev.order = (uint32_t)ntev;
                    ntev++;
                }
                r.p += len;
            } else if (status >= 0x80 && status < 0xF0) {
                uint8_t msg[3];
                uint8_t len = ((status & 0xF0) == 0xC0 || (status & 0xF0) == 0xD0) ? 2 : 3;
                msg[0] = status;
                msg[1] = r.p < tend ? *r.p++ : 0;
                msg[2] = len == 3 ? (r.p < tend ? *r.p++ : 0) : 0;
                if (ntev == tevcap) { tevcap = tevcap ? tevcap * 2 : 1024; tev = (tick_event_t *)realloc(tev, sizeof(tick_event_t) * tevcap); }
                memset(&tev[ntev].ev, 0, sizeof(timed_event_t));
                tev[ntev].tick = tick; memcpy(tev[ntev].ev.msg, msg, 3); tev[ntev].ev.len = len; tev[ntev].ev.order = (uint32_t)ntev;
                ntev++;
            } else {
                /* system common / realtime: skip the byte */
            }
        }
        r.p = tend;
    }

    qsort(tempos, ntempos, sizeof(tempo_t), cmp_tempo);

    /* ticks -> seconds through the tempo map */
    {
        /* precompute the second offset at each tempo change */
        double *tempo_time = (double *)malloc(sizeof(double) * ntempos);
        double tpq = (division & 0x8000) ? 0.0 : (double)division;
        tempo_time[0] = 0.0;
        for (i = 1; i < ntempos; i++) {
            double dt;
            if (tpq > 0.0)
                dt = (double)(tempos[i].tick - tempos[i - 1].tick) * tempos[i - 1].us_per_quarter / 1e6 / tpq;
            else
                dt = (double)(tempos[i].tick - tempos[i - 1].tick) / ticks_per_second_smpte;
            tempo_time[i] = tempo_time[i - 1] + dt;
        }
        *end_time = 0.0;
        for (i = 0; i < ntev; i++) {
            size_t k = 0;
            double time;
            while (k + 1 < ntempos && tempos[k + 1].tick <= tev[i].tick) k++;
            if (tpq > 0.0)
                time = tempo_time[k] + (double)(tev[i].tick - tempos[k].tick) * tempos[k].us_per_quarter / 1e6 / tpq;
            else
                time = tempo_time[k] + (double)(tev[i].tick - tempos[k].tick) / ticks_per_second_smpte;
            if (tev[i].ev.len)
                push_event(out, time, tev[i].ev.msg, tev[i].ev.len, NULL, 0);
            else
                push_event(out, time, NULL, 0, tev[i].ev.sysex, tev[i].ev.sysex_len);
            if (time > *end_time) *end_time = time;
        }
        free(tempo_time);
    }

    free(tempos);
    free(tev);
    free(data);
    return 1;
}

/* ---- WAV writer (16-bit PCM stereo) ---- */

static void
wr_le32(FILE *f, uint32_t v) { uint8_t b[4] = { v & 0xFF, (v >> 8) & 0xFF, (v >> 16) & 0xFF, (v >> 24) & 0xFF }; fwrite(b, 1, 4, f); }
static void
wr_le16(FILE *f, uint16_t v) { uint8_t b[2] = { v & 0xFF, (v >> 8) & 0xFF }; fwrite(b, 1, 2, f); }

static void
write_wav_header(FILE *f, uint32_t rate, uint32_t nsamples)
{
    fwrite("RIFF", 1, 4, f);
    wr_le32(f, 36 + nsamples * 4);
    fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f);
    wr_le32(f, 16);
    wr_le16(f, 1);          /* PCM */
    wr_le16(f, 2);          /* stereo */
    wr_le32(f, rate);
    wr_le32(f, rate * 4);
    wr_le16(f, 4);
    wr_le16(f, 16);
    fwrite("data", 1, 4, f);
    wr_le32(f, nsamples * 4);
}

/* ---- main ---- */

static void
usage(void)
{
    fprintf(stderr,
        "whysynth-render %s: render WhySynth patches to a WAV file\n"
        "\n"
        "  whysynth-render [options] --note N [--note N ...]\n"
        "  whysynth-render [options] --midi FILE.mid\n"
        "  whysynth-render --bank FILE --list\n"
        "\n"
        "options:\n"
        "  --bank FILE       WhySynth patch file (.WhySynth)\n"
        "  --program N       patch number, 1-based (default 1)\n"
        "  --note N          MIDI note to play (repeat for a chord)\n"
        "  --velocity V      1-127 (default 100)\n"
        "  --hold S          seconds to hold the notes (default 2)\n"
        "  --seconds S       total length; default hold plus 3 seconds of release\n"
        "  --rate R          sample rate (default 48000)\n"
        "  --tuning HZ       A4 frequency (default 440)\n"
        "  --gain DB         output gain (default 0)\n"
        "  --poly N          polyphony 1-64 (default 12)\n"
        "  --mono MODE       0 poly, 1 mono, 2 mono legato, 3 mono both\n"
        "  --wait S          seconds to let PADsynth tables render before playing (default 2)\n"
        "  --midi FILE       render a Standard MIDI File (all channels)\n"
        "  --tail S          seconds after the last MIDI event (default 3)\n"
        "  --out FILE        output WAV (default out.wav)\n"
        "  --list            print the patch names and exit\n",
        WHYSYNTH_ENGINE_VERSION);
}

int
main(int argc, char **argv)
{
    const char *bank = NULL, *midi = NULL, *out_path = "out.wav";
    int program = 1, velocity = 100, poly = WHYSYNTH_DEFAULT_POLYPHONY, mono = 0, list = 0;
    double wait = 2.0, gain = 0.0;
    int notes[64], nnotes = 0;
    double hold = 2.0, seconds = -1.0, tail = 3.0, tuning = 440.0;
    uint32_t rate = 48000;
    whysynth_engine_t *e;
    event_list_t events = { NULL, 0, 0 };
    double end_time = 0.0;
    int i;

    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        const char *v = (i + 1 < argc) ? argv[i + 1] : NULL;
        if (!strcmp(a, "--bank") && v) { bank = v; i++; }
        else if (!strcmp(a, "--program") && v) { program = atoi(v); i++; }
        else if (!strcmp(a, "--note") && v) { if (nnotes < 64) notes[nnotes++] = atoi(v); i++; }
        else if (!strcmp(a, "--velocity") && v) { velocity = atoi(v); i++; }
        else if (!strcmp(a, "--hold") && v) { hold = atof(v); i++; }
        else if (!strcmp(a, "--seconds") && v) { seconds = atof(v); i++; }
        else if (!strcmp(a, "--rate") && v) { rate = (uint32_t)atoi(v); i++; }
        else if (!strcmp(a, "--tuning") && v) { tuning = atof(v); i++; }
        else if (!strcmp(a, "--gain") && v) { gain = atof(v); i++; }
        else if (!strcmp(a, "--wait") && v) { wait = atof(v); i++; }
        else if (!strcmp(a, "--poly") && v) { poly = atoi(v); i++; }
        else if (!strcmp(a, "--mono") && v) { mono = atoi(v); i++; }
        else if (!strcmp(a, "--midi") && v) { midi = v; i++; }
        else if (!strcmp(a, "--tail") && v) { tail = atof(v); i++; }
        else if (!strcmp(a, "--out") && v) { out_path = v; i++; }
        else if (!strcmp(a, "--list")) { list = 1; }
        else if (!strcmp(a, "--help") || !strcmp(a, "-h")) { usage(); return 0; }
        else { fprintf(stderr, "unknown option: %s\n\n", a); usage(); return 2; }
    }
    if (rate < 8000 || rate > 384000) { fprintf(stderr, "sample rate out of range\n"); return 2; }

    e = whysynth_engine_new((float)rate);
    if (!e) { fprintf(stderr, "could not create engine\n"); return 1; }

    if (bank) {
        char *err = NULL;
        int n = whysynth_engine_load_patches_file(e, bank, &err);
        if (!n) {
            fprintf(stderr, "could not load bank %s: %s\n", bank, err ? err : "unknown error");
            free(err);
            whysynth_engine_free(e);
            return 1;
        }
        fprintf(stderr, "loaded %d patches from %s\n", n, bank);
    }

    if (list) {
        for (i = 0; i < whysynth_engine_patch_count(e); i++)
            printf("%3d  %s\n", i + 1, whysynth_engine_patch_name(e, i));
        whysynth_engine_free(e);
        return 0;
    }

    if (!midi && nnotes == 0) {
        usage();
        whysynth_engine_free(e);
        return 2;
    }

    whysynth_engine_set_polyphony(e, poly);
    whysynth_engine_set_mono_mode(e, mono);
    if (program < 1) program = 1;
    if (program > whysynth_engine_patch_count(e)) program = whysynth_engine_patch_count(e);
    whysynth_engine_select_program(e, program - 1);
    whysynth_engine_set_param(e, 197, (float)tuning);
    {
        /* PADsynth and wavetable oscillators render their tables on a
         * worker thread; give it a moment, running silence meanwhile */
        float l[BLOCK], r[BLOCK];
        uint64_t warm = (uint64_t)(wait * rate), done_w = 0;
        while (done_w < warm) {
            whysynth_engine_render(e, l, r, BLOCK, NULL, 0);
            done_w += BLOCK;
        }
        whysynth_engine_reset(e);
    }

    if (midi) {
        if (!load_midi_file(midi, &events, &end_time)) { whysynth_engine_free(e); return 1; }
        if (seconds < 0.0) seconds = end_time + tail;
    } else {
        uint8_t msg[3];
        if (velocity < 1) velocity = 1;
        if (velocity > 127) velocity = 127;
        for (i = 0; i < nnotes; i++) {
            int n = notes[i] < 0 ? 0 : (notes[i] > 127 ? 127 : notes[i]);
            msg[0] = 0x90; msg[1] = (uint8_t)n; msg[2] = (uint8_t)velocity;
            push_event(&events, 0.0, msg, 3, NULL, 0);
        }
        for (i = 0; i < nnotes; i++) {
            int n = notes[i] < 0 ? 0 : (notes[i] > 127 ? 127 : notes[i]);
            msg[0] = 0x80; msg[1] = (uint8_t)n; msg[2] = 64;
            push_event(&events, hold, msg, 3, NULL, 0);
        }
        if (seconds < 0.0) seconds = hold + 3.0;
    }
    if (seconds < 0.01) seconds = 0.01;
    qsort(events.v, events.n, sizeof(timed_event_t), cmp_event);

    {
        FILE *f = fopen(out_path, "wb");
        uint64_t total = (uint64_t)(seconds * rate + 0.5);
        uint64_t done = 0;
        size_t next = 0;
        float bufl[BLOCK], bufr[BLOCK];
        float g = (float)pow(10.0, gain / 20.0);
        whysynth_event_t evs[256];
        double peak = 0.0;

        if (!f) { fprintf(stderr, "cannot write %s\n", out_path); whysynth_engine_free(e); return 1; }
        write_wav_header(f, rate, (uint32_t)total);

        while (done < total) {
            uint32_t n = (uint32_t)(total - done < BLOCK ? total - done : BLOCK);
            uint32_t ne = 0, k;
            int16_t pcm[BLOCK * 2];

            while (next < events.n && ne < 256) {
                uint64_t s = (uint64_t)(events.v[next].time * rate + 0.5);
                if (s >= done + n) break;
                {
                    const timed_event_t *te = &events.v[next];
                    uint32_t frame = (uint32_t)(s < done ? 0 : s - done);
                    if (te->len)
                        whysynth_event_from_midi(te->msg, te->len, frame, &evs[ne]);
                    else
                        whysynth_event_from_midi(te->sysex, te->sysex_len, frame, &evs[ne]);
                    if (evs[ne].type != Y_EV_NONE) ne++;
                }
                next++;
            }

            whysynth_engine_render(e, bufl, bufr, n, evs, ne);
            for (k = 0; k < n; k++) {
                float x = bufl[k] * g, y = bufr[k] * g;
                if (x > 1.0f) x = 1.0f; if (x < -1.0f) x = -1.0f;
                if (y > 1.0f) y = 1.0f; if (y < -1.0f) y = -1.0f;
                if (fabs(x) > peak) peak = fabs(x);
                if (fabs(y) > peak) peak = fabs(y);
                pcm[k * 2] = (int16_t)lrintf(x * 32767.0f);
                pcm[k * 2 + 1] = (int16_t)lrintf(y * 32767.0f);
            }
            for (k = 0; k < n * 2; k++) wr_le16(f, (uint16_t)pcm[k]);
            done += n;
        }
        fclose(f);
        fprintf(stderr, "wrote %s: %.2f s at %u Hz, peak %.1f dBFS\n", out_path, seconds, rate,
                peak > 0.0 ? 20.0 * log10(peak) : -200.0);
    }

    for (i = 0; i < (int)events.n; i++) free(events.v[i].sysex);
    free(events.v);
    whysynth_engine_free(e);
    return 0;
}
