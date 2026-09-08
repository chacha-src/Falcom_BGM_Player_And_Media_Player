#pragma once

/* Playback-verified archive list.

   The CEmu catalog describes far more archives than currently play correctly.
   These helpers answer "does this archive actually play?" from the generated
   table in cemu_support.cpp, so the UI can hide the rest and tell the user
   they are not supported yet instead of opening a silent title.

   `archive` is a CEmuGameEntry::archive value (zip stem, optionally in the
   "stem,companion" form); matching is case-insensitive. */

#ifdef __cplusplus
extern "C" {
#endif

int CEmuArchiveIsSupported(const char* archive);
int CEmuSupportedArchiveCount(void);

#ifdef __cplusplus
}
#endif
