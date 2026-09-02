#include "stdafx.h"
#include "CSasamiScoreArrange.h"
#include "CSasamiStaffCore.h"
#include <stdlib.h>
#include <math.h>

static int IsSel(const ScStaffUi* u, int idx)
{
	if (!u || idx < 0) return 0;
	if (u->nSel <= 0) return (u->selEv == idx) ? 1 : 0;
	for (int i = 0; i < u->nSel && i < SC_SEL_MAX; i++)
		if (u->selList[i] == idx) return 1;
	return 0;
}

static int NoteKindOk(uint8_t k)
{
	return k == SC_EV_NOTE || k == SC_EV_FM_NOTE;
}

static int InScope(const ScEvent& e, int idx, const ScStaffUi* u, int part, int scope)
{
	if (!NoteKindOk(e.kind)) return 0;
	if (scope == SC_ARR_ALL) return 1;
	if (scope == SC_ARR_PART) return e.ch == (uint8_t)part;
	return IsSel(u, idx) ? 1 : 0;
}

static int RandSigned(int maxAbs)
{
	if (maxAbs <= 0) return 0;
	return (rand() % (maxAbs * 2 + 1)) - maxAbs;
}

const wchar_t* ScArrangePresetName(int preset)
{
	switch (preset) {
	case SC_ARR_HUMANIZE: return LL14(L"ヒューマナイズ", L"Humanize", L"Humaniser", L"Humanizza", L"Humanizar",
		L"휴머나이즈", L"人性化", L"أنسنة", L"Очеловечить", L"Humanisieren", L"Humanizar", L"Humaniseren", L"Humanizuj", L"İnsansılaştır");
	case SC_ARR_PUSH_PULL: return LL14(L"プッシュ/プル", L"Push/Pull", L"Push/Pull", L"Push/Pull", L"Push/Pull",
		L"푸시/풀", L"推拉", L"دفع/سحب", L"Push/Pull", L"Push/Pull", L"Push/Pull", L"Push/Pull", L"Push/Pull", L"Push/Pull");
	case SC_ARR_EXP_WAVE: return LL14(L"Exp起伏", L"Exp wave", L"Vague Exp", L"Onda Exp", L"Onda Exp",
		L"Exp 파형", L"Exp起伏", L"موجة Exp", L"Волна Exp", L"Exp-Welle", L"Onda Exp", L"Exp-golf", L"Fala Exp", L"Exp dalga");
	case SC_ARR_DRUM_HUMAN: return LL14(L"ドラム実演", L"Drum feel", L"Batterie vivante", L"Batteria viva", L"Bateria viva",
		L"드럼 실연", L"鼓组真人感", L"إيقاع حي", L"Живые барабаны", L"Drum-Feel", L"Bateria viva", L"Drumgevoel", L"Żywe bębny", L"Davul hissi");
	default: return L"?";
	}
}

const wchar_t* ScChordTypeName(int type)
{
	switch (type) {
	case SC_CHORD_OCTAVE: return LL14(L"オクターブ", L"Octave", L"Octave", L"Ottava", L"Octava", L"옥타브", L"八度", L"أوكتاف", L"Октава", L"Oktave", L"Oitava", L"Octaaf", L"Oktawa", L"Oktav");
	case SC_CHORD_POWER: return LL14(L"パワー (1+5)", L"Power (1+5)", L"Power (1+5)", L"Power (1+5)", L"Power (1+5)", L"파워", L"强力和弦", L"باور", L"Power", L"Power", L"Power", L"Power", L"Power", L"Power");
	case SC_CHORD_FIFTHS: return LL14(L"5度系", L"Fifths", L"Quintes", L"Quinte", L"Quintas", L"5도", L"五度", L"خوامس", L"Квинты", L"Quinten", L"Quintas", L"Kwinten", L"Kwinty", L"Beşliler");
	case SC_CHORD_MAJOR: return LL14(L"メジャー", L"Major", L"Majeur", L"Maggiore", L"Mayor", L"메이저", L"大调", L"ماجور", L"Мажор", L"Dur", L"Maior", L"Majeur", L"Dur", L"Majör");
	case SC_CHORD_MINOR: return LL14(L"マイナー", L"Minor", L"Mineur", L"Minore", L"Menor", L"마이너", L"小调", L"مينور", L"Минор", L"Moll", L"Menor", L"Mineur", L"Mol", L"Minör");
	case SC_CHORD_SUS4: return LL14(L"サス4", L"Sus4", L"Sus4", L"Sus4", L"Sus4", L"서스4", L"挂四", L"معلق4", L"Sus4", L"Sus4", L"Sus4", L"Sus4", L"Sus4", L"Sus4");
	case SC_CHORD_ADD9: return LL14(L"アド9", L"Add9", L"Add9", L"Add9", L"Add9", L"애드9", L"加九", L"إضافة9", L"Add9", L"Add9", L"Add9", L"Add9", L"Add9", L"Add9");
	case SC_CHORD_MAJ7: return LL14(L"メジャー7", L"Maj7", L"Maj7", L"Maj7", L"Maj7", L"메이저7", L"大七", L"ماجور7", L"Маж7", L"Maj7", L"Maj7", L"Maj7", L"Maj7", L"Maj7");
	case SC_CHORD_MIN7: return LL14(L"マイナー7", L"Min7", L"Min7", L"Min7", L"Min7", L"마이너7", L"小七", L"مينور7", L"Мин7", L"Min7", L"Min7", L"Min7", L"Min7", L"Min7");
	default: return LL14(L"?", L"?", L"?", L"?", L"?", L"?", L"?", L"?", L"?", L"?", L"?", L"?", L"?", L"?");
	}
}

const wchar_t* ScPatternName(int id)
{
	switch (id) {
	case SC_PAT_Q_1BAR: return LL14(L"4分×1小節", L"Quarter ×1 bar", L"Noires ×1", L"Semiminime ×1", L"Negras ×1", L"4분×1마디", L"四分×1小节", L"سوداء ×1", L"Четверти ×1", L"Viertel ×1", L"Semínimas ×1", L"Kwarten ×1", L"Ćwierć ×1", L"Dörtlük ×1");
	case SC_PAT_E_1BAR: return LL14(L"8分×1小節", L"8ths ×1 bar", L"Croches ×1", L"Crome ×1", L"Corcheas ×1", L"8분×1", L"八分×1", L"ثامنة ×1", L"Восьмые ×1", L"Achtel ×1", L"Colcheias ×1", L"Achtsten ×1", L"Ósemki ×1", L"Sekizlik ×1");
	case SC_PAT_S_1BAR: return LL14(L"16分×1小節", L"16ths ×1 bar", L"Doubles ×1", L"Semicrome ×1", L"Semicorcheas ×1", L"16분×1", L"十六分×1", L"16 ×1", L"1/16 ×1", L"16tel ×1", L"16 ×1", L"16en ×1", L"16 ×1", L"16’lık ×1");
	case SC_PAT_E_SS: return LL14(L"8+16+16 /拍", L"8+16+16 /beat", L"8+16+16 /temps", L"8+16+16 /batt", L"8+16+16 /pulso", L"8+16+16/박", L"8+16+16/拍", L"8+16+16", L"8+16+16", L"8+16+16 /Schlag", L"8+16+16", L"8+16+16", L"8+16+16", L"8+16+16");
	case SC_PAT_SS_E: return LL14(L"16+16+8 /拍", L"16+16+8 /beat", L"16+16+8", L"16+16+8", L"16+16+8", L"16+16+8", L"16+16+8", L"16+16+8", L"16+16+8", L"16+16+8", L"16+16+8", L"16+16+8", L"16+16+8", L"16+16+8");
	case SC_PAT_TRIPLET_1BAR: return LL14(L"3連符×1小節", L"Triplets ×1 bar", L"Triolets ×1", L"Terzine ×1", L"Tresillos ×1", L"셋잇단×1", L"三连音×1", L"ثلاثي ×1", L"Триоли ×1", L"Triolen ×1", L"Tercinas ×1", L"Triolen ×1", L"Triole ×1", L"Üçleme ×1");
	case SC_PAT_Q_REST_Q: return LL14(L"4分休符+4分", L"Q rest + Q", L"Soupir+noire", L"Pausa+semiminima", L"Silencio+negra", L"4분쉼+4분", L"四分休+四分", L"سكتة+سوداء", L"Пауза+четверть", L"Pause+Viertel", L"Pausa+semínima", L"Rust+kwart", L"Pauza+ćwierć", L"Es+dörtlük");
	case SC_PAT_DOTTED_E: return LL14(L"付点8分+16分", L"Dotted 8th+16th", L"Croche pointée+16", L"Croma puntata+16", L"Corchea c/puntillo+16", L"점8분+16", L"附点八+十六", L"منقوطة+16", L"Пунктир 8+16", L"Punktierte 8+16", L"Colcheia pont.+16", L"Gepunte 8+16", L"Ósemka z kropką+16", L"Noktalı 8+16");
	case SC_PAT_2BAR_E: return LL14(L"8分×2小節", L"8ths ×2 bars", L"Croches ×2", L"Crome ×2", L"Corcheas ×2", L"8분×2", L"八分×2", L"ثامنة ×2", L"Восьмые ×2", L"Achtel ×2", L"Colcheias ×2", L"Achtsten ×2", L"Ósemki ×2", L"Sekizlik ×2");
	case SC_PAT_4BAR_Q: return LL14(L"4分×4小節", L"Quarter ×4 bars", L"Noires ×4", L"Semiminime ×4", L"Negras ×4", L"4분×4", L"四分×4", L"سوداء ×4", L"Четверти ×4", L"Viertel ×4", L"Semínimas ×4", L"Kwarten ×4", L"Ćwierć ×4", L"Dörtlük ×4");
	case SC_PAT_8BAR_Q: return LL14(L"4分×8小節", L"Quarter ×8 bars", L"Noires ×8", L"Semiminime ×8", L"Negras ×8", L"4분×8", L"四分×8", L"سوداء ×8", L"Четверти ×8", L"Viertel ×8", L"Semínimas ×8", L"Kwarten ×8", L"Ćwierć ×8", L"Dörtlük ×8");
	default: return L"?";
	}
}

static int PushEv(ScEvent* ev, int* evCount, int evMax, uint32_t tick, uint8_t ch, uint8_t kind, uint8_t a, uint8_t b, uint8_t c, uint16_t dur)
{
	if (!ev || !evCount || *evCount >= evMax) return 0;
	ScEvent& e = ev[*evCount];
	e.tick = tick;
	e.seq = (uint32_t)(*evCount);
	e.ch = ch;
	e.kind = kind;
	e.a = a; e.b = b; e.c = c;
	e.dur = dur;
	e.flags = 0;
	(*evCount)++;
	return 1;
}

int ScChordIntervals(int type, int* outSemis, int maxOut)
{
	if (!outSemis || maxOut < 2) return 0;
	int n = 0;
	auto add = [&](int s) { if (n < maxOut) outSemis[n++] = s; };
	add(0);
	switch (type) {
	case SC_CHORD_OCTAVE: add(12); break;
	case SC_CHORD_POWER: add(7); break;
	case SC_CHORD_FIFTHS: add(7); add(12); break;
	case SC_CHORD_MAJOR: add(4); add(7); break;
	case SC_CHORD_MINOR: add(3); add(7); break;
	case SC_CHORD_SUS4: add(5); add(7); break;
	case SC_CHORD_ADD9: add(4); add(7); add(14); break;
	case SC_CHORD_MAJ7: add(4); add(7); add(11); break;
	case SC_CHORD_MIN7: add(3); add(7); add(10); break;
	default: add(12); break;
	}
	return n;
}

int ScChordBuildPitches(int rootMidi, int chordType, int voices, int* outMidi, int maxOut)
{
	if (!outMidi || maxOut < 1) return 0;
	int semis[8];
	int nv = ScChordIntervals(chordType, semis, 8);
	if (voices < 1) voices = nv;
	if (voices > nv) voices = nv;
	if (voices > maxOut) voices = maxOut;
	int n = 0;
	for (int i = 0; i < voices; i++) {
		int note = rootMidi + semis[i];
		if (note < 0) note = 0;
		if (note > 127) note = 127;
		outMidi[n++] = note;
	}
	return n;
}

static uint8_t ScPackFmNoteByte(int midiNote)
{
	int oct = midiNote / 12 - 1;
	int scale = midiNote % 12;
	if (oct < 0) oct = 0;
	if (oct > 9) oct = 9;
	if (scale < 0) scale = 0;
	if (scale > 11) scale = 11;
	return (uint8_t)(((oct & 0x0F) << 4) | (scale & 0x0F));
}

int ScChordPlaceAt(ScEvent* ev, int* evCount, int evMax, int part,
	uint32_t tick, int rootMidi, int dur, int vel, int gate, int chordType, int voices,
	uint8_t noteKind, int fmPack)
{
	int pitches[8];
	int nv = ScChordBuildPitches(rootMidi, chordType, voices, pitches, 8);
	if (nv < 1) return 0;
	if (dur < 1) dur = SC_PPQN;
	if (vel < 1) vel = 100;
	if (gate < 1) gate = 100;
	if (gate > 100) gate = 100;
	int n = 0;
	for (int i = 0; i < nv; i++) {
		uint8_t a = fmPack ? ScPackFmNoteByte(pitches[i]) : (uint8_t)pitches[i];
		uint8_t flags = 0;
		/* Mark sharps for black keys so engraved accidentals show next to seconds/unisons. */
		static const int kBlack[12] = { 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0 };
		if (!fmPack && kBlack[pitches[i] % 12])
			flags = (uint8_t)SC_EF_ACC_SHARP;
		if (!PushEv(ev, evCount, evMax, tick, (uint8_t)part, noteKind, a, (uint8_t)vel, (uint8_t)gate, (uint16_t)dur))
			break;
		ev[*evCount - 1].flags = flags;
		n++;
	}
	return n;
}

int ScPatternSpanTicks(int patternId, int bars, int meterNumer, int meterDenom, int ppqn)
{
	if (ppqn <= 0) ppqn = SC_PPQN;
	if (meterNumer < 1) meterNumer = 4;
	if (meterDenom < 1) meterDenom = 4;
	if (bars < 1) bars = 1;
	int useBars = bars;
	if (patternId == SC_PAT_2BAR_E) useBars = 2;
	if (patternId == SC_PAT_4BAR_Q) useBars = 4;
	if (patternId == SC_PAT_8BAR_Q) useBars = 8;
	const int beatTicks = (ppqn * 4) / meterDenom;
	return useBars * beatTicks * meterNumer;
}

int ScDeleteNotesInRange(ScEvent* ev, int* evCount, int part, uint32_t t0, uint32_t t1)
{
	if (!ev || !evCount) return 0;
	int w = 0, n = 0;
	for (int i = 0; i < *evCount; i++) {
		const ScEvent& e = ev[i];
		if (e.ch == (uint8_t)part && NoteKindOk(e.kind) && e.tick >= t0 && e.tick < t1) {
			n++;
			continue;
		}
		ev[w++] = e;
	}
	*evCount = w;
	return n;
}

int ScArrangeApply(ScEvent* ev, int* evCount, const ScStaffUi* u, int part,
	int scope, int preset, int strength)
{
	if (!ev || !evCount || !u) return 0;
	if (strength < 0) strength = 0;
	if (strength > 100) strength = 100;
	const int amt = strength;
	int changed = 0;
	const int isDrumPart = (part == 9) || (u->clef[part] == 3);

	for (int i = 0; i < *evCount; i++) {
		ScEvent& e = ev[i];
		if (!InScope(e, i, u, part, scope)) continue;
		if (preset == SC_ARR_HUMANIZE || (preset == SC_ARR_DRUM_HUMAN && isDrumPart)) {
			int dv = RandSigned(amt * 12 / 100);
			int v = (int)e.b + dv;
			if (preset == SC_ARR_DRUM_HUMAN) {
				/* BD quieter accents, HH more variance */
				if (e.a == 36 || e.a == 35) v = 90 + RandSigned(amt / 8);      /* kick */
				else if (e.a == 38 || e.a == 40) v = 100 + RandSigned(amt / 6); /* snare */
				else if (e.a >= 42 && e.a <= 46) v = 70 + RandSigned(amt / 4); /* hats */
			}
			if (v < 1) v = 1; if (v > 127) v = 127;
			e.b = (uint8_t)v;
			int g = (e.c >= 1 && e.c <= 100) ? e.c : 100;
			g += RandSigned(amt / 10);
			if (g < 20) g = 20; if (g > 100) g = 100;
			e.c = (uint8_t)g;
			changed++;
		} else if (preset == SC_ARR_PUSH_PULL) {
			int dt = RandSigned(amt * SC_PPQN / 400);
			int64_t t = (int64_t)e.tick + dt;
			if (t < 0) t = 0;
			e.tick = (uint32_t)t;
			changed++;
		} else if (preset == SC_ARR_EXP_WAVE || (preset == SC_ARR_DRUM_HUMAN && !isDrumPart)) {
			/* bake into note vel as stand-in for exp when no strip rewrite here */
			double ph = (double)e.tick / (double)SC_PPQN;
			int wave = (int)(sin(ph * 0.5) * (amt * 20 / 100));
			int v = (int)e.b + wave;
			if (v < 1) v = 1; if (v > 127) v = 127;
			e.b = (uint8_t)v;
			changed++;
		} else if (preset == SC_ARR_DRUM_HUMAN) {
			/* non-drum fallthrough already handled */
		}
	}
	return changed;
}

int ScChordExpandNotes(ScEvent* ev, int* evCount, int evMax, int part,
	uint32_t t0, uint32_t t1, int chordType, int voices)
{
	if (!ev || !evCount) return 0;
	int semis[8];
	int nv = ScChordIntervals(chordType, semis, 8);
	if (voices < 2) voices = 2;
	if (voices > nv) voices = nv;
	if (voices > 5) voices = 5;
	int added = 0;
	const int n0 = *evCount;
	for (int i = 0; i < n0; i++) {
		const ScEvent e = ev[i];
		if (e.ch != (uint8_t)part || !NoteKindOk(e.kind)) continue;
		if (e.tick < t0 || e.tick >= t1) continue;
		for (int v = 1; v < voices; v++) {
			int midi = (int)e.a;
			if (e.kind == SC_EV_FM_NOTE)
				midi = (((e.a >> 4) & 0x0F) * 12 + (e.a & 0x0F) + 12);
			midi += semis[v];
			if (midi < 0) midi = 0;
			if (midi > 127) midi = 127;
			uint8_t na = (uint8_t)midi;
			if (e.kind == SC_EV_FM_NOTE) {
				int oct = midi / 12 - 1;
				int sc = midi % 12;
				if (oct < 0) oct = 0;
				if (oct > 9) oct = 9;
				na = (uint8_t)(((oct & 0x0F) << 4) | (sc & 0x0F));
			}
			if (PushEv(ev, evCount, evMax, e.tick, e.ch, e.kind, na, e.b, e.c, e.dur))
				added++;
		}
	}
	return added;
}

static int ParseRoot(const wchar_t* s, int* outRoot /*0=C..11*/, int* outAdvance)
{
	if (!s || !s[0]) return 0;
	int i = 0;
	wchar_t c = s[i++];
	int root = -1;
	if (c >= L'A' && c <= L'G') {
		static const int map[] = { 9, 11, 0, 2, 4, 5, 7 }; /* A..G */
		root = map[c - L'A'];
	} else if (c >= L'a' && c <= L'g') {
		static const int map[] = { 9, 11, 0, 2, 4, 5, 7 };
		root = map[c - L'a'];
	} else return 0;
	if (s[i] == L'#' || s[i] == L'♯') { root = (root + 1) % 12; i++; }
	else if (s[i] == L'b' || s[i] == L'♭') { root = (root + 11) % 12; i++; }
	*outRoot = root;
	*outAdvance = i;
	return 1;
}

int ScChordFromSymbol(ScEvent* ev, int* evCount, int evMax, int part,
	uint32_t tick, int dur, int vel, const wchar_t* symbol, int octave)
{
	if (!ev || !evCount || !symbol) return 0;
	int rootPc = 0, adv = 0;
	if (!ParseRoot(symbol, &rootPc, &adv)) return 0;
	const wchar_t* q = symbol + adv;
	int type = SC_CHORD_MAJOR;
	if (wcsstr(q, L"min7") || wcsstr(q, L"m7") || wcsstr(q, L"-7")) type = SC_CHORD_MIN7;
	else if (wcsstr(q, L"maj7") || wcsstr(q, L"M7") || wcsstr(q, L"Δ")) type = SC_CHORD_MAJ7;
	else if (wcsstr(q, L"sus4") || wcsstr(q, L"sus")) type = SC_CHORD_SUS4;
	else if (wcsstr(q, L"add9")) type = SC_CHORD_ADD9;
	else if (wcsstr(q, L"power") || wcsstr(q, L"5")) type = SC_CHORD_POWER;
	else if (q[0] == L'm' || wcsstr(q, L"min") || wcsstr(q, L"-")) type = SC_CHORD_MINOR;
	int semis[8];
	int nv = ScChordIntervals(type, semis, 8);
	if (octave < 1) octave = 4;
	if (octave > 7) octave = 7;
	int base = 12 * (octave + 1) + rootPc; /* C4 = 60 → octave 4 → 12*5=60 */
	/* MIDI: C4=60 = 12*5 + 0 — use octave as C octave number */
	base = 12 * (octave + 1) + rootPc;
	if (base > 127) base = 60 + rootPc;
	int gate = 100;
	if (vel < 1) vel = 100;
	if (dur < 1) dur = SC_PPQN;
	int n = 0;
	for (int i = 0; i < nv && i < 5; i++) {
		int note = base + semis[i];
		if (note < 0 || note > 127) continue;
		if (PushEv(ev, evCount, evMax, tick, (uint8_t)part, SC_EV_NOTE,
			(uint8_t)note, (uint8_t)vel, (uint8_t)gate, (uint16_t)dur))
			n++;
	}
	return n;
}

int ScPatternPlace(ScEvent* ev, int* evCount, int evMax, int part,
	uint32_t tick, int note, int vel, int gate, int patternId, int bars,
	int meterNumer, int meterDenom, int ppqn)
{
	if (!ev || !evCount || ppqn <= 0) return 0;
	if (meterNumer < 1) meterNumer = 4;
	if (meterDenom < 1) meterDenom = 4;
	if (bars < 1) bars = 1;
	if (note < 0) note = 60;
	if (note > 127) note = 127;
	if (vel < 1) vel = 100;
	if (gate < 1) gate = 100;
	const int beatTicks = (ppqn * 4) / meterDenom;
	const int barTicks = beatTicks * meterNumer;
	int placed = 0;
	auto put = [&](uint32_t t, int dur, int isRest) {
		if (isRest) {
			if (PushEv(ev, evCount, evMax, t, (uint8_t)part, SC_EV_REST, 0, 0, 0, (uint16_t)dur))
				placed++;
		} else {
			if (PushEv(ev, evCount, evMax, t, (uint8_t)part, SC_EV_NOTE,
				(uint8_t)note, (uint8_t)vel, (uint8_t)gate, (uint16_t)dur))
				placed++;
		}
	};

	int useBars = bars;
	if (patternId == SC_PAT_2BAR_E) useBars = 2;
	if (patternId == SC_PAT_4BAR_Q) useBars = 4;
	if (patternId == SC_PAT_8BAR_Q) useBars = 8;

	for (int b = 0; b < useBars; b++) {
		const uint32_t bar0 = tick + (uint32_t)(b * barTicks);
		for (int beat = 0; beat < meterNumer; beat++) {
			const uint32_t bt = bar0 + (uint32_t)(beat * beatTicks);
			switch (patternId) {
			case SC_PAT_Q_1BAR:
			case SC_PAT_4BAR_Q:
			case SC_PAT_8BAR_Q:
				put(bt, beatTicks, 0);
				break;
			case SC_PAT_E_1BAR:
			case SC_PAT_2BAR_E:
				put(bt, beatTicks / 2, 0);
				put(bt + beatTicks / 2, beatTicks / 2, 0);
				break;
			case SC_PAT_S_1BAR:
				for (int s = 0; s < 4; s++)
					put(bt + (beatTicks * s) / 4, beatTicks / 4, 0);
				break;
			case SC_PAT_E_SS:
				put(bt, beatTicks / 2, 0);
				put(bt + beatTicks / 2, beatTicks / 4, 0);
				put(bt + (beatTicks * 3) / 4, beatTicks / 4, 0);
				break;
			case SC_PAT_SS_E:
				put(bt, beatTicks / 4, 0);
				put(bt + beatTicks / 4, beatTicks / 4, 0);
				put(bt + beatTicks / 2, beatTicks / 2, 0);
				break;
			case SC_PAT_TRIPLET_1BAR:
				for (int t = 0; t < 3; t++)
					put(bt + (beatTicks * t) / 3, beatTicks / 3, 0);
				break;
			case SC_PAT_Q_REST_Q:
				if ((beat & 1) == 0) put(bt, beatTicks, 1);
				else put(bt, beatTicks, 0);
				break;
			case SC_PAT_DOTTED_E:
				put(bt, (beatTicks * 3) / 4, 0);
				put(bt + (beatTicks * 3) / 4, beatTicks / 4, 0);
				break;
			default:
				put(bt, beatTicks, 0);
				break;
			}
		}
	}
	return placed;
}

int ScTransposeSelected(ScEvent* ev, int evCount, const ScStaffUi* u, int semis)
{
	if (!ev || !u || !semis) return 0;
	int n = 0;
	for (int i = 0; i < evCount; i++) {
		if (!IsSel(u, i)) continue;
		if (!NoteKindOk(ev[i].kind)) continue;
		int note = (int)ev[i].a + semis;
		if (note < 0) note = 0;
		if (note > 127) note = 127;
		ev[i].a = (uint8_t)note;
		n++;
	}
	return n;
}

int ScTransposeChannel(ScEvent* ev, int evCount, int ch, int semis)
{
	if (!ev || !semis || ch < 0) return 0;
	int n = 0;
	for (int i = 0; i < evCount; i++) {
		if ((int)ev[i].ch != ch) continue;
		if (!NoteKindOk(ev[i].kind)) continue;
		int note = (int)ev[i].a + semis;
		if (note < 0) note = 0;
		if (note > 127) note = 127;
		ev[i].a = (uint8_t)note;
		n++;
	}
	return n;
}

int ScTransposeAll(ScEvent* ev, int evCount, int semis)
{
	if (!ev || !semis) return 0;
	int n = 0;
	for (int i = 0; i < evCount; i++) {
		if (!NoteKindOk(ev[i].kind)) continue;
		int note = (int)ev[i].a + semis;
		if (note < 0) note = 0;
		if (note > 127) note = 127;
		ev[i].a = (uint8_t)note;
		n++;
	}
	return n;
}

static int ScFmNoteByteToMidi(int track, uint8_t nb)
{
	if (track == 6) return nb & 0x0F;
	const int octNib = (nb >> 4) & 0x0F;
	const int scale = nb & 0x0F;
	if (ScStaffIsFmSsgTrack(1, track))
		return octNib * 12 + scale;
	return (octNib + 1) * 12 + scale;
}

static uint8_t ScFmMidiToNoteByte(int track, int midi)
{
	if (midi < 0) midi = 0;
	if (midi > 127) midi = 127;
	if (track == 6) {
		int pad = midi;
		if (pad < 0) pad = 0;
		if (pad > 5) pad = 5;
		return (uint8_t)pad;
	}
	if (ScStaffIsFmSsgTrack(1, track)) {
		const int oct = midi / 12;
		const int sc = midi % 12;
		return (uint8_t)(((oct & 0x0F) << 4) | (sc & 0x0F));
	}
	int oct = midi / 12 - 1;
	int scale = midi % 12;
	if (oct < 0) oct = 0;
	if (oct > 9) oct = 9;
	return (uint8_t)(((oct & 0x0F) << 4) | (scale & 0x0F));
}

static int ScFmTransposeOne(int track, uint8_t* nb, int semis)
{
	if (!nb || !semis) return 0;
	const int midi = ScFmNoteByteToMidi(track, *nb) + semis;
	*nb = ScFmMidiToNoteByte(track, midi);
	return 1;
}

int ScTransposeFmSelected(ScEvent* ev, int evCount, const ScStaffUi* u, int semis)
{
	if (!ev || !u || !semis) return 0;
	int n = 0;
	for (int i = 0; i < evCount; i++) {
		if (!IsSel(u, i)) continue;
		if (ev[i].kind != SC_EV_FM_NOTE) continue;
		if (ScFmTransposeOne((int)ev[i].ch, &ev[i].a, semis)) n++;
	}
	return n;
}

int ScTransposeFmChannel(ScEvent* ev, int evCount, int ch, int semis)
{
	if (!ev || !semis || ch < 0) return 0;
	int n = 0;
	for (int i = 0; i < evCount; i++) {
		if ((int)ev[i].ch != ch) continue;
		if (ev[i].kind != SC_EV_FM_NOTE) continue;
		if (ScFmTransposeOne(ch, &ev[i].a, semis)) n++;
	}
	return n;
}

int ScTransposeFmAll(ScEvent* ev, int evCount, int semis)
{
	if (!ev || !semis) return 0;
	int n = 0;
	for (int i = 0; i < evCount; i++) {
		if (ev[i].kind != SC_EV_FM_NOTE) continue;
		if (ScFmTransposeOne((int)ev[i].ch, &ev[i].a, semis)) n++;
	}
	return n;
}

int ScScaleDurSelected(ScEvent* ev, int evCount, const ScStaffUi* u, int mul, int div)
{
	if (!ev || !u || mul < 1 || div < 1) return 0;
	int n = 0;
	for (int i = 0; i < evCount; i++) {
		if (!IsSel(u, i)) continue;
		if (!NoteKindOk(ev[i].kind) && ev[i].kind != SC_EV_REST && ev[i].kind != SC_EV_FM_REST)
			continue;
		int d = ((int)ev[i].dur * mul) / div;
		if (d < 1) d = 1;
		if (d > 65535) d = 65535;
		ev[i].dur = (uint16_t)d;
		n++;
	}
	return n;
}

int ScBulkPropsSelected(ScEvent* ev, int evCount, const ScStaffUi* u, int vel, int gate, int applyVel, int applyGate)
{
	if (!ev || !u) return 0;
	int n = 0;
	for (int i = 0; i < evCount; i++) {
		if (!IsSel(u, i)) continue;
		if (!NoteKindOk(ev[i].kind)) continue;
		if (applyVel) {
			int v = vel; if (v < 1) v = 1; if (v > 127) v = 127;
			ev[i].b = (uint8_t)v;
		}
		if (applyGate) {
			int g = gate; if (g < 1) g = 1; if (g > 100) g = 100;
			ev[i].c = (uint8_t)g;
		}
		n++;
	}
	return n;
}
