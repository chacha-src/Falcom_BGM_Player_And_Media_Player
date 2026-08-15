#pragma once
// 設定・プレイリスト等の .dat を exe 隣の oggYSEDbgm_uni_avx2.dat (zstd) にまとめる。
// 実体は %TEMP% へ必要時展開し、保存時にアーカイブへ戻す。

#ifndef DATARCHIVE_H
#define DATARCHIVE_H

BOOL DatArc_Init(LPCTSTR exeDirWithSlash);
void DatArc_Shutdown();

// ステージングディレクトリ (末尾 \)
LPCTSTR DatArc_StageDir();
void DatArc_Chdir();

// leaf 名 (例: oggYSEDbgmu.dat) → ステージ上のフルパス。読込前に展開する。
CString DatArc_Path(LPCTSTR leaf);

BOOL DatArc_Exists(LPCTSTR leaf);
BOOL DatArc_Commit(LPCTSTR leaf);
BOOL DatArc_Delete(LPCTSTR leaf);
BOOL DatArc_Rename(LPCTSTR fromLeaf, LPCTSTR toLeaf);
BOOL DatArc_FlushAll();

// 連続 Save 時に全アーカイブ再圧縮を1回にまとめる (TRUEで保留、FALSEで保留分を flush)
void DatArc_FlushSuspend(BOOL suspend);

#endif
