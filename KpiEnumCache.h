#pragma once
#include <tchar.h>

/* %LOCALAPPDATA%\oggYSED\kpicache\ — KPI/外部プラグイン列挙結果のキャッシュ。
   一致すれば LoadLibrary なしで拡張子台帳を復元する。

   重要:
   - 対応拡張子・KPI v2/v5・arch・Winamp 等は「以前のフル列挙」で取得した値を保存する。
     PE エクスポートやファイル名だけでは拡張子は取れない（指紋・候補判定のみ）。
   - プラグイン更新後は Invalidate、または size/mtime 指紋不一致で再列挙する。
     （更新してもキャッシュを残すと拡張子・種別が古いまま＝デグレ） */

/* rootDir = exe ディレクトリ。成功時 kpicnt / kpif / ext / kvar / kpiarch / plugkind を埋める */
BOOL KpiEnumCache_TryApply(LPCTSTR rootDir);

/* 列挙完了後に保存（拡張子が空のエントリは書かない） */
void KpiEnumCache_Save(LPCTSTR rootDir);

/* ダウンロード／再読込／サイレント更新後に呼ぶ */
void KpiEnumCache_Invalidate(void);
