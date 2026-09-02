#pragma once
#include "SasamiComposerDoc.h"

class CWnd;

/* Composer-side copy of VST Host slot actions (editor / send-ch / program).
   Does not modify VstHostDlg — intentional duplication. */

/* Popup at screenPt for an already-loaded part. Updates bind if non-NULL.
   Returns 1 if bind / labels should refresh. */
int ScVstShowPartMenu(CWnd* owner, int part1to32, CPoint screenPt, ScMidiVstBind* bind);

/* Tone gauge / note-props: pick+load if needed, then tone map or dedicated.
   Returns: 0=cancel/fail, 1=ok (GS/XG tone map — do NOT open editor),
   2=ok dedicated VST3-style (caller may open editor). */
int ScVstAssignToneForPart(CWnd* owner, int part1to32, ScMidiVstBind* bind);
/* Instrument picker → load into part (used by tone map VST3… button). */
int ScVstPickLoadForPart(CWnd* owner, int part1to32, ScMidiVstBind* bind);
/* 1 = VST2 multi-timbre — mid-score prog/bank OK. VST3 / dedicated → tick 0 only. */
int ScMidiPartAllowMidScoreTone(const ScMidiVstBind* bind, int ch0);
