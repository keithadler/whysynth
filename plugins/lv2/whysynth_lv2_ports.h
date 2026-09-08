/* WhySynth LV2 port numbering, shared by the plugin and the TTL generator */
#ifndef _WHYSYNTH_LV2_PORTS_H
#define _WHYSYNTH_LV2_PORTS_H

#define WHYSYNTH_LV2_PARAM_COUNT 196

enum {
    LV2_PORT_CONTROL = 0,
    LV2_PORT_NOTIFY,
    LV2_PORT_OUT_LEFT,
    LV2_PORT_OUT_RIGHT,
    LV2_PORT_FIRST_PARAM,                                           /* 4 .. 199 */
    LV2_PORT_POLYPHONY = LV2_PORT_FIRST_PARAM + WHYSYNTH_LV2_PARAM_COUNT,   /* 200 */
    LV2_PORT_MONO_MODE,                                             /* 201 */
    LV2_PORT_GLIDE_MODE,                                            /* 202 */
    LV2_PORT_COUNT
};

#endif
