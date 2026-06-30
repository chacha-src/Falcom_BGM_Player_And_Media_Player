#pragma once

#include "StdAfx.h"

struct MpM3uImportOptions
{
	int  targetPlaylist;   // savedata.playlistnum 相当。新規時は loadplaylistname の末尾(<新規>)
	BOOL createNew;        // TRUE=新規プレイリスト
	BOOL utf8;             // TRUE=UTF-8強制 / FALSE=自動判定(BOM・内容からUTF-8/ACP)
	BOOL resolveRelative;  // 相対パスをプレイリストファイル基準で解決
	BOOL skipMissing;      // 存在しないファイルをスキップ
	BOOL skipDuplicates;   // 同一 fol をスキップ
};

// 現在の pl->pc[] を M3U/M3U8 へ書き出す
BOOL MpExportPlaylistM3U(const CString& path, BOOL utf8);

// プレイリストファイル(M3U/M3U8/PLS)を読み込み、指定先へ追加する
// 戻り値: 追加した曲数(-1=失敗)
int MpImportPlaylistFile(const CString& path, const MpM3uImportOptions& opt);

// 拡張子がプレイリスト形式か
BOOL MpIsPlaylistExtension(const CString& path);
