#pragma once
#include "CCustomControl.h"
#include "FileTagInfo.h"
#include "oggDlg.h"
#include <atlimage.h>

// WAV / mp3/FLAC 書き出し・タグ編集共用: タグ欄とジャケットD&D

CString ExportTagUi_CoverHint();
BOOL ExportTagUi_IsImagePath(const CString& path);

// pic: サムネ用 CStatic(SS_BITMAP)。hint: ドロップ案内。coverBmp: 所有ビットマップ(呼び出し側が破棄)
void ExportTagUi_SetCover(CStatic& pic, CCustomStatic& hint, CString& coverPath, HBITMAP& coverBmp, const CString& path);
void ExportTagUi_ClearCover(CStatic& pic, CCustomStatic& hint, CString& coverPath, HBITMAP& coverBmp);
BOOL ExportTagUi_OnDropFiles(HDROP hDrop, CStatic& pic, CCustomStatic& hint, CString& coverPath, HBITMAP& coverBmp);

// 埋め込み/サイドカーの既存ジャケをプレビュー表示。coverPath は触らない（未変更＝再埋め込みしない）
BOOL ExportTagUi_ShowExistingCover(CStatic& pic, CCustomStatic& hint, HBITMAP& coverBmp, LPCTSTR mediaPath);

void ExportTagUi_InitFields(bool multi, const playlistdata0& pc,
	CCustomEdit& title, CCustomEdit& artist, CCustomEdit& album,
	CCustomStatic& titleL, CCustomStatic& artistL, CCustomStatic& albumL,
	CCustomStatic& coverL, CStatic& coverPic, CCustomStatic& coverHint,
	CCustomStandardButton& coverClear, CString& coverPath, HBITMAP& coverBmp);

void ExportTagUi_Collect(bool multi, int copyTags,
	CCustomEdit& title, CCustomEdit& artist, CCustomEdit& album,
	const CString& coverPath, WavExportOptions& opts);
