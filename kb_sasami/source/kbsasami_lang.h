#pragma once
/* kbsasami.kpi と kbsasami_host の言語。
   0=ja 1=en 2=fr 3=it 4=es 5=ko 6=zh 7=ar 8=ru 9=de 10=pt 11=nl 12=pl 13=tr
   本体が OGG_UI_LANG を置く。無いときは UI 言語（ogg.cpp の初回判定と同じ）。 */
#include <windows.h>
#include <stdlib.h>

inline int KbsDetectUiLang()
{
	wchar_t buf[8];
	DWORD n = GetEnvironmentVariableW(L"OGG_UI_LANG", buf, 8);
	if (n > 0 && n < 8) {
		int v = _wtoi(buf);
		if (v >= 0 && v <= 13)
			return v;
	}
	switch (PRIMARYLANGID(GetUserDefaultUILanguage())) {
	case LANG_JAPANESE:   return 0;
	case LANG_ENGLISH:    return 1;
	case LANG_FRENCH:     return 2;
	case LANG_ITALIAN:    return 3;
	case LANG_SPANISH:    return 4;
	case LANG_KOREAN:     return 5;
	case LANG_CHINESE:    return 6;
	case LANG_ARABIC:     return 7;
	case LANG_RUSSIAN:    return 8;
	case LANG_GERMAN:     return 9;
	case LANG_PORTUGUESE: return 10;
	case LANG_DUTCH:      return 11;
	case LANG_POLISH:     return 12;
	case LANG_TURKISH:    return 13;
	default:              return 1;
	}
}

inline const wchar_t* KbsPick14(
	const wchar_t* ja, const wchar_t* en, const wchar_t* fr, const wchar_t* it,
	const wchar_t* es, const wchar_t* ko, const wchar_t* zh, const wchar_t* ar,
	const wchar_t* ru, const wchar_t* de, const wchar_t* pt, const wchar_t* nl,
	const wchar_t* pl, const wchar_t* tr)
{
	switch (KbsDetectUiLang()) {
	case 0:  return ja;
	case 2:  return fr;
	case 3:  return it;
	case 4:  return es;
	case 5:  return ko;
	case 6:  return zh;
	case 7:  return ar;
	case 8:  return ru;
	case 9:  return de;
	case 10: return pt;
	case 11: return nl;
	case 12: return pl;
	case 13: return tr;
	default: return en;
	}
}
