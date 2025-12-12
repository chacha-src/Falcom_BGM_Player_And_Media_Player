# libgcrypt ビルドスクリプト

## 概要

このスクリプトは、MSYS2/MinGW-w64環境を使用してlibgcryptとlibgpg-errorをビルドし、Visual Studio 2022で使用可能な.libファイルを生成します。

## 前提条件

### 1. MSYS2のインストール
```bash
# MSYS2のダウンロード
# https://www.msys2.org/ からダウンロード

# インストール後、MSYS2 MINGW64を起動
```

### 2. 必要なパッケージのインストール
```bash
# パッケージの更新
pacman -Syu

# 開発ツールのインストール
pacman -S mingw-w64-x86_64-toolchain
pacman -S base-devel
pacman -S mingw-w64-x86_64-autotools
pacman -S mingw-w64-x86_64-binutils
pacman -S mingw-w64-x86_64-pkg-config
```

## 使用方法

### 方法1: バッチファイルを使用（推奨）

1. **ファイルの配置**
   ```
   プロジェクトディレクトリ/
   ├── build-libgcrypt.sh
   ├── build-libgcrypt.bat
   └── libgcrypt-build-README.md
   ```

2. **バッチファイルの実行**
   ```cmd
   build-libgcrypt.bat
   ```

### 方法2: 直接スクリプトを実行

1. **MSYS2 MINGW64を起動**

2. **スクリプトの実行**
   ```bash
   chmod +x build-libgcrypt.sh
   ./build-libgcrypt.sh
   ```

## ビルド結果

### 生成されるファイル
```
C:\libgcrypt-build\final\
├── libgcrypt.lib          # libgcrypt静的ライブラリ
├── libgpg-error.lib       # libgpg-error静的ライブラリ
├── libgcrypt.a            # libgcrypt静的ライブラリ（MinGW用）
├── libgpg-error.a         # libgpg-error静的ライブラリ（MinGW用）
└── include\
    ├── gcrypt.h           # libgcryptメインヘッダー
    ├── gcrypt-module.h    # libgcryptモジュールヘッダー
    ├── gpg-error.h        # libgpg-errorヘッダー
    ├── gpgrt.h            # libgpg-errorランタイムヘッダー
    └── config.h           # 設定ファイル
```

## Visual Studio 2022での使用

### 1. プロジェクト設定

#### インクルードパスの追加
```
プロジェクトプロパティ > C/C++ > 全般 > 追加のインクルードディレクトリ
C:\libgcrypt-build\final\include
```

#### ライブラリパスの追加
```
プロジェクトプロパティ > リンカー > 全般 > 追加のライブラリディレクトリ
C:\libgcrypt-build\final
```

#### ライブラリのリンク
```
プロジェクトプロパティ > リンカー > 入力 > 追加の依存関係
libgcrypt.lib
libgpg-error.lib
```

#### プリプロセッサ定義
```
プロジェクトプロパティ > C/C++ > プリプロセッサ > プリプロセッサの定義
HAVE_CONFIG_H
_CRT_SECURE_NO_WARNINGS
```

### 2. 使用例
```cpp
#include <gcrypt.h>
#include <iostream>

int main() {
    // libgcryptの初期化
    if (!gcry_check_version(GCRYPT_VERSION)) {
        std::cerr << "libgcrypt version mismatch" << std::endl;
        return 1;
    }
    
    // 初期化の完了
    gcry_control(GCRYCTL_INITIALIZATION_FINISHED, 0);
    
    std::cout << "libgcrypt initialized successfully" << std::endl;
    
    return 0;
}
```

## スクリプトの詳細

### ビルド設定

#### libgpg-error
- **バージョン**: 1.47
- **設定**: 静的ライブラリのみ、ドキュメント・テスト無効
- **コンパイラフラグ**: `-DHAVE_CONFIG_H -D_CRT_SECURE_NO_WARNINGS`

#### libgcrypt
- **バージョン**: 1.10.2
- **設定**: 静的ライブラリのみ、ドキュメント・テスト無効
- **無効化された暗号化**: arcfour, blowfish, cast5, des, idea, rijndael, twofish
- **無効化された公開鍵暗号**: dsa, elgamal, rsa
- **無効化されたハッシュ**: md4, md5, rmd160, sha1, sha256, sha512, tiger

### dlltool対応

#### x86_64-w64-mingw32-dlltool.exe 2.44対応
```bash
# 古い形式（非対応）
x86_64-w64-mingw32-dlltool --def file.def --output-lib file.lib

# 新しい形式（対応）
x86_64-w64-mingw32-dlltool -d file.def -l file.lib
```

## トラブルシューティング

### よくある問題

#### 1. MSYS2が見つからない
```
エラー: MSYS2が見つかりません
```
**解決方法**: MSYS2をインストールし、パスを確認

#### 2. 必要なパッケージが不足
```
警告: mingw-w64-x86_64-gcc がインストールされていません
```
**解決方法**: 必要なパッケージをインストール
```bash
pacman -S mingw-w64-x86_64-toolchain
```

#### 3. ビルドエラー
```
ビルドが失敗しました: configureの実行に失敗
```
**解決方法**: 
- 依存関係の確認
- コンパイラの確認
- ログの詳細確認

#### 4. .libファイル生成エラー
```
libgcrypt.libの生成に失敗
```
**解決方法**: 
- dlltoolのバージョン確認
- .defファイルの内容確認

### デバッグ方法

#### 詳細ログの有効化
```bash
# スクリプト内でset -xを追加
set -x
```

#### 個別ビルドの実行
```bash
# libgpg-errorのみビルド
cd /c/libgcrypt-build/libgpg-error
./autogen.sh
./configure --host=x86_64-w64-mingw32 --prefix=/c/libgcrypt-build/output --enable-static --disable-shared
make
make install
```

## 注意事項

### 法的制限
- libgcryptはLGPLライセンス
- 暗号化機能の使用には法的制限がある場合があります
- 個人使用目的でのみ使用してください

### 技術的制限
- Windows x86/x64対応のみ
- 静的リンクのみ対応
- 一部の暗号化アルゴリズムは無効化されています

### セキュリティ
- 暗号化機能の適切な使用が必要
- キー管理の適切な実装が必要

## 更新履歴

- **v1.0**: 初回リリース
- **v1.1**: x86_64-w64-mingw32-dlltool.exe 2.44対応
- **v1.2**: エラーハンドリングの改善

## ライセンス

このスクリプトはMITライセンスの下で提供されています。
libgcryptとlibgpg-errorはそれぞれLGPLライセンスです。 