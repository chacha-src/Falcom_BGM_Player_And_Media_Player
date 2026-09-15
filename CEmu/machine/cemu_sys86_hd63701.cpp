/* Sys86 HD63701 実装ヘルパ。cemu_hard_ac.cpp 文脈へ直接コンパイル。独立翻訳単位。 */
#include "StdAfx.h"
#include "cemu_hard_ac.h"
#include "cemu_hd63701_bus.h"
#include "../chip/cemu_chip_opm.h"
#include "../chip/cemu_chip_c30.h"
#include "../vendor/hd63701/hd63701core.h"
#include "../vendor/hd63701/burnint.h"
#include "../vendor/hd63701/m6800.h"
#include <string.h>
#include <stdlib.h>

/* Sys86ContainsI の実装 */
static int Sys86ContainsI(const char* s, const char* needle)
{
	if (!s || !needle || !needle[0]) return 0;
	for (; *s; s++) {
		const char* a = s;
		const char* b = needle;
		while (*a && *b) {
			char ca = *a, cb = *b;
			if (ca >= 'A' && ca <= 'Z') ca = (char)(ca - 'A' + 'a');
			if (cb >= 'A' && cb <= 'Z') cb = (char)(cb - 'A' + 'a');
			if (ca != cb) break;
			a++; b++;
		}
		if (!*b) return 1;
	}
	return 0;
}

/* バス読込 */
uint8_t CHardAc::HD63701Read8(uint16_t addr)
{
	if ((addr & 0xffe0u) == 0x0000u)
		return (uint8_t)m6803_internal_registers_r((unsigned short)(addr & 0x1fu));
	/* HD63701V0: $80-FF に 128B（MAME hd6801_mem）。CUS60 は曲開始時 $017F まで RAM テスト（[AE+33] から F2E3）。$40-$1FF を内部 RAM に保ち、比較バックが F33F/F0DC へ落ちないようにする。 */
	if (addr >= 0x0040u && addr <= 0x01ffu)
		return hd63701Ram_[addr];
	if ((addr & 0xfc00u) == 0x1000u) {
		const unsigned off = addr & 0x3ffu;
		/* CUS60 F0DC: $1180=$A6 のあと $1181=$A6 待ち（メイン 6809 ドアベル）。ホスト CPU は無い — A6 を返し待ちを完了させる。IRQ ネストが F33F の後続 JSR [AE+4] をリブート嵐にした。 */
		if (off == 0x181u && namcoCus30_[0x180u] == 0xa6u)
			return 0xa6u;
		return namcoCus30_[off];
	}
	if (wsg63701_ && addr <= 0x03ffu)
		return chip_ ? CEmuChipC30Read(chip_, addr) : namcoCus30_[addr];

	/* skykid.cpp mcu_map: ワーク RAM $C000-C7FF（drgnbstr/pacland/skykid） */
	if (wsg63701_ && addr >= 0xc000u && addr <= 0xc7ffu && hd63701Rom_)
		return hd63701Rom_[addr];

	if (!wsg63701_ && addr >= 0x1400u && addr <= 0x1fffu)
		return hd63701Ram_[0x200u + (addr - 0x1400u)];
	/* MAME hopmappy YM $2000、genpeitd $2800、wndrmomo $3800、roishtar $6000。CUS60 STA $2000 stub は roishtar 以外のマップに残す。拡張ゲームの MCU ROM（$4000-$BFFF）のときは $6000 を YM デコードしない。 */
	if (!wsg63701_) {
		/* MAME: ゲームあたり YM ペア 1 組。FBNeo の write ハンドラは 4 組列挙するが MapMemory ROM $4000-7FFF が勝つので wndrmomo は $4000-$BFFF（$6000 含む）を ROM としてチェックサムする。全ペアをデコードすると F124 合計が失敗（fault 3）し 80A9 が永久リブート。CUS60 の STA $2000 stub は roishtar（ROM $2000-3FFF）以外で生きる。 */
		const uint16_t ymEven = (uint16_t)(addr & 0xfffeu);
		const uint16_t ymBase = hd63701YmBase_ ? hd63701YmBase_ : 0x2000u;
		const int roishtarRom = (hd63701MapKind_ == 1
			&& addr >= 0x2000u && addr <= 0x3fffu);
		if (!roishtarRom && (ymEven == ymBase || ymEven == 0x2000u))
			return chip_ ? chip_->ReadStatus() : 0;
		if (!roishtarRom) {
			const uint16_t blk = (uint16_t)(addr & 0xff00u);
			const uint16_t off = (uint16_t)(addr & 0x00ffu);
			if ((blk == ymBase || blk == 0x2000u)
				&& (off == 0x20u || off == 0x21u || off == 0x30u || off == 0x31u))
				return 0xff;
		}
	}
	if (hd63701Rom_ && addr < 0x10000u)
		return hd63701Rom_[addr];
	return 0xff;
}

/* バス書込 */
void CHardAc::HD63701Write8(uint16_t addr, uint8_t v)
{
	if ((addr & 0xffe0u) == 0x0000u) {
		m6803_internal_registers_w((unsigned short)(addr & 0x1fu), v);
		return;
	}
	if (addr >= 0x0040u && addr <= 0x01ffu) {
		hd63701Ram_[addr] = v;
		return;
	}
	if ((addr & 0xfc00u) == 0x1000u) {
		/* $1182=$A6 が必須: IRQ ベクタ [AE+8] はドアベルが A6 のときだけ AE+20..+28 音楽チェーンを走る（さもなくば AA/+2C のあと RTI）。 */
		const unsigned off = addr & 0x3ffu;
		namcoCus30_[off] = v;
		CChip* c30 = pcm_ ? pcm_ : (wsg63701_ ? chip_ : NULL);
		if (c30) c30->Write(off, v);
		if (wsg63701_) opmWrites_++;
		return;
	}
	/* pacland/skykid: 一部 MCU ビルドは低 amap 経由で 15XX も poke */
	if (wsg63701_ && addr <= 0x03ffu) {
		namcoCus30_[addr] = v;
		if (chip_) { chip_->Write(addr, v); opmWrites_++; }
		return;
	}
	if (wsg63701_ && addr >= 0xc000u && addr <= 0xc7ffu && hd63701Rom_) {
		hd63701Rom_[addr] = v;
		return;
	}
	if (!wsg63701_ && addr >= 0x1400u && addr <= 0x1fffu) {
		/* 8259 は $1400 に DSW/IN をパック（32 バイト）。$C8 を F20A dest（$14F0）のままにすると 8287 のビット展開がベクタ表へ歩く（14F8=478F → TRAP → FF78 fault 8）。$14F0-$156B を F14A/F20A（PC $F364）と 813E wipe（PC $814D）用に残す。 */
		if (addr >= 0x14f0u && addr < 0x156cu && hd63701_) {
			const uint16_t pc = HD63701Pc(hd63701_);
			if (pc >= 0x8240u && pc < 0x82f0u)
				return;
		}
		hd63701Ram_[0x200u + (addr - 0x1400u)] = v;
		return;
	}
	if (!wsg63701_) {
		const uint16_t ymEven = (uint16_t)(addr & 0xfffeu);
		const uint16_t ymBase = hd63701YmBase_ ? hd63701YmBase_ : 0x2000u;
		const int roishtarRom = (hd63701MapKind_ == 1
			&& addr >= 0x2000u && addr <= 0x3fffu);
		if (!roishtarRom && (ymEven == ymBase || ymEven == 0x2000u)) {
			if (chip_) {
				chip_->Write(addr & 1u, v);
				if (addr & 1u) {
					opmWrites_++;
					hd63701YmWrites_++;
				}
			}
			return;
		}
	}
	/* IRQ／ウォッチドッグストローブ — 無視 */
}

/* バス読込 */
uint8_t CHardAc::HD63701PortRead(uint16_t port)
{
	(void)port;
	return 0xff;
}

/* バス書込 */
void CHardAc::HD63701PortWrite(uint16_t port, uint8_t v)
{
	(void)port;
	(void)v;
}

/* CHardAc::HD63701InjectSong の実装 */
void CHardAc::HD63701InjectSong(uint8_t cmd)
{
	soundCmd_ = cmd;
	soundCmdPending_ = 1;
	/* CUS60 曲メールボックス（F4B1 / メインループ F07C）:
	   - 停止: $1183=0 かつ $B0 クリア。次開始をエッジにするため $1182 も CLR
	   - 開始: $1183=cmd、$B0=0。メインループが $1182=$A6 を格納して F4B1 を JSR
	   - AE ベクタ基点（0x11C0）を確保し IRQ／メイン呼び出し表を有効に保つ */
	if (hd63701Ram_[0xaeu] == 0 && hd63701Ram_[0xafu] == 0) {
		/* 6800 STX は hi 次いで lo — AE は 11C0 BE でなければならない */
		hd63701Ram_[0xaeu] = 0x11;
		hd63701Ram_[0xafu] = 0xc0;
		if (hd63701Rom_ && (namcoCus30_[0x1c0] | namcoCus30_[0x1c1]) == 0)
			memcpy(namcoCus30_ + 0x1c0, hd63701Rom_ + 0xf18e, 0x7c);
	}
	if (cmd == 0) {
		hd63701Ram_[0xb0u] = 0;
		namcoCus30_[0x182] = 0;
		namcoCus30_[0x183] = 0;
		namcoCus30_[0x191] = 0;
		CChip* c30 = pcm_ ? pcm_ : (wsg63701_ ? chip_ : NULL);
		if (c30) {
			c30->Write(0x182, 0);
			c30->Write(0x183, 0);
			c30->Write(0x191, 0);
		}
	} else {
		/* ホストドアベル: $1183=cmd、$B0=0、$1182=$A6。F4B1 は A6 を消費し曲を開始（その後 SEI）。Sys86RunCycles が A6 + CLI を再アサートし 60Hz IRQ を HOLD して [AE+8] が音楽チェーンを走らせる。 */
		hd63701Ram_[0xb0u] = 0;
		namcoCus30_[0x183] = cmd;
		namcoCus30_[0x182] = 0xa6;
		/* $1191 は CUS60 SFX 表（1-7）。添字 6 は RESET（F4AE）を JSR。BGM は F4B1 用に $1183 に残す。曲 ID をここにミラーしない。 */
		CChip* c30 = pcm_ ? pcm_ : (wsg63701_ ? chip_ : NULL);
		if (c30) {
			c30->Write(0x183, cmd);
			c30->Write(0x182, 0xa6);
			c30->Write(0x380, cmd); /* $1380: YM 要求ラッチ（846B） */
		}
		namcoCus30_[0x380u] = cmd;
		if (wsg63701_ && chip_)
			CEmuChipC30SetEnable(chip_, 1);
	}
	if (hd63701_)
		HD63701SetInputLine(hd63701_, HD63701_LINE_IRQ, HD63701_CLEAR_LINE);
}

/* データを載せる */
int CHardAc::LoadRomsSys86(CEmuZipFs* fs, const CEmuGameEntry* ge)
{
	if (!hd63701_ || !fs || !ge) return 0;
	memset(hd63701Ram_, 0, sizeof(hd63701Ram_));
	memset(namcoCus30_, 0, sizeof(namcoCus30_));
	hd63701YmWrites_ = 0;

	/* アーカイブ名からマップ種別（FBNeo nSubCPUConfig） */
	hd63701MapKind_ = 0;
	hd63701YmBase_ = 0x2000;
	if (_stricmp(ge->archive, "genpeitd") == 0) {
		hd63701MapKind_ = 2;
		hd63701YmBase_ = 0x2800;
	} else if (_strnicmp(ge->archive, "rthunder", 8) == 0) {
		hd63701MapKind_ = 3;
		hd63701YmBase_ = 0x2000;
	} else if (_stricmp(ge->archive, "wndrmomo") == 0) {
		hd63701MapKind_ = 4;
		hd63701YmBase_ = 0x3800;
	} else if (_stricmp(ge->archive, "roishtar") == 0) {
		hd63701MapKind_ = 1;
		hd63701YmBase_ = 0x6000;
	}

	if (hd63701Rom_) { free(hd63701Rom_); hd63701Rom_ = NULL; hd63701RomSize_ = 0; }
	hd63701Rom_ = (uint8_t*)calloc(1, 0x10000);
	if (!hd63701Rom_) return 0;
	hd63701RomSize_ = 0x10000;

	/* 内部 CUS60 @ F000（4K） */
	int gotInt = 0, gotExt = 0;
	for (int i = 0; i < fs->fileCount; i++) {
		char pathA[CEMU_ZIP_PATH];
		WideCharToMultiByte(CP_ACP, 0, fs->files[i].path, -1, pathA, (int)sizeof(pathA), NULL, NULL);
		const unsigned sz = fs->files[i].size;
		const unsigned char* data = fs->files[i].data;
		if (!data || data == (const unsigned char*)1) continue;
		if (!gotInt && sz == 0x1000u
			&& (Sys86ContainsI(pathA, "cus60") || Sys86ContainsI(pathA, "cus63")
				|| Sys86ContainsI(pathA, "mcu"))) {
			/* cus60-*.bin / cus63-*.bin / rt1-mcu.bin / pl1-mcu.bin に合わせる */
			memcpy(hd63701Rom_ + 0xf000, data, 0x1000);
			gotInt = 1;
		}
	}
	/* 外部サブプログラム */
	for (int i = 0; i < ge->romCount; i++) {
		const CEmuRomEntry* r = &ge->rom[i];
		if (_stricmp(r->type, "code") != 0 && _stricmp(r->type, "sound") != 0
			&& _stricmp(r->type, "audiocpu") != 0 && _stricmp(r->type, "mcu") != 0
			&& _stricmp(r->type, "sub") != 0)
			continue;
		unsigned sz = 0;
		const unsigned char* data = CEmuZipFsFind(fs, r->name, &sz);
		if (!data || sz < 0x1000u || sz > 0x10000u) continue;
		if (sz == 0x1000u) continue; /* 内部は既に処理済み */
		unsigned base = 0x8000u;
		if (hd63701MapKind_ == 2 || hd63701MapKind_ == 3 || hd63701MapKind_ == 4
			|| hd63701MapKind_ == 1)
			base = 0x4000u; /* FBNeo: $4000+ に 8K/32K 外部 MCU コード */
		else if (sz <= 0x4000u)
			base = 0x8000u;
		const unsigned n = (sz > (0x10000u - base)) ? (0x10000u - base) : sz;
		if (hd63701MapKind_ == 1 && sz >= 0x8000u) {
			/* MAME roishtar_mcu_map: $2000 に mcusub+2000、$8000 に mcusub+4000。YM @6000 が重なる。$4000 から 32K イメージを塗らない。 */
			memcpy(hd63701Rom_ + 0x2000, data + 0x2000, 0x2000);
			memcpy(hd63701Rom_ + 0x8000, data + 0x4000, 0x4000);
		} else {
			memcpy(hd63701Rom_ + base, data, n);
			/* FBNeo memcpy(DrvMCUROM, +0x4000, 0x4000) はイメージだけ埋める — CPU の $0000-$3FFF 読みは ROM ではなく YM/ポートハンドラ。そのミラーを実行イメージへ塗ると $2000/$2800 がプログラムバイトに見え（wndrmomo $FF → YM busy-wait 永久）。 */
		}
		gotExt = 1;
		break;
	}
	if (!gotExt) {
		/* ヒューリスティック: 最大の 8K〜32K 非波形メンバ */
		int best = -1;
		unsigned bestSz = 0;
		for (int i = 0; i < fs->fileCount; i++) {
			char pathA[CEMU_ZIP_PATH];
			WideCharToMultiByte(CP_ACP, 0, fs->files[i].path, -1, pathA, (int)sizeof(pathA), NULL, NULL);
			const unsigned sz = fs->files[i].size;
			if (sz < 0x2000u || sz > 0x8000u) continue;
			if (Sys86ContainsI(pathA, "cus60") || Sys86ContainsI(pathA, "mcu")) continue;
			if (Sys86ContainsI(pathA, "wave") || Sys86ContainsI(pathA, "pcm")) continue;
			if (sz > bestSz) { bestSz = sz; best = i; }
		}
		if (best >= 0) {
			unsigned base = (hd63701MapKind_ >= 1 && hd63701MapKind_ <= 4)
				? 0x4000u : 0x8000u;
			const unsigned n = (bestSz > (0x10000u - base)) ? (0x10000u - base) : bestSz;
			memcpy(hd63701Rom_ + base, fs->files[best].data, n);
			gotExt = 1;
		}
	}
	if (!gotInt) return 0;

	/* 任意の 63701x PCM サンプル ROM — 未ロードのまま（PLAY には CUS30+YM で足りる） */
	opmWrites_ = 0;
	cpuCycles_ = 0;
	if (chip_) chip_->Reset();
	if (pcm_) pcm_->Reset();

	CEmuHD63701BusSetAc(this);
	CEmuHD63701BusAttach(hd63701_, this);
	HD63701Reset(hd63701_);
	if (pcm_)
		CEmuChipC30SetEnable(pcm_, 1);
	return 1;
}

/* データを載せる */
int CHardAc::LoadRomsWsg63701(CEmuZipFs* fs, const CEmuGameEntry* ge)
{
	/* pacland / skykid / hopmappy / metrocrs: HD63701 + CUS30 MAPPY（YM 無し）。Sys86 ROM ローダ（CUS60/63 @F000 + 外部 @8000）を再利用し、波形 PROM を C30 へ付ける。 */
	if (!hd63701_ || !chip_ || !fs || !ge) return 0;
	if (!LoadRomsSys86(fs, ge)) return 0;
	/* CUS30 波形 RAM は MCU が埋める。最初の 256 バイトメンバ（多くはカラー PROM）を波形表として付けない。 */
	CEmuChipC30SetEnable(chip_, 1);
	return 1;
}
