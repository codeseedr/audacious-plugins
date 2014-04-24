#ifndef MODPLUG_PLUGIN_H
#define MODPLUG_PLUGIN_H

#include "settings.h"

#ifdef __cplusplus
extern "C" {
#endif

#include <libaudcore/plugin.h>

void InitSettings (const ModplugSettings * settings);
int CanPlayFileFromVFS (const char * filename, VFSFile * file);
bool_t PlayFile (const char * filename, VFSFile * file);
Tuple * GetSongTuple (const char * filename, VFSFile * file);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* MODPLUG_PLUGIN_H */
