#pragma once
#include "afxwin.h"

class CWnd;

/* Computer-keyboard piano (OCT -/+, green keycaps). Sends live MIDI so a
   dedicated part hears only its channel and the rest stays on the shared VST2.
   stepTarget: score window that accepts WM_SASAMI_SCORE_MIDI with lParam
   SC_SOFTKEY_STEP_* (STEP button shown only when set). part0 < 0 keeps ch. */
enum {
	SC_SOFTKEY_STEP_NOTE = 1, /* wParam = short MIDI msg */
	SC_SOFTKEY_STEP_REST = 2,
	SC_SOFTKEY_STEP_BACK = 3
};
void ScSoftKeyOpen(CWnd* owner, CWnd* stepTarget = NULL, int part0 = -1);
void ScSoftKeySetPart(int part0);
void ScSoftKeyDetach(CWnd* stepTarget);
