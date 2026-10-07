#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>
#include <string.h>
#include <string>

#include "kbsasami_decoder.h"
#include "kbsasami_monitor.h"
#include "ComposerConvert.h"
#include "../../kpi_host_ipc.h"

extern HINSTANCE g_hKpi;

static const wchar_t SEC_KBSASAMI[] = L"kbsasami";
static const wchar_t KEY_VST[] = L"vst";
static const wchar_t KEY_RAIRA[] = L"raira";
static const wchar_t KEY_FMMIDIMONITOR[] = L"fmmidimonitor";
static const wchar_t KEY_MIDIMODE[] = L"midimode";
static const wchar_t KEY_MAP_LEGACY[] = L"map";
static const wchar_t KEY_FMMODE[] = L"fmmode";
static const wchar_t KEY_VST_GS[] = L"vstfullpath_gs";
static const wchar_t KEY_VST_XG[] = L"vstfullpath_xg";

/* fmmidi/ymfm の正規化が小さく、mpy/mpw2/rcp/mid が実聴で約 1/6。
   raira=0（本家）はさらに /2 のうえ 1.5 倍下げる。 */
static const double kFmMidiOutGain = 6.0;

static uint8_t s_fileBuf[SASAMI_MAX_FILE];

int KbSasamiDecoder::MemGetc(void* fp)
{
	MemFile* f = (MemFile*)fp;
	if (f->pos < f->size) return (int)f->p[f->pos++];
	return -1;
}

KbSasamiDecoder::KbSasamiDecoder(IKpiConfig* pConfig)
	: m_synthesizer(&m_note_factory)
	, m_pConfig(pConfig)
{
	kpi_InitMediaInfo(&m_MediaInfo);
	if (m_pConfig) m_pConfig->AddRef();
	m_kind = SASAMI_KIND_UNKNOWN;
	m_nPorts = 0;
	m_curSample = 0;
	m_lastSample = 0;
	m_endSample = 0;
	m_silentSample = 0;
	m_seeking = false;
	m_fmMode = false;
	m_liveStream = false;
	m_raira = 0;
	m_fmOutBits = -64;
	m_vst = 0;
	m_foreignVst = 0;
	m_vstGs[0] = 0;
	m_vstXg[0] = 0;
	memset(&m_vstSess, 0, sizeof(m_vstSess));
	m_mapDefault = 0;
	m_fmModeDefault = 2;
	m_gsMapLsb = 0;
	m_laBankMsb = 0;
	m_wopnMode = 0;
	m_titleSjis[0] = 0;
	m_loopStart = -1.0;
	m_loopEnd = -1.0;
	m_smfSize = 0;
	m_openPath[0] = 0;
	m_monHold = 0;
	m_forceHead = 0;
	m_synths[0] = &m_synthesizer;
	for (int i = 1; i < MAX_PORTS; i++) m_synths[i] = NULL;
}

static KbSasamiDecoder* g_sasamiLive;

void KbSasamiDecoder::LiveBind()
{
	InterlockedExchangePointer((PVOID*)&g_sasamiLive, this);
}

void KbSasamiDecoder::LiveUnbind()
{
	InterlockedCompareExchangePointer((PVOID*)&g_sasamiLive, NULL, this);
}

void KbSasamiDecoder::LiveInjectShort(unsigned int msg)
{
	if (m_fmMode) return;
	std::lock_guard<std::mutex> lk(m_midiLock);
	midi_message(0, (uint_least32_t)msg);
}

void KbSasamiDecoder::LiveInjectSysex(const void* data, size_t size)
{
	if (m_fmMode || !data || size < 2) return;
	std::lock_guard<std::mutex> lk(m_midiLock);
	sysex_message(0, data, size);
}

extern "C" int SasamiKpiLiveInjectShort(unsigned int msg)
{
	KbSasamiDecoder* d = (KbSasamiDecoder*)InterlockedCompareExchangePointer(
		(PVOID*)&g_sasamiLive, NULL, NULL);
	if (!d) return 0;
	d->LiveInjectShort(msg);
	return 1;
}

extern "C" int SasamiKpiLiveInjectSysex(const unsigned char* data, int bytes)
{
	KbSasamiDecoder* d = (KbSasamiDecoder*)InterlockedCompareExchangePointer(
		(PVOID*)&g_sasamiLive, NULL, NULL);
	if (!d || !data || bytes < 2) return 0;
	d->LiveInjectSysex(data, (size_t)bytes);
	return 1;
}

static volatile LONG s_restartHead = 0;

void KbSasamiRequestRestartHead()
{
	InterlockedExchange(&s_restartHead, 1);
}

KbSasamiDecoder::~KbSasamiDecoder()
{
	/* 8ms dump タイマを先に止める。End より後だと Close まで回り続ける。 */
	m_fm.Close();
	KbsMonEnd(this);
	LiveUnbind();
	for (int i = 1; i < MAX_PORTS; i++) {
		delete m_synths[i];
		m_synths[i] = NULL;
	}
	KbVstSessionClose(&m_vstSess);
	KbVstDisconnect();
	if (m_pConfig) {
		m_pConfig->Release();
		m_pConfig = NULL;
	}
}

void KbSasamiDecoder::ReadOptions()
{
	m_raira = 0;
	m_vst = 0;
	m_mapDefault = 0;
	m_fmModeDefault = 2;
	if (m_pConfig) {
		m_raira = (int)m_pConfig->GetInt(SEC_KBSASAMI, KEY_RAIRA, 0);
		m_vst = (int)m_pConfig->GetInt(SEC_KBSASAMI, KEY_VST, 0);
		m_mapDefault = (int)m_pConfig->GetInt(SEC_KBSASAMI, KEY_MIDIMODE, -1);
		if (m_mapDefault < 0)
			m_mapDefault = (int)m_pConfig->GetInt(SEC_KBSASAMI, KEY_MAP_LEGACY, 0);
		m_fmModeDefault = (int)m_pConfig->GetInt(SEC_KBSASAMI, KEY_FMMODE, 2);
		m_pConfig->GetStr(SEC_KBSASAMI, KEY_VST_GS, m_vstGs, (DWORD)(sizeof(m_vstGs)), L"");
		m_pConfig->GetStr(SEC_KBSASAMI, KEY_VST_XG, m_vstXg, (DWORD)(sizeof(m_vstXg)), L"");
	}
	if (m_mapDefault < 0 || m_mapDefault > 19) m_mapDefault = 0;
	if (m_fmModeDefault < 0 || m_fmModeDefault > 2) m_fmModeDefault = 2;
	if (m_raira)
		m_vst = m_vst ? 0 : 1;
	m_note_factory.set_raira(m_raira);
	/* raira=1 は ogg のモニタ。fmmidimonitor は見に行かない。
	   raira=0（本家）だけ読む。無い人は 0 にすれば kbsasami_host の窓を出さない。既定は 1。 */
	int mon = 0;
	if (!m_raira) {
		mon = 1;
		if (m_pConfig)
			mon = (int)m_pConfig->GetInt(SEC_KBSASAMI, KEY_FMMIDIMONITOR, 1) ? 1 : 0;
	}
	KbsMonConfigure(mon);
}

static int PathIsCemuLiveMid(const wchar_t* path)
{
	if (!path || !path[0]) return 0;
	const wchar_t* base = path;
	for (const wchar_t* p = path; *p; ++p)
		if (*p == L'\\' || *p == L'/') base = p + 1;
	if (_wcsnicmp(base, L"cemu_mpu_", 9) == 0) return 1;
	if (wcsstr(base, L"cemu-live")) return 1;
	return 0;
}

static int ReadLineInts(const char** pp, int* dst, int maxn)
{
	const char* p = *pp;
	int n = 0;
	while (n < maxn) {
		while (*p == ' ' || *p == '\t' || *p == '\r') p++;
		if (*p == 0 || *p == '\n' || *p == ';' || *p == '@' || *p == '*') break;
		char* end = NULL;
		long v = strtol(p, &end, 10);
		if (end == p) break;
		dst[n++] = (int)v;
		p = end;
	}
	while (*p && *p != '\n') p++;
	if (*p == '\n') p++;
	*pp = p;
	return n;
}

static int OpInRange(const int* op)
{
	return op[0] >= 0 && op[0] <= 31
		&& op[1] >= 0 && op[1] <= 31
		&& op[2] >= 0 && op[2] <= 31
		&& op[3] >= 0 && op[3] <= 15
		&& op[4] >= 0 && op[4] <= 15
		&& op[5] >= 0 && op[5] <= 127
		&& op[6] >= 0 && op[6] <= 3
		&& op[7] >= 0 && op[7] <= 15
		&& op[8] >= 0 && op[8] <= 7
		&& op[9] >= 0 && op[9] <= 3;
}

static void FillOps(FMPARAMETER* p, const int ops[4][10])
{
	p->op1.AR = ops[0][0]; p->op1.DR = ops[0][1]; p->op1.SR = ops[0][2]; p->op1.RR = ops[0][3];
	p->op1.SL = ops[0][4]; p->op1.TL = ops[0][5]; p->op1.KS = ops[0][6]; p->op1.ML = ops[0][7];
	p->op1.DT = ops[0][8]; p->op1.AMS = ops[0][9];
	p->op2.AR = ops[1][0]; p->op2.DR = ops[1][1]; p->op2.SR = ops[1][2]; p->op2.RR = ops[1][3];
	p->op2.SL = ops[1][4]; p->op2.TL = ops[1][5]; p->op2.KS = ops[1][6]; p->op2.ML = ops[1][7];
	p->op2.DT = ops[1][8]; p->op2.AMS = ops[1][9];
	p->op3.AR = ops[2][0]; p->op3.DR = ops[2][1]; p->op3.SR = ops[2][2]; p->op3.RR = ops[2][3];
	p->op3.SL = ops[2][4]; p->op3.TL = ops[2][5]; p->op3.KS = ops[2][6]; p->op3.ML = ops[2][7];
	p->op3.DT = ops[2][8]; p->op3.AMS = ops[2][9];
	p->op4.AR = ops[3][0]; p->op4.DR = ops[3][1]; p->op4.SR = ops[3][2]; p->op4.RR = ops[3][3];
	p->op4.SL = ops[3][4]; p->op4.TL = ops[3][5]; p->op4.KS = ops[3][6]; p->op4.ML = ops[3][7];
	p->op4.DT = ops[3][8]; p->op4.AMS = ops[3][9];
}

static void LoadProgramsFile(fm_note_factory& factory, const wchar_t* path)
{
	FILE* fp = NULL;
	_wfopen_s(&fp, path, L"rb");
	if (!fp) return;
	if (fseek(fp, 0, SEEK_END) != 0) { fclose(fp); return; }
	long sz = ftell(fp);
	if (sz <= 0 || sz > 16 * 1024 * 1024) { fclose(fp); return; }
	if (fseek(fp, 0, SEEK_SET) != 0) { fclose(fp); return; }
	std::string raw((size_t)sz, '\0');
	if (fread(&raw[0], 1, (size_t)sz, fp) != (size_t)sz) { fclose(fp); return; }
	fclose(fp);
	const unsigned char* b = (const unsigned char*)raw.data();
	size_t n = raw.size();
	size_t i0 = 0;
	int utf16 = 0;
	if (n >= 2 && b[0] == 0xFF && b[1] == 0xFE) { utf16 = 1; i0 = 2; }
	else if (n >= 3 && b[0] == 0xEF && b[1] == 0xBB && b[2] == 0xBF) i0 = 3;
	else {
		int pairs = 0, zeros = 0;
		for (size_t i = 1; i < 80 && i < n; i += 2) { pairs++; if (b[i] == 0) zeros++; }
		if (pairs > 8 && zeros * 4 > pairs * 3) utf16 = 1;
	}
	std::string text;
	text.reserve(n);
	if (utf16) {
		for (size_t i = i0; i + 1 < n; i += 2) {
			unsigned c = (unsigned)b[i] | ((unsigned)b[i + 1] << 8);
			text.push_back(c < 128 ? (char)c : ' ');
		}
	} else {
		for (size_t i = i0; i < n; i++) {
			unsigned char c = b[i];
			if (c < 128) text.push_back((char)c);
			else {
				text.push_back(' ');
				if ((c & 0xE0) == 0xC0 && i + 1 < n) i += 1;
				else if ((c & 0xF0) == 0xE0 && i + 2 < n) i += 2;
				else if ((c & 0xF8) == 0xF0 && i + 3 < n) i += 3;
			}
		}
	}
	const char* p = text.c_str();
	while (*p) {
		while (*p == ' ' || *p == '\t' || *p == '\r') p++;
		if (*p == '\n') { p++; continue; }
		if (*p != '@' && *p != '*') {
			while (*p && *p != '\n') p++;
			continue;
		}
		const int drum = (*p == '*');
		p++;
		int prog = 0;
		if (ReadLineInts(&p, &prog, 1) != 1) continue;
		int ops[4][10];
		int ok = 1;
		if (drum) {
			int hdr[6];
			if (ReadLineInts(&p, hdr, 6) != 6) continue;
			if (hdr[0] < 0 || hdr[0] > 7 || hdr[1] < 0 || hdr[1] > 7 || hdr[2] < 0 || hdr[2] > 7) continue;
			if (hdr[3] < 0 || hdr[3] > 127 || hdr[4] < 0 || hdr[4] > 16383 || hdr[5] < 0 || hdr[5] > 127) continue;
			for (int k = 0; k < 4; k++) {
				if (ReadLineInts(&p, ops[k], 10) != 10 || !OpInRange(ops[k])) { ok = 0; break; }
			}
			if (!ok) continue;
			DRUMPARAMETER d;
			memset(&d, 0, sizeof(d));
			d.ALG = hdr[0]; d.FB = hdr[1]; d.LFO = hdr[2];
			d.key = hdr[3]; d.panpot = hdr[4]; d.assign = hdr[5];
			FillOps(&d, ops);
			factory.set_drum_program(prog, d);
		} else {
			int hdr[4];
			int hn = ReadLineInts(&p, hdr, 4);
			if (hn < 3) continue;
			if (hdr[0] < 0 || hdr[0] > 7 || hdr[1] < 0 || hdr[1] > 7 || hdr[2] < 0 || hdr[2] > 7) continue;
			int tr = (hn >= 4) ? hdr[3] : 0;
			if (tr < -48) tr = -48;
			if (tr > 48) tr = 48;
			for (int k = 0; k < 4; k++) {
				if (ReadLineInts(&p, ops[k], 10) != 10 || !OpInRange(ops[k])) { ok = 0; break; }
			}
			if (!ok) continue;
			FMPARAMETER fm;
			memset(&fm, 0, sizeof(fm));
			fm.ALG = hdr[0]; fm.FB = hdr[1]; fm.LFO = hdr[2]; fm.transpose = tr;
			FillOps(&fm, ops);
			factory.set_program(prog, fm);
		}
	}
}

void KbSasamiDecoder::LoadProgramsTxt()
{
	wchar_t sz[MAX_PATH];
	GetModuleFileNameW(g_hKpi, sz, MAX_PATH);
	wchar_t* slash = wcsrchr(sz, L'\\');
	if (slash) slash[1] = 0;
	wcsncat_s(sz, L"programs.txt", _TRUNCATE);
	LoadProgramsFile(m_note_factory, sz);
	auto loadBank = [&](const wchar_t* name, int resId, int family, int append) {
		if (slash) slash[1] = 0;
		wcsncat_s(sz, name, _TRUNCATE);
		if (m_note_factory.load_wopn(sz, family, append)) return;
		if (!g_hKpi) return;
		HRSRC hr = FindResourceW(g_hKpi, MAKEINTRESOURCEW(resId), RT_RCDATA);
		if (!hr) return;
		HGLOBAL hg = LoadResource(g_hKpi, hr);
		DWORD n = SizeofResource(g_hKpi, hr);
		const void* p = LockResource(hg);
		if (p && n >= 32)
			m_note_factory.load_wopn_mem(p, n, family, append);
	};
	/* 隣に無い本家でも GS/XG バンクを使う。無いと programs.txt の GM 番号だけになる。 */
	loadBank(L"gs.wopn", 102, 1, 0);
	loadBank(L"xg.wopn", 103, 0, 1);
}

void KbSasamiDecoder::midi_message(int port, uint_least32_t message)
{
	if (!m_seeking)
		KbsMonMidi(port, (unsigned int)message);
	const int st = (int)(message & 0xf0);
	/* Seek の早送りでも CC/PC は載せる。ノートだけ飛ばす（一斉発音しない）。 */
	if (m_seeking && (st == 0x80 || st == 0x90 || st == 0xa0))
		return;
	if (port < 0 || port >= m_nPorts) port = 0;
	if (m_laBankMsb == 127 && m_synths[port]) {
		const int ch = (int)(message & 0x0f);
		if (st == 0xc0 && ch != 9) {
			m_synths[port]->control_change(ch, 0, 127);
			m_synths[port]->control_change(ch, 32, 1);
		}
	}
	m_synths[port]->midi_event(message);
}

void KbSasamiDecoder::sysex_message(int port, const void* data, std::size_t size)
{
	if (port < 0 || port >= m_nPorts) port = 0;
	m_synths[port]->sysex_message(data, size);
	const unsigned char* d = (const unsigned char*)data;
	/* GM/GM2 On はバンクを捨てて PC だけになる。判定が GS/XG なら wopn のモードへ戻す。 */
	int gmReset = (d && size == 6 && d[0] == 0xf0 && d[1] == 0x7e && d[2] == 0x7f &&
		d[3] == 0x09 && (d[4] == 0x01 || d[4] == 0x02 || d[4] == 0x03) && d[5] == 0xf7) ? 1 : 0;
	if (gmReset && (m_wopnMode == (int)system_mode_gs || m_wopnMode == (int)system_mode_xg)) {
		if (m_synths[port])
			m_synths[port]->set_system_mode((system_mode_t)m_wopnMode);
		ApplyGsBankLsb();
		return;
	}
	if (m_gsMapLsb >= 1 && m_gsMapLsb <= 4 && d && size >= 11 &&
		d[0] == 0xf0 && d[1] == 0x41 && d[3] == 0x42 && d[4] == 0x12 &&
		d[5] == 0x40 && d[6] == 0x00 && d[7] == 0x7f) {
		ApplyGsBankLsb();
	}
}

void KbSasamiDecoder::meta_event(int, const void*, std::size_t) {}

void KbSasamiDecoder::ApplyMapForce(int mapForce, SasamiMidiMap* map)
{
	int gsLsb = 0;
	int laBank = 0;
	SasamiMapForceToSel(mapForce, map, &gsLsb, &laBank);
	m_gsMapLsb = (gsLsb >= 1 && gsLsb <= 4) ? gsLsb : 0;
	m_laBankMsb = (laBank == 127) ? 127 : 0;
	/* 1=GS 2=XG 3=55 4=88 5=88Pro 6=8820 9=LA。これ以外は曲の SysEx 任せ。 */
	if (mapForce == 2)
		m_wopnMode = (int)system_mode_xg;
	else if (mapForce == 1 || (mapForce >= 3 && mapForce <= 6) || mapForce == 9)
		m_wopnMode = (int)system_mode_gs;
	else
		m_wopnMode = 0;
}

void KbSasamiDecoder::ApplyWopnMode()
{
	if (m_wopnMode != (int)system_mode_gs && m_wopnMode != (int)system_mode_xg)
		return;
	for (int i = 0; i < m_nPorts; i++) {
		if (m_synths[i])
			m_synths[i]->set_system_mode((system_mode_t)m_wopnMode);
	}
}

void KbSasamiDecoder::ApplyGsBankLsb()
{
	for (int i = 0; i < m_nPorts; i++) {
		if (!m_synths[i]) continue;
		for (int ch = 0; ch < 16; ch++) {
			if (ch == 9) continue;
			if (m_laBankMsb == 127) {
				m_synths[i]->control_change(ch, 0, 127);
				m_synths[i]->control_change(ch, 32, 1);
			} else if (m_gsMapLsb >= 1 && m_gsMapLsb <= 4) {
				/* 初期バンク 0x3C80 のままだと MSB が 121 扱いで gs.wopn を外す。 */
				m_synths[i]->control_change(ch, 0, 0);
				m_synths[i]->control_change(ch, 32, m_gsMapLsb);
			}
		}
	}
}

void KbSasamiDecoder::reset()
{
	for (int i = 0; i < m_nPorts; i++) {
		if (m_synths[i])
			m_synths[i]->reset();
	}
	m_note_factory.reset_pool_frame();
	ApplyWopnMode();
	ApplyGsBankLsb();
	KbsMonNotesOff();
}

DWORD WINAPI KbSasamiDecoder::UpdateConfig(void*)
{
	const int wasVst = m_foreignVst ? 1 : 0;
	ReadOptions();
	if (m_raira) {
		KbsMonEnd(this);
		return 0;
	}
	if (m_kind == SASAMI_KIND_FPY || m_kind == SASAMI_KIND_FPY2)
		return 0;
	const int wantVst = m_vst ? 1 : 0;
	if (wantVst != wasVst)
		SwitchMidiEngine();
	return 0;
}

static DWORD SmfBe32(const uint8_t* p)
{
	return ((DWORD)p[0] << 24) | ((DWORD)p[1] << 16) | ((DWORD)p[2] << 8) | (DWORD)p[3];
}

static void SmfPutBe32(uint8_t* p, DWORD v)
{
	p[0] = (uint8_t)(v >> 24);
	p[1] = (uint8_t)(v >> 16);
	p[2] = (uint8_t)(v >> 8);
	p[3] = (uint8_t)v;
}

/* fmmidi は ApplyGsBankLsb でマップを載せる。VST は SMF に無いと GM ピアノになる。 */
static int SmfPatchMapBanks(uint8_t* smf, int* pSize, int cap, int gsLsb, int laMsb)
{
	if (!smf || !pSize || *pSize < 22 || cap < *pSize) return 0;
	if (laMsb != 127 && (gsLsb < 1 || gsLsb > 4)) return 0;
	if (memcmp(smf, "MThd", 4) != 0) return 0;
	const DWORD hdrLen = SmfBe32(smf + 4);
	uint8_t* tr = smf + 8 + hdrLen;
	if (tr + 8 > smf + *pSize) return 0;
	if (memcmp(tr, "MTrk", 4) != 0) return 0;
	const DWORD trLen = SmfBe32(tr + 4);
	uint8_t* body = tr + 8;
	if (body + trLen > smf + *pSize) return 0;
	uint8_t init[16 * 8];
	int n = 0;
	for (int ch = 0; ch < 16; ch++) {
		if (ch == 9) continue;
		init[n++] = 0;
		init[n++] = (uint8_t)(0xB0 | ch);
		init[n++] = 0;
		init[n++] = (laMsb == 127) ? 127 : 0;
		init[n++] = 0;
		init[n++] = (uint8_t)(0xB0 | ch);
		init[n++] = 32;
		init[n++] = (laMsb == 127) ? 0 : (uint8_t)gsLsb;
	}
	if (*pSize + n > cap) return 0;
	const int tail = (int)((smf + *pSize) - body);
	memmove(body + n, body, (size_t)tail);
	memcpy(body, init, (size_t)n);
	SmfPutBe32(tr + 4, trLen + (DWORD)n);
	*pSize += n;
	return 1;
}

void KbSasamiDecoder::KeepSmf(const uint8_t* smf, DWORD smfLen)
{
	if (!smf || smfLen < 22 || smfLen > (DWORD)SASAMI_MAX_SMF) return;
	if (smf != m_smf)
		memcpy(m_smf, smf, (size_t)smfLen);
	m_smfSize = (int)smfLen;
}

int KbSasamiDecoder::LoadFmMidiSequencer(DWORD rate)
{
	if (m_smfSize < 22) return 0;
	if (rate < 8000 || rate > 192000) rate = 44100;
	m_fmMode = false;
	m_foreignVst = 0;
	LoadProgramsTxt();
	for (int i = 1; i < MAX_PORTS; i++) {
		if (m_synths[i] && m_synths[i] != &m_synthesizer) {
			delete m_synths[i];
			m_synths[i] = NULL;
		}
	}
	MemFile mf;
	mf.p = m_smf;
	mf.size = (DWORD)m_smfSize;
	mf.pos = 0;
	if (!m_sequencer.load(&mf, MemGetc)) return 0;
	m_nPorts = m_sequencer.get_num_ports();
	if (m_nPorts < 1) m_nPorts = 1;
	if (m_nPorts > MAX_PORTS) m_nPorts = MAX_PORTS;
	for (int i = 1; i < m_nPorts; i++) {
		if (!m_synths[i])
			m_synths[i] = new synthesizer(&m_note_factory);
	}
	reset();
	m_loopStart = m_sequencer.find_marker("loopStart");
	m_loopEnd = m_sequencer.find_marker("loopEnd");
	m_MediaInfo.dwSampleRate = rate;
	m_MediaInfo.dwChannels = 2;
	/* 要求が 16/24/32 ならその int。-32 は float32。無指定と -64 は倍精度。 */
	m_MediaInfo.nBitsPerSample = m_fmOutBits ? m_fmOutBits : -64;
	m_MediaInfo.dwSeekableFlags = KPI_MEDIAINFO::SEEK_FLAGS_SAMPLE;
	m_liveStream = PathIsCemuLiveMid(m_openPath) ? true : false;
	if (m_liveStream) {
		m_MediaInfo.dwUnitSample = 0;
		m_MediaInfo.qwLength = (UINT64)-1;
		m_lastSample = 0;
	} else {
		m_MediaInfo.dwUnitSample = rate / 100;
		double totalSec = m_sequencer.get_total_time();
		if (m_loopEnd > m_loopStart && m_loopStart >= 0.0)
			totalSec = m_loopEnd;
		if (totalSec < 0.01) totalSec = 0.01;
		m_MediaInfo.qwLength = (UINT64)(totalSec * 1000.0 * 10000.0);
		m_lastSample = kpi_100nsToSample(m_MediaInfo.qwLength, rate);
	}
	m_MediaInfo.dwCount = 1;
	m_MediaInfo.dwNumber = 1;
	m_curSample = 0;
	LiveBind();
	return 1;
}

int KbSasamiDecoder::SwitchMidiEngine()
{
	if (m_smfSize < 22) return 0;
	const DWORD oldRate = m_MediaInfo.dwSampleRate ? m_MediaInfo.dwSampleRate : 44100;
	m_monHold = 1;
	KbsMonHold(1);
	int ok = 0;
	UINT64 pos = 0;
	{
		std::lock_guard<std::mutex> lk(m_midiLock);
		if (m_foreignVst) {
			KbVstSessionClose(&m_vstSess);
			memset(&m_vstSess, 0, sizeof(m_vstSess));
			m_foreignVst = 0;
		}
		if (m_vst != 0)
			ok = (OpenForeignVst(m_smf, (DWORD)m_smfSize) > 0) ? 1 : 0;
		else
			ok = LoadFmMidiSequencer(oldRate);
		if (!ok) {
			m_monHold = 0;
			KbsMonHold(0);
			return 0;
		}
		if (m_foreignVst) {
			if (KbVstSessionSeek(&m_vstSess, 0))
				m_curSample = 0;
			pos = m_curSample;
		} else
			pos = SeekFmMidiLocked(0);
	}
	if (!m_raira)
		KbsMonSeek((__int64)pos, (int)m_MediaInfo.dwSampleRate);
	m_monHold = 0;
	return 1;
}

static int PathIsStandardSmfW(const wchar_t* path)
{
	if (!path || !path[0]) return 0;
	const wchar_t* dot = wcsrchr(path, L'.');
	if (!dot) return 0;
	return (_wcsicmp(dot, L".mid") == 0 || _wcsicmp(dot, L".midi") == 0
		|| _wcsicmp(dot, L".kar") == 0 || _wcsicmp(dot, L".rmi") == 0) ? 1 : 0;
}

static int WriteMonMid(const void* p, int n, wchar_t* out, int cap)
{
	if (!p || n < 22 || !out || cap < 16) return 0;
	wchar_t tmp[MAX_PATH];
	if (!GetTempPathW(MAX_PATH, tmp)) return 0;
	wchar_t dir[MAX_PATH];
	_snwprintf_s(dir, _TRUNCATE, L"%sogg_kbsasami", tmp);
	CreateDirectoryW(dir, NULL);
	_snwprintf_s(out, cap, _TRUNCATE, L"%s\\kbsmon_play.mid", dir);
	HANDLE h = CreateFileW(out, GENERIC_WRITE, FILE_SHARE_READ, NULL,
		CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE) return 0;
	DWORD wr = 0;
	const BOOL ok = WriteFile(h, p, (DWORD)n, &wr, NULL);
	CloseHandle(h);
	return (ok && wr == (DWORD)n) ? 1 : 0;
}

void KbSasamiDecoder::MonShow(int fm, const wchar_t* path)
{
	if (m_raira) {
		KbsMonEnd(this);
		return;
	}
	/* raira=0 で fmmidimonitor=0。ホストへ MON_SHOW を送らない。 */
	if (!KbsMonIsOn())
		return;
	const wchar_t* use = path;
	wchar_t written[MAX_PATH];
	written[0] = 0;
	if (!fm) {
		/* モニタは kpi が読んだ SMF を出す。ksv は VST 用コピーで、ホストが触ると
		   TB がソースと不一致になる。元 .mid が読めるならそれを、だめなら m_smf。 */
		int have = 0;
		if (PathIsStandardSmfW(path)) {
			const DWORD a = GetFileAttributesW(path);
			if (a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY))
				have = 1;
		}
		if (!have && m_smfSize >= 22 && WriteMonMid(m_smf, m_smfSize, written, MAX_PATH)) {
			use = written;
			have = 1;
		}
	}
	KbsMonBegin(this, fm, use, m_titleSjis);
}

DWORD __fastcall KbSasamiDecoder::Open(const KPI_MEDIAINFO* cpRequest, IKpiFile* pFile, IKpiFolder* pFolder)
{
	(void)pFolder;
	if (!pFile) return 0;
	ReadOptions();
	/* 本家は 16bit 固定のことが多く、0 や未対応の -64 だとバッファ長が狂って
	   Seek／リング巻き戻しに見える。明示された精度だけ従う。 */
	m_fmOutBits = m_raira ? -64 : 16;
	if (cpRequest) {
		const int b = cpRequest->nBitsPerSample;
		if (b == 16 || b == 24 || b == 32 || b == -32 || b == -64)
			m_fmOutBits = b;
	}
	if (InterlockedExchange(&s_restartHead, 0)) {
		m_forceHead = 1;
		KbsMonForgetPos();
	}
	m_monHold = 1;
	KbsMonHold(1);
	pFile->AddRef();
	UINT64 sz = pFile->GetSize();
	if (sz == 0 || sz == (UINT64)-1 || sz > SASAMI_MAX_FILE) {
		pFile->Release();
		return 0;
	}
	pFile->Seek(0, FILE_BEGIN);
	DWORD n = pFile->Read(s_fileBuf, (DWORD)sz);
	wchar_t name[MAX_PATH];
	name[0] = 0;
	pFile->GetFileName(name, MAX_PATH);
	const wchar_t* real = NULL;
	pFile->GetRealFileW(&real);
	pFile->Release();
	if (n == 0) return 0;
	m_gsMapLsb = 0;
	m_laBankMsb = 0;
	m_wopnMode = 0;
	m_smfSize = 0;
	m_openPath[0] = 0;

	const int smfMagic = (n >= 4 && s_fileBuf[0] == 'M' && s_fileBuf[1] == 'T'
		&& s_fileBuf[2] == 'h' && s_fileBuf[3] == 'd') ? 1 : 0;
	const wchar_t* pathForKind = (real && real[0]) ? real : name;
	if (pathForKind && pathForKind[0])
		wcsncpy_s(m_openPath, pathForKind, _TRUNCATE);
	/* ホストが既に SMF 化したファイルは MThd でここに来る。
	   本家は変換しない。らいらが書いた raira=1 が ini に残っていても、
	   生 RCP/EUP 等ならここで翻訳する（raira は vst 入れ替え専用）。 */
	std::vector<unsigned char> composerMid;
	const uint8_t* smfPtr = s_fileBuf;
	DWORD smfLen = n;
	int smfDirect = smfMagic;
	if (!smfDirect && ComposerConvertMemToMidi(s_fileBuf, n, pathForKind, composerMid)
		&& composerMid.size() >= 22) {
		smfPtr = composerMid.data();
		smfLen = (DWORD)composerMid.size();
		smfDirect = 1;
	}
	if (smfDirect) {
		KeepSmf(smfPtr, smfLen);
		{
			int mapForce = SasamiResolveMapForceW(pathForKind, m_mapDefault);
			mapForce = SasamiAutoMapForce(mapForce, NULL, m_smf, m_smfSize, pathForKind, NULL);
			SasamiMidiMap map = SASAMI_MAP_GS88;
			ApplyMapForce(mapForce, &map);
			(void)map;
		}
		SmfPatchMapBanks(m_smf, &m_smfSize, SASAMI_MAX_SMF, m_gsMapLsb, m_laBankMsb);
		if (m_vst != 0) {
			int vr = OpenForeignVst(m_smf, (DWORD)m_smfSize);
			if (vr <= 0) return 0;
			MonShow(0, pathForKind);
			return 1;
		}
		DWORD rate = 44100;
		if (cpRequest && cpRequest->dwSampleRate >= 8000 && cpRequest->dwSampleRate <= 192000)
			rate = cpRequest->dwSampleRate;
		m_kind = SASAMI_KIND_MPY;
		m_titleSjis[0] = 0;
		if (!LoadFmMidiSequencer(rate)) return 0;
		MonShow(0, pathForKind);
		return 1;
	}

	SasamiKind hint = SasamiKindFromPath(pathForKind);
	if (hint == SASAMI_KIND_UNKNOWN) {
		if (n >= 3 && s_fileBuf[0] == 0xEE && s_fileBuf[1] == 0xEE && s_fileBuf[2] == 0xEE)
			hint = SASAMI_KIND_MPW2;
		else if (n >= 4 && s_fileBuf[1] == 0 && (s_fileBuf[0] + s_fileBuf[1] * 256) >= 0x1000)
			hint = SASAMI_KIND_FPY;
		else
			hint = SASAMI_KIND_MPY;
	}

	static SasamiSong s_song;
	if (!SasamiLoadMemory(s_fileBuf, n, hint, &s_song)) return 0;
	m_kind = s_song.kind;
	strncpy_s(m_titleSjis, s_song.titleSjis, _TRUNCATE);

	DWORD rate = 44100;
	if (cpRequest && cpRequest->dwSampleRate >= 8000 && cpRequest->dwSampleRate <= 192000)
		rate = cpRequest->dwSampleRate;

	if (s_song.kind == SASAMI_KIND_FPY || s_song.kind == SASAMI_KIND_FPY2) {
		m_fmMode = true;
		wchar_t plugDir[MAX_PATH];
		GetModuleFileNameW(g_hKpi, plugDir, MAX_PATH);
		wchar_t* sl = wcsrchr(plugDir, L'\\');
		if (sl) *sl = 0;
		else plugDir[0] = 0;
		const int fmMode = SasamiResolveFmModeW(pathForKind, m_fmModeDefault);
		wchar_t songDir[MAX_PATH];
		songDir[0] = 0;
		if (pathForKind && pathForKind[0]) {
			wcsncpy_s(songDir, pathForKind, _TRUNCATE);
			wchar_t* slSong = wcsrchr(songDir, L'\\');
			if (!slSong) slSong = wcsrchr(songDir, L'/');
			if (slSong) *slSong = 0;
			else songDir[0] = 0;
		}
		if (!m_fm.Open(s_song, rate, plugDir, fmMode, songDir[0] ? songDir : plugDir)) return 0;
		/* FMモニタ dump は OPN/OPNA なら常時 ON。
		   以前は m_raira 依存だったが、IKpiConfig が NullConfig になると
		   raira が 0 のまま音声だけ再生され、live が更新されずモニタが固まる。 */
		if (fmMode == 1 || fmMode == 2)
			m_fm.SetFmMonDump(1, pathForKind);
		else
			m_fm.SetFmMonDump(0, pathForKind);
		m_MediaInfo.dwSampleRate = m_fm.SampleRate();
		m_MediaInfo.dwChannels = 2;
		m_MediaInfo.nBitsPerSample = 16;
		m_MediaInfo.dwSeekableFlags = KPI_MEDIAINFO::SEEK_FLAGS_SAMPLE;
		/* 0 = ホストが任意サイズで Render してよい。rate/100 だと
		   KPI 規約上は 10ms 固定になり、本体の 4ms スライスと食い違う。 */
		m_MediaInfo.dwUnitSample = 0;
		{
			const UINT64 samples = m_fm.TotalSamples();
			const DWORD sr = m_MediaInfo.dwSampleRate;
			UINT64 ns = 0;
			if (sr > 0)
				ns = (samples * 10000000ull + (UINT64)sr - 1ull) / (UINT64)sr;
			m_MediaInfo.qwLength = ns;
		}
		m_MediaInfo.dwCount = 1;
		m_MediaInfo.dwNumber = 1;
		m_lastSample = m_fm.TotalSamples();
		m_curSample = 0;
		MonShow(1, pathForKind);
		return 1;
	}

	/* 先に SMF 化する。VST に空の m_smf を渡すとホスト側の解析がソースとずれる。 */
	m_fmMode = false;
	m_smfSize = 0;
	const wchar_t* pathForMap = (real && real[0]) ? real : name;
	int mapForce = SasamiResolveMapForceW(pathForMap, m_mapDefault);
	mapForce = SasamiAutoMapForce(mapForce, &s_song, NULL, 0, pathForMap, s_song.titleSjis);
	SasamiMidiMap map = SASAMI_MAP_GS88;
	ApplyMapForce(mapForce, &map);
	if (!SasamiConvertToSmf(s_song, map, m_gsMapLsb, m_smf, SASAMI_MAX_SMF, &m_smfSize, m_laBankMsb)) return 0;

	if (m_vst != 0) {
		int vr = OpenForeignVst(m_smf, (DWORD)m_smfSize);
		if (vr <= 0) return 0;
		MonShow(0, pathForKind);
		return 1;
	}

	if (!LoadFmMidiSequencer(rate)) return 0;
	MonShow(0, pathForKind);
	return 1;
}

DWORD WINAPI KbSasamiDecoder::Select(DWORD dwNumber, const KPI_MEDIAINFO** ppMediaInfo, IKpiTagInfo* pTagInfo, DWORD dwTagGetFlags)
{
	if (ppMediaInfo) *ppMediaInfo = NULL;
	if (dwNumber > m_MediaInfo.dwCount) return 0;
	if (!ppMediaInfo) return 1;
	*ppMediaInfo = &m_MediaInfo;
	(void)pTagInfo;
	(void)dwTagGetFlags;
	return 1;
}

int KbSasamiDecoder::OpenForeignVst(const uint8_t* smf, DWORD smfLen)
{
	/* raira=1 は本体が VST を持つ。このルートは他アプリだけ。 */
	if (m_raira) return 0;
	if (!smf || smfLen < 22) return -1;
	if (smf != m_smf && smfLen <= (DWORD)SASAMI_MAX_SMF) {
		memcpy(m_smf, smf, (size_t)smfLen);
		m_smfSize = (int)smfLen;
	} else if (smf == m_smf)
		m_smfSize = (int)smfLen;
	if (!KbVstSessionOpen(m_smf, (DWORD)m_smfSize, m_vstGs, m_vstXg, &m_vstSess))
		return -1;
	m_foreignVst = 1;
	m_fmMode = false;
	const DWORD rate = m_vstSess.rate ? m_vstSess.rate : 44100;
	m_MediaInfo.dwSampleRate = rate;
	m_MediaInfo.dwChannels = 2;
	m_MediaInfo.nBitsPerSample = 16;
	m_MediaInfo.dwSeekableFlags = KPI_MEDIAINFO::SEEK_FLAGS_SAMPLE;
	m_MediaInfo.dwUnitSample = 0;
	UINT64 ns = 0;
	if (rate && m_vstSess.lengthSamples)
		ns = (m_vstSess.lengthSamples * 10000000ull + (UINT64)rate - 1ull) / (UINT64)rate;
	m_MediaInfo.qwLength = ns;
	m_MediaInfo.dwCount = 1;
	m_MediaInfo.dwNumber = 1;
	m_lastSample = m_vstSess.lengthSamples;
	m_curSample = 0;
	return 1;
}

static DWORD FmPcmBytesPerFrame(int bits)
{
	int a = bits < 0 ? -bits : bits;
	if (a < 8) a = 64;
	return 2u * (DWORD)(a / 8);
}

static double FmClipUnit(double x, double gain)
{
	double v = x * gain;
	if (v > 1.0) v = 1.0;
	if (v < -1.0) v = -1.0;
	return v;
}

static void FmWritePcm(BYTE* dst, const double* mix, DWORD frames, double gain, int bits)
{
	const DWORD n = frames * 2;
	if (bits == -64) {
		double* out = (double*)dst;
		for (DWORD i = 0; i < n; i++)
			out[i] = FmClipUnit(mix[i], gain);
		return;
	}
	if (bits == -32) {
		float* out = (float*)dst;
		for (DWORD i = 0; i < n; i++)
			out[i] = (float)FmClipUnit(mix[i], gain);
		return;
	}
	if (bits == 16) {
		int16_t* out = (int16_t*)dst;
		for (DWORD i = 0; i < n; i++) {
			long long s = llrint(FmClipUnit(mix[i], gain) * 32767.0);
			if (s > 32767) s = 32767;
			if (s < -32768) s = -32768;
			out[i] = (int16_t)s;
		}
		return;
	}
	if (bits == 32) {
		int32_t* out = (int32_t*)dst;
		for (DWORD i = 0; i < n; i++) {
			long long s = llrint(FmClipUnit(mix[i], gain) * 2147483647.0);
			if (s > 2147483647LL) s = 2147483647LL;
			if (s < -2147483647LL - 1) s = -2147483647LL - 1;
			out[i] = (int32_t)s;
		}
		return;
	}
	BYTE* p = dst;
	for (DWORD i = 0; i < n; i++) {
		long long s = llrint(FmClipUnit(mix[i], gain) * 8388607.0);
		if (s > 8388607) s = 8388607;
		if (s < -8388608) s = -8388608;
		p[0] = (BYTE)(s & 0xFF);
		p[1] = (BYTE)((s >> 8) & 0xFF);
		p[2] = (BYTE)((s >> 16) & 0xFF);
		p += 3;
	}
}

static double FmSampleUnit(const BYTE* frame, int bits)
{
	if (bits == -64) return *(const double*)frame;
	if (bits == -32) return (double)*(const float*)frame;
	if (bits == 16) return (double)*(const int16_t*)frame / 32767.0;
	if (bits == 32) return (double)*(const int32_t*)frame / 2147483647.0;
	int v = frame[0] | (frame[1] << 8) | (frame[2] << 16);
	if (v & 0x800000) v |= ~0xFFFFFF;
	return (double)v / 8388607.0;
}

DWORD WINAPI KbSasamiDecoder::Render(BYTE* pBuffer, DWORD dwSizeSample)
{
	if (!pBuffer || dwSizeSample == 0) return 0;
	if (m_foreignVst) {
		const uint32_t bytes = dwSizeSample * 4u;
		uint32_t got = 0, eof = 0;
		if (!KbVstSessionRender(&m_vstSess, pBuffer, bytes, &got, &eof))
			return 0;
		DWORD frames = got / 4u;
		if (frames > dwSizeSample) frames = dwSizeSample;
		if (frames < dwSizeSample && (eof & (KPIHOST32_EOF_MIDI_PENDING | KPIHOST32_EOF_MIDI_KEEPALIVE))) {
			ZeroMemory(pBuffer + frames * 4, (dwSizeSample - frames) * 4u);
			m_curSample += dwSizeSample;
			return dwSizeSample;
		}
		m_curSample += frames;
		if (!m_raira)
			KbsMonPlay(( __int64)m_curSample, (int)m_MediaInfo.dwSampleRate);
		return frames;
	}
	if (m_fmMode) {
		/* 可聴位置は FM 側の 8ms タイマが出す。ここはデコード先頭なので送らない。 */
		return m_fm.Render((int16_t*)pBuffer, dwSizeSample);
	}

	std::lock_guard<std::mutex> lk(m_midiLock);
	const double rate = (double)m_MediaInfo.dwSampleRate;
	const int looping = (m_loopEnd > m_loopStart && m_loopStart >= 0.0) ? 1 : 0;
	const UINT64 loopStartSamp = looping ? (UINT64)(m_loopStart * rate + 0.5) : 0;
	const UINT64 loopEndSamp = looping ? (UINT64)(m_loopEnd * rate + 0.5) : 0;

	DWORD remain = dwSizeSample;
	BYTE* p = pBuffer;
	int wrapGuard = 0;
	const int outBits = m_MediaInfo.nBitsPerSample ? m_MediaInfo.nBitsPerSample : -64;
	const DWORD bytesPerFrame = FmPcmBytesPerFrame(outBits);
	const double gain = m_raira ? kFmMidiOutGain : (kFmMidiOutGain * 0.5 / 1.5);
	while (remain) {
		if (looping && loopEndSamp > loopStartSamp + 1 && m_curSample > loopEndSamp) {
			if (++wrapGuard > 64) {
				ZeroMemory(p, remain * bytesPerFrame);
				break;
			}
			/* ハング中の NoteOff は SMF の loopEnd で済んでいる。
			   all_sound_off は 2 周目の A01 が欠ける。 */
			for (int i = 0; i < m_nPorts; i++) {
				if (!m_synths[i]) continue;
				for (int ch = 0; ch < 16; ch++)
					m_synths[i]->control_change(ch, 0x40, 0);
				m_synths[i]->all_note_off();
			}
			KbsMonNotesOff();
			m_note_factory.reset_pool_frame();
			m_sequencer.set_position(m_loopStart);
			m_curSample = loopStartSamp;
		}
		DWORD chunk = remain;
		if (chunk > MIX_FRAMES) chunk = MIX_FRAMES;
		if (looping && loopEndSamp > loopStartSamp && m_curSample <= loopEndSamp
			&& m_curSample + chunk > loopEndSamp + 1)
			chunk = (DWORD)(loopEndSamp + 1 - m_curSample);
		if (chunk == 0) {
			if (looping && loopEndSamp > loopStartSamp) {
				m_curSample = loopEndSamp + 1;
				continue;
			}
			ZeroMemory(p, remain * bytesPerFrame);
			break;
		}
		const double tChunkEnd = (double)(m_curSample + chunk) / rate;
		for (DWORD i = 0; i < chunk * 2; i++) m_mix[i] = 0.0;
		/* ブロック先頭でオンもオフも済ませると、ブロックより短い音符が無音になる。
		   イベントの時刻まで今の鍵盤で描き、そのあとメッセージを渡す。 */
		double tNow = (double)m_curSample / rate;
		DWORD filled = 0;
		int sliceGuard = 0;
		while (filled < chunk && sliceGuard++ < 200000) {
			const double nt = m_sequencer.peek_time();
			double limit = tChunkEnd;
			int hit = 0;
			if (nt < limit) {
				limit = nt;
				hit = 1;
			}
			DWORD n = 0;
			if (limit > tNow) {
				const double samp = (limit - tNow) * rate;
				if (samp >= 1.0)
					n = (DWORD)samp;
				else if (samp > 0.0 && !hit)
					n = 1;
				if (n > chunk - filled) n = chunk - filled;
			}
			if (n > 0) {
				m_note_factory.begin_pool_frame(n, rate);
				for (int i = 0; i < m_nPorts; i++) {
					if (!m_synths[i]) continue;
					m_synths[i]->synthesize_mixing(m_mix + filled * 2, n, m_MediaInfo.dwSampleRate);
				}
				m_note_factory.end_pool_frame();
				filled += n;
				tNow = (double)(m_curSample + filled) / rate;
			}
			if (!hit) break;
			const double before = m_sequencer.peek_time();
			if (before < tChunkEnd && (n == 0 || tNow + 1.5 / rate >= before))
				m_sequencer.play_forward(before + 1.0e-9, this);
			if (m_sequencer.peek_time() <= before && n == 0)
				break;
		}
		FmWritePcm(p, m_mix, chunk, gain, outBits);
		m_curSample += chunk;
		remain -= chunk;
		p += chunk * bytesPerFrame;
	}

	if (!m_liveStream && !looping && m_sequencer.is_play_end()) {
		const double limit = 0.001;
		const DWORD step = bytesPerFrame / 2;
		for (DWORD i = 0; i < dwSizeSample; i++) {
			if (m_endSample++ >= m_MediaInfo.dwSampleRate * 5) return i;
			if (m_silentSample++ >= m_MediaInfo.dwSampleRate / 2) return i;
			const BYTE* fr = pBuffer + (size_t)i * bytesPerFrame;
			const double l = FmSampleUnit(fr, outBits);
			const double r = FmSampleUnit(fr + step, outBits);
			if (l < -limit || l > limit || r < -limit || r > limit)
				m_silentSample = 0;
		}
	}
	if (!m_raira)
		KbsMonPlay((__int64)m_curSample, (int)m_MediaInfo.dwSampleRate);
	return dwSizeSample;
}

UINT64 KbSasamiDecoder::SeekFmMidiLocked(UINT64 qwPosSample)
{
	m_seeking = true;
	m_sequencer.play(0, this);
	reset();
	UINT64 pos = qwPosSample;
	if (m_loopEnd > m_loopStart && m_loopStart >= 0.0) {
		const double rate = (double)m_MediaInfo.dwSampleRate;
		const UINT64 ls = (UINT64)(m_loopStart * rate + 0.5);
		const UINT64 le = (UINT64)(m_loopEnd * rate + 0.5);
		if (le > ls && pos >= le)
			pos = ls + ((pos - ls) % (le - ls));
	}
	if (pos > 0) {
		const double t = (double)pos / (double)m_MediaInfo.dwSampleRate;
		m_sequencer.play(t, this);
	}
	m_seeking = false;
	m_curSample = pos;
	m_endSample = 0;
	m_silentSample = 0;
	return pos;
}

UINT64 WINAPI KbSasamiDecoder::Seek(UINT64 qwPosSample, DWORD)
{
	if (m_forceHead) {
		qwPosSample = 0;
		m_forceHead = 0;
	}
	if (m_foreignVst) {
		if (!KbVstSessionSeek(&m_vstSess, qwPosSample))
			return m_curSample;
		m_curSample = qwPosSample;
		if (!m_raira)
			KbsMonSeek((__int64)m_curSample, (int)m_MediaInfo.dwSampleRate);
		m_monHold = 0;
		return qwPosSample;
	}
	if (m_fmMode) {
		const UINT64 pos = m_fm.SeekSample(qwPosSample);
		if (!m_raira)
			KbsMonSeek((__int64)pos, (int)m_MediaInfo.dwSampleRate);
		m_monHold = 0;
		return pos;
	}
	UINT64 pos = 0;
	{
		std::lock_guard<std::mutex> lk(m_midiLock);
		pos = SeekFmMidiLocked(qwPosSample);
	}
	if (!m_raira)
		KbsMonSeek((__int64)m_curSample, (int)m_MediaInfo.dwSampleRate);
	m_monHold = 0;
	return pos;
}
