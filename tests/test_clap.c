/* whysynth CLAP plugin test: load the built plugin as a host would
 *
 * Copyright (C) 2026 Keith Adler. GPL-2.0-or-later.
 *
 * Arguments: path to the plugin binary, path to the bank directory.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdbool.h>

#include <clap/clap.h>

#ifdef _WIN32
#include <windows.h>
static void *dl_open(const char *p) { return (void *)LoadLibraryA(p); }
static void *dl_sym(void *h, const char *s) { return (void *)GetProcAddress((HMODULE)h, s); }
static const char *dl_error(void) { return "LoadLibrary failed"; }
#else
#include <dlfcn.h>
static void *dl_open(const char *p) { return dlopen(p, RTLD_NOW | RTLD_LOCAL); }
static void *dl_sym(void *h, const char *s) { return dlsym(h, s); }
static const char *dl_error(void) { return dlerror(); }
#endif

static int failures = 0, checks = 0;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

/* ---- a minimal host ---- */

static int callback_requests = 0;
static int rescans = 0;

static const void *host_get_extension(const clap_host_t *host, const char *id);
static void host_request_restart(const clap_host_t *host) {}
static void host_request_process(const clap_host_t *host) {}
static void host_request_callback(const clap_host_t *host) { callback_requests++; }

static void host_params_rescan(const clap_host_t *host, clap_param_rescan_flags flags) { rescans++; }
static void host_params_clear(const clap_host_t *host, clap_id id, clap_param_clear_flags flags) {}
static void host_params_request_flush(const clap_host_t *host) {}
static const clap_host_params_t host_params = { host_params_rescan, host_params_clear, host_params_request_flush };

static void host_log(const clap_host_t *host, clap_log_severity sev, const char *msg) { printf("  plugin log [%d]: %s\n", (int)sev, msg); }
static const clap_host_log_t host_log_ext = { host_log };

static const clap_host_t host = {
    CLAP_VERSION_INIT, NULL, "test host", "whysynth", "https://github.com/keithadler/whysynth", "1.0",
    host_get_extension, host_request_restart, host_request_process, host_request_callback
};

static const void *
host_get_extension(const clap_host_t *h, const char *id)
{
    if (!strcmp(id, CLAP_EXT_PARAMS)) return &host_params;
    if (!strcmp(id, CLAP_EXT_LOG)) return &host_log_ext;
    return NULL;
}

/* ---- event lists ---- */

#define MAX_IN 64
typedef struct {
    clap_input_events_t list;
    uint8_t  storage[MAX_IN][128];   /* larger than any core event */
    uint32_t n;
} in_events_t;

static uint32_t in_size(const clap_input_events_t *l) { return ((const in_events_t *)l->ctx)->n; }
static const clap_event_header_t *in_get(const clap_input_events_t *l, uint32_t i) { return (const clap_event_header_t *)((const in_events_t *)l->ctx)->storage[i]; }

static void in_init(in_events_t *e) { memset(e, 0, sizeof(*e)); e->list.ctx = e; e->list.size = in_size; e->list.get = in_get; }
static void in_push(in_events_t *e, const void *ev, size_t size) { if (size > 128) { printf("event too large\n"); exit(1); } if (e->n < MAX_IN) { memcpy(e->storage[e->n], ev, size); e->n++; } }

static void
in_note(in_events_t *e, uint16_t type, uint32_t time, int16_t key, double vel)
{
    clap_event_note_t n;
    memset(&n, 0, sizeof(n));
    n.header.size = sizeof(n); n.header.time = time; n.header.space_id = CLAP_CORE_EVENT_SPACE_ID; n.header.type = type;
    n.note_id = -1; n.port_index = 0; n.channel = 0; n.key = key; n.velocity = vel;
    in_push(e, &n, sizeof(n));
}

static void
in_midi(in_events_t *e, uint32_t time, uint8_t a, uint8_t b, uint8_t c)
{
    clap_event_midi_t m;
    memset(&m, 0, sizeof(m));
    m.header.size = sizeof(m); m.header.time = time; m.header.space_id = CLAP_CORE_EVENT_SPACE_ID; m.header.type = CLAP_EVENT_MIDI;
    m.port_index = 0; m.data[0] = a; m.data[1] = b; m.data[2] = c;
    in_push(e, &m, sizeof(m));
}

static void
in_param(in_events_t *e, uint32_t time, clap_id id, double value)
{
    clap_event_param_value_t p;
    memset(&p, 0, sizeof(p));
    p.header.size = sizeof(p); p.header.time = time; p.header.space_id = CLAP_CORE_EVENT_SPACE_ID; p.header.type = CLAP_EVENT_PARAM_VALUE;
    p.param_id = id; p.note_id = -1; p.port_index = -1; p.channel = -1; p.key = -1; p.value = value;
    in_push(e, &p, sizeof(p));
}

typedef struct {
    clap_output_events_t list;
    int pushed;
    clap_event_param_value_t last_param;
} out_events_t;

static bool out_try_push(const clap_output_events_t *l, const clap_event_header_t *ev)
{
    out_events_t *o = (out_events_t *)l->ctx;
    o->pushed++;
    if (ev->type == CLAP_EVENT_PARAM_VALUE && ev->size == sizeof(clap_event_param_value_t))
        memcpy(&o->last_param, ev, sizeof(o->last_param));
    return true;
}

/* ---- streams ---- */

typedef struct { uint8_t *data; size_t size, cap, pos; } membuf_t;

static int64_t mem_write(const clap_ostream_t *s, const void *buf, uint64_t n)
{
    membuf_t *m = (membuf_t *)s->ctx;
    if (m->size + n > m->cap) { m->cap = (m->size + n) * 2; m->data = (uint8_t *)realloc(m->data, m->cap); }
    memcpy(m->data + m->size, buf, n);
    m->size += n;
    return (int64_t)n;
}

static int64_t mem_read(const clap_istream_t *s, void *buf, uint64_t n)
{
    membuf_t *m = (membuf_t *)s->ctx;
    uint64_t left = m->size - m->pos;
    if (n > left) n = left;
    /* deliver in small pieces to exercise the plugin's read loop */
    if (n > 1000) n = 1000;
    memcpy(buf, m->data + m->pos, n);
    m->pos += n;
    return (int64_t)n;
}

/* ---- helpers ---- */

#define BLOCK 256
static float outL[BLOCK], outR[BLOCK];

static double
process_blocks(const clap_plugin_t *p, in_events_t *in, out_events_t *out, int blocks, float *peak)
{
    clap_process_t pr;
    clap_audio_buffer_t ab;
    float *chans[2] = { outL, outR };
    double energy = 0.0;
    int b;
    uint32_t i;

    memset(&pr, 0, sizeof(pr));
    memset(&ab, 0, sizeof(ab));
    ab.data32 = chans; ab.channel_count = 2;
    pr.frames_count = BLOCK;
    pr.steady_time = -1;
    pr.audio_outputs = &ab; pr.audio_outputs_count = 1;
    pr.in_events = &in->list;
    pr.out_events = &out->list;
    if (peak) *peak = 0.0f;

    for (b = 0; b < blocks; b++) {
        clap_process_status st = p->process(p, &pr);
        CHECK(st != CLAP_PROCESS_ERROR, "process returned error");
        for (i = 0; i < BLOCK; i++) {
            energy += (double)outL[i] * outL[i];
            if (peak && fabsf(outL[i]) > *peak) *peak = fabsf(outL[i]);
        }
        in->n = 0;  /* events only in the first block */
    }
    return sqrt(energy / (blocks * BLOCK));
}

int
main(int argc, char **argv)
{
    const char *plugin_path = argc > 1 ? argv[1] : NULL;
    const char *bank_dir = argc > 2 ? argv[2] : ".";
    void *lib;
    const clap_plugin_entry_t *entry;
    const clap_plugin_factory_t *factory;
    const clap_plugin_t *p;
    const clap_plugin_params_t *params;
    const clap_plugin_state_t *state;
    const clap_plugin_audio_ports_t *aports;
    const clap_plugin_note_ports_t *nports;
    const clap_plugin_preset_load_t *preset;
    in_events_t in;
    out_events_t out;
    char text[64];
    double v;

    if (!plugin_path) { fprintf(stderr, "usage: test_clap PLUGIN BANKDIR\n"); return 2; }
    setvbuf(stdout, NULL, _IONBF, 0);

    lib = dl_open(plugin_path);
    if (!lib) { fprintf(stderr, "cannot load %s: %s\n", plugin_path, dl_error()); return 1; }
    entry = (const clap_plugin_entry_t *)dl_sym(lib, "clap_entry");
    CHECK(entry != NULL, "clap_entry exported");
    if (!entry) return 1;
    CHECK(clap_version_is_compatible(entry->clap_version), "entry clap version");
    CHECK(entry->init(plugin_path), "entry init");

    factory = (const clap_plugin_factory_t *)entry->get_factory(CLAP_PLUGIN_FACTORY_ID);
    CHECK(factory != NULL, "plugin factory");
    CHECK(factory->get_plugin_count(factory) == 1, "one plugin");
    {
        const clap_plugin_descriptor_t *d = factory->get_plugin_descriptor(factory, 0);
        CHECK(d && !strcmp(d->id, "com.github.keithadler.whysynth"), "descriptor id");
        CHECK(d && !strcmp(d->name, "WhySynth"), "descriptor name");
        CHECK(factory->create_plugin(factory, &host, "com.example.nope") == NULL, "unknown id refused");
        p = factory->create_plugin(factory, &host, d->id);
    }
    CHECK(p != NULL, "create plugin");
    if (!p) return 1;
    CHECK(p->init(p), "plugin init");

    params = (const clap_plugin_params_t *)p->get_extension(p, CLAP_EXT_PARAMS);
    state  = (const clap_plugin_state_t *)p->get_extension(p, CLAP_EXT_STATE);
    aports = (const clap_plugin_audio_ports_t *)p->get_extension(p, CLAP_EXT_AUDIO_PORTS);
    nports = (const clap_plugin_note_ports_t *)p->get_extension(p, CLAP_EXT_NOTE_PORTS);
    preset = (const clap_plugin_preset_load_t *)p->get_extension(p, CLAP_EXT_PRESET_LOAD);
    CHECK(params && state && aports && nports && preset, "extensions present");
    CHECK(p->get_extension(p, "clap.gui") == NULL, "no gui claimed");

    /* ports */
    {
        clap_audio_port_info_t ai;
        clap_note_port_info_t ni;
        CHECK(aports->count(p, false) == 1 && aports->count(p, true) == 0, "audio port counts");
        CHECK(aports->get(p, 0, false, &ai) && ai.channel_count == 2 && (ai.flags & CLAP_AUDIO_PORT_IS_MAIN), "audio port info");
        CHECK(nports->count(p, true) == 1 && nports->count(p, false) == 0, "note port counts");
        CHECK(nports->get(p, 0, true, &ni) && (ni.supported_dialects & CLAP_NOTE_DIALECT_MIDI) && (ni.supported_dialects & CLAP_NOTE_DIALECT_CLAP), "note port dialects");
    }

    /* params: 196 synth parameters plus 4 settings */
    {
        uint32_t i, n = params->count(p);
        CHECK(n == 200, "param count %u", n);
        for (i = 0; i < n; i++) {
            clap_param_info_t info;
            CHECK(params->get_info(p, i, &info), "param %u info", i);
            CHECK(params->get_value(p, info.id, &v), "param %u value", i);
            CHECK(v >= info.min_value && v <= info.max_value, "param %u (%s) value %f in range", i, info.name, v);
            CHECK(params->value_to_text(p, info.id, v, text, sizeof(text)), "param %u text", i);
        }
        { clap_param_info_t none; CHECK(!params->get_info(p, 999, &none), "no param 999"); }
        CHECK(params->value_to_text(p, 2, 1, text, sizeof(text)) && !strcmp(text, "minBLEP"), "Osc1 mode text (%s)", text);
        CHECK(params->text_to_value(p, 2, "Noise", &v) && fabs(v - 7.0) < 1e-9, "Osc1 mode text to value %f", v);
        CHECK(params->text_to_value(p, 1001, "Mono legato", &v) && fabs(v - 2.0) < 1e-9, "mono text to value %f", v);
        CHECK(params->text_to_value(p, 1003, "12: Anything", &v) && fabs(v - 11.0) < 1e-9, "program text to value %f", v);
        CHECK(params->value_to_text(p, 1003, 0, text, sizeof(text)) && strstr(text, "1:") == text, "program text (%s)", text);
    }

    /* load a patch file through preset-load */
    {
        char path[1024];
        snprintf(path, sizeof(path), "%s/version_20051005_patches.WhySynth", bank_dir);
        CHECK(preset->from_location(p, CLAP_PRESET_DISCOVERY_LOCATION_FILE, path, "3"), "preset load %s", path);
        CHECK(params->get_value(p, 1003, &v) && fabs(v - 2.0) < 1e-9, "load key selected patch 3 (%f)", v);
        CHECK(!preset->from_location(p, CLAP_PRESET_DISCOVERY_LOCATION_FILE, "/nonexistent.WhySynth", NULL), "missing file fails");
        CHECK(callback_requests > 0, "plugin asked for a main-thread callback");
        p->on_main_thread(p);
        CHECK(rescans > 0, "plugin rescanned params on the main thread");
    }

    /* activate and play */
    CHECK(p->activate(p, 48000.0, 32, BLOCK), "activate");
    CHECK(p->start_processing(p), "start processing");
    in_init(&in);
    memset(&out, 0, sizeof(out)); out.list.ctx = &out; out.list.try_push = out_try_push;

    {
        float peak;
        double r;
        /* pick a patch whose oscillator 1 is minBLEP: it sounds at once,
         * where PADsynth patches wait for a table to render */
        {
            int k, chosen = -1;
            for (k = 0; k < 400 && chosen < 0; k++) {
                in_param(&in, 0, 1003, k);
                process_blocks(p, &in, &out, 1, NULL);
                if (params->get_value(p, 2, &v) && fabs(v - 1.0) < 1e-9) chosen = k;
            }
            CHECK(chosen >= 0, "found a minBLEP patch (%d)", chosen);
        }
        in_note(&in, CLAP_EVENT_NOTE_ON, 0, 60, 0.8);
        in_note(&in, CLAP_EVENT_NOTE_ON, 10, 64, 0.8);
        in_midi(&in, 20, 0x90, 67, 100);
        r = process_blocks(p, &in, &out, 40, &peak);
        CHECK(r > 0.005, "chord audible: rms %.4f", r);
        CHECK(peak <= 1.5f, "peak %.3f within range", peak);

        in_note(&in, CLAP_EVENT_NOTE_OFF, 0, 60, 0.5);
        in_note(&in, CLAP_EVENT_NOTE_OFF, 0, 64, 0.5);
        in_midi(&in, 0, 0x80, 67, 64);
        process_blocks(p, &in, &out, 600, NULL);
        r = process_blocks(p, &in, &out, 100, NULL);
        CHECK(r < 1e-3, "quiet after release: rms %.6f", r);

        /* a program change through MIDI is reported back, and rewrites parameters */
        out.pushed = 0;
        in_midi(&in, 0, 0xC0, 3, 0);
        process_blocks(p, &in, &out, 1, NULL);
        CHECK(out.pushed >= 1 && out.last_param.param_id != 0, "program change reported (pushed %d)", out.pushed);
        CHECK(params->get_value(p, 1003, &v) && fabs(v - 3.0) < 1e-9, "program is now 4");

        /* param events */
        in_param(&in, 0, 60, 23.5);       /* VCF1 frequency, 0..50 */
        in_param(&in, 0, 1000, 3);        /* polyphony */
        in_param(&in, 0, 1001, 1);        /* mono */
        process_blocks(p, &in, &out, 1, NULL);
        CHECK(params->get_value(p, 60, &v) && fabs(v - 23.5) < 1e-3, "VCF1 frequency applied (%f)", v);
        CHECK(params->get_value(p, 1000, &v) && fabs(v - 3.0) < 1e-9, "polyphony applied (%f)", v);
        CHECK(params->get_value(p, 1001, &v) && fabs(v - 1.0) < 1e-9, "mono applied (%f)", v);
    }

    /* state round trip */
    {
        membuf_t m = { NULL, 0, 0, 0 };
        clap_ostream_t os = { &m, mem_write };
        clap_istream_t is = { &m, mem_read };
        double before, after;
        params->get_value(p, 60, &before);
        CHECK(state->save(p, &os), "state save");
        CHECK(m.size > 10000, "state size %zu", m.size);
        in_param(&in, 0, 60, 41.0);
        process_blocks(p, &in, &out, 1, NULL);
        params->get_value(p, 60, &v);
        CHECK(fabs(v - 41.0) < 1e-3, "VCF1 changed before load");
        CHECK(state->load(p, &is), "state load");
        params->get_value(p, 60, &after);
        CHECK(fabs(after - before) < 1e-3, "VCF1 restored by state (%f vs %f)", after, before);
        CHECK(params->get_value(p, 1003, &v) && fabs(v - 3.0) < 1e-9, "program restored by state (%f)", v);
        m.pos = 0; m.data[0] = 'Z';
        CHECK(!state->load(p, &is), "corrupt state refused");
        free(m.data);
    }

    /* flush while active */
    in.n = 0;
    in_param(&in, 0, 197, 450.0);
    params->flush(p, &in.list, &out.list);
    CHECK(params->get_value(p, 197, &v) && fabs(v - 450.0) < 1e-3, "flush applied tuning (%f)", v);

    p->stop_processing(p);
    p->deactivate(p);
    /* sample rate change re-creates the engine and keeps state */
    CHECK(p->activate(p, 44100.0, 32, BLOCK), "activate at 44100");
    CHECK(params->get_value(p, 197, &v) && fabs(v - 450.0) < 1e-3, "tuning survives a sample rate change (%f)", v);
    p->deactivate(p);
    p->destroy(p);
    entry->deinit();

    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
