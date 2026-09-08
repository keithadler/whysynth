/* WhySynth LV2 plugin test: load the bundle through lilv as a host would
 *
 * Copyright (C) 2026 Keith Adler. GPL-2.0-or-later.
 * Arguments: path to the built whysynth.lv2 bundle, path to the patch directory.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#include <lilv/lilv.h>
#include <lv2/atom/atom.h>
#include <lv2/atom/forge.h>
#include <lv2/atom/util.h>
#include <lv2/midi/midi.h>
#include <lv2/urid/urid.h>
#include <lv2/presets/presets.h>

#include "whysynth_lv2_ports.h"

#define WHYSYNTH_URI "https://github.com/keithadler/whysynth"
#define BLOCK 256

static int failures = 0, checks = 0;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static char *uris[512];
static int nuris = 0;
static LV2_URID map_uri(LV2_URID_Map_Handle h, const char *uri)
{
    int i;
    for (i = 0; i < nuris; i++) if (!strcmp(uris[i], uri)) return (LV2_URID)(i + 1);
    if (nuris >= 512) return 0;
    uris[nuris++] = strdup(uri);
    return (LV2_URID)nuris;
}
static const char *unmap_uri(LV2_URID_Unmap_Handle h, LV2_URID u) { return (u >= 1 && (int)u <= nuris) ? uris[u - 1] : NULL; }
static LV2_URID_Map map = { NULL, map_uri };
static LV2_URID_Unmap unmap = { NULL, unmap_uri };

static uint8_t control_buf[8192];
static uint8_t notify_buf[1024];
static float outl[BLOCK], outr[BLOCK];
static float params[WHYSYNTH_LV2_PARAM_COUNT];
static float polyphony = 12.0f, mono = 0.0f, glide = 0.0f;
static LV2_Atom_Forge forge;
static LV2_Atom_Forge_Frame seq_frame;
static LV2_URID urid_midi;

static void begin_block(void)
{
    lv2_atom_forge_set_buffer(&forge, control_buf, sizeof(control_buf));
    lv2_atom_forge_sequence_head(&forge, &seq_frame, 0);
    ((LV2_Atom_Sequence *)notify_buf)->atom.type = map_uri(NULL, LV2_ATOM__Sequence);
    ((LV2_Atom_Sequence *)notify_buf)->atom.size = sizeof(notify_buf) - sizeof(LV2_Atom);
}
static void midi3(uint32_t frame, uint8_t a, uint8_t b, uint8_t c)
{
    uint8_t m[3] = { a, b, c };
    lv2_atom_forge_frame_time(&forge, frame);
    lv2_atom_forge_atom(&forge, 3, urid_midi);
    lv2_atom_forge_write(&forge, m, 3);
}
static double run_blocks(LilvInstance *inst, int blocks)
{
    double energy = 0.0;
    int b; uint32_t i;
    for (b = 0; b < blocks; b++) {
        if (b > 0) { begin_block(); lv2_atom_forge_pop(&forge, &seq_frame); }
        lilv_instance_run(inst, BLOCK);
        for (i = 0; i < BLOCK; i++) energy += (double)outl[i] * outl[i];
    }
    return sqrt(energy / (blocks * BLOCK));
}

int
main(int argc, char **argv)
{
    const char *bundle = argc > 1 ? argv[1] : NULL;
    LilvWorld *world;
    LilvNode *bundle_uri, *plugin_uri, *preset_class;
    const LilvPlugins *plugins;
    const LilvPlugin *plugin;
    LilvInstance *inst;
    const LV2_Feature map_feature = { LV2_URID__map, &map };
    const LV2_Feature unmap_feature = { LV2_URID__unmap, &unmap };
    const LV2_Feature *features[] = { &map_feature, &unmap_feature, NULL };
    char bundle_path[1100];
    uint32_t i;

    if (!bundle) { fprintf(stderr, "usage: test_lv2 BUNDLE_DIR PATCH_DIR\n"); return 2; }
    lv2_atom_forge_init(&forge, &map);
    urid_midi = map_uri(NULL, LV2_MIDI__MidiEvent);

    world = lilv_world_new();
    snprintf(bundle_path, sizeof(bundle_path), "%s/", bundle);
    bundle_uri = lilv_new_file_uri(world, NULL, bundle_path);
    lilv_world_load_bundle(world, bundle_uri);
    lilv_world_load_specifications(world);
    lilv_world_load_plugin_classes(world);
    plugins = lilv_world_get_all_plugins(world);
    plugin_uri = lilv_new_uri(world, WHYSYNTH_URI);
    plugin = lilv_plugins_get_by_uri(plugins, plugin_uri);
    CHECK(plugin != NULL, "bundle %s contains %s", bundle, WHYSYNTH_URI);
    if (!plugin) return 1;

    CHECK(lilv_plugin_get_num_ports(plugin) == LV2_PORT_COUNT, "%d ports, have %u", LV2_PORT_COUNT, lilv_plugin_get_num_ports(plugin));
    {
        LilvNode *name = lilv_plugin_get_name(plugin);
        CHECK(name && !strcmp(lilv_node_as_string(name), "WhySynth"), "plugin name");
        lilv_node_free(name);
    }
    /* every control port has a symbol, a default within its range */
    {
        LilvNode *sym_ok = NULL;
        int bad = 0;
        for (i = LV2_PORT_FIRST_PARAM; i < (uint32_t)LV2_PORT_COUNT; i++) {
            const LilvPort *port = lilv_plugin_get_port_by_index(plugin, i);
            LilvNode *def = NULL, *min = NULL, *max = NULL;
            const LilvNode *sym = port ? lilv_port_get_symbol(plugin, port) : NULL;
            if (!port || !sym || !lilv_node_as_string(sym)[0]) { bad++; continue; }
            lilv_port_get_range(plugin, port, &def, &min, &max);
            if (!def || !min || !max || lilv_node_as_float(def) < lilv_node_as_float(min) || lilv_node_as_float(def) > lilv_node_as_float(max)) bad++;
            lilv_node_free(def); lilv_node_free(min); lilv_node_free(max);
        }
        CHECK(bad == 0, "%d control ports have missing symbols or bad ranges", bad);
        (void)sym_ok;
    }
    /* factory presets are there */
    preset_class = lilv_new_uri(world, LV2_PRESETS__Preset);
    {
        LilvNodes *presets = lilv_plugin_get_related(plugin, preset_class);
        unsigned n = presets ? lilv_nodes_size(presets) : 0;
        CHECK(n >= 50, "%u factory presets", n);
        if (presets) {
            LILV_FOREACH(nodes, it, presets) {
                const LilvNode *pr = lilv_nodes_get(presets, it);
                lilv_world_load_resource(world, pr);
            }
            /* the first preset has a label */
            {
                const LilvNode *pr = lilv_nodes_get_first(presets);
                LilvNode *label_p = lilv_new_uri(world, LILV_NS_RDFS "label");
                LilvNodes *labels = lilv_world_find_nodes(world, pr, label_p, NULL);
                CHECK(labels && lilv_nodes_size(labels) == 1, "first preset has one label");
                lilv_nodes_free(labels);
                lilv_node_free(label_p);
            }
            lilv_nodes_free(presets);
        }
    }

    inst = lilv_plugin_instantiate(plugin, 48000.0, features);
    CHECK(inst != NULL, "instantiate");
    if (!inst) return 1;
    for (i = 0; i < WHYSYNTH_LV2_PARAM_COUNT; i++) {
        const LilvPort *port = lilv_plugin_get_port_by_index(plugin, LV2_PORT_FIRST_PARAM + i);
        LilvNode *def = NULL;
        lilv_port_get_range(plugin, port, &def, NULL, NULL);
        params[i] = def ? lilv_node_as_float(def) : 0.0f;
        lilv_node_free(def);
        lilv_instance_connect_port(inst, LV2_PORT_FIRST_PARAM + i, &params[i]);
    }
    lilv_instance_connect_port(inst, LV2_PORT_CONTROL, control_buf);
    lilv_instance_connect_port(inst, LV2_PORT_NOTIFY, notify_buf);
    lilv_instance_connect_port(inst, LV2_PORT_OUT_LEFT, outl);
    lilv_instance_connect_port(inst, LV2_PORT_OUT_RIGHT, outr);
    lilv_instance_connect_port(inst, LV2_PORT_POLYPHONY, &polyphony);
    lilv_instance_connect_port(inst, LV2_PORT_MONO_MODE, &mono);
    lilv_instance_connect_port(inst, LV2_PORT_GLIDE_MODE, &glide);
    lilv_instance_activate(inst);

    /* defaults are the init voice: Osc1 minBLEP into bus A, so a note sounds */
    params[0] = 1.0f;              /* Osc1 mode minBLEP */
    params[12] = 1.0f;             /* Osc1 -> Bus A level */
    params[72 - 2] = 1.0f;          /* Bus A level */
    {
        double r;
        begin_block(); midi3(0, 0x90, 60, 100); midi3(5, 0x90, 64, 100); lv2_atom_forge_pop(&forge, &seq_frame);
        r = run_blocks(inst, 40);
        CHECK(r > 0.001, "notes audible through LV2: rms %.5f", r);
        begin_block(); midi3(0, 0xB0, 120, 0); lv2_atom_forge_pop(&forge, &seq_frame);
        run_blocks(inst, 4);
        r = run_blocks(inst, 40);
        CHECK(r < 1e-3, "quiet after all sound off: rms %.6f", r);
    }
    /* control ports change the sound: pitch up */
    {
        double a, b;
        begin_block(); midi3(0, 0x90, 60, 100); lv2_atom_forge_pop(&forge, &seq_frame);
        a = run_blocks(inst, 20);
        begin_block(); midi3(0, 0xB0, 120, 0); lv2_atom_forge_pop(&forge, &seq_frame); run_blocks(inst, 4);
        params[12] = 0.1f;   /* much quieter */
        begin_block(); midi3(0, 0x90, 60, 100); lv2_atom_forge_pop(&forge, &seq_frame);
        b = run_blocks(inst, 20);
        CHECK(b < a * 0.5, "control port level change heard (%.4f -> %.4f)", a, b);
        begin_block(); midi3(0, 0xB0, 120, 0); lv2_atom_forge_pop(&forge, &seq_frame); run_blocks(inst, 4);
    }
    /* mono mode port */
    mono = 1.0f;
    begin_block(); midi3(0, 0x90, 60, 100); midi3(1, 0x90, 64, 100); midi3(2, 0x90, 67, 100); lv2_atom_forge_pop(&forge, &seq_frame);
    run_blocks(inst, 2);
    /* nothing observable from outside except no crash; the engine test covers voice counts */

    lilv_instance_deactivate(inst);
    lilv_instance_free(inst);
    lilv_node_free(preset_class);
    lilv_node_free(plugin_uri);
    lilv_node_free(bundle_uri);
    lilv_world_free(world);
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
