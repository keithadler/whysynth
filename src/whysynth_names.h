/* WhySynth - display names for stepped parameter values
 *
 * Copyright (C) 2026 Keith Adler. GPL-2.0-or-later.
 * The names are those of the GTK editor, so users see the same words.
 */

#ifndef _WHYSYNTH_NAMES_H
#define _WHYSYNTH_NAMES_H

/* combo_type is a Y_COMBO_TYPE_* value from whysynth_ports.h; returns NULL
 * when the value has no name */
const char *whysynth_combo_value_name(int combo_type, int value);

#endif
