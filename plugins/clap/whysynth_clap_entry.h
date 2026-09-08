/* WhySynth CLAP entry functions, for static linking by clap-wrapper */
#ifndef _WHYSYNTH_CLAP_ENTRY_H
#define _WHYSYNTH_CLAP_ENTRY_H
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
bool        whysynth_clap_init(const char *plugin_path);
void        whysynth_clap_deinit(void);
const void *whysynth_clap_get_factory(const char *factory_id);
#ifdef __cplusplus
}
#endif
#endif
