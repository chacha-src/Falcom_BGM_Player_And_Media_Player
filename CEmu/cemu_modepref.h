#pragma once
#include "cemu_types.h"
#include "cemu_catalog.h"
#include "cemu_zipfs.h"

/* Playlist chip / context-menu labels for multi-subtype archives.
   Sidecar under %LOCALAPPDATA%\oggYSED\cemumode\ — does not touch playlistdata0 / save.dat. */

enum { CEMU_MODE_MAX = 24, CEMU_MODE_TAG = 16 };

struct CEmuArchiveMode {
	char tag[CEMU_MODE_TAG];   /* OPNA / OPN / OPM / OPLL / MIDI / 86 / BEEP … */
	char subtype[CEMU_DRIVER_TYPE];
	int isMidi;
	int entryIndex;            /* index into CEmuCatalog::entry, or -1 */
};

/* Map catalog entry → display tag. Returns 1 on success. */
int CEmuModeTagFromEntry(const CEmuGameEntry* e, char* tag, int tagCap);

int CEmuModeIsMidiTag(const char* tag);

/* Unique modes for archive (dedupe by tag). Prefer non-MIDI default ordering. */
int CEmuCatalogListArchiveModes(const CEmuCatalog* cat, const char* archive,
	const char* dataDirHint, const CEmuZipFs* zipFs,
	CEmuArchiveMode* out, int outCap);

/* Like FindArchiveForZip, but if preferTag is set pick that mode; else best non-MIDI. */
const CEmuGameEntry* CEmuCatalogFindArchiveForZipMode(const CEmuCatalog* cat,
	const char* archive, const char* dataDirHint, const CEmuZipFs* zipFs,
	const char* preferTag);

/* Per-zip mode preference (physical zip path). Empty tag clears. */
int CEmuModePrefGet(const wchar_t* zipPath, char* tagOut, int tagCap);
void CEmuModePrefSet(const wchar_t* zipPath, const char* tag);

/* First .mid/.rmi/.smf inside zip → temp file for KPI/VST MIDI play. */
int CEmuZipExtractFirstMidi(const wchar_t* zipPath, wchar_t* outMidPath, int outCap);

/* Boot PCAT midiout glue, capture MPU-401 UART → Type-0 SMF for KPI/VST. */
int CEmuCapturePcatMidiToFile(const wchar_t* zipPath, unsigned titleCode,
	wchar_t* outMidPath, int outCap);

/* Realtime stream: CEmuMidiLive* in cemu_midi_live.h (stub SMF + inject). */
