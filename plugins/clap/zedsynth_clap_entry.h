/* ZedSynth CLAP entry functions, for static linking by clap-wrapper */
#ifndef _ZEDSYNTH_CLAP_ENTRY_H
#define _ZEDSYNTH_CLAP_ENTRY_H
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
bool        zedsynth_clap_init(const char *plugin_path);
void        zedsynth_clap_deinit(void);
const void *zedsynth_clap_get_factory(const char *factory_id);
#ifdef __cplusplus
}
#endif
#endif
