#include "kpihost_stdafx.h"

// ホスト起動時は lang=0。本体が最初の PING で savedata.lang を上書きする。
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
	case 0:  return s0;  // ja
	case 2:  return s2;  // fr
	case 3:  return s3;  // it
	case 4:  return s4;  // es
	case 5:  return s5;  // ko
	case 6:  return s6;  // zh
	case 7:  return s7;  // ar
	case 8:  return s8;  // ru
	case 9:  return s9;  // de
	case 10: return s10; // pt
	case 11: return s11; // nl
	case 12: return s12; // pl
	case 13: return s13; // tr
	default: return s1;  // en（範囲外も英語）
	}
}
