#pragma once
// キー / Camelot 互換ユーティリティ（AnalyzeMusicKey 結果の永続・相性提案）

// root: 0=C .. 11=B。minor: 0=major 1=minor。戻り: 1..24 (1A=1..12A=12, 1B=13..12B=24)。失敗0
int MpKeyToCamelot(int root, int minor);
void MpCamelotToKey(int camelot, int* root, int* minor);
// 隣接: out[0..n) に Camelot 番号。最大6
int MpCamelotNeighbors(int camelot, int* out, int outMax);
// KeyCodeAll 風文字列から root/minor を推定。成功で TRUE
BOOL MpKeyParseFromDisplay(LPCTSTR keyCode, int* root, int* minor);
CString MpCamelotLabel(int camelot); // "8A" 等
CString MpKeyDisplayName(int root, int minor);

// 現在の KeyCodeAll を読んで savedata + SongParams へ保存
void MpKeyCaptureFromLiveAnalysis();
// 現PLから相性曲を列挙。outPaths/outNames は呼び出し側配列。戻り件数
int MpKeyFindCompatibleInPlaylist(int maxOut, CString* outNames, int* outRows);
