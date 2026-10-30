#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>
#include <string.h>
#include <string>

#include "kbsasami_decoder.h"
#include "kbsasami_monitor.h"
#include "kbs_gimic.h"
#include "ComposerConvert.h"
#include "../../kpi_host_ipc.h"

extern HINSTANCE g_hKpi;

static const wchar_t SEC_KBSASAMI[] = L"kbsasami";
static const wchar_t KEY_VST[] = L"vst";
static const wchar_t KEY_RAIRA[] = L"raira";
static const wchar_t KEY_FMMIDIMONITOR[] = L"fmmidimonitor";
static const wchar_t KEY_GIMIC[] = L"gimic";
static const wchar_t KEY_MIDIMODE[] = L"midimode";
static const wchar_t KEY_MAP_LEGACY[] = L"map";
static const wchar_t KEY_FMMODE[] = L"fmmode";
static const wchar_t KEY_VST_GS[] = L"vstfullpath_gs";
static const wchar_t KEY_VST_XG[] = L"vstfullpath_xg";
static const wchar_t KEY_VST_GS_SF[] = L"vstsoundfont_gs";
static const wchar_t KEY_VST_XG_SF[] = L"vstsoundfont_xg";

/* fmmidi/ymfm の正規化が小さく、mpy/mpw2/rcp/mid が実聴で約 1/6。
   raira=0（本家）はさらに /2 のうえ 1.5 倍下げる。 */
static const double kFmMidiOutGain = 12.0;

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
	m_vstGsSf[0] = 0;
	m_vstXgSf[0] = 0;
	memset(&m_vstSess, 0, sizeof(m_vstSess));
	m_mapDefault = 0;
	m_fmModeDefault = 2;
	m_gsMapLsb = 0;
	m_laBankMsb = 0;
	m_wopnMode = 0;
	m_titleSjis[0] = 0;
	m_loopStart = -1.0;
	m_loopEnd = -1.0;
	m_loopClosed = 0;
	m_seqAutoLoop = 0;
	m_smfSize = 0;
	m_openPath[0] = 0;
	m_monHold = 0;
	m_forceHead = 0;
	memset(m_drumMap, 0, sizeof(m_drumMap));
	memset(m_kitPc, 0, sizeof(m_kitPc));
	memset(m_kitOn, 0, sizeof(m_kitOn));
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
		m_pConfig->GetStr(SEC_KBSASAMI, KEY_VST_GS_SF, m_vstGsSf, (DWORD)(sizeof(m_vstGsSf)), L"");
		m_pConfig->GetStr(SEC_KBSASAMI, KEY_VST_XG_SF, m_vstXgSf, (DWORD)(sizeof(m_vstXgSf)), L"");
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
	/* raira=1 でも読む。1 なら kbsasami_host32 を起こす。MON_SHOW は送らない。 */
	int gimic = 0;
	if (m_pConfig)
		gimic = (int)m_pConfig->GetInt(SEC_KBSASAMI, KEY_GIMIC, 0) ? 1 : 0;
	KbsGimicSetEnable(gimic);
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

void KbsApplyEmbeddedWopn(fm_note_factory& factory, int loadPool)
{
	struct Bank { int id; int family; int append; int xg; };
	const Bank banks[2] = {
		{ 102, 1, 0, 0 },
		{ 103, 0, 1, 1 },
	};
	if (!g_hKpi) return;
	for (int i = 0; i < 2; i++) {
		HRSRC hr = FindResourceW(g_hKpi, MAKEINTRESOURCEW(banks[i].id), RT_RCDATA);
		if (!hr) continue;
		HGLOBAL hg = LoadResource(g_hKpi, hr);
		DWORD n = SizeofResource(g_hKpi, hr);
		const void* p = LockResource(hg);
		if (!p || n < 32) continue;
		factory.seed_from_wopn(p, n, banks[i].xg);
		if (loadPool)
			factory.load_wopn_mem(p, n, banks[i].family, banks[i].append);
	}
}

void KbSasamiDecoder::LoadProgramsTxt()
{
	/* 隣の gs.wopn / xg.wopn / programs.txt は読まない。音色は kpi のリソース。 */
	KbsApplyEmbeddedWopn(m_note_factory, 1);
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
	/* ドラム番号 1/2 への PC は、その番号の全部のパートのキットを同時に変える。 */
	if (st == 0xc0 && port >= 0 && port < MAX_PORTS) {
		const int ch = (int)(message & 0x0f);
		const int map = m_drumMap[port][ch];
		if (map == 1 || map == 2) {
			m_kitPc[map] = (int)((message >> 8) & 0x7f);
			m_kitOn[map] = 1;
			ShareDrumKit(map);
		}
	}
}

void KbSasamiDecoder::sysex_message(int port, const void* data, std::size_t size)
{
	unsigned char routed[256];
	const unsigned char* d0 = (const unsigned char*)data;
	/* GS はケーブル 1 本のまま 40=ポートA / 50=ポートB。FF21 が 0 の
	   セットアップトラックに 50 が乗ると、B の音量・パン・音色が捨てられ、
	   モニタ（アドレスで B に載せる）だけ先に合う。 */
	if (d0 && size >= 8 && size <= sizeof(routed) && m_nPorts > 1
		&& d0[0] == 0xf0 && d0[1] == 0x41 && d0[3] == 0x42 && d0[4] == 0x12) {
		const int aa = d0[5] & 0x7f;
		int blk = -1;
		if (aa == 0x50) blk = 1;
		else if (aa == 0x60 && m_nPorts > 2) blk = 2;
		if (blk > 0 && blk < m_nPorts && m_synths[blk]) {
			memcpy(routed, d0, size);
			routed[5] = 0x40;
			data = routed;
			port = blk;
		}
	}
	if (port < 0 || port >= m_nPorts) port = 0;
	int rhyCh = -1;
	int rhyMap = -1;
	{
		const unsigned char* sx = (const unsigned char*)data;
		if (sx && size >= 11 && sx[0] == 0xf0 && sx[1] == 0x41 && sx[3] == 0x42
			&& sx[4] == 0x12 && sx[5] == 0x40 && (sx[6] & 0xf0) == 0x10
			&& sx[7] == 0x15 && sx[size - 1] == 0xf7) {
			const int block = sx[6] & 0x0f;
			rhyCh = (block == 0) ? 9 : (block < 10 ? block - 1 : block);
			const int v = sx[8] & 0x7f;
			rhyMap = (v == 2) ? 2 : ((v == 1) ? 1 : 0);
		}
	}
	m_synths[port]->sysex_message(data, size);
	if (rhyCh >= 0 && rhyCh < 16 && port >= 0 && port < MAX_PORTS) {
		m_drumMap[port][rhyCh] = rhyMap;
		if (rhyMap == 1 || rhyMap == 2) {
			if (m_kitOn[rhyMap])
				ShareDrumKit(rhyMap);
			else if (m_synths[port])
				m_synths[port]->program_change(rhyCh, 0);
		}
	}
	const unsigned char* d = (const unsigned char*)data;
	/* GM/GM2 On はバンクを捨てて PC だけになる。判定が GS/XG なら wopn のモードへ戻す。 */
	int gmReset = (d && size == 6 && d[0] == 0xf0 && d[1] == 0x7e && d[2] == 0x7f &&
		d[3] == 0x09 && (d[4] == 0x01 || d[4] == 0x02 || d[4] == 0x03) && d[5] == 0xf7) ? 1 : 0;
	if (gmReset && (m_wopnMode == (int)system_mode_gs || m_wopnMode == (int)system_mode_xg)) {
		if (m_synths[port])
			m_synths[port]->set_system_mode((system_mode_t)m_wopnMode);
		ApplyGsBankLsb();
		ResetDrumMaps();
		return;
	}
	if (d0 && size >= 11 && d0[0] == 0xf0 && d0[1] == 0x41 && d0[3] == 0x42 && d0[4] == 0x12
		&& d0[5] == 0x40 && d0[6] == 0x00 && d0[7] == 0x7f && (d0[8] & 0x7f) == 0x00) {
		/* GS Reset。ドラム番号とキットを初期（A10=MAP1、B10=MAP2）に戻す。 */
		ResetDrumMaps();
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
	ResetDrumMaps();
	KbsMonNotesOff();
}

void KbSasamiDecoder::ResetDrumMaps()
{
	for (int p = 0; p < MAX_PORTS; p++) {
		for (int c = 0; c < 16; c++)
			m_drumMap[p][c] = 0;
	}
	/* 音源全体でドラムは2つ。A のパート10が MAP1、B のパート10が MAP2。 */
	m_drumMap[0][9] = 1;
	if (m_nPorts > 1)
		m_drumMap[1][9] = 2;
	for (int m = 0; m < 3; m++) {
		m_kitPc[m] = 0;
		m_kitOn[m] = 0;
	}
}

void KbSasamiDecoder::ShareDrumKit(int map)
{
	if (map < 1 || map > 2 || !m_kitOn[map]) return;
	const int kit = m_kitPc[map] & 127;
	for (int p = 0; p < m_nPorts; p++) {
		if (!m_synths[p]) continue;
		for (int c = 0; c < 16; c++) {
			if (m_drumMap[p][c] != map) continue;
			m_synths[p]->program_change(c, kit);
		}
	}
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
	/* 前の曲の J を引き継ぐと、ループの無い mid がホスト側で回り続ける */
	m_MediaInfo.qwLoop = 0;
	m_MediaInfo.dwLoopCount = 0;
	m_loopStart = -1.0;
	m_loopEnd = -1.0;
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
		/* J で切った 1 周を qwLoop にする。回数とフェードは本体（書き出し含む）。 */
		if (m_loopEnd > m_loopStart && m_loopStart >= 0.0) {
			const UINT64 ls = (UINT64)(m_loopStart * (double)rate + 0.5);
			const UINT64 le = (UINT64)(m_loopEnd * (double)rate + 0.5);
			if (le > ls + 1) {
				m_MediaInfo.qwLength = kpi_SampleTo100ns(le, rate);
				m_MediaInfo.qwLoop = kpi_SampleTo100ns(le - ls, rate);
				m_MediaInfo.qwFadeOut = (UINT64)-1;
				m_MediaInfo.dwLoopCount = 0;
				m_lastSample = le;
			}
		}
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
	m_seqAutoLoop = 0;

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
		m_seqAutoLoop = 0;
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
			m_fm.SetFmMonDump(1, pathForKind, m_raira);
		else
			m_fm.SetFmMonDump(0, pathForKind, m_raira);
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
		{
			m_MediaInfo.qwLoop = 0;
			m_MediaInfo.dwLoopCount = 0;
			const UINT64 ls = m_fm.LoopStartSample();
			const UINT64 le = m_fm.LoopEndSample();
			const DWORD sr = m_MediaInfo.dwSampleRate;
			if (sr > 0 && le > ls + 1) {
				m_MediaInfo.qwLength = kpi_SampleTo100ns(le, sr);
				m_MediaInfo.qwLoop = kpi_SampleTo100ns(le - ls, sr);
				m_MediaInfo.qwFadeOut = (UINT64)-1;
				m_MediaInfo.dwLoopCount = 0;
				m_lastSample = le;
			}
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
		m_MediaInfo.qwLoop = 0;
		m_MediaInfo.dwLoopCount = 0;
		m_loopStart = -1.0;
		m_loopEnd = -1.0;
		{
			MemFile mf;
			mf.p = m_smf;
			mf.size = (DWORD)m_smfSize;
			mf.pos = 0;
			if (m_sequencer.load(&mf, MemGetc)) {
				m_loopStart = m_sequencer.find_marker("loopStart");
				m_loopEnd = m_sequencer.find_marker("loopEnd");
			}
		}
		int vr = OpenForeignVst(m_smf, (DWORD)m_smfSize);
		if (vr <= 0) return 0;
		if (m_loopEnd > m_loopStart && m_loopStart >= 0.0) {
			const DWORD sr = m_MediaInfo.dwSampleRate ? m_MediaInfo.dwSampleRate : rate;
			const UINT64 ls = (UINT64)(m_loopStart * (double)sr + 0.5);
			const UINT64 le = (UINT64)(m_loopEnd * (double)sr + 0.5);
			if (le > ls + 1) {
				m_MediaInfo.qwLength = kpi_SampleTo100ns(le, sr);
				m_MediaInfo.qwLoop = kpi_SampleTo100ns(le - ls, sr);
				m_MediaInfo.qwFadeOut = (UINT64)-1;
				m_MediaInfo.dwLoopCount = 0;
				m_lastSample = le;
			}
		}
		MonShow(0, pathForKind);
		return 1;
	}

	m_seqAutoLoop = 1;
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
	if (!KbVstSessionOpen(m_smf, (DWORD)m_smfSize, m_vstGs, m_vstXg, &m_vstSess,
		m_vstGsSf[0] ? m_vstGsSf : NULL, m_vstXgSf[0] ? m_vstXgSf : NULL))
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
	if (m_loopClosed) return 0;
	const double rate = (double)m_MediaInfo.dwSampleRate;
	const int looping = (m_seqAutoLoop && m_loopEnd > m_loopStart && m_loopStart >= 0.0) ? 1 : 0;
	const UINT64 loopStartSamp = looping ? (UINT64)(m_loopStart * rate + 0.5) : 0;
	const UINT64 loopEndSamp = looping ? (UINT64)(m_loopEnd * rate + 0.5) : 0;

	DWORD remain = dwSizeSample;
	BYTE* p = pBuffer;
	const int outBits = m_MediaInfo.nBitsPerSample ? m_MediaInfo.nBitsPerSample : -64;
	const DWORD bytesPerFrame = FmPcmBytesPerFrame(outBits);
	const double gain = m_raira ? kFmMidiOutGain : (kFmMidiOutGain * 0.5 / 1.5);
	while (remain) {
		/* FPY と同じ。ここで短く返すと本体が loop1（Q より前、先頭付近）へ
		   シークし、1回目の J が曲頭から鳴る。Q の時刻へ位置だけ戻して続ける。
		   play(0) はしない。先頭から送り直すと Q より前のノートが残る。 */
		if (looping && loopEndSamp > loopStartSamp + 1 && m_curSample >= loopEndSamp) {
			m_sequencer.set_position(m_loopStart);
			m_curSample = loopStartSamp;
			m_loopClosed = 0;
		}
		DWORD chunk = remain;
		if (chunk > MIX_FRAMES) chunk = MIX_FRAMES;
		if (looping && loopEndSamp > loopStartSamp && m_curSample < loopEndSamp
			&& m_curSample + chunk > loopEndSamp)
			chunk = (DWORD)(loopEndSamp - m_curSample);
		if (chunk == 0) {
			if (looping && loopEndSamp > loopStartSamp + 1 && m_curSample >= loopEndSamp) {
				m_sequencer.set_position(m_loopStart);
				m_curSample = loopStartSamp;
				m_loopClosed = 0;
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
	const double rate = (double)m_MediaInfo.dwSampleRate;
	const int haveLoop = (m_loopEnd > m_loopStart && m_loopStart >= 0.0 && rate > 0.0) ? 1 : 0;
	const UINT64 ls = haveLoop ? (UINT64)(m_loopStart * rate + 0.5) : 0;
	const UINT64 le = haveLoop ? (UINT64)(m_loopEnd * rate + 0.5) : 0;
	const UINT64 loopNear = (rate > 0.0) ? (UINT64)rate : 44100ull;
	const int nearLoop = (haveLoop && qwPosSample + loopNear >= ls && qwPosSample <= ls + loopNear) ? 1 : 0;
	const int atEnd = (haveLoop && le > ls && m_curSample + 1 >= le) ? 1 : 0;
	/* FPY の軟シーク。周回直後の先頭シークで play(0) すると Q より前に戻る。 */
	if (haveLoop && atEnd && (nearLoop || qwPosSample == 0)) {
		m_seeking = false;
		m_loopClosed = 0;
		m_sequencer.set_position(m_loopStart);
		m_curSample = ls;
		m_endSample = 0;
		m_silentSample = 0;
		return m_curSample;
	}
	m_seeking = true;
	m_loopClosed = 0;
	m_sequencer.play(0, this);
	reset();
	UINT64 pos = qwPosSample;
	if (haveLoop && le > ls && pos >= le)
		pos = ls + ((pos - ls) % (le - ls));
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
