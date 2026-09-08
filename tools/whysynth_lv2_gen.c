/* whysynth-lv2-gen: write the LV2 plugin description and factory presets
 *
 * Copyright (C) 2026 Keith Adler. GPL-2.0-or-later.
 *
 *   whysynth-lv2-gen ttl OUT
 *   whysynth-lv2-gen presets OUT [patchfile]
 *   whysynth-lv2-gen manifest OUT [suffix]
 *
 * Both are derived from the port table and the factory patches at build
 * time, so the LV2 bundle can never drift from the synth.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "whysynth_engine.h"
#include "whysynth_lv2_ports.h"

#define URI "https://github.com/keithadler/whysynth"

static void
put_num(FILE *f, float v)
{
    char buf[48];
    int i;
    snprintf(buf, sizeof(buf), "%.6g", (double)v);
    for (i = 0; buf[i]; i++) if (buf[i] == ',') buf[i] = '.';
    if (!strchr(buf, '.') && !strchr(buf, 'e') && !strchr(buf, 'n') && !strchr(buf, 'i')) strcat(buf, ".0");
    fputs(buf, f);
}

static void
put_ttl_string(FILE *f, const char *s)
{
    fputc('"', f);
    for (; *s; s++) {
        if (*s == '"' || *s == '\\') fputc('\\', f);
        fputc(*s, f);
    }
    fputc('"', f);
}

static void
write_ttl(FILE *f)
{
    int port;

    fprintf(f,
        "@prefix atom:  <http://lv2plug.in/ns/ext/atom#> .\n"
        "@prefix doap:  <http://usefulinc.com/ns/doap#> .\n"
        "@prefix foaf:  <http://xmlns.com/foaf/0.1/> .\n"
        "@prefix lv2:   <http://lv2plug.in/ns/lv2core#> .\n"
        "@prefix midi:  <http://lv2plug.in/ns/ext/midi#> .\n"
        "@prefix pg:    <http://lv2plug.in/ns/ext/port-groups#> .\n"
        "@prefix rdf:   <http://www.w3.org/1999/02/22-rdf-syntax-ns#> .\n"
        "@prefix rdfs:  <http://www.w3.org/2000/01/rdf-schema#> .\n"
        "@prefix units: <http://lv2plug.in/ns/extensions/units#> .\n"
        "@prefix urid:  <http://lv2plug.in/ns/ext/urid#> .\n"
        "\n"
        "<" URI ">\n"
        "    a lv2:Plugin , lv2:InstrumentPlugin ;\n"
        "    doap:name \"WhySynth\" ;\n"
        "    doap:license <http://opensource.org/licenses/gpl-2.0> ;\n"
        "    doap:maintainer [ foaf:name \"Keith Adler\" ; foaf:homepage <" URI "> ] ;\n"
        "    doap:developer [ foaf:name \"Sean Bolton\" ] ;\n"
        "    rdfs:comment \"Versatile multi-mode synthesizer: 4 oscillators, 2 filters, 3 LFOs, 5 envelopes, effects.\" ;\n"
        "    lv2:minorVersion 2 ;\n"
        "    lv2:microVersion 0 ;\n"
        "    lv2:requiredFeature urid:map ;\n"
        "    lv2:optionalFeature lv2:hardRTCapable ;\n"
        "    lv2:port [\n"
        "        a lv2:InputPort , atom:AtomPort ;\n"
        "        atom:bufferType atom:Sequence ;\n"
        "        atom:supports midi:MidiEvent ;\n"
        "        lv2:designation lv2:control ;\n"
        "        lv2:index %d ;\n"
        "        lv2:symbol \"control\" ;\n"
        "        lv2:name \"Control\"\n"
        "    ] , [\n"
        "        a lv2:OutputPort , atom:AtomPort ;\n"
        "        atom:bufferType atom:Sequence ;\n"
        "        lv2:designation lv2:control ;\n"
        "        lv2:index %d ;\n"
        "        lv2:symbol \"notify\" ;\n"
        "        lv2:name \"Notify\"\n"
        "    ] , [\n"
        "        a lv2:AudioPort , lv2:OutputPort ;\n"
        "        lv2:index %d ;\n"
        "        lv2:symbol \"out_left\" ;\n"
        "        lv2:name \"Output Left\"\n"
        "    ] , [\n"
        "        a lv2:AudioPort , lv2:OutputPort ;\n"
        "        lv2:index %d ;\n"
        "        lv2:symbol \"out_right\" ;\n"
        "        lv2:name \"Output Right\"\n"
        "    ]",
        LV2_PORT_CONTROL, LV2_PORT_NOTIFY, LV2_PORT_OUT_LEFT, LV2_PORT_OUT_RIGHT);

    for (port = WHYSYNTH_PORT_FIRST_PARAM; port < WHYSYNTH_PORT_COUNT; port++) {
        const whysynth_param_info_t *pi = whysynth_param_info(port);
        char sym[64];
        int v;

        whysynth_param_symbol(port, sym, sizeof(sym));
        fprintf(f, " , [\n        a lv2:InputPort , lv2:ControlPort ;\n        lv2:index %d ;\n        lv2:symbol \"%s\" ;\n        lv2:name ",
                port - WHYSYNTH_PORT_FIRST_PARAM + LV2_PORT_FIRST_PARAM, sym);
        put_ttl_string(f, pi->name);
        fprintf(f, " ;\n        lv2:default "); put_num(f, pi->def);
        fprintf(f, " ;\n        lv2:minimum "); put_num(f, pi->min);
        fprintf(f, " ;\n        lv2:maximum "); put_num(f, pi->max);
        if (pi->is_integer) {
            fprintf(f, " ;\n        lv2:portProperty lv2:integer");
            if (pi->kind == WHYSYNTH_KIND_COMBO) fprintf(f, " , lv2:enumeration");
            if (pi->kind == WHYSYNTH_KIND_BOOLEAN) fprintf(f, " , lv2:toggled");
        } else if (pi->kind == WHYSYNTH_KIND_LOGARITHMIC) {
            fprintf(f, " ;\n        lv2:portProperty <http://lv2plug.in/ns/ext/port-props#logarithmic>");
        }
        if (port == 197) fprintf(f, " ;\n        units:unit units:hz");
        if (pi->kind == WHYSYNTH_KIND_COMBO) {
            int first = 1;
            for (v = (int)pi->min; v <= (int)pi->max; v++) {
                const char *name = whysynth_param_value_name(port, v);
                if (!name) continue;
                fprintf(f, first ? " ;\n        lv2:scalePoint [ rdfs:label " : " ,\n                       [ rdfs:label ");
                put_ttl_string(f, name);
                fprintf(f, " ; rdf:value %d ]", v);
                first = 0;
            }
        }
        fprintf(f, "\n    ]");
    }

    fprintf(f,
        " , [\n"
        "        a lv2:InputPort , lv2:ControlPort ;\n"
        "        lv2:index %d ;\n"
        "        lv2:symbol \"polyphony\" ;\n"
        "        lv2:name \"Polyphony\" ;\n"
        "        lv2:portProperty lv2:integer ;\n"
        "        lv2:default %d ;\n        lv2:minimum 1 ;\n        lv2:maximum %d\n"
        "    ] , [\n"
        "        a lv2:InputPort , lv2:ControlPort ;\n"
        "        lv2:index %d ;\n"
        "        lv2:symbol \"voice_mode\" ;\n"
        "        lv2:name \"Voice mode\" ;\n"
        "        lv2:portProperty lv2:integer , lv2:enumeration ;\n"
        "        lv2:default 0 ;\n        lv2:minimum 0 ;\n        lv2:maximum 3 ;\n"
        "        lv2:scalePoint [ rdfs:label \"Poly\" ; rdf:value 0 ] , [ rdfs:label \"Mono\" ; rdf:value 1 ] ,\n"
        "                       [ rdfs:label \"Mono legato\" ; rdf:value 2 ] , [ rdfs:label \"Mono both\" ; rdf:value 3 ]\n"
        "    ] , [\n"
        "        a lv2:InputPort , lv2:ControlPort ;\n"
        "        lv2:index %d ;\n"
        "        lv2:symbol \"glide_mode\" ;\n"
        "        lv2:name \"Glide mode\" ;\n"
        "        lv2:portProperty lv2:integer , lv2:enumeration ;\n"
        "        lv2:default 0 ;\n        lv2:minimum 0 ;\n        lv2:maximum 4 ;\n"
        "        lv2:scalePoint [ rdfs:label \"Legato\" ; rdf:value 0 ] , [ rdfs:label \"Initial\" ; rdf:value 1 ] ,\n"
        "                       [ rdfs:label \"Always\" ; rdf:value 2 ] , [ rdfs:label \"Leftover\" ; rdf:value 3 ] ,\n"
        "                       [ rdfs:label \"Off\" ; rdf:value 4 ]\n"
        "    ] .\n",
        LV2_PORT_POLYPHONY, WHYSYNTH_DEFAULT_POLYPHONY, WHYSYNTH_MAX_POLYPHONY,
        LV2_PORT_MONO_MODE, LV2_PORT_GLIDE_MODE);
}

static void
preset_uri_part(const char *name, int index, char *buf, size_t size)
{
    size_t n = 0;
    int i, last_us = 0;
    n = (size_t)snprintf(buf, size, "p%03d_", index + 1);
    for (i = 0; name[i] && n + 1 < size; i++) {
        unsigned char c = (unsigned char)name[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) { buf[n++] = (char)c; last_us = 0; }
        else if (!last_us) { buf[n++] = '_'; last_us = 1; }
    }
    while (n > 0 && buf[n - 1] == '_') n--;
    buf[n] = 0;
}

/* hosts discover presets from the manifest, so every preset is listed there */
static void
write_manifest(FILE *f, whysynth_engine_t *e, const char *suffix)
{
    int count = whysynth_engine_patch_count(e), i;

    fprintf(f,
        "@prefix lv2:  <http://lv2plug.in/ns/lv2core#> .\n"
        "@prefix pset: <http://lv2plug.in/ns/ext/presets#> .\n"
        "@prefix rdfs: <http://www.w3.org/2000/01/rdf-schema#> .\n\n"
        "<" URI ">\n    a lv2:Plugin ;\n    lv2:binary <whysynth%s> ;\n    rdfs:seeAlso <whysynth.ttl> .\n\n", suffix);
    for (i = 0; i < count; i++) {
        char part[96];
        preset_uri_part(whysynth_engine_patch_name(e, i), i, part, sizeof(part));
        fprintf(f, "<" URI "#%s>\n    a pset:Preset ;\n    lv2:appliesTo <" URI "> ;\n    rdfs:seeAlso <presets.ttl> .\n\n", part);
    }
}

static void
write_presets(FILE *f, whysynth_engine_t *e)
{
    int count = whysynth_engine_patch_count(e);
    int i, port;
    float ports[WHYSYNTH_PORT_COUNT];

    fprintf(f,
        "@prefix atom:  <http://lv2plug.in/ns/ext/atom#> .\n"
        "@prefix lv2:   <http://lv2plug.in/ns/lv2core#> .\n"
        "@prefix pset:  <http://lv2plug.in/ns/ext/presets#> .\n"
        "@prefix rdfs:  <http://www.w3.org/2000/01/rdf-schema#> .\n"
        "@prefix state: <http://lv2plug.in/ns/ext/state#> .\n"
        "@prefix xsd:   <http://www.w3.org/2001/XMLSchema#> .\n\n");

    for (i = 0; i < count; i++) {
        char part[96], sym[64];
        const char *name = whysynth_engine_patch_name(e, i);
        whysynth_engine_select_program(e, i);
        whysynth_engine_get_params(e, ports);
        preset_uri_part(name, i, part, sizeof(part));
        fprintf(f, "<" URI "#%s>\n    a pset:Preset ;\n    lv2:appliesTo <" URI "> ;\n    rdfs:label ", part);
        put_ttl_string(f, name);
        fprintf(f, " ;\n    lv2:port");
        for (port = WHYSYNTH_PORT_FIRST_PARAM; port < WHYSYNTH_PORT_COUNT; port++) {
            whysynth_param_symbol(port, sym, sizeof(sym));
            fprintf(f, "%s [ lv2:symbol \"%s\" ; pset:value ", port == WHYSYNTH_PORT_FIRST_PARAM ? "" : " ,", sym);
            put_num(f, ports[port]);
            fprintf(f, " ]");
        }
        fprintf(f, " .\n\n");
    }
}

int
main(int argc, char **argv)
{
    const char *mode = argc > 1 ? argv[1] : "";
    const char *out_path = argc > 2 ? argv[2] : NULL;
    FILE *out;
    int rc = 0;

    if (!out_path || (strcmp(mode, "ttl") && strcmp(mode, "presets") && strcmp(mode, "manifest"))) {
        fprintf(stderr, "usage: whysynth-lv2-gen ttl OUT | presets OUT [patchfile] | manifest OUT [suffix]\n");
        return 2;
    }
    out = fopen(out_path, "w");
    if (!out) { fprintf(stderr, "cannot write %s\n", out_path); return 1; }

    if (!strcmp(mode, "ttl")) {
        write_ttl(out);
    } else {
        whysynth_engine_t *e = whysynth_engine_new(48000.0f);
        if (!e) { fprintf(stderr, "could not create engine\n"); fclose(out); return 1; }
        if (!strcmp(mode, "manifest")) {
            write_manifest(out, e, argc > 3 ? argv[3] : ".so");
        } else {
            if (argc > 3) {
                char *err = NULL;
                if (!whysynth_engine_load_patches_file(e, argv[3], &err)) {
                    fprintf(stderr, "could not load %s: %s\n", argv[3], err ? err : "?");
                    rc = 1;
                }
            }
            if (!rc) write_presets(out, e);
        }
        whysynth_engine_free(e);
    }
    fclose(out);
    return rc;
}
