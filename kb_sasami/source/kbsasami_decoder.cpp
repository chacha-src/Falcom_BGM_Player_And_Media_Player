#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <string>

#include "kbsasami_decoder.h"
#include "fmmon_shadow.h"

extern HINSTANCE g_hKpi;

static const wchar_t SEC_KBSASAMI[] = L"kbsasami";
static const wchar_t KEY_VST[] = L"vst";
static const wchar_t KEY_RAIRA[] = L"raira";
static const wchar_t KEY_MIDIMODE[] = L"midimode";
static const wchar_t KEY_MAP_LEGACY[] = L"map";
static const wchar_t KEY_FMMODE[] = L"fmmode";

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
	m_raira = 0;
	m_vst = 0;
	m_mapDefault = 4;
	m_fmModeDefault = 2;
	m_titleSjis[0] = 0;
	m_loopStart = -1.0;
	m_loopEnd = -1.0;
	m_smfSize = 0;
	m_synths[0] = &m_synthesizer;
	for (int i = 1; i < MAX_PORTS; i++) m_synths[i] = NULL;
}

KbSasamiDecoder::~KbSasamiDecoder()
{
	for (int i = 1; i < MAX_PORTS; i++) {
		delete m_synths[i];
		m_synths[i] = NULL;
	}
	m_fm.Close();
	if (m_pConfig) {
		m_pConfig->Release();
		m_pConfig = NULL;
	}
}

void KbSasamiDecoder::ReadOptions()
{
	m_raira = 0;
	m_vst = 0;
	m_mapDefault = 4;
	m_fmModeDefault = 2;
	if (m_pConfig) {
		m_raira = (int)m_pConfig->GetInt(SEC_KBSASAMI, KEY_RAIRA, 0);
		m_vst = (int)m_pConfig->GetInt(SEC_KBSASAMI, KEY_VST, 0);
		m_mapDefault = (int)m_pConfig->GetInt(SEC_KBSASAMI, KEY_MIDIMODE, -1);
		if (m_mapDefault < 0)
			m_mapDefault = (int)m_pConfig->GetInt(SEC_KBSASAMI, KEY_MAP_LEGACY, 4);
		m_fmModeDefault = (int)m_pConfig->GetInt(SEC_KBSASAMI, KEY_FMMODE, 2);
	}
	if (m_mapDefault < 0 || m_mapDefault > 19) m_mapDefault = 4;
	if (m_fmModeDefault < 0 || m_fmModeDefault > 2) m_fmModeDefault = 2;
	if (m_raira)
		m_vst = m_vst ? 0 : 1;
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
	if (slash) slash[1] = 0;
	wcsncat_s(sz, L"gs.wopn", _TRUNCATE);
	m_note_factory.load_wopn(sz, 1, 0);
	if (slash) slash[1] = 0;
	wcsncat_s(sz, L"xg.wopn", _TRUNCATE);
	m_note_factory.load_wopn(sz, 0, 1);
}

void KbSasamiDecoder::midi_message(int port, uint_least32_t message)
{
	if (m_seeking) return;
	if (port < 0 || port >= m_nPorts) port = 0;
	m_synths[port]->midi_event(message);
}

void KbSasamiDecoder::sysex_message(int port, const void* data, std::size_t size)
{
	if (m_seeking) return;
	if (port < 0 || port >= m_nPorts) port = 0;
	m_synths[port]->sysex_message(data, size);
}

void KbSasamiDecoder::meta_event(int, const void*, std::size_t) {}

void KbSasamiDecoder::reset()
{
	for (int i = 0; i < m_nPorts; i++)
		m_synths[i]->reset();
}

DWORD WINAPI KbSasamiDecoder::UpdateConfig(void*)
{
	ReadOptions();
	return 0;
}

DWORD __fastcall KbSasamiDecoder::Open(const KPI_MEDIAINFO* cpRequest, IKpiFile* pFile, IKpiFolder* pFolder)
{
	(void)pFolder;
	if (!pFile) return 0;
	ReadOptions();
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

	const int smfDirect = (n >= 4 && s_fileBuf[0] == 'M' && s_fileBuf[1] == 'T'
		&& s_fileBuf[2] == 'h' && s_fileBuf[3] == 'd') ? 1 : 0;
	const wchar_t* pathForKind = (real && real[0]) ? real : name;
	if (smfDirect) {
		if (m_vst != 0) return 0;
		DWORD rate = 44100;
		if (cpRequest && cpRequest->dwSampleRate >= 8000 && cpRequest->dwSampleRate <= 192000)
			rate = cpRequest->dwSampleRate;
		m_fmMode = false;
		m_kind = SASAMI_KIND_MPY;
		m_titleSjis[0] = 0;
		LoadProgramsTxt();
		MemFile mfSmf;
		mfSmf.p = s_fileBuf;
		mfSmf.size = n;
		mfSmf.pos = 0;
		if (!m_sequencer.load(&mfSmf, MemGetc)) return 0;
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
		m_MediaInfo.nBitsPerSample = 16;
		m_MediaInfo.dwSeekableFlags = KPI_MEDIAINFO::SEEK_FLAGS_SAMPLE;
		m_MediaInfo.dwUnitSample = rate / 100;
		double totalSec = m_sequencer.get_total_time();
		if (m_loopEnd > m_loopStart && m_loopStart >= 0.0)
			totalSec = m_loopEnd;
		if (totalSec < 0.01) totalSec = 0.01;
		m_MediaInfo.qwLength = (UINT64)(totalSec * 1000.0 * 10000.0);
		m_MediaInfo.dwCount = 1;
		m_MediaInfo.dwNumber = 1;
		m_lastSample = kpi_100nsToSample(m_MediaInfo.qwLength, rate);
		m_curSample = 0;
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
		return 1;
	}

	// MIDI: 解釈後 vst=1 なら fmmidi を起動せず失敗 (ホストが VST 経路へ)
	if (m_vst != 0)
		return 0;

	m_fmMode = false;
	m_smfSize = 0;
	const wchar_t* pathForMap = (real && real[0]) ? real : name;
	const int mapForce = SasamiResolveMapForceW(pathForMap, m_mapDefault);
	SasamiMidiMap map = SASAMI_MAP_GS88;
	int gsLsb = 2;
	SasamiMapForceToSel(mapForce, &map, &gsLsb);
	if (!SasamiConvertToSmf(s_song, map, gsLsb, m_smf, SASAMI_MAX_SMF, &m_smfSize)) return 0;
	LoadProgramsTxt();
	MemFile mf;
	mf.p = m_smf;
	mf.size = (DWORD)m_smfSize;
	mf.pos = 0;
	if (!m_sequencer.load(&mf, MemGetc)) return 0;
	m_nPorts = m_sequencer.get_num_ports();
	if (m_nPorts < 1) m_nPorts = 1;
	if (m_nPorts > MAX_PORTS) m_nPorts = MAX_PORTS;
	for (int i = 1; i < m_nPorts; i++)
		m_synths[i] = new synthesizer(&m_note_factory);
	reset();
	m_loopStart = m_sequencer.find_marker("loopStart");
	m_loopEnd = m_sequencer.find_marker("loopEnd");
	m_MediaInfo.dwSampleRate = rate;
	m_MediaInfo.dwChannels = 2;
	m_MediaInfo.nBitsPerSample = 16;
	m_MediaInfo.dwSeekableFlags = KPI_MEDIAINFO::SEEK_FLAGS_SAMPLE;
	m_MediaInfo.dwUnitSample = rate / 100;
	double totalSec = m_sequencer.get_total_time();
	if (m_loopEnd > m_loopStart && m_loopStart >= 0.0)
		totalSec = m_loopEnd;
	m_MediaInfo.qwLength = (UINT64)(totalSec * 1000.0 * 10000.0);
	m_MediaInfo.dwCount = 1;
	m_MediaInfo.dwNumber = 1;
	m_lastSample = kpi_100nsToSample(m_MediaInfo.qwLength, rate);
	m_curSample = 0;
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

DWORD WINAPI KbSasamiDecoder::Render(BYTE* pBuffer, DWORD dwSizeSample)
{
	if (!pBuffer || dwSizeSample == 0) return 0;
	if (m_fmMode)
		return m_fm.Render((int16_t*)pBuffer, dwSizeSample);

	const double rate = (double)m_MediaInfo.dwSampleRate;
	const int looping = (m_loopEnd > m_loopStart && m_loopStart >= 0.0) ? 1 : 0;
	const UINT64 loopStartSamp = looping ? (UINT64)(m_loopStart * rate + 0.5) : 0;
	const UINT64 loopEndSamp = looping ? (UINT64)(m_loopEnd * rate + 0.5) : 0;

	DWORD remain = dwSizeSample;
	BYTE* p = pBuffer;
	while (remain) {
		if (looping && loopEndSamp > loopStartSamp && m_curSample > loopEndSamp) {
			m_sequencer.set_position(m_loopStart);
			m_curSample = loopStartSamp;
		}
		DWORD chunk = remain;
		if (chunk > MIX_FRAMES) chunk = MIX_FRAMES;
		if (looping && loopEndSamp > loopStartSamp && m_curSample <= loopEndSamp
			&& m_curSample + chunk > loopEndSamp + 1)
			chunk = (DWORD)(loopEndSamp + 1 - m_curSample);
		if (chunk == 0) break;
		const double tEnd = (double)(m_curSample + chunk) / rate;
		m_sequencer.play_forward(tEnd, this);
		for (DWORD i = 0; i < chunk * 2; i++) m_mix[i] = 0.0;
		for (int i = 0; i < m_nPorts; i++)
			m_synths[i]->synthesize_mixing(m_mix, chunk, m_MediaInfo.dwSampleRate);
		int16_t* out = (int16_t*)p;
		for (DWORD i = 0; i < chunk * 2; i++) {
			int v = (int)(m_mix[i] * 32767.0);
			if (v > 32767) v = 32767;
			if (v < -32768) v = -32768;
			out[i] = (int16_t)v;
		}
		m_curSample += chunk;
		remain -= chunk;
		p += chunk * 4;
	}

	if (!looping && m_sequencer.is_play_end()) {
		const double limit = 0.001;
		int16_t* out = (int16_t*)pBuffer;
		for (DWORD i = 0; i < dwSizeSample; i++) {
			if (m_endSample++ >= m_MediaInfo.dwSampleRate * 5) return i;
			if (m_silentSample++ >= m_MediaInfo.dwSampleRate / 2) return i;
			const double l = out[i * 2] / 32767.0;
			const double r = out[i * 2 + 1] / 32767.0;
			if (l < -limit || l > limit || r < -limit || r > limit)
				m_silentSample = 0;
		}
	}
	return dwSizeSample;
}

UINT64 WINAPI KbSasamiDecoder::Seek(UINT64 qwPosSample, DWORD)
{
	if (m_fmMode)
		return m_fm.SeekSample(qwPosSample);
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
