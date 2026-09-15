#include "StdAfx.h"
#include "cemu_hard_sg1000.h"
#include "../chip/cemu_chip_sn76489.h"
#include "../z80/cemu_z80_bus.h"
#define BLARGG_LITTLE_ENDIAN 1
#include "../z80/Ay_Cpu.h"
#include <string.h>
#include <stdlib.h>

/* SG-1000 / SC-3000: Z80 + SN76489 @ ~3.58 MHz。カート ROM 0000-BFFF、RAM C000+ */
enum {
	SG1000_CPU_HZ = 3579545,
	SG1000_PSG_HZ = 3579545
};

/* SG-1000 ハード: Star Jacker メールボックス既定 */
CHardSg1000::CHardSg1000()
	: mailboxAddr_(0xC066)
	, soundUpdatePc_(0x5AFA)
	, soundMutePc_(0x5DEB)
	, cpuHz_(SG1000_CPU_HZ)
	, psgHz_(SG1000_PSG_HZ)
	, psgWrites_(0)
	, cpu_(NULL)
	, chip_(NULL)
	, sampleRate_(44100)
	, cpuCycles_(0)
	, vdpStatus_(0x80)
	, vdpAddrLo_(0)
	, vdpAddrHi_(0)
	, vdpAddrLatch_(0)
{
	hardKind = KIND_SG1000;
	memset(mem_, 0, sizeof(mem_));
}

/* チップ／CPU を破棄 */
CHardSg1000::~CHardSg1000()
{
	Shutdown();
}

/* カタログが SG-1000/SC-3000 か */
static int IsSg1000Platform(const CEmuGameEntry* ge)
{
	if (!ge) return 0;
	if (_stricmp(ge->subtype, "sg1000") == 0) return 1;
	if (_stricmp(ge->dataDir, "sc3000") == 0) return 1;
	if (_stricmp(ge->platform, "sg1000") == 0) return 1;
	if (_strnicmp(ge->archive, "sc_", 3) == 0) return 1;
	return 0;
}

/* Sega 風 PSG 再生糊を探す: LD A,(mbox); BIT 7,A; JP Z,mute。
   update 入口は、その検査を CALL する箇所（Star Jacker 5AFA→5E0A、Hero 734A→7367）。
   無ければ近くの外部 CALL プロローグ（Mikie 02CF→02E6）。 */
static int Sg1000DetectBit7Glue(const uint8_t* rom, unsigned romSize,
	uint16_t* outBox, uint16_t* outUpdate, uint16_t* outMute)
{
	if (!rom || romSize < 16 || !outBox || !outUpdate || !outMute) return 0;
	for (unsigned i = 0; i + 8 <= romSize; i++) {
		if (rom[i] != 0x3A || rom[i + 3] != 0xCB || rom[i + 4] != 0x7F
			|| rom[i + 5] != 0xCA)
			continue;
		const uint16_t box = (uint16_t)(rom[i + 1] | (rom[i + 2] << 8));
		/* Star Jacker 型メールボックスは C000-C2FF。Congo/Mikie は C1E6 / C300 — 広めのワーク RAM を許す */
		if (box < 0xC000 || box > 0xC3FF) continue;
		const uint16_t mute = (uint16_t)(rom[i + 6] | (rom[i + 7] << 8));
		if (mute < 0x100 || mute >= 0xC000) continue;

		uint16_t update = (uint16_t)i;
		int foundCall = 0;
		for (unsigned c = 0; c + 3 <= romSize; c++) {
			if (rom[c] != 0xCD) continue;
			const uint16_t t = (uint16_t)(rom[c + 1] | (rom[c + 2] << 8));
			if (t == (uint16_t)i) {
				update = (uint16_t)c;
				foundCall = 1;
				break;
			}
		}
		if (!foundCall) {
			/* 他コードが CALL するプロローグを手前に探す */
			for (unsigned dist = 1; dist < 64 && i >= dist; dist++) {
				const unsigned pc = i - dist;
				int refs = 0;
				for (unsigned c = 0; c + 3 <= romSize; c++) {
					if (rom[c] == 0xCD
						&& (uint16_t)(rom[c + 1] | (rom[c + 2] << 8)) == (uint16_t)pc)
						refs++;
				}
				if (refs > 0) {
					update = (uint16_t)pc;
					break;
				}
			}
		}
		*outBox = box;
		*outUpdate = update;
		*outMute = mute;
		return 1;
	}
	return 0;
}

/* CPU/PSG を生成し、既知アーカイブのメールボックスを上書き */
int CHardSg1000::Init(const CEmuGameEntry* ge, int sampleRate)
{
	if (!ge || !IsSg1000Platform(ge)) return 0;
	sampleRate_ = sampleRate > 0 ? sampleRate : 44100;
	cpuHz_ = SG1000_CPU_HZ;
	psgHz_ = SG1000_PSG_HZ;
	/* 既定: Star Jacker 配置（ROM 読込／検出後に上書き） */
	mailboxAddr_ = 0xC066;
	soundUpdatePc_ = 0x5AFA;
	soundMutePc_ = 0x5DEB;
	/* Congo Bongo: PSG エンジンメールボックス C1E6（BIT7 曲コマンド）、tick @4D3D（IRQ 0465 が呼ぶ）。C06A はゲームの「音楽 ON」フラグだけ。 */
	if (_stricmp(ge->archive, "sc_congobongo") == 0
		|| _stricmp(ge->archive, "sc_congo") == 0) {
		mailboxAddr_ = 0xC1E6;
		soundUpdatePc_ = 0x4D3D;
		soundMutePc_ = 0x510A;
	}
	/* Mikie: PSG エンジンメールボックス C300 / tick @6BA2（IRQ が呼ぶ）。C015/02CF はゲームプレイへ JP するブート経路。 */
	if (_stricmp(ge->archive, "sc_mikie") == 0
		|| _stricmp(ge->archive, "sc_shinnyushain") == 0) {
		mailboxAddr_ = 0xC300;
		soundUpdatePc_ = 0x6BA2;
		soundMutePc_ = 0x15AA;
	}
	chip_ = CEmuChipSn76489Create((uint32_t)psgHz_, sampleRate_);
	cpu_ = new Ay_Cpu();
	psgWrites_ = 0;
	return (chip_ && cpu_) ? 1 : 0;
}

/* CPU/チップを破棄 */
void CHardSg1000::Shutdown()
{
	if (CEmuZ80BusGetActive() == this)
		CEmuZ80BusSetActive(NULL);
	if (cpu_) { delete cpu_; cpu_ = NULL; }
	if (chip_) {
		CEmuChipSn76489Destroy(chip_);
		chip_ = NULL;
	}
}

/* VDP ステータスと入力 stub */
uint8_t CHardSg1000::PortIn(uint16_t port)
{
	const uint8_t p = (uint8_t)(port & 0xff);
	/* VDP ステータス (BF): フレーム IRQ 準備済みを返し、ポーリングが回らないようにする */
	if (p == 0xbf) {
		const uint8_t st = vdpStatus_;
		vdpStatus_ &= (uint8_t)~0x80;
		vdpAddrLatch_ = 0;
		return st | 0x80;
	}
	if (p == 0xbe)
		return 0x00;
	/* ジョイスティック／キーボード stub — 音源経路では未使用 */
	if (p == 0xdc || p == 0xdd || p == 0xde || p == 0xdf)
		return 0xff;
	return 0xff;
}

/* VDP アドレス／データ。PSG は OUT 経由 */
void CHardSg1000::PortOut(uint16_t port, uint8_t data)
{
	const uint8_t p = (uint8_t)(port & 0xff);
	if (p == 0x7f || p == 0x7e) {
		if (chip_) {
			chip_->Write(0, data);
			psgWrites_ = CEmuChipSn76489WriteCount(chip_);
		}
		return;
	}
	if (p == 0xbf) {
		if (!vdpAddrLatch_) {
			vdpAddrLo_ = data;
			vdpAddrLatch_ = 1;
		} else {
			vdpAddrHi_ = data;
			vdpAddrLatch_ = 0;
		}
		return;
	}
	if (p == 0xbe) {
		/* VDP データ — 無視（VRAM 無し） */
		return;
	}
}

/* カート ROM は無視。C000+ のみ書く */
void CHardSg1000::MemWrite(uint16_t addr, uint8_t data)
{
	/* カート ROM は書けない。C000-FFFF をワーク RAM として残す */
	if (addr >= 0xC000)
		mem_[addr] = data;
}

/* 64K ビュー */
uint8_t CHardSg1000::MemRead(uint16_t addr)
{
	return mem_[addr];
}

/* zip メンバ名から拡張子を除いたベース名 */
static void CEmuSgZipBaseName(const char* name, char* out, int outCap)
{
	if (!out || outCap <= 0) return;
	out[0] = 0;
	if (!name) return;
	const char* base = name;
	for (const char* p = name; *p; p++) {
		if (*p == '/' || *p == '\\') base = p + 1;
	}
	strncpy_s(out, (size_t)outCap, base, _TRUNCATE);
}

/* カート ROM を 0000 へ載せ、BIT7 糊を検出 */
int CHardSg1000::LoadRoms(CEmuZipFs* fs, const CEmuGameEntry* ge, unsigned titleCode)
{
	(void)titleCode;
	if (!fs || !ge || !cpu_) return 0;
	memset(mem_, 0, sizeof(mem_));
	int loaded = 0;

	for (int i = 0; i < ge->romCount; i++) {
		const CEmuRomEntry* r = &ge->rom[i];
		if (_stricmp(r->type, "code") != 0) continue;
		int off = r->offset;
		if (off < 0) off = 0;
		if (off >= 0xC000) continue;
		unsigned sz = 0;
		const unsigned char* data = CEmuZipFsFind(fs, r->name, &sz);
		if (!data || !sz) continue;
		unsigned n = sz;
		if (off + (int)n > 0xC000)
			n = (unsigned)(0xC000 - off);
		memcpy(mem_ + off, data, n);
		loaded++;
	}

	/* フォールバック: 最初の .sg メンバ */
	if (!loaded) {
		for (int i = 0; i < fs->fileCount; i++) {
			char pathA[CEMU_ZIP_PATH];
			WideCharToMultiByte(CP_ACP, 0, fs->files[i].path, -1, pathA, (int)sizeof(pathA), NULL, NULL);
			char base[CEMU_ZIP_PATH];
			CEmuSgZipBaseName(pathA, base, (int)sizeof(base));
			size_t n = strlen(base);
			if (n < 3) continue;
			if (n >= 3 && _stricmp(base + n - 3, ".sg") == 0) {
				/* 読込成功 */
			} else if (fs->files[i].size != 0x8000 && fs->files[i].size != 0xC000) {
				continue;
			}
			unsigned sz = fs->files[i].size;
			unsigned copy = sz > 0xC000 ? 0xC000u : sz;
			memcpy(mem_, fs->files[i].data, copy);
			loaded++;
			break;
		}
	}
	if (!loaded) return 0;

	/* カート画像から BIT7 メールボックス糊を自動検出（Init で Congo/Mikie を上書きした場合は除く） */
	if (!(_stricmp(ge->archive, "sc_congobongo") == 0
		|| _stricmp(ge->archive, "sc_congo") == 0
		|| _stricmp(ge->archive, "sc_mikie") == 0
		|| _stricmp(ge->archive, "sc_shinnyushain") == 0)) {
		uint16_t box = 0, upd = 0, mute = 0;
		if (Sg1000DetectBit7Glue(mem_, 0xC000u, &box, &upd, &mute)) {
			mailboxAddr_ = box;
			soundUpdatePc_ = upd;
			soundMutePc_ = mute;
		}
	}

	cpu_->reset(mem_);
	cpu_->r.pc = 0;
	cpu_->r.sp = 0xC064;
	cpuCycles_ = 0;
	vdpStatus_ = 0x80;
	vdpAddrLatch_ = 0;
	psgWrites_ = 0;
	if (chip_) chip_->Reset();
	return 1;
}

/* Z80 バスのアクティブ SG-1000 を設定 */
void CEmuHardSg1000SetActive(CHardSg1000* hw)
{
	CEmuZ80BusSetActive(hw);
}
