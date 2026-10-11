#include "stdafx.h"
#include "ogg.h"
#include "CSasamiMidiExportDlg.h"
#include <shlobj.h>
#include <shobjidl.h>

struct MexSaveState {
	int loops;
	wchar_t label[80];
	wchar_t hint[520];
	wchar_t bad[240];
};

enum { kMexGroup = 1, kMexEdit = 2, kMexHint = 3 };

static int MexReadLoops(const wchar_t* s, int* out)
{
	if (!s) return 0;
	while (*s == L' ' || *s == L'\t') s++;
	if (*s < L'0' || *s > L'9')
		return 0;
	int n = 0;
	for (; *s; s++) {
		if (*s == L' ' || *s == L'\t')
			continue;
		if (*s < L'0' || *s > L'9')
			return 0;
		n = n * 10 + (*s - L'0');
		if (n > 32)
			return 0;
	}
	if (n < 1)
		return 0;
	*out = n;
	return 1;
}

/* 回数の検査だけ。フォルダ移動などは通す。 */
class MexSaveEvents : public IFileDialogEvents {
public:
	LONG m_ref;
	MexSaveState* m_st;
	IFileDialogCustomize* m_cust;
	MexSaveEvents() : m_ref(1), m_st(NULL), m_cust(NULL) {}
	STDMETHODIMP QueryInterface(REFIID riid, void** ppv) {
		if (!ppv) return E_POINTER;
		if (riid == IID_IUnknown || riid == IID_IFileDialogEvents) {
			*ppv = static_cast<IFileDialogEvents*>(this);
			AddRef();
			return S_OK;
		}
		*ppv = NULL;
		return E_NOINTERFACE;
	}
	STDMETHODIMP_(ULONG) AddRef() { return (ULONG)InterlockedIncrement(&m_ref); }
	STDMETHODIMP_(ULONG) Release() {
		LONG n = InterlockedDecrement(&m_ref);
		if (n == 0) delete this;
		return (ULONG)n;
	}
	STDMETHODIMP OnFileOk(IFileDialog*) {
		if (!m_cust || !m_st) return S_OK;
		wchar_t* text = NULL;
		if (FAILED(m_cust->GetEditBoxText(kMexEdit, &text)) || !text) {
			m_cust->SetControlLabel(kMexHint, m_st->bad);
			return S_FALSE;
		}
		int n = 0;
		const int ok = MexReadLoops(text, &n);
		CoTaskMemFree(text);
		if (!ok) {
			m_cust->SetControlLabel(kMexHint, m_st->bad);
			return S_FALSE;
		}
		m_st->loops = n;
		return S_OK;
	}
	STDMETHODIMP OnFolderChanging(IFileDialog*, IShellItem*) { return S_OK; }
	STDMETHODIMP OnFolderChange(IFileDialog*) { return S_OK; }
	STDMETHODIMP OnSelectionChange(IFileDialog*) { return S_OK; }
	STDMETHODIMP OnShareViolation(IFileDialog*, IShellItem*, FDE_SHAREVIOLATION_RESPONSE* r) {
		if (r) *r = FDESVR_DEFAULT;
		return S_OK;
	}
	STDMETHODIMP OnTypeChange(IFileDialog*) { return S_OK; }
	STDMETHODIMP OnOverwrite(IFileDialog*, IShellItem*, FDE_OVERWRITE_RESPONSE* r) {
		if (r) *r = FDEOR_DEFAULT;
		return S_OK;
	}
};

static void MexFillText(MexSaveState* st)
{
	wcsncpy_s(st->label, LL14(
		L"ループ回数", L"Loop count", L"Nombre de boucles", L"Numero di cicli", L"Número de bucles",
		L"반복 횟수", L"循环次数", L"عدد التكرار", L"Число повторов", L"Wiederholungen",
		L"Repetições", L"Herhalingen", L"Liczba powtórzeń", L"Tekrar sayısı"), _TRUNCATE);
	wcsncpy_s(st->hint, LL14(
		L"前奏は1回のまま、繰り返し部分をこの回数書き込みます（1〜32）。",
		L"The intro is written once. The repeating section is written this many times (1–32).",
		L"L'intro une fois. La section répétée est écrite ce nombre de fois (1–32).",
		L"L'introduzione una volta. La sezione ripetuta è scritta questo numero di volte (1–32).",
		L"La introducción una vez. La sección que se repite se escribe este número de veces (1–32).",
		L"전주는 1회, 반복 구간은 이 횟수만큼 기록합니다(1–32).",
		L"前奏保留1次，重复段落按此次数写入（1–32）。",
		L"المقدمة مرة واحدة، ويُكتب المقطع المكرر بهذا العدد (1–32).",
		L"Вступление один раз, повторяющийся фрагмент — указанное число раз (1–32).",
		L"Intro einmal. Der Wiederholungsteil wird so oft geschrieben (1–32).",
		L"A introdução uma vez. A parte repetida é escrita este número de vezes (1–32).",
		L"Intro één keer. Het herhaalde deel wordt zo vaak geschreven (1–32).",
		L"Wstęp raz. Powtarzana część jest zapisana tyle razy (1–32).",
		L"Giriş bir kez. Tekrarlanan bölüm bu kadar kez yazılır (1–32)."), _TRUNCATE);
	wcsncpy_s(st->bad, LL14(
		L"1〜32の整数を入力してください。",
		L"Enter an integer from 1 to 32.",
		L"Entrez un entier de 1 à 32.",
		L"Inserisci un intero da 1 a 32.",
		L"Introduzca un entero de 1 a 32.",
		L"1–32의 정수를 입력하세요.",
		L"请输入 1～32 的整数。",
		L"أدخل عددًا صحيحًا من 1 إلى 32.",
		L"Введите целое число от 1 до 32.",
		L"Geben Sie eine ganze Zahl von 1 bis 32 ein.",
		L"Introduza um inteiro de 1 a 32.",
		L"Voer een geheel getal van 1 tot 32 in.",
		L"Wpisz liczbę całkowitą od 1 do 32.",
		L"1 ile 32 arasında bir tam sayı girin."), _TRUNCATE);
}

/* 今の保存ダイアログは旧テンプレートを左上に貼る。回数はダイアログ自身の下段へ。 */
static int MexPickWithLoops(HWND owner, wchar_t* path, int pathChars, MexSaveState* st)
{
	const HRESULT co = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
	const int coOwn = (co == S_OK) ? 1 : 0;
	IFileSaveDialog* dlg = NULL;
	HRESULT hr = CoCreateInstance(CLSID_FileSaveDialog, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg));
	if (FAILED(hr) || !dlg) {
		if (coOwn) CoUninitialize();
		return 0;
	}
	COMDLG_FILTERSPEC spec;
	spec.pszName = L"Standard MIDI (*.mid)";
	spec.pszSpec = L"*.mid";
	dlg->SetFileTypes(1, &spec);
	dlg->SetFileTypeIndex(1);
	dlg->SetDefaultExtension(L"mid");
	dlg->SetFileName(path);
	DWORD opt = 0;
	dlg->GetOptions(&opt);
	dlg->SetOptions(opt | FOS_OVERWRITEPROMPT | FOS_PATHMUSTEXIST | FOS_FORCEFILESYSTEM | FOS_NOCHANGEDIR);
	IFileDialogCustomize* cust = NULL;
	dlg->QueryInterface(IID_PPV_ARGS(&cust));
	if (cust) {
		cust->StartVisualGroup(kMexGroup, st->label);
		cust->AddEditBox(kMexEdit, L"4");
		cust->EndVisualGroup();
		cust->AddText(kMexHint, st->hint);
	}
	MexSaveEvents* ev = new MexSaveEvents();
	ev->m_st = st;
	ev->m_cust = cust;
	DWORD cookie = 0;
	dlg->Advise(ev, &cookie);
	hr = dlg->Show(owner);
	if (cookie) dlg->Unadvise(cookie);
	ev->Release();
	int ok = 0;
	if (SUCCEEDED(hr)) {
		IShellItem* item = NULL;
		if (SUCCEEDED(dlg->GetResult(&item)) && item) {
			wchar_t* name = NULL;
			if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &name)) && name) {
				wcsncpy_s(path, (size_t)pathChars, name, _TRUNCATE);
				CoTaskMemFree(name);
				ok = 1;
			}
			item->Release();
		}
	}
	if (cust) cust->Release();
	dlg->Release();
	if (coOwn) CoUninitialize();
	return ok;
}

int SasamiMidiPickSave(HWND owner, wchar_t* path, int pathChars, int askLoops, int* loops)
{
	if (!path || pathChars < 8)
		return 0;
	if (loops)
		*loops = 1;
	MexSaveState st;
	ZeroMemory(&st, sizeof(st));
	st.loops = 4;
	if (askLoops) {
		MexFillText(&st);
		if (!MexPickWithLoops(owner, path, pathChars, &st))
			return 0;
		if (loops)
			*loops = st.loops;
		return 1;
	}
	OPENFILENAMEW ofn;
	ZeroMemory(&ofn, sizeof(ofn));
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = owner;
	ofn.lpstrFilter = L"Standard MIDI (*.mid)\0*.mid\0\0";
	ofn.lpstrFile = path;
	ofn.nMaxFile = (DWORD)pathChars;
	ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_EXPLORER | OFN_ENABLESIZING;
	ofn.lpstrDefExt = L"mid";
	if (!GetSaveFileNameW(&ofn))
		return 0;
	if (loops)
		*loops = 1;
	return 1;
}
