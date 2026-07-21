// SongParams.cpp : 曲ごとのオーディオ/DSP パラメータ 保持・復元
#include "stdafx.h"
#include "SongParams.h"
#include "ogg.h"
#include "oggDlg.h"
#include "PlayList.h"
#include "CEqualizer.h"
#include "CAnalyzerDlg.h"
#include <afxmt.h>
#include <vector>

// ---- 参照するグローバル ----
extern save savedata;
extern COggDlg* og;
extern CPlayList* pl;
extern CString filen;   // 現在再生中の曲フルパス
extern int tempo;       // テンポ スライダー位置 0..400
extern int pitch;       // ピッチ スライダー位置 0..400

extern TCHAR karento2[1024]; // 実行ファイルのディレクトリ(末尾 \)

// ---- ファイル名 ----
#if _UNICODE
#define SONGPARAM_DAT_NAME L"oggYSEDbgmu_AudioData.dat"
#else
#define SONGPARAM_DAT_NAME "oggYSEDbgm_AudioData.dat"
#endif
static const int SONGPARAM_FILE_VERSION = 1;

// ---- メモリ内テーブル(g_cs で保護) ----
static CCriticalSection g_cs;
static std::vector<SongParam> g_tbl;

// ---- 保存デバウンス ----
static bool  g_dirty = false;
static DWORD g_lastWriteTick = 0;

// ---- 現在再生中の曲を追跡する状態 ----
static CString  s_curPath;       // 正規化済み(曲識別=パスのみで判定)
static CString  s_curList;       // 曲開始時に確定したリスト名
static bool     s_baselineValid = false;
static SongParam s_lastSaved;    // 直近に保存/復元したパラメータ(変更検知の基準)

// ============================================================================
// 小物
// ============================================================================
static int ClampI(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

// パラメータ部(listName/path を除く)だけを比較
static bool ParamsEqual(const SongParam& a, const SongParam& b)
{
	if (a.dsvol != b.dsvol || a.kakuVol != b.kakuVol) return false;
	if (a.pitchPos != b.pitchPos || a.tempoPos != b.tempoPos) return false;
	for (int i = 0; i < 20; i++) if (a.eq[i] != b.eq[i]) return false;
	if (a.eqsoundenv != b.eqsoundenv || a.eqsoundeq != b.eqsoundeq) return false;
	if (a.eqsoundeffect != b.eqsoundeffect) return false;
	if (a.eq_reverb != b.eq_reverb || a.eq_chorus != b.eq_chorus || a.eq_delay != b.eq_delay) return false;
	if (a.analyzerspecstyle != b.analyzerspecstyle) return false;
	return true;
}

// 現在のライブ値(savedata + グローバル)からパラメータ部を埋める
static void SnapshotCurrent(SongParam& p)
{
	p.dsvol = savedata.dsvol;
	p.kakuVol = savedata.kakuVol;
	p.pitchPos = pitch;
	p.tempoPos = tempo;
	for (int i = 0; i < 20; i++) p.eq[i] = savedata.eq[i];
	p.eqsoundenv = savedata.eqsoundenv;
	p.eqsoundeq = savedata.eqsoundeq;
	p.eqsoundeffect = savedata.eqsoundeffect;
	p.eq_reverb = savedata.eq_reverb;
	p.eq_chorus = savedata.eq_chorus;
	p.eq_delay = savedata.eq_delay;
	p.analyzerspecstyle = savedata.analyzerspecstyle;
}

// g_cs を保持した状態で呼ぶこと。見つからなければ -1。
static int FindIndexLocked(LPCTSTR listName, LPCTSTR path)
{
	if (!listName || !path) return -1;
	for (size_t i = 0; i < g_tbl.size(); i++) {
		if (_tcsicmp(g_tbl[i].listName, listName) == 0 &&
			_tcsicmp(g_tbl[i].path, path) == 0)
			return (int)i;
	}
	return -1;
}

CString SongParams_CurrentListName()
{
	int n = savedata.playlistnum;
	if (n < 0 || n >= 1000) n = 0;
	CString s = savedata.playlistname[n];
	if (s.IsEmpty()) s.Format(L"#%d", n); // 未命名でも安定するようにインデックス名で代替
	return s;
}

// ============================================================================
// ファイル入出力
// ============================================================================
void SongParams_LoadFile()
{
	CSingleLock lk(&g_cs, TRUE);
	g_tbl.clear();
	CString ss = karento2; ss += SONGPARAM_DAT_NAME;
	CFile f;
	if (f.Open(ss, CFile::modeRead | CFile::shareDenyWrite, NULL) != TRUE)
		return;
	try {
		int ver = 0, cnt = 0;
		if (f.Read(&ver, sizeof(int)) != sizeof(int)) { f.Close(); return; }
		if (f.Read(&cnt, sizeof(int)) != sizeof(int)) { f.Close(); return; }
		if (cnt < 0) cnt = 0;
		if (cnt > 100000) cnt = 100000; // 異常値ガード
		for (int i = 0; i < cnt; i++) {
			SongParam e;
			ZeroMemory(&e, sizeof(e));
			UINT got = f.Read(&e, sizeof(SongParam));
			if (got != sizeof(SongParam)) break; // 途中で切れていたら打ち切り
			e.listName[255] = 0;
			e.path[1023] = 0;
			g_tbl.push_back(e);
		}
	}
	catch (...) {
	}
	f.Close();
	g_dirty = false;
}

void SongParams_SaveFile()
{
	CSingleLock lk(&g_cs, TRUE);
	CString ss = karento2; ss += SONGPARAM_DAT_NAME;
	CFile f;
	if (f.Open(ss, CFile::modeCreate | CFile::modeWrite | CFile::shareExclusive, NULL) != TRUE)
		return;
	try {
		int ver = SONGPARAM_FILE_VERSION;
		int cnt = (int)g_tbl.size();
		f.Write(&ver, sizeof(int));
		f.Write(&cnt, sizeof(int));
		for (int i = 0; i < cnt; i++)
			f.Write(&g_tbl[i], sizeof(SongParam));
	}
	catch (...) {
	}
	f.Close();
	g_dirty = false;
	g_lastWriteTick = GetTickCount();
}

static void FlushIfDirty()
{
	bool dirty;
	{ CSingleLock lk(&g_cs, TRUE); dirty = g_dirty; }
	if (dirty) SongParams_SaveFile();
}

static void MarkDirtyAndMaybeWrite()
{
	DWORD now = GetTickCount();
	{ CSingleLock lk(&g_cs, TRUE); g_dirty = true; }
	// 連続変更(スライダードラッグ等)をまとめる。500ms ごとに書き出し。
	if (now - g_lastWriteTick >= 500)
		SongParams_SaveFile();
}

// ============================================================================
// 検索・更新・移行
// ============================================================================
bool SongParams_FindCopy(LPCTSTR listName, LPCTSTR path, SongParam& out)
{
	CSingleLock lk(&g_cs, TRUE);
	int idx = FindIndexLocked(listName, path);
	if (idx < 0) return false;
	out = g_tbl[idx];
	return true;
}

static bool FindExists(LPCTSTR listName, LPCTSTR path)
{
	CSingleLock lk(&g_cs, TRUE);
	return FindIndexLocked(listName, path) >= 0;
}

static void Upsert(LPCTSTR listName, LPCTSTR path, const SongParam& params)
{
	CSingleLock lk(&g_cs, TRUE);
	int idx = FindIndexLocked(listName, path);
	if (idx < 0) {
		SongParam e = params;
		_tcsncpy(e.listName, listName, 255); e.listName[255] = 0;
		_tcsncpy(e.path, path, 1023); e.path[1023] = 0;
		g_tbl.push_back(e);
	}
	else {
		SongParam& e = g_tbl[idx];
		// キーは保ちつつパラメータだけ更新
		e.dsvol = params.dsvol; e.kakuVol = params.kakuVol;
		e.pitchPos = params.pitchPos; e.tempoPos = params.tempoPos;
		for (int i = 0; i < 20; i++) e.eq[i] = params.eq[i];
		e.eqsoundenv = params.eqsoundenv; e.eqsoundeq = params.eqsoundeq;
		e.eqsoundeffect = params.eqsoundeffect;
		e.eq_reverb = params.eq_reverb; e.eq_chorus = params.eq_chorus; e.eq_delay = params.eq_delay;
		e.analyzerspecstyle = params.analyzerspecstyle;
	}
}

void SongParams_ResetAll()
{
	{
		CSingleLock lk(&g_cs, TRUE);
		g_tbl.clear();
		g_dirty = false;
	}
	CString ss = karento2; ss += SONGPARAM_DAT_NAME;
	::DeleteFile(ss);
	// 追跡状態もクリア(次の同一曲でも復元しない)
	s_curPath.Empty();
	s_baselineValid = false;
}

void SongParams_RenameList(LPCTSTR oldName, LPCTSTR newName)
{
	if (!oldName || !newName || !*newName) return;
	bool changed = false;
	{
		CSingleLock lk(&g_cs, TRUE);
		for (size_t i = 0; i < g_tbl.size(); i++) {
			if (_tcsicmp(g_tbl[i].listName, oldName) == 0) {
				_tcsncpy(g_tbl[i].listName, newName, 255);
				g_tbl[i].listName[255] = 0;
				changed = true;
			}
		}
	}
	if (changed) SongParams_SaveFile();
	// 追跡中リスト名も追従
	if (s_curList.CompareNoCase(oldName) == 0) s_curList = newName;
}

void SongParams_DeleteList(LPCTSTR name)
{
	if (!name) return;
	bool changed = false;
	{
		CSingleLock lk(&g_cs, TRUE);
		for (size_t i = 0; i < g_tbl.size(); ) {
			if (_tcsicmp(g_tbl[i].listName, name) == 0) {
				g_tbl.erase(g_tbl.begin() + i);
				changed = true;
			}
			else i++;
		}
	}
	if (changed) SongParams_SaveFile();
}

// ============================================================================
// 適用(メインスレッド)
// ============================================================================
void SongParams_ApplyEntryToMain(const SongParam& e)
{
	// savedata / グローバルへ反映(DSP はここを直接読むため即効く)
	savedata.dsvol = ClampI(e.dsvol, -498, 1);
	savedata.kakuVol = ClampI(e.kakuVol, 100, 900);
	savedata.kakuVal = savedata.kakuVol;
	tempo = ClampI(e.tempoPos, 0, 400);
	pitch = ClampI(e.pitchPos, 0, 400);
	for (int i = 0; i < 20; i++) savedata.eq[i] = ClampI(e.eq[i], 0, 200);
	savedata.eqsoundenv = e.eqsoundenv;
	savedata.eqsoundeq = e.eqsoundeq;
	savedata.eqsoundeffect = ClampI(e.eqsoundeffect, 0, 100);
	savedata.eq_reverb = ClampI(e.eq_reverb, 0, 200);
	savedata.eq_chorus = ClampI(e.eq_chorus, 0, 200);
	savedata.eq_delay = ClampI(e.eq_delay, 0, 200);
	savedata.analyzerspecstyle = ClampI(e.analyzerspecstyle, 0, 6);

	if (!og || !::IsWindow(og->GetSafeHwnd()))
		return;

	// メイン画面のスライダー(OnTimer が毎tick スライダー→savedata へ写すため必須)
	if (og->m_dsval.GetSafeHwnd())    og->m_dsval.SetPos(savedata.dsvol);
	if (og->m_kakuVol.GetSafeHwnd())  og->m_kakuVol.SetPos(savedata.kakuVol);
	if (og->m_tempo_sl.GetSafeHwnd()) og->m_tempo_sl.SetPos(tempo);
	if (og->m_pitch_sl.GetSafeHwnd()) og->m_pitch_sl.SetPos(pitch);

	// イコライザー画面が開いていれば、そのスライダー/コンボも更新
	// (開いている間は EQ の OnTimer がスライダー→savedata へ写すため必須)
	if (og->m_EqualizerDlg && ::IsWindow(og->m_EqualizerDlg->GetSafeHwnd())) {
		if (og->m_EqualizerDlg->m_pre.GetSafeHwnd())
			og->m_EqualizerDlg->m_pre.SetCurSel(savedata.eqsoundeq);
		if (og->m_EqualizerDlg->m_env.GetSafeHwnd())
			og->m_EqualizerDlg->m_env.SetCurSel(savedata.eqsoundenv);
		og->m_EqualizerDlg->SyncSlidersFromSavedata();
	}

	// アナライザーが開いていれば周波数表示モードを反映
	if (og->m_AnalyzerDlg && ::IsWindow(og->m_AnalyzerDlg->GetSafeHwnd()))
		og->m_AnalyzerDlg->ApplySpecStyleExternal(savedata.analyzerspecstyle);
}

// ============================================================================
// 同期本体(HandleNotifications / _export の先頭で呼ぶ)
// ============================================================================
void SongParams_Sync(bool exporting)
{
	CString path = NormalizePlaylistPath(filen);
	if (path.IsEmpty()) return;

	const bool feature = (savedata.saveSongParams != 0);

	if (exporting) {
		// WAV 出力: 曲頭で 1 回だけ呼ばれる(メインスレッド)。
		// 再生用の追跡状態(s_curPath 等)とは独立に、エントリがあれば即適用する。
		if (!feature) return;
		SongParam e;
		if (SongParams_FindCopy(SongParams_CurrentListName(), path, e))
			SongParams_ApplyEntryToMain(e);
		return;
	}

	// 以降は再生(HandleNotifications ループ)から周期的に呼ばれる
	if (path != s_curPath) {
		// --- 曲が変わった ---
		FlushIfDirty(); // 前の曲の未書き込みを確定
		s_curPath = path;
		s_curList = SongParams_CurrentListName(); // 曲開始時のリスト名で固定
		s_baselineValid = false;

		if (feature) {
			SongParam e;
			if (SongParams_FindCopy(s_curList, s_curPath, e)) {
				// 再生はこの関数を別スレッドから呼ぶので、UI 操作はメインスレッドへ委譲。
				// 復元が適用される前に「復元前の現在値」で保存し直してエントリを壊さない
				// よう、ベースライン確定は復元適用後(同一曲ブランチ)まで遅延させる。
				s_baselineValid = false;
				if (og && ::IsWindow(og->GetSafeHwnd()))
					og->PostMessage(WM_APP_SONGPARAM_RESTORE, 0, (LPARAM)new SongParam(e));
			}
			else {
				// エントリ無し: 設定は触らず、現状値を基準にしておく
				SnapshotCurrent(s_lastSaved);
				s_baselineValid = true;
			}
		}
		return;
	}

	// --- 同じ曲 ---
	if (!feature) { s_baselineValid = false; return; }

	if (!s_baselineValid) {
		// 機能を途中で ON にした場合など: 現状値を基準化して今回は保存しない
		SnapshotCurrent(s_lastSaved);
		s_baselineValid = true;
		return;
	}

	SongParam cur;
	SnapshotCurrent(cur);
	if (!ParamsEqual(cur, s_lastSaved)) {
		Upsert(s_curList, s_curPath, cur);
		s_lastSaved = cur;
		MarkDirtyAndMaybeWrite();
	}
}

// ============================================================================
// ツールチップ用: デフォルトと異なる項目だけを整形
// ============================================================================
static void TipAppend(CString& out, const CString& item)
{
	if (item.IsEmpty()) return;
	if (!out.IsEmpty()) out += L", ";
	out += item;
}

// テンポ/ピッチ スライダー位置(0..400)→ %(200=100%)
static double PosToPercent(int pos)
{
	if (pos >= 200) return (double)(pos - 100);
	return pos / 3.0 + 33.3;
}

CString SongParams_BuildTipExtra(LPCTSTR listName, LPCTSTR path)
{
	SongParam e;
	CString np = NormalizePlaylistPath(path);
	if (!SongParams_FindCopy(listName, np, e))
		return CString();

	CString body;
	CString tmp;

	if (e.dsvol != 1) {
		double pct = (e.dsvol + 499) * 2.0 / 10.0;
		tmp.Format(L"%s=%.1f%%", LL14(L"音量", L"Volume", L"Volume", L"Volume", L"Volumen", L"볼륨", L"音量", L"مستوى الصوت", L"Громкость", L"Lautstärke", L"Volume", L"Volume", L"Głośność", L"Ses"), pct);
		TipAppend(body, tmp);
	}
	if (e.kakuVol != 100) {
		tmp.Format(L"%s=%d%%", LL14(L"拡張音量", L"Ext.vol", L"Vol.ext", L"Vol.est", L"Vol.ext", L"확장 볼륨", L"扩展音量", L"صوت موسع", L"Расш.громк", L"Ext.Lautst", L"Vol.ext", L"Ext.vol", L"Głośn.rozsz", L"Geniş ses"), e.kakuVol);
		TipAppend(body, tmp);
	}
	if (e.tempoPos != 200) {
		tmp.Format(L"%s=%d%%", LL14(L"テンポ", L"Tempo", L"Tempo", L"Tempo", L"Tempo", L"템포", L"速度", L"الإيقاع", L"Темп", L"Tempo", L"Andamento", L"Tempo", L"Tempo", L"Tempo"), (int)(PosToPercent(e.tempoPos) + 0.5));
		TipAppend(body, tmp);
	}
	if (e.pitchPos != 200) {
		tmp.Format(L"%s=%d%%", LL14(L"ピッチ", L"Pitch", L"Hauteur", L"Intonazione", L"Tono", L"피치", L"音高", L"طبقة الصوت", L"Высота", L"Tonhöhe", L"Tom", L"Toonhoogte", L"Wysokość", L"Perde"), (int)(PosToPercent(e.pitchPos) + 0.5));
		TipAppend(body, tmp);
	}

	// EQ 15バンド: いずれかが中立(100)以外なら 15 個表示
	bool eqChanged = false;
	for (int i = 0; i < 15; i++) if (e.eq[i] != 100) { eqChanged = true; break; }
	if (eqChanged) {
		CString bands;
		for (int i = 0; i < 15; i++) {
			if (i) bands += L",";
			CString v; v.Format(L"%d", e.eq[i]); bands += v;
		}
		tmp.Format(L"%s=[%s]", LL14(L"EQ", L"EQ", L"EQ", L"EQ", L"EQ", L"EQ", L"EQ", L"EQ", L"EQ", L"EQ", L"EQ", L"EQ", L"EQ", L"EQ"), bands);
		TipAppend(body, tmp);
	}
	if (e.eq[15] != 100) { tmp.Format(L"%s=%d", LL14(L"マスター", L"Master", L"Maître", L"Master", L"Máster", L"마스터", L"主控", L"رئيسي", L"Мастер", L"Master", L"Mestre", L"Master", L"Master", L"Ana"), e.eq[15]); TipAppend(body, tmp); }
	if (e.eq[16] != 100) { tmp.Format(L"%s=%d", LL14(L"明瞭", L"Clarity", L"Clarté", L"Chiarezza", L"Claridad", L"명료", L"清晰", L"وضوح", L"Чёткость", L"Klarheit", L"Clareza", L"Helderheid", L"Klarość", L"Netlik"), e.eq[16]); TipAppend(body, tmp); }
	if (e.eq[17] != 100) { tmp.Format(L"%s=%d", LL14(L"バランス", L"Balance", L"Balance", L"Bilanciam", L"Balance", L"밸런스", L"平衡", L"توازن", L"Баланс", L"Balance", L"Balanço", L"Balans", L"Balans", L"Denge"), e.eq[17]); TipAppend(body, tmp); }
	if (e.eq[18] != 100) { tmp.Format(L"%s=%d", LL14(L"密度", L"Density", L"Densité", L"Densità", L"Densidad", L"밀도", L"密度", L"كثافة", L"Плотность", L"Dichte", L"Densidade", L"Dichtheid", L"Gęstość", L"Yoğunluk"), e.eq[18]); TipAppend(body, tmp); }
	if (e.eq[19] != 100) { tmp.Format(L"%s=%d", LL14(L"立体", L"3D", L"3D", L"3D", L"3D", L"입체", L"立体", L"مجسم", L"3D", L"3D", L"3D", L"3D", L"3D", L"3D"), e.eq[19]); TipAppend(body, tmp); }

	if (e.eqsoundenv != 0) {
		tmp.Format(L"%s=#%d(%d%%)", LL14(L"環境", L"Env", L"Env", L"Amb", L"Entorno", L"환경", L"环境", L"بيئة", L"Среда", L"Umgeb", L"Amb", L"Omg", L"Środ", L"Ortam"), e.eqsoundenv, e.eqsoundeffect);
		TipAppend(body, tmp);
	}
	if (e.eqsoundeq != 0) {
		tmp.Format(L"%s=#%d", LL14(L"EQﾌﾟﾘｾｯﾄ", L"EQ preset", L"EQ preset", L"EQ preset", L"EQ preset", L"EQ 프리셋", L"EQ预设", L"إعداد EQ", L"EQ пресет", L"EQ Preset", L"EQ preset", L"EQ preset", L"EQ preset", L"EQ ön ayar"), e.eqsoundeq);
		TipAppend(body, tmp);
	}
	if (e.eq_reverb != 0) { tmp.Format(L"%s=%d", LL14(L"リバーブ", L"Reverb", L"Réverb", L"Riverbero", L"Reverb", L"리버브", L"混响", L"صدى", L"Реверб", L"Hall", L"Reverb", L"Reverb", L"Pogłos", L"Yankı"), e.eq_reverb); TipAppend(body, tmp); }
	if (e.eq_chorus != 0) { tmp.Format(L"%s=%d", LL14(L"コーラス", L"Chorus", L"Chorus", L"Chorus", L"Chorus", L"코러스", L"合唱", L"كورس", L"Хорус", L"Chorus", L"Chorus", L"Chorus", L"Chór", L"Koro"), e.eq_chorus); TipAppend(body, tmp); }
	if (e.eq_delay != 0) { tmp.Format(L"%s=%d", LL14(L"ディレイ", L"Delay", L"Delay", L"Delay", L"Delay", L"딜레이", L"延迟", L"تأخير", L"Задержка", L"Delay", L"Delay", L"Delay", L"Delay", L"Gecikme"), e.eq_delay); TipAppend(body, tmp); }

	if (e.analyzerspecstyle != 0) {
		tmp.Format(L"%s=%d", LL14(L"表示", L"Disp", L"Aff", L"Disp", L"Vista", L"표시", L"显示", L"عرض", L"Вид", L"Anz", L"Exib", L"Weerg", L"Wyśw", L"Görün"), e.analyzerspecstyle);
		TipAppend(body, tmp);
	}

	if (body.IsEmpty())
		return CString();

	CString head = LL14(L"保存パラメータ", L"Saved params", L"Params enreg", L"Param salv", L"Params guard", L"저장 파라미터", L"已存参数", L"معلمات محفوظة", L"Сохр.параметры", L"Gespeichert", L"Params salvos", L"Opgeslagen", L"Zapisane param", L"Kayıtlı param");
	CString out;
	out.Format(L"\n%s: %s", head, body);
	return out;
}
