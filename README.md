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

また環境モデルのかかり方の度合いの変更できるため、かなり自由度が高くなっています。

### ピアノロール機能
簡易ピアノロールが実装されました。再生中のコードやメロディの表示にも対応しています。

### 波形・周波数アナライザー
Ozone風のリアルタイムアナライザーを追加しました。

- **上部:** 多チャンネルPCM波形（横スクロール）と L/R レベルメーター（RMS＋RMSピークホールド）
- **下部:** 対数軸の周波数特性（複数の表示モード、ピークホールド）
- **表示モード:** Ozone / Cubase Frequency / Voxengo SPAN / Ableton Spectrum / FabFilter Pro-Q / バー / 線のみ（右クリック）
- **波形速度:** x0.25〜x2.0（右クリック）
- **レイアウト:** 重ね描き / 上下分割 / 左右分割 / 2x2 / 2x4（右クリックメニュー）
- **EQオーバーレイ:** 既存15バンドEQ（`eq[0..14]`）の帯域とゲイン曲線をスペクトラム上に表示
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
アルバムジャケットの表示と、m3u対応のプレイリスト機能があります。曲ごとの音量やEQなどの設定も覚えます。

### アクリル（ぼかし）UI
Windows 11のアクリル風ぼかし表示に対応しています。

### PCMアップスケール
サンプリングレートやビット深度のアップスケール、マルチチャンネル出力に対応しています。

### 演奏プロンプト
再生中にピッチやテンポ、エフェクトなどを時間指定で動かすプロンプト機能があります。

![ファルコムプレイヤー画面](https://ppp.oohara.jp/img/ysedplay2_git6.png)

![メディアプレイヤー画面](https://ppp.oohara.jp/img/mp_2.png)

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
- ランダム再生、連続再生、ループ回数の指定
- ファイルのドラッグ＆ドロップ追加
- mp3やDirectShow再生の途中位置の保存
- WAV書き出し時の2GB超対応（RF64）
- ピアノロールの検出パラメータ調整
- レンダリング設定（デバイス、バッファ、ビット深度、フォント、ファイル関連付けなど）
- タスクバーのジャンプリスト（再生／停止、EQ、ジャケット、プレイリストなど）
- 起動時・定期の更新チェック
- Windowsミキサーでアプリがミュートされているときの警告
- メディアプレイヤー側の最小化連動やツールチップ表示など

## 注意事項
- **Brandish4 および ガガーブトリロジー**については、WAVファイルをHDDへコピーする必要があります（フォルダ名は `WAVE`, `WAVEDV`, `WAVEDVD` などゲームにより異なります）。
- ゲーム以外のWAVファイルはDirectShow扱いとなり、ループやフェードアウト機能は使用できません。

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

Additionally, the intensity of the environmental effects can be adjusted, offering a high degree of acoustic freedom.

### Piano Roll Feature
A simplified piano roll has been implemented. Chord and melody display during playback is also supported.

### Waveform & Spectrum Analyzer
An Ozone-inspired real-time analyzer has been added.

- **Top:** Multi-channel scrolling PCM waveform with L/R level meters (RMS + RMS peak hold)
- **Bottom:** Log-frequency spectrum (multiple display modes, with peak hold)
- **Display modes:** Ozone / Cubase Frequency / Voxengo SPAN / Ableton Spectrum / FabFilter Pro-Q / Bars / Line only (right-click)
- **Wave speed:** x0.25–x2.0 (right-click)
- **Layouts:** Overlay / split vertical / split horizontal / 2x2 / 2x4 (right-click menu)
- **EQ overlay:** Shows the existing 15-band EQ (`eq[0..14]`) band markers and gain curve on the spectrum
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
Album jacket display and m3u playlists are supported. Per-track settings such as volume and EQ are remembered.

### Acrylic (Blur) UI
Supports Windows 11 acrylic-style blur for the interface.

### PCM Upscaling
Supports sample-rate / bit-depth upscaling and multi-channel output.

### Performance Prompt
A timed prompt feature can change pitch, tempo, effects, and more during playback.

![Player Screen](https://ppp.oohara.jp/img/ysedplay2e_git6.png)

![Player Screen](https://ppp.oohara.jp/img/mpe.png)

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
- Random play, continuous play, and loop-count settings
- Drag-and-drop file adding
- Resume position for mp3 and DirectShow playback
- WAV export larger than 2GB (RF64)
- Piano-roll detection parameter tuning
- Rendering options (device, buffer, bit depth, fonts, file associations, etc.)
- Taskbar jump list (play/pause, EQ, jacket, playlist, and more)
- Startup and periodic update checks
- Warning when the app is muted in the Windows volume mixer
- Media-player extras such as minimize sync and tooltips

## Important Notes
- **For Brandish 4 and the Gagharv Trilogy:** WAV files must be copied to your HDD (Folder names like `WAVE`, `WAVEDV`, or `WAVEDVD` vary by game).
- Standard WAV files (not from game data) are handled via DirectShow; therefore, loop and fade-out functions are not available for these files.

## License / Author
Copyright (C) PrePrayerPower Soft
[https://ppp.oohara.jp/](https://ppp.oohara.jp/)
