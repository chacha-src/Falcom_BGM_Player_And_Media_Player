#pragma once
#include "SasamiComposerDoc.h"

class CWnd;

/* Composer-side copy of VST Host slot actions (editor / send-ch / program).
   Does not modify VstHostDlg — intentional duplication. */

/* Popup at screenPt for an already-loaded part. Updates bind if non-NULL.
   Returns 1 if bind / labels should refresh. */
int ScVstShowPartMenu(CWnd* owner, int part1to32, CPoint screenPt, ScMidiVstBind* bind);

/* Tone gauge / note-props: pick+load if needed, then open editor / tone map.
   Returns 1 if ok. */
int ScVstAssignToneForPart(CWnd* owner, int part1to32, ScMidiVstBind* bind);

/* Instrument picker → load into part (used by tone map VST3… button). */
int ScVstPickLoadForPart(CWnd* owner, int part1to32, ScMidiVstBind* bind);
