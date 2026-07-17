// ogg.cpp : アプリケーション用クラスの定義を行います。
//

#include "stdafx.h"
#include "Windows.h"
#include "ogg.h"
#include "oggDlg.h"
#include "CMediaPlayerDlg.h"
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
	::GetWindowText(hwnd,a,sizeof(a));s=a; 
	if(s.Find(_T("mp3/m4a簡易プレイヤ"))==-1) return TRUE;
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
	savedata.saveversion = 2;

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

#if _UNICODE
	if(GetKeyState(VK_CONTROL) < 0){
		if(AfxMessageBox(LL14(L"ANSI版からのコンバートを行いますか？", L"Convert from ANSI version?", L"Convertir depuis la version ANSI ?", L"Convertire dalla versione ANSI?", L"?Convertir desde version ANSI?", L"ANSI ???? ?????????", L"从ANSI版本??？", L"??????? ?? ????? ANSI?", L"Конвертировать из версии ANSI?", L"Von ANSI-Version konvertieren?", L"Converter da versao ANSI?", L"Converteren van ANSI-versie?", L"Konwertowa? z wersji ANSI?", L"ANSI surumunden donu?turulsun mu?"),MB_YESNO)==IDYES){
			convert();
			AfxMessageBox(LL14(L"コンバートが完了しました。", L"Conversion completed.", L"Conversion terminee.", L"Conversione completata.", L"Conversion completada.", L"??? ???????.", L"??完成。", L"????? ???????.", L"Конвертация завершена.", L"Konvertierung abgeschlossen.", L"Conversao concluida.", L"Conversie voltooid.", L"Konwersja zako?czona.", L"Donu?um tamamland?."));
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
		AfxMessageBox(LL14(L"ANSI版からのコンバートを行います。", L"Converting from ANSI version.", L"Conversion depuis la version ANSI en cours.", L"Conversione dalla versione ANSI in corso.", L"Convirtiendo desde version ANSI.", L"ANSI ???? ?? ????.", L"正在从ANSI版本??。", L"???? ??????? ?? ????? ANSI.", L"Конвертация из версии ANSI.", L"Konvertierung von ANSI-Version.", L"Convertendo da versao ANSI.", L"Converteren van ANSI-versie.", L"Konwertowanie z wersji ANSI.", L"ANSI surumunden donu?turuluyor."));
		convert();
		AfxMessageBox(LL14(L"コンバートが完了しました。", L"Conversion completed.", L"Conversion terminee.", L"Conversione completata.", L"Conversion completada.", L"??? ???????.", L"??完成。", L"????? ???????.", L"Конвертация завершена.", L"Konvertierung abgeschlossen.", L"Conversao concluida.", L"Conversie voltooid.", L"Konwersja zako?czona.", L"Donu?um tamamland?."));
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
	if (savedata.aerocheck == 99) {
		int abc = AfxMessageBox(LL14(L"エアロ(透過処理)がメイン画面等に実装されました。是非試してみて貰えれば。\n有効にしますか？(少し不安定な部分あります)\n(このメッセージは一回しか表示されません)\nWindows11以降では、有効にしないで下さい。", L"Aero (transparency) has been implemented on the main window, etc. Please try it.\nEnable it? (Some instability may occur)\n(This message will only be shown once)\nDo not enable on Windows 11 or later.", L"Aero (transparence) a ete implemente. Souhaitez-vous l'activer ?", L"Aero (trasparenza) implementato. Abilitare?", L"Aero (transparencia) implementado. ?Activar?", L"Aero(??)? ???????. ??????????", L"已??Aero(透明)功能。是否?用？", L"?? ????? Aero. ?? ??????", L"Aero реализован. Включить?", L"Aero implementiert. Aktivieren?", L"Aero implementado. Ativar?", L"Aero geimplementeerd. Inschakelen?", L"Aero zaimplementowano. W??czy??", L"Aero uyguland?. Etkinle?tirilsin mi?"), MB_YESNO);
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
#if _UNICODE
	if (ab.Open(L"oggYSEDbgmu.dat", CFile::modeCreate | CFile::modeWrite | CFile::shareExclusive, NULL) == TRUE) {
#else
	if (ab.Open("oggYSEDbgm.dat", CFile::modeCreate | CFile::modeWrite | CFile::shareExclusive, NULL) == TRUE) {
#endif
		ab.Write(&savedata, sizeof(save));
		ab.Close();
	}
	// 起動時のモード選択(ファルコムbgm特化型画面 / メディアプレイヤー画面)
	if (savedata.startupAsk) {
		CModeSelectDlg msd;
		msd.DoModal();   // savedata.playerMode / savedata.startupAsk を更新
		// 選択結果を即保存
		_tchdir(karento2);
		CFile sf;
#if _UNICODE
		if (sf.Open(L"oggYSEDbgmu.dat", CFile::modeCreate | CFile::modeWrite | CFile::shareExclusive, NULL) == TRUE) {
#else
		if (sf.Open("oggYSEDbgm.dat", CFile::modeCreate | CFile::modeWrite | CFile::shareExclusive, NULL) == TRUE) {
#endif
			sf.Write(&savedata, sizeof(save));
			sf.Close();
		}
	}

	COggDlg dlg;
	og=&dlg;
	m_pMainWnd = &dlg;
	int nResponse = dlg.DoModal();
	_tchdir(karento2);
#if _UNICODE
		if(ab.Open(L"oggYSEDbgmu.dat",CFile::modeCreate | CFile::modeWrite | CFile::shareExclusive,NULL)==TRUE){
#else
		if(ab.Open("oggYSEDbgm.dat",CFile::modeCreate | CFile::modeWrite | CFile::shareExclusive,NULL)==TRUE){
#endif
		ab.Write(&savedata,sizeof(save));
		ab.Close();
	}
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
	_tchdir(karento2);
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
	}else return;
}
#else
void COggApp::convert()
{
}
#endif

// RubberBand関連のグローバル変数の定義
RubberBand::RubberBandStretcher* g_rubberBandStretcher = NULL;
std::vector<float> m_convertedPcmFloatData;
std::vector<uint8_t> m_bufwav3_1;
std::vector<float> inputFloatData;