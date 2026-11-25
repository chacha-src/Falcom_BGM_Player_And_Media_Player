【ファイル名】OxzGui.exe
【タイトル名】oxz 書庫ツール
【作　　　者】ちゃちゃ
【バージョン】rel.2026.11.25
【動 作 O S 】Microsoft Windows 11
【コンパイラ】Microsoft Visual Studio 2026 Community
【ﾎｰﾑ  ﾍﾟｰｼﾞ】http://ppp.oohara.jp/

らいら本体の更新、プラグイン、おまけデータなどを配るための書庫「oxz」を
作る・開く・足す・展開するためのツールです。本体は x64 です。

新しく作る oxz は、このツール専用の書庫です。ファイルごとに、公開されている
複数の圧縮を試し、1 バイトでも小さい方を残します。ogg や画像のように
すでに縮んでいるファイルも、さらに縮むならその結果を使います。
変わっていないファイルは、次の保存では圧縮し直さず、そのまま写します。
exe や dll は、プログラム向けの一手間をかけてから圧縮します。
以前の oxz（中身が 7z のもの）も開けます。それを更新するときは、
古い中身をそのまま残し、変わったファイルだけを足します。
7-Zip では、新しく作った oxz は開けません。このツールで開いてください。
らいら本体は、作り直したあとにこの oxz を開きます。作り直す前の本体は、
以前の 7z 形式の oxz をこれまで通り開きます。

言語は設定から選べます。
日本語、英語、フランス語、イタリア語、スペイン語、韓国語、中国語、
アラビア語、ロシア語、ドイツ語、ポルトガル語、オランダ語、ポーランド語、
トルコ語の14言語です。らいら本体の言語設定とは別です。
言語を変えたあとは、もう一度インストールすると、エクスプローラーの
短いメニュー文言もその言語になります。


■ 同梱しておきたいファイル

OxzGui.exe      本体（x64）
OxzShell.dll    右クリック、右ドラッグ、oxz へのドロップ
7za.exe         インストール先へ一緒にコピーします。展開には使いません
readme.txt      この説明

この4つは同じフォルダに置いてください。開く、一覧、展開、テストは、exe に入れた復号器だけで行います。7za.exe や 7z.dll は呼び出しません。WIM は無圧縮、XPRESS、LZX を開きます。LZMS の WIM は開きません。


■ 最初にやること

OxzGui.exe を起動します。
設定の「インストール」を押すと、Program Files\OxzGui へコピーし、
エクスプローラーの右クリックに oxz を足します。管理者の確認が出ます。
インストールしたあとは、次の操作が使えます。

ファイルを右クリック：
　開く（oxz、7z、zip、rar、gz、bz2、xz、zst、tar、tgz、tar.gz、tbz2、tar.bz2、txz、tar.xz、tzst、tar.zst、cab、iso、wim、lzh、lha、arj を一つ選んだとき）
　展開...
　ここに展開
　「名前\」に展開（複数のときは、それぞれフォルダを作って展開）
　アーカイブをテスト
　圧縮...
　「名前.oxz」に圧縮（選んだものを一つにまとめる）
　それぞれ oxz に圧縮（2つ以上選んだとき。一つずつ別の oxz にする）
zip や 7z などのダブルクリック先は変えません。開く、展開、テストは oxz のメニューの中です。
開くと oxz と同じ画面で中を見て、ドロップで追加できます。保存で作れるのは oxz、zip、tar です。7z の新規作成はこの exe には入っていません。開いた zip、7z、rar などの元ファイルは書き換えません。
新しい oxz を開くときは目録だけを読むので、一覧はすぐ出ます。ファイルを開いたりコピーしたときだけ、そのファイルを展開します。
保存と、エディタで変わったあとの更新は、変わっていない圧縮データをそのまま繋ぎ、変わったファイルだけ圧縮し直します。

フォルダの何も無いところで右クリックすると、そのフォルダを oxz にできます。
右ボタンを押したまま別のフォルダへドラッグすると、
ここに解凍、フォルダを作って解凍、ここに oxz を作成、それぞれ oxz をここに作成、が出ます。
ファイルやフォルダを oxz のアイコンの上へドロップすると、
その書庫へ追加または上書きします。
ここに展開、「名前」に展開、「名前.oxz」に圧縮、右ドラッグの解凍や oxz の作成も、
進捗が 100% になるとウィンドウを閉じます。
開く、ダブルクリック、圧縮... で開いた編集画面は閉じません。
失敗したときはメッセージを出して、ウィンドウは開いたままです。

メニューが出ないときは、エクスプローラーを開き直してください。


■ 書庫の画面

開く、新規、ファイル追加、フォルダ追加、削除、名前変更、展開、保存、書庫テスト、関連図、設定
が並びます。書庫テストはファイルごとの成否を出します。関連図は、書庫全体とフォルダの絞め具合、フォルダとコーデックのつながりを描きます。

一覧は今いるフォルダだけを出します。名前の前には、その種類のアイコンが付きます。
exe や dll は、ファイル自身のアイコンです。先頭の .. をダブルクリックするか、
Backspace で上へ戻ります。フォルダをダブルクリックすると中に入ります。
ファイルをダブルクリックするか、右クリックの「開く」で、関連付けられたアプリが開きます。
txt や xml をエディタで保存すると、約 1 秒後に、変わったファイルだけを oxz へ反映します。
設定では圧縮率を 1 から 10 まで選べます。1 は速く、10 は小さくなります。PPMd を使うかどうかもそこで切り替えます。
開いているのが zip や 7z のときは元の書庫は残し、同じ名前の oxz に反映します。
まだ保存していない新規は、最初の保存で保存先を聞いてから反映します。
列見出しをクリックすると、その列で並べ替えます。

一覧の列は、名前、サイズ、大きさ（B / KB / MB / GB / TB）、
格納サイズ、格納量、比率、コーデック、種類、属性、更新日時、作成日時です。
新しい oxz では、ファイルごとに実際の格納サイズと比率、使ったコーデックが出ます。
512KB より大きいファイルは、先頭 512KB を各コーデック同時に試し、一番小さくなった方式で全体を圧縮します。
圧縮と展開は、別々のファイルを同時に進め、大きな bzip2、Zstd、LZMA2、Brotli はコーデックの内側でも分けて同時に処理します。書庫の形式はこれまでのままで、すでにある oxz もそのまま読めます。
PPMd は展開が重いので、8MB を超えるファイルには使いません。それより小さいファイルでも、他の方式より 5% 以上小さいときだけ使います。
埋め込み 7z の中だけのファイルは、一つにまとまっているので比率は出さず、コーデックは 7z です。
まだ保存していないファイルは - です。
フォルダの格納サイズは、中のファイルが全部分かっているとき、その合計です。

作業中は、書庫名、今のファイル名、そのファイルの％と全体の％、
プログレスバー、何個中の何個か、経過時間と残り時間が出ます。
圧縮はファイルごとの進み具合です。変わっていないファイルはすぐ終わります。
展開は短時間で終わります。

Delete で、選んだ項目を消します。消す前に確認します。
名前の編集中に Delete を押しても、項目は消えません。


■ ドラッグとクリップボード

一覧へファイルやフォルダをドロップすると、今開いているフォルダの中へ入ります。
フォルダの行の上へ落とすと、そのフォルダの中へ入ります。
同名は上書き、新しい名前は追加し、
その場で圧縮して保存し、一覧を更新します。保存ボタンは押さなくて済みます。
ウィンドウは開いたままです。
まだ保存先が無いときは、保存ダイアログが一度だけ出ます。
書庫を開いているときや、新規の編集中は、7z や zip や oxz を落としても開き直さず、そのファイルとして oxz に入れます。
何も開いていないときに、対応している書庫を一つだけ落とすと、その書庫を開きます。
RAR は UnRAR のソースを exe に組み込んで開きます。RAR は作りません。
UnRAR source code may be used in any software to handle
RAR archives without limitations free of charge, but cannot be
used to develop RAR (WinRAR) compatible archiver and to
re-create RAR compression algorithm, which is proprietary.
Distribution of modified UnRAR source code in separate form
or as a part of other software is permitted, provided that
full text of this paragraph, starting from "UnRAR source code"
words, is included in license, or in documentation if license
is not available, and in source code comments of resulting package.

一覧で選んだファイルやフォルダをエクスプローラーへドラッグすると、
そこへコピーして展開します。フォルダと中身を両方選んでいるときは、
フォルダだけを渡します。

Ctrl+C のあと、エクスプローラーで Ctrl+V すると、そこへコピーして展開します。
Ctrl+X で切り取ってエクスプローラーへ貼り付けると、そこへ移し、
書庫も保存し直します。
エクスプローラーで Ctrl+C し、このウィンドウで Ctrl+V すると、
ドロップと同じように追加または上書きして保存します。
書庫を開いているときに 7z や zip や oxz を貼り付けると、そのファイルとして追加します。何も開いていないときに一つだけ貼り付けると、その書庫を開きます。

Ctrl+A ですべて選択、F2 で名前変更、F5 で一覧の更新です。
Ctrl+Insert がコピー、Shift+Insert が貼り付け、Shift+Delete が切り取りです。
右クリックメニューにも、コピー、切り取り、貼り付け、すべて選択があります。


■ 更新

起動すると、https://ppp.oohara.jp/download/Oxz.oxz があればその更新時刻を、
無ければ同じ場所の Oxz.zip の更新時刻を、今動いている OxzGui.exe の時刻と比べます。
どちらも UTC です。サーバの方が新しければ、
「アップデートファイルがあります。今すぐ更新しますか？」と出します。

はいを選ぶと、そのファイルを取得して OxzGui.exe、OxzShell.dll、7za.exe を
置き換えます。インストール済みなら、そのあとインストールも行い、
GUI を起動し直します。管理者の確認が一度出ます。
いいえを選ぶと今回は何もせず、次に起動したときまた確認します。

ダウンロードした OxzGui.exe を開いたとき、Program Files の版の方が古ければ
「インストールされている版が古いので、更新します。」と出て、
インストール先を今開いた版に更新します。管理者の確認が出ます。
すでにインストール先の OxzGui.exe を開いているときは、この確認は出ません。

エクスプローラーから圧縮や展開だけを頼んだ起動では、サーバの確認は出しません。


■ らいら本体との関係

本体の更新、プラグイン、arcdata、おまけ画像は、
.oxz があればそれを、無ければ .7z、それも無ければ .zip を取りにいきます。
書庫の中の exe や dll は、本体が展開するときに元へ戻します。
このツールで作った oxz を、そのまま配布に使えます。


English
===================================================================

[File] OxzGui.exe
[Title] oxz archive tool
[Author] Chacha
[Version] rel.2026.11.23
[OS] Microsoft Windows 11
[Compiler] Microsoft Visual Studio 2026 Community
[Homepage] http://ppp.oohara.jp/

This tool creates, opens, updates, and extracts oxz archives.
The player uses them for updates, plugins, and bonus data. The exe is x64.

A newly written oxz is this tool's own package. Each file is tried with
several published compressors, and the smaller result is kept, even for
files that are already compressed. The next save copies bytes that did
not change. Exe and dll files get an extra preparation step before that.
Older oxz files, which are 7z archives, still open. Updating one keeps
that 7z as a base and adds only the files that changed. 7-Zip does not
open a newly written oxz. The player opens the new package after the
player itself is rebuilt. Until then, the player still opens the older
7z oxz files.

The language is chosen in Settings, separate from the player.
Japanese, English, French, Italian, Spanish, Korean, Chinese, Arabic,
Russian, German, Portuguese, Dutch, Polish, and Turkish.
After changing language, install again so the short Explorer menu matches.
The classic right-click menu follows the setting on the next click.


Files to keep together

OxzGui.exe      the tool (x64)
OxzShell.dll    right-click, right-drag, and drop onto an oxz file
7za.exe         copied by install; opening and extracting do not run it
readme.txt      this file

All four belong in the same folder. Open, list, extract, and test run the
decoders compiled into the exe. The program does not call 7za.exe or 7z.dll.
WIM opens uncompressed, XPRESS, and LZX. An LZMS WIM does not open.


First launch

Start OxzGui.exe.
Settings, Install copies the tool to Program Files\OxzGui and adds oxz to
the Explorer menu. Windows asks for administrator approval.

After that, a right-click can open one archive, extract (choose a folder,
extract here, or extract each archive into its own folder), test the archive, or
compress (open the editor, compress the selection into one oxz, or, when two or
more items are selected, compress each item into its own oxz).
Open, extract, and test also appear for 7z, zip, rar, gz, bz2, xz, zst,
tar, tgz, tar.gz, tbz2, tar.bz2, txz, tar.xz, tzst, tar.zst, cab, iso, wim,
lzh, lha, and arj. Double-click on those types stays with the program
that already opens them. Open shows the archive in this window, and a drop
adds files. Save writes oxz, zip, or tar. Creating a new 7z is not in this
exe. An opened zip, 7z, or rar is left unchanged.
Opening a new oxz reads only its directory, so the list appears at once.
A file is decompressed when you open or copy that file. Save, and an
update after an editor change, keeps unchanged compressed blocks and
recompresses only the files that changed.
A right-click on empty folder space can compress that folder.
Right-drag onto a folder offers extract here, extract into a new folder,
create one oxz there, and, for several items, create an oxz there for each.
Dropping files onto an oxz icon adds or overwrites inside that archive.
Extract here, extract into a named folder, compress to name.oxz, and the
right-drag extract or create commands close the window when the bar
reaches 100%. Open, a double-click, and Compress... leave the window open.
On failure it stays open and shows the error.

If the menu is missing, reopen Explorer.


The window

Open, New, Add files, Add folder, Delete, Rename, Extract, Save, Test,
Map, and Settings. Test lists each file as ok or failed. Map draws the
archive and folder ratios, and how folders connect to codecs.

The list shows only the folder you are in. Each name has its associated icon.
An exe or dll uses the icon stored in that file. Double-click .. or press
Backspace to go up. Double-click a folder to open it. Double-click a file,
or choose Open on the list menu, to start the associated program. Saving that
file in an editor updates the oxz about a second later, recompressing only
what changed. Settings chooses a compression level from 1 to 10: 1 is fast
and 10 is smaller. PPMd can be turned off there too. A zip or 7z stays as it was, and the edit is written to an
oxz of the same name. A new unsaved list
asks for the oxz path on the first save. Click a column title to sort by that
column.

Columns are name, size, a readable size (B / KB / MB / GB / TB),
stored size, a readable packed size, ratio, codec, type, attributes,
modified, and created.
A new oxz shows each file's real packed size, ratio, and codec.
A file larger than 512KB is sampled on its first 512KB. Every codec and
filter is tried at the same time, and the smallest one is used for the
whole file. Compression and extraction also run separate files at the
same time, and a large bzip2, Zstd, LZMA2, or Brotli member is split
inside the codec. The archive format stays as it is, and an existing
oxz still reads out to the same bytes. PPMd is slow to extract, so it is not used for a file over
8MB. On a smaller file it is used only when it is at least 5% smaller
than the other codecs.
A file that exists only inside an embedded 7z has no separate ratio, and
its codec is shown as 7z. A file that has not been saved yet shows a dash.
A folder's stored size is the sum of the files inside it when every one of
them is known.

While working, the window shows the archive name, the current file,
that file's percent and the overall percent, a progress bar, how many
items are done, and the elapsed and remaining time.
Compress reports each file. Files that did not change finish immediately.
Extract stays short.

Delete removes the selection after a confirm. Delete during a rename edit
does not remove the item.


Drag and clipboard

Drop files or folders on the list and they go into the folder that is
open. Dropping onto a folder row puts them inside that folder.
The same name is overwritten and a new name is added.
The archive is compressed and saved, and the list refreshes.
The window stays open. If there is no save path yet, the save dialog
appears once. While an archive is open, or a new list is being edited,
dropping a 7z, zip, or oxz adds that file into the oxz. It does not
switch to opening the dropped archive. Dropping one oxz, 7z, zip, gzip,
or another supported archive opens it only when nothing is open yet.
RAR is opened with the UnRAR sources compiled into the exe. This program
does not create RAR archives.
UnRAR source code may be used in any software to handle
RAR archives without limitations free of charge, but cannot be
used to develop RAR (WinRAR) compatible archiver and to
re-create RAR compression algorithm, which is proprietary.
Distribution of modified UnRAR source code in separate form
or as a part of other software is permitted, provided that
full text of this paragraph, starting from "UnRAR source code"
words, is included in license, or in documentation if license
is not available, and in source code comments of resulting package.

Drag a selection from the list to Explorer to copy those files out.
If a folder and its children are both selected, only the folder is sent.

Ctrl+C, then Ctrl+V in Explorer, extracts a copy there.
Ctrl+X, then paste in Explorer, moves the files out and saves the archive.
Ctrl+C in Explorer, then Ctrl+V in this window, adds or overwrites and
saves, the same as a drop. Pasting one archive while another is open adds that file. Pasting one archive when nothing is open opens it.

Ctrl+A selects all. F2 renames. F5 refreshes the list.
Ctrl+Insert copies, Shift+Insert pastes, Shift+Delete cuts.
The right-click menu has Copy, Cut, Paste, and Select all.


Updates

On startup the tool compares UTC times. If
https://ppp.oohara.jp/download/Oxz.oxz exists, that file's time is used.
Otherwise Oxz.zip in the same folder is used. The other time is the
OxzGui.exe that is running. When the server file is newer, the tool asks
whether to update now.

Yes downloads it and replaces OxzGui.exe, OxzShell.dll, and 7za.exe.
If the tool is installed, install runs as well, then the window starts
again. Administrator approval appears once.
No does nothing this time and asks again on the next launch.

Opening a downloaded OxzGui.exe, when Program Files still has an older
copy, says the installed copy is older and updates that install.
Administrator approval appears. Opening the exe that is already in
Program Files does not show this.

A launch that only compresses or extracts from Explorer does not ask
about the server file.


The player

Player updates, plugins, arcdata, and bonus images prefer .oxz, then .7z,
then .zip. The player restores exe and dll while extracting.
An oxz made with this tool can be published as-is.
