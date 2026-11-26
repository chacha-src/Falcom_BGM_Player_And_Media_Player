#include "OxzUpdate.h"
#include "OxzWork.h"
#include "OxzInst.h"
#include "OxzLang.h"
#include <wininet.h>
#include <shellapi.h>
#include <stdio.h>
#include <string>
#include <vector>

#pragma comment(lib, "wininet.lib")

static const wchar_t* kHost = L"ppp.oohara.jp";
static const wchar_t* kOxzObject = L"/download/Oxz.oxz";
static const wchar_t* kZipObject = L"/download/Oxz.zip";

static const wchar_t* kScript =
LR"PS(param(
  [string]$SrcExe,
  [string]$SrcDll,
  [string]$SrcSfx,
  [string]$Dest,
  [string]$Stage,
  [string]$Install,
  [string]$Elevate,
  [string]$WaitPid,
  [string]$Reopen
)
function Copy-Retry([string]$src, [string]$dst) {
  $old = $dst + '.old'
  for ($i = 0; $i -lt 50; $i++) {
    try {
      $dir = Split-Path -Parent $dst
      if (-not (Test-Path -LiteralPath $dir)) { [System.IO.Directory]::CreateDirectory($dir) | Out-Null }
      [System.IO.File]::Copy($src, $dst, $true)
      if (Test-Path -LiteralPath $old) { Remove-Item -LiteralPath $old -Force -ErrorAction SilentlyContinue }
      return
    } catch {
      try {
        if (Test-Path -LiteralPath $dst) {
          if (Test-Path -LiteralPath $old) { Remove-Item -LiteralPath $old -Force -ErrorAction SilentlyContinue }
          [System.IO.File]::Move($dst, $old)
          [System.IO.File]::Copy($src, $dst, $true)
          Remove-Item -LiteralPath $old -Force -ErrorAction SilentlyContinue
          return
        }
      } catch {}
    }
    Start-Sleep -Milliseconds 40
  }
  if ((-not (Test-Path -LiteralPath $dst)) -and (Test-Path -LiteralPath $old)) {
    try { [System.IO.File]::Move($old, $dst) } catch {}
  }
}
function Apply-Files {
  if ($WaitPid) {
    $deadline = (Get-Date).AddMilliseconds(1500)
    while ((Get-Process -Id ([int]$WaitPid) -ErrorAction SilentlyContinue) -and ((Get-Date) -lt $deadline)) {
      Start-Sleep -Milliseconds 25
    }
  }
  Get-Process -Name OxzGui -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
  Copy-Retry $SrcExe (Join-Path $Dest 'OxzGui.exe')
  Copy-Retry $SrcDll (Join-Path $Dest 'OxzShell.dll')
  if ($SrcSfx -and (Test-Path -LiteralPath $SrcSfx)) { Copy-Retry $SrcSfx (Join-Path $Dest 'OxzSfx.exe') }
  if ($Install -eq '1') {
    $installed = Join-Path $Dest 'OxzGui.exe'
    if (Test-Path -LiteralPath $installed) { & $installed /install_quiet }
  }
}
function Start-Oxz {
  $exe = Join-Path $Dest 'OxzGui.exe'
  if (-not (Test-Path -LiteralPath $exe)) { return }
  $cmd = Join-Path $env:SystemRoot 'System32\cmd.exe'
  $arg = '/c start "" "' + $exe + '"'
  if ($Reopen -and (Test-Path -LiteralPath $Reopen)) { $arg += ' "' + $Reopen + '"' }
  $psi = New-Object System.Diagnostics.ProcessStartInfo
  $psi.FileName = $cmd
  $psi.Arguments = $arg
  $psi.UseShellExecute = $false
  $psi.CreateNoWindow = $true
  [void][System.Diagnostics.Process]::Start($psi)
}
if ($Elevate -eq '1') {
  Apply-Files
  exit 0
}
$probe = Join-Path $Dest 'oxz_upd_probe.tmp'
$writable = $false
try {
  [System.IO.File]::WriteAllText($probe, 'x')
  Remove-Item -LiteralPath $probe -Force
  $writable = $true
} catch {}
if ($writable) {
  Apply-Files
} else {
  try {
    function Q([string]$s) { return '"' + $s + '"' }
    $ps = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
    $arg = '-NoProfile -WindowStyle Hidden -ExecutionPolicy Bypass -File ' + (Q $PSCommandPath) +
      ' -SrcExe ' + (Q $SrcExe) + ' -SrcDll ' + (Q $SrcDll) + ' -SrcSfx ' + (Q $SrcSfx) +
      ' -Dest ' + (Q $Dest) + ' -Stage ' + (Q $Stage) +
      ' -Install ' + (Q $Install) + ' -Elevate ' + (Q '1') +
      ' -WaitPid ' + (Q $WaitPid) + ' -Reopen ' + (Q $Reopen)
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $ps
    $psi.Arguments = $arg
    $psi.UseShellExecute = $true
    $psi.Verb = 'runas'
    $p = [System.Diagnostics.Process]::Start($psi)
    if ($p) { $p.WaitForExit() }
  } catch {}
}
Start-Oxz
if ($Stage) {
  $rd = Join-Path $env:SystemRoot 'System32\cmd.exe'
  Start-Process -FileName $rd -ArgumentList @('/c', ('rd /s /q "' + $Stage + '"')) -WindowStyle Hidden
}
Remove-Item -LiteralPath $PSCommandPath -Force -ErrorAction SilentlyContinue
exit 0
)PS";

static long long FileUtc(const wchar_t* path)
{
	WIN32_FILE_ATTRIBUTE_DATA fa;
	if (!GetFileAttributesExW(path, GetFileExInfoStandard, &fa)) return 0;
	ULARGE_INTEGER u;
	u.LowPart = fa.ftLastWriteTime.dwLowDateTime;
	u.HighPart = fa.ftLastWriteTime.dwHighDateTime;
	if (u.QuadPart < 116444736000000000ULL) return 0;
	return (long long)((u.QuadPart - 116444736000000000ULL) / 10000000ULL);
}

static void StampUtc(const wchar_t* path, long long unixTime)
{
	if (unixTime <= 0) return;
	ULARGE_INTEGER u;
	u.QuadPart = (ULONGLONG)unixTime * 10000000ULL + 116444736000000000ULL;
	FILETIME ft;
	ft.dwLowDateTime = u.LowPart;
	ft.dwHighDateTime = u.HighPart;
	HANDLE h = CreateFileW(path, FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE) return;
	SetFileTime(h, &ft, &ft, &ft);
	CloseHandle(h);
}

static void TuneNet(HINTERNET h)
{
	DWORD timeout = 20000;
	InternetSetOptionW(h, INTERNET_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
	InternetSetOptionW(h, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));
	InternetSetOptionW(h, INTERNET_OPTION_SEND_TIMEOUT, &timeout, sizeof(timeout));
	DWORD sec = SECURITY_FLAG_IGNORE_UNKNOWN_CA | SECURITY_FLAG_IGNORE_CERT_CN_INVALID | SECURITY_FLAG_IGNORE_CERT_DATE_INVALID;
	InternetSetOptionW(h, INTERNET_OPTION_SECURITY_FLAGS, &sec, sizeof(sec));
}

static const DWORD kNetFlags = INTERNET_FLAG_SECURE | INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE
	| INTERNET_FLAG_PRAGMA_NOCACHE | INTERNET_FLAG_NO_UI
	| INTERNET_FLAG_IGNORE_CERT_DATE_INVALID | INTERNET_FLAG_IGNORE_CERT_CN_INVALID;

static BOOL SendRelaxed(HINTERNET req)
{
	TuneNet(req);
	if (HttpSendRequestW(req, NULL, 0, NULL, 0)) return TRUE;
	DWORD sec = SECURITY_FLAG_IGNORE_UNKNOWN_CA | SECURITY_FLAG_IGNORE_CERT_CN_INVALID | SECURITY_FLAG_IGNORE_CERT_DATE_INVALID;
	InternetSetOptionW(req, INTERNET_OPTION_SECURITY_FLAGS, &sec, sizeof(sec));
	return HttpSendRequestW(req, NULL, 0, NULL, 0);
}

/* 1 = ある, -1 = 404, 0 = 時刻が取れないか通信失敗 */
static int ReadMod(HINTERNET req, long long* modified)
{
	DWORD status = 0, slen = sizeof(status);
	if (!HttpQueryInfoW(req, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &status, &slen, NULL))
		return 0;
	if (status == 404 || status == 410) return -1;
	if (status != 200) return 0;
	char raw[256];
	DWORD rawLen = sizeof(raw);
	SYSTEMTIME st;
	memset(raw, 0, sizeof(raw));
	memset(&st, 0, sizeof(st));
	if (!HttpQueryInfoA(req, HTTP_QUERY_LAST_MODIFIED, raw, &rawLen, NULL) || !InternetTimeToSystemTimeA(raw, &st, 0))
		return 0;
	FILETIME ft;
	if (!SystemTimeToFileTime(&st, &ft)) return 0;
	ULARGE_INTEGER u;
	u.LowPart = ft.dwLowDateTime;
	u.HighPart = ft.dwHighDateTime;
	*modified = (long long)((u.QuadPart - 116444736000000000ULL) / 10000000ULL);
	return 1;
}

static int ProbeVerb(HINTERNET conn, const wchar_t* verb, const wchar_t* object, long long* modified)
{
	DWORD flags = kNetFlags | INTERNET_FLAG_KEEP_CONNECTION;
	HINTERNET req = HttpOpenRequestW(conn, verb, object, NULL, NULL, NULL, flags, 0);
	if (!req) return 0;
	int rc = 0;
	if (SendRelaxed(req)) rc = ReadMod(req, modified);
	InternetCloseHandle(req);
	return rc;
}

static int Probe(HINTERNET inet, const wchar_t* object, long long* modified)
{
	HINTERNET conn = InternetConnectW(inet, kHost, INTERNET_DEFAULT_HTTPS_PORT, NULL, NULL, INTERNET_SERVICE_HTTP, 0, 0);
	if (!conn) return 0;
	TuneNet(conn);
	int rc = ProbeVerb(conn, L"HEAD", object, modified);
	if (rc == 0) rc = ProbeVerb(conn, L"GET", object, modified);
	InternetCloseHandle(conn);
	return rc;
}

static bool Download(const wchar_t* url, const wchar_t* dest)
{
	DeleteFileW(dest);
	HINTERNET inet = InternetOpenW(L"OxzGuiUpdate/1.0", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
	if (!inet) return false;
	TuneNet(inet);
	DWORD recv = 120000;
	InternetSetOptionW(inet, INTERNET_OPTION_RECEIVE_TIMEOUT, &recv, sizeof(recv));
	wchar_t bust[1200];
	_snwprintf_s(bust, _TRUNCATE, L"%s?t=%llu", url, (unsigned long long)GetTickCount64());
	HINTERNET urlh = InternetOpenUrlW(inet, bust, NULL, 0, kNetFlags, 0);
	if (!urlh) {
		InternetCloseHandle(inet);
		return false;
	}
	TuneNet(urlh);
	DWORD status = 0, slen = sizeof(status);
	if (!HttpQueryInfoW(urlh, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &status, &slen, NULL) || status < 200 || status >= 300) {
		InternetCloseHandle(urlh);
		InternetCloseHandle(inet);
		return false;
	}
	unsigned long long totalLen = 0;
	char lenBuf[64];
	DWORD lenLen = sizeof(lenBuf);
	memset(lenBuf, 0, sizeof(lenBuf));
	if (HttpQueryInfoA(urlh, HTTP_QUERY_CONTENT_LENGTH, lenBuf, &lenLen, NULL))
		totalLen = (unsigned long long)_atoi64(lenBuf);
	HANDLE file = CreateFileW(dest, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (file == INVALID_HANDLE_VALUE) {
		InternetCloseHandle(urlh);
		InternetCloseHandle(inet);
		return false;
	}
	unsigned char buf[64 * 1024];
	unsigned long long got = 0;
	DWORD last = GetTickCount();
	bool ok = true;
	for (;;) {
		DWORD n = 0;
		if (!InternetReadFile(urlh, buf, sizeof(buf), &n)) { ok = false; break; }
		if (!n) break;
		DWORD w = 0;
		if (!WriteFile(file, buf, n, &w, NULL) || w != n) { ok = false; break; }
		got += n;
		DWORD now = GetTickCount();
		if (now - last >= 150) {
			last = now;
			int pct = 0;
			if (totalLen) pct = (int)((got * 100ull) / totalLen);
			if (pct > 100) pct = 100;
			int overall = pct * 65 / 100;
			OxzProgFile(NULL, 0, 0, pct, overall);
		}
	}
	CloseHandle(file);
	InternetCloseHandle(urlh);
	InternetCloseHandle(inet);
	if (!ok || got < 1000) {
		DeleteFileW(dest);
		return false;
	}
	OxzProgFile(NULL, 0, 0, 100, 65);
	return true;
}

static bool FindNamed(const std::wstring& dir, const wchar_t* name, std::wstring& out)
{
	WIN32_FIND_DATAW fd;
	HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
	if (h == INVALID_HANDLE_VALUE) return false;
	bool found = false;
	do {
		if (fd.cFileName[0] == L'.' && (fd.cFileName[1] == 0 || (fd.cFileName[1] == L'.' && fd.cFileName[2] == 0)))
			continue;
		if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;
		std::wstring child = dir + L"\\" + fd.cFileName;
		if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
			if (FindNamed(child, name, out)) { found = true; break; }
		} else if (_wcsicmp(fd.cFileName, name) == 0) {
			out = child;
			found = true;
			break;
		}
	} while (FindNextFileW(h, &fd));
	FindClose(h);
	return found;
}

static bool LooksPe(const wchar_t* path, unsigned long long minBytes)
{
	HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE) return false;
	LARGE_INTEGER sz;
	unsigned char mag[2];
	DWORD rd = 0;
	bool ok = GetFileSizeEx(h, &sz) && (unsigned long long)sz.QuadPart >= minBytes
		&& ReadFile(h, mag, 2, &rd, NULL) && rd == 2 && mag[0] == 'M' && mag[1] == 'Z';
	CloseHandle(h);
	return ok;
}

static void PostFail(HWND hwnd, const wchar_t* text)
{
	const wchar_t* src = text && text[0] ? text : OxzTxt(OS_UPDATE_FAIL);
	size_t n = wcslen(src) + 1;
	wchar_t* copy = (wchar_t*)malloc(n * sizeof(wchar_t));
	if (!copy) return;
	memcpy(copy, src, n * sizeof(wchar_t));
	if (!PostMessageW(hwnd, WM_OXZ_UPDATE_FAIL, 0, (LPARAM)copy)) free(copy);
}

static bool WriteScript(const std::wstring& path)
{
	HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE) return false;
	WORD bom = 0xFEFF;
	DWORD wr = 0;
	WriteFile(h, &bom, sizeof(bom), &wr, NULL);
	size_t n = wcslen(kScript);
	bool ok = WriteFile(h, kScript, (DWORD)(n * sizeof(wchar_t)), &wr, NULL) && wr == n * sizeof(wchar_t);
	CloseHandle(h);
	if (!ok) DeleteFileW(path.c_str());
	return ok;
}

struct ApplyPack {
	HWND hwnd;
	OxzUpdHit hit;
	std::wstring reopen;
	std::wstring browse;
};

static int BrowseOk(const wchar_t* s)
{
	if (!s || !s[0]) return 0;
	if (wcsstr(s, L"..")) return 0;
	if (wcschr(s, L':') || wcschr(s, L'/')) return 0;
	if (s[0] == L'\\') return 0;
	return 1;
}

static void RememberReopen(const std::wstring& path, const std::wstring& browse)
{
	RegDeleteKeyValueW(HKEY_CURRENT_USER, L"Software\\OxzGui", L"reopen");
	RegDeleteKeyValueW(HKEY_CURRENT_USER, L"Software\\OxzGui", L"reopenBrowse");
	if (path.empty() || GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) return;
	wchar_t full[32768];
	DWORD n = GetFullPathNameW(path.c_str(), 32768, full, NULL);
	const wchar_t* use = (n && n < 32768) ? full : path.c_str();
	RegSetKeyValueW(HKEY_CURRENT_USER, L"Software\\OxzGui", L"reopen", REG_SZ, use, (DWORD)((wcslen(use) + 1) * sizeof(wchar_t)));
	if (BrowseOk(browse.c_str()))
		RegSetKeyValueW(HKEY_CURRENT_USER, L"Software\\OxzGui", L"reopenBrowse", REG_SZ, browse.c_str(), (DWORD)((browse.size() + 1) * sizeof(wchar_t)));
}

static void ForgetReopen()
{
	RegDeleteKeyValueW(HKEY_CURRENT_USER, L"Software\\OxzGui", L"reopen");
	RegDeleteKeyValueW(HKEY_CURRENT_USER, L"Software\\OxzGui", L"reopenBrowse");
}

static HANDLE ShellProcess()
{
	HWND desk = GetShellWindow();
	DWORD pid = 0;
	if (!desk) return NULL;
	GetWindowThreadProcessId(desk, &pid);
	if (!pid) return NULL;
	return OpenProcess(PROCESS_CREATE_PROCESS, FALSE, pid);
}

static bool CreateUpdater(wchar_t* cmd, const wchar_t* dir, DWORD flags, HANDLE parent)
{
	STARTUPINFOEXW sx;
	memset(&sx, 0, sizeof(sx));
	sx.StartupInfo.cb = parent ? (DWORD)sizeof(sx) : (DWORD)sizeof(STARTUPINFOW);
	sx.StartupInfo.dwFlags = STARTF_USESHOWWINDOW;
	sx.StartupInfo.wShowWindow = SW_HIDE;
	std::vector<BYTE> mem;
	if (parent) {
		SIZE_T bytes = 0;
		InitializeProcThreadAttributeList(NULL, 1, 0, &bytes);
		mem.resize(bytes ? bytes : 1);
		sx.lpAttributeList = (LPPROC_THREAD_ATTRIBUTE_LIST)&mem[0];
		if (!InitializeProcThreadAttributeList(sx.lpAttributeList, 1, 0, &bytes)
			|| !UpdateProcThreadAttribute(sx.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_PARENT_PROCESS, &parent, sizeof(parent), NULL, NULL)) {
			if (sx.lpAttributeList) DeleteProcThreadAttributeList(sx.lpAttributeList);
			return false;
		}
		flags |= EXTENDED_STARTUPINFO_PRESENT;
	}
	PROCESS_INFORMATION pi;
	memset(&pi, 0, sizeof(pi));
	BOOL ok = CreateProcessW(NULL, cmd, NULL, NULL, FALSE, flags, NULL, dir, &sx.StartupInfo, &pi);
	if (parent && sx.lpAttributeList) DeleteProcThreadAttributeList(sx.lpAttributeList);
	if (!ok) return false;
	CloseHandle(pi.hThread);
	CloseHandle(pi.hProcess);
	return true;
}

static bool LaunchUpdater(const std::wstring& cmd, const std::wstring& dir)
{
	std::vector<wchar_t> buf(cmd.begin(), cmd.end());
	buf.push_back(0);
	DWORD group = CREATE_NEW_PROCESS_GROUP | CREATE_NO_WINDOW;
	if (CreateUpdater(&buf[0], dir.c_str(), group | CREATE_BREAKAWAY_FROM_JOB, NULL)) return true;
	HANDLE shell = ShellProcess();
	if (shell) {
		bool ok = CreateUpdater(&buf[0], dir.c_str(), group, shell);
		CloseHandle(shell);
		if (ok) return true;
	}
	return CreateUpdater(&buf[0], dir.c_str(), group, NULL);
}

static DWORD WINAPI ApplyThread(LPVOID p)
{
	ApplyPack* pack = (ApplyPack*)p;
	HWND hwnd = pack->hwnd;
	OxzUpdHit hit = pack->hit;
	std::wstring reopen = pack->reopen;
	std::wstring browse = pack->browse;
	delete pack;
	if (!IsWindow(hwnd)) return 0;
	OxzProgBegin(hit.leaf);
	OxzProgFile(hit.leaf, 0, 1, 0, 0);
	wchar_t temp[MAX_PATH];
	GetTempPathW(MAX_PATH, temp);
	std::wstring stage = std::wstring(temp) + L"oxz_upd_" + std::to_wstring(GetCurrentProcessId());
	CreateDirectoryW(stage.c_str(), NULL);
	std::wstring pkg = stage + (hit.oxz ? L"\\Oxz.oxz" : L"\\Oxz.zip");
	std::wstring files = stage + L"\\files";
	CreateDirectoryW(files.c_str(), NULL);
	if (!Download(hit.url, pkg.c_str())) {
		OxzRmTree(stage);
		PostFail(hwnd, OxzTxt(OS_UPDATE_FAIL));
		return 0;
	}
	std::wstring err;
	int rc = hit.oxz ? OxzExtractTo(pkg.c_str(), files.c_str(), err, 65, 35) : OxzExtractPlain(pkg.c_str(), files.c_str(), err, 65, 35);
	if (rc != 0) {
		OxzRmTree(stage);
		PostFail(hwnd, err.empty() ? OxzTxt(OS_UPDATE_FAIL) : err.c_str());
		return 0;
	}
	std::wstring exe, dll, sfx;
	if (!FindNamed(files, L"OxzGui.exe", exe) || !FindNamed(files, L"OxzShell.dll", dll)
		|| !LooksPe(exe.c_str(), 80ull * 1024ull) || !LooksPe(dll.c_str(), 40ull * 1024ull)) {
		OxzRmTree(stage);
		PostFail(hwnd, OxzTxt(OS_UPDATE_BAD));
		return 0;
	}
	StampUtc(exe.c_str(), hit.modified);
	StampUtc(dll.c_str(), hit.modified);
	std::wstring script = std::wstring(temp) + L"oxz_upd_" + std::to_wstring(GetCurrentProcessId()) + L".ps1";
	if (!WriteScript(script)) {
		OxzRmTree(stage);
		PostFail(hwnd, OxzTxt(OS_UPDATE_FAIL));
		return 0;
	}
	std::wstring dest = OxzExeDir();
	std::wstring install = OxzIsInstalled() ? L"1" : L"0";
	wchar_t sys[MAX_PATH];
	GetSystemDirectoryW(sys, MAX_PATH);
	std::wstring ps = std::wstring(sys) + L"\\WindowsPowerShell\\v1.0\\powershell.exe";
	std::wstring args = L"-NoProfile -WindowStyle Hidden -ExecutionPolicy Bypass -File " + OxzQuote(script)
		+ L" -SrcExe " + OxzQuote(exe) + L" -SrcDll " + OxzQuote(dll)
		+ L" -SrcSfx " + OxzQuote((FindNamed(files, L"OxzSfx.exe", sfx) && LooksPe(sfx.c_str(), 80ull * 1024ull)) ? sfx : L"")
		+ L" -Dest " + OxzQuote(dest) + L" -Stage " + OxzQuote(stage)
		+ L" -Install " + install + L" -Elevate 0 -WaitPid " + std::to_wstring(GetCurrentProcessId());
	if (!reopen.empty() && GetFileAttributesW(reopen.c_str()) != INVALID_FILE_ATTRIBUTES)
		args += L" -Reopen " + OxzQuote(reopen);
	wchar_t work[MAX_PATH];
	GetTempPathW(MAX_PATH, work);
	RememberReopen(reopen, browse);
	if (!LaunchUpdater(OxzQuote(ps) + L" " + args, work)) {
		ForgetReopen();
		DeleteFileW(script.c_str());
		OxzRmTree(stage);
		PostFail(hwnd, OxzTxt(OS_UPDATE_FAIL));
		return 0;
	}
	ExitProcess(0);
	return 0;
}

static DWORD WINAPI CheckThread(LPVOID p)
{
	HWND hwnd = (HWND)p;
	DWORD flags = 0;
	if (!InternetGetConnectedState(&flags, 0)) return 0;
	wchar_t self[32768];
	GetModuleFileNameW(NULL, self, 32768);
	long long local = FileUtc(self);
	if (local <= 0) return 0;
	HINTERNET inet = InternetOpenW(L"OxzGuiUpdate/1.0", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
	if (!inet) return 0;
	TuneNet(inet);
	long long modified = 0;
	int which = Probe(inet, kOxzObject, &modified);
	int oxz = 1;
	if (which != 1) {
		oxz = 0;
		modified = 0;
		which = Probe(inet, kZipObject, &modified);
	}
	InternetCloseHandle(inet);
	if (which != 1 || modified <= local + 2) return 0;
	if (!IsWindow(hwnd)) return 0;
	OxzUpdHit* hit = new OxzUpdHit();
	memset(hit, 0, sizeof(*hit));
	_snwprintf_s(hit->url, _TRUNCATE, L"https://%s%s", kHost, oxz ? kOxzObject : kZipObject);
	wcscpy_s(hit->leaf, oxz ? L"Oxz.oxz" : L"Oxz.zip");
	hit->modified = modified;
	hit->oxz = oxz;
	if (!PostMessageW(hwnd, WM_OXZ_UPDATE, 0, (LPARAM)hit)) delete hit;
	return 0;
}

void OxzUpdateCheckAsync(HWND hwnd)
{
	HANDLE th = CreateThread(NULL, 0, CheckThread, hwnd, 0, NULL);
	if (th) CloseHandle(th);
}

void OxzUpdateBegin(HWND hwnd, const OxzUpdHit* hit, const wchar_t* reopen, const wchar_t* browse)
{
	if (!hwnd || !hit) return;
	ApplyPack* pack = new ApplyPack();
	pack->hwnd = hwnd;
	pack->hit = *hit;
	if (reopen && reopen[0]) pack->reopen = reopen;
	if (browse && browse[0]) pack->browse = browse;
	HANDLE th = CreateThread(NULL, 0, ApplyThread, pack, 0, NULL);
	if (!th) {
		delete pack;
		PostFail(hwnd, OxzTxt(OS_UPDATE_FAIL));
		return;
	}
	CloseHandle(th);
}
