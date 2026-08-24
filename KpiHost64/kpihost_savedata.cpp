#include "kpihost_stdafx.h"
save savedata = {};

const wchar_t* LangPick14(
	const wchar_t* s0, const wchar_t* s1, const wchar_t* s2, const wchar_t* s3,
	const wchar_t* s4, const wchar_t* s5, const wchar_t* s6, const wchar_t* s7,
	const wchar_t* s8, const wchar_t* s9, const wchar_t* s10, const wchar_t* s11,
	const wchar_t* s12, const wchar_t* s13)
{
	int lang = savedata.lang;
	if (lang < 0 || lang > 13)
		lang = 1;
	switch (lang) {
	case 0:  return s0;
	case 2:  return s2;
	case 3:  return s3;
	case 4:  return s4;
	case 5:  return s5;
	case 6:  return s6;
	case 7:  return s7;
	case 8:  return s8;
	case 9:  return s9;
	case 10: return s10;
	case 11: return s11;
	case 12: return s12;
	case 13: return s13;
	default: return s1;
	}
}
