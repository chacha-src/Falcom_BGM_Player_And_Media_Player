// ogg.cpp : アプリケーション用クラスの定義を行います。
//

#include "stdafx.h"
#include "Windows.h"
#include "ogg.h"
#include "oggDlg.h"
#include "CMediaPlayerDlg.h"
#include "UpdateCheck.h"
#include "SongParams.h"
#include "ProAudio.h"
#include "MpSidecar.h"
#include "direct.h"
#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

#include "rubberband/RubberBandStretcher.h"
/////////////////////////////////////////////////////////////////////////////
// COggApp

BEGIN_MESSAGE_MAP(COggApp, CWinApp)
	//{{AFX_MSG_MAP(COggApp)
		// メモ - ClassWizard はこの位置にマッピング用のマクロを追加または削除します。
		//        この位置に生成されるコードを編集しないでください。
	//}}AFX_MSG
	ON_COMMAND(ID_HELP, CWinApp::OnHelp)
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// COggApp クラスの構築

COggApp::COggApp()
{
	// TODO: この位置に構築用のコードを追加してください。
	// ここに InitInstance 中の重要な初期化処理をすべて記述してください。
}

/////////////////////////////////////////////////////////////////////////////
// 唯一の COggApp オブジェクト

COggApp theApp;
extern COggDlg *og;
/////////////////////////////////////////////////////////////////////////////
// COggApp クラスの初期化
save savedata;
TCHAR karento2[1024];
CString ndd;
void *Mutex;
TCHAR* nd;
BOOL CALLBACK ew(HWND hwnd , LPARAM lp);
BOOL CALLBACK ew(HWND hwnd,LPARAM lParam) {
	TCHAR a[1024];CString s;
	::GetWindowText(hwnd,a,_countof(a));s=a;
	// 既存インスタンスのタイトルは言語依存。ここは mutex 直後で savedata 未読込のため
	// LL14(現在言語)ではなく、SetWindowText 側と同じ14言語タイトルをすべて照合する。
	static const TCHAR* const kMainTitles[] = {
		_T("mp3/m4a簡易プレイヤ"),
		_T("mp3/m4a Simple Player"),
		_T("mp3/m4a Lecteur simple"),
		_T("mp3/m4a Lettore semplice"),
		_T("mp3/m4a Reproductor simple"),
		_T("mp3/m4a 간이 플레이어"),
		_T("mp3/m4a 简易播放器"),
		_T("mp3/m4a مشغل بسيط"),
		_T("mp3/m4a Простой плеер"),
		_T("mp3/m4a Einfacher Player"),
		_T("mp3/m4a Player simples"),
		_T("mp3/m4a Eenvoudige speler"),
		_T("mp3/m4a Prosty odtwarzacz"),
		_T("mp3/m4a Basit oynat"),
	};
	BOOL hit = FALSE;
	for (int i = 0; i < (int)_countof(kMainTitles); ++i) {
		if (s.Find(kMainTitles[i]) != -1) { hit = TRUE; break; }
	}
	if (!hit) return TRUE;
	COPYDATASTRUCT cd;
	cd.cbData=ndd.GetLength()*sizeof(TCHAR)+sizeof(TCHAR);
	cd.dwData=0;
	cd.lpData=nd;
	::SendMessage(hwnd,WM_COPYDATA, (WPARAM)NULL, (LPARAM)&cd);
	if(ndd.Left(1)!="*")
		::PostMessage(hwnd,WM_APP +1, (WPARAM)NULL, (LPARAM)NULL);
	return FALSE;
}

LRESULT COggApp::ProcessWndProcException(CException* e, const MSG* pMsg)
{
	// デバッガ出力に「どのウィンドウのどのメッセージ処理中か」を残す
	if (e) {
		TCHAR errmsg[512] = {};
		e->GetErrorMessage(errmsg, _countof(errmsg) - 1);
		TCHAR cls[128] = {};
		TCHAR title[128] = {};
		if (pMsg && pMsg->hwnd) {
			::GetClassName(pMsg->hwnd, cls, _countof(cls));
			::GetWindowText(pMsg->hwnd, title, _countof(title));
		}
		CString line;
		line.Format(_T("[ProcessWndProcException] %hs msg=0x%04X hwnd=0x%p class=%s title=%s: %s\n"),
			e->GetRuntimeClass() ? e->GetRuntimeClass()->m_lpszClassName : "?",
			pMsg ? (UINT)pMsg->message : 0,
			pMsg ? pMsg->hwnd : NULL,
			cls[0] ? cls : _T("?"),
			title[0] ? title : _T(""),
			errmsg);
		OutputDebugString(line);
	}
	return CWinApp::ProcessWndProcException(e, pMsg);
}

BOOL COggApp::InitInstance()
{
	// マニフェストに dpiAwareness を足すと SxS が壊れやすいので API で Per-Monitor V2 を要求
	{
		HMODULE hUser = ::GetModuleHandleW(L"user32.dll");
		if (hUser) {
			typedef BOOL(WINAPI* PFN_SetProcessDpiAwarenessContext)(HANDLE);
			auto pSet = (PFN_SetProcessDpiAwarenessContext)::GetProcAddress(hUser, "SetProcessDpiAwarenessContext");
			if (pSet) {
#ifndef DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
#define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 ((HANDLE)(LONG_PTR)-4)
#endif
				pSet(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
			} else {
				typedef BOOL(WINAPI* PFN_SetProcessDPIAware)(void);
				auto pOld = (PFN_SetProcessDPIAware)::GetProcAddress(hUser, "SetProcessDPIAware");
				if (pOld) pOld();
			}
		}
	}
//	INITCOMMONCONTROLSEX InitCtrls;
//	InitCtrls.dwSize = sizeof(InitCtrls);
	// アプリケーションで使用するすべてのコモン コントロール クラスを含めるには、
	// これを設定します。
//	InitCtrls.dwICC = ICC_STANDARD_CLASSES;
//	InitCommonControlsEx(&InitCtrls);
	CWinApp::InitInstance();

	AfxEnableControlContainer();
//	_CrtSetBreakAlloc(8408);
	// 標準的な初期化処理
	// もしこれらの機能を使用せず、実行ファイルのサイズを小さくしたけ
	//  れば以下の特定の初期化ルーチンの中から不必要なものを削除して
	//  ください。
	ndd="";
	if(m_lpCmdLine[0] !=NULL){	ndd=m_lpCmdLine; nd=m_lpCmdLine;}
	Mutex = CreateMutex(NULL,FALSE,_T("oggEDYSbgm"));
    DWORD Status = GetLastError(); 
    if(Mutex == NULL){exit(-1);} 
    if(Status == ERROR_ALREADY_EXISTS){ 
        ReleaseMutex(Mutex);
		if(ndd!="")
			EnumWindows( ew, 0);
        exit(0); 
    } 
//	_getcwd(karento2,255);
	if (::GetModuleFileName(NULL, karento2, 1000)){    //実行ファイルのフルパスを取得
		   //取得に成功
			TCHAR* ptmp = _tcsrchr(karento2, _T('\\')); // \の最後の出現位置を取得
			if (ptmp != NULL){   //ファイル名を削除
				ptmp = _tcsinc(ptmp);   //一文字進める
				*ptmp = _T('\0');
			}
	}
	_tchdir(karento2);
	if (DatArc_Init(karento2))
		DatArc_Chdir();
	ZeroMemory(&savedata,sizeof(save));
	savedata.supe=1;
	savedata.xx=-10000;
	savedata.yy=-10000;
	savedata.gx=-10000;
	savedata.gy=-10000;
	savedata.dsvol=1;
	savedata.evr=1;
	savedata.con=1;
	savedata.ffd=1;
	savedata.audiost=1;
	_tcscpy(savedata.ysf,_T("C:\\FALCOM\\YSF_WIN"));
	_tcscpy(savedata.ys6,_T("C:\\FALCOM\\YS6_WIN"));
	_tcscpy(savedata.ed6sc,_T("C:\\FALCOM\\ED_SORA2"));
	_tcscpy(savedata.ed6fc,_T("C:\\FALCOM\\ED6_WIN"));
	_tcscpy(savedata.yso,_T("C:\\FALCOM\\YSO_WIN"));
	_tcscpy(savedata.ed6tc,_T("C:\\FALCOM\\ED_SORA3"));
	_tcscpy(savedata.zweiii,_T("C:\\FALCOM\\ZWEI2"));
	_tcscpy(savedata.ysc,_T("C:\\FALCOM\\YSC"));
	_tcscpy(savedata.xa,_T("C:\\FALCOM\\X_NEXT"));
	_tcscpy(savedata.ys12,_T("C:\\FALCOM\\YS12_CMP\\ys1_win"));
	_tcscpy(savedata.ys122,_T("C:\\FALCOM\\YS12_CMP\\ys2_win"));
	_tcscpy(savedata.sor,_T("C:\\FALCOM\\SORO_WIN"));
	_tcscpy(savedata.zwei,_T("C:\\FALCOM\\ZWEI"));
	_tcscpy(savedata.gurumin,_T("C:\\FALCOM\\GURUMIN"));
	_tcscpy(savedata.dino,_T("C:\\FALCOM\\DIN"));
	_tcscpy(savedata.br4,_T("C:\\FALCOM\\BR4_WIN"));
	_tcscpy(savedata.ed3,_T("C:\\FALCOM\\ED3_XP"));
	_tcscpy(savedata.ed4,_T("C:\\FALCOM\\ED4_XP"));
	_tcscpy(savedata.ed5,_T("C:\\FALCOM\\ED5_XP"));
	_tcscpy(savedata.tuki,_T("C:\\FALCOM\\SWORDA"));
	_tcscpy(savedata.nishi,_T("C:\\FALCOM\\ZEP_WIN"));
	_tcscpy(savedata.arc,_T("C:\\FALCOM\\Arcturus"));
	_tcscpy(savedata.san1,_T("C:\\FALCOM\\FS"));
	_tcscpy(savedata.san2,_T("C:\\FALCOM\\FS2"));
	_tcscpy(savedata.zero, _T(""));
	savedata.kaisuu=2;
	savedata.spc=1;
	savedata.mp3=1;
	savedata.savecheck=0;
	savedata.kpivol=1;
	_tcscpy(savedata.font1,_T("Consolas"));
	_tcscpy(savedata.font2,_T("メイリオ"));
	savedata.savecheck_mp3 = 1;
	savedata.savecheck_dshow = 1;
	savedata.bit24 = 1;
	savedata.bit32 = 1;
	savedata.m4a = 1;
	savedata.kakuVol = 100;
	savedata.kakuVal = 100;
	savedata.ms = 30;
	savedata.ms2 = 16;
	savedata.eqCodeMs = 25;
	savedata.pro_gapless = 0;
	savedata.pro_xfade_ms = 0; /* クロスフェード撤去 */
	savedata.pro_rg_mode = 0;
	savedata.pro_rg_target = -18;
	savedata.pro_ms_width = 100;
	savedata.pro_ms_mono = 0;
	savedata.pro_export_limit = 1;
	savedata.pro_export_ceiling = 99;
	savedata.pro_export_tp = 1;
	savedata.pro_corr_meter = 1;
	savedata.mpLoopbackScore = 0;
	savedata.mpChordPanel = 0;
	savedata.deskLrcOn = 0;
	savedata.deskLrcX = 80;
	savedata.deskLrcY = 80;
	savedata.deskLrcW = 640;
	savedata.deskLrcH = 160;
	savedata.deskLrcAlpha = 200;
	savedata.mpVocalCenter = 100;
	savedata.mpMirrorOut = 0;
	savedata.mpMirrorVol = 100;
	savedata.mpMirrorDevice[0] = 0;
	savedata.mpRemoteOn = 0;
	savedata.mpRemotePort = 8765;
	savedata.mpAlarmHour = -1;
	savedata.mpAlarmMin = 0;
	savedata.mpSsVizOn = 0;
	savedata.mpDetectedBpm = 0;
	savedata.mpDjPadwindow = 0;
	savedata.mpNormTargetLufs = -14;
	savedata.mpKeyEqSuggest = 0;
	savedata.mpJacketRemOverlay = 1;
	savedata.mpBpmCand[0] = savedata.mpBpmCand[1] = savedata.mpBpmCand[2] = 0;
	savedata.tc_format = 0;
	savedata.tc_mp3_kbps = 192;
	savedata.tc_flac_level = 5;
	savedata.soundguid = { 0,0,0,0 };
	savedata.soundcur=0;
	savedata.samples = 192000;
	savedata.wup = 1.0;
	savedata.aerocheck = 0;
	savedata.speanamode = 0;
	savedata.speananum = 0;
	savedata.lrc_net = 0;
	savedata.eq[0] = 100;
	savedata.eq[1] = 100;
	savedata.eq[2] = 100;
	savedata.eq[3] = 100;
	savedata.eq[4] = 100;
	savedata.eq[5] = 100;
	savedata.eq[6] = 100;
	savedata.eq[7] = 100;
	savedata.eq[8] = 100;
	savedata.eq[9] = 100;
	savedata.eq[10] = 100;
	savedata.eq[11] = 100;
	savedata.eq[12] = 100;
	savedata.eq[13] = 100;
	savedata.eq[14] = 100;
	savedata.eq[15] = 100;
	savedata.eq[16] = 100;
	savedata.eq[17] = 100;
	savedata.eq[18] = 100;
	savedata.eq[19] = 100;
	savedata.eqsoundenv = 0;
	savedata.eqsoundeq = 0;
	savedata.eqx = -1;
	savedata.eqy = -1;
	savedata.eqsoundeffect = 50;
	savedata.eqwindow = 0;
	savedata.lang = 0; // 0:日本語 1:英語 2:仏 3:伊 4:西 5:韓 6:中 7:阿 8:露 9:独 10:葡 11:蘭 12:波 13:土
	savedata.langselect = 0; // 0:選択前 1:選択後のフラグ
	savedata.upscale_enable = 1;
	savedata.speaker_layout = 0;
	savedata.lastUpdateCheck = 0;
	savedata.updateAttemptExeTime = 0;
	savedata.pianorollwindow = 0;
	savedata.pianorollx = -1;
	savedata.pianorolly = -1;
	savedata.pianorollw = 800;
	savedata.pianorollh = 450;
	savedata.analyzerwindow = 0;
	savedata.analyzerx = -1;
	savedata.analyzery = -1;
	savedata.analyzerw = 720;
	savedata.analyzerh = 420;
	savedata.analyzerspeclayout = 0;
	savedata.analyzerspecstyle = 0;
	savedata.analyzerpeakhold = 1;
	savedata.analyzereqoverlay = 1;
	savedata.analyzerwavespeed = 100;
	savedata.pianorollscrollspeed = 100;
	savedata.pianorollexprlegend = 1;
	savedata.pianorollexprmarks = 1;
	savedata.pianorolllevelmeter = 1;
	savedata.pianorolltopmost = 0;
	savedata.pianorollreattack = 0;
	savedata.pianorollimpulse = 0;
	savedata.pianorollharmghost = 1;
	savedata.pianorollharmprof = 1;
	savedata.analyzerlevelmeter = 1;
	savedata.analyzertopmost = 0;
	savedata.analyzerwavemode = 0;
	savedata.analyzerlowermode = 0;
	savedata.analyzerspecdiff = 0;
	savedata.analyzerfreqzoom = 0;
	ZeroMemory(savedata.analyzermarkers, sizeof(savedata.analyzermarkers));
	savedata.saveversion = 2;
	savedata.prTuneSilencePct = 100;
	savedata.prTuneBandSilBassPct = 100;
	savedata.prTuneBandSilMidPct = 100;
	savedata.prTuneBandSilTrePct = 100;
	savedata.prTuneHoldBassPct = 100;
	savedata.prTuneHoldMidPct = 100;
	savedata.prTuneHoldTrePct = 100;
	savedata.prTuneRetrigPct = 100;
	savedata.prTunePickBassPct = 100;
	savedata.prTunePickLowMidPct = 100;
	savedata.prTunePickMelodyPct = 100;
	savedata.prTunePickTrePct = 100;
	savedata.prTuneHarmGhostPct = 100;
	savedata.prTuneHarmRejectPct = 100;
	savedata.prTuneHarmProfPct = 100;
	savedata.prTuneAbsFloorPct = 100;
	savedata.prTuneOnsetDeltaPct = 100;
	savedata.prTunewindow = 0;
	savedata.prTunex = -1;
	savedata.prTuney = -1;
	savedata.eqMainLock = 0;
	savedata.pianorollMainLock = 0;
	savedata.analyzerMainLock = 0;
	savedata.playlistMainLock = 0;
	savedata.renderMainLock = 0;
	savedata.folderMainLock = 0;
	savedata.mpPromptMainLock = 0;
	savedata.prTuneMainLock = 0;

	savedata.saveSongParams = 0;
	savedata.audioDataVersion = 0;

	savedata.playerMode = 0;   // 既定はファルコム特化型
	savedata.startupAsk = 1;   // 既定で起動時にモード選択ダイアログを表示
	savedata.mpHasPos = 0;     // メディアプレイヤー座標は未設定
	savedata.mpx = -10000;
	savedata.mpy = -10000;
	savedata.mpw = 0;
	savedata.mph = 0;
	savedata.wav_export_fade = 0;
	savedata.wav_export_fade_sec = 15;
	savedata.wav_export_trim_lead = 0;
	savedata.wav_export_trim_keep_sec = 1;
	savedata.wav_export_copy_tags = 1;
	savedata.wav_export_kpi_sec = 240;
	savedata.wav_export_sample_rate = 48000;
	savedata.mpPromptwindow = 0;
	savedata.mpCmdRollwindow = 0;
	savedata.mpCmdRollHasPos = 0;
	savedata.mpCmdRollX = -10000;
	savedata.mpCmdRollY = -10000;
	savedata.mpCmdRollW = 900;
	savedata.mpCmdRollH = 560;
	savedata.mpCmdRollMainLock = 0;
	savedata.mpCmdRollPxPerSec10 = 120; // 12.0 px/s
	savedata.wav_export_apply_prompt = 0;
	savedata.wav_export_xfade = 0;
	savedata.wav_export_xfade_sec = 5;
	savedata.wav_export_mix = 0;
	savedata.wav_export_mix_n = 2;
	savedata.mic_mix = 0;
	savedata.mic_mix_level = 100;
	savedata.mic_device[0] = 0;
	savedata.mic_device_cur = 0;
	savedata.loop_device[0] = 0;
	savedata.loop_device_cur = 0;
	savedata.cap_save_dir[0] = 0;
	savedata.record_format = 0;
	savedata.record_mp3_kbps = 192;
	savedata.record_mix_mic = 0;
	savedata.record_last_path[0] = 0;
	savedata.record_flac_level = 5;
	savedata.cap_with_audio = 1;
	savedata.cap_with_mic = 0;
	savedata.cap_fps = 15;
	savedata.cap_last_path[0] = 0;
	savedata.cap_mode = 0;
	savedata.cap_canvas_preset = 2;
	savedata.cap_canvas_w = 1920;
	savedata.cap_canvas_h = 1080;
	savedata.cap_include_mp = 0;
	savedata.cap_monitor_idx = 0;
	savedata.cap_effect = 0;
	savedata.cap_fx_n = 0;
	savedata.cap_fx0 = 0;
	savedata.cap_fx1 = 0;
	savedata.cap_fx2 = 0;
	savedata.cap_fx3 = 0;
	savedata.cap_fx4 = 0;
	savedata.cap_fx5 = 0;
	savedata.cap_fx6 = 0;
	savedata.cap_fx7 = 0;
	memset(savedata.cap_fx_str, 4, sizeof(savedata.cap_fx_str)); // SC_FX_STR_DEF
	memset(savedata.cap_fx_pre_name, 0, sizeof(savedata.cap_fx_pre_name));
	memset(savedata.cap_fx_pre_n, 0, sizeof(savedata.cap_fx_pre_n));
	memset(savedata.cap_fx_pre_fx, 0, sizeof(savedata.cap_fx_pre_fx));
	memset(savedata.cap_fx_pre_str, 4, sizeof(savedata.cap_fx_pre_str)); // SC_FX_STR_DEF
	savedata.cap_fx_pre_sel = 0;
	savedata.popupMenuFace[0] = 0;
	savedata.popupMenuPoint = 9;
	savedata.popupMenuBold = 0;
	savedata.popupMenuItalic = 0;
	savedata.popupMenuAnim = 0;
	savedata.deskLrcFontAuto = 1;
	savedata.deskLrcFontPt = 140;
	savedata.deskLrcLines = 10;
	savedata.deskLrcWinX = 80;
	savedata.deskLrcWinY = 80;
	savedata.deskLrcWinW = 640;
	savedata.deskLrcWinH = 160;
	savedata.mpDjScratchEffect = 100;
	savedata.mpDjScratchSpeed = 100;
	savedata.mpDjEqLow = 100;
	savedata.mpDjEqMid = 100;
	savedata.mpDjEqHigh = 100;
	savedata.mpDjFilter = 100;
	savedata.mpDjEqKill = 0;
	savedata.mpDjPadMainLock = 0;
	savedata.mpDjPadTopMost = 0;
	savedata.pianorollviewmode = 0;
	savedata.pianorollkeyrange = 108;
	savedata.pianorollnotename = 1;
	savedata.pianoroll3dyaw = -220;   // -22.0 度
	savedata.pianoroll3dpitch = 260;  //  26.0 度
	savedata.pianoroll3dzoom = 100;   // 1.00x
	savedata.dougatopmost = 0;
	savedata.dougaaspect = 0;

#if _UNICODE
	if(GetKeyState(VK_CONTROL) < 0){
		if(AfxMessageBox(LL14(L"ANSI版からのコンバートを行いますか？", L"Convert from ANSI version?", L"Convertir depuis la version ANSI ?", L"Convertire dalla versione ANSI?", L"Convertir desde version ANSI?", L"ANSI 버전에서 변환하시겠습니까?", L"从ANSI版本转换吗？", L"هل تريد التحويل من إصدار ANSI؟", L"Конвертировать из версии ANSI?", L"Von ANSI-Version konvertieren?", L"Converter da versao ANSI?", L"Converteren van ANSI-versie?", L"Konwertowac z wersji ANSI?", L"ANSI surumunden donusturulsun mu?"),MB_YESNO)==IDYES){
			convert();
			AfxMessageBox(LL14(L"コンバートが完了しました。", L"Conversion completed.", L"Conversion terminee.", L"Conversione completata.", L"Conversion completada.", L"변환이 완료되었습니다.", L"转换完成。", L"اكتمل التحويل.", L"Конвертация завершена.", L"Konvertierung abgeschlossen.", L"Conversao concluida.", L"Conversie voltooid.", L"Konwersja zakonczona.", L"Donusum tamamlandi."));
			ReleaseMutex(Mutex);
			exit(0);
		}
	}
	CFile ab,ac;
	// .dat が eq_reverb 以降(メディアプレイヤーモード版=36bab3c以降)のフィールドを
	// 含んでいたか。含む.datは ms2 を一度だけ誤って ×16 した不具合版で保存されたもの。
	bool datHadMpFields = false;
	int datFileSize = 0;
	if(ab.Open(L"oggYSEDbgmu.dat",CFile::modeRead | CFile::shareDenyWrite,NULL)!=TRUE && ac.Open(L"oggYSEDbgm.dat",CFile::modeRead | CFile::shareDenyWrite,NULL)==TRUE){
		ac.Close();
		AfxMessageBox(LL14(L"ANSI版からのコンバートを行います。", L"Converting from ANSI version.", L"Conversion depuis la version ANSI en cours.", L"Conversione dalla versione ANSI in corso.", L"Convirtiendo desde version ANSI.", L"ANSI 버전에서 변환합니다.", L"正在从ANSI版本转换。", L"جاري التحويل من إصدار ANSI.", L"Конвертация из версии ANSI.", L"Konvertierung von ANSI-Version.", L"Convertendo da versao ANSI.", L"Converteren van ANSI-versie.", L"Konwertowanie z wersji ANSI.", L"ANSI surumunden donusturuluyor."));
		convert();
		AfxMessageBox(LL14(L"コンバートが完了しました。", L"Conversion completed.", L"Conversion terminee.", L"Conversione completata.", L"Conversion completada.", L"변환이 완료되었습니다.", L"转换完成。", L"اكتمل التحويل.", L"Конвертация завершена.", L"Konvertierung abgeschlossen.", L"Conversao concluida.", L"Conversie voltooid.", L"Konwersja zakonczona.", L"Donusum tamamlandi."));
		ReleaseMutex(Mutex);
		exit(0);
#else
	CFile ab;
	bool datHadMpFields = false;
	int datFileSize = 0;
	if(ab.Open("oggYSEDbgm.dat",CFile::modeRead | CFile::shareDenyWrite,NULL)!=TRUE){
#endif
	}else{
		if(ab.m_hFile != CFile::hFileNull){
			const int a = (int)ab.GetLength();
			datFileSize = a;
			const int toRead = (a < (int)sizeof(save)) ? a : (int)sizeof(save);
			ab.Read(&savedata, toRead);
			ab.Close();
			// 末尾にフィールドを追加すると旧.datは sizeof(save) より小さくなるが、
			// それだけで「旧形式」と決めつけると、saveversion を既に持つ中間
			// バージョンの.datまで version 0 とみなして ms2 を再変換(×16)し、
			// 16ms→256ms のように値が壊れてしまう。
			// saveversion フィールドまで実際に読めていればその値を信頼し、
			// それより前(=saveversion 導入前)の.datだけを version 0 とみなす。
			if (a < (int)(offsetof(save, saveversion) + sizeof(savedata.saveversion)))
				savedata.saveversion = 0;
			// eq_reverb 以降のフィールドまで読めていれば、その.datは
			// メディアプレイヤーモード版(=ms2 を一度だけ誤って ×16 した版)で
			// 保存されたもの。後段の version 1→2 修復で ÷16 を行う判定に使う。
			datHadMpFields = (a >= (int)(offsetof(save, eq_reverb) + sizeof(savedata.eq_reverb)));
		}
	}
	if (savedata.speaker_layout < 0 || savedata.speaker_layout > 5)
		savedata.speaker_layout = 0;
	if (savedata.wav_export_fade_sec <= 0)
		savedata.wav_export_fade_sec = 15;
	if (savedata.wav_export_trim_keep_sec <= 0)
		savedata.wav_export_trim_keep_sec = 1;
	// アナライザー窓: 必ず構造体末尾に追記(旧.datは部分読込で未設定のまま→既定値)
	if (datFileSize < (int)(offsetof(save, analyzerwindow) + sizeof(savedata.analyzerwindow))) {
		savedata.analyzerwindow = 0;
		savedata.analyzerx = -1;
		savedata.analyzery = -1;
		savedata.analyzerw = 720;
		savedata.analyzerh = 420;
		savedata.analyzerspeclayout = 0;
		savedata.analyzerspecstyle = 0;
		savedata.analyzerpeakhold = 1;
		savedata.analyzereqoverlay = 1;
	} else if (savedata.analyzerw < 200 || savedata.analyzerh < 120
		|| savedata.analyzerw > 10000 || savedata.analyzerh > 10000) {
		savedata.analyzerx = -1;
		savedata.analyzery = -1;
		savedata.analyzerw = 720;
		savedata.analyzerh = 420;
	}
	if (datFileSize < (int)(offsetof(save, analyzerspeclayout) + sizeof(savedata.analyzerspeclayout))
		|| savedata.analyzerspeclayout < 0 || savedata.analyzerspeclayout > 4)
		savedata.analyzerspeclayout = 0;
	if (datFileSize < (int)(offsetof(save, analyzerspecstyle) + sizeof(savedata.analyzerspecstyle))
		|| savedata.analyzerspecstyle < 0 || savedata.analyzerspecstyle > 6)
		savedata.analyzerspecstyle = 0;
	if (datFileSize < (int)(offsetof(save, analyzerpeakhold) + sizeof(savedata.analyzerpeakhold)))
		savedata.analyzerpeakhold = 1;
	else if (savedata.analyzerpeakhold != 0)
		savedata.analyzerpeakhold = 1;
	if (datFileSize < (int)(offsetof(save, analyzereqoverlay) + sizeof(savedata.analyzereqoverlay)))
		savedata.analyzereqoverlay = 1;
	else if (savedata.analyzereqoverlay != 0)
		savedata.analyzereqoverlay = 1;
	if (datFileSize < (int)(offsetof(save, analyzerwavespeed) + sizeof(savedata.analyzerwavespeed))
		|| savedata.analyzerwavespeed < 25 || savedata.analyzerwavespeed > 200)
		savedata.analyzerwavespeed = 100;
	if (datFileSize < (int)(offsetof(save, pianorollscrollspeed) + sizeof(savedata.pianorollscrollspeed))
		|| savedata.pianorollscrollspeed < 25 || savedata.pianorollscrollspeed > 200)
		savedata.pianorollscrollspeed = 100;
	if (datFileSize < (int)(offsetof(save, pianorollexprlegend) + sizeof(savedata.pianorollexprlegend)))
		savedata.pianorollexprlegend = 1;
	else if (savedata.pianorollexprlegend != 0)
		savedata.pianorollexprlegend = 1;
	if (datFileSize < (int)(offsetof(save, pianorollexprmarks) + sizeof(savedata.pianorollexprmarks)))
		savedata.pianorollexprmarks = 1;
	else if (savedata.pianorollexprmarks != 0)
		savedata.pianorollexprmarks = 1;
	if (datFileSize < (int)(offsetof(save, pianorolllevelmeter) + sizeof(savedata.pianorolllevelmeter)))
		savedata.pianorolllevelmeter = 1;
	else if (savedata.pianorolllevelmeter != 0)
		savedata.pianorolllevelmeter = 1;
	if (datFileSize < (int)(offsetof(save, pianorolltopmost) + sizeof(savedata.pianorolltopmost)))
		savedata.pianorolltopmost = 0;
	else if (savedata.pianorolltopmost != 0)
		savedata.pianorolltopmost = 1;
	if (datFileSize < (int)(offsetof(save, pianorollreattack) + sizeof(savedata.pianorollreattack)))
		savedata.pianorollreattack = 0;
	else if (savedata.pianorollreattack != 0)
		savedata.pianorollreattack = 1;
	if (datFileSize < (int)(offsetof(save, pianorollimpulse) + sizeof(savedata.pianorollimpulse)))
		savedata.pianorollimpulse = 0;
	else if (savedata.pianorollimpulse != 0)
		savedata.pianorollimpulse = 1;
	if (datFileSize < (int)(offsetof(save, pianorollharmghost) + sizeof(savedata.pianorollharmghost)))
		savedata.pianorollharmghost = 1;
	else if (savedata.pianorollharmghost != 0)
		savedata.pianorollharmghost = 1;
	if (datFileSize < (int)(offsetof(save, pianorollharmprof) + sizeof(savedata.pianorollharmprof)))
		savedata.pianorollharmprof = 1;
	else if (savedata.pianorollharmprof != 0)
		savedata.pianorollharmprof = 1;
	if (datFileSize < (int)(offsetof(save, analyzerlevelmeter) + sizeof(savedata.analyzerlevelmeter)))
		savedata.analyzerlevelmeter = 1;
	else if (savedata.analyzerlevelmeter != 0)
		savedata.analyzerlevelmeter = 1;
	if (datFileSize < (int)(offsetof(save, analyzertopmost) + sizeof(savedata.analyzertopmost)))
		savedata.analyzertopmost = 0;
	else if (savedata.analyzertopmost != 0)
		savedata.analyzertopmost = 1;
	// プロンプト窓 位置・サイズ(末尾追記)
	if (datFileSize < (int)(offsetof(save, mpPromptHasPos) + sizeof(savedata.mpPromptHasPos))) {
		savedata.mpPromptHasPos = 0;
		savedata.mpPromptX = -10000;
		savedata.mpPromptY = -10000;
		savedata.mpPromptW = 375;
		savedata.mpPromptH = 330;
	}
	else if (savedata.mpPromptHasPos) {
		if (savedata.mpPromptW < 280 || savedata.mpPromptH < 240
			|| savedata.mpPromptW > 10000 || savedata.mpPromptH > 10000) {
			savedata.mpPromptHasPos = 0;
			savedata.mpPromptX = -10000;
			savedata.mpPromptY = -10000;
			savedata.mpPromptW = 375;
			savedata.mpPromptH = 330;
		}
	}
	// プロンプト履歴(末尾追記)
	if (datFileSize < (int)(offsetof(save, mpPromptHistCnt) + sizeof(savedata.mpPromptHistCnt))) {
		savedata.mpPromptHistCnt = 0;
		ZeroMemory(savedata.mpPromptHistText, sizeof(savedata.mpPromptHistText));
	}
	else if (savedata.mpPromptHistCnt < 0 || savedata.mpPromptHistCnt > 20) {
		savedata.mpPromptHistCnt = 0;
		ZeroMemory(savedata.mpPromptHistText, sizeof(savedata.mpPromptHistText));
	}
	// ピアノロール検出パラメータ(末尾追記)
	if (datFileSize < (int)(offsetof(save, prTuneSilencePct) + sizeof(savedata.prTuneSilencePct))) {
		savedata.prTuneSilencePct = 100;
		savedata.prTuneBandSilBassPct = 100;
		savedata.prTuneBandSilMidPct = 100;
		savedata.prTuneBandSilTrePct = 100;
		savedata.prTuneHoldBassPct = 100;
		savedata.prTuneHoldMidPct = 100;
		savedata.prTuneHoldTrePct = 100;
		savedata.prTuneRetrigPct = 100;
		savedata.prTunePickBassPct = 100;
		savedata.prTunePickLowMidPct = 100;
		savedata.prTunePickMelodyPct = 100;
		savedata.prTunePickTrePct = 100;
		savedata.prTuneHarmGhostPct = 100;
		savedata.prTuneHarmRejectPct = 100;
		savedata.prTuneHarmProfPct = 100;
		savedata.prTuneAbsFloorPct = 100;
		savedata.prTuneOnsetDeltaPct = 100;
	}
	else {
		int* tuneFields[] = {
			&savedata.prTuneSilencePct, &savedata.prTuneBandSilBassPct, &savedata.prTuneBandSilMidPct,
			&savedata.prTuneBandSilTrePct, &savedata.prTuneHoldBassPct, &savedata.prTuneHoldMidPct,
			&savedata.prTuneHoldTrePct, &savedata.prTuneRetrigPct, &savedata.prTunePickBassPct,
			&savedata.prTunePickLowMidPct, &savedata.prTunePickMelodyPct, &savedata.prTunePickTrePct,
			&savedata.prTuneHarmGhostPct, &savedata.prTuneHarmRejectPct, &savedata.prTuneHarmProfPct,
			&savedata.prTuneAbsFloorPct, &savedata.prTuneOnsetDeltaPct,
		};
		for (int* p : tuneFields) {
			if (*p < 25 || *p > 400) *p = 100;
		}
	}
	if (datFileSize < (int)(offsetof(save, prTunewindow) + sizeof(savedata.prTunewindow)))
		savedata.prTunewindow = 0;
	else if (savedata.prTunewindow != 0)
		savedata.prTunewindow = 1;
	if (datFileSize < (int)(offsetof(save, prTunex) + sizeof(savedata.prTunex))) {
		savedata.prTunex = -1;
		savedata.prTuney = -1;
	}
	if (datFileSize < (int)(offsetof(save, eqMainLock) + sizeof(savedata.eqMainLock))) {
		savedata.eqMainLock = 0;
		savedata.pianorollMainLock = 0;
		savedata.analyzerMainLock = 0;
		savedata.playlistMainLock = 0;
		savedata.renderMainLock = 0;
		savedata.folderMainLock = 0;
		savedata.mpPromptMainLock = 0;
		savedata.prTuneMainLock = 0;
	}
	else {
		int* lockFields[] = {
			&savedata.eqMainLock, &savedata.pianorollMainLock, &savedata.analyzerMainLock,
			&savedata.playlistMainLock, &savedata.renderMainLock, &savedata.folderMainLock,
			&savedata.mpPromptMainLock,
		};
		for (int* p : lockFields) {
			if (*p != 0) *p = 1;
		}
	}
	if (datFileSize < (int)(offsetof(save, prTuneMainLock) + sizeof(savedata.prTuneMainLock))) {
		savedata.prTuneMainLock = 0;
	}
	else if (savedata.prTuneMainLock != 0) {
		savedata.prTuneMainLock = 1;
	}
	// 曲ごとパラメータ保存フラグ(末尾追記): 旧 .dat には無いので既定 0(無効)
	if (datFileSize < (int)(offsetof(save, saveSongParams) + sizeof(savedata.saveSongParams))) {
		savedata.saveSongParams = 0;
	}
	// AudioData.dat キー移行フラグ(末尾追記): 旧 .dat には無いので 0=未移行
	if (datFileSize < (int)(offsetof(save, audioDataVersion) + sizeof(savedata.audioDataVersion))) {
		savedata.audioDataVersion = 0;
	}
	// ジャンプリスト履歴: 途中フィールド挿入で .dat がずれた場合などは破棄する
	{
		auto clearMpHist = []() {
			savedata.mpHistCnt = 0;
			ZeroMemory(savedata.mpHistName, sizeof(savedata.mpHistName));
			ZeroMemory(savedata.mpHistPath, sizeof(savedata.mpHistPath));
		};
		const bool histTooShort = (datFileSize < (int)offsetof(save, mpHistCnt));
		if (histTooShort || savedata.mpHistCnt < 0 || savedata.mpHistCnt > 8) {
			clearMpHist();
		}
		else {
			bool histBad = false;
			for (int i = 0; i < savedata.mpHistCnt; ++i) {
				savedata.mpHistPath[i][_countof(savedata.mpHistPath[i]) - 1] = 0;
				savedata.mpHistName[i][_countof(savedata.mpHistName[i]) - 1] = 0;
				const TCHAR* p = savedata.mpHistPath[i];
				if (p[0] == 0) { histBad = true; break; }
				const bool absDrive = (p[0] != 0 && p[1] == _T(':')
					&& (p[2] == _T('\\') || p[2] == _T('/')));
				const bool absUnc = (p[0] == _T('\\') && p[1] == _T('\\') && p[2] != 0);
				if (!absDrive && !absUnc) { histBad = true; break; }
				for (const TCHAR* c = p; *c; ++c) {
					if ((unsigned short)*c < 0x20) { histBad = true; break; }
				}
				if (histBad) break;
				for (const TCHAR* c = savedata.mpHistName[i]; *c; ++c) {
					if ((unsigned short)*c < 0x20) { histBad = true; break; }
				}
				if (histBad) break;
			}
			if (histBad)
				clearMpHist();
		}
	}
	// MP UI フラグ(末尾追記): 旧.dat は未所持
	if (datFileSize < (int)(offsetof(save, mpLrcExpand) + sizeof(savedata.mpLrcExpand)))
		savedata.mpLrcExpand = 0;
	else if (savedata.mpLrcExpand) savedata.mpLrcExpand = 1;
	if (datFileSize < (int)(offsetof(save, mpFindFilter) + sizeof(savedata.mpFindFilter)))
		savedata.mpFindFilter = 0;
	else if (savedata.mpFindFilter) savedata.mpFindFilter = 1;
	if (datFileSize < (int)(offsetof(save, mpToolsOpen) + sizeof(savedata.mpToolsOpen)))
		savedata.mpToolsOpen = 0;
	else if (savedata.mpToolsOpen) savedata.mpToolsOpen = 1;
	if (datFileSize < (int)(offsetof(save, mpSortKey) + sizeof(savedata.mpSortKey)))
		savedata.mpSortKey = 0;
	else if (savedata.mpSortKey < 0 || savedata.mpSortKey > 4)
		savedata.mpSortKey = 0;
	if (datFileSize < (int)(offsetof(save, mpSortAsc) + sizeof(savedata.mpSortAsc)))
		savedata.mpSortAsc = 1;
	else if (savedata.mpSortAsc) savedata.mpSortAsc = 1;
	else savedata.mpSortAsc = 0;
	if (datFileSize < (int)(offsetof(save, mpLibOpen) + sizeof(savedata.mpLibOpen)))
		savedata.mpLibOpen = 0;
	else if (savedata.mpLibOpen) savedata.mpLibOpen = 1;
	if (datFileSize < (int)(offsetof(save, mpHistOpen) + sizeof(savedata.mpHistOpen)))
		savedata.mpHistOpen = 0;
	else if (savedata.mpHistOpen) savedata.mpHistOpen = 1;
	if (datFileSize < (int)(offsetof(save, mpSpeanaStyle) + sizeof(savedata.mpSpeanaStyle)))
		savedata.mpSpeanaStyle = 0;
	else if (savedata.mpSpeanaStyle < 0 || savedata.mpSpeanaStyle > 2)
		savedata.mpSpeanaStyle = 0;
	if (datFileSize < (int)(offsetof(save, analyzerwavemode) + sizeof(savedata.analyzerwavemode)))
		savedata.analyzerwavemode = 0;
	else if (savedata.analyzerwavemode != 0)
		savedata.analyzerwavemode = 1;
	if (datFileSize < (int)(offsetof(save, analyzerlowermode) + sizeof(savedata.analyzerlowermode)))
		savedata.analyzerlowermode = 0;
	else if (savedata.analyzerlowermode < 0 || savedata.analyzerlowermode > 2)
		savedata.analyzerlowermode = 0;
	if (datFileSize < (int)(offsetof(save, analyzerspecdiff) + sizeof(savedata.analyzerspecdiff)))
		savedata.analyzerspecdiff = 0;
	else if (savedata.analyzerspecdiff != 0)
		savedata.analyzerspecdiff = 1;
	if (datFileSize < (int)(offsetof(save, analyzerfreqzoom) + sizeof(savedata.analyzerfreqzoom)))
		savedata.analyzerfreqzoom = 0;
	else if (savedata.analyzerfreqzoom < 0 || savedata.analyzerfreqzoom > 3)
		savedata.analyzerfreqzoom = 0;
	if (datFileSize < (int)(offsetof(save, analyzermarkers) + sizeof(savedata.analyzermarkers))) {
		ZeroMemory(savedata.analyzermarkers, sizeof(savedata.analyzermarkers));
	}
	else {
		for (int mi = 0; mi < 4; ++mi) {
			if (savedata.analyzermarkers[mi] < 0 || savedata.analyzermarkers[mi] > 96000)
				savedata.analyzermarkers[mi] = 0;
		}
	}
	if (datFileSize < (int)(offsetof(save, mpTempOpen) + sizeof(savedata.mpTempOpen)))
		savedata.mpTempOpen = 0;
	else if (savedata.mpTempOpen) savedata.mpTempOpen = 1;
	// 左ドロワー(Lib/Hist/Temp)は排他
	if (savedata.mpTempOpen) {
		savedata.mpLibOpen = 0;
		savedata.mpHistOpen = 0;
	}
	else if (savedata.mpLibOpen && savedata.mpHistOpen)
		savedata.mpHistOpen = 0;
	// MP窓座標もずれ破損しやすいので、明らかに不正なら未設定扱いにする
	if (savedata.mpHasPos) {
		if (savedata.mpw < 100 || savedata.mph < 100
			|| savedata.mpw > 10000 || savedata.mph > 10000) {
			savedata.mpHasPos = 0;
			savedata.mpx = -10000;
			savedata.mpy = -10000;
			savedata.mpw = 0;
			savedata.mph = 0;
		}
	}
	// 列幅・フォント名もずれで壊れやすい。異常値は捨てる
	{
		bool colBad = false;
		for (int i = 0; i < 5; ++i) {
			if (savedata.mpcol[i] < 0 || savedata.mpcol[i] > 4000) {
				colBad = true;
				break;
			}
		}
		if (colBad)
			ZeroMemory(savedata.mpcol, sizeof(savedata.mpcol));
		savedata.font1[_countof(savedata.font1) - 1] = 0;
		savedata.font2[_countof(savedata.font2) - 1] = 0;
		for (TCHAR* p = savedata.font1; *p; ++p) {
			if ((unsigned short)*p < 0x20) { savedata.font1[0] = 0; break; }
		}
		for (TCHAR* p = savedata.font2; *p; ++p) {
			if ((unsigned short)*p < 0x20) { savedata.font2[0] = 0; break; }
		}
		if (savedata.lang < 0 || savedata.lang > 13)
			savedata.lang = 0;
	}
	if (savedata.langselect == 0) {
		LANGID langId = GetUserDefaultUILanguage();
		WORD prim = PRIMARYLANGID(langId);
		// 0=ja, 1=en, 2=fr, 3=it, 4=es, 5=ko, 6=zh, 7=ar, 8=ru, 9=de, 10=pt, 11=nl, 12=pl, 13=tr
		switch (prim) {
		case LANG_JAPANESE:  savedata.lang = 0; break;
		case LANG_ENGLISH:   savedata.lang = 1; break;
		case LANG_FRENCH:    savedata.lang = 2; break;
		case LANG_ITALIAN:   savedata.lang = 3; break;
		case LANG_SPANISH:   savedata.lang = 4; break;
		case LANG_KOREAN:    savedata.lang = 5; break;
		case LANG_CHINESE:   savedata.lang = 6; break;
		case LANG_ARABIC:    savedata.lang = 7; break;
		case LANG_RUSSIAN:   savedata.lang = 8; break;
		case LANG_GERMAN:    savedata.lang = 9; break;
		case LANG_PORTUGUESE:savedata.lang = 10; break;
		case LANG_DUTCH:     savedata.lang = 11; break;
		case LANG_POLISH:    savedata.lang = 12; break;
		case LANG_TURKISH:   savedata.lang = 13; break;
		default:             savedata.lang = 1; break;  // fallback English
		}
		savedata.langselect = 1;
	}
#if _UNICODE
	if (ac.m_hFile != CFile::hFileNull)
		ac.Close();
#endif
	if (savedata.ms < 30) savedata.ms = 30;
	if (savedata.ms > 80) savedata.ms = 80;
	if (savedata.aero == 2)
		savedata.aero = 0;
	if (savedata.aero != 0)
		savedata.aero = 1;
	// saveversion 0: ms2 はスライダー位置(1..60)。1以降: ms2 は描画間隔ms(16..960, 16の倍数)。
	if (savedata.saveversion < 1) {
		if (savedata.ms2 < 1) savedata.ms2 = 1;
		if (savedata.ms2 > 60) savedata.ms2 = 60;
		savedata.ms2 *= 16;
		savedata.saveversion = 1;
	}
	// saveversion 1→2: メディアプレイヤーモードでフィールドが増えた版(36bab3c以降)は、
	// 部分読込判定で saveversion を誤って 0 に戻し、ms2 を一度だけ ×16 してしまう不具合が
	// あった(例: 16ms→256ms)。設定画面を知らないユーザーは直せないため、ここで自動修復する。
	// eq_reverb 以降を持つ.dat(=その不具合版で保存された.dat)で、まだ version 2 に
	// なっていないものは、その誤った ×16 を ÷16 で取り消す。
	// (中間版[saveversion導入〜MPモード前]の.datは eq_reverb を持たないため対象外=値はそのまま)
	if (savedata.saveversion < 2) {
		if (datHadMpFields)
			savedata.ms2 /= 16;
		savedata.saveversion = 2;
	}
	if (savedata.ms2 < 16) savedata.ms2 = 16;
	if (savedata.ms2 > 960) savedata.ms2 = 960;
	if (savedata.eqCodeMs < 16 || savedata.eqCodeMs > 500)
		savedata.eqCodeMs = 25;
	// ProAudio: 旧.dat は 0 埋め。初回だけ妥当な既定へ。
	savedata.pro_xfade_ms = 0; /* クロスフェード撤去（旧設定を無視） */
	if (savedata.pro_rg_mode < 0 || savedata.pro_rg_mode > 2)
		savedata.pro_rg_mode = 0;
	if (savedata.pro_rg_target > -1 || savedata.pro_rg_target < -30)
		savedata.pro_rg_target = -18;
	// 旧.dat の 0 埋めは「未設定」扱い（幅0でモノ化する事故を防ぐ）
	if (savedata.pro_ms_width < 0 || savedata.pro_ms_width > 200)
		savedata.pro_ms_width = 100;
	if (savedata.pro_export_ceiling < 50 || savedata.pro_export_ceiling > 100)
		savedata.pro_export_ceiling = 99;
	if (savedata.tc_format != 0 && savedata.tc_format != 1)
		savedata.tc_format = 0;
	if (savedata.tc_mp3_kbps < 64 || savedata.tc_mp3_kbps > 320)
		savedata.tc_mp3_kbps = 192;
	if (savedata.tc_flac_level < 0 || savedata.tc_flac_level > 8)
		savedata.tc_flac_level = 5;
	// 旧.dat は 0 埋めのため、フィールドが無ければ既定の「コピーする」にする
	if (datFileSize < (int)(offsetof(save, wav_export_copy_tags) + sizeof(savedata.wav_export_copy_tags)))
		savedata.wav_export_copy_tags = 1;
	else if (savedata.wav_export_copy_tags != 0)
		savedata.wav_export_copy_tags = 1;
	if (datFileSize < (int)(offsetof(save, wav_export_kpi_sec) + sizeof(savedata.wav_export_kpi_sec))
		|| savedata.wav_export_kpi_sec < 1 || savedata.wav_export_kpi_sec > 36000)
		savedata.wav_export_kpi_sec = 240;
	if (datFileSize < (int)(offsetof(save, wav_export_sample_rate) + sizeof(savedata.wav_export_sample_rate)))
		savedata.wav_export_sample_rate = 48000;
	else {
		const int sr = savedata.wav_export_sample_rate;
		if (!(sr == 0 || sr == 44100 || sr == 48000 || sr == 96000 || sr == 192000))
			savedata.wav_export_sample_rate = 48000;
	}
	// プロンプト/コマンドロール開閉・座標(末尾追記)
	if (datFileSize < (int)(offsetof(save, mpPromptwindow) + sizeof(savedata.mpPromptwindow)))
		savedata.mpPromptwindow = 0;
	else if (savedata.mpPromptwindow != 0)
		savedata.mpPromptwindow = 1;
	if (datFileSize < (int)(offsetof(save, mpCmdRollwindow) + sizeof(savedata.mpCmdRollwindow)))
		savedata.mpCmdRollwindow = 0;
	else if (savedata.mpCmdRollwindow != 0)
		savedata.mpCmdRollwindow = 1;
	if (datFileSize < (int)(offsetof(save, mpCmdRollHasPos) + sizeof(savedata.mpCmdRollHasPos))) {
		savedata.mpCmdRollHasPos = 0;
		savedata.mpCmdRollX = -10000;
		savedata.mpCmdRollY = -10000;
		savedata.mpCmdRollW = 900;
		savedata.mpCmdRollH = 560;
	}
	else if (savedata.mpCmdRollHasPos) {
		if (savedata.mpCmdRollW < 400 || savedata.mpCmdRollH < 280
			|| savedata.mpCmdRollW > 10000 || savedata.mpCmdRollH > 10000) {
			savedata.mpCmdRollHasPos = 0;
			savedata.mpCmdRollX = -10000;
			savedata.mpCmdRollY = -10000;
			savedata.mpCmdRollW = 900;
			savedata.mpCmdRollH = 560;
		}
	}
	if (datFileSize < (int)(offsetof(save, mpCmdRollMainLock) + sizeof(savedata.mpCmdRollMainLock)))
		savedata.mpCmdRollMainLock = 0;
	else if (savedata.mpCmdRollMainLock != 0)
		savedata.mpCmdRollMainLock = 1;
	if (datFileSize < (int)(offsetof(save, mpCmdRollPxPerSec10) + sizeof(savedata.mpCmdRollPxPerSec10))
		|| savedata.mpCmdRollPxPerSec10 < 15 || savedata.mpCmdRollPxPerSec10 > 4800)
		savedata.mpCmdRollPxPerSec10 = 120;
	if (datFileSize < (int)(offsetof(save, wav_export_apply_prompt) + sizeof(savedata.wav_export_apply_prompt)))
		savedata.wav_export_apply_prompt = 0;
	else if (savedata.wav_export_apply_prompt != 0)
		savedata.wav_export_apply_prompt = 1;
	if (datFileSize < (int)(offsetof(save, wav_export_xfade) + sizeof(savedata.wav_export_xfade)))
		savedata.wav_export_xfade = 0;
	else if (savedata.wav_export_xfade != 0)
		savedata.wav_export_xfade = 1;
	if (datFileSize < (int)(offsetof(save, wav_export_xfade_sec) + sizeof(savedata.wav_export_xfade_sec))
		|| savedata.wav_export_xfade_sec < 1 || savedata.wav_export_xfade_sec > 120)
		savedata.wav_export_xfade_sec = 5;
	if (datFileSize < (int)(offsetof(save, wav_export_mix) + sizeof(savedata.wav_export_mix)))
		savedata.wav_export_mix = 0;
	else if (savedata.wav_export_mix != 0)
		savedata.wav_export_mix = 1;
	if (datFileSize < (int)(offsetof(save, wav_export_mix_n) + sizeof(savedata.wav_export_mix_n))
		|| savedata.wav_export_mix_n < 2 || savedata.wav_export_mix_n > 64)
		savedata.wav_export_mix_n = 2;
	if (datFileSize < (int)(offsetof(save, mic_mix) + sizeof(savedata.mic_mix)))
		savedata.mic_mix = 0;
	else if (savedata.mic_mix != 0)
		savedata.mic_mix = 1;
	if (datFileSize < (int)(offsetof(save, mic_mix_level) + sizeof(savedata.mic_mix_level))
		|| savedata.mic_mix_level < 0 || savedata.mic_mix_level > 200)
		savedata.mic_mix_level = 100;
	if (datFileSize < (int)(offsetof(save, mic_device) + sizeof(savedata.mic_device)))
		savedata.mic_device[0] = 0;
	savedata.mic_device[_countof(savedata.mic_device) - 1] = 0;
	if (datFileSize < (int)(offsetof(save, mic_device_cur) + sizeof(savedata.mic_device_cur))
		|| savedata.mic_device_cur < 0)
		savedata.mic_device_cur = 0;
	if (datFileSize < (int)(offsetof(save, loop_device) + sizeof(savedata.loop_device)))
		savedata.loop_device[0] = 0;
	savedata.loop_device[_countof(savedata.loop_device) - 1] = 0;
	if (datFileSize < (int)(offsetof(save, loop_device_cur) + sizeof(savedata.loop_device_cur))
		|| savedata.loop_device_cur < 0)
		savedata.loop_device_cur = 0;
	if (datFileSize < (int)(offsetof(save, cap_save_dir) + sizeof(savedata.cap_save_dir)))
		savedata.cap_save_dir[0] = 0;
	savedata.cap_save_dir[_countof(savedata.cap_save_dir) - 1] = 0;
	if (datFileSize < (int)(offsetof(save, record_format) + sizeof(savedata.record_format))
		|| savedata.record_format < 0 || savedata.record_format > 2)
		savedata.record_format = 0;
	if (datFileSize < (int)(offsetof(save, record_mp3_kbps) + sizeof(savedata.record_mp3_kbps))
		|| savedata.record_mp3_kbps < 64 || savedata.record_mp3_kbps > 320)
		savedata.record_mp3_kbps = 192;
	if (datFileSize < (int)(offsetof(save, record_mix_mic) + sizeof(savedata.record_mix_mic)))
		savedata.record_mix_mic = 0;
	else if (savedata.record_mix_mic != 0)
		savedata.record_mix_mic = 1;
	if (datFileSize < (int)(offsetof(save, record_last_path) + sizeof(savedata.record_last_path)))
		savedata.record_last_path[0] = 0;
	savedata.record_last_path[_countof(savedata.record_last_path) - 1] = 0;
	if (datFileSize < (int)(offsetof(save, record_flac_level) + sizeof(savedata.record_flac_level))
		|| savedata.record_flac_level < 0 || savedata.record_flac_level > 8)
		savedata.record_flac_level = 5;
	if (datFileSize < (int)(offsetof(save, cap_with_audio) + sizeof(savedata.cap_with_audio)))
		savedata.cap_with_audio = 1;
	else if (savedata.cap_with_audio != 0)
		savedata.cap_with_audio = 1;
	if (datFileSize < (int)(offsetof(save, cap_with_mic) + sizeof(savedata.cap_with_mic)))
		savedata.cap_with_mic = 0;
	else if (savedata.cap_with_mic != 0)
		savedata.cap_with_mic = 1;
	if (datFileSize < (int)(offsetof(save, cap_fps) + sizeof(savedata.cap_fps))
		|| savedata.cap_fps < 5 || savedata.cap_fps > 120)
		savedata.cap_fps = 15;
	if (datFileSize < (int)(offsetof(save, cap_last_path) + sizeof(savedata.cap_last_path)))
		savedata.cap_last_path[0] = 0;
	savedata.cap_last_path[_countof(savedata.cap_last_path) - 1] = 0;
	// 簡易ピアノロール 表示拡張(末尾追記): 旧.dat には無いので既定値へ
	if (datFileSize < (int)(offsetof(save, pianorollviewmode) + sizeof(savedata.pianorollviewmode)))
		savedata.pianorollviewmode = 0;
	else if (savedata.pianorollviewmode != 1)
		savedata.pianorollviewmode = 0;
	if (datFileSize < (int)(offsetof(save, pianorollkeyrange) + sizeof(savedata.pianorollkeyrange)))
		savedata.pianorollkeyrange = 108;
	else if (savedata.pianorollkeyrange != 88)
		savedata.pianorollkeyrange = 108;
	if (datFileSize < (int)(offsetof(save, pianorollnotename) + sizeof(savedata.pianorollnotename)))
		savedata.pianorollnotename = 1;
	else if (savedata.pianorollnotename != 0)
		savedata.pianorollnotename = 1;
	if (datFileSize < (int)(offsetof(save, pianoroll3dyaw) + sizeof(savedata.pianoroll3dyaw))
		|| savedata.pianoroll3dyaw < -1800 || savedata.pianoroll3dyaw > 1800)
		savedata.pianoroll3dyaw = -220;
	if (datFileSize < (int)(offsetof(save, pianoroll3dpitch) + sizeof(savedata.pianoroll3dpitch))
		|| savedata.pianoroll3dpitch < -850 || savedata.pianoroll3dpitch > 850)
		savedata.pianoroll3dpitch = 260;
	if (datFileSize < (int)(offsetof(save, dougatopmost) + sizeof(savedata.dougatopmost)))
		savedata.dougatopmost = 0;
	else if (savedata.dougatopmost != 0)
		savedata.dougatopmost = 1;
	if (datFileSize < (int)(offsetof(save, dougaaspect) + sizeof(savedata.dougaaspect)))
		savedata.dougaaspect = 0;
	else if (savedata.dougaaspect != 0)
		savedata.dougaaspect = 1;
	if (datFileSize < (int)(offsetof(save, pianoroll3dzoom) + sizeof(savedata.pianoroll3dzoom))
		|| savedata.pianoroll3dzoom < 35 || savedata.pianoroll3dzoom > 400)
		savedata.pianoroll3dzoom = 100;
	if (datFileSize < (int)(offsetof(save, mpPromptAnalyzeMode) + sizeof(savedata.mpPromptAnalyzeMode)))
		savedata.mpPromptAnalyzeMode = 0;
	else if (savedata.mpPromptAnalyzeMode < 0 || savedata.mpPromptAnalyzeMode > 9)
		savedata.mpPromptAnalyzeMode = 0;
	// 拡張プロンプト本文: 旧.dat では未領域→短文バッファをコピー
	if (datFileSize < (int)(offsetof(save, mpPromptTextLong) + sizeof(TCHAR))) {
		savedata.mpPromptTextLong[0] = 0;
		if (savedata.mpPromptText[0])
			_tcsncpy(savedata.mpPromptTextLong, savedata.mpPromptText, _countof(savedata.mpPromptTextLong) - 1);
	}
	else if (!savedata.mpPromptTextLong[0] && savedata.mpPromptText[0]) {
		_tcsncpy(savedata.mpPromptTextLong, savedata.mpPromptText, _countof(savedata.mpPromptTextLong) - 1);
	}
	savedata.mpPromptTextLong[_countof(savedata.mpPromptTextLong) - 1] = 0;
	if (datFileSize < (int)(offsetof(save, mpPromptBackupEqSoundEq) + sizeof(savedata.mpPromptBackupEqSoundEq)))
		savedata.mpPromptBackupEqSoundEq = 0;
	if (datFileSize < (int)(offsetof(save, kliteAskSkip) + sizeof(savedata.kliteAskSkip)))
		savedata.kliteAskSkip = 0;
	else if (savedata.kliteAskSkip != 0)
		savedata.kliteAskSkip = 1;
	if (datFileSize < (int)(offsetof(save, cap_mode) + sizeof(savedata.cap_mode))
		|| savedata.cap_mode < 0 || savedata.cap_mode > 3)
		savedata.cap_mode = 0;
	if (datFileSize < (int)(offsetof(save, cap_canvas_preset) + sizeof(savedata.cap_canvas_preset))
		|| savedata.cap_canvas_preset < 0 || savedata.cap_canvas_preset > 4)
		savedata.cap_canvas_preset = 2;
	if (datFileSize < (int)(offsetof(save, cap_canvas_w) + sizeof(savedata.cap_canvas_w))
		|| savedata.cap_canvas_w < 160 || savedata.cap_canvas_w > 7680)
		savedata.cap_canvas_w = 1920;
	if (datFileSize < (int)(offsetof(save, cap_canvas_h) + sizeof(savedata.cap_canvas_h))
		|| savedata.cap_canvas_h < 120 || savedata.cap_canvas_h > 4320)
		savedata.cap_canvas_h = 1080;
	if (datFileSize < (int)(offsetof(save, cap_include_mp) + sizeof(savedata.cap_include_mp)))
		savedata.cap_include_mp = 0;
	else if (savedata.cap_include_mp != 0)
		savedata.cap_include_mp = 1;
	if (datFileSize < (int)(offsetof(save, cap_monitor_idx) + sizeof(savedata.cap_monitor_idx))
		|| savedata.cap_monitor_idx < 0 || savedata.cap_monitor_idx > 63)
		savedata.cap_monitor_idx = 0;
	if (datFileSize < (int)(offsetof(save, cap_effect) + sizeof(savedata.cap_effect))
		|| savedata.cap_effect < 0 || savedata.cap_effect >= 72)
		savedata.cap_effect = 0;
	if (datFileSize < (int)(offsetof(save, cap_fx_n) + sizeof(savedata.cap_fx_n))
		|| savedata.cap_fx_n < 0 || savedata.cap_fx_n > 8)
		savedata.cap_fx_n = 0;
	auto clampFx = [](int v) -> int {
		if (v < 0 || v >= 72) return 0; // SC_FX_COUNT
		return v;
	};
	if (datFileSize < (int)(offsetof(save, cap_fx0) + sizeof(savedata.cap_fx0)))
		savedata.cap_fx0 = 0;
	else
		savedata.cap_fx0 = clampFx(savedata.cap_fx0);
	if (datFileSize < (int)(offsetof(save, cap_fx1) + sizeof(savedata.cap_fx1)))
		savedata.cap_fx1 = 0;
	else
		savedata.cap_fx1 = clampFx(savedata.cap_fx1);
	if (datFileSize < (int)(offsetof(save, cap_fx2) + sizeof(savedata.cap_fx2)))
		savedata.cap_fx2 = 0;
	else
		savedata.cap_fx2 = clampFx(savedata.cap_fx2);
	if (datFileSize < (int)(offsetof(save, cap_fx3) + sizeof(savedata.cap_fx3)))
		savedata.cap_fx3 = 0;
	else
		savedata.cap_fx3 = clampFx(savedata.cap_fx3);
	if (datFileSize < (int)(offsetof(save, cap_fx4) + sizeof(savedata.cap_fx4)))
		savedata.cap_fx4 = 0;
	else
		savedata.cap_fx4 = clampFx(savedata.cap_fx4);
	if (datFileSize < (int)(offsetof(save, cap_fx5) + sizeof(savedata.cap_fx5)))
		savedata.cap_fx5 = 0;
	else
		savedata.cap_fx5 = clampFx(savedata.cap_fx5);
	if (datFileSize < (int)(offsetof(save, cap_fx6) + sizeof(savedata.cap_fx6)))
		savedata.cap_fx6 = 0;
	else
		savedata.cap_fx6 = clampFx(savedata.cap_fx6);
	if (datFileSize < (int)(offsetof(save, cap_fx7) + sizeof(savedata.cap_fx7)))
		savedata.cap_fx7 = 0;
	else
		savedata.cap_fx7 = clampFx(savedata.cap_fx7);
	// シーク loop つまみ: 旧.dat 未所持は 0=ロック(既定)
	if (datFileSize < (int)(offsetof(save, mpSeekLoopUnlock) + sizeof(savedata.mpSeekLoopUnlock)))
		savedata.mpSeekLoopUnlock = 0;
	else if (savedata.mpSeekLoopUnlock)
		savedata.mpSeekLoopUnlock = 1;
	// シーク波形: 旧.dat 未所持は表示ON
	if (datFileSize < (int)(offsetof(save, mpSeekWave) + sizeof(savedata.mpSeekWave)))
		savedata.mpSeekWave = 1;
	else if (savedata.mpSeekWave)
		savedata.mpSeekWave = 1;
	if (datFileSize < (int)(offsetof(save, mpPhraseSec) + sizeof(savedata.mpPhraseSec))
		|| savedata.mpPhraseSec < 1 || savedata.mpPhraseSec > 60)
		savedata.mpPhraseSec = 4;
	if (datFileSize < (int)(offsetof(save, mpSleepMin) + sizeof(savedata.mpSleepMin))
		|| savedata.mpSleepMin < 0 || savedata.mpSleepMin > 240)
		savedata.mpSleepMin = 0;
	if (datFileSize < (int)(offsetof(save, mpHistTod) + sizeof(savedata.mpHistTod))) {
		for (int i = 0; i < 8; ++i) savedata.mpHistTod[i] = -1;
	}
	else {
		for (int i = 0; i < 8; ++i) {
			if (savedata.mpHistTod[i] < -1 || savedata.mpHistTod[i] >= 24 * 60)
				savedata.mpHistTod[i] = -1;
		}
	}
	if (datFileSize < (int)(offsetof(save, mpBeatGrid) + sizeof(savedata.mpBeatGrid)))
		savedata.mpBeatGrid = 0;
	else if (savedata.mpBeatGrid) savedata.mpBeatGrid = 1;
	if (datFileSize < (int)(offsetof(save, mpXfadePreview) + sizeof(savedata.mpXfadePreview)))
		savedata.mpXfadePreview = 0;
	else if (savedata.mpXfadePreview) savedata.mpXfadePreview = 1;
	if (datFileSize < (int)(offsetof(save, mpLoopbackScore) + sizeof(savedata.mpLoopbackScore)))
		savedata.mpLoopbackScore = 0;
	else if (savedata.mpLoopbackScore) savedata.mpLoopbackScore = 1;
	if (datFileSize < (int)(offsetof(save, mpChordPanel) + sizeof(savedata.mpChordPanel)))
		savedata.mpChordPanel = 0;
	else if (savedata.mpChordPanel) savedata.mpChordPanel = 1;
	if (datFileSize < (int)(offsetof(save, deskLrcOn) + sizeof(savedata.deskLrcOn))) {
		savedata.deskLrcOn = 0;
		savedata.deskLrcX = 80;
		savedata.deskLrcY = 80;
		savedata.deskLrcW = 640;
		savedata.deskLrcH = 160;
		savedata.deskLrcAlpha = 200;
	}
	if (savedata.deskLrcAlpha < 40) savedata.deskLrcAlpha = 40;
	if (savedata.deskLrcAlpha > 255) savedata.deskLrcAlpha = 255;
	if (datFileSize < (int)(offsetof(save, mpVocalCenter) + sizeof(savedata.mpVocalCenter)))
		savedata.mpVocalCenter = 100;
	else if (savedata.mpVocalCenter < 0) savedata.mpVocalCenter = 0;
	else if (savedata.mpVocalCenter > 200) savedata.mpVocalCenter = 200;
	if (datFileSize < (int)(offsetof(save, mpMirrorOut) + sizeof(savedata.mpMirrorOut))) {
		savedata.mpMirrorOut = 0;
		savedata.mpMirrorVol = 100;
		savedata.mpMirrorDevice[0] = 0;
	}
	if (savedata.mpMirrorVol < 0) savedata.mpMirrorVol = 0;
	if (savedata.mpMirrorVol > 100) savedata.mpMirrorVol = 100;
	if (datFileSize < (int)(offsetof(save, mpRemoteOn) + sizeof(savedata.mpRemoteOn))) {
		savedata.mpRemoteOn = 0;
		savedata.mpRemotePort = 8765;
	}
	if (savedata.mpRemotePort < 1024 || savedata.mpRemotePort > 65535)
		savedata.mpRemotePort = 8765;
	if (datFileSize < (int)(offsetof(save, mpAlarmHour) + sizeof(savedata.mpAlarmHour))) {
		savedata.mpAlarmHour = -1;
		savedata.mpAlarmMin = 0;
	}
	if (savedata.mpAlarmMin < 0) savedata.mpAlarmMin = 0;
	if (savedata.mpAlarmMin > 59) savedata.mpAlarmMin = 59;
	if (savedata.mpAlarmHour < -1) savedata.mpAlarmHour = -1;
	if (savedata.mpAlarmHour > 23) savedata.mpAlarmHour = -1;
	if (datFileSize < (int)(offsetof(save, mpSsVizOn) + sizeof(savedata.mpSsVizOn)))
		savedata.mpSsVizOn = 0;
	if (datFileSize < (int)(offsetof(save, mpDetectedBpm) + sizeof(savedata.mpDetectedBpm)))
		savedata.mpDetectedBpm = 0;
	if (datFileSize < (int)(offsetof(save, mpDjPadwindow) + sizeof(savedata.mpDjPadwindow)))
		savedata.mpDjPadwindow = 0;
	else if (savedata.mpDjPadwindow != 0)
		savedata.mpDjPadwindow = 1;
	if (datFileSize < (int)(offsetof(save, mpNormTargetLufs) + sizeof(savedata.mpNormTargetLufs)))
		savedata.mpNormTargetLufs = -14;
	if (savedata.mpNormTargetLufs > -1) savedata.mpNormTargetLufs = -14;
	if (savedata.mpNormTargetLufs < -30) savedata.mpNormTargetLufs = -30;
	if (datFileSize < (int)(offsetof(save, mpKeyEqSuggest) + sizeof(savedata.mpKeyEqSuggest)))
		savedata.mpKeyEqSuggest = 0;
	else if (savedata.mpKeyEqSuggest) savedata.mpKeyEqSuggest = 1;
	if (datFileSize < (int)(offsetof(save, mpJacketRemOverlay) + sizeof(savedata.mpJacketRemOverlay)))
		savedata.mpJacketRemOverlay = 1; // 従来どおり表示
	else if (savedata.mpJacketRemOverlay) savedata.mpJacketRemOverlay = 1;
	if (datFileSize < (int)(offsetof(save, mpBpmCand) + sizeof(savedata.mpBpmCand))) {
		savedata.mpBpmCand[0] = savedata.mpBpmCand[1] = savedata.mpBpmCand[2] = 0;
		if (savedata.mpDetectedBpm > 0)
			savedata.mpBpmCand[0] = savedata.mpDetectedBpm;
	} else {
		for (int i = 0; i < 3; ++i) {
			if (savedata.mpBpmCand[i] < 0 || savedata.mpBpmCand[i] > 300)
				savedata.mpBpmCand[i] = 0;
		}
	}
	if (datFileSize < (int)(offsetof(save, cap_fx_str) + sizeof(savedata.cap_fx_str))) {
		memset(savedata.cap_fx_str, 4, sizeof(savedata.cap_fx_str)); // SC_FX_STR_DEF
	} else {
		for (int i = 0; i < 8; ++i) {
			for (int s = 0; s < 8; ++s) {
				if (savedata.cap_fx_str[i][s] > 8)
					savedata.cap_fx_str[i][s] = 4;
			}
		}
	}
	if (datFileSize < (int)(offsetof(save, cap_fx_pre_name) + sizeof(savedata.cap_fx_pre_name))) {
		memset(savedata.cap_fx_pre_name, 0, sizeof(savedata.cap_fx_pre_name));
		memset(savedata.cap_fx_pre_n, 0, sizeof(savedata.cap_fx_pre_n));
		memset(savedata.cap_fx_pre_fx, 0, sizeof(savedata.cap_fx_pre_fx));
		memset(savedata.cap_fx_pre_str, 4, sizeof(savedata.cap_fx_pre_str));
		savedata.cap_fx_pre_sel = 0;
	} else {
		for (int p = 0; p < 16; ++p) {
			savedata.cap_fx_pre_name[p][_countof(savedata.cap_fx_pre_name[p]) - 1] = 0;
			if (savedata.cap_fx_pre_n[p] < 0 || savedata.cap_fx_pre_n[p] > 8)
				savedata.cap_fx_pre_n[p] = 0;
			for (int i = 0; i < 8; ++i) {
				if (savedata.cap_fx_pre_fx[p][i] < 0 || savedata.cap_fx_pre_fx[p][i] >= 72) // SC_FX_COUNT
					savedata.cap_fx_pre_fx[p][i] = 0;
				for (int s = 0; s < 8; ++s) {
					if (savedata.cap_fx_pre_str[p][i][s] > 8)
						savedata.cap_fx_pre_str[p][i][s] = 4;
				}
			}
		}
		if (savedata.cap_fx_pre_sel < 0 || savedata.cap_fx_pre_sel > 15)
			savedata.cap_fx_pre_sel = 0;
	}
	if (datFileSize < (int)(offsetof(save, popupMenuFace) + sizeof(savedata.popupMenuFace))) {
		savedata.popupMenuFace[0] = 0;
		savedata.popupMenuPoint = 9;
		savedata.popupMenuBold = 0;
		savedata.popupMenuItalic = 0;
	} else {
		savedata.popupMenuFace[_countof(savedata.popupMenuFace) - 1] = 0;
		if (savedata.popupMenuPoint < 8 || savedata.popupMenuPoint > 24)
			savedata.popupMenuPoint = 9;
		savedata.popupMenuBold = savedata.popupMenuBold ? 1 : 0;
		savedata.popupMenuItalic = savedata.popupMenuItalic ? 1 : 0;
	}
	if (datFileSize < (int)(offsetof(save, deskLrcFontAuto) + sizeof(savedata.deskLrcFontAuto))) {
		savedata.deskLrcFontAuto = 1;
		savedata.deskLrcFontPt = 140;
		savedata.deskLrcLines = 10;
	} else {
		savedata.deskLrcFontAuto = savedata.deskLrcFontAuto ? 1 : 0;
		if (savedata.deskLrcFontPt < 80) savedata.deskLrcFontPt = 80;
		if (savedata.deskLrcFontPt > 480) savedata.deskLrcFontPt = 480;
	}
	if (datFileSize < (int)(offsetof(save, deskLrcLines) + sizeof(savedata.deskLrcLines)))
		savedata.deskLrcLines = 10;
	else {
		if (savedata.deskLrcLines < 3) savedata.deskLrcLines = 3;
		if (savedata.deskLrcLines > 20) savedata.deskLrcLines = 20;
	}
	if (datFileSize < (int)(offsetof(save, deskLrcWinX) + sizeof(savedata.deskLrcWinX) * 4)) {
		// 旧 mid フィールドから末尾へ移行
		savedata.deskLrcWinX = savedata.deskLrcX;
		savedata.deskLrcWinY = savedata.deskLrcY;
		savedata.deskLrcWinW = savedata.deskLrcW;
		savedata.deskLrcWinH = savedata.deskLrcH;
	}
	if (savedata.deskLrcWinW < 200) savedata.deskLrcWinW = 640;
	if (savedata.deskLrcWinH < 80) savedata.deskLrcWinH = 160;
	if (savedata.deskLrcWinW > 1600) savedata.deskLrcWinW = 800;
	if (savedata.deskLrcWinH > 900) savedata.deskLrcWinH = 360;
	if (datFileSize < (int)(offsetof(save, mpDjScratchEffect) + sizeof(savedata.mpDjScratchEffect) * 2)) {
		savedata.mpDjScratchEffect = 100;
		savedata.mpDjScratchSpeed = 100;
	} else {
		if (savedata.mpDjScratchEffect < 0) savedata.mpDjScratchEffect = 0;
		if (savedata.mpDjScratchEffect > 200) savedata.mpDjScratchEffect = 200;
		if (savedata.mpDjScratchSpeed < 0) savedata.mpDjScratchSpeed = 0;
		if (savedata.mpDjScratchSpeed > 200) savedata.mpDjScratchSpeed = 200;
	}
	if (datFileSize < (int)(offsetof(save, mpDjEqLow) + sizeof(savedata.mpDjEqLow) * 7)) {
		savedata.mpDjEqLow = 100;
		savedata.mpDjEqMid = 100;
		savedata.mpDjEqHigh = 100;
		savedata.mpDjFilter = 100;
		savedata.mpDjEqKill = 0;
		savedata.mpDjPadMainLock = 0;
		savedata.mpDjPadTopMost = 0;
	} else {
		if (savedata.mpDjEqLow < 0) savedata.mpDjEqLow = 0;
		if (savedata.mpDjEqLow > 200) savedata.mpDjEqLow = 200;
		if (savedata.mpDjEqMid < 0) savedata.mpDjEqMid = 0;
		if (savedata.mpDjEqMid > 200) savedata.mpDjEqMid = 200;
		if (savedata.mpDjEqHigh < 0) savedata.mpDjEqHigh = 0;
		if (savedata.mpDjEqHigh > 200) savedata.mpDjEqHigh = 200;
		if (savedata.mpDjFilter < 0) savedata.mpDjFilter = 0;
		if (savedata.mpDjFilter > 200) savedata.mpDjFilter = 200;
		savedata.mpDjEqKill &= 7;
		if (savedata.mpDjPadMainLock != 0) savedata.mpDjPadMainLock = 1;
		if (savedata.mpDjPadTopMost != 0) savedata.mpDjPadTopMost = 1;
	}
	if (datFileSize < (int)(offsetof(save, updateAttemptExeTime) + sizeof(savedata.updateAttemptExeTime)))
		savedata.updateAttemptExeTime = 0;
	if (datFileSize < (int)(offsetof(save, popupMenuAnim) + sizeof(savedata.popupMenuAnim)))
		savedata.popupMenuAnim = 0;
	else if (savedata.popupMenuAnim < 0 || savedata.popupMenuAnim > 5)
		savedata.popupMenuAnim = 0;
	// 旧: cap_effect のみ → チェーン1段へ移行
	if (savedata.cap_fx_n <= 0 && savedata.cap_effect > 0) {
		savedata.cap_fx_n = 1;
		savedata.cap_fx0 = savedata.cap_effect;
	}
	// K-Lite Codec Pack 未導入時の誘導(いいえで今後出さない)
	if (savedata.kliteAskSkip == 0) {
		int hasKlite = 0;
		HKEY hk = NULL;
		static const TCHAR* kliteUninst[] = {
			_T("SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\KLiteCodecPack_is1"),
			_T("SOFTWARE\\Wow6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\KLiteCodecPack_is1"),
			_T("SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\K-Lite Codec Pack Full"),
			_T("SOFTWARE\\Wow6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\K-Lite Codec Pack Full"),
			_T("SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\K-Lite Codec Pack Mega"),
			_T("SOFTWARE\\Wow6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\K-Lite Codec Pack Mega"),
			_T("SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\K-Lite Codec Pack Standard"),
			_T("SOFTWARE\\Wow6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\K-Lite Codec Pack Standard"),
			_T("SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\K-Lite Codec Pack Basic"),
			_T("SOFTWARE\\Wow6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\K-Lite Codec Pack Basic"),
		};
		for (int ki = 0; ki < (int)_countof(kliteUninst); ki++) {
			if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, kliteUninst[ki], 0, KEY_READ, &hk) == ERROR_SUCCESS) {
				RegCloseKey(hk);
				hasKlite = 1;
				break;
			}
		}
		if (!hasKlite) {
			TCHAR pf[MAX_PATH];
			TCHAR path[MAX_PATH];
			pf[0] = 0;
			if (GetEnvironmentVariable(_T("ProgramFiles"), pf, MAX_PATH) > 0) {
				_sntprintf(path, MAX_PATH, _T("%s\\K-Lite Codec Pack"), pf);
				path[MAX_PATH - 1] = 0;
				if (GetFileAttributes(path) != INVALID_FILE_ATTRIBUTES)
					hasKlite = 1;
			}
			if (!hasKlite) {
				pf[0] = 0;
				if (GetEnvironmentVariable(_T("ProgramFiles(x86)"), pf, MAX_PATH) > 0) {
					_sntprintf(path, MAX_PATH, _T("%s\\K-Lite Codec Pack"), pf);
					path[MAX_PATH - 1] = 0;
					if (GetFileAttributes(path) != INVALID_FILE_ATTRIBUTES)
						hasKlite = 1;
				}
			}
		}
		if (!hasKlite) {
			int abcKlite = AfxMessageBox(LL14(
				L"動画再生に必要な K-Lite Codec Pack がインストールされていないようです。\nインストール方法の解説ページを開きますか？\n(いいえを選ぶと今後表示しません)",
				L"K-Lite Codec Pack (needed for video playback) does not appear to be installed.\nOpen the install guide page?\n(Selecting No will not show this again.)",
				L"K-Lite Codec Pack (necessaire a la lecture video) ne semble pas installe.\nOuvrir la page d'installation ?\n(Non = ne plus afficher.)",
				L"K-Lite Codec Pack (necessario per i video) non sembra installato.\nAprire la guida all'installazione?\n(No = non mostrare piu.)",
				L"K-Lite Codec Pack (necesario para video) no parece instalado.\nAbrir la pagina de instalacion?\n(No = no volver a mostrar.)",
				L"동영상 재생에 필요한 K-Lite Codec Pack이 설치되지 않은 것 같습니다.\n설치 안내 페이지를 여시겠습니까?\n(아니요를 누르면 다시 표시하지 않습니다)",
				L"似乎未安装视频播放所需的 K-Lite Codec Pack。\n是否打开安装说明页面？\n（选择“否”后将不再显示）",
				L"يبدو ان K-Lite Codec Pack المطلوب للفيديو غير مثبت.\nفتح صفحة دليل التثبيت؟\n(لا = عدم الاظهار مجددا)",
				L"Похоже, K-Lite Codec Pack (нужен для видео) не установлен.\nОткрыть страницу с инструкцией?\n(Нет = больше не показывать.)",
				L"K-Lite Codec Pack (fuer Video noetig) scheint nicht installiert.\nInstallationsseite oeffnen?\n(Nein = nicht mehr anzeigen.)",
				L"K-Lite Codec Pack (necessario para video) nao parece instalado.\nAbrir a pagina de instalacao?\n(Nao = nao mostrar de novo.)",
				L"K-Lite Codec Pack (nodig voor video) lijkt niet geinstalleerd.\nInstallatiepagina openen?\n(Nee = niet meer tonen.)",
				L"K-Lite Codec Pack (potrzebny do wideo) wydaje sie nieinstalowany.\nOtworzyc strone instalacji?\n(Nie = nie pokazuj wiecej.)",
				L"Video icin gerekli K-Lite Codec Pack yuklu degil gibi.\nKurulum sayfasi acilsin mi?\n(Hayir = bir daha gosterme.)"
				), MB_YESNO | MB_ICONINFORMATION);
			if (abcKlite == IDYES) {
				::ShellExecute(NULL, _T("open"), _T("https://ppp.oohara.jp/k-lite.html"), NULL, NULL, SW_SHOWNORMAL);
			}
			else {
				savedata.kliteAskSkip = 1;
			}
		}
	}
	if (savedata.aerocheck == 99) {
		int abc = AfxMessageBox(LL14(
			L"Win10/11アクリルぼかしが実装されました。\n有効にしますか？\n(このメッセージは一回しか表示されません)",
			L"Acrylic blur for Windows 10/11 has been implemented.\nDo you want to enable it?\n(This message will only appear once.)",
			L"Le flou acrylique pour Windows 10/11 a ete implemente.\nVoulez-vous l'activer ?\n(Ce message ne s'affichera qu'une seule fois.)",
			L"La sfocatura acrilica per Windows 10/11 e stata implementata.\nVuoi abilitarla?\n(Questo messaggio apparira solo una volta.)",
			L"El desenfoque acrilico para Windows 10/11 ha sido implementado.\nDesea activarlo?\n(Este mensaje solo aparecera una vez.)",
			L"Windows 10/11 아크릴 블러가 구현되었습니다.\n활성화하시겠습니까?\n(이 메시지는 한 번만 표시됩니다.)",
			L"已实现 Windows 10/11 亚克力模糊效果。\n是否启用？\n（此消息仅显示一次。）",
			L"تم تنفيذ ضبابية الأكريليك لنظام Windows 10/11.\nهل تريد تفعيله؟\n(ستظهر هذه الرسالة مرة واحدة فقط.)",
			L"Реализовано акриловое размытие для Windows 10/11.\nВключить?\n(Это сообщение отобразится только один раз.)",
			L"Acryl-Unschaerfe fuer Windows 10/11 wurde implementiert.\nMoechten Sie sie aktivieren?\n(Diese Meldung wird nur einmal angezeigt.)",
			L"O desfoque acrilico para Windows 10/11 foi implementado.\nDeseja ativa-lo?\n(Esta mensagem sera exibida apenas uma vez.)",
			L"Acrylvervaging voor Windows 10/11 is geimplementeerd.\nWilt u dit inschakelen?\n(Dit bericht wordt slechts eenmaal weergegeven.)",
			L"Zaimplementowano rozmycie akrylowe dla Windows 10/11.\nCzy chcesz je wlaczyc?\n(Ten komunikat pojawi sie tylko raz.)",
			L"Windows 10/11 icin akrilik bulaniklik uygulandi.\nEtkinlestirilsin mi?\n(Bu mesaj yalnizca bir kez goruntulenir.)"
			), MB_YESNO);
		if (abc == IDYES) {
			savedata.aero = 1;
			savedata.aerocheck = 1;
		}
		else {
			savedata.aero = 0;
			savedata.aerocheck = 1;
		}
	}
	_tchdir(karento2);
	DatArc_Chdir();
#if _UNICODE
	if (ab.Open(L"oggYSEDbgmu.dat", CFile::modeCreate | CFile::modeWrite | CFile::shareExclusive, NULL) == TRUE) {
#else
	if (ab.Open("oggYSEDbgm.dat", CFile::modeCreate | CFile::modeWrite | CFile::shareExclusive, NULL) == TRUE) {
#endif
		ab.Write(&savedata, sizeof(save));
		ab.Close();
#if _UNICODE
		DatArc_Commit(L"oggYSEDbgmu.dat");
#else
		DatArc_Commit("oggYSEDbgm.dat");
#endif
	}

	// 曲ごとのオーディオ/DSP パラメータ(別ファイル)を読み込む
	SongParams_LoadFile();
	ProAudio_Init();
	MpHist_Init();
	MpSmart_Init();
	// 配布済 AudioData.dat の旧キー(pathのみ)を mode+ret2 付きへ一度だけ移行
	// (playlistu*.dat をディスクから走査。UI 未作成でも可)
	SongParams_ConvertKeysIfNeeded();

	// モード選択画面やメイン画面を開く前に更新を確認する。
	// 更新があればそのまま適用・再起動し、なければ通常の起動を続ける。
	RunStartupUpdateCheck();

	// 起動時のモード選択(ファルコムbgm特化型画面 / メディアプレイヤー画面)
	if (savedata.startupAsk) {
		CModeSelectDlg msd;
		msd.DoModal();   // savedata.playerMode / savedata.startupAsk を更新
		// 選択結果を即保存
		DatArc_Chdir();
		CFile sf;
#if _UNICODE
		if (sf.Open(L"oggYSEDbgmu.dat", CFile::modeCreate | CFile::modeWrite | CFile::shareExclusive, NULL) == TRUE) {
#else
		if (sf.Open("oggYSEDbgm.dat", CFile::modeCreate | CFile::modeWrite | CFile::shareExclusive, NULL) == TRUE) {
#endif
			sf.Write(&savedata, sizeof(save));
			sf.Close();
#if _UNICODE
			DatArc_Commit(L"oggYSEDbgmu.dat");
#else
			DatArc_Commit("oggYSEDbgm.dat");
#endif
		}
	}

	COggDlg dlg;
	og=&dlg;
	m_pMainWnd = &dlg;
	int nResponse = dlg.DoModal();
	// 曲ごとパラメータの未書き込み分を確定
	SongParams_SaveFile();
	DatArc_Chdir();
#if _UNICODE
		if(ab.Open(L"oggYSEDbgmu.dat",CFile::modeCreate | CFile::modeWrite | CFile::shareExclusive,NULL)==TRUE){
#else
		if(ab.Open("oggYSEDbgm.dat",CFile::modeCreate | CFile::modeWrite | CFile::shareExclusive,NULL)==TRUE){
#endif
		ab.Write(&savedata,sizeof(save));
		ab.Close();
#if _UNICODE
		DatArc_Commit(L"oggYSEDbgmu.dat");
#else
		DatArc_Commit("oggYSEDbgm.dat");
#endif
	}
	DatArc_Shutdown();
	if (nResponse == IDOK)
	{
		// TODO: ダイアログが <OK> で消された時のコードを
		//       記述してください。
	}
	else if (nResponse == IDCANCEL)
	{
		// TODO: ダイアログが <キャンセル> で消された時のコードを
		//       記述してください。
	}

	// ダイアログが閉じられてからアプリケーションのメッセージ ポンプを開始するよりは、
	// アプリケーションを終了するために FALSE を返してください。
	ReleaseMutex(Mutex); 
	return FALSE;
}

	struct save1{
		char ysf[1024];
		char ys6[1024];
		char ed6fc[1024];
		char ed6sc[1024];
		int douga;
		int supe;
		int supe2;

		int random;
		int kaisuu;
		int gameflg[4];

		int xx,yy;
		int gx,gy;

		char yso[1024];
		int gameflg2;

		char ed6tc[1024];
		int gameflg3;

		char zweiii[1024];
		int gameflg4;

		int dsvol;
		int render;

		char ysc[1024];
		int gameflg5;
		int gameflg6;

		char xa[1024];
		int gameflg7;

		char ys12[1024];
		int gameflg8;
		int gameflg9;

		char sor[1024];
		int gameflg10;
		char ys122[1024];

		char zwei[1024];
		int gameflg11;

		char gurumin[1024];
		int gameflg12;

		char dino[1024];
		int gameflg13;

		RECT p;

		char br4[1024];
		int gameflg14;

		char ed3[1024];
		int gameflg15;

		char ed4[1024];
		int gameflg16;

		char ed5[1024];
		int gameflg17;

		char tuki[1024];
		char nishi[1024];
		char arc[1024];
		char san1[1024];
		char san2[1024];

		int fs;
		int evr;
		int con;
		int aero;
		int pl;
		int ffd;
	};

struct playlistdataold{
	char name[1024];
	char art[1024];
	char alb[1024];
	char fol[1024];
	int sub;
	int loop1;
	int loop2;
	int ret2;
	int res1;
	int res2;
};

struct playlistdatanew{
	TCHAR name[1024];
	TCHAR art[1024];
	TCHAR alb[1024];
	TCHAR fol[1024];
	int sub;
	int loop1;
	int loop2;
	int ret2;
	int res1;
	int res2;
};
#ifdef _UNICODE
void COggApp::convert()
{
	save1 saveold;
	DatArc_Chdir();
	CFile ab;
	if(ab.Open(_T("oggYSEDbgm.dat"),CFile::modeRead | CFile::shareDenyWrite,NULL)!=TRUE){
		return;
	}else{
		ab.Read(&saveold,sizeof(save1));
		ab.Close();
	}
	savedata.aero=saveold.aero;
	WCHAR ss[1024];	LPWSTR ss1; char *s3;
	s3=saveold.arc; ss1=savedata.arc;
	MultiByteToWideChar(CP_ACP,0,s3,-1,ss1,1024);
	s3=saveold.br4; ss1=savedata.br4;
	MultiByteToWideChar(CP_ACP,0,s3,-1,ss1,1024);
	savedata.con=saveold.con;
	s3=saveold.dino; ss1=savedata.dino;
	MultiByteToWideChar(CP_ACP,0,s3,-1,ss1,1024);
	savedata.douga=saveold.douga;
	savedata.dsvol=saveold.dsvol;
	s3=saveold.ed3; ss1=savedata.ed3;
	MultiByteToWideChar(CP_ACP,0,s3,-1,ss1,1024);
	s3=saveold.ed4; ss1=savedata.ed4;
	MultiByteToWideChar(CP_ACP,0,s3,-1,ss1,1024);
	s3=saveold.ed5; ss1=savedata.ed5;
	MultiByteToWideChar(CP_ACP,0,s3,-1,ss1,1024);
	s3=saveold.ed6fc; ss1=savedata.ed6fc;
	MultiByteToWideChar(CP_ACP,0,s3,-1,ss1,1024);
	s3=saveold.ed6sc; ss1=savedata.ed6sc;
	MultiByteToWideChar(CP_ACP,0,s3,-1,ss1,1024);
	s3=saveold.ed6tc; ss1=savedata.ed6tc;
	MultiByteToWideChar(CP_ACP,0,s3,-1,ss1,1024);
	savedata.evr=saveold.evr;
	savedata.ffd=saveold.ffd;
	savedata.fs=saveold.fs;
	savedata.gameflg[0]=saveold.gameflg[0];
	savedata.gameflg[1]=saveold.gameflg[1];
	savedata.gameflg[2]=saveold.gameflg[2];
	savedata.gameflg[3]=saveold.gameflg[3];
	savedata.gameflg10=saveold.gameflg10;
	savedata.gameflg11=saveold.gameflg11;
	savedata.gameflg12=saveold.gameflg12;
	savedata.gameflg13=saveold.gameflg13;
	savedata.gameflg14=saveold.gameflg14;
	savedata.gameflg15=saveold.gameflg15;
	savedata.gameflg16=saveold.gameflg16;
	savedata.gameflg17=saveold.gameflg17;
	savedata.gameflg2=saveold.gameflg2;
	savedata.gameflg3=saveold.gameflg3;
	savedata.gameflg4=saveold.gameflg4;
	savedata.gameflg5=saveold.gameflg5;
	savedata.gameflg6=saveold.gameflg6;
	savedata.gameflg7=saveold.gameflg7;
	savedata.gameflg8=saveold.gameflg8;
	savedata.gameflg9=saveold.gameflg9;
	s3=saveold.gurumin; ss1=savedata.gurumin;
	MultiByteToWideChar(CP_ACP,0,s3,-1,ss1,1024);
	savedata.gx=saveold.gx;
	savedata.gy=saveold.gy;
	savedata.kaisuu=saveold.kaisuu;
	s3=saveold.nishi; ss1=savedata.nishi;
	MultiByteToWideChar(CP_ACP,0,s3,-1,ss1,1024);
	savedata.p.bottom=saveold.p.bottom;
	savedata.p.left=saveold.p.left;
	savedata.p.right=saveold.p.right;
	savedata.p.top=saveold.p.top;
	savedata.pl=saveold.pl;
	savedata.random=saveold.random;
	savedata.render=saveold.render;
	s3=saveold.san1; ss1=savedata.san1;
	MultiByteToWideChar(CP_ACP,0,s3,-1,ss1,1024);
	s3=saveold.san2; ss1=savedata.san2;
	MultiByteToWideChar(CP_ACP,0,s3,-1,ss1,1024);
	s3=saveold.sor; ss1=savedata.sor;
	MultiByteToWideChar(CP_ACP,0,s3,-1,ss1,1024);
	savedata.supe=saveold.supe;
	savedata.supe2=saveold.supe2;
	s3=saveold.tuki; ss1=savedata.tuki;
	MultiByteToWideChar(CP_ACP,0,s3,-1,ss1,1024);
	s3=saveold.xa; ss1=savedata.xa;
	MultiByteToWideChar(CP_ACP,0,s3,-1,ss1,1024);
	s3=saveold.ys12; ss1=savedata.ys12;
	MultiByteToWideChar(CP_ACP,0,s3,-1,ss1,1024);
	s3=saveold.ys122; ss1=savedata.ys122;
	MultiByteToWideChar(CP_ACP,0,s3,-1,ss1,1024);
	s3=saveold.ys6; ss1=savedata.ys6;
	MultiByteToWideChar(CP_ACP,0,s3,-1,ss1,1024);
	s3=saveold.ysc; ss1=savedata.ysc;
	MultiByteToWideChar(CP_ACP,0,s3,-1,ss1,1024);
	s3=saveold.ysf; ss1=savedata.ysf;
	MultiByteToWideChar(CP_ACP,0,s3,-1,ss1,1024);
	s3=saveold.yso; ss1=savedata.yso;
	MultiByteToWideChar(CP_ACP,0,s3,-1,ss1,1024);
	savedata.xx=saveold.xx;
	savedata.yy=saveold.yy;
	s3=saveold.zwei; ss1=savedata.zwei;
	MultiByteToWideChar(CP_ACP,0,s3,-1,ss1,1024);
	s3=saveold.zweiii; ss1=savedata.zweiii;
	MultiByteToWideChar(CP_ACP,0,s3,-1,ss1,1024);

	if(ab.Open(L"oggYSEDbgmu.dat",CFile::modeCreate | CFile::modeWrite | CFile::shareExclusive,NULL)==TRUE){
		ab.Write(&savedata,sizeof(save));
		ab.Close();
		DatArc_Commit(L"oggYSEDbgmu.dat");
	}

	//プレイリスト移植
	int cnt,x1,y1,cx,cy,x=-10000,y,c[4],c1[5];;
	CFile f;if(f.Open(_T("playlist.dat"),CFile::modeRead | CFile::shareDenyWrite,NULL)==TRUE){
		CFile g;g.Open(_T("playlistu.dat"),CFile::modeCreate|CFile::modeWrite | CFile::shareExclusive,NULL);
		f.Read(&cnt,4);
		f.Read(&x1,4);
		f.Read(&y1,4);
		f.Read(&cx,4);
		f.Read(&cy,4);
		f.Read(&c1[0],4);
		f.Read(&c1[1],4);
		f.Read(&c1[2],4);
		f.Read(&c1[3],4);
		f.Read(&c1[4],4);
		g.Write(&cnt,4);
		g.Write(&x1,4);
		g.Write(&y1,4);
		g.Write(&cx,4);
		g.Write(&cy,4);
		g.Write(&c1[0],4);
		g.Write(&c1[1],4);
		g.Write(&c1[2],4);
		g.Write(&c1[3],4);
		g.Write(&c1[4],4);
		playlistdataold pld;
		playlistdatanew pldn;
		for(int i=0;i<cnt;i++){
			f.Read(&pld,sizeof(pld));
			s3=pld.alb; ss1=pldn.alb;MultiByteToWideChar(CP_ACP,0,s3,-1,ss1,1024);
			s3=pld.art; ss1=pldn.art;MultiByteToWideChar(CP_ACP,0,s3,-1,ss1,1024);
			s3=pld.fol; ss1=pldn.fol;MultiByteToWideChar(CP_ACP,0,s3,-1,ss1,1024);
			s3=pld.name; ss1=pldn.name;MultiByteToWideChar(CP_ACP,0,s3,-1,ss1,1024);
			pldn.loop1=pld.loop1;
			pldn.loop2=pld.loop2;
			pldn.res1=pld.res1;
			pldn.res2=pld.res2;
			pldn.ret2=pld.ret2;
			pldn.sub=pld.sub;
			g.Write(&pldn,sizeof(pldn));
		}
		c[0]=0;f.Read(&c[0],4);
		c[1]=0;f.Read(&c[1],4);
		c[2]=1;f.Read(&c[2],4);
		c[3]=1;f.Read(&c[3],4);
		g.Write(&c[0],4);
		g.Write(&c[1],4);
		g.Write(&c[2],4);
		g.Write(&c[3],4);
		f.Close();
		g.Close();
		DatArc_FlushAll();
	}else return;
}
#else
void COggApp::convert()
{
}
#endif

// RubberBand関連のグローバル変数の定義（0=レガシー/スロット0、1=スロット1）
RubberBand::RubberBandStretcher* g_rubberBandStretcher[2] = { NULL, NULL };
std::vector<float> m_convertedPcmFloatData;
std::vector<uint8_t> m_bufwav3_1;
std::vector<float> inputFloatData;