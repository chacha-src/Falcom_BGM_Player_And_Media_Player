#include "stdafx.h"
#include "ogg.h"
#include "CSasamiFmVoiceDlg.h"
#include "PlayList.h"
#include "kb_sasami/source/sasami_write.h"

/* Built-in SASAMI neiro bank (48 bytes/slot, first 25 used as OPN voice) */
#include "kb_sasami/source/sasami_neiro.inc"

extern CPlayList* pl;

IMPLEMENT_DYNAMIC(CSasamiFmVoiceDlg, CCustomBlurDialogExBase)

CSasamiFmVoiceDlg::CSasamiFmVoiceDlg(CWnd* pParent)
	: CCustomBlurDialogExBase(CSasamiFmVoiceDlg::IDD, pParent)
{
	memset(m_voice, 0, sizeof(m_voice));
	m_voice[24] = 0x3C;
	m_dragKnob = -1;
	m_dragStartY = m_dragStartValue = 0;
	for (int i = 0; i < 22; ++i) m_knobRc[i].SetRectEmpty();
}

CSasamiFmVoiceDlg::~CSasamiFmVoiceDlg() {}

void CSasamiFmVoiceDlg::SetVoice(const uint8_t v[25])
{
	if (v) memcpy(m_voice, v, 25);
}

void CSasamiFmVoiceDlg::GetVoice(uint8_t v[25]) const
{
	if (v) memcpy(v, m_voice, 25);
}

int CSasamiFmVoiceDlg::DoEdit(CWnd* owner, uint8_t v[25])
{
	UNREFERENCED_PARAMETER(owner);
	SetVoice(v);
	const INT_PTR r = DoModal();
	if (r == IDOK) GetVoice(v);
	return (int)r;
}

void CSasamiFmVoiceDlg::DoDataExchange(CDataExchange* pDX)
{
	CCustomBlurDialogExBase::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_SASAMI_FV_PREVIEW, m_btnPreview);
	DDX_Control(pDX, IDC_SASAMI_FV_OK, m_btnOk);
	DDX_Control(pDX, IDC_SASAMI_FV_CANCEL, m_btnCancel);
	DDX_Control(pDX, IDC_SASAMI_FV_PRESET, m_cmbPreset);
}

BEGIN_MESSAGE_MAP(CSasamiFmVoiceDlg, CCustomBlurDialogExBase)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_SIZE()
	ON_WM_LBUTTONDOWN()
	ON_WM_LBUTTONUP()
	ON_WM_MOUSEMOVE()
	ON_BN_CLICKED(IDC_SASAMI_FV_PREVIEW, &CSasamiFmVoiceDlg::OnBnClickedPreview)
	ON_BN_CLICKED(IDC_SASAMI_FV_OK, &CSasamiFmVoiceDlg::OnBnClickedOk)
	ON_BN_CLICKED(IDC_SASAMI_FV_CANCEL, &CSasamiFmVoiceDlg::OnBnClickedCancel)
	ON_CBN_SELCHANGE(IDC_SASAMI_FV_PRESET, &CSasamiFmVoiceDlg::OnCbnPreset)
END_MESSAGE_MAP()

void CSasamiFmVoiceDlg::ApplyLang()
{
	SetWindowText(LL14(
		L"FM音色 (25バイト)", L"FM Voice (25 bytes)", L"Timbre FM (25 octets)", L"Voce FM (25 byte)", L"Voz FM (25 bytes)",
		L"FM 음색 (25바이트)", L"FM音色(25字节)", L"صوت FM (25 بايت)", L"FM-тембр (25 байт)", L"FM-Klang (25 Bytes)",
		L"Voz FM (25 bytes)", L"FM-klank (25 bytes)", L"Głos FM (25 bajtów)", L"FM ses (25 bayt)"));
	m_btnPreview.SetWindowText(LL14(L"プレビュー", L"Preview", L"Aperçu", L"Anteprima", L"Vista previa", L"미리듣기", L"试听", L"معاينة", L"Просмотр", L"Vorschau", L"Prévia", L"Voorbeeld", L"Podgląd", L"Önizle"));
	m_btnOk.SetWindowText(L"OK");
	m_btnCancel.SetWindowText(LL14(L"キャンセル", L"Cancel", L"Annuler", L"Annulla", L"Cancelar", L"취소", L"取消", L"إلغاء", L"Отмена", L"Abbrechen", L"Cancelar", L"Annuleren", L"Anuluj", L"İptal"));
}

void CSasamiFmVoiceDlg::LayoutChrome()
{
	if (!::IsWindow(m_hWnd)) return;
	CRect rc;
	GetClientRect(&rc);
	const int cap = CCC_GetCustomCaptionHeight(m_hWnd);
	const int pad = 8;
	int y = cap + pad;
	m_btnPreview.MoveWindow(pad, y, 96, 28);
	m_btnOk.MoveWindow(pad + 104, y, 72, 28);
	m_btnCancel.MoveWindow(pad + 184, y, 96, 28);
	/* Closed combo height must stay ~line height; large H overlaps the knob body. */
	if (m_cmbPreset.GetSafeHwnd())
		m_cmbPreset.MoveWindow(pad + 292, y, 180, 28);
	y += 40;
	m_bodyRc.SetRect(pad, y, rc.Width() - pad, rc.Height() - pad);

	const int algoW = max(150, m_bodyRc.Width() / 4);
	const int gridLeft = m_bodyRc.left + algoW + 10;
	const int cellW = max(34, (m_bodyRc.right - gridLeft) / 5);
	const int rowH = max(48, m_bodyRc.Height() / 4);
	for (int op = 0; op < 4; ++op)
		for (int p = 0; p < 5; ++p)
			m_knobRc[op * 5 + p].SetRect(gridLeft + p * cellW, m_bodyRc.top + op * rowH,
				gridLeft + (p + 1) * cellW - 3, m_bodyRc.top + (op + 1) * rowH - 3);
	m_knobRc[20].SetRect(m_bodyRc.left + 8, m_bodyRc.bottom - 78, m_bodyRc.left + 72, m_bodyRc.bottom - 8);
	m_knobRc[21].SetRect(m_bodyRc.left + 80, m_bodyRc.bottom - 78, m_bodyRc.left + 144, m_bodyRc.bottom - 8);
}

BOOL CSasamiFmVoiceDlg::OnInitDialog()
{
	CCustomBlurDialogExBase::OnInitDialog();
	m_btnPreview.SetAeroMode(FALSE);
	m_btnOk.SetAeroMode(FALSE);
	m_btnCancel.SetAeroMode(FALSE);
	m_btnPreview.SetFlat(TRUE);
	m_btnOk.SetFlat(TRUE);
	m_btnCancel.SetFlat(TRUE);
	if (m_cmbPreset.GetSafeHwnd()) m_cmbPreset.SetAeroMode(FALSE);
	ApplyLang();
	if (m_cmbPreset.GetSafeHwnd()) {
		m_cmbPreset.ResetContent();
		m_cmbPreset.AddString(L"(current)");
		const int nNeiro = (int)(sizeof(kSasamiNeiro) / 0x30);
		for (int i = 0; i < nNeiro; i++) {
			wchar_t s[32];
			_snwprintf_s(s, _TRUNCATE, L"Neiro @%d", i);
			m_cmbPreset.AddString(s);
		}
		m_cmbPreset.SetCurSel(0);
	}
	if (!ScRestoreWndGeom(this, savedata.sasamiFmVoiceX, savedata.sasamiFmVoiceY,
		savedata.sasamiFmVoiceW, savedata.sasamiFmVoiceH, 640, 420))
		SetWindowPos(NULL, 0, 0, 780, 560, SWP_NOMOVE | SWP_NOZORDER);
	LayoutChrome();
	return TRUE;
}

void CSasamiFmVoiceDlg::OnCbnPreset()
{
	if (!m_cmbPreset.GetSafeHwnd()) return;
	int sel = m_cmbPreset.GetCurSel();
	if (sel <= 0) return; /* 0 = current */
	int idx = sel - 1;
	const int nNeiro = (int)(sizeof(kSasamiNeiro) / 0x30);
	if (idx < 0 || idx >= nNeiro) return;
	memcpy(m_voice, kSasamiNeiro + idx * 0x30, 25);
	InvalidateRect(m_bodyRc, FALSE);
}

void CSasamiFmVoiceDlg::OnSize(UINT nType, int cx, int cy)
{
	CCustomBlurDialogExBase::OnSize(nType, cx, cy);
	if (m_btnOk.GetSafeHwnd()) LayoutChrome();
}

BOOL CSasamiFmVoiceDlg::OnEraseBkgnd(CDC* pDC)
{
	if (!pDC) return TRUE;
	CRect rc; GetClientRect(&rc);
	const int cap = CCC_GetCustomCaptionHeight(m_hWnd);
	pDC->FillSolidRect(0, cap, rc.Width(), max(0, m_bodyRc.top - cap), RGB(24, 29, 27));
	return TRUE;
}

static void VoiceDrawKnob(CDC& dc, const CRect& rc, int val, int vmax, const wchar_t* name)
{
	const int r = max(8, min(rc.Width(), rc.Height() - 15) / 2 - 3);
	const CPoint c(rc.CenterPoint().x, rc.top + r + 3);
	CBrush br(RGB(42, 57, 51)); CPen rim(PS_SOLID, 1, RGB(105, 174, 140));
	CBrush* ob = dc.SelectObject(&br); CPen* op = dc.SelectObject(&rim);
	dc.Ellipse(c.x-r, c.y-r, c.x+r+1, c.y+r+1);
	const double a = 3.1415926535 * (.75 + 1.5 * val / max(1, vmax));
	CPen needle(PS_SOLID, 2, RGB(239, 202, 99)); dc.SelectObject(&needle);
	dc.MoveTo(c); dc.LineTo(c.x + (int)(cos(a)*(r-3)), c.y - (int)(sin(a)*(r-3)));
	dc.SelectObject(op); dc.SelectObject(ob);
	dc.SetBkMode(TRANSPARENT); dc.SetTextColor(RGB(220, 235, 228));
	CString s; s.Format(L"%s %d", name, val); dc.DrawText(s, CRect(rc.left, c.y+r, rc.right, rc.bottom), DT_CENTER|DT_TOP|DT_SINGLELINE);
}

void CSasamiFmVoiceDlg::OnPaint()
{
	CPaintDC dc(this);
	CRect body = m_bodyRc;
	if (body.IsRectEmpty()) { CCC_CaptionPaint(dc, m_hWnd); return; }
	CDC mem;
	mem.CreateCompatibleDC(&dc);
	CBitmap bmp;
	bmp.CreateCompatibleBitmap(&dc, body.Width(), body.Height());
	CBitmap* old = mem.SelectObject(&bmp);
	mem.FillSolidRect(0, 0, body.Width(), body.Height(), RGB(20, 27, 24));
	mem.SetBkMode(TRANSPARENT);
	mem.SetTextColor(RGB(198, 226, 211));
	CFont* oldF = mem.SelectObject(GetFont());
	CRect algo(5, 5, max(145, body.Width()/4)-5, max(90, body.Height()-86));
	mem.FillSolidRect(algo, RGB(28,44,36)); mem.Rectangle(algo);
	CString title; title.Format(L"ALGORITHM %d", m_voice[24] & 7);
	mem.DrawText(title, algo, DT_CENTER|DT_TOP|DT_SINGLELINE);
	CRect boxes[4]; const int alg = m_voice[24]&7;
	for (int i=0;i<4;++i) {
		int x = algo.left+12 + (algo.Width()-34) * i / 3;
		int y = algo.CenterPoint().y + ((alg==7 || i==3) ? 8 : ((i&1)?18:-18));
		boxes[i].SetRect(x,y,x+22,y+18);
	}
	CPen wire(PS_SOLID,1,RGB(126,202,161)); CPen* oldP=mem.SelectObject(&wire);
	for(int i=0;i<3;++i){ mem.MoveTo(boxes[i].CenterPoint()); mem.LineTo(boxes[i+1].CenterPoint()); }
	mem.SelectObject(oldP);
	for(int i=0;i<4;++i){ mem.FillSolidRect(boxes[i],RGB(51,79,64)); CString s; s.Format(L"%d",i+1); mem.DrawText(s,boxes[i],DT_CENTER|DT_VCENTER|DT_SINGLELINE); }
	static const wchar_t* names[5]={L"TL",L"AR",L"DR",L"SR",L"RR"};
	for(int opi=0;opi<4;++opi) for(int p=0;p<5;++p) {
		CRect kr=m_knobRc[opi*5+p]; kr.OffsetRect(-body.left,-body.top);
		int idx=0,val=0,vmax=31;
		if(p==0){idx=4+opi;val=m_voice[idx]&127;vmax=127;}
		else if(p==1){idx=8+opi;val=m_voice[idx]&31;}
		else if(p==2){idx=12+opi;val=m_voice[idx]&31;}
		else if(p==3){idx=16+opi;val=m_voice[idx]&31;}
		else {idx=20+opi;val=m_voice[idx]&15;vmax=15;}
		VoiceDrawKnob(mem,kr,val,vmax,names[p]);
		if(p==0){ CString opn; opn.Format(L"OP%d",opi+1); mem.TextOut(kr.left+2,kr.top+2,opn); }
	}
	CRect fb=m_knobRc[20]; fb.OffsetRect(-body.left,-body.top); VoiceDrawKnob(mem,fb,(m_voice[24]>>3)&7,7,L"FB");
	CRect ar=m_knobRc[21]; ar.OffsetRect(-body.left,-body.top); VoiceDrawKnob(mem,ar,m_voice[24]&7,7,L"ALG");
	mem.SelectObject(oldF);
	CCC_BlitStretchOpaque(dc.GetSafeHdc(), body.left, body.top, body.Width(), body.Height(), mem.GetSafeHdc(), 0, 0, body.Width(), body.Height());
	mem.SelectObject(old);
	CCC_CaptionPaint(dc, m_hWnd);
}

static int VoiceKnobValue(const uint8_t* v, int k)
{
	if (k==20) return (v[24]>>3)&7;
	if (k==21) return v[24]&7;
	const int op=k/5,p=k%5;
	if(p==0)return v[4+op]&127;
	if(p==1)return v[8+op]&31;
	if(p==2)return v[12+op]&31;
	if(p==3)return v[16+op]&31;
	return v[20+op]&15;
}

void CSasamiFmVoiceDlg::OnLButtonDown(UINT nFlags, CPoint point)
{
	for(int i=0;i<22;++i) if(m_knobRc[i].PtInRect(point)) {
		m_dragKnob=i; m_dragStartY=point.y; m_dragStartValue=VoiceKnobValue(m_voice,i); SetCapture(); return;
	}
	CCustomBlurDialogExBase::OnLButtonDown(nFlags,point);
}

void CSasamiFmVoiceDlg::OnMouseMove(UINT nFlags, CPoint point)
{
	if(m_dragKnob>=0 && (nFlags&MK_LBUTTON)) {
		const int vmax=(m_dragKnob>=20)?7:((m_dragKnob%5==0)?127:((m_dragKnob%5==4)?15:31));
		int val=max(0,min(vmax,m_dragStartValue+(m_dragStartY-point.y)*max(1,vmax/48)));
		if(m_dragKnob==20) m_voice[24]=(uint8_t)((m_voice[24]&7)|(val<<3));
		else if(m_dragKnob==21) m_voice[24]=(uint8_t)((m_voice[24]&0x38)|val);
		else { const int op=m_dragKnob/5,p=m_dragKnob%5,idx=(p+1)*4+op,mask=(p==0?127:(p==4?15:31)); m_voice[idx]=(uint8_t)((m_voice[idx]&~mask)|val); }
		InvalidateRect(m_bodyRc,FALSE); return;
	}
	CCustomBlurDialogExBase::OnMouseMove(nFlags,point);
}

void CSasamiFmVoiceDlg::OnLButtonUp(UINT nFlags, CPoint point)
{
	if(m_dragKnob>=0){ m_dragKnob=-1; if(GetCapture()==this) ReleaseCapture(); return; }
	CCustomBlurDialogExBase::OnLButtonUp(nFlags,point);
}

int CSasamiFmVoiceDlg::PreviewBeep()
{
	ScFmDoc* doc = (ScFmDoc*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(ScFmDoc));
	if (!doc) return 0;
	ScFmDocClear(doc);
	int vi = ScFmAllocVoice(doc, m_voice);
	int ok = 0;
	if (vi >= 0) {
		ScFmAddVoiceSelect(doc, 0, 0, vi, 1);
		ScFmAddVolTl(doc, 0, 0, 16);
		ScFmAddNote(doc, 0, 0, (uint8_t)((4 << 4) | 0), 48);
		ScFmAddRest(doc, 48, 0, 24);
		SasamiWriteFm* w = (SasamiWriteFm*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(SasamiWriteFm));
		if (w && ScFmDocToWrite(doc, w)) {
			uint8_t* bin = (uint8_t*)HeapAlloc(GetProcessHeap(), 0, SASAMI_WRITE_MAX);
			if (bin) {
				uint32_t sz = SasamiBuildFpy(w, bin, SASAMI_WRITE_MAX);
				if (sz) {
					wchar_t dir[MAX_PATH], path[MAX_PATH];
					GetTempPathW(MAX_PATH, dir);
					_snwprintf_s(path, _TRUNCATE, L"%sogg_sasami_voice_prev.fpy", dir);
					ok = SasamiWriteFileW(path, bin, sz);
					if (ok && pl) pl->AddFilePath(path);
					else ok = 0;
				}
				HeapFree(GetProcessHeap(), 0, bin);
			}
			SasamiWriteFmClear(w);
		}
		if (w) HeapFree(GetProcessHeap(), 0, w);
	}
	HeapFree(GetProcessHeap(), 0, doc);
	return ok;
}

void CSasamiFmVoiceDlg::OnBnClickedPreview() { PreviewBeep(); }

void CSasamiFmVoiceDlg::OnBnClickedOk()
{
	ScSaveWndGeom(this, &savedata.sasamiFmVoiceX, &savedata.sasamiFmVoiceY,
		&savedata.sasamiFmVoiceW, &savedata.sasamiFmVoiceH);
	EndDialog(IDOK);
}

void CSasamiFmVoiceDlg::OnBnClickedCancel()
{
	ScSaveWndGeom(this, &savedata.sasamiFmVoiceX, &savedata.sasamiFmVoiceY,
		&savedata.sasamiFmVoiceW, &savedata.sasamiFmVoiceH);
	EndDialog(IDCANCEL);
}
