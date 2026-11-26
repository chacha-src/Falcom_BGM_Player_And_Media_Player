OxzCore.lib と OxzCore.h
=======================

x64 の静的ライブラリです。新しい oxz（先頭が OXZPKG）を作り、開き、展開し、
変わったファイルだけを更新します。分割（.o01）と自己解凍 exe は、このライブラリの
仕事ではありません。それらは OxzGui が、できた一つの oxz を切って付けます。


■ 置く場所と、一緒にリンクするもの

ヘッダ    OxzCore\OxzCore.h
ライブラリ OxzCore\OxzCore.lib   Release | x64

ライブラリは /MT（静的ランタイム）、/O2、/arch:AVX2、C++17 で作ってあります。
呼び出す側も x64 で、ランタイムは /MT にしてください。/MD と混ぜるとリンクできません。

OxzCore.lib のほかに、同じランタイムで作った次の静的ライブラリが要ります。

  libzstd_static.lib
  libFLAC_static_avx2_2026.lib
  libogg_static_avx2_2026.lib
  libz-static.lib

このリポジトリでは x64lib にあります。さらに、OxzGui やゲーム本体と共有している
次の C / C++ ソース（フィルタと LZMA2 / PPMd デコード部）を一緒にコンパイルするかリンクします。
（PPMd デコーダを使うため、コンパイル時に Z7_PPMD_SUPPORT と _7ZIP_ST を定義します。）

  OxzFilt.cpp
  lzma_sdk\LzmaDec.c
  lzma_sdk\Lzma2Dec.c
  lzma_sdk\Ppmd7.c
  lzma_sdk\Ppmd7Dec.c
  lzma_sdk\CpuArch.c

インクルードは OxzCore.h だけで足ります。コーデックのヘッダを、呼び出す側がインクルードする必要はありません。


■ 初期設定

関数は C リンケージです。パスは wchar_t の絶対パスでも相対パスでも受け取れます。
戻り値は OXZ_CORE_OK (0)、OXZ_CORE_ERR (2)、中身が空のときの OXZ_CORE_EMPTY (3) です。

  OxzCoreSetLevel(10);     /* 1 は速い。10 は小さい。未設定時は 10 */
  OxzCoreSetPpmd(1);       /* 0 で PPMd を使わない */
  OxzCoreSetThreads(0);    /* 0 は自動。1 から 12 */

レベル 4 以上は、先に軽い設定で候補を試し、有力なものだけ本番の設定でやり直します。
256KB 以下はファイル全体で方式を決めます。それより大きいファイルは、等間隔の
8 箇所、合計 128KB で決めます。PPMd は 8MB を超えるファイルには使いません。


■ 新規に書く

作業フォルダの中身が、そのまま書庫の中の相対パスになります。

  unsigned long long bytes = 0;
  int rc = OxzCoreSave(L"C:\\work", L"C:\\out\\data.oxz", &bytes, OnProg, NULL);
  if (rc == OXZ_CORE_EMPTY) { /* フォルダにファイルが無い */ }
  if (rc != OXZ_CORE_OK) { /* 失敗。中途半端な .oxz は残さない */ }

保存は data.oxz.building に書いてから、成功したときだけ data.oxz へ移します。
OnProg は次の形です。展開中は別スレッドから呼ばれることがあるので、
画面を触るときは自分で直列化してください。

  void OnProg(const wchar_t* name, unsigned index, unsigned total, int pct, void* user);


■ 開く

  if (!OxzCoreIsPkg(path)) { /* OXZPKG ではない。古い 7z 形式の oxz は 0 */ }

  OxzCoreItem* items = NULL;
  int count = 0;
  uint64_t baseOff = 0, baseSize = 0;
  if (OxzCoreReadDir(path, &items, &count, &baseOff, &baseSize) != OXZ_CORE_OK) { /* 失敗 */ }
  for (int i = 0; i < count; ++i) {
    /* items[i].name は相対パス。isDir、tomb（削除印）、unpacked、packed、method */
  }
  OxzCoreFreeItems(items, count);

baseSize が 0 より大きいときは、先頭に古い 7z 形式の本体が付いています。
OxzCoreReadDir はその本体を展開しません。一覧だけです。


■ 展開する

  int rc = OxzCoreExtract(path, L"C:\\out", BaseFn, NULL, OnProg, NULL);

純然たる OXZPKG（baseSize が 0）なら、BaseFn は NULL で構いません。
古い本体が付いている書庫では、BaseFn が必須です。ライブラリは本体だけを
一時ファイルへ写し、BaseFn に渡します。BaseFn はその 7z を destDir へ展開し、
成功で 0、失敗で 0 以外を返します。一時ファイルの削除はライブラリが行います。

  int BaseFn(const wchar_t* sevenPath, const wchar_t* destDir, void* user);


■ 更新する

一度展開した作業フォルダを覚えてから、ディスク上の変更を保存します。
変わっていない圧縮データはコピーし、変わったファイルだけ圧縮し直します。

  OxzCoreRemember(L"C:\\work", path);          /* 展開直後のフォルダと、元の oxz */
  OxzCoreDrop(L"old\\gone.dat");               /* 一覧から外す。フォルダなら中も */
  OxzCoreRename(L"a.txt", L"b.txt");           /* 圧縮データはそのまま */
  OxzCoreMaterialize(L"a.txt", L"C:\\tmp\\a.txt"); /* 覚えている 1 ファイルだけ展開 */
  OxzCoreSave(L"C:\\work", path, &bytes, OnProg, NULL);

Remember していないフォルダを Save すると、全部を新規として圧縮します。
OxzCoreForget は、覚えた一覧を捨てます。プロセスの中で書庫を切り替えるときに呼びます。

OxzCoreList は、Remember したあとの、削除していない項目です。
OxzCoreReadOverlay は、overlay 番号の圧縮ブロックを展開したバイトを malloc します。
使い終わったら OxzCoreFree で返します。


■ しないこと

このライブラリは zip、tar、gz、7z、rar を読み書きしません。
ボリューム（.o01）の分割と、自己解凍 exe の連結もしません。
できた oxz を切る、exe を先頭に足す、という処理は OxzGui 側です。


------------------------------------------------------------

OxzCore.lib and OxzCore.h
=========================

This is an x64 static library. It creates, opens, extracts, and updates a
new oxz archive (the file starts with OXZPKG). Splitting into .o01 volumes
and building a self-extracting exe are not this library's job. OxzGui cuts
and wraps the single oxz after the library has written it.


Files and link line

Header     OxzCore\OxzCore.h
Library    OxzCore\OxzCore.lib    Release | x64

The library is built with /MT, /O2, /arch:AVX2, and C++17. The caller must
be x64 and must also use /MT. Mixing /MD with this library does not link.

Link these static libraries, built with the same runtime, beside OxzCore.lib:

  libzstd_static.lib
  libFLAC_static_avx2_2026.lib
  libogg_static_avx2_2026.lib
  libz-static.lib

In this repository they live in x64lib. Also compile or link the shared filter
and LZMA2 / PPMd decoder sources (with Z7_PPMD_SUPPORT and _7ZIP_ST defined):

  OxzFilt.cpp
  lzma_sdk\LzmaDec.c
  lzma_sdk\Lzma2Dec.c
  lzma_sdk\Ppmd7.c
  lzma_sdk\Ppmd7Dec.c
  lzma_sdk\CpuArch.c

The caller includes only OxzCore.h. Codec headers are not part of the public interface.


Setup

The functions use C linkage. Paths are wchar_t, absolute or relative.
Return values are OXZ_CORE_OK (0), OXZ_CORE_ERR (2), and OXZ_CORE_EMPTY (3)
when a new archive would contain no files.

  OxzCoreSetLevel(10);     /* 1 is fast, 10 is small. Default is 10. */
  OxzCoreSetPpmd(1);       /* 0 disables PPMd. */
  OxzCoreSetThreads(0);    /* 0 is automatic. The range is 1 to 12. */

At level 4 and above the library scouts every codec with a fast setting,
then re-encodes only the likely winners at the real setting. A file of
256KB or less is judged whole. A larger file is judged from eight evenly
spaced samples, 128KB in total. PPMd is not used on a file over 8MB.


Create

The directory tree becomes the paths inside the archive.

  unsigned long long bytes = 0;
  int rc = OxzCoreSave(L"C:\\work", L"C:\\out\\data.oxz", &bytes, OnProg, NULL);
  if (rc == OXZ_CORE_EMPTY) { /* the directory has no files */ }
  if (rc != OXZ_CORE_OK) { /* failed. A partial .oxz is not left behind. */ }

Save writes data.oxz.building and replaces data.oxz only after success.
OnProg has this shape. During extract it may run on a worker thread, so
marshal any UI work yourself.

  void OnProg(const wchar_t* name, unsigned index, unsigned total, int pct, void* user);


Open

  if (!OxzCoreIsPkg(path)) { /* not OXZPKG. An older 7z-bodied oxz returns 0. */ }

  OxzCoreItem* items = NULL;
  int count = 0;
  uint64_t baseOff = 0, baseSize = 0;
  if (OxzCoreReadDir(path, &items, &count, &baseOff, &baseSize) != OXZ_CORE_OK) { /* failed */ }
  for (int i = 0; i < count; ++i) {
    /* items[i].name is the relative path. isDir, tomb (deleted), unpacked, packed, method */
  }
  OxzCoreFreeItems(items, count);

When baseSize is greater than 0, an older 7z body is still attached.
OxzCoreReadDir does not extract that body. It only lists the archive.


Extract

  int rc = OxzCoreExtract(path, L"C:\\out", BaseFn, NULL, OnProg, NULL);

For a pure OXZPKG (baseSize is 0), BaseFn may be NULL.
An archive that still carries the old body requires BaseFn. The library
copies that body to a temporary file and passes it to BaseFn. BaseFn
extracts that 7z into destDir and returns 0 on success, nonzero on failure.
The library deletes the temporary file.

  int BaseFn(const wchar_t* sevenPath, const wchar_t* destDir, void* user);


Update

Remember the folder just after extract, then save the edits on disk.
Unchanged compressed blocks are copied. Only changed files are compressed again.

  OxzCoreRemember(L"C:\\work", path);          /* the extracted folder and the source oxz */
  OxzCoreDrop(L"old\\gone.dat");               /* drop one entry. A directory drops its children. */
  OxzCoreRename(L"a.txt", L"b.txt");           /* the compressed bytes stay as they are */
  OxzCoreMaterialize(L"a.txt", L"C:\\tmp\\a.txt"); /* extract one remembered file */
  OxzCoreSave(L"C:\\work", path, &bytes, OnProg, NULL);

Save on a folder that was not remembered compresses everything as a new archive.
OxzCoreForget discards the remembered list. Call it when the process switches archives.

OxzCoreList returns the remembered entries that have not been dropped.
OxzCoreReadOverlay mallocs the decoded bytes of one overlay block.
Return that pointer with OxzCoreFree.


Out of scope

This library does not read or write zip, tar, gz, 7z, or rar.
It does not split a file into .o01 volumes and it does not prepend a
self-extracting exe. OxzGui does both of those after a single oxz exists.
