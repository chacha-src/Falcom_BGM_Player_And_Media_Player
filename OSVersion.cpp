#include "stdafx.h"
#include "OSVersion.h"
#include <winreg.h>
#define PRODUCT_CORE_ARM                            0x00000061
#define PRODUCT_CORE_N                              0x00000062
#define PRODUCT_CORE_COUNTRYSPECIFIC                0x00000063
#define PRODUCT_CORE_SINGLELANGUAGE                 0x00000064
#define PRODUCT_CORE                                0x00000065
#define PRODUCT_PROFESSIONAL_WMC                    0x00000067
#define PRODUCT_MOBILE_CORE                         0x00000068
#define PRODUCT_EMBEDDED_INDUSTRY_EVAL              0x00000069
#define PRODUCT_EMBEDDED_INDUSTRY_E_EVAL            0x0000006A
#define PRODUCT_EMBEDDED_EVAL                       0x0000006B
#define PRODUCT_EMBEDDED_E_EVAL                     0x0000006C
#define PRODUCT_CORE_SERVER                         0x0000006D
#define PRODUCT_CLOUD_STORAGE_SERVER                0x0000006E
#define PRODUCT_EDUCATION							0x00000079
#define PRODUCT_EDUCATION_N							0x0000007A
#define PRODUCT_MOBILE_ENTERPRISE					0x00000085

// レジストリからバージョン情報を取得する関数
CString GetVersionFromRegistry()
{
	CString versionStr;
	HKEY hKey;
	LONG result = RegOpenKeyEx(HKEY_LOCAL_MACHINE,
		_T("SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion"),
		0, KEY_READ, &hKey);
	
	if (result == ERROR_SUCCESS)
	{
		TCHAR szDisplayVersion[256] = { 0 };
		TCHAR szReleaseId[256] = { 0 };
		TCHAR szCurrentBuild[256] = { 0 };
		TCHAR szUBR[256] = { 0 };
		DWORD dwType = REG_SZ;
		DWORD dwSize = sizeof(szDisplayVersion);
		
		// DisplayVersion (Windows 10 20H2以降、Windows 11で使用)
		if (RegQueryValueEx(hKey, _T("DisplayVersion"), NULL, &dwType, (LPBYTE)szDisplayVersion, &dwSize) == ERROR_SUCCESS)
		{
			versionStr = szDisplayVersion;
		}
		// ReleaseId (Windows 10 1903以前で使用)
		else
		{
			dwSize = sizeof(szReleaseId);
			if (RegQueryValueEx(hKey, _T("ReleaseId"), NULL, &dwType, (LPBYTE)szReleaseId, &dwSize) == ERROR_SUCCESS)
			{
				versionStr = szReleaseId;
			}
		}
		
		// CurrentBuild (ビルド番号)
		dwSize = sizeof(szCurrentBuild);
		if (RegQueryValueEx(hKey, _T("CurrentBuild"), NULL, &dwType, (LPBYTE)szCurrentBuild, &dwSize) == ERROR_SUCCESS)
		{
			// UBR (Update Build Revision) も取得
			dwSize = sizeof(szUBR);
			if (RegQueryValueEx(hKey, _T("UBR"), NULL, &dwType, (LPBYTE)szUBR, &dwSize) == ERROR_SUCCESS)
			{
				// ビルド番号とUBRを組み合わせる
			}
		}
		
		RegCloseKey(hKey);
	}
	
	return versionStr;
}

// ビルド番号からWindows 10/11/12のバージョン番号を判定
CString GetWindows10VersionString(DWORD buildNumber, BOOL isWindows11)
{
	CString versionStr;
	
	if (isWindows11)
	{
		// Windows 11
		if (buildNumber >= 26200)
		{
			versionStr = _T("25H2");
		}
		else if (buildNumber >= 26100)
		{
			versionStr = _T("24H2");
		}
		else if (buildNumber >= 22631)
		{
			versionStr = _T("23H2");
		}
		else if (buildNumber >= 22621)
		{
			versionStr = _T("22H2");
		}
		else if (buildNumber >= 22000)
		{
			versionStr = _T("21H2");
		}
	}
	else
	{
		// Windows 10
		if (buildNumber >= 26200)
		{
			versionStr = _T("25H2");
		}
		else if (buildNumber >= 26100)
		{
			versionStr = _T("24H2");
		}
		else if (buildNumber >= 19045)
		{
			versionStr = _T("23H2");
		}
		else if (buildNumber >= 19044)
		{
			versionStr = _T("22H2");
		}
		else if (buildNumber >= 19043)
		{
			versionStr = _T("21H1");
		}
		else if (buildNumber >= 19042)
		{
			versionStr = _T("20H2");
		}
		else if (buildNumber >= 19041)
		{
			versionStr = _T("2004");
		}
		else if (buildNumber >= 18363)
		{
			versionStr = _T("1909");
		}
		else if (buildNumber >= 18362)
		{
			versionStr = _T("1903");
		}
		else if (buildNumber >= 17763)
		{
			versionStr = _T("1809");
		}
		else if (buildNumber >= 17134)
		{
			versionStr = _T("1803");
		}
		else if (buildNumber >= 16299)
		{
			versionStr = _T("1709");
		}
		else if (buildNumber >= 15063)
		{
			versionStr = _T("1703");
		}
		else if (buildNumber >= 14393)
		{
			versionStr = _T("1607");
		}
		else if (buildNumber >= 10586)
		{
			versionStr = _T("1511");
		}
		else if (buildNumber >= 10240)
		{
			versionStr = _T("1507");
		}
	}
	
	return versionStr;
}

// ビルド番号からWindows 12のバージョン番号を判定
CString GetWindows12VersionString(DWORD buildNumber)
{
	CString versionStr;
	
	// Windows 12
	if (buildNumber >= 26200)
	{
		versionStr = _T("25H2");
	}
	else if (buildNumber >= 26100)
	{
		versionStr = _T("24H2");
	}
	else if (buildNumber >= 28000)
	{
		// Windows 12の初期バージョン（予想）
		versionStr = _T("24H2");
	}
	
	return versionStr;
}

// プレビュー版かどうかを判定
BOOL IsPreviewBuild()
{
	HKEY hKey;
	LONG result;
	
	// CurrentVersionのレジストリキーで判定
	result = RegOpenKeyEx(HKEY_LOCAL_MACHINE,
		_T("SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion"),
		0, KEY_READ, &hKey);
	
	if (result == ERROR_SUCCESS)
	{
		// 最も確実な方法: InsiderPreviewフラグを確認（1の場合のみプレビュー版）
		DWORD dwInsiderPreview = 0;
		DWORD dwType = REG_DWORD;
		DWORD dwSize = sizeof(dwInsiderPreview);
		if (RegQueryValueEx(hKey, _T("InsiderPreview"), NULL, &dwType, (LPBYTE)&dwInsiderPreview, &dwSize) == ERROR_SUCCESS)
		{
			if (dwInsiderPreview == 1)
			{
				RegCloseKey(hKey);
				return TRUE;
			}
		}
		
		// BuildLabExで判定（_prereleaseが含まれている場合のみプレビュー版）
		// 注意: _releaseや_refreshは正式リリース版でも使われるため、_prereleaseのみを判定
		TCHAR szBuildLabEx[512] = { 0 };
		dwType = REG_SZ;
		dwSize = sizeof(szBuildLabEx);
		
		if (RegQueryValueEx(hKey, _T("BuildLabEx"), NULL, &dwType, (LPBYTE)szBuildLabEx, &dwSize) == ERROR_SUCCESS)
		{
			CString buildLabEx = szBuildLabEx;
			buildLabEx.MakeLower();
			// _prereleaseが含まれている場合のみプレビュー版
			if (buildLabEx.Find(_T("_prerelease")) >= 0)
			{
				RegCloseKey(hKey);
				return TRUE;
			}
		}
		
		// ReleaseTypeで判定（一部のプレビュー版で使用）
		TCHAR szReleaseType[256] = { 0 };
		dwType = REG_SZ;
		dwSize = sizeof(szReleaseType);
		if (RegQueryValueEx(hKey, _T("ReleaseType"), NULL, &dwType, (LPBYTE)szReleaseType, &dwSize) == ERROR_SUCCESS)
		{
			CString releaseType = szReleaseType;
			releaseType.MakeLower();
			if (releaseType.Find(_T("insider")) >= 0 || 
				releaseType.Find(_T("preview")) >= 0 ||
				releaseType.Find(_T("prerelease")) >= 0)
			{
				RegCloseKey(hKey);
				return TRUE;
			}
		}
		
		RegCloseKey(hKey);
	}
	
	// WindowsSelfHost\Applicabilityキーの存在確認（より厳密に）
	// このキーが存在しても、正式リリース版にアップグレードした場合は残ることがあるため、
	// 上記の判定でプレビュー版でないことが確認できた場合は、このキーだけでは判定しない
	
	return FALSE;
}


BOOL COSVersion::IsWow64()
{
	BOOL bIsWow64 = FALSE;

	typedef BOOL(WINAPI *LPFN_ISWOW64PROCESS) (HANDLE, PBOOL);
	LPFN_ISWOW64PROCESS fnIsWow64Process;

	fnIsWow64Process = (LPFN_ISWOW64PROCESS)GetProcAddress(
		GetModuleHandle(TEXT("kernel32")), "IsWow64Process");

	if (NULL != fnIsWow64Process)
	{
		if (!fnIsWow64Process(GetCurrentProcess(), &bIsWow64))
		{
			//handle error
		}
	}
	return bIsWow64;
}

CString COSVersion::GetVersionString()
{
	edition = PRODUCT_UNDEFINED;
	ZeroMemory(&in, sizeof(in)); in.dwOSVersionInfoSize = sizeof(OSVERSIONINFOEX); GetVersionExW((OSVERSIONINFO*)&in);
	CString s,ss;
	HMODULE	hModule;

	BOOL(CALLBACK* pfnGetProductInfo)(DWORD dwOSMajorVersion, DWORD dwOSMinorVersion, DWORD dwSpMajorVersion, DWORD dwSpMinorVersion, PDWORD pdwReturnedProductType);
	hModule = ::LoadLibrary(_T("kernel32.dll"));
	(*(FARPROC*)&pfnGetProductInfo) = ::GetProcAddress(hModule, "GetProductInfo");
	switch (in.dwPlatformId) {
	case 1:
		switch (in.dwMinorVersion) {
		case 0:
			ss = _T("Windows 95"); break;
		case 10:
			ss = _T("Windows 98"); break;
		case 90:
			ss = _T("Windows Me"); break;
		}break;
	case 2:
		switch (in.dwMajorVersion) {
		case 3:
			ss = _T("Windows NT 3.51"); break;
		case 4:
			ss = _T("Windows NT 4.0"); break;
		case 5:
			if (in.dwMinorVersion == 0) {
				ss = _T("Windows 2000"); break;
			}
			if (in.dwMinorVersion == 1) {
				ss = _T("Windows XP");
				if ((in.wSuiteMask & VER_SUITE_PERSONAL) == VER_SUITE_PERSONAL) {
					ss += " Home Edition";
				}
				else {
					ss += " Professional Edition";
	
			}


				break;
			}
			if (in.dwMinorVersion == 2) {
				if (in.wProductType == VER_NT_WORKSTATION){
					ss = _T("Windows XP");
					if ((in.wSuiteMask & VER_SUITE_PERSONAL) == VER_SUITE_PERSONAL) {
						ss += " Home Edition";
					}
					else {
						ss += " Professional Edition";
					}
				}else{
					ss = _T("Windows Server 2003");
					if ((in.wSuiteMask & VER_SUITE_DATACENTER)
						== VER_SUITE_DATACENTER)
					{
						ss += " Datacenter Edition";
					}
					else if ((in.wSuiteMask & VER_SUITE_ENTERPRISE)
						== VER_SUITE_ENTERPRISE)
					{
						ss += " Enterprise Edition";
					}
					else if (in.wSuiteMask == VER_SUITE_BLADE)
					{
						ss += " Web Edition";
					}
					else
					{
						ss += " Standard Edition";
					}
				}
				break;
			}
		case 6:
			if (pfnGetProductInfo(
				in.dwMajorVersion,
				in.dwMinorVersion,
				in.wServicePackMajor,
				in.wServicePackMinor,
				&edition)) {
				if (in.dwMinorVersion == 0) {
					if (in.wProductType == VER_NT_WORKSTATION)
					{
						ss = _T("Windows Vista");
						switch (edition)
						{
						case PRODUCT_ENTERPRISE:
						case PRODUCT_ENTERPRISE_N:
						case PRODUCT_ENTERPRISE_E:
						case PRODUCT_ENTERPRISE_SERVER_CORE:
						case PRODUCT_ENTERPRISE_SERVER_IA64:
							ss += " Enterprise Edition";
							break;
						case PRODUCT_ULTIMATE:
						case PRODUCT_ULTIMATE_N:
						case PRODUCT_ULTIMATE_E:
							ss += " Ultimate Edition";
							break;
						case PRODUCT_BUSINESS:
							ss += " Business Edition";
							break;
						case PRODUCT_HOME_PREMIUM:
						case PRODUCT_HOME_PREMIUM_N:
							ss += " Home Premium Edition";
							break;
						case 0x30:
						case 49:
							ss += " Professional Edition";
							break;
						case PRODUCT_HOME_BASIC:
							ss += " Home Basic Edition";
							break;
						case PRODUCT_STARTER:
							ss += " Starter Edition";
							break;
						default:
							ss += " Unknown Edition";
							break;
						}
					}
					else {
						ss = _T("Windows Server 2008");
						if ((in.wSuiteMask & VER_SUITE_DATACENTER)
							== VER_SUITE_DATACENTER)
						{
							ss += " Datacenter Edition";
						}
						else if ((in.wSuiteMask & VER_SUITE_ENTERPRISE)
							== VER_SUITE_ENTERPRISE)
						{
							ss += " Enterprise Edition";
						}
						else if (in.wSuiteMask == VER_SUITE_BLADE)
						{
							ss += " Web Edition";
						}
						else
						{
							ss += " Standard Edition";
						}
						break;
					}

					break;
				}
				else {
					if (in.dwMinorVersion == 1) {
						if (in.wProductType == VER_NT_WORKSTATION) {
							ss = _T("Windows 7");
							switch (edition)
							{
							case PRODUCT_ENTERPRISE:
							case PRODUCT_ENTERPRISE_N:
							case PRODUCT_ENTERPRISE_E:
							case PRODUCT_ENTERPRISE_SERVER_CORE:
							case PRODUCT_ENTERPRISE_SERVER_IA64:
								ss += " Enterprise Edition";
								break;
							case PRODUCT_ULTIMATE:
							case PRODUCT_ULTIMATE_N:
							case PRODUCT_ULTIMATE_E:
								ss += " Ultimate Edition";
								break;
							case PRODUCT_BUSINESS:
								ss += " Business Edition";
								break;
							case PRODUCT_HOME_PREMIUM:
							case PRODUCT_HOME_PREMIUM_N:
								ss += " Home Premium Edition";
								break;
							case 0x30:
							case 49:
								ss += " Professional Edition";
								break;
							case PRODUCT_HOME_BASIC:
								ss += " Home Basic Edition";
								break;
							case PRODUCT_STARTER:
								ss += " Starter Edition";
								break;
							default:
								ss += " Unknown Edition";
								break;
							}
						}
						else {
							ss = _T("Windows Server 2008 R2");
							if ((in.wSuiteMask & VER_SUITE_DATACENTER)
								== VER_SUITE_DATACENTER)
							{
								ss += " Datacenter Edition";
							}
							else if ((in.wSuiteMask & VER_SUITE_ENTERPRISE)
								== VER_SUITE_ENTERPRISE)
							{
								ss += " Enterprise Edition";
							}
							else if (in.wSuiteMask == VER_SUITE_BLADE)
							{
								ss += " Web Edition";
							}
							else
							{
								ss += " Standard Edition";
							}
							break;
						}
						break;
					}
					if (in.dwMinorVersion == 2) {
						if (in.wProductType == VER_NT_WORKSTATION) {
							ss = _T("Windows 8");
							if (edition == 0x48 || edition == 4 || edition == 0x54)
								ss += " Enterprise Edition";
							if (edition == 0x30 || edition == 0x31)
								ss += " Professional Edition";
							if (edition == 0x67)
								ss += " Pro with MC Edition";
							if (edition == 0x36 || edition == 0x65 || edition == 0x62)
								ss += " Home Edition";
							if (edition == PRODUCT_MOBILE_CORE)
								ss += " Mobile Edition";
							break;
						}
						else {
							ss = _T("Windows Server 2012");
							if ((in.wSuiteMask & VER_SUITE_DATACENTER)
								== VER_SUITE_DATACENTER)
							{
								ss += " Datacenter Edition";
							}
							else if ((in.wSuiteMask & VER_SUITE_ENTERPRISE)
								== VER_SUITE_ENTERPRISE)
							{
								ss += " Enterprise Edition";
							}
							else if (in.wSuiteMask == VER_SUITE_BLADE)
							{
								ss += " Web Edition";
							}
							else
							{
								ss += " Standard Edition";
							}
							break;
						}
						break;
					}
					if (in.dwMinorVersion == 3) {
						if (in.wProductType == VER_NT_WORKSTATION) {
							ss = _T("Windows 8.1");
							if (edition == 0x48 || edition == 4 || edition == 0x54)
								ss += " Enterprise Edition";
							if (edition == 0x30 || edition == 0x31)
								ss += " Professional Edition";
							if (edition == 0x67)
								ss += " Pro with MC Edition";
							if (edition == 0x36 || edition == 0x65 || edition == 0x62)
								ss += " Home Edition";
							if (edition == PRODUCT_MOBILE_CORE)
								ss += " Mobile Edition";
							break;
						}
						else {
							ss = _T("Windows Server 2012 R2");
							if ((in.wSuiteMask & VER_SUITE_DATACENTER)
								== VER_SUITE_DATACENTER)
							{
								ss += " Datacenter Edition";
							}
							else if ((in.wSuiteMask & VER_SUITE_ENTERPRISE)
								== VER_SUITE_ENTERPRISE)
							{
								ss += " Enterprise Edition";
							}
							else if (in.wSuiteMask == VER_SUITE_BLADE)
							{
								ss += " Web Edition";
							}
							else
							{
								ss += " Standard Edition";
							}
							break;
						}
						break;
					}
					if (in.dwMinorVersion == 4) {
						// Windows 10 Technical Preview / Insider Preview
						BOOL isPreview = IsPreviewBuild();
						ss = _T("Windows 10");
						
						// レジストリからDisplayVersionを取得（より正確な情報）
						CString regVersion = GetVersionFromRegistry();
						
						// レジストリから取得したバージョン番号を優先
						if (!regVersion.IsEmpty())
						{
							ss += _T(" ");
							ss += regVersion;
						}
						else
						{
							// バージョン番号を取得
							CString versionNum = GetWindows10VersionString(in.dwBuildNumber, FALSE);
							if (!versionNum.IsEmpty())
							{
								ss += _T(" ");
								ss += versionNum;
							}
						}
						
						if (isPreview)
						{
							ss += _T(" Preview");
						}
						
						switch (edition)
						{
						case PRODUCT_ENTERPRISE:
						case PRODUCT_ENTERPRISE_N:
						case PRODUCT_ENTERPRISE_E:
						case PRODUCT_ENTERPRISE_SERVER_CORE:
						case PRODUCT_ENTERPRISE_SERVER_IA64:
							ss += " Enterprise Edition";
							break;
						case 0x30:
						case 49:
						case PRODUCT_PROFESSIONAL_WMC:
							ss += " Professional Edition";
							break;
						case PRODUCT_CORE:
						case PRODUCT_CORE_N:
							ss += " Home Edition";
							break;
						case PRODUCT_CORE_COUNTRYSPECIFIC:
							ss += " Home China Edition";
							break;
						case PRODUCT_CORE_SINGLELANGUAGE:
							ss += " Home Single Language Edition";
							break;
						case PRODUCT_EDUCATION:
						case PRODUCT_EDUCATION_N:
							ss += " Education Edition";
							break;
						case PRODUCT_MOBILE_CORE:
							ss += " Mobile Edition";
							break;
						case PRODUCT_MOBILE_ENTERPRISE:
							ss += " Mobile Enterprise Edition";
							break;
						default:
							ss += " Unknown Edition";
							break;
						}
						break;
					}
				}
			}
		case 10:
			if (pfnGetProductInfo(
				in.dwMajorVersion,
				in.dwMinorVersion,
				in.wServicePackMajor,
				in.wServicePackMinor,
				&edition)) {
				if (in.dwMinorVersion == 0) {
					BOOL isWindows11 = (in.dwBuildNumber >= 22000 && in.dwBuildNumber < 28000);
					BOOL isWindows12 = (in.dwBuildNumber >= 28000);
					BOOL isPreview = IsPreviewBuild();
					BOOL isServer = (in.wProductType != VER_NT_WORKSTATION);
					
					// レジストリからDisplayVersionを取得（より正確な情報）
					CString regVersion = GetVersionFromRegistry();
					
					if (isServer)
					{
						// Windows Server版の判定
						if (in.dwBuildNumber >= 26100)
						{
							ss = _T("Windows Server 2025");
						}
						else if (in.dwBuildNumber >= 20348)
						{
							ss = _T("Windows Server 2022");
						}
						else if (in.dwBuildNumber >= 17763)
						{
							ss = _T("Windows Server 2019");
						}
						else if (in.dwBuildNumber >= 14393)
						{
							ss = _T("Windows Server 2016");
						}
						else
						{
							ss = _T("Windows Server");
						}
						
						// レジストリから取得したバージョン番号を優先
						if (!regVersion.IsEmpty())
						{
							ss += _T(" ");
							ss += regVersion;
						}
						else
						{
							// バージョン番号を取得
							CString versionNum = GetWindows10VersionString(in.dwBuildNumber, FALSE);
							if (!versionNum.IsEmpty())
							{
								ss += _T(" ");
								ss += versionNum;
							}
						}
						
						// プレビュー版の場合
						if (isPreview)
						{
							ss += _T(" Preview");
						}
						
						// サーバーエディション情報
						if ((in.wSuiteMask & VER_SUITE_DATACENTER) == VER_SUITE_DATACENTER)
						{
							ss += " Datacenter Edition";
						}
						else if ((in.wSuiteMask & VER_SUITE_ENTERPRISE) == VER_SUITE_ENTERPRISE)
						{
							ss += " Enterprise Edition";
						}
						else if (in.wSuiteMask == VER_SUITE_BLADE)
						{
							ss += " Web Edition";
						}
						else
						{
							ss += " Standard Edition";
						}
					}
					else
					{
						// クライアント版の判定
						if (isWindows12)
						{
							ss = _T("Windows 12");
							// レジストリから取得したバージョン番号を優先
							if (!regVersion.IsEmpty())
							{
								ss += _T(" ");
								ss += regVersion;
							}
							else
							{
								// バージョン番号（24H2、25H2など）を取得
								CString versionNum = GetWindows12VersionString(in.dwBuildNumber);
								if (!versionNum.IsEmpty())
								{
									ss += _T(" ");
									ss += versionNum;
								}
							}
						}
						else if (isWindows11)
						{
							ss = _T("Windows 11");
							// レジストリから取得したバージョン番号を優先
							if (!regVersion.IsEmpty())
							{
								ss += _T(" ");
								ss += regVersion;
							}
							else
							{
								// バージョン番号（21H2、22H2、23H2、24H2、25H2など）を取得
								CString versionNum = GetWindows10VersionString(in.dwBuildNumber, TRUE);
								if (!versionNum.IsEmpty())
								{
									ss += _T(" ");
									ss += versionNum;
								}
							}
						}
						else
						{
							ss = _T("Windows 10");
							// レジストリから取得したバージョン番号を優先
							if (!regVersion.IsEmpty())
							{
								ss += _T(" ");
								ss += regVersion;
							}
							else
							{
								// バージョン番号（1507、1511、1607、1703、1709、1803、1809、1903、1909、2004、20H2、21H1、21H2、22H2、23H2、24H2、25H2など）を取得
								CString versionNum = GetWindows10VersionString(in.dwBuildNumber, FALSE);
								if (!versionNum.IsEmpty())
								{
									ss += _T(" ");
									ss += versionNum;
								}
							}
						}
						
						// プレビュー版の場合
						if (isPreview)
						{
							ss += _T(" Preview");
						}
						
						// エディション情報
						switch (edition)
						{
						case PRODUCT_ENTERPRISE:
						case PRODUCT_ENTERPRISE_N:
						case PRODUCT_ENTERPRISE_E:
						case PRODUCT_ENTERPRISE_SERVER_CORE:
						case PRODUCT_ENTERPRISE_SERVER_IA64:
							ss += " Enterprise Edition";
							break;
						case 0x30:
						case 49:
							ss += " Professional Edition";
							break;
						case PRODUCT_CORE:
						case PRODUCT_CORE_N:
							ss += " Home Edition";
							break;
						case PRODUCT_CORE_COUNTRYSPECIFIC:
							ss += " Home China Edition";
							break;
						case PRODUCT_CORE_SINGLELANGUAGE:
							ss += " Home Single Language Edition";
							break;
						case PRODUCT_EDUCATION:
						case PRODUCT_EDUCATION_N:
							ss += " Education Edition";
							break;
						case PRODUCT_MOBILE_CORE:
							ss += " Mobile Edition";
							break;
						case PRODUCT_MOBILE_ENTERPRISE:
							ss += " Mobile Enterprise Edition";
							break;
						default:
							ss += " Unknown Edition";
							break;
						}
					}
				}
			}
		}
	}

	::FreeLibrary(hModule);
	/* IsWow64 は 32bit プロセスが 64bit OS 上のときだけ TRUE。
	   本体が x64 だと FALSE になり「32bit」と出るので、動いているプロセスの幅で書く。 */
#if defined(_WIN64)
	const TCHAR* procBits = _T("64bit");
#else
	const TCHAR* procBits = _T("32bit");
#endif
	s.Format(_T("%s %s %s"), ss, in.szCSDVersion, procBits);
	return s;
}

void COSVersion::GetVersionInfo(OSVERSIONINFOEX& in, DWORD& edition, BOOL& bit)
{
	HMODULE	hModule;
	edition = PRODUCT_UNDEFINED;
	ZeroMemory(&in, sizeof(in)); in.dwOSVersionInfoSize = sizeof(OSVERSIONINFOEX); GetVersionExW((OSVERSIONINFO*)&in);
	BOOL(CALLBACK* pfnGetProductInfo)(DWORD dwOSMajorVersion, DWORD dwOSMinorVersion, DWORD dwSpMajorVersion, DWORD dwSpMinorVersion, PDWORD pdwReturnedProductType);
	hModule = ::LoadLibrary(_T("kernel32.dll"));
	(*(FARPROC*)&pfnGetProductInfo) = ::GetProcAddress(hModule, "GetProductInfo");
	if (pfnGetProductInfo) {
		pfnGetProductInfo(
			in.dwMajorVersion,
			in.dwMinorVersion,
			in.wServicePackMajor,
			in.wServicePackMinor,
			&edition);
	}
}