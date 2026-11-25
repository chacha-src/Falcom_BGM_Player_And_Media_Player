#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <ocidl.h>
#include <stdio.h>
#include <vector>
#include <string>
#include "OxzId.h"
#include "OxzLang.h"
#include "OxzExt.h"

static HINSTANCE g_hinst = NULL;
static volatile long g_objs = 0;
static volatile long g_locks = 0;

static std::wstring OxzDllExe()
{
	wchar_t buf[32768];
	GetModuleFileNameW(g_hinst, buf, 32768);
	wchar_t* s = wcsrchr(buf, L'\\');
	if (s) *(s + 1) = 0;
	return std::wstring(buf) + L"OxzGui.exe";
}

static std::wstring OxzQ(const std::wstring& s)
{
	return L"\"" + s + L"\"";
}

static bool OxzLaunch(const std::wstring& args)
{
	std::wstring cmd = OxzQ(OxzDllExe()) + L" " + args;
	std::vector<wchar_t> buf(cmd.begin(), cmd.end());
	buf.push_back(0);
	STARTUPINFOW si;
	PROCESS_INFORMATION pi;
	memset(&si, 0, sizeof(si));
	memset(&pi, 0, sizeof(pi));
	si.cb = sizeof(si);
	si.dwFlags = STARTF_USESHOWWINDOW;
	si.wShowWindow = SW_SHOWNORMAL;
	AllowSetForegroundWindow(ASFW_ANY);
	if (!CreateProcessW(NULL, &buf[0], NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi))
		return false;
	CloseHandle(pi.hThread);
	CloseHandle(pi.hProcess);
	return true;
}

static std::wstring OxzListFile()
{
	wchar_t temp[MAX_PATH];
	wchar_t file[MAX_PATH];
	static volatile long seq = 0;
	GetTempPathW(MAX_PATH, temp);
	_snwprintf_s(file, _TRUNCATE, L"%soxzdnd_%u_%ld.txt", temp, GetCurrentProcessId(), InterlockedIncrement(&seq));
	return file;
}

static bool OxzWriteSel(const std::wstring& file, const std::vector<std::wstring>& paths)
{
	HANDLE h = CreateFileW(file.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	DWORD wr = 0;
	wchar_t bom = 0xFEFF;
	if (h == INVALID_HANDLE_VALUE) return false;
	WriteFile(h, &bom, sizeof(bom), &wr, NULL);
	for (size_t i = 0; i < paths.size(); ++i) {
		std::wstring line = paths[i] + L"\r\n";
		WriteFile(h, line.data(), (DWORD)(line.size() * sizeof(wchar_t)), &wr, NULL);
	}
	CloseHandle(h);
	return true;
}

enum { MODE_PACK = 1, MODE_HERE = 2, MODE_TO = 3, MODE_DROP = 4, MODE_MENU = 5, MODE_DRAG = 6 };

class OxzExec : public IExecuteCommand, public IObjectWithSelection, public IInitializeCommand
{
public:
	explicit OxzExec(int mode) : m_ref(1), m_mode(mode), m_sel(NULL) { InterlockedIncrement(&g_objs); }
	~OxzExec() { if (m_sel) m_sel->Release(); InterlockedDecrement(&g_objs); }

	IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv)
	{
		if (!ppv) return E_POINTER;
		*ppv = NULL;
		if (riid == IID_IUnknown || riid == IID_IExecuteCommand)
			*ppv = static_cast<IExecuteCommand*>(this);
		else if (riid == IID_IObjectWithSelection)
			*ppv = static_cast<IObjectWithSelection*>(this);
		else if (riid == IID_IInitializeCommand)
			*ppv = static_cast<IInitializeCommand*>(this);
		else
			return E_NOINTERFACE;
		AddRef();
		return S_OK;
	}
	IFACEMETHODIMP_(ULONG) AddRef() { return (ULONG)InterlockedIncrement(&m_ref); }
	IFACEMETHODIMP_(ULONG) Release()
	{
		long n = InterlockedDecrement(&m_ref);
		if (n == 0) delete this;
		return (ULONG)n;
	}
	IFACEMETHODIMP SetKeyState(DWORD) { return S_OK; }
	IFACEMETHODIMP SetParameters(LPCWSTR) { return S_OK; }
	IFACEMETHODIMP SetPosition(POINT) { return S_OK; }
	IFACEMETHODIMP SetShowWindow(int) { return S_OK; }
	IFACEMETHODIMP SetNoShowUI(BOOL) { return S_OK; }
	IFACEMETHODIMP SetDirectory(LPCWSTR psz)
	{
		m_dir = psz ? psz : L"";
		return S_OK;
	}
	IFACEMETHODIMP SetSelection(IShellItemArray* psia)
	{
		if (m_sel) m_sel->Release();
		m_sel = psia;
		if (m_sel) m_sel->AddRef();
		return S_OK;
	}
	IFACEMETHODIMP GetSelection(REFIID riid, void** ppv)
	{
		if (!m_sel) return E_FAIL;
		return m_sel->QueryInterface(riid, ppv);
	}
	IFACEMETHODIMP Initialize(LPCWSTR, IPropertyBag*) { return S_OK; }
	IFACEMETHODIMP Execute()
	{
		std::vector<std::wstring> paths;
		if (m_sel) {
			DWORD n = 0;
			m_sel->GetCount(&n);
			for (DWORD i = 0; i < n; ++i) {
				IShellItem* it = NULL;
				PWSTR name = NULL;
				if (FAILED(m_sel->GetItemAt(i, &it)) || !it) continue;
				if (SUCCEEDED(it->GetDisplayName(SIGDN_FILESYSPATH, &name)) && name) {
					paths.push_back(name);
					CoTaskMemFree(name);
				}
				it->Release();
			}
		}
		if (paths.empty() && !m_dir.empty()) paths.push_back(m_dir);
		if (paths.empty()) return E_FAIL;
		std::wstring list = OxzListFile();
		if (!OxzWriteSel(list, paths)) return E_FAIL;
		const wchar_t* sw = L"/packlist";
		if (m_mode == MODE_HERE) sw = L"/exthere";
		else if (m_mode == MODE_TO) sw = L"/extractto";
		if (!OxzLaunch(std::wstring(sw) + L" " + OxzQ(list))) return E_FAIL;
		return S_OK;
	}

private:
	long m_ref;
	int m_mode;
	IShellItemArray* m_sel;
	std::wstring m_dir;
};

class OxzDrop : public IDropTarget, public IPersistFile
{
public:
	OxzDrop() : m_ref(1), m_effect(DROPEFFECT_NONE) { InterlockedIncrement(&g_objs); }
	~OxzDrop() { InterlockedDecrement(&g_objs); }

	IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv)
	{
		if (!ppv) return E_POINTER;
		*ppv = NULL;
		if (riid == IID_IUnknown || riid == IID_IDropTarget)
			*ppv = static_cast<IDropTarget*>(this);
		else if (riid == IID_IPersistFile || riid == IID_IPersist)
			*ppv = static_cast<IPersistFile*>(this);
		else
			return E_NOINTERFACE;
		AddRef();
		return S_OK;
	}
	IFACEMETHODIMP_(ULONG) AddRef() { return (ULONG)InterlockedIncrement(&m_ref); }
	IFACEMETHODIMP_(ULONG) Release()
	{
		long n = InterlockedDecrement(&m_ref);
		if (n == 0) delete this;
		return (ULONG)n;
	}
	IFACEMETHODIMP GetClassID(CLSID* p) { if (!p) return E_POINTER; *p = CLSID_OxzDrop; return S_OK; }
	IFACEMETHODIMP IsDirty() { return S_FALSE; }
	IFACEMETHODIMP Load(LPCOLESTR name, DWORD) { m_file = name ? name : L""; return m_file.empty() ? E_FAIL : S_OK; }
	IFACEMETHODIMP Save(LPCOLESTR, BOOL) { return E_NOTIMPL; }
	IFACEMETHODIMP SaveCompleted(LPCOLESTR) { return S_OK; }
	IFACEMETHODIMP GetCurFile(LPOLESTR* ppsz)
	{
		if (!ppsz) return E_POINTER;
		size_t cb = (m_file.size() + 1) * sizeof(wchar_t);
		*ppsz = (LPOLESTR)CoTaskMemAlloc(cb);
		if (!*ppsz) return E_OUTOFMEMORY;
		memcpy(*ppsz, m_file.c_str(), cb);
		return S_OK;
	}
	IFACEMETHODIMP DragEnter(IDataObject* data, DWORD, POINTL, DWORD* effect)
	{
		FORMATETC fmt = { CF_HDROP, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
		m_effect = (data && data->QueryGetData(&fmt) == S_OK) ? DROPEFFECT_COPY : DROPEFFECT_NONE;
		if (effect) *effect = m_effect;
		return S_OK;
	}
	IFACEMETHODIMP DragOver(DWORD, POINTL, DWORD* effect)
	{
		if (effect) *effect = m_effect;
		return S_OK;
	}
	IFACEMETHODIMP DragLeave() { return S_OK; }
	IFACEMETHODIMP Drop(IDataObject* data, DWORD, POINTL, DWORD* effect)
	{
		if (effect) *effect = m_effect;
		if (!data || m_file.empty()) return E_FAIL;
		FORMATETC fmt = { CF_HDROP, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
		STGMEDIUM stg;
		memset(&stg, 0, sizeof(stg));
		if (FAILED(data->GetData(&fmt, &stg))) return E_FAIL;
		std::vector<std::wstring> paths;
		HDROP h = (HDROP)GlobalLock(stg.hGlobal);
		if (h) {
			UINT n = DragQueryFileW(h, 0xFFFFFFFF, NULL, 0);
			for (UINT i = 0; i < n; ++i) {
				UINT c = DragQueryFileW(h, i, NULL, 0);
				if (!c) continue;
				std::wstring s(c + 1, L'\0');
				DragQueryFileW(h, i, &s[0], c + 1);
				s.resize(wcslen(s.c_str()));
				if (!s.empty()) paths.push_back(s);
			}
			GlobalUnlock(stg.hGlobal);
		}
		ReleaseStgMedium(&stg);
		if (paths.empty()) return E_FAIL;
		std::wstring list = OxzListFile();
		if (!OxzWriteSel(list, paths)) return E_FAIL;
		if (!OxzLaunch(L"/addto " + OxzQ(m_file) + L" /list " + OxzQ(list))) return E_FAIL;
		return S_OK;
	}

private:
	long m_ref;
	DWORD m_effect;
	std::wstring m_file;
};

enum {
	ACT_OPEN = 1, ACT_EXTRACT_DLG, ACT_EXTRACT_HERE, ACT_EXTRACT_TO, ACT_TEST,
	ACT_COMPRESS_DLG, ACT_COMPRESS, ACT_DRAG_HERE, ACT_DRAG_FOLDER, ACT_DRAG_CREATE,
	ACT_TAG = 20, ACT_CONV = 21, ACT_PACK_EACH = 22,
	ACT_TO_OXZ = 30, ACT_TO_ZIP = 31, ACT_TO_TAR = 32
};

struct OxzCmd { int act; std::wstring extra; };
class OxzMenu;
static OxzMenu* g_dragOwner = NULL;
static DWORD g_dragTick = 0;

static bool EndsOxz(const std::wstring& path)
{
	return path.size() >= 4 && _wcsicmp(path.c_str() + path.size() - 4, L".oxz") == 0;
}

static bool IsDirPath(const std::wstring& path)
{
	DWORD a = GetFileAttributesW(path.c_str());
	return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

static bool EndsI(const std::wstring& path, const wchar_t* ext)
{
	size_t n = path.size();
	size_t e = ext ? wcslen(ext) : 0;
	return e && e <= n && _wcsicmp(path.c_str() + n - e, ext) == 0;
}

static bool IsVolumeExt(const std::wstring& path)
{
	size_t dot = path.find_last_of(L'.');
	size_t i;
	if (dot == std::wstring::npos || path.size() < dot + 4 || path.size() > dot + 6) return false;
	if ((path[dot + 1] != L'o' && path[dot + 1] != L'O')) return false;
	for (i = dot + 2; i < path.size(); ++i)
		if (path[i] < L'0' || path[i] > L'9') return false;
	return path.size() - (dot + 2) >= 2;
}

static bool IsExtractable(const std::wstring& path)
{
	if (IsDirPath(path)) return false;
	if (IsVolumeExt(path)) return true;
	for (int i = 0; kOxzExtractExts[i]; ++i)
		if (EndsI(path, kOxzExtractExts[i])) return true;
	return false;
}

static void StripArcExt(std::wstring& name)
{
	for (int i = 0; kOxzExtractExts[i]; ++i) {
		if (!EndsI(name, kOxzExtractExts[i])) continue;
		name.resize(name.size() - wcslen(kOxzExtractExts[i]));
		break;
	}
	if (EndsI(name, L".tar")) name.resize(name.size() - 4);
}

static bool IsAudioPath(const std::wstring& path)
{
	if (path.size() < 5 || IsDirPath(path)) return false;
	const wchar_t* e = path.c_str() + path.size() - 4;
	if (path.size() >= 5 && _wcsicmp(path.c_str() + path.size() - 5, L".flac") == 0) return true;
	return _wcsicmp(e, L".wav") == 0 || _wcsicmp(e, L".mp3") == 0;
}

static std::wstring BaseOf(const std::wstring& path)
{
	size_t s = path.find_last_of(L"\\/");
	return s == std::wstring::npos ? path : path.substr(s + 1);
}

static std::wstring ParentOf(const std::wstring& path)
{
	size_t s = path.find_last_of(L"\\/");
	if (s == std::wstring::npos) return L"";
	if (s == 2 && path.size() >= 2 && path[1] == L':') return path.substr(0, 3);
	return path.substr(0, s);
}

static std::wstring JoinOf(const std::wstring& dir, const std::wstring& name)
{
	if (dir.empty()) return name;
	if (dir.back() == L'\\' || dir.back() == L'/') return dir + name;
	return dir + L"\\" + name;
}

static std::wstring LeafOxz(const std::vector<std::wstring>& paths)
{
	std::wstring name;
	if (paths.size() == 1) {
		name = BaseOf(paths[0]);
		if (!IsDirPath(paths[0])) {
			size_t dot = name.find_last_of(L'.');
			if (dot != std::wstring::npos && dot > 0) name.resize(dot);
		}
	} else if (!paths.empty()) {
		name = BaseOf(ParentOf(paths[0]));
		if (name.empty() || name.back() == L':') name = L"oxz";
	}
	while (!name.empty() && (name.back() == L'\\' || name.back() == L'/')) name.pop_back();
	if (name.empty()) name = L"oxz";
	return name + L".oxz";
}

static std::wstring AvoidOf(std::wstring dest, const std::vector<std::wstring>& inputs)
{
	auto clash = [&](const std::wstring& d) {
		for (size_t i = 0; i < inputs.size(); ++i)
			if (_wcsicmp(inputs[i].c_str(), d.c_str()) == 0) return true;
		return false;
	};
	if (!clash(dest)) return dest;
	std::wstring stem = dest;
	if (EndsOxz(stem)) stem.resize(stem.size() - 4);
	for (int n = 2; n < 100; ++n) {
		wchar_t b[32];
		_snwprintf_s(b, _TRUNCATE, L"_%d.oxz", n);
		std::wstring t = stem + b;
		if (!clash(t)) return t;
	}
	return dest;
}

static std::wstring Amp(const std::wstring& s)
{
	std::wstring o;
	for (size_t i = 0; i < s.size(); ++i) {
		if (s[i] == L'&') o.push_back(L'&');
		o.push_back(s[i]);
	}
	return o;
}

static std::wstring Format1(int id, const std::wstring& arg)
{
	std::wstring safe = Amp(arg);
	const wchar_t* fmt = OxzTxt(id);
	int n = _scwprintf(fmt, safe.c_str());
	if (n < 0) n = 512;
	std::wstring buf((size_t)n + 1, L'\0');
	_snwprintf_s(&buf[0], buf.size(), _TRUNCATE, fmt, safe.c_str());
	buf.resize(wcslen(buf.c_str()));
	return buf;
}

static const wchar_t* VerbOf(int act)
{
	switch (act) {
	case ACT_OPEN: return L"oxz.open";
	case ACT_EXTRACT_DLG: return L"oxz.extractdlg";
	case ACT_EXTRACT_HERE: return L"oxz.exthere";
	case ACT_EXTRACT_TO: return L"oxz.extractto";
	case ACT_TEST: return L"oxz.test";
	case ACT_COMPRESS_DLG: return L"oxz.compressdlg";
	case ACT_COMPRESS: return L"oxz.compress";
	case ACT_DRAG_HERE: return L"oxz.draghere";
	case ACT_DRAG_FOLDER: return L"oxz.dragfolder";
	case ACT_TAG: return L"oxz.tagedit";
	case ACT_CONV: return L"oxz.convert";
	case ACT_PACK_EACH: return L"oxz.packeach";
	case ACT_TO_OXZ: return L"oxz.tooxz";
	case ACT_TO_ZIP: return L"oxz.tozip";
	case ACT_TO_TAR: return L"oxz.totar";
	default: return L"oxz.create";
	}
}

class OxzMenu : public IContextMenu, public IShellExtInit
{
public:
	explicit OxzMenu(int drag) : m_ref(1), m_drag(drag), m_idFirst(0), m_idLast(0), m_filled(NULL)
	{
		InterlockedIncrement(&g_objs);
	}
	~OxzMenu()
	{
		if (g_dragOwner == this) g_dragOwner = NULL;
		InterlockedDecrement(&g_objs);
	}
	IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv)
	{
		if (!ppv) return E_POINTER;
		*ppv = NULL;
		if (riid == IID_IUnknown || riid == IID_IContextMenu)
			*ppv = static_cast<IContextMenu*>(this);
		else if (riid == IID_IShellExtInit)
			*ppv = static_cast<IShellExtInit*>(this);
		else
			return E_NOINTERFACE;
		AddRef();
		return S_OK;
	}
	IFACEMETHODIMP_(ULONG) AddRef() { return (ULONG)InterlockedIncrement(&m_ref); }
	IFACEMETHODIMP_(ULONG) Release()
	{
		long n = InterlockedDecrement(&m_ref);
		if (n == 0) delete this;
		return (ULONG)n;
	}
	IFACEMETHODIMP Initialize(PCIDLIST_ABSOLUTE pidlFolder, IDataObject* data, HKEY)
	{
		try {
			m_folder.clear();
			m_files.clear();
			wchar_t path[MAX_PATH];
			if (pidlFolder && SHGetPathFromIDListW(pidlFolder, path)) m_folder = path;
			if (!data) return S_OK;
			FORMATETC fmt = { CF_HDROP, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
			STGMEDIUM stg;
			memset(&stg, 0, sizeof(stg));
			if (FAILED(data->GetData(&fmt, &stg))) return S_OK;
			HDROP h = (HDROP)GlobalLock(stg.hGlobal);
			if (h) {
				UINT n = DragQueryFileW(h, 0xFFFFFFFF, NULL, 0);
				for (UINT i = 0; i < n; ++i) {
					UINT c = DragQueryFileW(h, i, NULL, 0);
					if (!c) continue;
					std::wstring s(c + 1, L'\0');
					DragQueryFileW(h, i, &s[0], c + 1);
					s.resize(wcslen(s.c_str()));
					if (!s.empty()) m_files.push_back(s);
				}
				GlobalUnlock(stg.hGlobal);
			}
			ReleaseStgMedium(&stg);
			return S_OK;
		} catch (...) {
			return E_FAIL;
		}
	}
	IFACEMETHODIMP QueryContextMenu(HMENU hmenu, UINT indexMenu, UINT idCmdFirst, UINT idCmdLast, UINT uFlags)
	{
		try {
			if (uFlags & CMF_DEFAULTONLY) return S_OK;
			if (m_filled == hmenu && !m_cmds.empty())
				return MAKE_HRESULT(SEVERITY_SUCCESS, 0, (USHORT)m_cmds.size());
			if (m_drag && g_dragOwner && g_dragOwner != this && GetTickCount() - g_dragTick < 50)
				return S_OK;
			OxzLangInit();
			m_cmds.clear();
			m_filled = hmenu;
			m_idFirst = idCmdFirst;
			m_idLast = idCmdLast;
			if (!m_drag && m_files.empty() && !m_folder.empty()) m_files.push_back(m_folder);
			if (m_drag && m_folder.empty() && !m_files.empty()) m_folder = ParentOf(m_files[0]);
			if (m_files.empty()) return S_OK;
			int oxzCount = 0;
			std::wstring one;
			for (size_t i = 0; i < m_files.size(); ++i) {
				if (!IsExtractable(m_files[i])) continue;
				oxzCount++;
				one = m_files[i];
			}
			std::wstring where = (m_drag && !m_folder.empty()) ? m_folder : ParentOf(m_files[0]);
			std::wstring dest = AvoidOf(JoinOf(where, LeafOxz(m_files)), m_files);
			HMENU sub = CreatePopupMenu();
			if (sub) {
				FillStandard(sub, dest, oxzCount, one);
				if (GetMenuItemCount(sub) > 0) {
					MENUITEMINFOW top;
					memset(&top, 0, sizeof(top));
					top.cbSize = sizeof(top);
					top.fMask = MIIM_SUBMENU | MIIM_STRING;
					top.hSubMenu = sub;
					top.dwTypeData = (LPWSTR)OxzTxt(OS_MENU);
					InsertMenuItemW(hmenu, indexMenu, TRUE, &top);
					indexMenu++;
				} else {
					DestroyMenu(sub);
				}
			}
			if (!m_drag && oxzCount) {
				HMENU conv = CreatePopupMenu();
				if (conv) {
					AddItem(conv, OxzTxt(OS_TO_OXZ), ACT_TO_OXZ, one);
					AddItem(conv, OxzTxt(OS_TO_ZIP), ACT_TO_ZIP, one);
					AddItem(conv, OxzTxt(OS_TO_TAR), ACT_TO_TAR, one);
					MENUITEMINFOW top;
					memset(&top, 0, sizeof(top));
					top.cbSize = sizeof(top);
					top.fMask = MIIM_SUBMENU | MIIM_STRING;
					top.hSubMenu = conv;
					top.dwTypeData = (LPWSTR)OxzTxt(OS_ARCHIVE_CONV);
					InsertMenuItemW(hmenu, indexMenu, TRUE, &top);
					indexMenu++;
				}
			}
			if (m_drag) {
				int at = (int)indexMenu;
				AddSep(hmenu, at);
				at++;
				if (oxzCount) {
					if (AddItem(hmenu, OxzTxt(OS_DRAG_HERE), ACT_DRAG_HERE, m_folder, at)) at++;
					if (AddItem(hmenu, OxzTxt(OS_DRAG_FOLDER), ACT_DRAG_FOLDER, m_folder, at)) at++;
				}
				if (AddItem(hmenu, Format1(OS_DRAG_CREATE, BaseOf(dest)), ACT_DRAG_CREATE, dest, at)) at++;
				if (m_files.size() > 1 && AddItem(hmenu, OxzTxt(OS_PACK_EACH_HERE), ACT_PACK_EACH, m_folder, at)) at++;
				HMENU formats = CreatePopupMenu();
				if (formats) {
					std::wstring zipDest = dest, tarDest = dest;
					size_t dot = dest.find_last_of(L'.');
					if (dot != std::wstring::npos) { zipDest.resize(dot); tarDest.resize(dot); }
					zipDest += L".zip";
					tarDest += L".tar";
					AddItem(formats, OxzTxt(OS_MENU), ACT_DRAG_CREATE, dest);
					AddItem(formats, OxzTxt(OS_TO_ZIP), ACT_DRAG_CREATE, zipDest);
					AddItem(formats, OxzTxt(OS_TO_TAR), ACT_DRAG_CREATE, tarDest);
					MENUITEMINFOW pop;
					memset(&pop, 0, sizeof(pop));
					pop.cbSize = sizeof(pop);
					pop.fMask = MIIM_SUBMENU | MIIM_STRING;
					pop.hSubMenu = formats;
					pop.dwTypeData = (LPWSTR)OxzTxt(OS_DRAG_SUB);
					InsertMenuItemW(hmenu, at, TRUE, &pop);
				}
			}
			if (m_drag) {
				g_dragOwner = this;
				g_dragTick = GetTickCount();
			}
			return MAKE_HRESULT(SEVERITY_SUCCESS, 0, (USHORT)m_cmds.size());
		} catch (...) {
			return E_FAIL;
		}
	}
	IFACEMETHODIMP InvokeCommand(CMINVOKECOMMANDINFO* info)
	{
		try {
			if (!info) return E_INVALIDARG;
			UINT_PTR offset = (UINT_PTR)-1;
			const wchar_t* verbW = NULL;
			if (info->cbSize >= sizeof(CMINVOKECOMMANDINFOEX) && (info->fMask & CMIC_MASK_UNICODE))
				verbW = ((CMINVOKECOMMANDINFOEX*)info)->lpVerbW;
			if (verbW && !IS_INTRESOURCE(verbW)) {
				for (size_t i = 0; i < m_cmds.size(); ++i)
					if (wcscmp(verbW, VerbOf(m_cmds[i].act)) == 0) { offset = i; break; }
			} else if (!verbW && info->lpVerb && !IS_INTRESOURCE(info->lpVerb)) {
				return E_FAIL;
			} else if (verbW) {
				offset = LOWORD((ULONG_PTR)verbW);
			} else {
				// The first command is offset 0. MAKEINTRESOURCE(0) is a null pointer.
				offset = LOWORD((ULONG_PTR)info->lpVerb);
			}
			if (offset >= m_cmds.size()) return E_INVALIDARG;
			return Run(m_cmds[offset]) ? S_OK : E_FAIL;
		} catch (...) {
			return E_FAIL;
		}
	}
	IFACEMETHODIMP GetCommandString(UINT_PTR idCmd, UINT uType, UINT*, CHAR* name, UINT cch)
	{
		if (idCmd >= m_cmds.size() || !name) return E_INVALIDARG;
		const wchar_t* s = VerbOf(m_cmds[idCmd].act);
		if ((uType & ~((UINT)GCS_UNICODE)) == GCS_VALIDATEA) return S_OK;
		if (uType == GCS_VERBW || uType == GCS_HELPTEXTW) {
			if (!cch) return E_INVALIDARG;
			lstrcpynW((LPWSTR)name, s, (int)cch);
			return S_OK;
		}
		if (uType == GCS_VERBA || uType == GCS_HELPTEXTA) {
			WideCharToMultiByte(CP_ACP, 0, s, -1, name, (int)cch, NULL, NULL);
			return S_OK;
		}
		return E_FAIL;
	}

private:
	void AddSep(HMENU menu, int at = -1)
	{
		MENUITEMINFOW mi;
		memset(&mi, 0, sizeof(mi));
		mi.cbSize = sizeof(mi);
		mi.fMask = MIIM_FTYPE;
		mi.fType = MFT_SEPARATOR;
		UINT pos = at < 0 ? (UINT)GetMenuItemCount(menu) : (UINT)at;
		InsertMenuItemW(menu, pos, TRUE, &mi);
	}
	bool AddItem(HMENU menu, const std::wstring& text, int act, const std::wstring& extra, int at = -1)
	{
		if (text.empty()) return false;
		if (m_idFirst + (UINT)m_cmds.size() > m_idLast) return false;
		MENUITEMINFOW mi;
		memset(&mi, 0, sizeof(mi));
		mi.cbSize = sizeof(mi);
		mi.fMask = MIIM_ID | MIIM_STRING;
		mi.wID = m_idFirst + (UINT)m_cmds.size();
		mi.dwTypeData = (LPWSTR)text.c_str();
		mi.cch = (UINT)text.size();
		UINT pos = at < 0 ? (UINT)GetMenuItemCount(menu) : (UINT)at;
		if (!InsertMenuItemW(menu, pos, TRUE, &mi)) return false;
		OxzCmd c;
		c.act = act;
		c.extra = extra;
		m_cmds.push_back(c);
		return true;
	}
	void FillStandard(HMENU sub, const std::wstring& dest, int oxzCount, const std::wstring& one)
	{
		if (m_files.size() == 1 && oxzCount == 1) AddItem(sub, OxzTxt(OS_OPEN), ACT_OPEN, one);
		if (oxzCount) {
			AddItem(sub, OxzTxt(OS_EXTRACT_DLG), ACT_EXTRACT_DLG, L"");
			AddItem(sub, OxzTxt(OS_SHELL_HERE), ACT_EXTRACT_HERE, L"");
			if (oxzCount == 1) {
				std::wstring stem = BaseOf(one);
				StripArcExt(stem);
				if (stem.empty()) stem = L"oxz";
				AddItem(sub, Format1(OS_EXTRACT_TO_NAME, stem), ACT_EXTRACT_TO, L"");
			}
			else AddItem(sub, OxzTxt(OS_EXTRACT_EACH), ACT_EXTRACT_TO, L"");
			AddItem(sub, OxzTxt(OS_TEST), ACT_TEST, L"");
			AddSep(sub);
			AddItem(sub, OxzTxt(OS_TO_OXZ), ACT_TO_OXZ, L"oxz");
			AddItem(sub, OxzTxt(OS_TO_ZIP), ACT_TO_ZIP, L"zip");
			AddItem(sub, OxzTxt(OS_TO_TAR), ACT_TO_TAR, L"tar");
			AddSep(sub);
		}
		AddItem(sub, OxzTxt(OS_COMPRESS_DLG), ACT_COMPRESS_DLG, L"");
		AddItem(sub, Format1(OS_COMPRESS_TO, BaseOf(dest)), ACT_COMPRESS, dest);
		if (!m_drag && m_files.size() > 1) AddItem(sub, OxzTxt(OS_PACK_EACH), ACT_PACK_EACH, L"");
		int audio = 0;
		for (size_t i = 0; i < m_files.size(); ++i)
			if (IsAudioPath(m_files[i])) audio++;
		if (audio) {
			AddSep(sub);
			AddItem(sub, OxzTxt(OS_TAGEDIT), ACT_TAG, L"");
			AddItem(sub, OxzTxt(OS_CONVERT), ACT_CONV, L"");
		}
	}
	bool Run(const OxzCmd& c)
	{
		std::vector<std::wstring> use = m_files;
		if (c.act == ACT_OPEN) {
			std::wstring path = c.extra.empty() ? (m_files.empty() ? L"" : m_files[0]) : c.extra;
			if (path.empty()) return false;
			return OxzLaunch(OxzQ(path));
		}
		if (c.act == ACT_EXTRACT_DLG || c.act == ACT_EXTRACT_HERE || c.act == ACT_EXTRACT_TO || c.act == ACT_TEST || c.act == ACT_DRAG_HERE || c.act == ACT_DRAG_FOLDER) {
			use.clear();
			for (size_t i = 0; i < m_files.size(); ++i)
				if (IsExtractable(m_files[i])) use.push_back(m_files[i]);
			if (use.empty()) return false;
		}
		if (c.act == ACT_TO_OXZ || c.act == ACT_TO_ZIP || c.act == ACT_TO_TAR) {
			use.clear();
			for (size_t i = 0; i < m_files.size(); ++i)
				if (IsExtractable(m_files[i])) use.push_back(m_files[i]);
			if (use.empty()) return false;
		}
		if (c.act == ACT_TAG || c.act == ACT_CONV) {
			use.clear();
			for (size_t i = 0; i < m_files.size(); ++i)
				if (IsAudioPath(m_files[i])) use.push_back(m_files[i]);
			if (use.empty()) return false;
		}
		std::wstring list = OxzListFile();
		if (!OxzWriteSel(list, use)) return false;
		std::wstring args;
		switch (c.act) {
		case ACT_EXTRACT_DLG: args = L"/extractdlg " + OxzQ(list); break;
		case ACT_EXTRACT_HERE: args = L"/exthere " + OxzQ(list); break;
		case ACT_EXTRACT_TO: args = L"/extractto " + OxzQ(list); break;
		case ACT_TEST: args = L"/test " + OxzQ(list); break;
		case ACT_TAG: args = L"/tagedit " + OxzQ(list); break;
		case ACT_CONV: args = L"/convert " + OxzQ(list); break;
		case ACT_TO_OXZ:
		case ACT_TO_ZIP:
		case ACT_TO_TAR: args = L"/convto " + c.extra + L" " + OxzQ(list); break;
		case ACT_COMPRESS_DLG: args = L"/editlist " + OxzQ(list); break;
		case ACT_COMPRESS:
		case ACT_DRAG_CREATE: args = L"/packto " + OxzQ(c.extra) + L" /list " + OxzQ(list); break;
		case ACT_DRAG_HERE: args = L"/extdest " + OxzQ(c.extra) + L" /list " + OxzQ(list); break;
		case ACT_DRAG_FOLDER: args = L"/extdest " + OxzQ(c.extra) + L" /sub /list " + OxzQ(list); break;
		case ACT_PACK_EACH:
			args = c.extra.empty() ? (L"/packeach " + OxzQ(list)) : (L"/packeachto " + OxzQ(c.extra) + L" /list " + OxzQ(list));
			break;
		default: return false;
		}
		return OxzLaunch(args);
	}
	long m_ref;
	int m_drag;
	UINT m_idFirst;
	UINT m_idLast;
	HMENU m_filled;
	std::wstring m_folder;
	std::vector<std::wstring> m_files;
	std::vector<OxzCmd> m_cmds;
};

class OxzFactory : public IClassFactory
{
public:
	explicit OxzFactory(int mode) : m_ref(1), m_mode(mode) {}
	IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv)
	{
		if (!ppv) return E_POINTER;
		if (riid == IID_IUnknown || riid == IID_IClassFactory) {
			*ppv = static_cast<IClassFactory*>(this);
			AddRef();
			return S_OK;
		}
		*ppv = NULL;
		return E_NOINTERFACE;
	}
	IFACEMETHODIMP_(ULONG) AddRef() { return (ULONG)InterlockedIncrement(&m_ref); }
	IFACEMETHODIMP_(ULONG) Release()
	{
		long n = InterlockedDecrement(&m_ref);
		if (n == 0) delete this;
		return (ULONG)n;
	}
	IFACEMETHODIMP CreateInstance(IUnknown* outer, REFIID riid, void** ppv)
	{
		if (outer) return CLASS_E_NOAGGREGATION;
		if (m_mode == MODE_DROP) {
			OxzDrop* o = new OxzDrop();
			HRESULT hr = o->QueryInterface(riid, ppv);
			o->Release();
			return hr;
		}
		if (m_mode == MODE_MENU || m_mode == MODE_DRAG) {
			OxzMenu* o = new OxzMenu(m_mode == MODE_DRAG);
			HRESULT hr = o->QueryInterface(riid, ppv);
			o->Release();
			return hr;
		}
		OxzExec* o = new OxzExec(m_mode);
		HRESULT hr = o->QueryInterface(riid, ppv);
		o->Release();
		return hr;
	}
	IFACEMETHODIMP LockServer(BOOL lock)
	{
		if (lock) InterlockedIncrement(&g_locks);
		else InterlockedDecrement(&g_locks);
		return S_OK;
	}

private:
	long m_ref;
	int m_mode;
};

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID)
{
	if (reason == DLL_PROCESS_ATTACH) {
		g_hinst = h;
		DisableThreadLibraryCalls(h);
	}
	return TRUE;
}

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void** ppv)
{
	int mode = 0;
	if (IsEqualGUID(rclsid, CLSID_OxzPack)) mode = MODE_PACK;
	else if (IsEqualGUID(rclsid, CLSID_OxzHere)) mode = MODE_HERE;
	else if (IsEqualGUID(rclsid, CLSID_OxzTo)) mode = MODE_TO;
	else if (IsEqualGUID(rclsid, CLSID_OxzDrop)) mode = MODE_DROP;
	else if (IsEqualGUID(rclsid, CLSID_OxzMenu)) mode = MODE_MENU;
	else if (IsEqualGUID(rclsid, CLSID_OxzDrag)) mode = MODE_DRAG;
	else return CLASS_E_CLASSNOTAVAILABLE;
	OxzFactory* f = new OxzFactory(mode);
	HRESULT hr = f->QueryInterface(riid, ppv);
	f->Release();
	return hr;
}

STDAPI DllCanUnloadNow(void)
{
	return (g_objs == 0 && g_locks == 0) ? S_OK : S_FALSE;
}

STDAPI DllRegisterServer(void)
{
	return S_OK;
}

STDAPI DllUnregisterServer(void)
{
	return S_OK;
}
