// Render.cpp : インプリメンテーション ファイル
//

#include "stdafx.h"
#include "ogg.h"
#include "OSVersion.h"
#include "Render.h"
#include "Graph.h"
#include "dsound.h"
#include "ZeroFol.h"
#include "oggDlg.h"
#include "PlayList.h"
#include "CImageBase.h"
#include "AudioUpscaler.h"
#include "AudioDevSync.h"
#include <mutex>
#include <mmdeviceapi.h>
#include <FunctionDiscoveryKeys_devpkey.h>

static void SerializeLogFont(const LOGFONT* lf, TCHAR* str, int maxLen)
{
	if (!lf || !str) return;
	_sntprintf(str, maxLen, _T("%s|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d"),
		lf->lfFaceName,
		lf->lfHeight,
		lf->lfWidth,
		lf->lfEscapement,
		lf->lfOrientation,
		lf->lfWeight,
		lf->lfItalic,
		lf->lfUnderline,
		lf->lfStrikeOut,
		lf->lfCharSet,
		lf->lfOutPrecision,
		lf->lfClipPrecision,
		lf->lfQuality,
		lf->lfPitchAndFamily);
}

static bool DeserializeLogFont(const TCHAR* str, LOGFONT* lf)
{
	if (!str || !lf || _tcslen(str) == 0) return false;
	if (_tcschr(str, '|') == NULL) {
		memset(lf, 0, sizeof(LOGFONT));
		_tcsncpy(lf->lfFaceName, str, LF_FACESIZE - 1);
		lf->lfFaceName[LF_FACESIZE - 1] = 0;
		return false;
	}
	memset(lf, 0, sizeof(LOGFONT));
	TCHAR faceName[LF_FACESIZE] = { 0 };
	int height = 0, width = 0, escapement = 0, orientation = 0, weight = 0;
	int italic = 0, underline = 0, strikeOut = 0, charSet = 0, outPrecision = 0, clipPrecision = 0, quality = 0, pitchAndFamily = 0;
	int parsed = _stscanf(str, _T("%[^|]|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d"),
		faceName, &height, &width, &escapement, &orientation, &weight,
		&italic, &underline, &strikeOut, &charSet, &outPrecision, &clipPrecision, &quality, &pitchAndFamily);
	if (parsed >= 1) {
		_tcsncpy(lf->lfFaceName, faceName, LF_FACESIZE - 1);
		lf->lfFaceName[LF_FACESIZE - 1] = 0;
	}
	if (parsed >= 14) {
		lf->lfHeight = height;
		lf->lfWidth = width;
		lf->lfEscapement = escapement;
		lf->lfOrientation = orientation;
		lf->lfWeight = weight;
		lf->lfItalic = (BYTE)italic;
		lf->lfUnderline = (BYTE)underline;
		lf->lfStrikeOut = (BYTE)strikeOut;
		lf->lfCharSet = (BYTE)charSet;
		lf->lfOutPrecision = (BYTE)outPrecision;
		lf->lfClipPrecision = (BYTE)clipPrecision;
		lf->lfQuality = (BYTE)quality;
		lf->lfPitchAndFamily = (BYTE)pitchAndFamily;
		return true;
	}
	return false;
}

extern IGraphBuilder *pGraphBuilder;
extern std::mutex cl2;
extern ULONG oldw;

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif


extern save savedata;
CImageBase* renderbase;

namespace {

class CRdHelpDlg : public CDialog
{
public:
	enum { IDD = IDD_RD_HELP };
	explicit CRdHelpDlg(CWnd* pParent = nullptr) : CDialog(IDD, pParent) {}
protected:
	virtual BOOL OnInitDialog();
	virtual void PostNcDestroy();
	virtual void OnOK();
	virtual void OnCancel();
	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	afx_msg void OnClose();
	DECLARE_MESSAGE_MAP()
};

static CRdHelpDlg* g_rdHelpDlg = nullptr;

BEGIN_MESSAGE_MAP(CRdHelpDlg, CDialog)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_CLOSE()
END_MESSAGE_MAP()

BOOL CRdHelpDlg::OnInitDialog()
{
	CDialog::OnInitDialog();
	SetIcon(nullptr, TRUE);
	SetIcon(nullptr, FALSE);
	ModifyStyleEx(0, WS_EX_DLGMODALFRAME, SWP_FRAMECHANGED);
	SetWindowText(LL14(
		L"レンダリング操作ガイド", L"Rendering Guide", L"Guide de rendu", L"Guida rendering",
		L"Guía de renderizado", L"렌더링 가이드", L"渲染指南", L"دليل العرض",
		L"Руководство рендеринга", L"Rendering-Anleitung", L"Guia de renderização", L"Rendergids",
		L"Przewodnik renderowania", L"Render kılavuzu"));
	if (CWnd* pOk = GetDlgItem(IDOK))
		pOk->SetWindowText(LL14(L"閉じる", L"Close", L"Fermer", L"Chiudi", L"Cerrar", L"닫기", L"关闭", L"إغلاق",
			L"Закрыть", L"Schliessen", L"Fechar", L"Sluiten", L"Zamknij", L"Kapat"));
	return TRUE;
}

void CRdHelpDlg::OnOK() { DestroyWindow(); }
void CRdHelpDlg::OnCancel() { DestroyWindow(); }
void CRdHelpDlg::OnClose() { DestroyWindow(); }

void CRdHelpDlg::PostNcDestroy()
{
	CDialog::PostNcDestroy();
	if (g_rdHelpDlg == this)
		g_rdHelpDlg = nullptr;
	delete this;
}

BOOL CRdHelpDlg::OnEraseBkgnd(CDC* pDC)
{
	CRect rc; GetClientRect(&rc);
	pDC->FillSolidRect(rc, RGB(248, 248, 252));
	return TRUE;
}

void CRdHelpDlg::OnPaint()
{
	CPaintDC pdc(this);
	CCC_GdiHelpPaint hp;
	if (!CCC_GdiHelpBeginPaint(this, pdc, hp))
		return;
	CDC& dc = hp.mem;
	CRect rc = hp.rc;
	const int footerH = hp.footerH;
	dc.SetBkMode(TRANSPARENT);
	CFont* oldFont = dc.SelectObject(GetFont());

	TEXTMETRIC tm{};
	dc.GetTextMetrics(&tm);
	const int lh = max(14, tm.tmHeight + tm.tmExternalLeading + 1);
	const int titleLh = lh + 1;
	CBrush frameBrush(RGB(130, 130, 150));

	auto title = [&](int x, int y, LPCTSTR t) {
		dc.SetTextColor(RGB(55, 45, 85));
		dc.TextOut(x, y, t);
	};
	auto body = [&](int x, int y, LPCTSTR t) {
		dc.SetTextColor(RGB(65, 65, 80));
		dc.TextOut(x, y, t);
	};
	auto muted = [&](int x, int y, LPCTSTR t) {
		dc.SetTextColor(RGB(100, 100, 115));
		dc.TextOut(x, y, t);
	};

	int y = 6;
	const int L = 10;
	title(L, y, LL14(L"レンダリング操作ガイド", L"Rendering — Guide", L"Guide rendu", L"Guida rendering",
		L"Guía renderizado", L"렌더링 가이드", L"渲染指南", L"دليل العرض",
		L"Руководство рендеринга", L"Rendering-Guide", L"Guia renderização", L"Rendergids",
		L"Przewodnik renderowania", L"Render kılavuzu"));
	y += titleLh;
	muted(L, y, LL14(
		L"出力デバイス・バッファ・映像レンダラ・アクリルなど、再生まわりの総合設定です。詳細は各コントロールのツールチップも参照。",
		L"Global playback settings: device, buffer, video renderer, acrylic. See control tooltips for detail.",
		L"Réglages globaux: périphérique, tampon, rendu vidéo, acrylique. Voir aussi les info-bulles.",
		L"Impostazioni globali: dispositivo, buffer, renderer, acrilico. Vedi anche i tooltip.",
		L"Ajustes globales: dispositivo, búfer, renderizador, acrílico. Ver también tooltips.",
		L"출력 장치·버퍼·영상 렌더러·아크릴 등 재생 종합 설정입니다. 각 툴팁도 참고하세요.",
		L"播放综合设置：输出设备、缓冲、视频渲染器、亚克力等。各控件提示也有细节。",
		L"إعدادات التشغيل العامة: الجهاز والمخزن والعارض والأكريليك. راجع أيضاً التلميحات.",
		L"Общие настройки: устройство, буфер, рендерер, акрил. См. также подсказки.",
		L"Globale Einstellungen: Geraet, Puffer, Video-Renderer, Acryl. Auch Tooltips beachten.",
		L"Definições globais: dispositivo, buffer, renderer, acrílico. Veja também as dicas.",
		L"Globale instellingen: apparaat, buffer, renderer, acryl. Zie ook tooltips.",
		L"Ustawienia globalne: urządzenie, bufor, renderer, akryl. Zobacz też podpowiedzi.",
		L"Genel oynatma ayarları: aygıt, tampon, video, akrilik. İpuçlarına da bakın."));
	y += lh + 4;

	title(L, y, LL14(L"音声出力", L"Audio output", L"Sortie audio", L"Uscita audio", L"Salida de audio", L"오디오 출력", L"音频输出", L"إخراج الصوت",
		L"Аудиовыход", L"Audioausgabe", L"Saída de áudio", L"Audio-uitvoer", L"Wyjście audio", L"Ses çıkışı"));
	y += titleLh;
	body(L, y, LL14(L"・再生デバイス …… DirectSound 出力先", L"· Playback device …… DirectSound output target", L"· Périphérique …… sortie DirectSound", L"· Dispositivo …… uscita DirectSound",
		L"· Dispositivo …… salida DirectSound", L"· 재생 장치 …… DirectSound 출력", L"· 播放设备 …… DirectSound 输出", L"· جهاز التشغيل …… مخرج DirectSound",
		L"· Устройство …… вывод DirectSound", L"· Wiedergabegeraet …… DirectSound-Ausgabe", L"· Dispositivo …… saída DirectSound", L"· Apparaat …… DirectSound-uitvoer",
		L"· Urządzenie …… wyjście DirectSound", L"· Oynatma aygıtı …… DirectSound çıkışı")); y += lh;
	body(L, y, LL14(L"・割込間隔 …… バッファ処理の割り込み。短すぎると音飛びの原因", L"· Interrupt interval …… buffer timing; too short may glitch", L"· Intervalle …… timing tampon; trop court = saccades", L"· Intervallo …… timing buffer; troppo corto = salti",
		L"· Intervalo …… temporización; muy corto = cortes", L"· 인터럽트 간격 …… 버퍼 타이밍. 너무 짧으면 끊김", L"· 中断间隔 …… 缓冲时序；过短可能跳音", L"· فاصل المقاطعة …… توقيت المخزن؛ القصير جداً يقطع",
		L"· Интервал …… тайминг буфера; слишком мало — сбои", L"· Interrupt …… Puffer-Timing; zu kurz = Aussetzer", L"· Intervalo …… timing do buffer; curto demais falha", L"· Interrupt …… buffertiming; te kort = haperingen",
		L"· Interwał …… timing bufora; za krótki = przeskoki", L"· Kesme aralığı …… tampon zamanlaması; çok kısa = atlama")); y += lh;
	body(L, y, LL14(L"・MAXサンプル／24・32bit／アップスケール …… 出力形式の上限と変換", L"· Max sample / 24·32bit / upscale …… output format limits", L"· Échant. max / 24·32 bits / upscale …… format de sortie", L"· Camp. max / 24·32 bit / upscale …… formato uscita",
		L"· Muestreo máx. / 24·32 bits / upscale …… formato", L"· MAX 샘플/24·32bit/업스케일 …… 출력 형식", L"· 最大采样/24·32bit/升频 …… 输出格式上限", L"· أقصى عينات/24·32بت/ترقية …… حدود الخرج",
		L"· Макс. частота / 24·32 бит / апскейл …… формат", L"· Max. Rate / 24·32 Bit / Upscale …… Ausgabeformat", L"· Taxa máx. / 24·32 bits / upscale …… formato", L"· Max. sample / 24·32 bit / upscale …… formaat",
		L"· Maks. próbk. / 24·32 bit / upscale …… format", L"· Maks. örnek / 24·32 bit / upscale …… çıkış biçimi")); y += lh + 4;

	title(L, y, LL14(L"映像 / UI", L"Video / UI", L"Vidéo / UI", L"Video / UI", L"Vídeo / UI", L"영상 / UI", L"视频 / UI", L"فيديو / واجهة",
		L"Видео / UI", L"Video / UI", L"Vídeo / UI", L"Video / UI", L"Wideo / UI", L"Video / UI"));
	y += titleLh;
	body(L, y, LL14(L"・EVR …… Vista以降の既定映像レンダラ（Indeo等はOFF推奨）", L"· EVR …… default video renderer on Vista+ (OFF for Indeo etc.)", L"· EVR …… rendu vidéo par défaut (Vista+; OFF pour Indeo)", L"· EVR …… renderer predefinito (Vista+; OFF per Indeo)",
		L"· EVR …… renderizador predeterminado (Vista+; OFF para Indeo)", L"· EVR …… Vista+ 기본 렌더러(Indeo 등은 OFF)", L"· EVR …… Vista+ 默认渲染器（Indeo 等建议关）", L"· EVR …… عارض افتراضي (Vista+؛ أوقف لـ Indeo)",
		L"· EVR …… рендерер по умолчанию (Vista+; выкл. для Indeo)", L"· EVR …… Standard-Renderer (Vista+; AUS bei Indeo)", L"· EVR …… renderer padrão (Vista+; OFF para Indeo)", L"· EVR …… standaardrenderer (Vista+; UIT voor Indeo)",
		L"· EVR …… domyślny renderer (Vista+; WYŁ. dla Indeo)", L"· EVR …… varsayılan renderer (Vista+; Indeo için KAPALI)")); y += lh;
	body(L, y, LL14(L"・デスクトップコンポジション …… Aero。OFFでも映像がきれいになる場合あり", L"· Desktop composition …… Aero; OFF may still look clean without EVR", L"· Composition bureau …… Aero; OFF peut rester net sans EVR", L"· Composizione …… Aero; OFF puo restare nitido senza EVR",
		L"· Composición …… Aero; OFF puede verse bien sin EVR", L"· 데스크톱 컴포지션 …… Aero. OFF여도 화질이 좋을 수 있음", L"· 桌面合成 …… Aero；关闭时无 EVR 也可能清晰", L"· تركيب سطح المكتب …… Aero؛ قد يبدو جيداً بدون EVR",
		L"· Композиция …… Aero; без неё видео может быть чётким", L"· Desktop-Komposition …… Aero; ohne sie oft trotzdem klar", L"· Composição …… Aero; OFF pode ficar nítido sem EVR", L"· Compositie …… Aero; UIT kan nog steeds scherp zijn",
		L"· Kompozycja …… Aero; WYŁ. też może wyglądać dobrze", L"· Masaüstü birleşimi …… Aero; KAPALI da net olabilir")); y += lh;
	body(L, y, LL14(L"・アクリルモード …… Win10以降の半透明ぼかし背景", L"· Acrylic mode …… translucent blurred background on Win10+", L"· Mode acrylique …… fond flou translucide (Win10+)", L"· Modalita acrilica …… sfondo sfocato (Win10+)",
		L"· Modo acrílico …… fondo borroso (Win10+)", L"· 아크릴 모드 …… Win10+ 반투명 흐림 배경", L"· 亚克力模式 …… Win10+ 半透明模糊背景", L"· وضع الأكريليك …… خلفية ضبابية (Win10+)",
		L"· Акрил …… полупрозрачный размытый фон (Win10+)", L"· Acryl …… transparenter Unschaerfe-Hintergrund (Win10+)", L"· Acrílico …… fundo desfocado (Win10+)", L"· Acryl …… doorzichtige wazige achtergrond (Win10+)",
		L"· Akryl …… półprzezroczyste rozmyte tło (Win10+)", L"· Akrilik …… yarı saydam bulanık arka plan (Win10+)")); y += lh + 4;

	const int gx = L, gy = y, gw = min(340, rc.Width() - L * 2), gh = lh * 2 + 12;
	dc.FillSolidRect(gx, gy, gw, gh, RGB(245, 246, 250));
	dc.FillSolidRect(gx + 4, gy + 6, 40, gh - 12, RGB(70, 140, 90));
	dc.FillSolidRect(gx + 52, gy + 6, 44, gh - 12, RGB(180, 140, 60));
	dc.FillSolidRect(gx + 104, gy + 6, 48, gh - 12, RGB(70, 110, 160));
	dc.FillSolidRect(gx + 160, gy + 6, 52, gh - 12, RGB(150, 70, 70));
	dc.SetTextColor(RGB(255, 255, 255));
	dc.TextOut(gx + 10, gy + 8, L"DS");
	dc.TextOut(gx + 58, gy + 8, L"Buf");
	dc.TextOut(gx + 112, gy + 8, L"EVR");
	dc.TextOut(gx + 168, gy + 8, L"Acrylic");
	dc.FrameRect(CRect(gx, gy, gx + gw, gy + gh), &frameBrush);
	y = gy + gh + 6;

	title(L, y, LL14(L"倍率 / その他", L"Scale / Other", L"Échelle / Autres", L"Scala / Altro", L"Escala / Otros", L"배율 / 기타", L"倍率 / 其他", L"المقياس / أخرى",
		L"Масштаб / Прочее", L"Skalierung / Sonstiges", L"Escala / Outros", L"Schaal / Overig", L"Skala / Inne", L"Ölçek / Diğer"));
	y += titleLh;
	body(L, y, LL14(L"・SPC / mp3 / kpi 倍率 …… プラグイン出力ゲイン（EQで割れ抑制）", L"· SPC / mp3 / kpi scales …… plugin gain (EQ helps avoid clipping)", L"· Échelles SPC / mp3 / kpi …… gain plugin (EQ anti-clip)", L"· Scale SPC / mp3 / kpi …… gain plugin (EQ anti-clip)",
		L"· Escalas SPC / mp3 / kpi …… ganancia (EQ anti-clip)", L"· SPC/mp3/kpi 배율 …… 플러그인 게인(EQ가 클리핑 완화)", L"· SPC/mp3/kpi 倍率 …… 插件增益（EQ 防削波）", L"· مقاييس SPC/mp3/kpi …… كسب المكوّن (EQ يمنع القص)",
		L"· Множители SPC/mp3/kpi …… усиление (EQ против клиппинга)", L"· SPC/mp3/kpi-Skalierung …… Plugin-Gain (EQ gegen Clipping)", L"· Escalas SPC/mp3/kpi …… ganho (EQ anti-clip)", L"· SPC/mp3/kpi-schalen …… plugingain (EQ tegen clipping)",
		L"· Skale SPC/mp3/kpi …… wzmocnienie (EQ przeciw przesterom)", L"· SPC/mp3/kpi ölçekleri …… eklenti kazancı (EQ kırpmayı azaltır)")); y += lh;
	body(L, y, LL14(L"・表示間隔／コード間隔／スペアナ …… 描画負荷と見た目の調整", L"· Display / chord / spectrum …… tune draw load and look", L"· Affichage / accords / spectre …… charge et rendu", L"· Display / accordi / spettro …… carico e aspetto",
		L"· Pantalla / acordes / espectro …… carga y aspecto", L"· 표시/코드/스펙 …… 그리기 부하와 모양 조정", L"· 显示/和弦/频谱 …… 调整绘制负载与外观", L"· العرض/الأكورد/الطيف …… ضبط الحمل والمظهر",
		L"· Дисплей / аккорды / спектр …… нагрузка и вид", L"· Anzeige / Akkorde / Spektrum …… Last und Optik", L"· Exibição / acordes / espectro …… carga e aspeto", L"· Weergave / akkoorden / spectrum …… belasting en uiterlijk",
		L"· Wyświetlanie / akordy / widmo …… obciążenie i wygląd", L"· Görüntü / akor / spektrum …… yük ve görünüm")); y += lh;
	body(L, y, LL14(L"・関連付け …… 音声に加え動画(avi/mp4/mkv/mov/webm等)とプレイリストも登録", L"· File association …… audio plus video (avi/mp4/mkv/mov/webm…) and playlists", L"· Association …… audio + vidéo (avi/mp4/mkv…) et playlists", L"· Associazione …… audio + video (avi/mp4/mkv…) e playlist",
		L"· Asociación …… audio + vídeo (avi/mp4/mkv…) y listas", L"· 파일 연결 …… 음성+동영상(avi/mp4/mkv 등)+재생목록", L"· 文件关联 …… 音频+视频(avi/mp4/mkv等)+播放列表", L"· ربط الملفات …… صوت+فيديو+قوائم",
		L"· Связь файлов …… аудио + видео (avi/mp4/mkv…) и плейлисты", L"· Dateizuordnung …… Audio + Video (avi/mp4/mkv…) und Playlists", L"· Associação …… áudio + vídeo (avi/mp4/mkv…) e playlists", L"· Koppeling …… audio + video (avi/mp4/mkv…) en playlists",
		L"· Powiązanie …… audio + wideo (avi/mp4/mkv…) i playlisty", L"· İlişkilendirme …… ses + video (avi/mp4/mkv…) ve listeler")); y += lh;
	muted(L, y, LL14(
		L"OKで保存して閉じる。キャンセルは変更を破棄。各項目の細かい注意はツールチップにあります。",
		L"OK saves and closes. Cancel discards changes. Fine print is in the tooltips.",
		L"OK enregistre et ferme. Annuler annule. Détails dans les info-bulles.",
		L"OK salva e chiude. Annulla scarta. Dettagli nei tooltip.",
		L"OK guarda y cierra. Cancelar descarta. Detalles en tooltips.",
		L"OK는 저장 후 닫기. 취소는 변경 폐기. 세부 주의는 툴팁에 있습니다.",
		L"OK 保存并关闭。取消丢弃更改。细节见各提示。",
		L"موافق يحفظ ويغلق. إلغاء يتجاهل. التفاصيل في التلميحات.",
		L"OK сохраняет и закрывает. Отмена отбрасывает. Подробности в подсказках.",
		L"OK speichert und schliesst. Abbrechen verwirft. Details in Tooltips.",
		L"OK salva e fecha. Cancelar descarta. Detalhes nas dicas.",
		L"OK slaat op en sluit. Annuleren verwerpt. Details in tooltips.",
		L"OK zapisuje i zamyka. Anuluj odrzuca. Szczegóły w podpowiedziach.",
		L"Tamam kaydedip kapatır. İptal değişiklikleri atar. Ayrıntılar ipuçlarında."));

	dc.SelectObject(oldFont);
	CCC_GdiHelpEndPaint(hp);
}

} // namespace

static void ReleaseRenderGrassBackdrop(){
	if (!renderbase)
		return;
	CImageBase* p = renderbase;
	renderbase = NULL;
	if (p->GetSafeHwnd())
		p->DestroyWindow();
	else
		delete p;
}

static void SyncRenderGrassBackdrop(CRender* pRender);

extern int sek;
extern void DoEvent();
extern int fade1;
extern int wavbit_sample_Hz, wavchannel, wavsam_depth;
extern LPDIRECTSOUND8 m_ds;
extern LPDIRECTSOUNDBUFFER m_dsb1;
extern LPDIRECTSOUNDBUFFER8 m_dsb;
extern LPDIRECTSOUNDBUFFER m_p;

// mp3.h と同一（Render.cpp から mp3.h を include しない: CWread 内が ExtractI4 等に依存）
#define RENDER_DS_BUFSZ ((UINT)10240 * 6 / 2)
#define RENDER_DS_BUFNUM 5

static void RenderRecreateSecondarySound(COggDlg* og)
{
	if (!og || !og->m_hWnd || !m_ds)
		return;
	std::lock_guard<std::mutex> guard(cl2);
	ConfigurePlaybackOutputAndUpscaler();
	ResetAudioUpscalerPipeline();
	sek = 1;
	int dsTryRate = g_ds_pcm_rate;
	static const GUID GUID_SUBTYPE_PCM = { 0x00000001, 0x0000, 0x0010,{ 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };

	WAVEFORMATEX wfx1;
	wfx1.wFormatTag = WAVE_FORMAT_PCM;
	wfx1.nChannels = (WORD)((g_ds_pcm_ch <= 2) ? g_ds_pcm_ch : 2);
	wfx1.nSamplesPerSec = dsTryRate;
	wfx1.wBitsPerSample = (WORD)g_ds_pcm_bits;
	wfx1.nBlockAlign = (WORD)(wfx1.nChannels * wfx1.wBitsPerSample / 8);
	wfx1.nAvgBytesPerSec = (DWORD)((DWORD)wfx1.nSamplesPerSec * (DWORD)wfx1.nBlockAlign);
	wfx1.cbSize = 0;

	DWORD targetSpeakers = (DWORD)DirectSoundChannelMaskForOutput(g_ds_pcm_ch, savedata.speaker_layout);
	WAVEFORMATEXTENSIBLE wfx = {};
	wfx.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
	wfx.Format.nChannels = (WORD)g_ds_pcm_ch;
	wfx.Format.nSamplesPerSec = dsTryRate;
	wfx.Format.wBitsPerSample = (WORD)g_ds_pcm_bits;
	wfx.Format.nBlockAlign = (WORD)(wfx.Format.wBitsPerSample / 8 * wfx.Format.nChannels);
	wfx.Format.nAvgBytesPerSec = (DWORD)(wfx.Format.nSamplesPerSec * wfx.Format.nBlockAlign);
	wfx.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
	wfx.dwChannelMask = targetSpeakers;
	wfx.SubFormat = GUID_SUBTYPE_PCM;

	fade1 = 0;
	for (;;) {
		DSBUFFERDESC dsbd;
		ZeroMemory(&dsbd, sizeof(DSBUFFERDESC));
		dsbd.dwSize = sizeof(DSBUFFERDESC);
		dsbd.dwFlags = DSBCAPS_CTRLPOSITIONNOTIFY | DSBCAPS_LOCSOFTWARE | DSBCAPS_GLOBALFOCUS | DSBCAPS_GETCURRENTPOSITION2 | DSBCAPS_CTRLVOLUME;
		dsbd.dwBufferBytes = g_ds_buffer_bytes;
		wfx1.nSamplesPerSec = dsTryRate;
		wfx1.nAvgBytesPerSec = (DWORD)wfx1.nSamplesPerSec * (DWORD)wfx1.nBlockAlign;
		wfx.Format.nSamplesPerSec = dsTryRate;
		wfx.Format.nAvgBytesPerSec = (DWORD)(wfx.Format.nSamplesPerSec * wfx.Format.nBlockAlign);
		if (g_ds_pcm_ch > 2)
			dsbd.lpwfxFormat = (LPWAVEFORMATEX)&wfx;
		else
			dsbd.lpwfxFormat = &wfx1;

		og->ReleaseDXSound();
		if (og->WASAPIInit() == 0)
			og->init(og->m_hWnd, dsTryRate);
		HRESULT r = m_ds->CreateSoundBuffer(&dsbd, &m_dsb1, NULL);
		if (m_dsb1 == NULL || m_p == NULL) {
			dsTryRate -= 1000;
			if (dsTryRate <= 0) {
				og->ReleaseDXSound();
				if (og->WASAPIInit() == 0)
					og->init(og->m_hWnd, 44100);
				return;
			}
			wfx.Format.nSamplesPerSec = dsTryRate;
			wfx.Format.nAvgBytesPerSec = (DWORD)(wfx.Format.nSamplesPerSec * wfx.Format.nBlockAlign);
			wfx1.nSamplesPerSec = dsTryRate;
			wfx1.nAvgBytesPerSec = wfx1.nSamplesPerSec * wfx1.nBlockAlign;
			continue;
		}
		int i;
		for (i = 0; i < 10; i++) {
			r = m_dsb1->QueryInterface(IID_IDirectSoundBuffer8, (void**)&m_dsb);
			if (m_dsb == NULL) { DoEvent(); Sleep(100); continue; }
			break;
		}
		if (m_dsb == NULL)
			return;
		g_ds_pcm_rate = dsTryRate;
		{
			int srcBits = abs(wavsam_depth);
			if (wavsam_depth < 0)
				srcBits = 16;
			if (!(srcBits == 8 || srcBits == 16 || srcBits == 24 || srcBits == 32))
				srcBits = 16;
			g_audioUpscaler.Configure(wavbit_sample_Hz, wavchannel, srcBits, g_ds_pcm_rate, g_ds_pcm_ch, g_ds_pcm_bits);
			g_pcm_upscale_active = g_audioUpscaler.IsActive() ? 1 : 0;
		}
		g_audioUpscaler.Reset();
		oldw = 0;
		m_dsb->Play(0, 0, DSBPLAY_LOOPING);
		return;
	}
}

void MpRecreatePlaybackOutput()
{
	extern COggDlg* og;
	RenderRecreateSecondarySound(og);
}

/////////////////////////////////////////////////////////////////////////////
// CRender ダイアログ

IMPLEMENT_DYNAMIC(CRender, CCustomBlurDialogExBase)
CRender::CRender(CWnd* pParent /*=NULL*/)
	: CCustomBlurDialogExBase(CRender::IDD, pParent)
{
	//{{AFX_DATA_INIT(CRender)
		// メモ - ClassWizard はこの位置にマッピング用のマクロを追加または削除します。
	//}}AFX_DATA_INIT
}



void CRender::DoDataExchange(CDataExchange* pDX)
{
	CCustomBlurDialogExBase::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(CRender)
	DDX_Control(pDX, IDC_COMBO1, m_1);
	//}}AFX_DATA_MAP
	DDX_Control(pDX, IDC_RD_HELP, m_help);
	DDX_Control(pDX, IDC_CHECK1, m_evr);	DDX_Control(pDX, IDC_CHECK2, m_con);
	DDX_Control(pDX, IDC_CHECK3, m_a);
	DDX_Control(pDX, IDC_CHECK27, m_ffd);
	DDX_Control(pDX, IDCANCEL2, m_l);
	DDX_Control(pDX, IDC_CHECK30, m_vob);
	DDX_Control(pDX, IDC_CHECK31, m_haali);
	DDX_Control(pDX, IDC_CHECK32, m_spc2x);
	DDX_Control(pDX, IDC_CHECK33, m_spc4x);
	DDX_Control(pDX, IDC_CHECK34, m_spc8x);
	DDX_Control(pDX, IDC_CHECK35, m_spc1x);
	DDX_Control(pDX, IDC_CHECK36, m_spc16x);
	DDX_Control(pDX, IDC_CHECK40, m_mp31);
	DDX_Control(pDX, IDC_CHECK37, m_mp315);
	DDX_Control(pDX, IDC_CHECK38, m_mp32);
	DDX_Control(pDX, IDC_CHECK39, m_mp325);
	DDX_Control(pDX, IDC_CHECK41, m_mp33);
	DDX_Control(pDX, IDC_CHECK45, m_kpi10);
	DDX_Control(pDX, IDC_CHECK42, m_kpi15);
	DDX_Control(pDX, IDC_CHECK43, m_kpi20);
	DDX_Control(pDX, IDC_CHECK44, m_kpi25);
	DDX_Control(pDX, IDC_CHECK46, m_kpi30);
	DDX_Control(pDX, IDCANCEL3, m_kpi);
	DDX_Control(pDX, IDC_CHECK47, m_mp3orig);
	DDX_Control(pDX, IDC_CHECK48, m_audiost);
	DDX_Control(pDX, IDC_CHECK49, m_24);
	DDX_Control(pDX, IDC_CHECK50, m_m4a);
	DDX_Control(pDX, IDC_CHECK51, m_32bit);
	DDX_Control(pDX, IDC_SLIDER3, m_ms);
	DDX_Control(pDX, IDC_STATIC9, m_ms2);
	DDX_Control(pDX, IDC_SLIDER5, m_hyouji2);
	DDX_Control(pDX, IDC_STATIC10, m_hyouji3);
	DDX_Control(pDX, IDC_SLIDER_EQCODE, m_eqCode);
	DDX_Control(pDX, IDC_STATIC_EQCODE_MS, m_eqCodeMs);
	DDX_Control(pDX, IDC_COMBO2, m_soundlist);
	DDX_Control(pDX, IDC_COMBO_MICDEV, m_miclist);
	DDX_Control(pDX, IDC_STATIC_R_MIC, m_micLabel);
	DDX_Control(pDX, IDC_BUTTON1, m_ao);
	DDX_Control(pDX, IDC_COMBO3, m_Hz);
	DDX_Control(pDX, IDC_STATIC12, m_wup);
	DDX_Control(pDX, IDC_SLIDER6, w_wups);
	DDX_Control(pDX, IDC_CHECK52, m_speana);
	DDX_Control(pDX, IDC_COMBO4, m_speana_num);
	DDX_Control(pDX, IDC_CHECK_lrc, m_netlrc);
	DDX_Control(pDX, IDOK, m_okdummy);
	DDX_Control(pDX, IDCANCEL5, m_kanren);
	DDX_Control(pDX, IDCANCEL, m_canceldummy);
	DDX_Control(pDX, IDC_COMBO_LANG, m_comboLang);
	DDX_Control(pDX, IDC_CHECK_UPSCALE, m_upscale);
	DDX_Control(pDX, IDC_COMBO_SPEAKER, m_speaker);
}



BEGIN_MESSAGE_MAP(CRender, CCustomBlurDialogExBase)
	//{{AFX_MSG_MAP(CRender)
	//}}AFX_MSG_MAP
	ON_BN_CLICKED(IDC_RD_HELP, &CRender::OnBnClickedHelp)
	ON_WM_SIZE()
	ON_WM_DESTROY()
	ON_BN_CLICKED(IDCANCEL2, &CRender::OnBnClickedCancel2)	ON_BN_CLICKED(IDC_CHECK32, &CRender::Onspc2x)
	ON_BN_CLICKED(IDC_CHECK33, &CRender::Onspc4x)
	ON_BN_CLICKED(IDC_CHECK34, &CRender::Onspc8x)
	ON_BN_CLICKED(IDC_CHECK35, &CRender::Onspc1x)
	ON_BN_CLICKED(IDC_CHECK36, &CRender::Onspc16x)
	ON_BN_CLICKED(IDC_CHECK40, &CRender::Onmp31)
	ON_BN_CLICKED(IDC_CHECK37, &CRender::Onmp315)
	ON_BN_CLICKED(IDC_CHECK38, &CRender::Onmp32)
	ON_BN_CLICKED(IDC_CHECK39, &CRender::Onmp325)
	ON_BN_CLICKED(IDC_CHECK41, &CRender::Onmp33)
	ON_BN_CLICKED(IDC_CHECK45, &CRender::Onkpi10)
	ON_BN_CLICKED(IDC_CHECK42, &CRender::Onkpi15)
	ON_BN_CLICKED(IDC_CHECK43, &CRender::Onkpi20)
	ON_BN_CLICKED(IDC_CHECK44, &CRender::Onkpi25)
	ON_BN_CLICKED(IDC_CHECK46, &CRender::Onkpi30)
	ON_BN_CLICKED(IDCANCEL3, &CRender::Onkpi)
	ON_BN_CLICKED(IDC_FONT, &CRender::OnFontMain)
	ON_BN_CLICKED(IDC_FONT2, &CRender::OnFontList)
	ON_BN_CLICKED(IDOK, &CRender::OnBnClickedOk)
	ON_BN_CLICKED(IDC_CHECK49, &CRender::OnBnClicked24bit)
	ON_BN_CLICKED(IDC_CHECK50, &CRender::OnBnClickedCheck50)
	ON_BN_CLICKED(IDCANCEL4, &CRender::OnBnClickedCancel4)
	ON_WM_TIMER()
	ON_CBN_SELCHANGE(IDC_COMBO2, &CRender::OnCbnSelchangeCombo2)
	ON_CBN_SELCHANGE(IDC_COMBO_MICDEV, &CRender::OnCbnSelchangeMic)
	ON_BN_CLICKED(IDC_BUTTON1, &CRender::OnBnClickedButton1)
	ON_CBN_SELCHANGE(IDC_COMBO3, &CRender::OnCbnSelchangeCombo3)
	ON_CBN_SELCHANGE(IDC_COMBO_SPEAKER, &CRender::OnCbnSelchangeSpeaker)
	ON_BN_CLICKED(IDC_CHECK_UPSCALE, &CRender::OnBnClickedCheckUpscale)
	ON_BN_CLICKED(IDC_CHECK51, &CRender::OnBnClicked32bit)
	ON_BN_CLICKED(IDC_CHECK3, &CRender::OnBnClickedCheck3)
	ON_WM_CTLCOLOR()
	ON_WM_CREATE()
	ON_WM_MOVING()
	ON_BN_CLICKED(IDCANCEL, &CRender::OnBnClickedCancel)
	ON_BN_CLICKED(IDC_CHECK52, &CRender::OnBnClickedCheck52)
	ON_CBN_EDITCHANGE(IDC_COMBO4, &CRender::OnCbnEditchangeCombo4)
	ON_CBN_SELCHANGE(IDC_COMBO4, &CRender::OnCbnSelchangeCombo4)
	ON_BN_CLICKED(IDCANCEL5, &CRender::OnBnClickedCancel5)
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CRender メッセージ ハンドラ
CComboBox *sl;
GUID slg[200];
int slgc;
CString sls[200];
DWORD samp[] = { 11025, 12000, 22050, 24000, 44100, 48000, 96000, 192000, 384000, 768000, 1536000, 3072000 };
extern COggDlg* og;


BOOL CRender::OnInitDialog()
{
	CCustomBlurDialogExBase::OnInitDialog();
	m_help.SetWindowText(L"?");
	m_help.SetFlat(TRUE);
	m_help.SetGradation(RGB(255, 245, 220), RGB(240, 210, 160), 0, TRUE);
	LayoutHelpBtn();

	m_bakSoundGuid = savedata.soundguid;	m_bakSoundCur = savedata.soundcur;
	m_bakSamples = savedata.samples;
	m_bakUpscale = savedata.upscale_enable;
	m_bakSpeaker = savedata.speaker_layout;
	m_bakBit24 = savedata.bit24;
	m_bakBit32 = savedata.bit32;
	m_bakAero = savedata.aero;
	_tcscpy(m_bakFont1, savedata.font1);
	_tcscpy(m_bakFont2, savedata.font2);

	SetWindowText(LL14(L"レンダリング選択", L"Rendering Options", L"Options de rendu", L"Opzioni di rendering", L"Opciones de renderizado", L"렌더링 옵션", L"渲染选项", L"خيارات العرض", L"Параметры рендеринга", L"Rendering-Optionen", L"Opções de renderização", L"Renderopties", L"Opcje renderowania", L"Render seçenekleri"));
	SetDlgItemText(IDOK, LL14(L"OK", L"OK", L"OK", L"OK", L"OK", L"확인", L"确定", L"موافق", L"OK", L"OK", L"OK", L"OK", L"OK", L"Tamam"));
	SetDlgItemText(IDCANCEL, LL14(L"キャンセル", L"Cancel", L"Annuler", L"Annulla", L"Cancelar", L"취소", L"取消", L"إلغاء", L"Отмена", L"Abbrechen", L"Cancelar", L"Annuleren", L"Anuluj", L"İptal"));
	SetDlgItemText(IDCANCEL2, LL14(L"DirectShowフィルタ一覧", L"DirectShow Filter List", L"Liste des filtres DirectShow", L"Elenco filtri DirectShow", L"Lista de filtros DirectShow", L"DirectShow 필터 목록", L"DirectShow 过滤器列表", L"قائمة مرشحات DirectShow", L"Список фильтров DirectShow", L"DirectShow-Filterliste", L"Lista de filtros DirectShow", L"DirectShow-filterlijst", L"Lista filtrów DirectShow", L"DirectShow Filtre Listesi"));
	SetDlgItemText(IDCANCEL3, LL14(L"kpi一覧", L"kpi List", L"Liste kpi", L"Elenco kpi", L"Lista kpi", L"kpi 목록", L"kpi 列表", L"قائمة kpi", L"Список kpi", L"kpi-Liste", L"Lista kpi", L"kpi-lijst", L"Lista kpi", L"kpi Listesi"));
	SetDlgItemText(IDCANCEL5, LL14(L"関連付け", L"File Association", L"Association de fichiers", L"Associazione file", L"Asociación de archivos", L"파일 연결", L"文件关联", L"ربط الملفات", L"Связь файлов", L"Dateizuordnung", L"Associação de ficheiros", L"Bestandskoppeling", L"Powiazanie plików", L"Dosya ilişkilendirme"));
	SetDlgItemText(IDC_CHECK1, LL14(L"デフォルトでEVR使用(Vista以降)", L"Default EVR use (Vista+)", L"EVR par défaut (Vista+)", L"Uso EVR predefinito (Vista+)", L"Uso EVR predeterminado (Vista+)", L"기본 EVR 사용(Vista+)", L"默认使用 EVR（Vista+）", L"استخدام EVR افتراضي (Vista+)", L"Использовать EVR по умолчанию (Vista+)", L"EVR standardmäßig (Vista+)", L"Usar EVR por defeito (Vista+)", L"Standaard EVR (Vista+)", L"Domyślne EVR (Vista+)", L"Varsayılan EVR kullan (Vista+)"));
	SetDlgItemText(IDC_CHECK2, LL14(L"デスクトップコンポジションを使用する", L"Use desktop composition", L"Utiliser la composition du bureau", L"Usa composizione desktop", L"Usar composición de escritorio", L"데스크톱 컴포지션 사용", L"使用桌面合成", L"استخدام تركيب سطح المكتب", L"Использовать композицию рабочего стола", L"Desktop-Komposition verwenden", L"Usar composição do ambiente de trabalho", L"Bureaubladcompositie gebruiken", L"Użyj kompozycji pulpitu", L"Masaüstü birleşimini kullan"));
	SetDlgItemText(IDC_CHECK3, LL14(L"アクリルモードを使用する", L"Use acrylic mode", L"Utiliser le mode acrylique", L"Usa modalità acrilica", L"Usar modo acrílico", L"아크릴 모드 사용", L"使用亚克力模式", L"استخدام وضع الأكريليك", L"Использовать акриловый режим", L"Akrylmodus verwenden", L"Usar modo acrílico", L"Acrylmodus gebruiken", L"Użyj trybu akrylowego", L"Akrilik modu kullan"));
	SetDlgItemText(IDC_CHECK27, LL14(L"ffdshow使用", L"Use ffdshow", L"Utiliser ffdshow", L"Usa ffdshow", L"Usar ffdshow", L"ffdshow 사용", L"使用 ffdshow", L"استخدام ffdshow", L"Использовать ffdshow", L"ffdshow verwenden", L"Usar ffdshow", L"ffdshow gebruiken", L"Użyj ffdshow", L"ffdshow kullan"));
	SetDlgItemText(IDC_CHECK30, LL14(L"vobやdatはHaaliスキップ対応とする", L"vob/dat skip Haali by default", L"vob/dat ignorer Haali par défaut", L"vob/dat salta Haali per default", L"vob/dat omitir Haali por defecto", L"vob/dat 기본 Haali 건너뛰기", L"vob/dat 默认跳过 Haali", L"تخطي Haali لـ vob/dat افتراضيًا", L"vob/dat пропускать Haali по умолчанию", L"vob/dat Haali standardmäßig überspringen", L"vob/dat omitir Haali por defeito", L"vob/dat standaard Haali overslaan", L"vob/dat domyślnie pomiń Haali", L"vob/dat varsayılan Haali atla"));
	SetDlgItemText(IDC_CHECK31, LL14(L"Haaliを使用しない", L"Do not use Haali", L"Ne pas utiliser Haali", L"Non usare Haali", L"No usar Haali", L"Haali 사용 안 함", L"不使用 Haali", L"عدم استخدام Haali", L"Не использовать Haali", L"Haali nicht verwenden", L"Não usar Haali", L"Haali niet gebruiken", L"Nie używaj Haali", L"Haali kullanma"));
	SetDlgItemText(IDC_CHECK47, LL14(L"mp3 オリジナルデコーダを使う", L"Use original mp3 decoder", L"Utiliser le décodeur mp3 d'origine", L"Usa decoder mp3 originale", L"Usar decodificador mp3 original", L"원본 mp3 디코더 사용", L"使用原始 mp3 解码器", L"استخدام وحدة فك ترميز mp3 الأصلية", L"Использовать оригинальный декодер mp3", L"Originalen MP3-Decoder verwenden", L"Usar descodificador mp3 original", L"Originele mp3-decoder gebruiken", L"Użyj oryginalnego dekodera mp3", L"Orijinal mp3 çözücü kullan"));
	SetDlgItemText(IDC_CHECK48, LL14(L"複数音声の動画の時は音声選択画面を出す", L"Show audio selection for multi-audio video", L"Afficher la sélection audio pour vidéo multi-audio", L"Mostra selezione audio per video multi-audio", L"Mostrar selección de audio para vídeo multi-audio", L"다중 오디오 동영상 재생 시 오디오 선택 화면 표시", L"多音轨视频时显示音频选择", L"عرض اختيار الصوت للفيديو متعدد الصوت", L"Показывать выбор аудио для мультиаудио видео", L"Tonauswahl bei Multi-Audio-Video anzeigen", L"Mostrar seleção de áudio para vídeo multi-áudio", L"Audio-selectie tonen voor multi-audiovideo", L"Pokaż wybór dźwięku dla wideo wielodźwiękowego", L"Çoklu sesli videoda ses seçimini göster"));
	SetDlgItemText(IDC_CHECK49, LL14(L"24bit使用", L"Use 24bit", L"Utiliser 24 bits", L"Usa 24 bit", L"Usar 24 bits", L"24bit 사용", L"使用 24 位", L"استخدام 24 بت", L"Использовать 24 бит", L"24 Bit verwenden", L"Usar 24 bits", L"24 bit gebruiken", L"Użyj 24 bitów", L"24 bit kullan"));
	SetDlgItemText(IDC_CHECK50, LL14(L"m4aを内蔵エンジンで演奏する", L"Play m4a with built-in engine", L"Lire m4a avec moteur intégré", L"Riproduci m4a con motore integrato", L"Reproducir m4a con motor integrado", L"내장 엔진으로 m4a 재생", L"使用内置引擎播放 m4a", L"تشغيل m4a بمحرك مدمج", L"Воспроизводить m4a встроенным движком", L"m4a mit integrierter Engine abspielen", L"Reproduzir m4a com motor integrado", L"m4a afspelen met ingebouwde engine", L"Odtwarzaj m4a wbudowanym silnikiem", L"Yerleşik motorla m4a çal"));
	SetDlgItemText(IDC_CHECK51, LL14(L"32bit使用", L"Use 32bit", L"Utiliser 32 bits", L"Usa 32 bit", L"Usar 32 bits", L"32bit 사용", L"使用 32 位", L"استخدام 32 بت", L"Использовать 32 бит", L"32 Bit verwenden", L"Usar 32 bits", L"32 bit gebruiken", L"Użyj 32 bitów", L"32 bit kullan"));
	SetDlgItemText(IDC_CHECK52, LL14(L"スペアナのモード切り替える", L"Switch spectrum analyzer mode", L"Changer le mode analyseur de spectre", L"Cambia modalità analizzatore di spettro", L"Cambiar modo del analizador de espectro", L"스펙트럼 분석기 모드 전환", L"切换频谱分析仪模式", L"تبديل وضع محلل الطيف", L"Переключить режим анализатора спектра", L"Spektrumanalysator-Modus wechseln", L"Mudar modo do analisador de espetro", L"Modus spectrumanalyser wijzigen", L"Przełącz tryb analizatora widma", L"Spektrum analizörü modunu değiştir"));
	SetDlgItemText(IDC_CHECK_lrc, LL14(L"lrcをネットから取得する(LRCLib/NetEase等による)", L"Fetch lrc from network (LRCLib/NetEase etc.)", L"Récupérer les paroles depuis le réseau (LRCLib/NetEase etc.)", L"Recupera lrc da rete (LRCLib/NetEase ecc.)", L"Obtener lrc de la red (LRCLib/NetEase etc.)", L"네트워크에서 lrc 가져오기 (LRCLib/NetEase 등)", L"从网络获取 lrc（LRCLib/NetEase 等）", L"جلب lrc من الشبكة (LRCLib/NetEase إلخ)", L"Загружать lrc из сети (LRCLib/NetEase и т.д.)", L"Lrc aus dem Netz laden (LRCLib/NetEase usw.)", L"Obter lrc da rede (LRCLib/NetEase etc.)", L"Lrc ophalen van netwerk (LRCLib/NetEase etc.)", L"Pobierz lrc z sieci (LRCLib/NetEase itp.)", L"Ağdan lrc al (LRCLib/NetEase vb.)"));
	SetDlgItemText(IDC_BUTTON1, LL14(L"碧の軌跡用t_bgm._dt", L"t_bgm._dt for Ao no Kiseki", L"t_bgm._dt pour Ao no Kiseki", L"t_bgm._dt per Ao no Kiseki", L"t_bgm._dt para Ao no Kiseki", L"아오의 궤적용 t_bgm._dt", L"碧之轨迹用 t_bgm._dt", L"t_bgm._dt لـ Ao no Kiseki", L"t_bgm._dt для Ao no Kiseki", L"t_bgm._dt für Ao no Kiseki", L"t_bgm._dt para Ao no Kiseki", L"t_bgm._dt voor Ao no Kiseki", L"t_bgm._dt dla Ao no Kiseki", L"Ao no Kiseki için t_bgm._dt"));
	SetDlgItemText(IDC_FONT, LL14(L"メイン用フォント", L"Main font", L"Police principale", L"Carattere principale", L"Fuente principal", L"메인 글꼴", L"主字体", L"الخط الرئيسي", L"Основной шрифт", L"Hauptschriftart", L"Fonte principal", L"Hoofdlettertype", L"Czcionka główna", L"Ana yazı tipi"));
	SetDlgItemText(IDC_FONT2, LL14(L"リスト用フォント", L"List font", L"Police liste", L"Carattere lista", L"Fuente de lista", L"목록 글꼴", L"列表字体", L"خط القائمة", L"Шрифт списка", L"Listenschriftart", L"Fonte da lista", L"Lijstlettertype", L"Czcionka listy", L"Liste yazı tipi"));
	SetDlgItemText(IDC_STATIC_LANG, LL14(L"言語", L"Language", L"Langue", L"Lingua", L"Idioma", L"언어", L"语言", L"اللغة", L"Язык", L"Sprache", L"Idioma", L"Taal", L"Język", L"Dil"));
	SetDlgItemText(IDC_STATIC_R_BUF, LL14(L"割込間隔", L"Interrupt interval", L"Intervalle d'interruption", L"Intervallo di interruzione", L"Intervalo de interrupción", L"인터럽트 간격", L"中断间隔", L"فاصل المقاطعة", L"Интервал прерывания", L"Interrupt-Intervall", L"Intervalo de interrupção", L"Interrupt-interval", L"Interwał przerwania", L"Kesme aralığı"));
	SetDlgItemText(IDC_STATIC_R_MP3, LL14(L"mp3音量", L"mp3 volume", L"Volume mp3", L"Volume mp3", L"Volumen mp3", L"mp3 볼륨", L"mp3 音量", L"حجم mp3", L"Громкость mp3", L"MP3-Lautstärke", L"Volume mp3", L"mp3-volume", L"Głośność mp3", L"mp3 sesi"));
	SetDlgItemText(IDC_STATIC_R_KPI, LL14(L"その他のkpi", L"Other kpi", L"Autres kpi", L"Altri kpi", L"Otros kpi", L"기타 kpi", L"其他 kpi", L"kpi أخرى", L"Другие kpi", L"Andere kpi", L"Outros kpi", L"Andere kpi", L"Inne kpi", L"Diğer kpi"));
	SetDlgItemText(IDC_STATIC_R_DISP, LL14(L"表示間隔", L"Display interval", L"Intervalle d'affichage", L"Intervallo display", L"Intervalo de pantalla", L"표시 간격", L"显示间隔", L"فاصل العرض", L"Интервал отображения", L"Anzeigeintervall", L"Intervalo de exibição", L"Weergave-interval", L"Interwał wyświetlania", L"Görüntüleme aralığı"));
	SetDlgItemText(IDC_STATIC_R_CODE, LL14(L"コード間隔", L"Chord interval", L"Intervalle accords", L"Intervallo accordi", L"Intervalo de acordes", L"코드 간격", L"和弦间隔", L"فاصل الأكورد", L"Интервал аккордов", L"Akkordintervall", L"Intervalo de acordes", L"Akkoordinterval", L"Interwał akordów", L"Akor aralığı"));
	SetDlgItemText(IDC_STATIC_R_DEV, LL14(L"再生デバイス", L"Playback device", L"Périphérique lecture", L"Dispositivo riproduzione", L"Dispositivo reproducción", L"재생 장치", L"播放设备", L"جهاز التشغيل", L"Устройство воспроизведения", L"Wiedergabegerät", L"Dispositivo reprodução", L"Afspeelapparaat", L"Urządzenie odtwarzania", L"Oynatma cihazı"));
	SetDlgItemText(IDC_STATIC_R_MIC, LL14(L"マイク(録音)", L"Microphone (record)", L"Micro (enregistrement)", L"Microfono (registrazione)", L"Microfono (grabacion)", L"마이크(녹음)", L"麦克风(录音)", L"ميكروفون (تسجيل)", L"Микрофон (запись)", L"Mikrofon (Aufnahme)", L"Microfone (gravacao)", L"Microfoon (opname)", L"Mikrofon (nagrywanie)", L"Mikrofon (kayit)"));
	SetDlgItemText(IDC_STATIC_R_SAMP, LL14(L"MAXサンプルレート：", L"MAX sample rate:", L"Freq. échantillonnage max:", L"Freq. campionamento max:", L"Frec. muestreo máx.:", L"최대 샘플레이트:", L"最大采样率：", L"معدل العينات الأقصى:", L"Макс. частота дискретизации:", L"Max. Abtastrate:", L"Taxa amostragem máx.:", L"Max. samplefrequentie:", L"Maks. częstotliwość:", L"Maks. örnekleme oranı:"));
	SetDlgItemText(IDC_STATIC_R_SPEANA, LL14(L"スペアナ倍率", L"Spectrum scale", L"Échelle spectre", L"Scala spettro", L"Escala espectro", L"스펙트럼 배율", L"频谱倍率", L"مقياس الطيف", L"Масштаб спектра", L"Spektrumskala", L"Escala espectro", L"Spectrumschaal", L"Skala widma", L"Spektrum ölçeği"));
	SetDlgItemText(IDC_STATIC_R_SPC, LL14(L".SPC,.HES音量(kpi)", L".SPC,.HES volume(kpi)", L"Volume .SPC,.HES (kpi)", L"Volume .SPC,.HES (kpi)", L"Volumen .SPC,.HES (kpi)", L".SPC,.HES 볼륨(kpi)", L".SPC,.HES 音量(kpi)", L"حجم .SPC,.HES (kpi)", L"Громкость .SPC,.HES (kpi)", L".SPC,.HES-Lautstärke (kpi)", L"Volume .SPC,.HES (kpi)", L".SPC,.HES-volume (kpi)", L"Głośność .SPC,.HES (kpi)", L".SPC,.HES sesi (kpi)"));
	SetDlgItemText(IDC_STATIC_R_BIT, LL14(L"演奏bit深度：", L"Playback bits:", L"Bits lecture:", L"Bit riproduzione:", L"Bits reproducción:", L"재생 비트:", L"播放位深：", L"بت التشغيل:", L"Битность воспроизведения:", L"Wiedergabe-Bits:", L"Bits reprodução:", L"Afspeelbits:", L"Bity odtwarzania:", L"Oynatma bitleri:"));
	SetDlgItemText(IDC_STATIC_R_SPEAKER, LL14(L"出力チャンネル", L"Output channels", L"Canaux de sortie", L"Canali di uscita", L"Canales de salida", L"출력 채널", L"输出声道", L"قنوات الإخراج", L"Выходные каналы", L"Ausgabekanäle", L"Canais de saída", L"Uitgangskanalen", L"Kanały wyjściowe", L"Çıkış kanalları"));
	SetDlgItemText(IDC_CHECK_UPSCALE, LL14(L"アップスケール出力", L"Upscale output", L"Sortie upscalée", L"Upscaling in uscita", L"Salida con upscaling", L"업스케일 출력", L"升频输出", L"تحسين الدقة للإخراج", L"Апскейл вывода", L"Upscale-Ausgabe", L"Saída com upscaling", L"Upscale uitvoer", L"Wyjście upscaling", L"Yükseltmeli çıkış"));
	SetDlgItemText(IDC_STATIC12, LL14(L"倍", L"x", L"x", L"x", L"x", L"x", L"倍", L"x", L"x", L"x", L"x", L"x", L"x", L"x"));
	// SPC volume checkboxes (x1, x2, x4, x8, x16)
	SetDlgItemText(IDC_CHECK35, LL14(L"等倍", L"1x", L"1x", L"1x", L"1x", L"1x", L"1倍", L"1x", L"1x", L"1x", L"1x", L"1x", L"1x", L"1x"));
	SetDlgItemText(IDC_CHECK32, LL14(L"2倍", L"2x", L"2x", L"2x", L"2x", L"2x", L"2倍", L"2x", L"2x", L"2x", L"2x", L"2x", L"2x", L"2x"));
	SetDlgItemText(IDC_CHECK33, LL14(L"4倍", L"4x", L"4x", L"4x", L"4x", L"4x", L"4倍", L"4x", L"4x", L"4x", L"4x", L"4x", L"4x", L"4x"));
	SetDlgItemText(IDC_CHECK34, LL14(L"8倍", L"8x", L"8x", L"8x", L"8x", L"8x", L"8倍", L"8x", L"8x", L"8x", L"8x", L"8x", L"8x", L"8x"));
	SetDlgItemText(IDC_CHECK36, LL14(L"16倍", L"16x", L"16x", L"16x", L"16x", L"16x", L"16倍", L"16x", L"16x", L"16x", L"16x", L"16x", L"16x", L"16x"));
	// MP3 volume checkboxes (1x, 1.5x, 2x, 2.5x, 3x)
	SetDlgItemText(IDC_CHECK40, LL14(L"等倍", L"1x", L"1x", L"1x", L"1x", L"1x", L"1倍", L"1x", L"1x", L"1x", L"1x", L"1x", L"1x", L"1x"));
	SetDlgItemText(IDC_CHECK37, LL14(L"1.5倍", L"1.5x", L"1,5x", L"1,5x", L"1,5x", L"1.5x", L"1.5倍", L"1.5x", L"1,5x", L"1,5x", L"1,5x", L"1,5x", L"1,5x", L"1.5x"));
	SetDlgItemText(IDC_CHECK38, LL14(L"2倍", L"2x", L"2x", L"2x", L"2x", L"2x", L"2倍", L"2x", L"2x", L"2x", L"2x", L"2x", L"2x", L"2x"));
	SetDlgItemText(IDC_CHECK39, LL14(L"2.5倍", L"2.5x", L"2,5x", L"2,5x", L"2,5x", L"2.5x", L"2.5倍", L"2.5x", L"2,5x", L"2,5x", L"2,5x", L"2,5x", L"2,5x", L"2.5x"));
	SetDlgItemText(IDC_CHECK41, LL14(L"3倍", L"3x", L"3x", L"3x", L"3x", L"3x", L"3倍", L"3x", L"3x", L"3x", L"3x", L"3x", L"3x", L"3x"));
	// KPI volume checkboxes (1x, 2x, 3x, 4x, 5x)
	SetDlgItemText(IDC_CHECK45, LL14(L"等倍", L"1x", L"1x", L"1x", L"1x", L"1x", L"1倍", L"1x", L"1x", L"1x", L"1x", L"1x", L"1x", L"1x"));
	SetDlgItemText(IDC_CHECK42, LL14(L"2倍", L"2x", L"2x", L"2x", L"2x", L"2x", L"2倍", L"2x", L"2x", L"2x", L"2x", L"2x", L"2x", L"2x"));
	SetDlgItemText(IDC_CHECK43, LL14(L"3倍", L"3x", L"3x", L"3x", L"3x", L"3x", L"3倍", L"3x", L"3x", L"3x", L"3x", L"3x", L"3x", L"3x"));
	SetDlgItemText(IDC_CHECK44, LL14(L"4倍", L"4x", L"4x", L"4x", L"4x", L"4x", L"4倍", L"4x", L"4x", L"4x", L"4x", L"4x", L"4x", L"4x"));
	SetDlgItemText(IDC_CHECK46, LL14(L"5倍", L"5x", L"5x", L"5x", L"5x", L"5x", L"5倍", L"5x", L"5x", L"5x", L"5x", L"5x", L"5x", L"5x"));
	m_comboLang.AddString(LL14(L"日本語", L"Japanese", L"Japonais", L"Giapponese", L"Japonés", L"일본어", L"日语", L"اليابانية", L"Японский", L"Japanisch", L"Japonês", L"Japans", L"Japoński", L"Japonca"));
	m_comboLang.AddString(L"English");
	m_comboLang.AddString(L"Français");
	m_comboLang.AddString(L"Italiano");
	m_comboLang.AddString(L"Español");
	m_comboLang.AddString(L"한국어");
	m_comboLang.AddString(L"中文");
	m_comboLang.AddString(L"العربية");
	m_comboLang.AddString(L"Русский");
	m_comboLang.AddString(L"Deutsch");
	m_comboLang.AddString(L"Português");
	m_comboLang.AddString(L"Nederlands");
	m_comboLang.AddString(L"Polski");
	m_comboLang.AddString(L"Türkçe");
	m_comboLang.SetCurSel(savedata.lang >= 0 && savedata.lang <= 13 ? savedata.lang : 0);
	OSVERSIONINFO in; ZeroMemory(&in, sizeof(in)); in.dwOSVersionInfoSize = sizeof(OSVERSIONINFO); GetVersionEx(&in);
	if (in.dwMajorVersion <= 5)
		m_1.AddString(LL14(L"デフォルト", L"Default", L"Par défaut", L"Predefinito", L"Predeterminado", L"기본값", L"默认", L"افتراضي", L"По умолчанию", L"Standard", L"Predefinição", L"Standaard", L"Domyślny", L"Varsayılan"));
	else
		m_1.AddString(LL14(L"デフォルト(普通/EVR)", L"Default (normal/EVR)", L"Par défaut (normal/EVR)", L"Predefinito (normale/EVR)", L"Predeterminado (normal/EVR)", L"기본값(일반/EVR)", L"默认（普通/EVR）", L"افتراضي (عادي/EVR)", L"По умолчанию (обычный/EVR)", L"Standard (normal/EVR)", L"Padrão (normal/EVR)", L"Standaard (normaal/EVR)", L"Domyślny (normalny/EVR)", L"Varsayılan (normal/EVR)"));
	m_1.AddString(L"VMR7");
	m_1.AddString(L"VMR9");
	m_1.SetCurSel(savedata.render);
	switch (savedata.spc) {
	case 1:m_spc1x.SetCheck(TRUE); break;
	case 2:m_spc2x.SetCheck(TRUE); break;
	case 4:m_spc4x.SetCheck(TRUE); break;
	case 8:m_spc8x.SetCheck(TRUE); break;
	case 16:m_spc16x.SetCheck(TRUE); break;
	}
	switch (savedata.mp3) {
	case 1:m_mp31.SetCheck(TRUE); break;
	case 2:m_mp315.SetCheck(TRUE); break;
	case 3:m_mp32.SetCheck(TRUE); break;
	case 4:m_mp325.SetCheck(TRUE); break;
	case 5:m_mp33.SetCheck(TRUE); break;
	}
	switch (savedata.kpivol) {
	case 1:m_kpi10.SetCheck(TRUE); break;
	case 2:m_kpi15.SetCheck(TRUE); break;
	case 3:m_kpi20.SetCheck(TRUE); break;
	case 4:m_kpi25.SetCheck(TRUE); break;
	case 5:m_kpi30.SetCheck(TRUE); break;
	}
	if (in.dwMajorVersion <= 5) {
		m_evr.EnableWindow(FALSE);
		m_con.EnableWindow(FALSE);
		m_a.EnableWindow(FALSE);
	}
	m_mp3orig.SetCheck(savedata.mp3orig);
	m_audiost.SetCheck(savedata.audiost);
	m_24.SetCheck(savedata.bit24);
	m_32bit.SetCheck(savedata.bit32);
	m_m4a.SetCheck(savedata.m4a);
	m_upscale.SetCheck(savedata.upscale_enable ? BST_CHECKED : BST_UNCHECKED);
	m_speaker.ResetContent();
	m_speaker.AddString(LL14(L"ステレオ (2ch)", L"Stereo (2ch)", L"Stéréo (2ch)", L"Stereo (2ch)", L"Estéreo (2ch)", L"스테레오 (2ch)", L"立体声 (2ch)", L"ستيريو (2ch)", L"Стерео (2ch)", L"Stereo (2ch)", L"Estéreo (2ch)", L"Stereo (2ch)", L"Stereo (2ch)", L"Stereo (2ch)"));
	m_speaker.AddString(LL14(L"2.1ch (L+R+LFE)", L"2.1ch (L+R+LFE)", L"2,1ch (G+D+LFE)", L"2.1ch (L+R+LFE)", L"2.1ch (L+R+LFE)", L"2.1ch (L+R+LFE)", L"2.1ch (左+右+低音)", L"2.1ش (يسار+يمين+باس)", L"2.1ch (L+R+LFE)", L"2.1ch (L+R+LFE)", L"2.1ch (L+R+LFE)", L"2.1ch (L+R+LFE)", L"2.1ch (L+R+LFE)", L"2.1ch (L+R+LFE)"));
	m_speaker.AddString(LL14(L"4ch (クアッド)", L"4ch (quad)", L"4 canaux (quad)", L"4ch (quad)", L"4 canales (cuad.)", L"4채널 (쿼드)", L"四声道（环绕）", L"4 قنوات", L"4 канала (квад)", L"4 Kanäle (Quad)", L"4 canais (quad)", L"4 kanalen (quad)", L"4 kanały (quad)", L"4 kanal (quad)"));
	m_speaker.AddString(LL14(L"5.1ch サラウンド", L"5.1 surround", L"Surround 5.1", L"Surround 5.1", L"Sonido envolvente 5.1", L"5.1 서라운드", L"5.1 环绕声", L"صوت محيطي 5.1", L"Объёмный 5.1", L"5.1 Surround", L"Surround 5.1", L"5.1 surround", L"Dźwięk przestrzenny 5.1", L"5.1 surround"));
	m_speaker.AddString(LL14(L"7.1ch サラウンド", L"7.1 surround", L"Surround 7.1", L"Surround 7.1", L"Sonido envolvente 7.1", L"7.1 서라운드", L"7.1 环绕声", L"صوت محيطي 7.1", L"Объёмный 7.1", L"7.1 Surround", L"Surround 7.1", L"7.1 surround", L"Dźwięk przestrzenny 7.1", L"7.1 surround"));
	m_speaker.AddString(LL14(L"オリジナル（マッピングなし）", L"Original (no channel mapping)", L"Original (sans mappage)", L"Originale (nessun mapping)", L"Original (sin mapeo)", L"원본(채널 매핑 없음)", L"原始（不映射声道）", L"أصلي (بدون تعيين قنوات)", L"Исходный канал без микширования", L"Original (kein Kanal-Mapping)", L"Original (sem mapeamento)", L"Origineel (geen kanaal-mapping)", L"Oryginał (bez mapowania kanałów)", L"Orijinal (kanal eşlemesi yok)"));
	{
		int sp = savedata.speaker_layout;
		if (sp < 0 || sp > 5) sp = 0;
		m_speaker.SetCurSel(sp);
	}


	CCustomControlUtility::BeginDialogToolTip(m_tooltip, this);
	if (m_help.GetSafeHwnd())
		m_tooltip.AddTool(&m_help, LL14(L"操作ガイドを表示", L"Show operation guide", L"Afficher le guide", L"Mostra guida", L"Mostrar guía", L"조작 가이드 표시", L"显示操作指南", L"إظهار الدليل", L"Показать руководство", L"Bedienungsanleitung", L"Mostrar guia", L"Handleiding tonen", L"Pokaż przewodnik", L"İşlem kılavuzunu göster"));
	m_tooltip.AddTool(GetDlgItem(IDOK), LL14(L"設定を保存して閉じます", L"Save settings and close", L"Enregistrer les parametres et fermer", L"Salva impostazioni e chiudi", L"Guardar ajustes y cerrar", L"설정 저장 후 닫기", L"保存设置并关闭", L"حفظ الإعدادات وإغلاق", L"Сохранить настройки и закрыть", L"Einstellungen speichern und schließen", L"Salvar configuracoes e fechar", L"Instellingen opslaan en sluiten", L"Zapisz ustawienia i zamknij", L"Ayarları kaydet ve kapat"));	m_tooltip.AddTool(GetDlgItem(IDCANCEL), LL14(L"保存せずに閉じます", L"Close without saving", L"Fermer sans enregistrer", L"Chiudi senza salvare", L"Cerrar sin guardar", L"저장하지 않고 닫기", L"不保存并关闭", L"إغلاق دون حفظ", L"Закрыть без сохранения", L"Ohne Speichern schließen", L"Fechar sem salvar", L"Sluiten zonder opslaan", L"Zamknij bez zapisywania", L"Kaydetmeden kapat"));
	m_tooltip.AddTool(GetDlgItem(IDC_COMBO2), LL14(L"DirectSoundの出力デバイスを選択します", L"Select DirectSound output device", L"Choisir le peripherique de sortie DirectSound", L"Seleziona dispositivo di uscita DirectSound", L"Seleccionar dispositivo de salida DirectSound", L"DirectSound 출력 장치 선택", L"选择 DirectSound 输出设备", L"اختر جهاز إخراج DirectSound", L"Выбрать устройство вывода DirectSound", L"DirectSound-Ausgabegerat wahlen", L"Selecionar dispositivo de saida DirectSound", L"DirectSound-uitvoerapparaat kiezen", L"Wybierz urzadzenie wyjsciowe DirectSound", L"DirectSound cikis aygitini sec"));
	m_tooltip.AddTool(GetDlgItem(IDC_COMBO_MICDEV), LL14(L"WAV保存時のマイクミックス／録音に使うマイク端末を選びます", L"Select microphone for WAV mic-mix / recording", L"Choisir le micro pour le mix WAV / enregistrement", L"Scegli il microfono per mix WAV / registrazione", L"Elegir microfono para mix WAV / grabacion", L"WAV 마이크 믹스/녹음에 쓸 마이크 선택", L"选择用于WAV麦克风混音/录音的麦克风", L"اختر الميكروفون لمزج/تسجيل WAV", L"Выберите микрофон для микса/записи WAV", L"Mikrofon fur WAV-Mix / Aufnahme wahlen", L"Escolher microfone para mix WAV / gravacao", L"Kies microfoon voor WAV-mix / opname", L"Wybierz mikrofon do miksu/nagrania WAV", L"WAV miks/kayit icin mikrofon secin"));
	m_tooltip.AddTool(GetDlgItem(IDC_FONT), LL14(L"メイン画面のフォントを設定します", L"Set main window font", L"Definir la police de la fenetre principale", L"Imposta carattere finestra principale", L"Establecer fuente de ventana principal", L"메인 화면 글꼴 설정", L"设置主窗口字体", L"تعيين خط النافذة الرئيسية", L"Задать шрифт главного окна", L"Schriftart des Hauptfensters festlegen", L"Definir fonte da janela principal", L"Lettertype hoofdvenster instellen", L"Ustaw czcionke okna glownego", L"Ana pencere yazi tipini ayarla"));
	m_tooltip.AddTool(GetDlgItem(IDC_FONT2), LL14(L"リスト画面（プレイリスト等）のフォントを設定します", L"Set list view font (playlist, etc.)", L"Definir la police des vues liste (playlist, etc.)", L"Imposta carattere viste elenco (playlist, ecc.)", L"Establecer fuente de listas (playlist, etc.)", L"목록 화면(재생 목록 등) 글꼴 설정", L"设置列表界面（播放列表等）字体", L"تعيين خط عرض القائمة (قائمة التشغيل، إلخ)", L"Задать шрифт списков (плейлист и т.д.)", L"Schriftart fur Listenansichten festlegen", L"Definir fonte das listas (playlist etc.)", L"Lettertype lijstweergaven instellen", L"Ustaw czcionke widokow listy", L"Liste gorunumu yazi tipini ayarla"));
	m_tooltip.AddTool(GetDlgItem(IDCANCEL2), LL14(L"再生中の使用DirectShowフィルタを表示します。", L"Show DirectShow filters in use during playback.", L"Afficher les filtres DirectShow utilises pendant la lecture.", L"Mostra filtri DirectShow in uso durante la riproduzione.", L"Mostrar filtros DirectShow en uso durante la reproduccion.", L"재생 중 사용 중인 DirectShow 필터 표시.", L"显示播放中使用的 DirectShow 过滤器。", L"إظهار مرشحات DirectShow المستخدمة أثناء التشغيل.", L"Показать фильтры DirectShow при воспроизведении.", L"DirectShow-Filter wahrend der Wiedergabe anzeigen.", L"Mostrar filtros DirectShow em uso durante reproducao.", L"DirectShow-filters tonen tijdens afspelen.", L"Pokaz filtry DirectShow uzywane podczas odtwarzania.", L"Calma sirasinda kullanilan DirectShow filtrelerini goster."));
	m_tooltip.AddTool(GetDlgItem(IDCANCEL3), LL14(L"kpi一覧を表示します。", L"Show kpi list.", L"Afficher la liste kpi.", L"Mostra elenco kpi.", L"Mostrar lista kpi.", L"kpi 목록 표시.", L"显示 kpi 列表。", L"إظهار قائمة kpi.", L"Показать список kpi.", L"kpi-Liste anzeigen.", L"Mostrar lista kpi.", L"kpi-lijst tonen.", L"Pokaz liste kpi.", L"kpi listesini goster."));
	m_tooltip.AddTool(GetDlgItem(IDC_CHECK1), LL14(L"Windows Vista/7以降で有効です。\nIndeoを用いた動画の場合OFFにしてください。\nそれ以外はONでいいです。", L"Effective on Windows Vista/7+.\nTurn OFF for Indeo video.\nOtherwise ON.", L"Actif sous Windows Vista/7+.\nDesactiver pour video Indeo.\nSinon laissez active.", L"Attivo su Windows Vista/7+.\nDisattiva per video Indeo.\nAltrimenti lascia attivo.", L"Efectivo en Windows Vista/7+.\nDesactivar para video Indeo.\nEn otros casos dejar activado.", L"Windows Vista/7 이상에서 유효.\nIndeo 동영상은 끄세요.\n그 외는 켜두면 됩니다.", L"Windows Vista/7 及以上有效。\nIndeo 视频请关闭。\n其他情况可开启。", L"فعّال على Windows Vista/7+.\nأوقفه لفيديو Indeo.\nوإلا اتركه مفعّلاً.", L"Действует в Windows Vista/7+.\nВыключите для видео Indeo.\nИначе оставьте включенным.", L"Unter Windows Vista/7+ wirksam.\nBei Indeo-Video AUS.\nSonst AN lassen.", L"Efetivo no Windows Vista/7+.\nDesative para video Indeo.\nCaso contrario deixe ativo.", L"Actief op Windows Vista/7+.\nUit voor Indeo-video.\nAnders aan laten.", L"Dziala w Windows Vista/7+.\nWylacz dla wideo Indeo.\nW pozostalych przypadkach wlacz.", L"Windows Vista/7+ uzerinde gecerli.\nIndeo videosu icin kapat.\nDiger durumlarda acik birak."));
	m_tooltip.AddTool(GetDlgItem(IDC_CHECK2), LL14(L"Windows Vista/7以降で有効です。\nデスクトップコンポジション(Aero)を使用するかどうかを選択します。\n使用しないにするとEVRじゃなくても動画画面はきれいになります。", L"Effective on Vista/7+.\nUse desktop composition (Aero).\nWithout it, video may still look good without EVR.", L"Actif sous Vista/7+.\nUtiliser la composition bureau (Aero).\nSans Aero, la video peut rester nette sans EVR.", L"Attivo su Vista/7+.\nUsa composizione desktop (Aero).\nSenza Aero, il video puo essere comunque nitido.", L"Efectivo en Vista/7+.\nUsar composicion de escritorio (Aero).\nSin Aero, el video puede verse bien sin EVR.", L"Vista/7+에서 유효.\n데스크톱 컴포지션(Aero) 사용 여부.\n끄면 EVR 없이도 화질이 좋을 수 있음.", L"Vista/7+ 有效。\n是否使用桌面合成(Aero)。\n关闭时无 EVR 也可能画面清晰。", L"فعّال على Vista/7+.\nاستخدام تركيب سطح المكتب (Aero).\nبدونه قد يبدو الفيديو جيداً.", L"Действует в Vista/7+.\nКомпозиция рабочего стола (Aero).\nБез нее видео может быть четким без EVR.", L"Unter Vista/7+.\nDesktop-Komposition (Aero).\nOhne Aero kann Video auch ohne EVR gut aussehen.", L"Efetivo no Vista/7+.\nComposicao da area de trabalho (Aero).\nSem Aero, video pode ficar bom sem EVR.", L"Actief op Vista/7+.\nBureauscompositie (Aero).\nZonder Aero kan video toch scherp zijn.", L"Dziala w Vista/7+.\nKompozycja pulpitu (Aero).\nBez Aero obraz moze byc dobry bez EVR.", L"Vista/7+ uzerinde gecerli.\nMasaustu bilesimi (Aero).\nKapali olsa bile EVR olmadan video iyi olabilir."));
	m_tooltip.AddTool(GetDlgItem(IDC_CHECK3), LL14(L"Windows 10以降で有効です。\nアクリルモード（半透明ぼかし背景）を使用するかどうか決めます。", L"Effective on Windows 10+.\nEnable acrylic mode (translucent blurred background).", L"Actif sous Windows 10+.\nActiver le mode acrylique (fond flou translucide).", L"Attivo su Windows 10+.\nAbilita modalita acrilica (sfondo sfocato traslucido).", L"Efectivo en Windows 10+.\nActivar modo acrilico (fondo borroso translucido).", L"Windows 10+에서 유효.\n아크릴 모드(반투명 흐림 배경) 사용 여부.", L"Windows 10+ 有效。\n是否启用亚克力模式（半透明模糊背景）。", L"فعّال على Windows 10+.\nتفعيل وضع الأكريليك (خلفية ضبابية شفافة).", L"Действует в Windows 10+.\nРежим акрила (полупрозрачный размытый фон).", L"Unter Windows 10+.\nAcryl-Modus (transparenter Unscharfe-Hintergrund).", L"Efetivo no Windows 10+.\nModo acrilico (fundo desfocado translucido).", L"Actief op Windows 10+.\nAcrylmodus (doorzichtige wazige achtergrond).", L"Dziala w Windows 10+.\nTryb akrylowy (polprzezroczyste rozmyte tlo).", L"Windows 10+ uzerinde gecerli.\nAkrilik mod (yarim saydam bulanik arka plan)."));
	m_tooltip.AddTool(GetDlgItem(IDC_CHECK27), LL14(L"動画にffdshowを使うかどうか選択します。\nWindows7の場合、デフォルトでDivxなどを再生できるのでそちらを使いたい人はOFFにしてください。", L"Use ffdshow for video.\nOn Win7, DivX works by default; turn OFF if you prefer that.", L"Utiliser ffdshow pour la video.\nSous Win7, DivX fonctionne par defaut ; desactivez si vous preferez.", L"Usa ffdshow per i video.\nSu Win7 DivX funziona di default; disattiva se preferisci quello.", L"Usar ffdshow para video.\nEn Win7 DivX funciona por defecto; desactiva si lo prefieres.", L"동영상에 ffdshow 사용 여부.\nWin7에서는 DivX가 기본이므로 그쪽을 쓰려면 끄세요.", L"选择视频是否使用 ffdshow。\nWin7 默认可用 DivX 等，想用后者请关闭。", L"استخدام ffdshow للفيديو.\nفي Win7 يعمل DivX افتراضياً؛ أوقفه إن فضّلت ذلك.", L"Использовать ffdshow для видео.\nВ Win7 DivX по умолчанию; выключите, если предпочитаете его.", L"ffdshow fur Video.\nUnter Win7 funktioniert DivX standardmassig; AUS wenn gewunscht.", L"Usar ffdshow para video.\nNo Win7 DivX funciona por padrao; desligue se preferir.", L"ffdshow voor video.\nOp Win7 werkt DivX standaard; uit als je dat wilt.", L"Uzyj ffdshow do wideo.\nW Win7 DivX dziala domyslnie; wylacz jesli wolisz.", L"Video icin ffdshow kullan.\nWin7'de DivX varsayilan; onu tercih ediyorsan kapat."));
	m_tooltip.AddTool(GetDlgItem(IDC_CHECK30), LL14(L"vobとdatファイルはHaaliを通さないように作られていますが、\nvobに複数音声があるときにはチェックを入れて下さい。", L"vob/dat skip Haali by default.\nCheck when vob has multiple audio tracks.", L"vob/dat ignore Haali par defaut.\nCochez si le vob a plusieurs pistes audio.", L"vob/dat saltano Haali di default.\nSpunta se il vob ha piu tracce audio.", L"vob/dat omiten Haali por defecto.\nMarca si el vob tiene varias pistas de audio.", L"vob/dat는 기본적으로 Haali를 거치지 않습니다.\nvob에 다중 음성이 있으면 체크하세요.", L"vob/dat 默认不经过 Haali。\nvob 有多条音轨时请勾选。", L"vob/dat يتخطى Haali افتراضياً.\nحدّد عند وجود عدة مسارات صوتية في vob.", L"vob/dat по умолчанию без Haali.\nОтметьте, если в vob несколько аудiodорожек.", L"vob/dat uberspringt Haali standardmassig.\nAnhaken bei mehreren Audiospuren in vob.", L"vob/dat ignora Haali por padrao.\nMarque se o vob tiver varias faixas de audio.", L"vob/dat slaat Haali standaard over.\nVink aan bij meerdere audiosporen in vob.", L"vob/dat domyslnie omija Haali.\nZaznacz, gdy vob ma wiele sciezek audio.", L"vob/dat varsayilan olarak Haali kullanmaz.\nvob'da birden fazla ses varsa isaretle."));
	m_tooltip.AddTool(GetDlgItem(IDC_CHECK31), LL14(L"動画にHaaliを使いません。\n動画が重いと思った時や複数音声が無い時はチェックを入れると軽くなります。", L"Do not use Haali for video.\nCheck if video is heavy or has no multiple audio.", L"Ne pas utiliser Haali pour la video.\nCochez si la video est lourde ou sans pistes multiples.", L"Non usare Haali per i video.\nSpunta se il video e pesante o senza audio multiplo.", L"No usar Haali para video.\nMarca si el video es pesado o no tiene audio multiple.", L"동영상에 Haali를 사용하지 않습니다.\n무겁거나 다중 음성이 없으면 체크하면 가벼워집니다.", L"视频不使用 Haali。\n感觉卡顿时或无多音轨时可勾选以减轻负担。", L"عدم استخدام Haali للفيديو.\nحدّد إذا كان الفيديو ثقيلاً أو بلا مسارات متعددة.", L"Не использовать Haali для видео.\nОтметьте, если видео тяжелое или без нескольких дорожек.", L"Haali fur Video nicht verwenden.\nAnhaken bei schwerem Video oder ohne Mehrspur-Audio.", L"Nao usar Haali para video.\nMarque se o video for pesado ou sem audio multiplo.", L"Geen Haali voor video.\nVink aan bij zware video of zonder meerdere audiosporen.", L"Nie uzywaj Haali do wideo.\nZaznacz, gdy wideo jest ciezkie lub bez wielu sciezek.", L"Video icin Haali kullanma.\nVideo agirsa veya coklu ses yoksa isaretle, hafifler."));
	m_tooltip.AddTool(GetDlgItem(IDC_CHECK32), LL14(L"kpi SPC/NEZplug++等のSPC出力倍率を2倍にします。\nイコライザー処理で音割れを抑えます。", L"Set KPI SPC/NEZplug++ SPC output to 2x.\nThe equalizer prevents clipping.", L"Multiplicateur SPC kpi SPC/NEZplug++ : 2x.\nL'egaliseur evite la saturation.", L"Moltiplicatore SPC kpi SPC/NEZplug++ : 2x.\nL'equalizzatore evita il clipping.", L"Multiplicador SPC kpi SPC/NEZplug++ : 2x.\nEl ecualizador evita el recorte.", L"kpi SPC/NEZplug++ SPC 출력 배율 2배.\n이퀄라이저가 클리핑 방지.", L"kpi SPC/NEZplug++ 的 SPC 输出倍率2倍。\n均衡器防止削波。", L"مضاعف SPC لـ kpi SPC/NEZplug++ : 2×.\nالمعادل يمنع القص.", L"Множитель SPC kpi SPC/NEZplug++ : 2x.\nЭквалайзер предотвращает клиппинг.", L"SPC-Ausgang kpi SPC/NEZplug++ : 2x.\nEqualizer verhindert Clipping.", L"Multiplicador SPC kpi SPC/NEZplug++ : 2x.\nEqualizador evita clipping.", L"SPC-vermenigvuldiger kpi SPC/NEZplug++ : 2x.\nEqualizer voorkomt clipping.", L"Mnoznik SPC kpi SPC/NEZplug++ : 2x.\nKorektor zapobiega przesterowaniu.", L"kpi SPC/NEZplug++ SPC cikis carpani 2 kat.\nEkolayzer kirpilmayi onler."));
	m_tooltip.AddTool(GetDlgItem(IDC_CHECK33), LL14(L"kpi SPC/NEZplug++等のSPC出力倍率を3倍にします。\nイコライザー処理で音割れを抑えます。", L"Set KPI SPC/NEZplug++ SPC output to 3x.\nThe equalizer prevents clipping.", L"Multiplicateur SPC kpi SPC/NEZplug++ : 3x.\nL'egaliseur evite la saturation.", L"Moltiplicatore SPC kpi SPC/NEZplug++ : 3x.\nL'equalizzatore evita il clipping.", L"Multiplicador SPC kpi SPC/NEZplug++ : 3x.\nEl ecualizador evita el recorte.", L"kpi SPC/NEZplug++ SPC 출력 배율 3배.\n이퀄라이저가 클리핑 방지.", L"kpi SPC/NEZplug++ 的 SPC 输出倍率3倍。\n均衡器防止削波。", L"مضاعف SPC لـ kpi SPC/NEZplug++ : 3×.\nالمعادل يمنع القص.", L"Множитель SPC kpi SPC/NEZplug++ : 3x.\nЭквалайзер предотвращает клиппинг.", L"SPC-Ausgang kpi SPC/NEZplug++ : 3x.\nEqualizer verhindert Clipping.", L"Multiplicador SPC kpi SPC/NEZplug++ : 3x.\nEqualizador evita clipping.", L"SPC-vermenigvuldiger kpi SPC/NEZplug++ : 3x.\nEqualizer voorkomt clipping.", L"Mnoznik SPC kpi SPC/NEZplug++ : 3x.\nKorektor zapobiega przesterowaniu.", L"kpi SPC/NEZplug++ SPC cikis carpani 3 kat.\nEkolayzer kirpilmayi onler."));
	m_tooltip.AddTool(GetDlgItem(IDC_CHECK34), LL14(L"kpi SPC/NEZplug++等のSPC出力倍率を4倍にします。\nイコライザー処理で音割れを抑えます。", L"Set KPI SPC/NEZplug++ SPC output to 4x.\nThe equalizer prevents clipping.", L"Multiplicateur SPC kpi SPC/NEZplug++ : 4x.\nL'egaliseur evite la saturation.", L"Moltiplicatore SPC kpi SPC/NEZplug++ : 4x.\nL'equalizzatore evita il clipping.", L"Multiplicador SPC kpi SPC/NEZplug++ : 4x.\nEl ecualizador evita el recorte.", L"kpi SPC/NEZplug++ SPC 출력 배율 4배.\n이퀄라이저가 클리핑 방지.", L"kpi SPC/NEZplug++ 的 SPC 输出倍率4倍。\n均衡器防止削波。", L"مضاعف SPC لـ kpi SPC/NEZplug++ : 4×.\nالمعادل يمنع القص.", L"Множитель SPC kpi SPC/NEZplug++ : 4x.\nЭквалайзер предотвращает клиппинг.", L"SPC-Ausgang kpi SPC/NEZplug++ : 4x.\nEqualizer verhindert Clipping.", L"Multiplicador SPC kpi SPC/NEZplug++ : 4x.\nEqualizador evita clipping.", L"SPC-vermenigvuldiger kpi SPC/NEZplug++ : 4x.\nEqualizer voorkomt clipping.", L"Mnoznik SPC kpi SPC/NEZplug++ : 4x.\nKorektor zapobiega przesterowaniu.", L"kpi SPC/NEZplug++ SPC cikis carpani 4 kat.\nEkolayzer kirpilmayi onler."));
	m_tooltip.AddTool(GetDlgItem(IDC_CHECK35), LL14(L"kpi SPC/NEZplug++等のSPC出力倍率を等倍にします。\nイコライザー処理で音割れを抑えます。", L"Set KPI SPC/NEZplug++ SPC output to 1x.\nThe equalizer prevents clipping.", L"Multiplicateur SPC kpi SPC/NEZplug++ : 1x.\nL'egaliseur evite la saturation.", L"Moltiplicatore SPC kpi SPC/NEZplug++ : 1x.\nL'equalizzatore evita il clipping.", L"Multiplicador SPC kpi SPC/NEZplug++ : 1x.\nEl ecualizador evita el recorte.", L"kpi SPC/NEZplug++ SPC 출력 배율 1배.\n이퀄라이저가 클리핑 방지.", L"kpi SPC/NEZplug++ 的 SPC 输出倍率1倍。\n均衡器防止削波。", L"مضاعف SPC لـ kpi SPC/NEZplug++ : 1×.\nالمعادل يمنع القص.", L"Множитель SPC kpi SPC/NEZplug++ : 1x.\nЭквалайзер предотвращает клиппинг.", L"SPC-Ausgang kpi SPC/NEZplug++ : 1x.\nEqualizer verhindert Clipping.", L"Multiplicador SPC kpi SPC/NEZplug++ : 1x.\nEqualizador evita clipping.", L"SPC-vermenigvuldiger kpi SPC/NEZplug++ : 1x.\nEqualizer voorkomt clipping.", L"Mnoznik SPC kpi SPC/NEZplug++ : 1x.\nKorektor zapobiega przesterowaniu.", L"kpi SPC/NEZplug++ SPC cikis carpani 1 kat.\nEkolayzer kirpilmayi onler."));
	m_tooltip.AddTool(GetDlgItem(IDC_CHECK36), LL14(L"kpi SPC/NEZplug++等のSPC出力倍率を5倍にします。\nイコライザー処理で音割れを抑えます。", L"Set KPI SPC/NEZplug++ SPC output to 5x.\nThe equalizer prevents clipping.", L"Multiplicateur SPC kpi SPC/NEZplug++ : 5x.\nL'egaliseur evite la saturation.", L"Moltiplicatore SPC kpi SPC/NEZplug++ : 5x.\nL'equalizzatore evita il clipping.", L"Multiplicador SPC kpi SPC/NEZplug++ : 5x.\nEl ecualizador evita el recorte.", L"kpi SPC/NEZplug++ SPC 출력 배율 5배.\n이퀄라이저가 클리핑 방지.", L"kpi SPC/NEZplug++ 的 SPC 输出倍率5倍。\n均衡器防止削波。", L"مضاعف SPC لـ kpi SPC/NEZplug++ : 5×.\nالمعادل يمنع القص.", L"Множитель SPC kpi SPC/NEZplug++ : 5x.\nЭквалайзер предотвращает клиппинг.", L"SPC-Ausgang kpi SPC/NEZplug++ : 5x.\nEqualizer verhindert Clipping.", L"Multiplicador SPC kpi SPC/NEZplug++ : 5x.\nEqualizador evita clipping.", L"SPC-vermenigvuldiger kpi SPC/NEZplug++ : 5x.\nEqualizer voorkomt clipping.", L"Mnoznik SPC kpi SPC/NEZplug++ : 5x.\nKorektor zapobiega przesterowaniu.", L"kpi SPC/NEZplug++ SPC cikis carpani 5 kat.\nEkolayzer kirpilmayi onler"));
	m_tooltip.AddTool(GetDlgItem(IDC_CHECK40), LL14(L"mp3の出力倍率を等倍にします。\nイコライザー処理で音割れを抑えます。", L"Set MP3 output to 1x.\nThe equalizer prevents clipping.", L"Multiplicateur mp3 : 1x.\nL'egaliseur evite la saturation.", L"Moltiplicatore mp3 : 1x.\nL'equalizzatore evita il clipping.", L"Multiplicador mp3 : 1x.\nEl ecualizador evita el recorte.", L"mp3 출력 배율 1배.\n이퀄라이저가 클리핑 방지.", L"mp3 输出倍率1倍。\n均衡器防止削波。", L"مضاعف mp3 : 1×.\nالمعادل يمنع القص.", L"Множитель mp3 : 1x.\nЭквалайзер предотвращает клиппинг.", L"mp3-Ausgang : 1x.\nEqualizer verhindert Clipping.", L"Multiplicador mp3 : 1x.\nEqualizador evita clipping.", L"mp3-vermenigvuldiger : 1x.\nEqualizer voorkomt clipping.", L"Mnoznik mp3 : 1x.\nKorektor zapobiega przesterowaniu.", L"mp3 cikis carpani 1 kat.\nEkolayzer kirpilmayi onler"));
	m_tooltip.AddTool(GetDlgItem(IDC_CHECK37), LL14(L"mp3の出力倍率を1.5倍にします。\nイコライザー処理で音割れを抑えます。", L"Set MP3 output to 1.5x.\nThe equalizer prevents clipping.", L"Multiplicateur mp3 : 1,5x.\nL'egaliseur evite la saturation.", L"Moltiplicatore mp3 : 1,5x.\nL'equalizzatore evita il clipping.", L"Multiplicador mp3 : 1,5x.\nEl ecualizador evita el recorte.", L"mp3 출력 배율 1.5배.\n이퀄라이저가 클리핑 방지.", L"mp3 输出倍率1.5倍。\n均衡器防止削波。", L"مضاعف mp3 : 1.5×.\nالمعادل يمنع القص.", L"Множитель mp3 : 1,5x.\nЭквалайзер предотвращает клиппинг.", L"mp3-Ausgang : 1,5x.\nEqualizer verhindert Clipping.", L"Multiplicador mp3 : 1,5x.\nEqualizador evita clipping.", L"mp3-vermenigvuldiger : 1,5x.\nEqualizer voorkomt clipping.", L"Mnoznik mp3 : 1,5x.\nKorektor zapobiega przesterowaniu.", L"mp3 cikis carpani 1,5 kat.\nEkolayzer kirpilmayi onler"));
	m_tooltip.AddTool(GetDlgItem(IDC_CHECK38), LL14(L"mp3の出力倍率を2倍にします。\nイコライザー処理で音割れを抑えます。", L"Set MP3 output to 2x.\nThe equalizer prevents clipping.", L"Multiplicateur mp3 : 2x.\nL'egaliseur evite la saturation.", L"Moltiplicatore mp3 : 2x.\nL'equalizzatore evita il clipping.", L"Multiplicador mp3 : 2x.\nEl ecualizador evita el recorte.", L"mp3 출력 배율 2배.\n이퀄라이저가 클리핑 방지.", L"mp3 输出倍率2倍。\n均衡器防止削波。", L"مضاعف mp3 : 2×.\nالمعادل يمنع القص.", L"Множитель mp3 : 2x.\nЭквалайзер предотвращает клиппинг.", L"mp3-Ausgang : 2x.\nEqualizer verhindert Clipping.", L"Multiplicador mp3 : 2x.\nEqualizador evita clipping.", L"mp3-vermenigvuldiger : 2x.\nEqualizer voorkomt clipping.", L"Mnoznik mp3 : 2x.\nKorektor zapobiega przesterowaniu.", L"mp3 cikis carpani 2 kat.\nEkolayzer kirpilmayi onler"));
	m_tooltip.AddTool(GetDlgItem(IDC_CHECK39), LL14(L"mp3の出力倍率を2.5倍にします。\nイコライザー処理で音割れを抑えます。", L"Set MP3 output to 2.5x.\nThe equalizer prevents clipping.", L"Multiplicateur mp3 : 2,5x.\nL'egaliseur evite la saturation.", L"Moltiplicatore mp3 : 2,5x.\nL'equalizzatore evita il clipping.", L"Multiplicador mp3 : 2,5x.\nEl ecualizador evita el recorte.", L"mp3 출력 배율 2.5배.\n이퀄라이저가 클리핑 방지.", L"mp3 输出倍率2.5倍。\n均衡器防止削波。", L"مضاعف mp3 : 2.5×.\nالمعادل يمنع القص.", L"Множитель mp3 : 2,5x.\nЭквалайзер предотвращает клиппинг.", L"mp3-Ausgang : 2,5x.\nEqualizer verhindert Clipping.", L"Multiplicador mp3 : 2,5x.\nEqualizador evita clipping.", L"mp3-vermenigvuldiger : 2,5x.\nEqualizer voorkomt clipping.", L"Mnoznik mp3 : 2,5x.\nKorektor zapobiega przesterowaniu.", L"mp3 cikis carpani 2,5 kat.\nEkolayzer kirpilmayi onler"));
	m_tooltip.AddTool(GetDlgItem(IDC_CHECK41), LL14(L"mp3の出力倍率を3倍にします。\nイコライザー処理で音割れを抑えます。", L"Set MP3 output to 3x.\nThe equalizer prevents clipping.", L"Multiplicateur mp3 : 3x.\nL'egaliseur evite la saturation.", L"Moltiplicatore mp3 : 3x.\nL'equalizzatore evita il clipping.", L"Multiplicador mp3 : 3x.\nEl ecualizador evita el recorte.", L"mp3 출력 배율 3배.\n이퀄라이저가 클리핑 방지.", L"mp3 输出倍率3倍。\n均衡器防止削波。", L"مضاعف mp3 : 3×.\nالمعادل يمنع القص.", L"Множитель mp3 : 3x.\nЭквалайзер предотвращает клиппинг.", L"mp3-Ausgang : 3x.\nEqualizer verhindert Clipping.", L"Multiplicador mp3 : 3x.\nEqualizador evita clipping.", L"mp3-vermenigvuldiger : 3x.\nEqualizer voorkomt clipping.", L"Mnoznik mp3 : 3x.\nKorektor zapobiega przesterowaniu.", L"mp3 cikis carpani 3 kat.\nEkolayzer kirpilmayi onler"));
	m_tooltip.AddTool(GetDlgItem(IDC_CHECK45), LL14(L"kpiの出力倍率を等倍にします。\nイコライザー処理で音割れを抑えます。", L"Set KPI output to 1x.\nThe equalizer prevents clipping.", L"Multiplicateur kpi : 1x.\nL'egaliseur evite la saturation.", L"Moltiplicatore kpi : 1x.\nL'equalizzatore evita il clipping.", L"Multiplicador kpi : 1x.\nEl ecualizador evita el recorte.", L"kpi 출력 배율 1배.\n이퀄라이저가 클리핑 방지.", L"kpi 输出倍率1倍。\n均衡器防止削波。", L"مضاعف kpi : 1×.\nالمعادل يمنع القص.", L"Множитель kpi : 1x.\nЭквалайзер предотвращает клиппинг.", L"kpi-Ausgang : 1x.\nEqualizer verhindert Clipping.", L"Multiplicador kpi : 1x.\nEqualizador evita clipping.", L"kpi-vermenigvuldiger : 1x.\nEqualizer voorkomt clipping.", L"Mnoznik kpi : 1x.\nKorektor zapobiega przesterowaniu.", L"kpi cikis carpani 1 kat.\nEkolayzer kirpilmayi onler"));
	m_tooltip.AddTool(GetDlgItem(IDC_CHECK42), LL14(L"kpiの出力倍率を2倍にします。\nイコライザー処理で音割れを抑えます。", L"Set KPI output to 2x.\nThe equalizer prevents clipping.", L"Multiplicateur kpi : 2x.\nL'egaliseur evite la saturation.", L"Moltiplicatore kpi : 2x.\nL'equalizzatore evita il clipping.", L"Multiplicador kpi : 2x.\nEl ecualizador evita el recorte.", L"kpi 출력 배율 2배.\n이퀄라이저가 클리핑 방지.", L"kpi 输出倍率2倍。\n均衡器防止削波。", L"مضاعف kpi : 2×.\nالمعادل يمنع القص.", L"Множитель kpi : 2x.\nЭквалайзер предотвращает клиппинг.", L"kpi-Ausgang : 2x.\nEqualizer verhindert Clipping.", L"Multiplicador kpi : 2x.\nEqualizador evita clipping.", L"kpi-vermenigvuldiger : 2x.\nEqualizer voorkomt clipping.", L"Mnoznik kpi : 2x.\nKorektor zapobiega przesterowaniu.", L"kpi cikis carpani 2 kat.\nEkolayzer kirpilmayi onler"));
	m_tooltip.AddTool(GetDlgItem(IDC_CHECK43), LL14(L"kpiの出力倍率を3倍にします。\nイコライザー処理で音割れを抑えます。", L"Set KPI output to 3x.\nThe equalizer prevents clipping.", L"Multiplicateur kpi : 3x.\nL'egaliseur evite la saturation.", L"Moltiplicatore kpi : 3x.\nL'equalizzatore evita il clipping.", L"Multiplicador kpi : 3x.\nEl ecualizador evita el recorte.", L"kpi 출력 배율 3배.\n이퀄라이저가 클리핑 방지.", L"kpi 输出倍率3倍。\n均衡器防止削波。", L"مضاعف kpi : 3×.\nالمعادل يمنع القص.", L"Множитель kpi : 3x.\nЭквалайзер предотвращает клиппинг.", L"kpi-Ausgang : 3x.\nEqualizer verhindert Clipping.", L"Multiplicador kpi : 3x.\nEqualizador evita clipping.", L"kpi-vermenigvuldiger : 3x.\nEqualizer voorkomt clipping.", L"Mnoznik kpi : 3x.\nKorektor zapobiega przesterowaniu.", L"kpi cikis carpani 3 kat.\nEkolayzer kirpilmayi onler"));
	m_tooltip.AddTool(GetDlgItem(IDC_CHECK44), LL14(L"kpiの出力倍率を4倍にします。\nイコライザー処理で音割れを抑えます。", L"Set KPI output to 4x.\nThe equalizer prevents clipping.", L"Multiplicateur kpi : 4x.\nL'egaliseur evite la saturation.", L"Moltiplicatore kpi : 4x.\nL'equalizzatore evita il clipping.", L"Multiplicador kpi : 4x.\nEl ecualizador evita el recorte.", L"kpi 출력 배율 4배.\n이퀄라이저가 클리핑 방지.", L"kpi 输出倍率4倍。\n均衡器防止削波。", L"مضاعف kpi : 4×.\nالمعادل يمنع القص.", L"Множитель kpi : 4x.\nЭквалайзер предотвращает клиппинг.", L"kpi-Ausgang : 4x.\nEqualizer verhindert Clipping.", L"Multiplicador kpi : 4x.\nEqualizador evita clipping.", L"kpi-vermenigvuldiger : 4x.\nEqualizer voorkomt clipping.", L"Mnoznik kpi : 4x.\nKorektor zapobiega przesterowaniu.", L"kpi cikis carpani 4 kat.\nEkolayzer kirpilmayi onler"));
	m_tooltip.AddTool(GetDlgItem(IDC_CHECK46), LL14(L"kpiの出力倍率を5倍にします。\nイコライザー処理で音割れを抑えます。", L"Set KPI output to 5x.\nThe equalizer prevents clipping.", L"Multiplicateur kpi : 5x.\nL'egaliseur evite la saturation.", L"Moltiplicatore kpi : 5x.\nL'equalizzatore evita il clipping.", L"Multiplicador kpi : 5x.\nEl ecualizador evita el recorte.", L"kpi 출력 배율 5배.\n이퀄라이저가 클리핑 방지.", L"kpi 输出倍率5倍。\n均衡器防止削波。", L"مضاعف kpi : 5×.\nالمعادل يمنع القص.", L"Множитель kpi : 5x.\nЭквалайзер предотвращает клиппинг.", L"kpi-Ausgang : 5x.\nEqualizer verhindert Clipping.", L"Multiplicador kpi : 5x.\nEqualizador evita clipping.", L"kpi-vermenigvuldiger : 5x.\nEqualizer voorkomt clipping.", L"Mnoznik kpi : 5x.\nKorektor zapobiega przesterowaniu.", L"kpi cikis carpani 5 kat.\nEkolayzer kirpilmayi onler"));
	m_tooltip.AddTool(GetDlgItem(IDC_CHECK47), LL14(L"mp3のデコーダをオリジナルのデコーダを使わずに、独自で使ったデコーダを使う。\nエラーなどで演奏できないときにチェック入れて下さい。\nまた独自で正常にならない時ははずして下さい。", L"Use custom mp3 decoder instead of original.\nCheck if playback fails.\nUncheck if custom causes issues.", L"Utiliser le decodeur mp3 personnalise.\nCochez si la lecture echoue.\nDecochez s'il pose probleme.", L"Usa decodificatore mp3 personalizzato.\nSpunta se la riproduzione fallisce.\nTogli se causa problemi.", L"Usar decodificador mp3 personalizado.\nMarca si falla la reproduccion.\nDesmarca si causa problemas.", L"mp3를 원본이 아닌 자체 디코더로 재생합니다.\n오류로 재생되지 않을 때 체크하세요.\n자체 디코더가 정상이 아니면 해제하세요.", L"不使用原始解码器，改用自定义 mp3 解码器。\n因错误无法播放时请勾选。\n自定义解码器异常时请取消勾选。", L"استخدام فك mp3 مخصص.\nحدّد عند فشل التشغيل.\nألغِ إن سبب مشاكل.", L"Использовать свой mp3-декодер.\nОтметьте при сбое воспроизведения.\nСнимите, если мешает.", L"Eigenen mp3-Decoder nutzen.\nAnhaken bei Fehlern.\nAb wenn Probleme.", L"Usar decodificador mp3 proprio.\nMarque se falhar.\nDesmarque se der problema.", L"Eigen mp3-decoder gebruiken.\nAanvinken bij fouten.\nUit bij problemen.", L"Uzyj wlasnego dekodera mp3.\nZaznacz przy bledach.\nOdznacz gdy szkodzi.", L"Ozel mp3 kod cozucu kullan.\nCalma hatasinda isaretle.\nSorun cikarirsa kaldir."));
	m_tooltip.AddTool(GetDlgItem(IDC_CHECK48), LL14(L"複数音声のある動画を再生する時に、再生前に\n音声ストリームの選択画面を表示します。\n通常ストリーム1がメインとして使われ、ストリーム2以降はコメンタリや英語音声などに使われています。", L"When playing a video with multiple audio tracks,\nthe audio stream selection screen is shown before playback.\nStream 1 is usually the main one; stream 2 onward are for commentary, English audio, etc.", L"Lors de la lecture d'une video a plusieurs pistes audio,\nl'ecran de selection de piste s'affiche avant la lecture.\nLa piste 1 est generalement la principale ; les pistes 2 et suivantes servent au commentaire, a l'anglais, etc.", L"Quando riproduci un video con piu tracce audio,\nprima della riproduzione appare la schermata di scelta della traccia.\nLa traccia 1 e di solito la principale; dalla 2 in poi per commento, inglese, ecc.", L"Al reproducir un video con varias pistas de audio,\nse muestra la pantalla de seleccion de pista antes de reproducir.\nLa pista 1 suele ser la principal; de la 2 en adelante para comentario, ingles, etc.", L"다중 음성 동영상 재생 전\n음성 스트림 선택 화면을 표시합니다.\n보통 스트림 1이 메인, 스트림 2 이후는 해설이나 영어 음성 등에 사용됩니다.", L"播放多音轨视频前\n显示音轨选择界面。\n通常音轨1为主音轨，音轨2及以后用于解说或英语音频等。", L"عند تشغيل فيديو متعدد المسارات الصوتية،\nتظهر شاشة اختيار مسار الصوت قبل التشغيل.\nالمسار 1 عادةً هو الرئيسي؛ والمسار 2 وما بعده للتعليق والصوت الإنجليزي وغيره.", L"При воспроизведении видео с несколькими аудиодорожками\nперед началом показывается экран выбора аудиодорожки.\nДорожка 1 обычно основная; дорожка 2 и далее — для комментариев, английской озвучки и т. п.", L"Beim Abspielen eines Videos mit mehreren Audiospuren\nwird vor der Wiedergabe die Spurauswahl angezeigt.\nSpur 1 ist meist die Hauptspur; Spur 2 und weitere fur Kommentar, englischen Ton usw.", L"Ao reproduzir um video com varias faixas de audio,\na tela de selecao de faixa aparece antes da reproducao.\nA faixa 1 costuma ser a principal; da 2 em diante para comentario, ingles, etc.", L"Bij het afspelen van een video met meerdere audiosporen\nverschijnt voor het afspelen het spoorkeuzescherm.\nSpoor 1 is meestal het hoofdspoor; spoor 2 en verder voor commentaar, Engels, enz.", L"Podczas odtwarzania wideo z wieloma sciezkami audio\nprzed odtwarzaniem pojawia sie ekran wyboru sciezki.\nSciezka 1 to zwykle glowna; sciezka 2 i dalsze do komentarza, angielskiego itp.", L"Coklu sesli videoda calmadan once\nses akisi secimini goster.\n1 genelde ana; 2+ yorum/Ingilizce."));
	m_tooltip.AddTool(GetDlgItem(IDC_CHECK49), LL14(L"対応しているkpiを24bit(ハイレゾ)で再生します。\n通常は16bitですが、まれに対応しているものがあります。\n音割れについては考慮されていないため、spcなど倍率を上げないといけないものは気をつけて下さい。", L"Play supported kpi at 24bit (hi-res).\nUsually 16bit, but some rarely support it.\nClipping is not considered, so be careful with formats needing higher gain such as spc.", L"Lire les kpi compatibles en 24 bits (haute resolution).\nHabituellement 16 bits, mais certains le prennent en charge.\nLe clipping n'est pas gere : attention aux formats a fort gain comme spc.", L"Riproduci i kpi supportati a 24 bit (alta risoluzione).\nDi solito 16 bit, ma alcuni lo supportano.\nIl clipping non e gestito: attenzione ai formati con gain alto come spc.", L"Reproducir los kpi compatibles a 24 bits (alta resolucion).\nNormalmente 16 bits, pero algunos lo admiten.\nNo hay proteccion contra clipping: cuidado con formatos de mayor ganancia como spc.", L"지원하는 kpi를 24bit(하이레조)로 재생합니다.\n보통은 16bit이지만 드물게 지원하는 것이 있습니다.\n음 깨짐은 고려되지 않으므로 spc 등 배율을 올려야 하는 것은 주의하세요.", L"以24bit（高解析度）播放支持的kpi。\n通常为16bit，偶尔有支持的。\n未考虑削波，spc等需要提高倍率的请注意。", L"تشغيل ملفات kpi المدعومة بدقة 24 بت (فائقة الدقة).\nعادةً تكون 16 بت، لكن بعضها يدعمها.\nلا يُراعى تقطيع الصوت، فانتبه للأنواع التي تحتاج رفع المضاعف مثل spc.", L"Воспроизведение поддерживаемых kpi в 24 бита (Hi-Res).\nОбычно 16 бит, но некоторые поддерживают.\nКлиппинг не учитывается — осторожно с форматами, требующими усиления, например spc.", L"Unterstutzte kpi in 24 Bit (Hi-Res) abspielen.\nUblich sind 16 Bit, manche unterstutzen mehr.\nClipping wird nicht berucksichtigt: Vorsicht bei Formaten mit hoher Verstarkung wie spc.", L"Reproduzir os kpi suportados em 24 bits (alta resolucao).\nNormalmente 16 bits, mas alguns suportam.\nO clipping nao e tratado: cuidado com formatos de ganho alto como spc.", L"Ondersteunde kpi in 24 bit (hi-res) afspelen.\nMeestal 16 bit, maar sommige ondersteunen het.\nClipping wordt niet meegerekend: let op formaten met hoge versterking zoals spc.", L"Odtwarzaj obslugiwane kpi w 24 bitach (hi-res).\nZwykle 16 bitow, ale niektore to obsluguja.\nPrzesterowanie nie jest uwzgledniane: uwaga na formaty o wysokim wzmocnieniu jak spc.", L"Desteklenen kpi'yi 24 bit (yuksek cozunurluk) calar.\nGenelde 16 bittir, nadiren destekleyenler var.\nKirpilma dikkate alinmaz; spc gibi carpani artirmak gerekenlere dikkat edin."));
	m_tooltip.AddTool(GetDlgItem(IDC_CHECK51), LL14(L"対応しているkpiを32bit(ハイレゾ)で再生します。\n通常は16bitですが、まれに対応しているものがあります。\n音割れについては考慮されていないため、spcなど倍率を上げないといけないものは気をつけて下さい。", L"Play supported kpi at 32bit (hi-res).\nUsually 16bit, but some rarely support it.\nClipping is not considered, so be careful with formats needing higher gain such as spc.", L"Lire les kpi compatibles en 32 bits (haute resolution).\nHabituellement 16 bits, mais certains le prennent en charge.\nLe clipping n'est pas gere : attention aux formats a fort gain comme spc.", L"Riproduci i kpi supportati a 32 bit (alta risoluzione).\nDi solito 16 bit, ma alcuni lo supportano.\nIl clipping non e gestito: attenzione ai formati con gain alto come spc.", L"Reproducir los kpi compatibles a 32 bits (alta resolucion).\nNormalmente 16 bits, pero algunos lo admiten.\nNo hay proteccion contra clipping: cuidado con formatos de mayor ganancia como spc.", L"지원하는 kpi를 32bit(하이레조)로 재생합니다.\n보통은 16bit이지만 드물게 지원하는 것이 있습니다.\n음 깨짐은 고려되지 않으므로 spc 등 배율을 올려야 하는 것은 주의하세요.", L"以32bit（高解析度）播放支持的kpi。\n通常为16bit，偶尔有支持的。\n未考虑削波，spc等需要提高倍率的请注意。", L"تشغيل ملفات kpi المدعومة بدقة 32 بت (فائقة الدقة).\nعادةً تكون 16 بت، لكن بعضها يدعمها.\nلا يُراعى تقطيع الصوت، فانتبه للأنواع التي تحتاج رفع المضاعف مثل spc.", L"Воспроизведение поддерживаемых kpi в 32 бита (Hi-Res).\nОбычно 16 бит, но некоторые поддерживают.\nКлиппинг не учитывается — осторожно с форматами, требующими усиления, например spc.", L"Unterstutzte kpi in 32 Bit (Hi-Res) abspielen.\nUblich sind 16 Bit, manche unterstutzen mehr.\nClipping wird nicht berucksichtigt: Vorsicht bei Formaten mit hoher Verstarkung wie spc.", L"Reproduzir os kpi suportados em 32 bits (alta resolucao).\nNormalmente 16 bits, mas alguns suportam.\nO clipping nao e tratado: cuidado com formatos de ganho alto como spc.", L"Ondersteunde kpi in 32 bit (hi-res) afspelen.\nMeestal 16 bit, maar sommige ondersteunen het.\nClipping wordt niet meegerekend: let op formaten met hoge versterking zoals spc.", L"Odtwarzaj obslugiwane kpi w 32 bitach (hi-res).\nZwykle 16 bitow, ale niektore to obsluguja.\nPrzesterowanie nie jest uwzgledniane: uwaga na formaty o wysokim wzmocnieniu jak spc.", L"Desteklenen kpi'yi 32 bit (yuksek cozunurluk) calar.\nGenelde 16 bittir, nadiren destekleyenler var.\nKirpilma dikkate alinmaz; spc gibi carpani artirmak gerekenlere dikkat edin."));
	m_tooltip.AddTool(GetDlgItem(IDC_CHECK50), LL14(L"m4aを内蔵エンジンで演奏します。", L"Play m4a with built-in engine.", L"Lire m4a avec le moteur integre.", L"Riproduci m4a con motore integrato.", L"Reproducir m4a con motor integrado.", L"m4a를 내장 엔진으로 재생.", L"使用内置引擎播放 m4a。", L"تشغيل m4a بالمحرك المدمج.", L"Воспроизводить m4a встроенным движком.", L"m4a mit eingebauter Engine abspielen.", L"Reproduzir m4a com motor integrado.", L"m4a afspelen met ingebouwde engine.", L"Odtwarzaj m4a wbudowanym silnikiem.", L"m4a'yi dahili motorla cal."));
	m_tooltip.AddTool(GetDlgItem(IDC_CHECK52), LL14(L"スペアナの表示モードを切り替えます", L"Switch spectrum analyzer display mode", L"Changer le mode d'affichage du spectre", L"Cambia modalita visualizzazione spettro", L"Cambiar modo de visualizacion del espectro", L"스펙트럼 분석기 표시 모드 전환", L"切换频谱分析仪显示模式", L"تبديل وضع عرض محلل الطيف", L"Переключить режим отображения спектра", L"Spektrum-Anzeigemodus wechseln", L"Alternar modo de exibicao do espectro", L"Spectrumweergavemodus wisselen", L"Przelacz tryb wyswietlania spektrum", L"Spektrum gosterim modunu degistir"));
	m_tooltip.AddTool(GetDlgItem(IDC_COMBO_LANG), LL14(L"UI表示言語を切り替えます。\n設定を保存して再起動後に反映されます。", L"Switch UI language.\nTakes effect after saving and restarting.", L"Changer la langue de l'interface.\nPrend effet apres enregistrement et redemarrage.", L"Cambia lingua interfaccia.\nEffetto dopo salvataggio e riavvio.", L"Cambiar idioma de la interfaz.\nEfectivo tras guardar y reiniciar.", L"UI 표시 언어 전환.\n저장 후 재시작 시 적용.", L"切换界面语言。\n保存并重启后生效。", L"تبديل لغة الواجهة.\nيسري بعد الحفظ وإعادة التشغيل.", L"Сменить язык интерфейса.\nПосле сохранения и перезапуска.", L"UI-Sprache wechseln.\nNach Speichern und Neustart wirksam.", L"Alterar idioma da interface.\nApos salvar e reiniciar.", L"UI-taal wijzigen.\nNa opslaan en herstarten.", L"Zmien jezyk interfejsu.\nPo zapisaniu i restarcie.", L"Arayuz dilini degistir.\nKaydet ve yeniden baslat sonrasi gecerli."));
	m_tooltip.AddTool(GetDlgItem(IDC_COMBO3), LL14(L"再生するサンプルレートを設定します。\nサウンドカードが対応していない場合自動的に再生時対応上限まで下げます。", L"Set playback sample rate.\nAuto-lowers if sound card unsupported.", L"Regler la frequence d'echantillonnage.\nReduit automatiquement si la carte son ne supporte pas.", L"Imposta frequenza di campionamento.\nRiduce automaticamente se non supportata.", L"Ajustar frecuencia de muestreo.\nReduce automaticamente si no es compatible.", L"재생 샘플레이트 설정.\n사운드카드 미지원 시 자동으로 낮춤.", L"设置播放采样率。\n声卡不支持时自动降至上限。", L"تعيين معدل العينات.\nيُخفض تلقائياً إذا لم تدعمه البطاقة.", L"Задать частоту дискретизации.\nАвтопонижение, если карта не поддерживает.", L"Wiedergabe-Samplerate einstellen.\nAutomatisch reduzieren bei Nichtunterstutzung.", L"Definir taxa de amostragem.\nReduz automaticamente se nao suportada.", L"Afspeelfrequentie instellen.\nAutomatisch verlagen indien niet ondersteund.", L"Ustaw czestotliwosc probkowania.\nAuto-obnizanie gdy karta nie obsluguje.", L"Ornekleme hizini ayarla.\nSes karti desteklemezse otomatik dusurur."));
	m_tooltip.AddTool(GetDlgItem(IDC_CHECK_UPSCALE), LL14(L"設定したサンプルレート・ビット深度・チャンネルでDirectSoundに出力します。\nオフにするとソース形式のまま出力します。", L"Output to DirectSound at configured rate, bit depth, and channels.\nOff keeps the source format.", L"Sortie DirectSound au debit / bits / canaux configures.\nDesactive = format source.", L"Uscita DirectSound con frequenza, bit e canali impostati.\nSpento = formato sorgente.", L"Salida DirectSound con frecuencia, bits y canales configurados.\nApagado = formato de origen.", L"설정한 샘플레이트·비트·채널로 DirectSound 출력.\n끄면 소스 형식 유지.", L"按设置的采样率、位深和声道输出到 DirectSound。\n关闭则保持源格式。", L"إخراج DirectSound بالمعدل والبت والقنوات المضبوطة.\nإيقاف = تنسيق المصدر.", L"Вывод в DirectSound с заданной частотой, битностью и каналами.\nВыкл. — формат источника.", L"Ausgabe an DirectSound mit eingestellter Rate, Bittiefe und Kanalen.\nAus = Quellformat.", L"Saida DirectSound com taxa, bits e canais configurados.\nDesligado = formato da fonte.", L"DirectSound-uitvoer met ingestelde rate, bits en kanalen.\nUit = bronformaat.", L"Wyjscie DirectSound z ustawiona czestotliwoscia, bitami i kanalami.\nWyl. = format zrodla.", L"Ayarlanan hiz, bit ve kanallarla DirectSound cikisi.\nKapali = kaynak bicimi."));
	m_tooltip.AddTool(GetDlgItem(IDC_COMBO_SPEAKER), LL14(L"アップスケール時の出力チャンネル配置（2ch / 2.1 / 4ch / 5.1 / 7.1 / マッピングなし）を選びます。マッピングなしはソースのチャンネル数のまま、レート・ビット深度のみ変換します。", L"Speaker layout when upscaling (2ch / 2.1 / 4ch / 5.1 / 7.1 / no mapping). No mapping keeps source channel count; only rate and bit depth change.", L"Disposition haut-parleurs en upscaling (2ch / 2.1 / 4ch / 5.1 / 7.1 / sans mappage). Sans mappage : meme nombre de canaux, seuls debit et bits changent.", L"Layout altoparlanti (2ch / 2.1 / 4ch / 5.1 / 7.1 / nessun mapping). Nessun mapping: stessi canali, solo frequenza e bit.", L"Disposicion de altavoces (2ch / 2.1 / 4ch / 5.1 / 7.1 / sin mapeo). Sin mapeo: mismos canales; solo tasa y bits.", L"업스케일 시 스피커(2ch/2.1/4ch/5.1/7.1/매핑 없음). 매핑 없음은 소스 채널 수 유지, 레이트·비트만 변환.", L"升频时的扬声器布局（含不映射声道）。不映射则保持源声道数，仅转换采样率与位深。", L"تخطيط السماعات مع خيار بدون تعيين. بدون تعيين: نفس عدد القنوات؛ تغيير المعدل والبت فقط.", L"Раскладка каналов при апскейле; «без маппинга» сохраняет число каналов источника, меняются только частота и битность.", L"Lautsprecher-Layout; „kein Mapping“ behalt Kanalzahl, nur Rate/Bits.", L"Layout de altifalante; sem mapeamento mantem canais da fonte, so taxa e bits.", L"Luidsprekerindeling; geen mapping behoudt bronkanalen, alleen rate en bits.", L"Uklad kanalow; bez mapowania = ta sama liczba kanalow, zmiana tylko czestotliwosci i bitow.", L"Hoparlor duzeni; esleme yok kaynak kanal sayisini korur, yalnizca hiz ve bit derinligi degisir."));
	m_tooltip.AddTool(GetDlgItem(IDC_COMBO4), LL14(L"スペアナで表示する表示方法を選択します。\n使う時は横のチェックボックスにチェックを入れてください\n音階：88鍵盤として表示します\n周波数帯：周波数として表示します\n標準：既定の見やすい形のスペアナで表示します", L"Select spectrum display.\nCheck the box to use.\nScale: 88-key piano\nFreq band: frequency view\nStandard: default spectrum", L"Choisir l'affichage du spectre.\nCochez la case pour activer.\nGamme : clavier 88 touches\nBandes : frequences\nStandard : spectre par defaut", L"Scegli visualizzazione spettro.\nSpunta la casella per usare.\nScale : tastiera 88 tasti\nBande : frequenze\nStandard : spettro predefinito", L"Elegir visualizacion del espectro.\nMarca la casilla para usar.\nEscala : piano 88 teclas\nBandas : frecuencias\nEstandar : espectro predeterminado", L"스펙트럼 표시 방식 선택.\n사용 시 옆 체크박스 선택.\n음계: 88건반\n주파수대: 주파수\n표준: 기본 스펙트럼", L"选择频谱显示方式。\n使用时请勾选旁边复选框。\n音阶：88键\n频段：频率\n标准：默认频谱", L"اختر عرض الطيف.\nحدّد المربع للاستخدام.\nسلم : 88 مفتاحاً\nنطاق : تردد\nقياسي : الطيف الافتراضي", L"Выбрать отображение спектра.\nОтметьте флажок для включения.\nГамма: 88 клавиш\nПолосы: частоты\nСтандарт: обычный спектр", L"Spektrum-Anzeige wahlen.\nKastchen ankreuzen zum Aktivieren.\nTonleiter: 88 Tasten\nBanden: Frequenzen\nStandard: Default-Spektrum", L"Escolher exibicao do espectro.\nMarque a caixa para usar.\nEscala: 88 teclas\nBandas: frequencias\nPadrao: espectro padrao", L"Spectrumweergave kiezen.\nVink aan om te gebruiken.\nToonladder: 88 toetsen\nBanden: frequenties\nStandaard: standaardspectrum", L"Wybierz wyswietlanie widma.\nZaznacz pole aby uzyc.\nSkala: 88 klawiszy\nPasma: czestotliwosci\nStandard: domyslne widmo", L"Spektrum gosterimini sec.\nKullanmak icin kutuyu isaretle.\nDizi: 88 tus\nBant: frekans\nStandart: varsayilan spektrum"));
	m_tooltip.AddTool(GetDlgItem(IDC_BUTTON1), LL14(L"碧の軌跡用のt_bgm._dtを設定します。", L"Set t_bgm._dt for Ao no Kiseki.", L"Definir t_bgm._dt pour Ao no Kiseki.", L"Imposta t_bgm._dt per Ao no Kiseki.", L"Establecer t_bgm._dt para Ao no Kiseki.", L"Ao no Kiseki용 t_bgm._dt 설정.", L"设置碧之轨迹的 t_bgm._dt。", L"تعيين t_bgm._dt لـ Ao no Kiseki.", L"Задать t_bgm._dt для Ao no Kiseki.", L"t_bgm._dt fur Ao no Kiseki festlegen.", L"Definir t_bgm._dt para Ao no Kiseki.", L"t_bgm._dt instellen voor Ao no Kiseki.", L"Ustaw t_bgm._dt dla Ao no Kiseki.", L"Ao no Kiseki icin t_bgm._dt ayarla"));
	m_tooltip.AddTool(GetDlgItem(IDCANCEL5), LL14(
		L"関連付けに追加します（音声・動画・プレイリスト）。\nwin10以降は「アプリで開く」候補への登録が主です。既定変更はOS側の設定が必要なことがあります。",
		L"Add file associations (audio, video, playlists).\nOn Win10+ this mainly registers as an Open With candidate; default may need OS settings.",
		L"Ajouter des associations (audio, video, listes).\nSous Win10+ surtout candidat Ouvrir avec.",
		L"Aggiunge associazioni (audio, video, playlist).\nSu Win10+ soprattutto Apri con.",
		L"Anade asociaciones (audio, video, listas).\nEn Win10+ principalmente Abrir con.",
		L"파일 연결 추가(음성·동영상·재생목록).\nWin10+는 '연결 프로그램' 후보 등록이 주입니다.",
		L"添加文件关联（音频、视频、播放列表）。\nWin10+ 主要为“打开方式”候选。",
		L"إضافة ارتباطات (صوت/فيديو/قوائم).\nفي Win10+ غالباً مرشح فتح باستخدام.",
		L"Добавить ассоциации (аудио, видео, плейлисты).\nНа Win10+ в основном «Открыть с помощью».",
		L"Zuordnungen hinzufugen (Audio, Video, Playlists).\nUnter Win10+ vor allem Offnen mit.",
		L"Adiciona associacoes (audio, video, playlists).\nNo Win10+ sobretudo Abrir com.",
		L"Koppelingen toevoegen (audio, video, playlists).\nOp Win10+ vooral Openen met.",
		L"Dodaje powiazania (audio, wideo, playlisty).\nNa Win10+ glownie Otworz za pomoca.",
		L"Iliskilendirme ekler (ses, video, listeler).\nWin10+'da genellikle Birlikte Ac."));
	m_tooltip.AddTool(GetDlgItem(IDC_CHECK_lrc), LL14(L"歌詞情報をネットから参照するようにします。\n数パターン試すため少し再生までに時間かかります。", L"Fetch lyrics from network.\nMay take longer to start playback.", L"Recuperer les paroles sur le reseau.\nPeut retarder le demarrage de la lecture.", L"Recupera testi dalla rete.\nPuo ritardare l'avvio della riproduzione.", L"Obtener letras de la red.\nPuede tardar mas en iniciar la reproduccion.", L"가사 정보를 네트워크에서 조회.\n시도가 여러 번이라 재생 시작이 다소 지연될 수 있음.", L"从网络获取歌词。\n需尝试多种来源，播放可能稍慢。", L"جلب كلمات الأغاني من الشبكة.\nقد يتأخر بدء التشغيل.", L"Загружать текст песен из сети.\nСтарт воспроизведения может занять больше времени.", L"Texte aus dem Netz laden.\nWiedergabestart kann langer dauern.", L"Buscar letras na rede.\nPode demorar para iniciar reproducao.", L"Teksten ophalen via netwerk.\nAfspelen kan langer op starten.", L"Pobieraj teksty z sieci.\nStart odtwarzania moze trwac dluzej.", L"Sozleri agdan al.\nCalma baslangici biraz gecikebilir."));
	m_tooltip.AddTool(GetDlgItem(IDC_SLIDER3), LL14(L"演奏のバッファ処理での割り込み時間を設定します。\n少なすぎると音飛びする可能性があります。", L"Set buffer interrupt time.\nToo low may cause audio glitches.", L"Regler le temps d'interruption du tampon.\nTrop bas peut provoquer des saccades audio.", L"Imposta tempo di interruzione buffer.\nTroppo basso puo causare salti audio.", L"Ajustar tiempo de interrupcion del buffer.\nMuy bajo puede causar cortes de audio.", L"버퍼 처리 인터럽트 시간 설정.\n너무 낮으면 끊김 가능.", L"设置播放缓冲中断时间。\n过低可能导致跳音。", L"ضبط وقت مقاطعة المخزن المؤقت.\nالمنخفض جداً قد يسبب تقطيعاً.", L"Задать время прерывания буфера.\nСлишком мало — возможны сбои звука.", L"Puffer-Unterbrechungszeit einstellen.\nZu niedrig kann Knackser verursachen.", L"Definir tempo de interrupcao do buffer.\nMuito baixo pode causar falhas de audio.", L"Buffer-onderbrekingstijd instellen.\nTe laag kan haperingen geven.", L"Ustaw czas przerwania bufora.\nZa niski moze powodowac przeskakiwanie.", L"Tampon kesme suresini ayarla.\nCok dusuk ses atlatabilir."));
	m_tooltip.AddTool(GetDlgItem(IDC_SLIDER5), LL14(L"描画の間隔時間を設定します。\nCPU使用が高いときに上げます。", L"Set render interval.\nIncrease when CPU usage is high.", L"Regler l'intervalle de rendu.\nAugmentez si le CPU est charge.", L"Imposta intervallo di rendering.\nAumenta se il CPU e sotto carico.", L"Ajustar intervalo de renderizado.\nSube si el CPU esta alto.", L"그리기 간격 설정.\nCPU 사용률이 높을 때 늘리세요.", L"设置绘制间隔。\nCPU 占用高时可增大。", L"ضبط فترة الرسم.\nزِدها عند ارتفاع استخدام المعالج.", L"Задать интервал отрисовки.\nУвеличьте при высокой нагрузке на CPU.", L"Render-Intervall einstellen.\nBei hoher CPU-Last erhohen.", L"Definir intervalo de renderizacao.\nAumente se a CPU estiver alta.", L"Renderinterval instellen.\nVerhoog bij hoge CPU-belasting.", L"Ustaw odstep renderowania.\nZwieksz przy wysokim obciazeniu CPU.", L"Cizim araligini ayarla.\nCPU yuksekken artir."));
	m_tooltip.AddTool(GetDlgItem(IDC_SLIDER_EQCODE), LL14(L"EQコード表示の更新間隔を設定します。\n短くすると追従が速く、長くすると負荷が下がります。", L"Set EQ chord display update interval.\nShorter = faster tracking; longer = lower load.", L"Intervalle de maj des accords EQ.\nPlus court = plus reactif; plus long = moins de charge.", L"Intervallo aggiornamento accordi EQ.\nPiu corto = piu reattivo; piu lungo = meno carico.", L"Intervalo de actualizacion de acordes EQ.\nMas corto = mas reactivo; mas largo = menos carga.", L"EQ 코드 표시 갱신 간격.\n짧을수록 빠른 추종, 길수록 부하 감소.", L"设置 EQ 和弦显示更新间隔。\n越短跟随越快，越长负载越低。", L"ضبط فاصل تحديث أكورد EQ.\nأقصر=تتبع أسرع؛ أطول=حمل أقل.", L"Интервал обновления аккордов EQ.\nКороче — быстрее; дольше — меньше нагрузка.", L"Update-Intervall der EQ-Akkorde.\nKuerzer = schneller; laenger = weniger Last.", L"Intervalo de atualizacao dos acordes EQ.\nMais curto = mais rapido; mais longo = menos carga.", L"Update-interval EQ-akkoorden.\nKorter = sneller; langer = minder belasting.", L"Interwal odswiezania akordow EQ.\nKrotszy = szybciej; dluzszy = mniejsze obciazenie.", L"EQ akor guncelleme araligi.\nKisa = daha hizli; uzun = daha az yuk."));
	m_tooltip.AddTool(GetDlgItem(IDC_SLIDER6), LL14(L"スペアナの表示倍率を設定します。", L"Set spectrum display scale.", L"Regler l'echelle d'affichage du spectre.", L"Imposta scala visualizzazione spettro.", L"Ajustar escala de visualizacion del espectro.", L"스펙트럼 표시 배율 설정.", L"设置频谱显示倍率。", L"ضبط مقياس عرض الطيف.", L"Задать масштаб отображения спектра.", L"Spektrum-Anzeigeskala einstellen.", L"Definir escala de exibicao do espectro.", L"Spectrumweergaveschaal instellen.", L"Ustaw skale wyswietlania spektrum.", L"Spektrum gosterim olcegini ayarla."));

	CCustomControlUtility::FinalizeDialogToolTip(m_tooltip, 512, 10000);
	CCC_CaptionLayout(m_hWnd);
	LayoutHelpBtn();

	m_ms.SetMode(1);	m_hyouji2.SetMode(1);
	m_eqCode.SetMode(1);
	w_wups.SetMode(1);
	m_evr.SetCheck(savedata.evr);
	m_con.SetCheck(savedata.con);
	m_a.SetCheck(savedata.aero);
	COSVersion os;
	os.GetVersionString();
	if (os.in.dwMajorVersion < 6)
		m_a.ShowWindow(SW_HIDE);

	m_ffd.SetCheck(savedata.ffd);
	m_vob.SetCheck(savedata.vob);
	m_haali.SetCheck(savedata.haali);
	m_speana.SetCheck(savedata.speanamode);
	extern CPlayList* pl;
	extern COggDlg* og;
	extern int ip;
	ip = 0;
	og->KillTimer(4923);
	og->KillTimer(4924);
	if (pl) {
		pl->KillTimer(4923);
		pl->KillTimer(4924);
	}
#if WIN64
	m_kpi.EnableWindow(FALSE);
#else
#endif
	m_ms.SetRange(30, 80);
	if (savedata.ms < 30) savedata.ms = 30;
	m_ms.SetPos(savedata.ms);
	if (savedata.ms > 80) savedata.ms = 80;
	m_hyouji2.SetRange(1, 60);
	int ms2Pos = (savedata.ms2 + 15) / 16;
	if (ms2Pos < 1) ms2Pos = 1;
	if (ms2Pos > 60) ms2Pos = 60;
	m_hyouji2.SetPos(ms2Pos);
	m_eqCode.SetRange(16, 200);
	if (savedata.eqCodeMs < 16) savedata.eqCodeMs = 25;
	if (savedata.eqCodeMs > 200) savedata.eqCodeMs = 200;
	m_eqCode.SetPos(savedata.eqCodeMs);
	{
		const wchar_t* msUnit = LL14(L"ms", L"ms", L"ms", L"ms", L"ms", L"ms", L"毫秒", L"ms", L"мс", L"ms", L"ms", L"ms", L"ms", L"ms");
		CString s; s.Format(L"%d%s", savedata.ms, msUnit);
		m_ms2.SetWindowText(s);
		CString s2; s2.Format(L"%d%s", ms2Pos * 16, msUnit);
		m_hyouji3.SetWindowText(s2);
		CString s3; s3.Format(L"%d%s", savedata.eqCodeMs, msUnit);
		m_eqCodeMs.SetWindowText(s3);
	}
	SetTimer(11, 100, NULL);
	w_wups.SetRange(100, 1000);
	w_wups.SetPos((int)savedata.wup);

	//sl = &m_soundlist;
	slgc = 0;
	m_soundlist.Clear();
	DirectSoundEnumerate(DSEnumCallback, NULL);
	for (int k = 0; k < slgc; k++) {
		m_soundlist.AddString(sls[k]);
	}
	m_soundlist.SetCurSel(savedata.soundcur);

	// マイク(録音)端末: 共通列挙（savedata.mic_device）
	AudioMicDevFillCombo(m_miclist);

	if (!pGraphBuilder)
		m_l.EnableWindow(FALSE);
	CString abc = savedata.zero;
	if (abc == L"") {
		m_ao.ShowWindow(FALSE);
	}
	// { 11025, 12000, 22050, 24000, 44100, 48000, 96000, 192000, 384000, 768000, 1536000, 3072000 };
	m_Hz.AddString(LL14(L"--低周波数帯- イコライザーでアップスケール対応し処理される", L"--Low freq- EQ upscale processed", L"--Basses fréq.- Traité par upscaling EQ", L"--Basse freq.- Elaborato con upscaling EQ", L"--Baja freq.- Procesado con upscaling EQ", L"--저주파 대역- 이퀄라이저 업스케일로 처리됨", L"--低频段- 均衡器升频处理后处理", L"--ترددات منخفضة- يُعالج بتكبير EQ", L"--Низкие част.- Обрабатывается с upscaling EQ", L"--Niedrige Freq.- Mit EQ-Upscaling verarbeitet", L"--Baixa freq.- Processado com upscaling EQ", L"--Lage freq.- Verwerkt met EQ-upscaling", L"--Niskie częst.- Przetwarzane z upscaling EQ", L"--Düşük frek.- EQ upscaling ile işlenir"),TRUE);
	m_Hz.AddString(L"11025");
	m_Hz.AddString(L"12000");
	m_Hz.AddString(L"22050");
	m_Hz.AddString(L"24000");
	m_Hz.AddString(LL14(L"--通常波数帯- イコライザー通常処理される", L"--Normal freq- EQ normal processed", L"--Fréq. normales- Traitement EQ normal", L"--Freq. normali- Elaborazione EQ normale", L"--Freq. normal- Procesado EQ normal", L"--일반 주파 대역- 이퀄라이저 일반 처리", L"--通常频段- 均衡器正常处理", L"--ترددات عادية- معالجة EQ عادية", L"--Обычные част.- Обычная обработка EQ", L"--Normale Freq.- Normale EQ-Verarbeitung", L"--Freq. normal- Processamento EQ normal", L"--Normale freq.- Normale EQ-verwerking", L"--Normalne częst.- Normalne przetwarzanie EQ", L"--Normal frek.- Normal EQ işleme"), TRUE);
	m_Hz.AddString(L"44100");
	m_Hz.AddString(L"48000");
	m_Hz.AddString(L"96000");
	m_Hz.AddString(L"192000");
	m_Hz.AddString(LL14(L"--高周波数帯- イコライザー処理されない場合がある", L"--High freq- EQ may not process", L"--Hautes fréq.- L'égaliseur peut ne pas traiter", L"--Alte freq.- L'EQ potrebbe non elaborare", L"--Alta freq.- El ecualizador puede no procesar", L"--고주파 대역- 이퀄라이저가 처리하지 않을 수 있음", L"--高频段- 均衡器可能无法处理", L"--ترددات عالية- قد لا يعالج المعادل", L"--Высокие част.- EQ может не обрабатывать", L"--Hohe Freq.- EQ verarbeitet ggf. nicht", L"--Alta freq.- O equalizador pode não processar", L"--Hoge freq.- EQ verwerkt mogelijk niet", L"--Wysokie częst.- EQ może nie przetwarzać", L"--Yüksek frek.- EQ işlemeyebilir"), TRUE);
	m_Hz.AddString(L"384000");
	m_Hz.AddString(L"768000");
	m_Hz.AddString(L"1536000");
	m_Hz.AddString(L"3072000 ");
	for (int l = 0; l < 12; l++) {
		if (savedata.samples == samp[l]) {
			m_Hz.SetCurSel(l);
			break;
		}
	}

	m_speana_num.AddString(LL14(L"音階", L"Scale", L"Gamme", L"Scala", L"Escala", L"음계", L"音阶", L"مقياس", L"Гамма", L"Tonleiter", L"Escala", L"Toonladder", L"Skala", L"Ölçek"));
	m_speana_num.AddString(LL14(L"低周波帯特化", L"Low freq focus", L"Focus basse fréquence", L"Focus basse frequenze", L"Enfoque bajas frecuencias", L"저주파 특화", L"低频特化", L"تركيز التردد المنخفض", L"Фокус низких частот", L"Tieffrequenz-Fokus", L"Foco baixas frequências", L"Laagfrequent focus", L"Fokus niskich częstotliwości", L"Düşük frekans odak"));
	m_speana_num.AddString(LL14(L"標準", L"Standard", L"Standard", L"Standard", L"Estándar", L"표준", L"标准", L"قياسي", L"Стандарт", L"Standard", L"Padrão", L"Standaard", L"Standard", L"Standart"));
	m_speana_num.AddString(LL14(L"高周波帯", L"High freq", L"Haute fréquence", L"Alta frequenza", L"Alta frecuencia", L"고주파대", L"高频带", L"التردد العالي", L"Высокие частоты", L"Hochfrequenz", L"Alta frequência", L"Hogefrequent", L"Wysokie częstotliwości", L"Yüksek frekans"));
	m_speana_num.AddString(LL14(L"音声特化", L"Voice focus", L"Focus vocal", L"Focus voce", L"Enfoque voz", L"음성 특화", L"人声特化", L"تركيز الصوت", L"Фокус голоса", L"Stimmen-Fokus", L"Foco vocal", L"Stemfocus", L"Fokus głosu", L"Ses odak"));
	m_speana_num.SetCurSel(savedata.speananum);


	m_netlrc.SetCheck(savedata.lrc_net);

	if (savedata.aero && !CCC_IsWin11()) {
		ReleaseRenderGrassBackdrop();
		renderbase = new CImageBase;
		renderbase->Create(NULL);
		renderbase->oya = this;
	}
	else {
		ReleaseRenderGrassBackdrop();
	}
	CRect r;
	GetWindowRect(&r);
	if (renderbase && renderbase->GetSafeHwnd())
		renderbase->MoveWindow(&r);
	// TOPMOST 禁止: 他UIがメインになったとき下に回せる。グラスはダイアログの直下へ。
	::SetWindowPos(m_hWnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
	if (renderbase && renderbase->GetSafeHwnd())
		::SetWindowPos(renderbase->m_hWnd, m_hWnd, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

	EnableMainWindowLock(&savedata.renderMainLock, TRUE);
	CCC_MainLockSetHeaderRow(m_hWnd, 0, 18);
	CCC_MainLockBringToFront(m_hWnd);
	CCC_CaptionLayout(m_hWnd);
	LayoutHelpBtn();
	return TRUE;  // コントロールにフォーカスを設定しないとき、戻り値は TRUE となります
	              // 例外: OCX プロパティ ページの戻り値は FALSE となります
}

void CRender::LayoutHelpBtn()
{
	CCC_CaptionPlaceHelpBtn(m_hWnd, &m_help);
}

void CRender::ShowHelpSheet()
{
	if (g_rdHelpDlg && ::IsWindow(g_rdHelpDlg->GetSafeHwnd())) {
		CCC_PresentOwnedHelp(g_rdHelpDlg, this);
		return;
	}
	if (g_rdHelpDlg && !::IsWindow(g_rdHelpDlg->GetSafeHwnd()))
		g_rdHelpDlg = nullptr;
	// オーナー付きモードレス。ヘルプはオーナー上、他UI前面時は下へ（TOPMOSTしない）
	CRdHelpDlg* dlg = new CRdHelpDlg(this);
	if (!dlg->Create(IDD_RD_HELP, this)) {
		delete dlg;
		return;
	}
	g_rdHelpDlg = dlg;
	CCC_PresentOwnedHelp(dlg, this);
}

void CRender::OnBnClickedHelp()
{
	ShowHelpSheet();
}

void CRender::OnSize(UINT nType, int cx, int cy)
{
	CCustomBlurDialogExBase::OnSize(nType, cx, cy);
	if (nType != SIZE_MINIMIZED) {
		CCC_CaptionLayout(m_hWnd);
		LayoutHelpBtn();
	}
}

void CRender::OnDestroy()
{
	AudioMicDevUnregisterCombo(&m_miclist);
	if (g_rdHelpDlg && ::IsWindow(g_rdHelpDlg->GetSafeHwnd()))
		g_rdHelpDlg->DestroyWindow();
	CCustomBlurDialogExBase::OnDestroy();
}
BOOL CALLBACK CRender::DSEnumCallback(LPGUID pGUID, LPCWSTR strDesc,LPCWSTR strDrvName, LPVOID pContext)
{
	if (pGUID)
	{
		sls[slgc] = strDesc;
		CopyMemory(&slg[slgc], pGUID, sizeof(GUID));
		slgc++;
	}

	return TRUE;
}

void CRender::OnOK() 
{
	// TODO: この位置にその他の検証用のコードを追加してください
	savedata.render=m_1.GetCurSel();
	savedata.evr=m_evr.GetCheck();
	savedata.con=m_con.GetCheck();
	savedata.aero=m_a.GetCheck();
	savedata.ffd=m_ffd.GetCheck();
	savedata.vob=m_vob.GetCheck();
	savedata.haali=m_haali.GetCheck();
	savedata.audiost=m_audiost.GetCheck();
	savedata.bit24 = m_24.GetCheck();
	savedata.bit32 = m_32bit.GetCheck();
	savedata.m4a = m_m4a.GetCheck();
	savedata.ms = m_ms.GetPos();
	savedata.ms2 = m_hyouji2.GetPos() * 16;
	savedata.eqCodeMs = m_eqCode.GetPos();
	if (savedata.eqCodeMs < 16) savedata.eqCodeMs = 16;
	if (savedata.eqCodeMs > 500) savedata.eqCodeMs = 500;
	savedata.samples = samp[m_Hz.GetCurSel()];
	savedata.speanamode = m_speana.GetCheck();
	savedata.speananum = m_speana_num.GetCurSel();
	savedata.lang = m_comboLang.GetCurSel();

	//	savedata.mp3orig=m_mp3orig.GetCheck();
	ReleaseRenderGrassBackdrop();
	CCustomBlurDialogExBase::OnOK();
}

INT_PTR CRender::OnToolHitTest(CPoint point, TOOLINFO* pTI) const
{
	// TODO: ここに特定なコードを追加するか、もしくは基本クラスを呼び出してください。

	return CCustomBlurDialogExBase::OnToolHitTest(point, pTI);
}

BOOL CRender::PreTranslateMessage(MSG* pMsg)
{
	// TODO: ここに特定なコードを追加するか、もしくは基本クラスを呼び出してください。
		m_tooltip.RelayEvent(pMsg);

	return CCustomBlurDialogExBase::PreTranslateMessage(pMsg);
}

void CRender::OnBnClickedCancel2()
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
	CGraph *a = new CGraph(CWnd::FromHandle(GetSafeHwnd()));
	::SetWindowPos(m_hWnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
	if (renderbase)::SetWindowPos(renderbase->m_hWnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
	if(pGraphBuilder)
		a->DoModal();
	delete a;
}

void CRender::Onspc2x()
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
	m_spc2x.SetCheck(TRUE);
	m_spc4x.SetCheck(FALSE);
	m_spc8x.SetCheck(FALSE);
	m_spc16x.SetCheck(FALSE);
	m_spc1x.SetCheck(FALSE);
	savedata.spc=2;
}

void CRender::Onspc4x()
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
	m_spc2x.SetCheck(FALSE);
	m_spc4x.SetCheck(TRUE);
	m_spc8x.SetCheck(FALSE);
	m_spc16x.SetCheck(FALSE);
	m_spc1x.SetCheck(FALSE);
	savedata.spc=4;
}

void CRender::Onspc8x()
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
	m_spc2x.SetCheck(FALSE);
	m_spc4x.SetCheck(FALSE);
	m_spc8x.SetCheck(TRUE);
	m_spc16x.SetCheck(FALSE);
	m_spc1x.SetCheck(FALSE);
	savedata.spc=8;
}

void CRender::Onspc1x()
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
	m_spc2x.SetCheck(FALSE);
	m_spc4x.SetCheck(FALSE);
	m_spc8x.SetCheck(FALSE);
	m_spc16x.SetCheck(FALSE);
	m_spc1x.SetCheck(TRUE);
	savedata.spc=1;
}

void CRender::Onspc16x()
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
	m_spc2x.SetCheck(FALSE);
	m_spc4x.SetCheck(FALSE);
	m_spc8x.SetCheck(FALSE);
	m_spc16x.SetCheck(TRUE);
	m_spc1x.SetCheck(FALSE);
	savedata.spc=16;
}

void CRender::Onmp31()
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
	m_mp31.SetCheck(TRUE);
	m_mp315.SetCheck(FALSE);
	m_mp32.SetCheck(FALSE);
	m_mp325.SetCheck(FALSE);
	m_mp33.SetCheck(FALSE);
	savedata.mp3=1;
}

void CRender::Onmp315()
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
	m_mp31.SetCheck(FALSE);
	m_mp315.SetCheck(TRUE);
	m_mp32.SetCheck(FALSE);
	m_mp325.SetCheck(FALSE);
	m_mp33.SetCheck(FALSE);
	savedata.mp3=2;
}

void CRender::Onmp32()
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
	m_mp31.SetCheck(FALSE);
	m_mp315.SetCheck(FALSE);
	m_mp32.SetCheck(TRUE);
	m_mp325.SetCheck(FALSE);
	m_mp33.SetCheck(FALSE);
	savedata.mp3=3;
}

void CRender::Onmp325()
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
	m_mp31.SetCheck(FALSE);
	m_mp315.SetCheck(FALSE);
	m_mp32.SetCheck(FALSE);
	m_mp325.SetCheck(TRUE);
	m_mp33.SetCheck(FALSE);
	savedata.mp3=4;
}

void CRender::Onmp33()
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
	m_mp31.SetCheck(FALSE);
	m_mp315.SetCheck(FALSE);
	m_mp32.SetCheck(FALSE);
	m_mp325.SetCheck(FALSE);
	m_mp33.SetCheck(TRUE);
	savedata.mp3=5;
}

void CRender::Onkpi10()
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
	m_kpi10.SetCheck(TRUE);
	m_kpi15.SetCheck(FALSE);
	m_kpi20.SetCheck(FALSE);
	m_kpi25.SetCheck(FALSE);
	m_kpi30.SetCheck(FALSE);
	savedata.kpivol=1;
}

void CRender::Onkpi15()
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
	m_kpi10.SetCheck(FALSE);
	m_kpi15.SetCheck(TRUE);
	m_kpi20.SetCheck(FALSE);
	m_kpi25.SetCheck(FALSE);
	m_kpi30.SetCheck(FALSE);
	savedata.kpivol=2;
}

void CRender::Onkpi20()
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
	m_kpi10.SetCheck(FALSE);
	m_kpi15.SetCheck(FALSE);
	m_kpi20.SetCheck(TRUE);
	m_kpi25.SetCheck(FALSE);
	m_kpi30.SetCheck(FALSE);
	savedata.kpivol=3;
}

void CRender::Onkpi25()
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
	m_kpi10.SetCheck(FALSE);
	m_kpi15.SetCheck(FALSE);
	m_kpi20.SetCheck(FALSE);
	m_kpi25.SetCheck(TRUE);
	m_kpi30.SetCheck(FALSE);
	savedata.kpivol=4;
}

void CRender::Onkpi30()
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
	m_kpi10.SetCheck(FALSE);
	m_kpi15.SetCheck(FALSE);
	m_kpi20.SetCheck(FALSE);
	m_kpi25.SetCheck(FALSE);
	m_kpi30.SetCheck(TRUE);
	savedata.kpivol=5;
}

void CRender::OnBnClicked24bit()
{
	savedata.bit24 = m_24.GetCheck();
	if (og) RenderRecreateSecondarySound(og);
}

void CRender::OnBnClicked32bit()
{
	savedata.bit32 = m_32bit.GetCheck();
	if (og) RenderRecreateSecondarySound(og);
}

void CRender::OnBnClickedCheck50()
{
	// TODO: ここにコントロール通知ハンドラー コードを追加します。
	savedata.m4a = m_m4a.GetCheck();
}



#include "Kpilist.h"
void CRender::Onkpi()
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
	SetTimer(7000, 300, NULL);
}

extern HFONT	hFont;
#include "afxdlgs.h"
void CRender::OnFontMain()
{
	LOGFONT dlgLogFont;
	memset(&dlgLogFont, 0, sizeof(LOGFONT));
	
	if (_tcslen(savedata.font1) > 0 && DeserializeLogFont(savedata.font1, &dlgLogFont)) {
		// Loaded saved font attributes successfully
	} else {
		LOGFONT      logFont;
		CFont* f = CFont::FromHandle(hFont);
		if (f) {
			f->GetLogFont(&logFont);
		} else {
			memset(&logFont, 0, sizeof(LOGFONT));
			_tcscpy(logFont.lfFaceName, _T("ＭＳ ゴシック"));
		}
		dlgLogFont = logFont;
		dlgLogFont.lfHeight = -16; // Standard size for editing
		dlgLogFont.lfWidth = 0;
		if (_tcslen(savedata.font1) > 0) {
			_tcscpy(dlgLogFont.lfFaceName, savedata.font1);
		}
	}

	CFontDialog fontDlg(&dlgLogFont);
	if (fontDlg.DoModal() == IDOK){
		SerializeLogFont(fontDlg.m_cf.lpLogFont, savedata.font1, 1024);
		
		DeleteObject(hFont);
		LOGFONT logFont = *(fontDlg.m_cf.lpLogFont);
		logFont.lfHeight *= 4;
		logFont.lfWidth *= 4;
		hFont = CreateFontIndirect(&logFont);
		
		if (og) {
			og->Invalidate();
			og->RedrawWindow();
		}
	}
}

#include "PlayList.h"
extern CPlayList *pl;
void CRender::OnFontList()
{
	if (!pl) return;
	LOGFONT dlgLogFont;
	memset(&dlgLogFont, 0, sizeof(LOGFONT));

	if (_tcslen(savedata.font2) > 0 && DeserializeLogFont(savedata.font2, &dlgLogFont)) {
		// Loaded saved list font successfully
	} else {
		LOGFONT      logFont;
		CFont* f = pl->m_lc.GetFont();
		if (f) {
			f->GetLogFont(&logFont);
		} else {
			memset(&logFont, 0, sizeof(LOGFONT));
			_tcscpy(logFont.lfFaceName, _T("メイリオ"));
			logFont.lfHeight = -15;
			logFont.lfCharSet = SHIFTJIS_CHARSET;
		}
		dlgLogFont = logFont;
		if (_tcslen(savedata.font2) > 0) {
			_tcscpy(dlgLogFont.lfFaceName, savedata.font2);
		}
	}

	CFontDialog fontDlg(&dlgLogFont, CF_SCREENFONTS);
	if (fontDlg.DoModal() == IDOK){
		if (pl->m_fontList.GetSafeHandle()) {
			pl->m_fontList.DeleteObject();
		}
		if (pl->m_fontList.CreateFontIndirect(fontDlg.m_cf.lpLogFont)) {
			pl->m_lc.SetFont(&pl->m_fontList, TRUE);
			pl->m_find.SetFont(&pl->m_fontList, TRUE);
			SerializeLogFont(fontDlg.m_cf.lpLogFont, savedata.font2, 1024);
			pl->m_lc.Invalidate();
			pl->m_find.Invalidate();
		}
	}
}


void CRender::OnBnClickedOk()
{
	// TODO: ここにコントロール通知ハンドラー コードを追加します。
	savedata.render = m_1.GetCurSel();
	savedata.evr = m_evr.GetCheck();
	savedata.con = m_con.GetCheck();
	savedata.aero = m_a.GetCheck();
	savedata.ffd = m_ffd.GetCheck();
	savedata.vob = m_vob.GetCheck();
	savedata.haali = m_haali.GetCheck();
	savedata.audiost = m_audiost.GetCheck();
	savedata.bit24 = m_24.GetCheck();
	savedata.bit32 = m_32bit.GetCheck();
	savedata.m4a = m_m4a.GetCheck();
	savedata.lrc_net = m_netlrc.GetCheck();
	savedata.ms = m_ms.GetPos();
	savedata.ms2 = m_hyouji2.GetPos() * 16;
	savedata.eqCodeMs = m_eqCode.GetPos();
	if (savedata.eqCodeMs < 16) savedata.eqCodeMs = 16;
	if (savedata.eqCodeMs > 500) savedata.eqCodeMs = 500;
	savedata.speanamode = m_speana.GetCheck();
	savedata.speananum = m_speana_num.GetCurSel();
	savedata.lang = m_comboLang.GetCurSel();
	int ihz = m_Hz.GetCurSel();
	if (ihz >= 0 && ihz < 12)
		savedata.samples = samp[ihz];
	savedata.upscale_enable = m_upscale.GetCheck() ? 1 : 0;
	int sp = m_speaker.GetCurSel();
	savedata.speaker_layout = (sp >= 0 && sp <= 5) ? sp : 0;
	int dev = m_soundlist.GetCurSel();
	if (dev >= 0) {
		memcpy(&savedata.soundguid, &slg[dev], sizeof(GUID));
		if (dev == 0)
			savedata.soundguid = { 0,0,0,0 };
		savedata.soundcur = dev;
	}
	AudioMicDevApplyFromCombo(m_miclist);
	extern int gameon;
	ReleaseRenderGrassBackdrop();
	CCustomBlurDialogExBase::OnOK();
}

void CRender::OnCbnSelchangeMic()
{
	AudioMicDevApplyFromCombo(m_miclist);
}



static BOOL RenderRegReadDefaultString(HKEY hKey, CString& out)
{
	out.Empty();
	DWORD type = 0;
	DWORD cb = 0;
	LONG r = RegQueryValueEx(hKey, NULL, NULL, &type, NULL, &cb);
	if (r != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ) || cb < sizeof(TCHAR))
		return FALSE;
	LPTSTR buf = out.GetBuffer((int)(cb / sizeof(TCHAR) + 1));
	r = RegQueryValueEx(hKey, NULL, NULL, &type, (LPBYTE)buf, &cb);
	out.ReleaseBuffer();
	return r == ERROR_SUCCESS;
}

static BOOL RenderCmdReferencesOurPlayer(const CString& cmd)
{
	if (cmd.IsEmpty())
		return FALSE;
	CString s(cmd);
	s.MakeLower();
	return s.Find(_T("oggysebgm")) >= 0;
}

static void RenderUpdateProgIdOpenCommand(HKEY hRoot, const CString& subKey, LPCTSTR szExePath)
{
	CString openKey = subKey + _T("\\shell\\open\\command");
	HKEY hOpen = NULL;
	if (RegOpenKeyEx(hRoot, openKey, 0, KEY_READ | KEY_WRITE, &hOpen) != ERROR_SUCCESS)
		return;
	CString cur;
	if (!RenderRegReadDefaultString(hOpen, cur) || !RenderCmdReferencesOurPlayer(cur)) {
		RegCloseKey(hOpen);
		return;
	}
	CString strCommand;
	strCommand.Format(_T("\"%s\" \"%%1\""), szExePath);
	RegSetValueEx(hOpen, NULL, 0, REG_SZ, (const BYTE*)(LPCTSTR)strCommand,
		(DWORD)((strCommand.GetLength() + 1) * sizeof(TCHAR)));
	RegCloseKey(hOpen);

	CString iconKey = subKey + _T("\\DefaultIcon");
	HKEY hIcon = NULL;
	if (RegCreateKeyEx(hRoot, iconKey, 0, NULL, REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hIcon, NULL) == ERROR_SUCCESS) {
		CString strIcon;
		strIcon.Format(_T("%s,0"), szExePath);
		RegSetValueEx(hIcon, NULL, 0, REG_SZ, (const BYTE*)(LPCTSTR)strIcon,
			(DWORD)((strIcon.GetLength() + 1) * sizeof(TCHAR)));
		RegCloseKey(hIcon);
	}
}

// 関連付け対象: 再生できる音声／動画(DirectShow)／プレイリスト。IsDougaVideoFile と揃える。
static const TCHAR* const* RenderAssocExtensions(int* outCount)
{
	static const TCHAR* kExt[] = {
		// 音声
		_T(".mp3"), _T(".mp2"), _T(".mp1"), _T(".rmp"),
		_T(".ogg"), _T(".oga"), _T(".opus"),
		_T(".flac"), _T(".wav"), _T(".wave"),
		_T(".m4a"), _T(".aac"), _T(".wma"),
		_T(".aif"), _T(".aiff"), _T(".aifc"),
		_T(".dsf"), _T(".dff"),
		_T(".tta"), _T(".tak"), _T(".ape"), _T(".wv"),
		// 動画 (PlayList の DirectShow 動画判定と同系)
		_T(".avi"), _T(".mp4"), _T(".m4v"), _T(".mkv"),
		_T(".wmv"), _T(".asf"), _T(".mov"), _T(".qt"),
		_T(".mpg"), _T(".mpeg"), _T(".mpe"), _T(".m1v"),
		_T(".m2v"), _T(".mpv"), _T(".vob"), _T(".ts"),
		_T(".m2ts"), _T(".mts"), _T(".webm"), _T(".ogv"),
		_T(".flv"), _T(".f4v"), _T(".3gp"), _T(".3g2"),
		_T(".divx"), _T(".rm"), _T(".rmvb"),
		// プレイリスト
		_T(".m3u"), _T(".m3u8"), _T(".pls"), _T(".xspf"),
	};
	if (outCount) *outCount = (int)_countof(kExt);
	return kExt;
}

static void RenderMigrateLegacyFileAssociations(LPCTSTR szExePath, LPCTSTR pszNewProgID, const TCHAR* const* extensions, int extCount)
{
	HKEY hClasses = NULL;
	if (RegOpenKeyEx(HKEY_CURRENT_USER, _T("Software\\Classes"), 0, KEY_READ, &hClasses) == ERROR_SUCCESS) {
		for (DWORD idx = 0;; ++idx) {
			TCHAR name[256];
			DWORD nameLen = _countof(name);
			FILETIME ft;
			if (RegEnumKeyEx(hClasses, idx, name, &nameLen, NULL, NULL, NULL, &ft) != ERROR_SUCCESS)
				break;
			CString sub(name);
			if (sub.Find(_T("oggYSEDbgm")) < 0)
				continue;
			RenderUpdateProgIdOpenCommand(hClasses, sub, szExePath);
		}
		RegCloseKey(hClasses);
	}

	for (int i = 0; i < extCount; ++i) {
		CString extKey = CString(_T("Software\\Classes\\")) + extensions[i];
		TCHAR progid[512] = {};
		DWORD cb = sizeof(progid);
		if (RegGetValue(HKEY_CURRENT_USER, extKey, NULL, RRF_RT_REG_SZ, NULL, progid, &cb) != ERROR_SUCCESS)
			continue;

		CString pid(progid);
		CString cmdKey = CString(_T("Software\\Classes\\")) + pid + _T("\\shell\\open\\command");
		TCHAR cmdBuf[1024] = {};
		cb = sizeof(cmdBuf);
		BOOL hadOurCmd = FALSE;
		if (RegGetValue(HKEY_CURRENT_USER, cmdKey, NULL, RRF_RT_REG_SZ, NULL, cmdBuf, &cb) == ERROR_SUCCESS
			&& RenderCmdReferencesOurPlayer(cmdBuf))
		{
			hadOurCmd = TRUE;
			CString strCommand;
			strCommand.Format(_T("\"%s\" \"%%1\""), szExePath);
			HKEY hCmd = NULL;
			if (RegCreateKeyEx(HKEY_CURRENT_USER, cmdKey, 0, NULL, REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hCmd, NULL) == ERROR_SUCCESS) {
				RegSetValueEx(hCmd, NULL, 0, REG_SZ, (const BYTE*)(LPCTSTR)strCommand,
					(DWORD)((strCommand.GetLength() + 1) * sizeof(TCHAR)));
				RegCloseKey(hCmd);
			}
		}

		if (hadOurCmd && (pid.Find(_T("oggYSEDbgm")) >= 0 || _tcsicmp(pid, pszNewProgID) == 0)) {
			RegSetKeyValue(HKEY_CURRENT_USER, extKey, NULL, REG_SZ, pszNewProgID,
				(DWORD)((_tcslen(pszNewProgID) + 1) * sizeof(TCHAR)));
		}
	}
}


BOOL CRender::MySetFileType(LPCTSTR lpExt, LPCTSTR lpDocName, LPCTSTR lpDocType, LPCTSTR lpPath, LPCTSTR lpPath1)
{
	CRegKey reg;

	// lpExtをlpDocNameに関連付ける
	if (reg.SetValue(HKEY_CLASSES_ROOT, lpExt, lpDocName) != ERROR_SUCCESS)
		return FALSE;
	// lpDocName作成
	CString strDocNameTmp = lpDocName;
	CString strIcon = lpPath1; strIcon += ",0";
	if (reg.SetValue(HKEY_CLASSES_ROOT, strDocNameTmp, lpDocType) != ERROR_SUCCESS)
		return FALSE;
	if (reg.SetValue(HKEY_CLASSES_ROOT, strDocNameTmp + _T("\\shell"), _T("open"))
		!= ERROR_SUCCESS)
		return FALSE;
	if (reg.SetValue(HKEY_CLASSES_ROOT, strDocNameTmp + _T("\\shell\\open\\command"),
		lpPath) != ERROR_SUCCESS)
		return FALSE;
	if (reg.SetValue(HKEY_CLASSES_ROOT, strDocNameTmp + _T("\\DefaultIcon"),
		strIcon) != ERROR_SUCCESS)
		return FALSE;

	if (reg.SetValue(HKEY_LOCAL_MACHINE, _T("SOFTWARE\\RegisteredApplications"),
		_T("SOFTWARE\\PrePrayerPowerSoft\\oggYSEDbgm_uni\\Capabilities"),_T("oggYSEDbgm_uni")) != ERROR_SUCCESS)
		return FALSE;
	if (reg.Create(HKEY_LOCAL_MACHINE, _T("SOFTWARE\\PrePrayerPowerSoft\\oggYSEDbgm_uni\\Capabilities")) != ERROR_SUCCESS)
		return FALSE;
	if (reg.SetValue(HKEY_LOCAL_MACHINE, _T("SOFTWARE\\PrePrayerPowerSoft\\oggYSEDbgm_uni\\Capabilities"),
		strDocNameTmp, lpExt) != ERROR_SUCCESS)
		return FALSE;
	strIcon = _T("Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\FileExts\\");
	strIcon += lpExt;
	strIcon += _T("\\UserChoice");
	reg.Open(HKEY_CURRENT_USER, _T(""));
	if(reg.DeleteSubKey(strIcon) != ERROR_SUCCESS)
		return FALSE;
	reg.Close();
	if (reg.SetValue(HKEY_CURRENT_USER, strIcon,
		lpDocName, _T("Progid")) != ERROR_SUCCESS)
		return FALSE;

	// 関連付けが変更された事をシステムに通知
	::SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, NULL, NULL);
	return TRUE;
}

extern TCHAR karento2[1024];

void CRender::OnBnClickedCancel4()
{
	// TODO: ここにコントロール通知ハンドラー コードを追加します。
	CString s,ss;
	s = "\"";
	s += karento2;
	s += "oggYSEDbgm_uni.exe\" \"%1\"";
	ss = karento2;
	ss += "oggYSEDbgm_uni.exe";
	int extN = 0;
	const TCHAR* const* legacyExt = RenderAssocExtensions(&extN);
	RenderMigrateLegacyFileAssociations(ss, _T("falcombgm.mediaplayer"), legacyExt, extN);
	const CString openLabel = LL14(L"簡易プレイヤで開く", L"Open with Simple Player", L"Ouvrir avec le lecteur simple", L"Apri con lettore semplice", L"Abrir con reproductor simple", L"간이 플레이어로 열기", L"用简易播放器打开", L"فتح بمشغل بسيط", L"Открыть простым проигрывателем", L"Mit Simple Player öffnen", L"Abrir com leitor simples", L"Openen met eenvoudige speler", L"Otwórz prostym odtwarzaczem", L"Basit oynatıcıyla aç");
	for (int i = 0; i < extN; ++i) {
		CString progId;
		progId.Format(_T("oggYSEDbgm_uni.exe%s"), legacyExt[i]);
		MySetFileType(legacyExt[i], progId, openLabel, s, ss);
	}
	::SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, NULL, NULL);
	MessageBox(LL14(
		L"一応関連づけを走らせてみました。\n音声・動画・プレイリスト拡張子に関連をつけました。",
		L"File association attempted.\nAssociated audio, video, and playlist extensions.",
		L"Association de fichiers tentée.\nAudio, vidéo et playlists associés.",
		L"Associazione file tentata.\nAssociati audio, video e playlist.",
		L"Asociación de archivos intentada.\nAsociados audio, vídeo y listas.",
		L"파일 연결을 시도했습니다.\n음성·동영상·재생목록 확장자에 연결했습니다.",
		L"已尝试文件关联。\n已关联音频、视频与播放列表扩展名。",
		L"تمت محاولة ربط الملفات.\nتم ربط امتدادات الصوت والفيديو وقوائم التشغيل.",
		L"Попытка связи файлов.\nСвязаны аудио, видео и плейлисты.",
		L"Dateizuordnung versucht.\nAudio-, Video- und Playlist-Erweiterungen verknuepft.",
		L"Associação de ficheiros tentada.\nAssociados áudio, vídeo e playlists.",
		L"Bestandskoppeling geprobeerd.\nAudio-, video- en playlist-extensies gekoppeld.",
		L"Próbowano powiązania plików.\nPowiązano audio, wideo i playlisty.",
		L"Dosya ilişkilendirme denendi.\nSes, video ve çalma listesi uzantıları ilişkilendirildi."));
	::SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, NULL, NULL);
}


void CRender::OnTimer(UINT_PTR nIDEvent)
{
	// TODO: ここにメッセージ ハンドラー コードを追加するか、既定の処理を呼び出します。
	if (nIDEvent == 7000) {
		KillTimer(7000);
		// 親を付けないとメイン背面に回り、CRender の下に隠れて操作不能になる
		CKpilist k(this);
		k.status = 0;
		k.DoModal();
		return;
	}
	savedata.ms = m_ms.GetPos();
	{
		const wchar_t* msUnit = LL14(L"ms", L"ms", L"ms", L"ms", L"ms", L"ms", L"毫秒", L"ms", L"мс", L"ms", L"ms", L"ms", L"ms", L"ms");
		CString s; s.Format(L"%d%s", savedata.ms, msUnit);
		m_ms2.SetWindowText(s);
	}
	savedata.ms2 = m_hyouji2.GetPos() * 16;
	{
		const wchar_t* msUnit = LL14(L"ms", L"ms", L"ms", L"ms", L"ms", L"ms", L"毫秒", L"ms", L"мс", L"ms", L"ms", L"ms", L"ms", L"ms");
		CString s2; s2.Format(L"%d%s", savedata.ms2, msUnit);
		m_hyouji3.SetWindowText(s2);
	}
	savedata.eqCodeMs = m_eqCode.GetPos();
	if (savedata.eqCodeMs < 16) savedata.eqCodeMs = 16;
	if (savedata.eqCodeMs > 500) savedata.eqCodeMs = 500;
	{
		const wchar_t* msUnit = LL14(L"ms", L"ms", L"ms", L"ms", L"ms", L"ms", L"毫秒", L"ms", L"мс", L"ms", L"ms", L"ms", L"ms", L"ms");
		CString s3; s3.Format(L"%d%s", savedata.eqCodeMs, msUnit);
		m_eqCodeMs.SetWindowText(s3);
	}
	savedata.wup = w_wups.GetPos()/ 100.0;
	CString s;
	{
		const wchar_t* fmt = LL14(L"%1.2lf倍", L"%1.2lfx", L"%1.2lfx", L"%1.2lfx", L"%1.2lfx", L"%1.2lfx", L"%1.2lf倍", L"%1.2lfx", L"%1.2lfx", L"%1.2lfx", L"%1.2lfx", L"%1.2lfx", L"%1.2lfx", L"%1.2lfx");
		s.Format(fmt, savedata.wup);
	}
	m_wup.SetWindowText(s);
	if (nIDEvent == 90) {
		KillTimer(90);
		::SetWindowPos(m_hWnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
		if (renderbase && renderbase->GetSafeHwnd())
			::SetWindowPos(renderbase->m_hWnd, m_hWnd, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
	}
	CCustomBlurDialogExBase::OnTimer(nIDEvent);
}

void CRender::OnCbnSelchangeCombo2()
{
	int dev = m_soundlist.GetCurSel();
	if (dev < 0) return;
	memcpy(&savedata.soundguid, &slg[dev], sizeof(GUID));
	if (dev == 0)
		savedata.soundguid = { 0,0,0,0 };
	savedata.soundcur = dev;
	if (og)
		RenderRecreateSecondarySound(og);
}

void CRender::OnCbnSelchangeSpeaker()
{
	int s = m_speaker.GetCurSel();
	if (s >= 0 && s <= 5)
		savedata.speaker_layout = s;
	if (og)
		RenderRecreateSecondarySound(og);
}

void CRender::OnBnClickedCheckUpscale()
{
	savedata.upscale_enable = m_upscale.GetCheck() ? 1 : 0;
	if (og)
		RenderRecreateSecondarySound(og);
}


void CRender::OnBnClickedButton1()
{
	// TODO: ここにコントロール通知ハンドラー コードを追加します。
	CZeroFol z(this);
	z.DoModal();
}


void CRender::OnCbnSelchangeCombo3()
{
	int ihz = m_Hz.GetCurSel();
	if (ihz >= 0 && ihz < 12)
		savedata.samples = samp[ihz];
	if (og)
		RenderRecreateSecondarySound(og);
}


HBRUSH CRender::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
{
#if CCUSTOM_AERO_SUPPORT
	if (CCC_IsAeroEnabled() && CCC_IsWin11())
		return CCustomBlurDialogExBase::OnCtlColor(pDC, pWnd, nCtlColor);
#endif
	HBRUSH hbr = CCustomBlurDialogExBase::OnCtlColor(pDC, pWnd, nCtlColor);

	if (savedata.aero == 1) {
		if (nCtlColor == CTLCOLOR_DLG)
			return m_brDlg;
		if (nCtlColor == CTLCOLOR_STATIC)
		{
			SetBkMode(pDC->m_hDC, TRANSPARENT);
			return m_brDlg;
		}
	}
	return hbr;
}


int CRender::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
	if (CCustomBlurDialogExBase::OnCreate(lpCreateStruct) == -1)
		return -1;

#if CCUSTOM_AERO_SUPPORT
	if (CCC_IsWin11())
		return 0;
#endif
	ModifyStyleEx(0, WS_EX_LAYERED);
	SetLayeredWindowAttributes(RGB(255, 0, 0), 0, LWA_COLORKEY);
	m_brDlg.CreateSolidBrush(RGB(255, 0, 0));
	return 0;
}


void CRender::OnMoving(UINT fwSide, LPRECT pRect)
{
	CCustomBlurDialogExBase::OnMoving(fwSide, pRect);
	CRect r;
	GetWindowRect(&r);
	if (savedata.aero && renderbase && renderbase->GetSafeHwnd())
		renderbase->MoveWindow(&r);
	// TODO: ここにメッセージ ハンドラー コードを追加します。
}

int CRender::Create(CWnd* pWnd)
{
	m_pParent = NULL;
	BOOL bret = CCustomBlurDialogExBase::Create(CRender::IDD, this);
#if CCUSTOM_AERO_SUPPORT
	if (savedata.aero == 1 && !CCC_IsWin11())
#else
	if (savedata.aero == 1)
#endif
	{
		ModifyStyleEx(0, WS_EX_LAYERED);
		SetLayeredWindowAttributes(RGB(255, 0, 0), 0, LWA_COLORKEY);
		if (!m_brDlg.GetSafeHandle())
			m_brDlg.CreateSolidBrush(RGB(255, 0, 0));
	}
	if (bret == TRUE)
		ShowWindow(SW_SHOW);
	return bret;
}

void CRender::OnBnClickedCheck3()
{
	const int newAero = m_a.GetCheck() ? 1 : 0;
	if (savedata.aero == newAero)
		return;
	savedata.aero = newAero;
	RefreshAeroMode();
#if CCUSTOM_AERO_SUPPORT
	if (!CCC_IsWin11())
		SyncRenderGrassBackdrop(this);
	else
	{
		ReleaseRenderGrassBackdrop();
		ModifyStyleEx(WS_EX_LAYERED, 0);
	}
#else
	SyncRenderGrassBackdrop(this);
#endif
	Invalidate(FALSE);
	if (og)
		og->PostRefreshAllAeroWindows();
	// mp 再適用で設定が背後へ回るのを防ぐ（Post 後にもう一度手前へ）
	::SetWindowPos(m_hWnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
	::SetForegroundWindow(m_hWnd);
	if (renderbase && renderbase->GetSafeHwnd())
		::SetWindowPos(renderbase->m_hWnd, m_hWnd, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

void CRender::OnBnClickedCancel()
{
	bool bSoundChanged = (memcmp(&savedata.soundguid, &m_bakSoundGuid, sizeof(GUID)) != 0 ||
	                      savedata.soundcur != m_bakSoundCur ||
	                      savedata.samples != m_bakSamples ||
	                      savedata.upscale_enable != m_bakUpscale ||
	                      savedata.speaker_layout != m_bakSpeaker ||
	                      savedata.bit24 != m_bakBit24 ||
	                      savedata.bit32 != m_bakBit32);

	bool bFont1Changed = (_tcscmp(savedata.font1, m_bakFont1) != 0);
	bool bFont2Changed = (_tcscmp(savedata.font2, m_bakFont2) != 0);

	savedata.soundguid = m_bakSoundGuid;
	savedata.soundcur = m_bakSoundCur;
	savedata.samples = m_bakSamples;
	savedata.upscale_enable = m_bakUpscale;
	savedata.speaker_layout = m_bakSpeaker;
	savedata.bit24 = m_bakBit24;
	savedata.bit32 = m_bakBit32;
	_tcscpy(savedata.font1, m_bakFont1);
	_tcscpy(savedata.font2, m_bakFont2);

	m_24.SetCheck(savedata.bit24);
	m_32bit.SetCheck(savedata.bit32);
	m_upscale.SetCheck(savedata.upscale_enable ? BST_CHECKED : BST_UNCHECKED);
	{
		int sp = savedata.speaker_layout;
		if (sp < 0 || sp > 5) sp = 0;
		m_speaker.SetCurSel(sp);
	}
	if (m_soundlist.GetCount() > 0 && savedata.soundcur >= 0 && savedata.soundcur < m_soundlist.GetCount())
		m_soundlist.SetCurSel(savedata.soundcur);
	for (int l = 0; l < 12; l++) {
		if (savedata.samples == samp[l]) {
			m_Hz.SetCurSel(l);
			break;
		}
	}

	if (bSoundChanged && og)
		RenderRecreateSecondarySound(og);

	if (bFont1Changed) {
		DeleteObject(hFont);
		LOGFONT logFont;
		bool has_font1 = false;
		if (_tcslen(savedata.font1) > 0) {
			has_font1 = DeserializeLogFont(savedata.font1, &logFont);
		}
		if (has_font1) {
			logFont.lfHeight *= 4;
			logFont.lfWidth *= 4;
		} else {
			memset(&logFont, 0, sizeof(LOGFONT));
			logFont.lfHeight = 16 * 4;
			logFont.lfWidth = 8 * 4;
			logFont.lfWeight = FW_ULTRABOLD;
			logFont.lfQuality = DRAFT_QUALITY;
			if (_tcslen(savedata.font1) > 0) {
				_tcscpy(logFont.lfFaceName, savedata.font1);
			} else {
				_tcscpy(logFont.lfFaceName, _T("ＭＳ ゴシック"));
			}
		}
		hFont = CreateFontIndirect(&logFont);
		if (og) {
			og->Invalidate();
			og->RedrawWindow();
		}
	}

	if (bFont2Changed && pl) {
		if (pl->m_fontList.GetSafeHandle()) {
			pl->m_fontList.DeleteObject();
		}
		BOOL retfont = FALSE;
		LOGFONT logFont;
		if (_tcslen(savedata.font2) > 0 && DeserializeLogFont(savedata.font2, &logFont)) {
			retfont = pl->m_fontList.CreateFontIndirect(&logFont);
		} else if (_tcslen(savedata.font2) > 0) {
			retfont = pl->m_fontList.CreateFont(-15, 0, 0, 0, FW_NORMAL, 0, 0, 0, SHIFTJIS_CHARSET, OUT_TT_PRECIS, CLIP_CHARACTER_PRECIS, DRAFT_QUALITY, DEFAULT_PITCH | FF_SWISS, savedata.font2);
		}
		if (!retfont) {
			retfont = pl->m_fontList.CreateFont(-15, 0, 0, 0, FW_NORMAL, 0, 0, 0, SHIFTJIS_CHARSET, OUT_TT_PRECIS, CLIP_CHARACTER_PRECIS, DRAFT_QUALITY, DEFAULT_PITCH | FF_SWISS, _T("メイリオ"));
		}
		if (retfont) {
			pl->m_lc.SetFont(&pl->m_fontList, TRUE);
			pl->m_find.SetFont(&pl->m_fontList, TRUE);
			pl->m_lc.Invalidate();
			pl->m_find.Invalidate();
		}
	}

	const bool bAeroChanged = (savedata.aero != m_bakAero);
	if (bAeroChanged) {
		savedata.aero = m_bakAero;
		m_a.SetCheck(savedata.aero ? BST_CHECKED : BST_UNCHECKED);
		RefreshAeroMode();
#if CCUSTOM_AERO_SUPPORT
		if (!CCC_IsWin11())
			SyncRenderGrassBackdrop(this);
		else
		{
			ReleaseRenderGrassBackdrop();
			ModifyStyleEx(WS_EX_LAYERED, 0);
		}
#else
		SyncRenderGrassBackdrop(this);
#endif
		Invalidate(FALSE);
		if (og)
			og->PostRefreshAllAeroWindows();
	}

	ReleaseRenderGrassBackdrop();
	CCustomBlurDialogExBase::OnCancel();
}

static void SyncRenderGrassBackdrop(CRender* pRender)
{
	if (!pRender || !pRender->GetSafeHwnd())
		return;
#if CCUSTOM_AERO_SUPPORT
	if (CCC_IsWin11())
	{
		ReleaseRenderGrassBackdrop();
		pRender->ModifyStyleEx(WS_EX_LAYERED, 0);
		return;
	}
#endif
	CRect r;
	pRender->GetWindowRect(&r);
	if (CCC_IsAeroEnabled())
	{
		if (renderbase && !renderbase->GetSafeHwnd())
			ReleaseRenderGrassBackdrop();
		if (!renderbase)
		{
			renderbase = new CImageBase;
			renderbase->Create(NULL);
			renderbase->oya = pRender;
		}
		if (renderbase && renderbase->GetSafeHwnd())
		{
			renderbase->MoveWindow(&r);
			::SetWindowPos(renderbase->m_hWnd, pRender->m_hWnd, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
		}
		if (!pRender->m_brDlg.GetSafeHandle())
			pRender->m_brDlg.CreateSolidBrush(RGB(255, 0, 0));
		pRender->ModifyStyleEx(0, WS_EX_LAYERED);
		pRender->SetLayeredWindowAttributes(RGB(255, 0, 0), 0, LWA_COLORKEY);
	}
	else
	{
		ReleaseRenderGrassBackdrop();
		pRender->ModifyStyleEx(WS_EX_LAYERED, 0);
	}
}

void CRender::OnBnClickedCheck52()
{
	savedata.speanamode = m_speana.GetCheck();
	savedata.speananum = m_speana_num.GetCurSel();
}

void CRender::OnCbnEditchangeCombo4()
{
	// TODO: ここにコントロール通知ハンドラー コードを追加します。
	m_speana.SetCheck(TRUE);
	savedata.speanamode = m_speana.GetCheck();
	savedata.speananum = m_speana_num.GetCurSel();
}

void CRender::OnCbnSelchangeCombo4()
{
	// TODO: ここにコントロール通知ハンドラー コードを追加します。
	m_speana.SetCheck(TRUE);
	savedata.speanamode = m_speana.GetCheck();
	savedata.speananum = m_speana_num.GetCurSel();
}

void CRender::OnBnClickedCancel5()
{
	// プログラムの実行ファイルパスを取得
	TCHAR szExePath[MAX_PATH];
	GetModuleFileName(NULL, szExePath, MAX_PATH);

	CString strProgID = _T("falcombgm.mediaplayer");
	CString strAppName = LL14(L"Falcom BGM&メディアプレイヤー", L"Falcom BGM&Media Player", L"Falcom BGM et lecteur média", L"Falcom BGM e lettore multimediale", L"Falcom BGM y reproductor multimedia", L"Falcom BGM 미디어 플레이어", L"Falcom BGM 媒体播放器", L"Falcom BGM ومشغل الوسائط", L"Falcom BGM и медиаплеер", L"Falcom BGM & Media Player", L"Falcom BGM e reprodutor multimédia", L"Falcom BGM & media player", L"Falcom BGM i odtwarzacz multimediów", L"Falcom BGM ve medya oynatıcı");

	// 対応拡張子一覧（音声・動画・プレイリスト）
	int extN = 0;
	const TCHAR* const* extensions = RenderAssocExtensions(&extN);

	RenderMigrateLegacyFileAssociations(szExePath, strProgID, extensions, extN);

	HKEY hKey;
	LONG result;

	// 1. ProgIDの登録
	CString strProgIDKey = _T("Software\\Classes\\") + strProgID;
	result = RegCreateKeyEx(HKEY_CURRENT_USER, strProgIDKey, 0, NULL,
		REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, NULL);
	if (result == ERROR_SUCCESS)
	{
		RegSetValueEx(hKey, NULL, 0, REG_SZ,
			(BYTE*)(LPCTSTR)strAppName,
			(strAppName.GetLength() + 1) * sizeof(TCHAR));
		RegCloseKey(hKey);
	}

	// 2. DefaultIconの設定
	CString strIconKey = strProgIDKey + _T("\\DefaultIcon");
	result = RegCreateKeyEx(HKEY_CURRENT_USER, strIconKey, 0, NULL,
		REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, NULL);
	if (result == ERROR_SUCCESS)
	{
		CString strIcon;
		strIcon.Format(_T("%s,0"), szExePath);
		RegSetValueEx(hKey, NULL, 0, REG_SZ,
			(BYTE*)(LPCTSTR)strIcon,
			(strIcon.GetLength() + 1) * sizeof(TCHAR));
		RegCloseKey(hKey);
	}

	// 3. shell\open\commandの設定
	CString strCommandKey = strProgIDKey + _T("\\shell\\open\\command");
	result = RegCreateKeyEx(HKEY_CURRENT_USER, strCommandKey, 0, NULL,
		REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, NULL);
	if (result == ERROR_SUCCESS)
	{
		CString strCommand;
		strCommand.Format(_T("\"%s\" \"%%1\""), szExePath);
		RegSetValueEx(hKey, NULL, 0, REG_SZ,
			(BYTE*)(LPCTSTR)strCommand,
			(strCommand.GetLength() + 1) * sizeof(TCHAR));
		RegCloseKey(hKey);
	}

	// 4. 各拡張子にOpenWithProgidsを設定
	for (int i = 0; i < extN; i++)
	{
		CString strExtKey;
		strExtKey.Format(_T("Software\\Classes\\%s\\OpenWithProgids"), extensions[i]);

		result = RegCreateKeyEx(HKEY_CURRENT_USER, strExtKey, 0, NULL,
			REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, NULL);
		if (result == ERROR_SUCCESS)
		{
			// 値は空でOK(値の存在自体が意味を持つ)
			RegSetValueEx(hKey, strProgID, 0, REG_NONE, NULL, 0);
			RegCloseKey(hKey);
		}
	}

	// 5. アプリケーションの登録
	CString strAppKey = _T("Software\\") + strAppName;
	result = RegCreateKeyEx(HKEY_CURRENT_USER, strAppKey + _T("\\Capabilities"),
		0, NULL, REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, NULL);
	if (result == ERROR_SUCCESS)
	{
		CString strDesc = LL14(L"ファルコムゲームBGMのループ再生対応メディアプレイヤー", L"Falcom game BGM loop playback media player", L"Lecteur média pour BGM Falcom avec boucle", L"Lettore media BGM Falcom con loop", L"Reproductor de BGM Falcom con bucle", L"팔콤 게임 BGM 루프 재생 미디어 플레이어", L"支持 Falcom 游戏 BGM 循环播放的媒体播放器", L"مشغل وسائط BGM ألعاب فالكوم مع تكرار", L"Медиаплеер для BGM Falcom с зацикливанием", L"Falcom-Spiel-BGM-Loop-Medienplayer", L"Reprodutor BGM Falcom com loop", L"Falcom BGM loop-mediaspeler", L"Odtwarzacz mediów BGM Falcom z pętlą", L"Falcom oyun BGM döngü medya oynatıcı");
		RegSetValueEx(hKey, _T("ApplicationDescription"), 0, REG_SZ,
			(BYTE*)(LPCTSTR)strDesc,
			(strDesc.GetLength() + 1) * sizeof(TCHAR));
		RegSetValueEx(hKey, _T("ApplicationName"), 0, REG_SZ,
			(BYTE*)(LPCTSTR)strAppName,
			(strAppName.GetLength() + 1) * sizeof(TCHAR));
		RegCloseKey(hKey);
	}

	// 6. ファイル関連付けの登録
	CString strFileAssocKey = strAppKey + _T("\\Capabilities\\FileAssociations");
	result = RegCreateKeyEx(HKEY_CURRENT_USER, strFileAssocKey, 0, NULL,
		REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, NULL);
	if (result == ERROR_SUCCESS)
	{
		for (int i = 0; i < extN; i++)
		{
			RegSetValueEx(hKey, extensions[i], 0, REG_SZ,
				(BYTE*)(LPCTSTR)strProgID,
				(strProgID.GetLength() + 1) * sizeof(TCHAR));
		}
		RegCloseKey(hKey);
	}

	// 7. Registered Applicationsに登録
	CString strRegAppKey = _T("Software\\RegisteredApplications");
	result = RegCreateKeyEx(HKEY_CURRENT_USER, strRegAppKey, 0, NULL,
		REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, NULL);
	if (result == ERROR_SUCCESS)
	{
		CString strCapPath = _T("Software\\") + strAppName + _T("\\Capabilities");
		RegSetValueEx(hKey, strAppName, 0, REG_SZ,
			(BYTE*)(LPCTSTR)strCapPath,
			(strCapPath.GetLength() + 1) * sizeof(TCHAR));
		RegCloseKey(hKey);
	}

	// 8. 変更をシステムに通知
	SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, NULL, NULL);

	AfxMessageBox(LL14(
		L"ファイルの関連付け登録が完了しました。\n音声・動画(avi/mp4/mkv/wmv/mov/webm 等)・プレイリストを含めました。",
		L"File association registration completed.\nIncludes audio, video (avi/mp4/mkv/wmv/mov/webm, etc.), and playlists.",
		L"Enregistrement terminé.\nAudio, vidéo (avi/mp4/mkv/…) et playlists inclus.",
		L"Registrazione completata.\nInclusi audio, video (avi/mp4/mkv/…) e playlist.",
		L"Registro completado.\nIncluye audio, vídeo (avi/mp4/mkv/…) y listas.",
		L"파일 연결 등록이 완료되었습니다.\n음성·동영상(avi/mp4/mkv 등)·재생목록을 포함했습니다.",
		L"文件关联注册已完成。\n已包含音频、视频(avi/mp4/mkv 等)与播放列表。",
		L"اكتمل تسجيل ربط الملفات.\nيشمل الصوت والفيديو وقوائم التشغيل.",
		L"Регистрация связи файлов завершена.\nВключены аудио, видео и плейлисты.",
		L"Dateizuordnung abgeschlossen.\nAudio, Video und Playlists eingeschlossen.",
		L"Registo concluído.\nInclui áudio, vídeo e playlists.",
		L"Registratie voltooid.\nAudio, video en playlists inbegrepen.",
		L"Rejestracja zakończona.\nUwzględniono audio, wideo i playlisty.",
		L"Dosya ilişkilendirme kaydı tamamlandı.\nSes, video ve çalma listeleri dahil."), MB_ICONINFORMATION);
}
