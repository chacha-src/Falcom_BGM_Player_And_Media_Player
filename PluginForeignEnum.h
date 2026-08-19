#pragma once
#include <afx.h>

// exe 配下（plug / plugloop と同じ再帰）の各ディレクトリ内候補 DLL を台帳へ追加。
// Plugins フォルダ限定ではない。1 候補ごとに OggPluginLoadOnOneFileDone() を呼ぶ。
void PluginForeign_EnumInDir(const CString& dirPath);

// 同ディレクトリ内の候補 DLL 数（再帰なし）。CountKpiFiles 用。
// 名前パターンまたは PE エクスポート（winampGetInModule2 / XMPIN_GetInterface / AIMPPluginGetHeader）。
int PluginForeign_CountCandidatesInDir(const CString& dirPath);

// 読み込み進捗 1 件分（oggDlg 側実装）
void OggPluginLoadOnOneFileDone();
