chachakotorinのgithubから移転です。

# oggYSEDbgm メディアプレイヤー「らいら」
日本ファルコム (Nihon Falcom) BGMプレイヤー / 高機能メディアプレイヤー

**対応OS:** Windows 11 以降

## 概要
このソフトは、イースシリーズや軌跡シリーズなどの日本ファルコム作品のBGMファイル（Ogg, WAV, mp3等）を、**ゲーム中と同様にループ付きで再生**できるプレイヤーです。
また、DirectShowを使用した動画再生や、ハイレゾ音源（DSD/FLAC）の再生にも対応しています。

少し画面がグラフィカルになりました。
多言語化しました。
日本語"Japanese"、英語"English"、フランス語"Français"、イタリア語"Italiano"、スペイン語"Español"、韓国語"한국어"、中国語"中文"、アラビア語"العربية"、ロシア語"Русский"、ドイツ語"Deutsch"、ポルトガル語"Português"、オランダ語"Nederlands"、ポーランド語"Polski"、トルコ語"Türkçe"


### 環境モデル・イコライザー機能
イコライザー機能が追加され100種類の環境モデルや15バンドのイコライザーも実装されました。
イコライザープリセットも100個用意されています、
環境モデルで音割れ防止のため、楽曲が既に大きいボリュームの場合、ダイナミックコンプレッサーにより音割れしないよう処理が施されております。

グローバルパラメータとして、鮮明さ、低音域高音域バランス、音の密度、音の立体感、リバーブ、コーラス、ディレイも用意してあります。
これらは環境モデルとは別で動作します。
リバーブ／コーラス／ディレイは、スライダー後半でパンリバーブ、コーラスディストーション、マルチディレイ側にも振り分けられます。

また環境モデルのかかり方の度合いの変更できるため、かなり自由度が高くなっています。

### ピアノロール機能
108鍵盤の簡易ピアノロールが実装されました。右クリックメニューから操作でき、検出パラメータの微調整や、再生中のコード・メロディ表示にも対応しています。

### 波形・周波数アナライザー
Ozone風のリアルタイムアナライザーを追加しました。

- **上部:** 多チャンネルPCM波形（横スクロール）と L/R レベルメーター（RMS＋RMSピークホールド）
- **下部:** 対数軸の周波数特性（複数の表示モード、ピークホールド）
- **表示モード:** Ozone / Cubase Frequency / Voxengo SPAN / Ableton Spectrum / FabFilter Pro-Q / バー / 線のみ（右クリック）
- **波形速度:** x0.25〜x2.0（右クリック）
- **レイアウト:** 重ね描き / 上下分割 / 左右分割 / 2x2 / 2x4（右クリックメニュー）
- **EQオーバーレイ:** 既存15バンドEQの帯域とゲイン曲線をスペクトラム上に表示
- **マウス読取:** ホバーで周波数・dB・チャンネル（分割時はカーソル下のパネル）を表示
- **操作:** フリーズ（F） / ピークホールド（P） / EQ表示（E） / ピークリセット（Space・ダブルクリック）
- **描画:** 解析ワーカースレッド＋UI自由走行（ピアノロールと同様）。アクリル（ぼかし）モードにも対応

メディアプレイヤー画面モードの再生中アイコン（♪点滅2コマ）と選択♡の描画順も整備しました。

### メディアプレイヤー画面モード搭載
普通のメディアプレイヤーのようなモード「らいら」を搭載。起動時にファルコムBGM画面との切り替えもできます。

### テンポ・ピッチ変更
再生中のテンポとピッチを、それぞれ独立して変えられます。

### 歌詞表示
.lrc形式の歌詞表示に対応しています。ネットからの歌詞取得もできます。

### ジャケット・プレイリスト
アルバムジャケットの表示と、プレイリスト機能（m3u / m3u8 / pls / xspf の取り込みなど）があります。曲ごとの音量やEQなどの設定も覚えます。
「曲ごとに設定保存」のチェックを入れると、★付きの曲は保存済みパラメータを読み直して反映します。チェックを外すときは、その時点の設定をその曲へ保存してから無効化します。

### アクリル（ぼかし）UI
Windows 11のアクリル風ぼかし表示に対応しています。

### PCMアップスケール
サンプリングレートやビット深度のアップスケール、マルチチャンネル出力に対応しています。

### 演奏プロンプト
再生中にピッチやテンポ、エフェクトなどを時間指定で動かすプロンプト機能があります。テキスト編集のほか、曲を読みながらの解析・履歴の保存／読込、雰囲気モードにも対応しています。

### コマンドロール
プロンプトを時間軸のロール上で編集・配置できます（複数レーン）。メディアプレイヤー側から開けます。メイン画面に追随するロックや表示倍率の保存にも対応しています。

### 再生詳細
ギャップレス、ReplayGain、Mid/Side、相関メーター、書き出しリミッター、ループ区間、ループ境界フェード、キュー、タグ書き込み、簡易波形プレビューなどをまとめた設定です。メディアプレイヤー画面の「詳細」ボタン、またはプレイリストの右クリックから開けます。
タグ書き込みは **MP3 / FLAC / WAV / M4A / Ogg Vorbis** に対応しています（暗号化FLACやOpus書き込みは非対応）。

### ファイル情報
プレイリストやメディアプレイヤーの右クリック「ファイル情報」から、曲の表示名・アーティスト・アルバム・パス、タグ、ループなどを確認・編集できます。

- タグ→プレイリスト反映、タグ再読込、タグ書き込み（上記フォーマット）
- パス参照／Explorerで選択表示／パスコピー／ファイル名コピー
- ファイルの有無・拡張子・サイズ表示、曲ごと設定（★）の表示と削除
- 再生詳細へのジャンプ、ループの編集（OKでプレイリストへ保存）
- 複数選択時はアーティスト／アルバムの一括編集

### タグ編集
プレイリストの右クリック「タグ編集」から、タイトル／アーティスト／アルバムに加え、年・トラック・ジャンル・コメントやジャケットを編集してファイルへ書き込めます。複数選択時は空欄の項目は変えず、入力した項目だけ一括適用します。

### 音声書き出し（WAV / mp3 / FLAC）
プレイリストの右クリック「音声書き出し」から、選択曲を **WAV / mp3 / FLAC** に書き出せます（タブで形式切替）。いったん PCM（WAV経路）へレンダリングしたあと、必要ならエンコードします。

- **ループ回数**、**フェードアウト**、**先頭無音を揃える**（長い先頭無音はカット、短い場合は無音を足して指定秒に揃える）
- **サンプリング**（ソースのまま／44.1〜192 kHz）、KPI曲向けの**長さ(秒)**
- **タグとジャケットをコピー**、タイトル／アーティスト／アルバム／ジャケットの指定
- **プロンプト実行を適用**（書き出しPCMに演奏プロンプトを反映）
- **クロスフェード**（複数選択時）: 曲を1ファイルに順次連結し、曲間を指定秒で等パワー交差。コンテキストの「クロスフェード書き出し」からも起動可
- **ミックス**（複数選択時）: 同時に重ねる曲数と曲ごとの音量割合(%)を指定して1ファイルへ。クロスフェードと併用すると、終わった枠へ次曲を指定秒で補充投入

従来の WAV 書き出しと同様、2GB超は RF64 に対応しています。

### マイクミックス（再生中WAVへ）
「WAVへ保存」がONのとき、再生PCMにマイク入力をミックスして書き込めます。マイク端末はレンダリング設定（CRender）側で選び、ミックス量は0〜200%です。メディアプレイヤーのプレイリスト右クリックからもON/OFFできます。

### デバイス録音
メディアプレイヤー下段の「録音」から、再生端末のループバックを **WAV / mp3 / FLAC** に録音できます。端末・形式・品質・保存先を指定でき、マイクの同時ミックスにも対応しています。mp3 / FLAC はいったんWAV経由で変換します。

### 画面キャプチャ
同じく下段の「キャプチャ」から、画面を **MP4（H.264 + AAC）** で録画できます。

- **モード:** プライマリ画面 / 全モニタ（仮想デスクトップ） / ウィンドウ合成
- **ウィンドウ合成:** 複数ウィンドウをレイヤとして配置。プレビュー上でドラッグ移動・四隅での拡大縮小、Z順の入れ替え
- **システム音**（ループバック）と**マイク**の有無、FPS、出力解像度
- **MPの曲を載せる:** 開いているメディアプレイヤー画面を合成に含め、配置・サイズを調整可能
- プレビューは録画中も更新されます（枠やHUDは録画ファイルには入りません）

![ファルコムプレイヤー画面](https://ppp.oohara.jp/img/ysedplay2_git7.png)

![メディアプレイヤー画面](https://ppp.oohara.jp/img/mp3.png)

![メディアプレイヤー画面](https://ppp.oohara.jp/img/mpe3.png)

![メディアプレイヤー画面](https://ppp.oohara.jp/img/rec.png)

## 対応ゲームタイトル
以下のゲームのBGMループ再生に対応しています。

### イース (Ys) シリーズ
- イース6 ナプシュテムの匣
- イース フェルガナの誓い
- イース・オリジン
- Ys I&II Chronicles / 完全版
- Steam版 Ys セルセタの樹海
- Steam版 Ys VIII (Ys8)
- Steam版 Ys IX (Ys9)
- Steam版 Ys X (Ys10)

### 軌跡 (Trails) シリーズ
- 英雄伝説6 空の軌跡 FC / SC / The 3rd (Steam版 The 1st含む)
- 英雄伝説 零の軌跡
- 英雄伝説 碧の軌跡
- Steam版 閃の軌跡 I / II / III / IV
- Steam版 創の軌跡
- Steam版 黎の軌跡 II
- Steam版 界の軌跡

### その他のファルコム作品 / 他社作品
- Zwei!! (CD版はADPCM、DVD版WAVはPCM処理)
- Zwei II
- XANADU NEXT
- アークトゥルス
- ぐるみん (※データ頭のRIFF検索によるADPCM対応)
- ソーサリアン オリジナル
- ダイナソア リザレクション
- ブランディッシュ4 眠れる神の塔
- **ガガーブトリロジー三部作**
  - 白き魔女
  - 朱紅い雫
  - 海の檻歌
- 西風の狂詩曲 (ラプソディー)
- 月影のデスティニー (mp3)
- 幻想三国志 1 / 2 (mp3)

## 主な機能

### 1. ゲームBGM再生・変換
- **ループ再生:** ゲーム内と同じように自然にループ再生します。
- **フォーマット変換:** Ogg, mp3などのゲーム内BGMをWAVファイルに変換可能です。Ver0.4b以降はフェードアウト機能にも対応。
- **フォルダ設定:** インストールされているゲームフォルダを指定して読み込みます（初期設定はデフォルトインストールフォルダ）。

### 2. 一般オーディオ再生
以下のフォーマットの再生に対応しています。
- mp3, m4a, aac, alac, flac, tta, ape
- DSD (dsf, dff)
- OggOpus (48k)
- Kb Media Playerの旧kpiプラグインと新kpiの一部に対応 (Pluginsフォルダに入れて使用。64bit版はKpiHost64経由)
  - kpi一覧では拡張子テキストで絞り込みできます（入力すると即座に反映。チェック状態は曲単位で保持）

### 3. 動画再生 (DirectShow)
avi, mpgなどのDirectShow対応動画を再生可能です。Windows Vista以降ではEVRを使用し、高画質で再生します。映像・音声・字幕ストリームの切り替えにも対応しています。
※ avi等の再生にはコーデックパック（K-Lite Codec Pack等）が必要です。
[解説サイト: ppp.oohara.jp/k-lite.html](http://ppp.oohara.jp/k-lite.html)

#### 動画再生中の操作・ショートカット

| 操作 | 機能 |
| :--- | :--- |
| **右クリック** | 画面サイズ変更 (1倍 / 1.5倍 / 2倍) |
| **ダブルクリック** | フルスクリーン切り替え |
| **ホイール / ドラッグ** | ウィンドウサイズ変更 |
| **C キー** | 一時停止 |
| **↑ / ↓ キー** | 音量調整 |
| **← / → キー** | シーク (早送り/巻き戻し) |

### 補足
- ランダム再生、連続再生、ループ回数の指定、A-Bリピート
- ファイルのドラッグ＆ドロップ追加
- mp3やDirectShow再生の途中位置の保存
- 音声書き出し（WAV / mp3 / FLAC。ループ／フェード／先頭無音揃え／クロスフェード／同時ミックス／サンプリング指定）
- WAV書き出し時の2GB超対応（RF64）
- マイクミックス（WAVへ保存時）、デバイス録音（ループバック→WAV/mp3/FLAC）、画面キャプチャ（MP4）
- ピアノロールの検出パラメータ調整ダイアログ（多数スライダー）
- プレイリストからアナライザー／ピアノロールを直接開く
- 並べ替え、他プレイリストへの移動・コピー、選択曲のジャケ再取得
- 欠損ファイル確認（パス修正・適用・インライン編集）と欠損マークの再スキャン
- プレイリスト取り込み（m3u / m3u8 / pls / xspf。UTF-8、相対パス、欠損／重複スキップなど）
- レンダリング設定（デバイス、バッファ、ビット深度、フォント、ファイル関連付けなど）
- タスクバーのジャンプリスト（再生／停止、EQ、ジャケット、プレイリストなど）
- 起動時・定期の更新チェック
- Windowsミキサーでアプリがミュートされているときの警告
- メディアプレイヤー側の最小化連動やツールチップ表示など
- メイン画面を動かすと関連ウィンドウが追随
- 曲ごと設定のON/OFF時の復元・保存、ファイル情報／タグ編集画面の操作
- タグ書き込み（MP3 / FLAC / WAV / M4A / Ogg Vorbis）
- kpi一覧の拡張子絞り込み

## 注意事項
- **Brandish4 および ガガーブトリロジー**については、WAVファイルをHDDへコピーする必要があります（フォルダ名は `WAVE`, `WAVEDV`, `WAVEDVD` などゲームにより異なります）。
- ゲーム以外のWAVファイルはDirectShow扱いとなり、ループやフェードアウト機能は使用できません。

## クレジット
- スペアナ用FFT: Copyright Takuya OOURA, 1996-2001  
  http://www.kurims.kyoto-u.ac.jp/~ooura/fft-j.html
- ぐるみん BGM ループテーブル: ぽかん's Home Page（Falcom データアーカイブ 変換ツール Ver.0.16b）より拝借  
  http://www.geocities.jp/pokan_chan/#FALCNVRT

## ライセンス / 作者
Copyright (C) PrePrayerPower Soft
[https://ppp.oohara.jp/](https://ppp.oohara.jp/)



# oggYSEDbgm Media Player "Raira"
Nihon Falcom BGM Player / High-Performance Media Player

**Compatible OS:** Windows 11 or later

## Overview
This software is a specialized media player designed to play background music (Ogg, WAV, mp3, etc.) from **Nihon Falcom** titles—such as the *Ys* and *Trails* series—with the **exact loop points used in-game**.
Additionally, it supports high-resolution audio (DSD/FLAC) and video playback via DirectShow.

The interface is now more graphical and supports multiple languages.
**Supported Languages:**
Japanese, English, Français, Italiano, Español, 한국어, 中文, العربية, Русский, Deutsch, Português, Nederlands, Polski, Türkçe

### Environmental Modeling & Equalizer
An Equalizer function has been added, featuring **100 environmental models** and a **15-band EQ**.
100 EQ Presets are also included.
To prevent audio clipping when using environmental models, a **Dynamic Compressor** is implemented to automatically process tracks with high volume levels.

Global parameters such as **Sharpness**, **Low/High Balance**, **Sound Density**, **Stereo Depth**, **Reverb**, **Chorus**, and **Delay** are also available.
These operate independently of the environmental models.
For Reverb / Chorus / Delay, the upper half of each slider switches to pan reverb, chorus distortion, and multi delay.

Additionally, the intensity of the environmental effects can be adjusted, offering a high degree of acoustic freedom.

### Piano Roll Feature
A simplified **108-key** piano roll has been implemented. Use the right-click menu for controls, detection tuning, and chord/melody display during playback.

### Waveform & Spectrum Analyzer
An Ozone-inspired real-time analyzer has been added.

- **Top:** Multi-channel scrolling PCM waveform with L/R level meters (RMS + RMS peak hold)
- **Bottom:** Log-frequency spectrum (multiple display modes, with peak hold)
- **Display modes:** Ozone / Cubase Frequency / Voxengo SPAN / Ableton Spectrum / FabFilter Pro-Q / Bars / Line only (right-click)
- **Wave speed:** x0.25–x2.0 (right-click)
- **Layouts:** Overlay / split vertical / split horizontal / 2x2 / 2x4 (right-click menu)
- **EQ overlay:** Shows the existing 15-band EQ band markers and gain curve on the spectrum
- **Mouse readout:** Hover to read Hz, dB, and channel (on split layouts, the panel under the cursor)
- **Controls:** Freeze (F) / Peak hold (P) / EQ overlay (E) / Reset peaks (Space or double-click)
- **Rendering:** Analysis worker thread with free-running UI present (same approach as the piano roll), including Acrylic (blur) mode support

Playback note icon blinking (two frames) and heart (♡) draw order on the media-player list were also fixed.

### Media Player Screen Mode Included
Includes a media-player mode called **Raira**. You can choose between the Falcom BGM screen and this mode at startup.

### Tempo & Pitch Control
Tempo and pitch can be adjusted independently during playback.

### Lyrics Display
Supports .lrc lyrics display, including optional online lyric lookup.

### Jacket Art & Playlist
Album jacket display and playlists are supported (import m3u / m3u8 / pls / xspf, among other features). Per-track settings such as volume and EQ are remembered.
Turning **Save per-song** on reloads and applies saved parameters for tracks marked with ★. Turning it off saves the current settings for that track before disabling the feature.

### Acrylic (Blur) UI
Supports Windows 11 acrylic-style blur for the interface.

### PCM Upscaling
Supports sample-rate / bit-depth upscaling and multi-channel output.

### Performance Prompt
A timed prompt feature can change pitch, tempo, effects, and more during playback. Besides text editing, it supports analyze-while-listening, history save/load, and atmosphere modes.

### Command Roll
Edit and place prompt commands on a time-based roll (multiple lanes). Open it from the media player. Follow-main lock and zoom level can be remembered.

### Playback Details
Gapless playback, ReplayGain, Mid/Side, correlation meter, export limiter, loop points, loop-boundary fade, cues, tag writing, and a simple waveform preview are grouped in one window. Open it from the media player **Extra** button or the playlist context menu.
Tag writing supports **MP3 / FLAC / WAV / M4A / Ogg Vorbis** (encrypted FLAC and Opus writing are not supported).

### File Info
From the playlist or media-player context menu (**File Info**), you can view and edit display name, artist, album, path, tags, loops, and more.

- Apply tags → playlist fields, reload tags, write tags (formats above)
- Browse path / select in Explorer / copy path / copy file name
- Missing-file / extension / size status, and per-song settings (★) summary with clear
- Jump to Playback Details; edit loop points (saved to the playlist on OK)
- With multiple tracks selected, batch-edit artist / album

### Tag Edit
From the playlist context menu (**Edit tags**), edit title / artist / album plus year, track, genre, comment, and cover art, then write to the file. With multiple selection, blank fields are left unchanged and only filled fields are applied to all.

### Audio Export (WAV / mp3 / FLAC)
From the playlist context menu (**Audio export**), write selected tracks as **WAV / mp3 / FLAC** (format tabs). Audio is rendered through the PCM/WAV path, then encoded when needed.

- **Loop count**, **fade-out**, and **align leading silence** (trim if longer than N seconds; pad silence if shorter)
- **Sample rate** (source / 44.1–192 kHz) and KPI **length (sec)** when needed
- **Copy tags and cover**, plus optional title / artist / album / cover override
- **Apply prompt execution** (bake performance prompts into the exported PCM)
- **Crossfade** (multi-select): join tracks into one file with equal-power overlaps; also available via **Crossfade export…**
- **Mix** (multi-select): layer a chosen number of tracks at once with per-track volume % into one file; with Crossfade on, refill ended slots over the given seconds

Exports larger than 2GB use RF64, same as the traditional WAV path.

### Mic Mix (into live WAV save)
When **Save to WAV** is on, microphone input can be mixed into the PCM being written. Pick the capture device in Rendering (CRender); mix level is 0–200%. You can also toggle it from the media-player playlist context menu.

### Device Recording
From the media player **Record** button, capture playback-device loopback to **WAV / mp3 / FLAC**. Choose device, format, quality, and path; optional mic mix is available. mp3 / FLAC go through a temporary WAV encode step.

### Screen Capture
From **Capture**, record the screen to **MP4 (H.264 + AAC)**.

- **Modes:** primary monitor / all monitors (virtual desktop) / window composition
- **Window composition:** place multiple windows as layers; drag to move, corner handles to resize, Z-order controls
- Optional **system audio** (loopback) and **microphone**, FPS, and output resolution
- **Include MP song:** composite the open media-player window and adjust its layout on the preview
- Preview keeps updating while recording (HUD overlays are not written into the file)

![Player Screen](https://ppp.oohara.jp/img/ysedplay2e_git7.png)

![Player Screen](https://ppp.oohara.jp/img/mp3e.png)

![Player Screen](https://ppp.oohara.jp/img/mpe3e.png)

![Player Screen](https://ppp.oohara.jp/img/rece.png)

## Supported Game Titles
The player supports seamless BGM looping for the following titles:

### Ys Series
- Ys VI: The Ark of Napishtim
- Ys: The Oath in Felghana
- Ys Origin
- Ys I & II Chronicles / Complete
- Ys: Memories of Celceta (Steam)
- Ys VIII: Lacrimosa of DANA (Steam)
- Ys IX: Monstrum Nox (Steam)
- Ys X: Nordics (Steam)

### Trails Series
- The Legend of Heroes: Trails in the Sky FC / SC / the 3rd (Including Steam 1st)
- The Legend of Heroes: Trails from Zero
- The Legend of Heroes: Trails to Azure
- The Legend of Heroes: Trails of Cold Steel I / II / III / IV (Steam)
- The Legend of Heroes: Trails into Reverie (Steam)
- The Legend of Heroes: Trails through Daybreak II (Steam)
- The Legend of Heroes: Trails beyond the Horizon (Steam)

### Other Falcom & Third-Party Works
- Zwei!! (ADPCM for CD version / PCM for DVD version)
- Zwei: The Ilvard Resurrection
- Xanadu Next
- Arcturus
- Gurumin (ADPCM support via RIFF header search)
- Sorcerian Original
- Dinosaur Resurrection
- Brandish 4: The Tower of Sleeping God
- **Gagharv Trilogy:**
  - White Witch
  - A Tear of Vermillion
  - Cagesong of the Ocean
- The Rhapsody of Zephyr
- Destiny of the Light and Shadow (mp3)
- Genso Sangokushi 1 / 2 (mp3)

## Key Features

### 1. Game BGM Playback & Conversion
- **Seamless Looping:** Replicates the natural in-game music transitions.
- **Format Conversion:** Convert Ogg or mp3 game files to WAV. (Fade-out support added in Ver 0.4b).
- **Directory Mapping:** Scan your installed game folders (defaults to standard installation paths).

### 2. General Audio Playback
Supports the following formats:
- mp3, m4a, aac, alac, flac, tta, ape
- DSD (dsf, dff)
- OggOpus (48k)
- Legacy and modern **kpi plugins** (Kb Media Player) are supported (place in the `Plugins` folder; the 64-bit build uses KpiHost64).
  - The kpi list can be filtered by extension text (updates as you type; checkbox state is kept per plugin)

### 3. Video Playback (DirectShow)
Plays avi, mpg, and other DirectShow-compatible formats. On Windows Vista and later, it utilizes **EVR (Enhanced Video Renderer)** for high-quality output. Video, audio, and subtitle stream switching is also supported.
*Note: Playback of certain formats may require codec packs (e.g., K-Lite Codec Pack).*
[Instructional Site: ppp.oohara.jp/k-lite.html](http://ppp.oohara.jp/k-lite.html)

#### Video Controls & Shortcuts

| Action | Function |
| :--- | :--- |
| **Right-Click** | Change Window Size (1x / 1.5x / 2x) |
| **Double-Click** | Toggle Fullscreen |
| **Wheel / Drag** | Resize Window |
| **C Key** | Pause / Resume |
| **Up / Down Arrow** | Volume Control |
| **Left / Right Arrow** | Seek (Rewind / Fast Forward) |

### Additional Notes
- Random play, continuous play, loop-count settings, and A-B repeat
- Drag-and-drop file adding
- Resume position for mp3 and DirectShow playback
- Audio export (WAV / mp3 / FLAC; loop / fade / leading-silence align / crossfade / concurrent mix / sample-rate)
- WAV export larger than 2GB (RF64)
- Mic mix (with Save to WAV), device recording (loopback → WAV/mp3/FLAC), screen capture (MP4)
- Piano-roll detection tuning dialog (many sliders)
- Open analyzer / piano roll directly from the playlist
- Sort, move/copy to another playlist, refresh jacket for selection
- Missing-file review (path fix / apply / inline edit) and missing-mark rescan
- Playlist import (m3u / m3u8 / pls / xspf; UTF-8, relative paths, skip missing/duplicates)
- Rendering options (device, buffer, bit depth, fonts, file associations, etc.)
- Taskbar jump list (play/pause, EQ, jacket, playlist, and more)
- Startup and periodic update checks
- Warning when the app is muted in the Windows volume mixer
- Media-player extras such as minimize sync and tooltips
- Related windows follow when the main window is moved
- Per-song settings restore/save on feature toggle; File Info / Tag Edit tools
- Tag writing (MP3 / FLAC / WAV / M4A / Ogg Vorbis)
- kpi list extension filter

## Important Notes
- **For Brandish 4 and the Gagharv Trilogy:** WAV files must be copied to your HDD (Folder names like `WAVE`, `WAVEDV`, or `WAVEDVD` vary by game).
- Standard WAV files (not from game data) are handled via DirectShow; therefore, loop and fade-out functions are not available for these files.

## Credits
- Spectrum FFT: Copyright Takuya OOURA, 1996-2001  
  http://www.kurims.kyoto-u.ac.jp/~ooura/fft-j.html
- Gurumin BGM loop table: courtesy of pokan's Home Page (Falcom data-archive converter Ver.0.16b)  
  http://www.geocities.jp/pokan_chan/#FALCNVRT

## License / Author
Copyright (C) PrePrayerPower Soft
[https://ppp.oohara.jp/](https://ppp.oohara.jp/)
