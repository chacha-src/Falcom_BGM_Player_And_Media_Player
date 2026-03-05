chachakotorinのgithubから移転です。

# oggYSEDbgm
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
イコライザープリセットも50個用意されています、
環境モデルで音割れ防止のため、楽曲が既に大きいボリュームの場合、ダイナミックコンプレッサーにより音割れしないよう処理が施されております。

グローバルパラメータとして、鮮明さ、低音域高音域バランス、音の密度、音の立体感も用意してあります。
これらは環境モデルとは別で動作します。

また環境モデルのかかり方の度合いの変更できるため、かなり自由度が高くなっています。

![プレイヤー画面](https://ppp.oohara.jp/img/ysedplay2_git5.PNG)

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
- 英雄伝説6 空の軌跡 FC / SC / The 3rd (Steam版 The 1st含む) The 1stはプレイ中なのであと1曲曲名不明
- 英雄伝説 零の軌跡
- Steam版 閃の軌跡 I / II / III / IV
- Steam版 創の軌跡

### その他のファルコム作品 / 他社作品
- Zwei!! (CD版はADPCM、DVD版WAVはPCM処理)
- Zwei II
- XANADU NEXT
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
- mp3, m4a, aac, alac, flac
- DSD (dsf, dff)
- OggOpus (48k)
- Kb Media Playerの旧kpiプラグインと新kpiの一部に対応 (Win32版のみ/Pluginsフォルダに入れて使用)

### 3. 動画再生 (DirectShow)
avi, mpgなどのDirectShow対応動画を再生可能です。Windows Vista以降ではEVRを使用し、高画質で再生します。
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

## 注意事項
- **Brandish4 および ガガーブトリロジー**については、WAVファイルをHDDへコピーする必要があります（フォルダ名は `WAVE`, `WAVEDV`, `WAVEDVD` などゲームにより異なります）。
- ゲーム以外のWAVファイルはDirectShow扱いとなり、ループやフェードアウト機能は使用できません。

## ライセンス / 作者
Copyright (C) PrePrayerPower Soft
[https://ppp.oohara.jp/](https://ppp.oohara.jp/)



# oggYSEDbgm
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
50 EQ Presets are also included.
To prevent audio clipping when using environmental models, a **Dynamic Compressor** is implemented to automatically process tracks with high volume levels.

Global parameters such as **Sharpness**, **Low/High Balance**, **Sound Density**, and **Stereo Depth** are also available.
These operate independently of the environmental models.

Additionally, the intensity of the environmental effects can be adjusted, offering a high degree of acoustic freedom.

![Player Screen](https://ppp.oohara.jp/img/ysedplay2_20260305e.PNG)

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
- The Legend of Heroes: Trails of Cold Steel I / II / III / IV (Steam)
- The Legend of Heroes: Trails into Reverie (Steam)

### Other Falcom & Third-Party Works
- Zwei!! (ADPCM for CD version / PCM for DVD version)
- Zwei: The Ilvard Resurrection
- Xanadu Next
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
- mp3, m4a, aac, alac, flac
- DSD (dsf, dff)
- OggOpus (48k)
- Legacy and modern **kpi plugins** (Kb Media Player) are supported (Win32 version only; place in the `Plugins` folder).

### 3. Video Playback (DirectShow)
Plays avi, mpg, and other DirectShow-compatible formats. On Windows Vista and later, it utilizes **EVR (Enhanced Video Renderer)** for high-quality output.
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

## Important Notes
- **For Brandish 4 and the Gagharv Trilogy:** WAV files must be copied to your HDD (Folder names like `WAVE`, `WAVEDV`, or `WAVEDVD` vary by game).
- Standard WAV files (not from game data) are handled via DirectShow; therefore, loop and fade-out functions are not available for these files.

## License / Author
Copyright (C) PrePrayerPower Soft
[https://ppp.oohara.jp/] (https://ppp.oohara.jp/)
