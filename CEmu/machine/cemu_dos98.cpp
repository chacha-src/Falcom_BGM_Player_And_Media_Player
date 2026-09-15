#include "StdAfx.h"
#include "cemu_dos98.h"
#include "../vendor/np2/np2ffi.h"
#include <stdlib.h>
#include <string.h>

extern int CEmuPc98ValkyKeepIrq0();

enum {
	FLAG_CF = 0x0001,
	FLAG_ZF = 0x0040
};

/* DosLin の実装 */
static unsigned DosLin(uint16_t seg, uint16_t off)
{
	return ((unsigned)seg << 4) + (unsigned)off;
}

/* Rd16 の実装 */
static uint16_t Rd16(const uint8_t* mem, unsigned lin)
{
	return (uint16_t)(mem[lin] | (mem[lin + 1] << 8));
}

/* Wr16 の実装 */
static void Wr16(uint8_t* mem, unsigned lin, uint16_t v)
{
	mem[lin] = (uint8_t)(v & 0xff);
	mem[lin + 1] = (uint8_t)(v >> 8);
}

/* Bcd8 の実装 */
static uint8_t Bcd8(unsigned v)
{
	return (uint8_t)(((v / 10u) << 4) | (v % 10u));
}

int CEmuDos98::traceDefault_ = 0;

CEmuDos98::CEmuDos98()
{
	memset(files_, 0, sizeof(files_));
	memset(handles_, 0, sizeof(handles_));
	fileCount_ = 0;
	nextHandle_ = 5;
	pspSeg_ = 0;
	dtaSeg_ = 0;
	dtaOff_ = 0;
	memset(installed_, 0, sizeof(installed_));
	memset(instSeg_, 0, sizeof(instSeg_));
	memset(instOff_, 0, sizeof(instOff_));
	memset(unhandledFn_, 0, sizeof(unhandledFn_));
	memset(unhandledVec_, 0, sizeof(unhandledVec_));
	memset(unhandledInt18_, 0, sizeof(unhandledInt18_));
	trapVec_ = 0;
	trapCs_ = 0;
	trapIp_ = 0;
	traceOn_ = traceDefault_;
	traceCount_ = 0;
	readLogCount_ = 0;
	findPat_[0] = 0;
	findNext_ = 0;
	allocStrategy_ = 0;
	pcAtBios_ = 0;
}

CEmuDos98::~CEmuDos98()
{
	FreeFiles();
}

/* CEmuDos98::FreeFiles の実装 */
void CEmuDos98::FreeFiles()
{
	for (int i = 0; i < fileCount_; i++) {
		free(files_[i].data);
		files_[i].data = NULL;
		files_[i].size = 0;
		files_[i].name[0] = 0;
	}
	fileCount_ = 0;
}

/* CEmuDos98::Reset の実装 */
void CEmuDos98::Reset()
{
	FreeFiles();
	memset(handles_, 0, sizeof(handles_));
	nextHandle_ = 5;
	pspSeg_ = 0;
	dtaSeg_ = 0;
	dtaOff_ = 0;
	memset(installed_, 0, sizeof(installed_));
	memset(instSeg_, 0, sizeof(instSeg_));
	memset(instOff_, 0, sizeof(instOff_));
	memset(unhandledFn_, 0, sizeof(unhandledFn_));
	memset(unhandledVec_, 0, sizeof(unhandledVec_));
	memset(unhandledInt18_, 0, sizeof(unhandledInt18_));
	trapVec_ = 0;
	trapCs_ = 0;
	trapIp_ = 0;
	traceOn_ = traceDefault_;
	traceCount_ = 0;
	readLogCount_ = 0;
	findPat_[0] = 0;
	findNext_ = 0;
	allocStrategy_ = 0;
}

/* CEmuDos98::UpperCopy の実装 */
void CEmuDos98::UpperCopy(char* dst, int dstCap, const char* src) const
{
	if (!dst || dstCap <= 0) return;
	dst[0] = 0;
	if (!src) return;
	int j = 0;
	for (int i = 0; src[i] && j < dstCap - 1; i++) {
		char c = src[i];
		if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
		dst[j++] = c;
	}
	dst[j] = 0;
}

/* CEmuDos98::FindFile の実装 */
const CEmuDos98File* CEmuDos98::FindFile(const char* name) const
{
	char up[DOS98_NAME];
	UpperCopy(up, (int)sizeof(up), name);
	const char* base = up;
	for (const char* p = up; *p; p++) {
		if (*p == '\\' || *p == '/' || *p == ':')
			base = p + 1;
	}
	for (int i = 0; i < fileCount_; i++) {
		if (_stricmp(files_[i].name, up) == 0 || _stricmp(files_[i].name, base) == 0)
			return &files_[i];
	}
	return NULL;
}

/* CEmuDos98::FindFileMut の実装 */
CEmuDos98File* CEmuDos98::FindFileMut(const char* name)
{
	return const_cast<CEmuDos98File*>(FindFile(name));
}

/* CEmuDos98::AddFile の実装 */
void CEmuDos98::AddFile(const char* name, const unsigned char* data, unsigned size)
{
	if (!name || fileCount_ >= DOS98_FILE_MAX) return;
	/* size==0 は許可（HOOT の NULL 音色番兵）。そのとき data は NULL でよい。 */
	if (size > 0 && !data) return;
	char up[DOS98_NAME];
	UpperCopy(up, (int)sizeof(up), name);
	const char* base = up;
	for (const char* p = up; *p; p++) {
		if (*p == '\\' || *p == '/' || *p == ':')
			base = p + 1;
	}
	CEmuDos98File* f = FindFileMut(base);
	if (!f) {
		f = &files_[fileCount_++];
		memset(f, 0, sizeof(*f));
		strncpy_s(f->name, base, _TRUNCATE);
	} else {
		free(f->data);
		f->data = NULL;
		f->size = 0;
	}
	if (size == 0) {
		f->data = (unsigned char*)malloc(1);
		if (f->data) f->data[0] = 0;
		f->size = 0;
		return;
	}
	f->data = (unsigned char*)malloc(size);
	if (!f->data) return;
	memcpy(f->data, data, size);
	f->size = size;
}

/* CEmuDos98::SetHandle の実装 */
void CEmuDos98::SetHandle(uint16_t handle, const char* name)
{
	if (handle >= DOS98_HANDLE_MAX || !name) return;
	handles_[handle].used = 1;
	UpperCopy(handles_[handle].name, DOS98_NAME, name);
	/* ベース名のみ */
	char* base = handles_[handle].name;
	for (char* p = handles_[handle].name; *p; p++) {
		if (*p == '\\' || *p == '/' || *p == ':')
			base = p + 1;
	}
	if (base != handles_[handle].name)
		memmove(handles_[handle].name, base, strlen(base) + 1);
	handles_[handle].pos = 0;
	if (handle >= nextHandle_)
		nextHandle_ = (uint16_t)(handle + 1);
}

/* CEmuDos98::AllocHandle の実装 */
uint16_t CEmuDos98::AllocHandle()
{
	/* 曲がそれぞれ別 DOS ハンドルに載るカタログは次の空き番号を 20 超へ押し、C ランタイム（AIL の HOOT.EXE が使う Borland）は固定 20 エントリ表をハンドルで添字し、INT 21h 読みを出さず拒否する。その表内の最下位空きスロットを先に渡す。 */
	for (uint16_t h = 5; h < 20; h++)
		if (!handles_[h].used)
			return h;
	uint16_t h = nextHandle_++;
	if (h >= DOS98_HANDLE_MAX) h = (uint16_t)(DOS98_HANDLE_MAX - 1);
	return h;
}

/* CEmuDos98::SetHandleText の実装 */
void CEmuDos98::SetHandleText(uint16_t handle, const char* text)
{
	if (handle >= DOS98_HANDLE_MAX || !text) return;
	char key[DOS98_NAME];
	_snprintf_s(key, _TRUNCATE, "\x01CONIN%02X", handle);
	AddFile(key, (const unsigned char*)text, (unsigned)strlen(text));
	handles_[handle].used = 1;
	strncpy_s(handles_[handle].name, key, _TRUNCATE);
	handles_[handle].pos = 0;
	if (handle >= nextHandle_)
		nextHandle_ = (uint16_t)(handle + 1);
}

/* CEmuDos98::InstallTrampolines の実装 */
void CEmuDos98::InstallTrampolines(uint8_t* mem)
{
	if (!mem) return;
	const unsigned base = DosLin(DOS98_TRAMP_SEG, 0);
	/* ベクタ 1 組につき stub ペア 1 つ。ただし上位 8 本は段落が環境ブロックのアリーナヘッダに再利用され、ベクタ 0 の IRET を共有する。INT F8-FF はこの層へトラップせず即戻る（トラップがしていたことと同じ）。 */
	enum { kTrapVectors = 0xF8 };
	for (unsigned v = 0; v < 256; v++) {
		const unsigned off = (v < kTrapVectors) ? v * 2 : 1;
		if (v < kTrapVectors) {
			mem[base + off] = 0xF4;     /* HLT 命令 */
			mem[base + off + 1] = 0xCF; /* IRET 命令 */
		}
		Wr16(mem, v * 4, (uint16_t)off);
		Wr16(mem, v * 4 + 2, DOS98_TRAMP_SEG);
	}
	/* BIOS 既定 INT 1Ch は IRET であり HLT ではない。INT 08 がここにチェインする。ネストフレームで IF=0 の HLT は起きない。 */
	mem[base + 0x1C * 2] = 0x90;
	mem[base + 0x1C * 2 + 1] = 0xCF;
	/* INT 1A（PC-98）と INT 21 AH=63 用の空 CG フォント／DBCS リード表。HLT stub と 0x200 の CALL-ret スロットより先へ置く。 */
	memset(mem + base + 0x0210, 0, 0x40);
}

/* バス書込 */
void CEmuDos98::WriteMcb(uint8_t* mem, uint16_t seg, uint8_t sig, uint16_t owner, uint16_t size) const
{
	const unsigned l = DosLin(seg, 0);
	mem[l] = sig;
	Wr16(mem, l + 1, owner);
	Wr16(mem, l + 3, size);
	for (unsigned i = 5; i < 16; i++)
		mem[l + i] = 0;
}

/* CEmuDos98::InitArena の実装 */
void CEmuDos98::InitArena(uint8_t* mem)
{
	if (!mem) return;
	const uint16_t size = (uint16_t)(DOS98_ARENA_END - DOS98_ARENA_START - 1);
	WriteMcb(mem, DOS98_ARENA_START, (uint8_t)'Z', 0, size);
}

/* CEmuDos98::InstallDosStructures の実装 */
void CEmuDos98::InstallDosStructures(uint8_t* mem, unsigned memKb, const char* blaster)
{
	if (!mem) return;
	const uint16_t seg = DOS98_SYSVARS_SEG;
	for (uint16_t off = 0; off < 0x60; off++)
		mem[DosLin(seg, off)] = 0;
	Wr16(mem, DosLin(seg, (uint16_t)(DOS98_LOL_OFF - 2)), DOS98_ARENA_START);
	Wr16(mem, DosLin(seg, (uint16_t)(DOS98_LOL_OFF + 4)), 0x0040);
	Wr16(mem, DosLin(seg, (uint16_t)(DOS98_LOL_OFF + 6)), seg);
	Wr16(mem, DosLin(seg, 0x40), 0xFFFF);
	Wr16(mem, DosLin(seg, 0x42), seg);
	Wr16(mem, DosLin(seg, 0x44), 0x0001);

	/* IBM PC BIOS データ領域 @ 0040:0000 — HOOT/AIL が memsize で割る。シリアル／パラレル数と COM/LPT 基点ワードは 0 のまま。MIDI スタックが MPU 330h ではなく 3F8h を歩かないようにする。 */
	memset(mem + 0x400, 0, 0x100);
	Wr16(mem, 0x410, 0x0021); /* 装備: ディスク＋80x25 カラー */
	if (memKb < 64) memKb = 64;
	if (memKb > 640) memKb = 640;
	Wr16(mem, 0x413, (uint16_t)memKb);
	/* キーボードリングは 0040:001E..003D 内。Head=tail=0 だと INT 16 が IVT を先読みバッファと見て歩く。 */
	Wr16(mem, 0x41A, 0x001E);
	Wr16(mem, 0x41C, 0x001E);
	Wr16(mem, 0x480, 0x001E);
	Wr16(mem, 0x482, 0x003E);
	mem[0x449] = 0x03;        /* ビデオモード 80x25 カラー */
	Wr16(mem, 0x44A, 80);     /* 桁数 */
	Wr16(mem, 0x463, 0x3D4);  /* CRT ポート */
	mem[0x484] = 24;          /* EGA 行数-1。0 は 1 行画面に見える */
	Wr16(mem, 0x485, 16);     /* 文字高さ */
	Wr16(mem, 0x46C, 0);      /* タイマ tick 下位 */
	Wr16(mem, 0x46E, 0);      /* タイマ tick 上位 */

	/* PC-98 BIOS ワーク @ 0500h はこの物理ページを sysvars（セグメント 0050h）と共有。LOL は 0510h。未使用バイトだけが装備フラグ — 実機で MADP/Falcom が探る同じビット。0501 bit3 = 80286（QueenSoft では「FM あり」も）。0536 bit2 = サウンドボード。 */
	mem[0x501] = (uint8_t)(mem[0x501] | 0x08);
	mem[0x536] = (uint8_t)(mem[0x536] | 0x04);

	/* PSP:002C 環境 — BLASTER はエミュレート SB ポート／IRQ と一致させる */
	{
		const unsigned envLin = DosLin(DOS98_ENV_SEG, 0);
		memset(mem + envLin, 0, 256);
		unsigned pos = 0;
		auto put = [&](const char* s) {
			size_t L = strlen(s);
			if (pos + L + 2 >= 256) return;
			memcpy(mem + envLin + pos, s, L);
			pos += (unsigned)L;
			mem[envLin + pos++] = 0;
		};
		put("COMSPEC=C:\\COMMAND.COM");
		put("PATH=C:\\");
		if (blaster && blaster[0])
			put(blaster);
		mem[envLin + pos++] = 0;
		/* ブロックにはアリーナヘッダが要る。標準 TSR プロローグは段落数を読んで文字列を歩きブロックを解放する（OPNDRV.COM）。無いとトランポリン末尾をカウントとして読み、自分のコードへゴミをコピーした。サイズは文字列だけ。DOS は終端後にカウントワードとプログラムパスも置くが、足すとヘッダがそれも覆い、KOEI 縮小ルーチンが文字列用に取ったバッファを越えて slack をコピーした。 */
		WriteMcb(mem, DOS98_ENV_MCB_SEG, (uint8_t)'M', DOS98_ENV_SEG,
			(uint16_t)((pos + 15) / 16));
	}
}

/* CEmuDos98::MaxFreeBlock の実装 */
uint16_t CEmuDos98::MaxFreeBlock(const uint8_t* mem) const
{
	uint16_t seg = DOS98_ARENA_START;
	uint16_t best = 0;
	for (int guard = 0; guard < 256; guard++) {
		const unsigned l = DosLin(seg, 0);
		const uint8_t sig = mem[l];
		const uint16_t owner = Rd16(mem, l + 1);
		const uint16_t size = Rd16(mem, l + 3);
		if (owner == 0 && size > best) best = size;
		if (sig == (uint8_t)'Z') return best;
		seg = (uint16_t)(seg + 1 + size);
	}
	return best;
}

/* CEmuDos98::Alloc の実装 */
int CEmuDos98::Alloc(uint8_t* mem, uint16_t paras, uint16_t owner, uint16_t* outSeg) const
{
	if (!outSeg) return 0;
	*outSeg = 0;
	uint16_t seg = DOS98_ARENA_START;
	for (int guard = 0; guard < 256; guard++) {
		const unsigned l = DosLin(seg, 0);
		const uint8_t sig = mem[l];
		const uint16_t blockOwner = Rd16(mem, l + 1);
		const uint16_t blockSize = Rd16(mem, l + 3);
		const int isLast = (sig == (uint8_t)'Z');
		if (blockOwner == 0 && blockSize >= paras) {
			if (blockSize > (uint16_t)(paras + 1)) {
				const uint16_t remSeg = (uint16_t)(seg + 1 + paras);
				const uint16_t remSize = (uint16_t)(blockSize - paras - 1);
				WriteMcb(mem, remSeg, isLast ? (uint8_t)'Z' : (uint8_t)'M', 0, remSize);
				WriteMcb(mem, seg, (uint8_t)'M', owner, paras);
			} else {
				WriteMcb(mem, seg, sig, owner, blockSize);
			}
			*outSeg = (uint16_t)(seg + 1);
			return 1;
		}
		if (isLast) return 0;
		seg = (uint16_t)(seg + 1 + blockSize);
	}
	return 0;
}

/* DosInArena の実装 */
static int DosInArena(uint16_t seg)
{
	return seg > DOS98_ARENA_START && seg < DOS98_ARENA_END;
}

/* CEmuDos98::Coalesce の実装 */
void CEmuDos98::Coalesce(uint8_t* mem) const
{
	uint16_t seg = DOS98_ARENA_START;
	for (int guard = 0; guard < 256; guard++) {
		const unsigned l = DosLin(seg, 0);
		const uint8_t sig = mem[l];
		if (sig != (uint8_t)'M' && sig != (uint8_t)'Z') return;
		if (sig == (uint8_t)'Z') return;
		const uint16_t owner = Rd16(mem, l + 1);
		const uint16_t size = Rd16(mem, l + 3);
		const uint16_t next = (uint16_t)(seg + 1 + size);
		const unsigned nl = DosLin(next, 0);
		const uint8_t nsig = mem[nl];
		const uint16_t nowner = Rd16(mem, nl + 1);
		const uint16_t nsize = Rd16(mem, nl + 3);
		if (owner == 0 && nowner == 0) {
			WriteMcb(mem, seg, nsig, 0, (uint16_t)(size + 1 + nsize));
			continue;
		}
		seg = next;
	}
}

/* CEmuDos98::FreeBlock の実装 */
void CEmuDos98::FreeBlock(uint8_t* mem, uint16_t dataSeg) const
{
	if (!DosInArena(dataSeg)) return;
	const uint16_t mcb = (uint16_t)(dataSeg - 1);
	Wr16(mem, DosLin(mcb, 0) + 1, 0);
	Coalesce(mem);
}

/* CEmuDos98::Resize の実装 */
int CEmuDos98::Resize(uint8_t* mem, uint16_t dataSeg, uint16_t paras, uint16_t* errMax) const
{
	if (errMax) *errMax = 0;
	if (!DosInArena(dataSeg)) return 1;
	const uint16_t mcb = (uint16_t)(dataSeg - 1);
	const unsigned l = DosLin(mcb, 0);
	const uint8_t sig = mem[l];
	const uint16_t owner = Rd16(mem, l + 1);
	const uint16_t size = Rd16(mem, l + 3);
	if (paras <= size) {
		/* Tiny モデル COM（SS=CS=PSP）は AH=4A で約 1KB へ縮小し、穴へ AH=48 192KB。その穴は CS:0000-FFFF 内なのでグルーの near IP（gintetsu 2002:0FAD）が曲バッファを実行する（#UD）。生きた CS は INT 21 トランポリン — IRET フレームの呼び出し元を使う。 */
		{
			const unsigned fr = DosLin(np2_reg_get(NP2_R_SS),
				np2_reg_get(NP2_R_SP));
			const uint16_t callerCs = Rd16(mem, fr + 2);
			if (dataSeg == callerCs && paras < 0x1000u && size >= 0x1000u)
				paras = 0x1000u;
		}
		if (size >= (uint16_t)(paras + 1)) {
			const uint16_t remSeg = (uint16_t)(mcb + 1 + paras);
			const uint16_t remSize = (uint16_t)(size - paras - 1);
			WriteMcb(mem, remSeg, sig == (uint8_t)'Z' ? (uint8_t)'Z' : (uint8_t)'M', 0, remSize);
			WriteMcb(mem, mcb, (uint8_t)'M', owner, paras);
			Coalesce(mem);
		}
		return 1;
	}
	if (sig == (uint8_t)'Z') {
		if (errMax) *errMax = size;
		return 0;
	}
	const uint16_t next = (uint16_t)(mcb + 1 + size);
	const unsigned nl = DosLin(next, 0);
	const uint16_t nowner = Rd16(mem, nl + 1);
	const uint16_t nsize = Rd16(mem, nl + 3);
	const uint8_t nsig = mem[nl];
	const uint16_t combined = (uint16_t)(size + 1 + nsize);
	if (nowner == 0 && combined >= paras) {
		if (combined > (uint16_t)(paras + 1)) {
			const uint16_t remSeg = (uint16_t)(mcb + 1 + paras);
			const uint16_t remSize = (uint16_t)(combined - paras - 1);
			WriteMcb(mem, remSeg, nsig == (uint8_t)'Z' ? (uint8_t)'Z' : (uint8_t)'M', 0, remSize);
			WriteMcb(mem, mcb, (uint8_t)'M', owner, paras);
		} else {
			WriteMcb(mem, mcb, nsig, owner, combined);
		}
		return 1;
	}
	if (errMax)
		*errMax = (uint16_t)(size + (nowner == 0 ? (1 + nsize) : 0));
	return 0;
}

/* CEmuDos98::BuildPsp の実装 */
void CEmuDos98::BuildPsp(uint8_t* mem, uint16_t pspSeg, uint16_t memTop, const char* tail, uint16_t envSeg) const
{
	const unsigned p = DosLin(pspSeg, 0);
	memset(mem + p, 0, 256);
	mem[p + 0x00] = 0xCD;
	mem[p + 0x01] = 0x20;
	Wr16(mem, p + 0x02, memTop);
	Wr16(mem, p + 0x2C, envSeg);
	const char* t = tail ? tail : "";
	int n = (int)strlen(t);
	if (n > 126) n = 126;
	/* DOS コマンドテイルは引数があるとき先頭スペースを含む */
	mem[p + 0x80] = (uint8_t)n;
	for (int i = 0; i < n; i++)
		mem[p + 0x81 + i] = (uint8_t)t[i];
	mem[p + 0x81 + n] = 0x0D;
}

/* CEmuDos98::ResolveProgram の実装 */
int CEmuDos98::ResolveProgram(const char* name, const unsigned char** outData, unsigned* outSize, int* outIsExe) const
{
	if (!name || !outData || !outSize) return 0;
	*outData = NULL;
	*outSize = 0;
	if (outIsExe) *outIsExe = 0;
	char up[DOS98_NAME];
	UpperCopy(up, (int)sizeof(up), name);
	const char* candidates[3];
	char c1[DOS98_NAME], c2[DOS98_NAME];
	int nc = 0;
	if (strchr(up, '.')) {
		candidates[nc++] = up;
	} else {
		_snprintf_s(c1, _TRUNCATE, "%s.COM", up);
		_snprintf_s(c2, _TRUNCATE, "%s.EXE", up);
		candidates[nc++] = c1;
		candidates[nc++] = c2;
		candidates[nc++] = up;
	}
	for (int i = 0; i < nc; i++) {
		const CEmuDos98File* f = FindFile(candidates[i]);
		if (!f || !f->data) continue;
		*outData = f->data;
		*outSize = f->size;
		if (outIsExe)
			*outIsExe = (f->size >= 2 && f->data[0] == 'M' && f->data[1] == 'Z') ? 1 : 0;
		return 1;
	}
	return 0;
}

/* データを載せる */
int CEmuDos98::LoadCom(uint8_t* mem, const unsigned char* image, unsigned imageSize, const char* tail)
{
	if (!mem || !image) return 0;
	uint16_t psp = 0;
	if (!Alloc(mem, 0x1000, 0, &psp)) return 0;
	Wr16(mem, DosLin((uint16_t)(psp - 1), 0) + 1, psp);
	memset(mem + DosLin(psp, 0), 0, 0x1000u * 16u);
	BuildPsp(mem, psp, (uint16_t)(psp + 0x1000), tail, DOS98_ENV_SEG);
	if (0x100u + imageSize > 0x10000u) return 0;
	memcpy(mem + DosLin(psp, 0x100), image, imageSize);
	pspSeg_ = psp;
	dtaSeg_ = psp;
	dtaOff_ = 0x80;
	np2_reg_set(NP2_R_DS, psp);
	np2_reg_set(NP2_R_ES, psp);
	np2_set_ss_sp(psp, 0xFFFE);
	np2_reg_set(NP2_R_AX, 0);
	np2_set_cs_ip(psp, 0x100);
	np2_reg_set(NP2_R_FLAGS, 0x0202);
	return 1;
}

/* データを載せる */
int CEmuDos98::LoadExe(uint8_t* mem, const unsigned char* image, unsigned imageSize, const char* tail)
{
	if (!mem || !image || imageSize < 0x20 || image[0] != 'M' || image[1] != 'Z') return 0;
	const uint16_t bytesLast = Rd16(image, 0x02);
	const uint16_t pages = Rd16(image, 0x04);
	const uint16_t nreloc = Rd16(image, 0x06);
	const uint16_t hdrParas = Rd16(image, 0x08);
	const uint16_t minAlloc = Rd16(image, 0x0A);
	const uint16_t maxAlloc = Rd16(image, 0x0C);
	const uint16_t initSs = Rd16(image, 0x0E);
	const uint16_t initSp = Rd16(image, 0x10);
	const uint16_t initIp = Rd16(image, 0x14);
	const uint16_t initCs = Rd16(image, 0x16);
	const uint16_t relocOff = Rd16(image, 0x18);
	const unsigned hdrSize = (unsigned)hdrParas * 16u;
	unsigned imageBytes = (unsigned)pages * 512u;
	if (bytesLast != 0)
		imageBytes = imageBytes - 512u + (unsigned)bytesLast;
	/* EXEPACK/PACKED トレーラを MZ 宣言ロードサイズより先に残す */
	if (imageBytes < imageSize)
		imageBytes = imageSize;
	const unsigned loadSize = (imageBytes > hdrSize) ? (imageBytes - hdrSize) : 0;
	const unsigned loadParas = (loadSize + 15u) / 16u;
	/* DOS は EXE を最大空きブロックへ載せる（maxAlloc はしばしば FFFF）。MSC CRT は memTop−SS を見る。minAlloc だけだと約 9 パラで HOOT が Abnormal program termination。 */
	const uint16_t freeParas = MaxFreeBlock(mem);
	const uint32_t minNeed = 0x10u + loadParas + (uint32_t)minAlloc;
	uint32_t want = 0x10u + loadParas + (uint32_t)((maxAlloc > minAlloc) ? maxAlloc : minAlloc);
	if (want > (uint32_t)freeParas)
		want = freeParas;
	if (want < minNeed)
		want = minNeed;
	if (want > (uint32_t)freeParas || want < 0x11u)
		return 0;
	uint16_t need = (uint16_t)want;
	uint16_t psp = 0;
	if (!Alloc(mem, need, 0, &psp)) return 0;
	Wr16(mem, DosLin((uint16_t)(psp - 1), 0) + 1, psp);
	const uint16_t loadSeg = (uint16_t)(psp + 0x10);
	/* PSP＋イメージ＋BSS をゼロ。MSC CRT はゴミ BSS で abort（Abnormal program termination）。 */
	memset(mem + DosLin(psp, 0), 0, (unsigned)need * 16u);
	BuildPsp(mem, psp, (uint16_t)(psp + need), tail, DOS98_ENV_SEG);
	const unsigned dst = DosLin(loadSeg, 0);
	const unsigned srcEnd = (hdrSize + loadSize < imageSize) ? (hdrSize + loadSize) : imageSize;
	if (srcEnd > hdrSize)
		memcpy(mem + dst, image + hdrSize, srcEnd - hdrSize);
	for (uint16_t i = 0; i < nreloc; i++) {
		const unsigned e = (unsigned)relocOff + (unsigned)i * 4u;
		if (e + 4 > imageSize) break;
		const uint16_t off = Rd16(image, e);
		const uint16_t sg = Rd16(image, e + 2);
		const unsigned target = DosLin((uint16_t)(loadSeg + sg), off);
		Wr16(mem, target, (uint16_t)(Rd16(mem, target) + loadSeg));
	}
	pspSeg_ = psp;
	dtaSeg_ = psp;
	dtaOff_ = 0x80;
	np2_reg_set(NP2_R_DS, psp);
	np2_reg_set(NP2_R_ES, psp);
	np2_set_ss_sp((uint16_t)(loadSeg + initSs), initSp);
	np2_reg_set(NP2_R_AX, 0);
	np2_set_cs_ip((uint16_t)(loadSeg + initCs), initIp);
	np2_reg_set(NP2_R_FLAGS, 0x0202);
	return 1;
}

int CEmuDos98::LoadDeviceImage(uint8_t* mem, const char* name, uint16_t* outSeg,
	uint16_t* outStratOff, uint16_t* outIntrOff, unsigned extraParas) const
{
	if (!mem || !name || !outSeg) return 0;
	*outSeg = 0;
	if (outStratOff) *outStratOff = 0;
	if (outIntrOff) *outIntrOff = 0;
	const CEmuDos98File* file = FindFile(name);
	if (!file || !file->data || file->size < 18) return 0;
	const unsigned char* data = file->data;
	unsigned size = file->size;
	/* hoot は一部キャラクタデバイスを type=device MDR.EXE / mmd.sys と書くが zip メンバは MZ 包み。strategy/interrupt は MZ stub 後の SYS ヘッダ。MZ フィールドを使うとリロケ表へ CALL（#UD）。EXE 入口自体が SS=0 をセットし INT 21 4C01。 */
	if (size >= 0x20 && data[0] == 'M' && data[1] == 'Z') {
		const unsigned hdrSize = (unsigned)(data[8] | (data[9] << 8)) * 16u;
		if (hdrSize >= 0x20 && hdrSize + 18u <= size) {
			const unsigned char* sys = data + hdrSize;
			const uint16_t attr = (uint16_t)(sys[4] | (sys[5] << 8));
			if (attr & 0x8000) {
				data = sys;
				size -= hdrSize;
			}
		}
	}
	if (size < 18) return 0;
	const uint16_t stratEarly = (uint16_t)(data[6] | (data[7] << 8));
	const uint16_t intrEarly = (uint16_t)(data[8] | (data[9] << 8));
	/* Tiny モデル SYS（MDR.EXE）: interrupt プロローグ MOV AX,0 / MOV SS,AX / MOV DS,AX は 64KB DGROUP を欲する。DEVICE= は約 19KB だったので CS:AE98 が次の AH=48 バッファへ着地しゴミ実行（#BR/#UD）。 */
	int tinyDs0 = 0;
	if ((unsigned)intrEarly + 32u <= size) {
		for (unsigned i = 0; i + 2 < 24u; i++) {
			if (data[intrEarly + i] != 0xB8 || data[intrEarly + i + 1] != 0
				|| data[intrEarly + i + 2] != 0)
				continue;
			for (unsigned j = i; j + 1 < 32u; j++) {
				if (data[intrEarly + j] == 0x8E && data[intrEarly + j + 1] == 0xD0) {
					tinyDs0 = 1;
					break;
				}
			}
			break;
		}
	}
	/* 実 DOS は INIT 中に残りメモリ全部をドライバへ渡し、break 番地へ縮小する。MMD2.SYS はイメージ先（CS+~0x25C）に 4096 バイト曲バッファを置く。ファイルサイズだけだと次 MCB／トランポリンを壊す。 */
	unsigned paras = (size + 15u) / 16u + 1u;
	if (paras < 0x80u)
		paras = 0x80u;
	paras += 0x300u;
	paras += extraParas;
	if (tinyDs0 && paras < 0x1000u)
		paras = 0x1000u;
	/* maesta7 DBOOT.SYS（ヘッダ MIDIdriv）: INT F1 AH=80 が DAT バンクを CS:1330 へ読む（CX=0xDEA8）。既定 image+0x300 パラは約 18KB なので 27KB バンクが D98M を壊し CC 全ノートオフだけ残った。 */
	if (size > 18u && memcmp(data + 10, "MIDIdriv", 8) == 0 && paras < 0x1000u)
		paras = 0x1000u;
	/* デバイス CS は 16bit。NMUSE -d8192 -k11264 でも 64KB に収まる。 */
	if (paras > 0x1000u)
		paras = 0x1000u;
	uint16_t seg = 0;
	if (!Alloc(mem, (uint16_t)paras, 8, &seg) || !seg) return 0;
	{
		const unsigned lin = DosLin(seg, 0);
		const unsigned allocBytes = paras * 16u;
		memset(mem + lin, 0, allocBytes);
		memcpy(mem + lin, data, size);
		uint8_t* img = mem + lin;
		/* SYS ヘッダ名でパッチし、カタログ綴り漏れを防ぐ */
		if (size > 18) {
			if (memcmp(img + 10, "$MUSE2$", 7) == 0) {
				if (size > 0xE4u && img[0xE2] == 0xBC && img[0xE3] == 0x5F
					&& img[0xE4] == 0x00) {
					img[0xE3] = 0x00;
					img[0xE4] = 0x3E;
				}
				if (size > 0x555u && img[0x553] == 0xBC && img[0x554] == 0x9F
					&& img[0x555] == 0x00) {
					img[0x554] = 0x00;
					img[0x555] = 0x2E;
				}
			}
			if (memcmp(img + 10, "$NMUSE$", 7) == 0
				&& size > 0x648u && img[0x647] == 0xFF && img[0x648] == 0xFF) {
				/* rakuichi: 未プローブポート番兵。プリセット 188h／INT14 スロット。IN 288/088/188 が全部オープンバスに見えても INIT が植込を飛ばさない。ymfm busy スピンを NOP。 */
				img[0x647] = 0x88;
				img[0x648] = 0x01;
				if (size > 0x654u && img[0x653] == 0 && img[0x654] == 0)
					img[0x653] = 0x50;
				for (unsigned i = 0; i + 4 < size && i + 4 < 0x1200u; i++) {
					if (img[i] == 0xEC && img[i + 1] == 0xD0 && img[i + 2] == 0xD0
						&& img[i + 3] == 0x72 && img[i + 4] == 0xFB) {
						img[i + 3] = 0x90;
						img[i + 4] = 0x90;
						break;
					}
				}
			}
		}
	}
	*outSeg = seg;
	if (outStratOff) *outStratOff = stratEarly;
	if (outIntrOff) *outIntrOff = intrEarly;

	/* Cave／DS=CS／setvect CS パッチは MDR のみ。他の tiny モデル SYS（現状なし）に INT14 ロック付け替えやエンベロープクランプを付けない。上の 64KB DGROUP 確保はどの tinyDs0 にも適用。 */
	int isMdr = 0;
	if (size >= 18) {
		const unsigned char* nm = data + 10;
		if ((nm[0] | 32) == 'm' && (nm[1] | 32) == 'd' && (nm[2] | 32) == 'r')
			isMdr = 1;
	}

	if (tinyDs0 && isMdr) {
		uint8_t* img = mem + DosLin(seg, 0);
		/* Tiny モデル MSC INT 14／INT 40: PUSHAW のあと DS=CS、SS は呼び出し元（COM）のまま。ローカルは COM スタック、near ポインタは DGROUP。C の stos/malloc が COM BSS を塗り、CPU が後で実行（#UD at 2002:0FAD）。デバイス INIT は既に SS=CS／SP=21F0。C ハンドラは切替しない。64KB DGROUP 末尾へ cave: サイト毎 SS:SP。ハンドラ SP は INIT スタックより 4KB 上。INT 14 プレーヤループの STI を NOP し、ネスト tick が外フレームを IRET しない（[1AC0] が 1 で固まる）。 */
		{
			unsigned proOff[4], epiOff[4], nPro = 0, nEpi = 0;
			for (unsigned i = 0; i + 11 <= size && nPro < 4; i++) {
				if (img[i] == 0x60 && img[i + 1] == 0x1E && img[i + 2] == 0x06
					&& img[i + 3] == 0x8B && img[i + 4] == 0xEC
					&& img[i + 5] == 0xB8 && img[i + 6] == 0 && img[i + 7] == 0
					&& img[i + 8] == 0x8E && img[i + 9] == 0xD8
					&& img[i + 10] == 0xFC)
					proOff[nPro++] = i;
			}
			for (unsigned i = 0; i + 6 <= size && nEpi < 4; i++) {
				if (img[i] == 0x8B && img[i + 1] == 0xE5 && img[i + 2] == 0x07
					&& img[i + 3] == 0x1F && img[i + 4] == 0x61 && img[i + 5] == 0xCF)
					epiOff[nEpi++] = i;
			}
			/* INIT の `MOV SP,imm / MOV SS,AX` が C DGROUP スタック。ヒープ（イメージ先）と下へ伸びるスタックが乗らないよう cave をその頂より上へ。 */
			uint16_t dgroupSp = 0x21F0;
			for (unsigned i = 0; i + 5 < 48u && (unsigned)intrEarly + i + 5 < size; i++) {
				const unsigned o = (unsigned)intrEarly + i;
				if (img[o] == 0xBC && img[o + 3] == 0x8E && img[o + 4] == 0xD0) {
					dgroupSp = (uint16_t)(img[o + 1] | (img[o + 2] << 8));
					break;
				}
			}
			unsigned cave = 0xF000u;
			const unsigned need = 0x80u * nPro + 0x40u * nPro + 0x40u;
			if (dgroupSp >= 0xE000u)
				cave = ((unsigned)dgroupSp + 16u) & ~15u;
			/* INIT SP（gintetsu 0x21F0）はハンドラ SP（21BC 次いで 31BC）を追う #UD より 52 バイト上。ISR スタックを INIT SP より 4KB 上に。E000 へ置くと初ノートが死ぬ（near-ptr BSS／ヒープ歩き）。ヒープは INIT SP より下。 */
			uint16_t handlerSp = dgroupSp;
			if (cave >= 0xF000u && dgroupSp + 0x2000u < cave)
				handlerSp = (uint16_t)(dgroupSp + 0x1000u);
			if (nPro >= 1 && nEpi >= 1 && cave + need < 0x10000u && cave >= size + 16u) {
				auto emit16 = [img](unsigned o, uint16_t v) {
					img[o] = (uint8_t)v;
					img[o + 1] = (uint8_t)(v >> 8);
				};
				/* サイト毎セーブ（ISR vs INT 40）。ネスト ISR が COM SS:SP を上書きすると INT 40 の IRET に必要になる。 */
				const unsigned scratch = cave + 0x80u * nPro + 0x40u * nPro;
				const uint16_t tmpAx = (uint16_t)scratch;
				const uint16_t tmpBx = (uint16_t)(scratch + 2);
				for (unsigned k = 0; k < nPro; k++) {
					const unsigned ent = cave + 0x80u * k;
					const unsigned ex = cave + 0x80u * nPro + 0x40u * k;
					const uint16_t saveSp = (uint16_t)(scratch + 4 + k * 8);
					const uint16_t saveSs = (uint16_t)(saveSp + 2);
					const uint16_t switched = (uint16_t)(saveSp + 4);
					unsigned p = ex;
					img[p++] = 0x8B; img[p++] = 0xE5;
					img[p++] = 0x07; img[p++] = 0x1F; img[p++] = 0x61; img[p++] = 0xFA;
					img[p++] = 0x2E; img[p++] = 0x80; img[p++] = 0x3E;
					emit16(p, switched); p += 2;
					img[p++] = 0x00;
					const unsigned jeAt = p; img[p++] = 0x74; img[p++] = 0x00;
					img[p++] = 0x2E; img[p++] = 0x8B; img[p++] = 0x26;
					emit16(p, saveSp); p += 2;
					img[p++] = 0x2E; img[p++] = 0x8E; img[p++] = 0x16;
					emit16(p, saveSs); p += 2;
					img[p++] = 0x2E; img[p++] = 0xC6; img[p++] = 0x06;
					emit16(p, switched); p += 2;
					img[p++] = 0x00;
					const unsigned stay = p;
					img[p++] = 0xFB; img[p++] = 0xCF;
					img[jeAt + 1] = (uint8_t)(stay - (jeAt + 2));
					unsigned q = ent;
					img[q++] = 0xFA;
					img[q++] = 0x2E; img[q++] = 0xA3; emit16(q, tmpAx); q += 2;
					img[q++] = 0x2E; img[q++] = 0x89; img[q++] = 0x1E; emit16(q, tmpBx); q += 2;
					img[q++] = 0x8C; img[q++] = 0xC8;
					img[q++] = 0x8C; img[q++] = 0xD3;
					img[q++] = 0x3B; img[q++] = 0xC3;
					img[q++] = 0x2E; img[q++] = 0xA1; emit16(q, tmpAx); q += 2;
					img[q++] = 0x2E; img[q++] = 0x8B; img[q++] = 0x1E; emit16(q, tmpBx); q += 2;
					const unsigned jeAlr = q; img[q++] = 0x74; img[q++] = 0x00;
					img[q++] = 0x2E; img[q++] = 0x89; img[q++] = 0x26;
					emit16(q, saveSp); q += 2;
					img[q++] = 0x2E; img[q++] = 0x8C; img[q++] = 0x16;
					emit16(q, saveSs); q += 2;
					img[q++] = 0x2E; img[q++] = 0xC6; img[q++] = 0x06;
					emit16(q, switched); q += 2;
					img[q++] = 0x01;
					img[q++] = 0x2E; img[q++] = 0xA3; emit16(q, tmpAx); q += 2;
					img[q++] = 0x8C; img[q++] = 0xC8;
					img[q++] = 0x8E; img[q++] = 0xD0;
					img[q++] = 0xBC; emit16(q, handlerSp); q += 2;
					img[q++] = 0x2E; img[q++] = 0xA1; emit16(q, tmpAx); q += 2;
					const unsigned already = q;
					img[jeAlr + 1] = (uint8_t)(already - (jeAlr + 2));
					img[q++] = 0x60; img[q++] = 0x1E; img[q++] = 0x06;
					img[q++] = 0x8B; img[q++] = 0xEC;
					img[q++] = 0x0E; img[q++] = 0x1F;
					img[q++] = 0x0E; img[q++] = 0x07;
					img[q++] = 0xFC;
					img[q++] = 0xE9;
					const unsigned cont = proOff[k] + 11u;
					emit16(q, (uint16_t)(cont - (q + 2)));
					const unsigned po = proOff[k];
					img[po] = 0xE9;
					emit16(po + 1, (uint16_t)(ent - (po + 3)));
					for (unsigned z = 3; z < 11; z++)
						img[po + z] = 0x90;
					if (k < nEpi && epiOff[k] >= proOff[0]) {
						const unsigned eo = epiOff[k];
						img[eo] = 0xE9;
						emit16(eo + 1, (uint16_t)(ex - (eo + 3)));
						img[eo + 3] = 0x90; img[eo + 4] = 0x90; img[eo + 5] = 0x90;
					}
				}
				/* INT 14 プレーヤは call 0x9d0／0xde6 周りを STI し、ネスト tick が [1A12] を補充する。そのネスト IRET が COM SS:SP（switched=1）を戻し、外フレームを [1AC0]=1 のまま捨てる。STI を NOP。この IRQ が既に足した tick は走り、次 IRQ が次バッチを取る。 */
				if (nPro >= 1 && epiOff[0] > proOff[0] + 11u) {
					for (unsigned i = proOff[0] + 11u; i < epiOff[0]; i++) {
						if (img[i] == 0xFB)
							img[i] = 0x90;
					}
					/* JNZ 飛ばしは `MOV [lock],0` の 6 バイト先（PIC 復帰またはエピローグ）。ネスト／捨てられたフレームはロックを残し、以降の tick は TAIL のみ。ロック番地は 7KB MDR.EXE で 1AC0、wlfpk で 5FD2。 */
					unsigned clrLock = 0;
					for (unsigned i = proOff[0] + 11u; i + 6 <= epiOff[0]; i++) {
						if (img[i] == 0xC7 && img[i + 1] == 0x06
							&& img[i + 4] == 0 && img[i + 5] == 0)
							clrLock = i;
					}
					if (clrLock) {
						for (unsigned i = proOff[0] + 11u; i + 1 < clrLock; i++) {
							if (img[i] != 0x75)
								continue;
							const unsigned dest = i + 2u + (unsigned)img[i + 1];
							if (dest == clrLock + 6u)
								img[i + 1] = (uint8_t)(clrLock - (i + 2u));
						}
					}
				}
				/* エンベロープ CALL [bx+stateTable]: スロット 5-7 は保存 INT 14 ベクタ（0028／tramp 0060）と IRQ 武装フラグに再利用した BSS。状態 5 はデバイス INIT（`MOV SP,21F0`）を near-call し、handlerSp-0x34 の ISR フレームがコードとして取られる（#UD 0x64 at 31BC。wlfpk 48D6）。 */
				for (unsigned i = 0; i + 9 < size; i++) {
					if (img[i] != 0x8B || img[i + 1] != 0x5F || img[i + 2] != 0x06
						|| img[i + 3] != 0xD1 || img[i + 4] != 0xE3
						|| img[i + 5] != 0xFF || img[i + 6] != 0x97)
						continue;
					const uint16_t table = (uint16_t)(img[i + 7] | (img[i + 8] << 8));
					const unsigned clamp = scratch + 0x20u;
					if (clamp + 16u >= 0x10000u)
						break;
					unsigned c = clamp;
					img[c++] = 0x83; img[c++] = 0xFB; img[c++] = 0x05;
					img[c++] = 0x73; img[c++] = 0x06;
					img[c++] = 0xD1; img[c++] = 0xE3;
					img[c++] = 0xFF; img[c++] = 0x97;
					emit16(c, table); c += 2;
					img[c++] = 0xE9;
					const unsigned back = i + 9u;
					emit16(c, (uint16_t)(back - (c + 2)));
					img[i + 3] = 0xE9;
					emit16(i + 4, (uint16_t)(clamp - (i + 6)));
					img[i + 6] = 0x90; img[i + 7] = 0x90; img[i + 8] = 0x90;
					break;
				}
			}
		}
		for (unsigned i = 0; i + 4 < size; i++) {
			if (img[i] != 0xB8 || img[i + 1] != 0 || img[i + 2] != 0)
				continue;
			if (img[i + 3] == 0x8E && img[i + 4] == 0xD8) {
				/* MOV AX,CS; MOV ES,AX; MOV DS,AX — INT 40/14 は呼び出し元 ES（COM）を保ち、C stos/malloc がグルー（2002:0FAD）を塗り CPU が実行（#UD 0F）。続く CLD を食っても安全: MSC memcpy は自分の周りで STD/CLD。DF はクリア開始。 */
				if (i + 5 < size && img[i + 5] == 0xFC) {
					img[i] = 0x8C; img[i + 1] = 0xC8;
					img[i + 2] = 0x8E; img[i + 3] = 0xC0;
					img[i + 4] = 0x8E; img[i + 5] = 0xD8;
				} else {
					img[i] = 0x8C; img[i + 1] = 0xC8; img[i + 2] = 0x90;
				}
				continue;
			}
			for (unsigned j = 3; j < 16u && i + j + 1 < size; j++) {
				if (img[i + j] == 0x8E && img[i + j + 1] == 0xD0) {
					img[i] = 0x8C; img[i + 1] = 0xC8; img[i + 2] = 0x90;
					break;
				}
			}
		}
		/* C setvect(vec, MK_FP(0, off)): PUSH 0 / PUSH off / PUSH vec / CALL。INT 14（OPN ISR）と INT 40/2F（play API）はハンドラを 0000:offset に置いた。グルー INT 40 が IVT/BDA を実行し #UD。 */
		for (unsigned i = 0; i + 10 < size; i++) {
			if (img[i] != 0x68 || img[i + 1] != 0 || img[i + 2] != 0)
				continue;
			if (img[i + 3] != 0x68)
				continue;
			const unsigned off = (unsigned)(img[i + 4] | (img[i + 5] << 8));
			if (off < 0x20 || off >= size)
				continue;
			unsigned vec = 0, after = 0;
			if (img[i + 6] == 0x6A) {
				vec = img[i + 7];
				after = i + 8;
			} else if (img[i + 6] == 0x68 && img[i + 8] == 0) {
				vec = img[i + 7];
				after = i + 9;
			} else
				continue;
			if (vec == 0 || after >= size || img[after] != 0xE8)
				continue;
			img[i] = 0x0E; img[i + 1] = 0x90; img[i + 2] = 0x90;
		}
		/* wlfpk バンク内ボイス: cmd1/cmd2 が [609E]=4864／[60A4]=5864 を書いて DS。グルー INT 2F BX=1/2 はそれを行うが、マジック検査を逃すと far ptr が 0:0、オペコード 89 が IVT を添字し、後の tick が 4864+0x72 の 0F 0F を実行（#UD 48D6）。即値が既に名指す BSS ワードを植える。 */
		for (unsigned i = 0; i + 6 <= size; i++) {
			if (img[i] != 0xC7 || img[i + 1] != 0x06)
				continue;
			if (!((img[i + 4] == 0x64 && img[i + 5] == 0x48)
				|| (img[i + 4] == 0x64 && img[i + 5] == 0x58)))
				continue;
			const unsigned off = (unsigned)(img[i + 2] | (img[i + 3] << 8));
			if (off < size || off + 4u >= 0x10000u)
				continue;
			img[off] = img[i + 4];
			img[off + 1] = img[i + 5];
			img[off + 2] = (uint8_t)seg;
			img[off + 3] = (uint8_t)(seg >> 8);
		}
	}
	return 1;
}

/* データを載せる */
void CEmuDos98::LoadOverlay(uint8_t* mem, const unsigned char* image, unsigned imageSize, uint16_t loadSeg, uint16_t reloc) const
{
	if (!mem || !image) return;
	if (imageSize >= 0x20 && image[0] == 'M' && image[1] == 'Z') {
		const uint16_t bytesLast = Rd16(image, 0x02);
		const uint16_t pages = Rd16(image, 0x04);
		const uint16_t nreloc = Rd16(image, 0x06);
		const uint16_t hdrParas = Rd16(image, 0x08);
		const uint16_t relocOff = Rd16(image, 0x18);
		const unsigned hdrSize = (unsigned)hdrParas * 16u;
		unsigned imageBytes = (unsigned)pages * 512u;
		if (bytesLast != 0)
			imageBytes = imageBytes - 512u + (unsigned)bytesLast;
		/* パック ADVH/EXEPACK stub は MZ 宣言サイズより先に居ることが多い（dragon_c_98: ヘッダは本体 18356、ファイルは 18408）。切ると decrypt/jmp-0010 stub が落ち F1 がフックしない。 */
		if (imageBytes < imageSize)
			imageBytes = imageSize;
		const unsigned loadSize = (imageBytes > hdrSize) ? (imageBytes - hdrSize) : 0;
		const unsigned dst = DosLin(loadSeg, 0);
		const unsigned srcEnd = (hdrSize + loadSize < imageSize) ? (hdrSize + loadSize) : imageSize;
		if (srcEnd > hdrSize)
			memcpy(mem + dst, image + hdrSize, srcEnd - hdrSize);
		for (uint16_t i = 0; i < nreloc; i++) {
			const unsigned e = (unsigned)relocOff + (unsigned)i * 4u;
			if (e + 4 > imageSize) break;
			const uint16_t off = Rd16(image, e);
			const uint16_t sg = Rd16(image, e + 2);
			const unsigned target = DosLin((uint16_t)(loadSeg + sg), off);
			Wr16(mem, target, (uint16_t)(Rd16(mem, target) + reloc));
		}
	} else {
		memcpy(mem + DosLin(loadSeg, 0), image, imageSize);
	}
}

/* CEmuDos98::AllocBlock の実装 */
int CEmuDos98::AllocBlock(uint8_t* mem, uint16_t paras, uint16_t* outSeg)
{
	return Alloc(mem, paras, pspSeg_ ? pspSeg_ : 8, outSeg);
}

/* CEmuDos98::TrapVector の実装 */
int CEmuDos98::TrapVector(uint16_t cs, uint16_t ip, uint8_t* outVec) const
{
	if (cs != DOS98_TRAMP_SEG || ip >= 512 || (ip & 1)) return 0;
	if (outVec) *outVec = (uint8_t)(ip / 2);
	return 1;
}

/* CEmuDos98::VectorInstalled の実装 */
int CEmuDos98::VectorInstalled(uint8_t vec) const
{
	return installed_[vec] ? 1 : 0;
}

/* CEmuDos98::Ah の実装 */
uint8_t CEmuDos98::Ah() const
{
	return (uint8_t)(np2_reg_get(NP2_R_AX) >> 8);
}

/* CEmuDos98::Al の実装 */
uint8_t CEmuDos98::Al() const
{
	return (uint8_t)(np2_reg_get(NP2_R_AX) & 0xff);
}

/* CEmuDos98::SetAl の実装 */
void CEmuDos98::SetAl(uint8_t v)
{
	uint16_t ax = np2_reg_get(NP2_R_AX);
	np2_reg_set(NP2_R_AX, (uint16_t)((ax & 0xff00) | v));
}

/* SetAh の実装 */
static void SetAh(uint8_t v)
{
	uint16_t ax = np2_reg_get(NP2_R_AX);
	np2_reg_set(NP2_R_AX, (uint16_t)((ax & 0x00ff) | ((uint16_t)v << 8)));
}

/* CEmuDos98::SetCf の実装 */
void CEmuDos98::SetCf(int on)
{
	uint16_t f = np2_reg_get(NP2_R_FLAGS);
	if (on) f = (uint16_t)(f | FLAG_CF);
	else f = (uint16_t)(f & ~FLAG_CF);
	np2_reg_set(NP2_R_FLAGS, f);
}

/* SetZf の実装 */
static void SetZf(int on)
{
	uint16_t f = np2_reg_get(NP2_R_FLAGS);
	if (on) f = (uint16_t)(f | FLAG_ZF);
	else f = (uint16_t)(f & ~FLAG_ZF);
	np2_reg_set(NP2_R_FLAGS, f);
}

/* バス読込 */
void CEmuDos98::ReadCstr(const uint8_t* mem, uint16_t seg, uint16_t off, char* out, int outCap) const
{
	if (!out || outCap <= 0) return;
	out[0] = 0;
	if (!mem) return;
	unsigned a = DosLin(seg, off);
	int n = 0;
	while (a < 0x200000 && mem[a] && n < outCap - 1 && n < 128) {
		out[n++] = (char)mem[a++];
	}
	out[n] = 0;
}

/* CEmuDos98::Int18 の実装 */
void CEmuDos98::Int18()
{
	const uint16_t ax = np2_reg_get(NP2_R_AX);
	/* AX=9801 はグルーのアイドル poll であり BIOS 要求ではない */
	if (ax == 0x9801)
		return;
	const uint8_t f = Ah();
	switch (f) {
	case 0x00:
	case 0x01:
		np2_reg_set(NP2_R_AX, 0);
		SetCf(0);
		return;
	/* AH=02 はシフト／ctrl／caps ビットマップを見る。ここでは何も押していない。AL を触らないとドライバが古いレジスタを「修飾キー押し」と読みポーズ／ステップ経路へ入る。 */
	case 0x02:
		np2_reg_set(NP2_R_AX, (uint16_t)(ax & 0xFF00));
		SetCf(0);
		return;
	/* キーボード初期化／センス（SCBIOS AH=03）と残りの CRT/GDC 面。NP2 は成功で返す。呼び出し側は CF を「BIOS 無し」と見る。 */
	case 0x03:
	case 0x04:
	case 0x05:
		SetAl(0);
		SetCf(0);
		return;
	case 0x0A: case 0x0B: case 0x0C: case 0x0D:
	case 0x0E: case 0x0F:
	case 0x10: case 0x11: case 0x12: case 0x13:
	case 0x14: case 0x15: case 0x16:
		SetCf(0);
		return;
	case 0x1A: case 0x1B:
		/* 漢字／CG: INT 1Ah が置く空フォントと同じ。古い ES:BP は次 COM へ歩く（rakuichi INT6 @ 1F07:xxxx）。 */
		np2_reg_set(NP2_R_ES, DOS98_TRAMP_SEG);
		np2_reg_set(NP2_R_BP, 0x0210);
		SetCf(0);
		return;
	case 0x21: case 0x30:
	case 0x40: case 0x41: case 0x42: case 0x43:
		SetCf(0);
		return;
	default:
		unhandledInt18_[f] = 1;
		SetCf(0);
		return;
	}
}

/* CEmuDos98::Int21 の実装 */
CEmuDos98Result CEmuDos98::Int21(uint8_t* mem)
{
	const uint8_t f = Ah();
	switch (f) {
	case 0x00:
		/* プログラム終了 — PSP アリーナを解放し、次シェル（MUSIC.EXE 後の olteus 等）が再び Alloc できるようにする */
		FreeBlock(mem, pspSeg_);
		return DOS98_TERMINATED;
	case 0x4C:
		FreeBlock(mem, pspSeg_);
		return DOS98_TERMINATED;
	case 0x31: {
		uint16_t keep = np2_reg_get(NP2_R_DX);
		if (keep < 0x10) keep = 0x10;
		uint16_t err = 0;
		Resize(mem, pspSeg_, keep, &err);
		(void)err;
		return DOS98_RESIDENT;
	}
	case 0x02:
	case 0x06: {
		const uint8_t dl = (uint8_t)(np2_reg_get(NP2_R_DX) & 0xff);
		if (f == 0x06 && dl == 0xFF) {
			SetAl(0);
			uint16_t fl = np2_reg_get(NP2_R_FLAGS);
			fl = (uint16_t)(fl | FLAG_ZF);
			np2_reg_set(NP2_R_FLAGS, fl);
		} else {
			SetAl(dl);
		}
		break;
	}
	case 0x09: {
		uint16_t seg = np2_reg_get(NP2_R_DS);
		uint16_t off = np2_reg_get(NP2_R_DX);
		unsigned a = DosLin(seg, off);
		int n = 0;
		while (a < 0x200000 && mem[a] != '$' && n < 0x4000) {
			a++;
			n++;
		}
		SetAl(0x24);
		break;
	}
	case 0x0A: {
		/* バッファ入力。カウントバイトをゴミのままにするとローダが幽霊行待ち。「空＋CR」を格納。 */
		const unsigned a = DosLin(np2_reg_get(NP2_R_DS), np2_reg_get(NP2_R_DX));
		if (a + 2 < 0x200000) {
			mem[a + 1] = 0;
			mem[a + 2] = 0x0D;
		}
		SetAl(0x0D);
		SetCf(0);
		break;
	}
	case 0x0E:
		SetAl(1); /* 最終ドライブ = A: */
		SetCf(0);
		break;
	case 0x01:
	case 0x07:
	case 0x08:
		SetAl(0);
		break;
	case 0x0B:
		SetAl(0);
		break;
	case 0x1A:
		dtaSeg_ = np2_reg_get(NP2_R_DS);
		dtaOff_ = np2_reg_get(NP2_R_DX);
		break;
	case 0x2F:
		np2_reg_set(NP2_R_ES, dtaSeg_);
		np2_reg_set(NP2_R_BX, dtaOff_);
		break;
	case 0x25: {
		const uint8_t v = Al();
		const uint16_t seg = np2_reg_get(NP2_R_DS);
		const uint16_t off = np2_reg_get(NP2_R_DX);
		Wr16(mem, (unsigned)v * 4u, off);
		Wr16(mem, (unsigned)v * 4u + 2u, seg);
		installed_[v] = 1;
		instSeg_[v] = seg;
		instOff_[v] = off;
		break;
	}
	case 0x35: {
		const uint8_t v = Al();
		const uint16_t off = Rd16(mem, (unsigned)v * 4u);
		const uint16_t seg = Rd16(mem, (unsigned)v * 4u + 2u);
		np2_reg_set(NP2_R_BX, off);
		np2_reg_set(NP2_R_ES, seg);
		break;
	}
	case 0x30:
		np2_reg_set(NP2_R_AX, 0x0005);
		np2_reg_set(NP2_R_BX, 0);
		np2_reg_set(NP2_R_CX, 0);
		break;
	case 0x19:
		SetAl(0x02);
		break;
	case 0x2A:
		np2_reg_set(NP2_R_CX, 1996);
		np2_reg_set(NP2_R_DX, 0x0C18);
		SetAl(0);
		break;
	case 0x2C:
		np2_reg_set(NP2_R_CX, 0);
		np2_reg_set(NP2_R_DX, 0);
		break;
	case 0x33:
		SetAl(0);
		break;
	case 0x50:
		pspSeg_ = np2_reg_get(NP2_R_BX);
		break;
	case 0x51:
	case 0x62:
		np2_reg_set(NP2_R_BX, pspSeg_);
		break;
	case 0x52:
		np2_reg_set(NP2_R_ES, DOS98_SYSVARS_SEG);
		np2_reg_set(NP2_R_BX, DOS98_LOL_OFF);
		break;
	case 0x3D: {
		char name[DOS98_NAME];
		ReadCstr(mem, np2_reg_get(NP2_R_DS), np2_reg_get(NP2_R_DX), name, (int)sizeof(name));
		if (FindFile(name)) {
			const uint16_t h = AllocHandle();
			SetHandle(h, name);
			np2_reg_set(NP2_R_AX, h);
			SetCf(0);
		} else {
			np2_reg_set(NP2_R_AX, 0x0002);
			SetCf(1);
		}
		break;
	}
	case 0x3E: {
		uint16_t h = np2_reg_get(NP2_R_BX);
		if (h < DOS98_HANDLE_MAX)
			handles_[h].used = 0;
		SetCf(0);
		break;
	}
	case 0x3F: {
		const uint16_t h = np2_reg_get(NP2_R_BX);
		unsigned count = np2_reg_get(NP2_R_CX);
		/* 1000h 段落の 16bit `SHL CX,4` は 0 にラップ（= 64KB）。fgplay_h／OPNDRV は AH=3F で CX バイトを曲バッファへ読む。0 を「何も読まない」にするとインタプリタが 9A00 の未初期化メモリを走り、最初の FM ノートが固まる。 */
		if (count == 0)
			count = 0x10000u;
		const unsigned dst = DosLin(np2_reg_get(NP2_R_DS), np2_reg_get(NP2_R_DX));
		unsigned n = 0;
		if (h < DOS98_HANDLE_MAX && handles_[h].used) {
			const CEmuDos98File* file = FindFile(handles_[h].name);
			if (file && file->data) {
				const unsigned start = handles_[h].pos;
				n = count;
				if (start >= file->size) n = 0;
				else if (start + n > file->size) n = file->size - start;
				if (n && dst + n <= 0x200000) {
					memcpy(mem + dst, file->data + start, n);
					handles_[h].pos = start + n;
				} else {
					n = 0;
				}
			}
		}
		if (readLogCount_ < 32) {
			readLogHandle_[readLogCount_] = h;
			readLogBytes_[readLogCount_] = n;
			readLogCount_++;
		}
		np2_reg_set(NP2_R_AX, (uint16_t)n);
		SetCf(0);
		break;
	}
	case 0x40: {
		const uint16_t count = np2_reg_get(NP2_R_CX);
		np2_reg_set(NP2_R_AX, count);
		SetCf(0);
		break;
	}
	case 0x42: {
		const uint16_t h = np2_reg_get(NP2_R_BX);
		const uint8_t whence = Al();
		uint16_t seekCx = np2_reg_get(NP2_R_CX);
		uint16_t seekDx = np2_reg_get(NP2_R_DX);
		/* Linel Neverending Story II CODE.COM は Hoot 曲ポート（07E2h）から IN AX,DX し、DX をゼロせず AH=42。原点 0 シークがオフセット 2018 に着地し .BIN ヘッダを飛ばす。実 DOS も同じ。ポート番号はファイルオフセットではない。 */
		if (pcAtBios_ && whence == 0 && seekCx == 0 && seekDx == 0x07E2)
			seekDx = 0;
		const uint32_t off = ((uint32_t)seekCx << 16) | seekDx;
		uint32_t len = 0;
		if (h < DOS98_HANDLE_MAX && handles_[h].used) {
			const CEmuDos98File* file = FindFile(handles_[h].name);
			if (file) len = file->size;
			uint32_t base = 0;
			if (whence == 1) base = handles_[h].pos;
			else if (whence == 2) base = len;
			uint32_t np = base + off;
			if (np > len) np = len;
			handles_[h].pos = np;
			np2_reg_set(NP2_R_AX, (uint16_t)(np & 0xffff));
			np2_reg_set(NP2_R_DX, (uint16_t)(np >> 16));
		}
		SetCf(0);
		break;
	}
	case 0x43: {
		/* ファイル属性の取得／設定 — AIL/HOOT が open 前にプローブ */
		if (Al() == 0) {
			char name[DOS98_NAME];
			ReadCstr(mem, np2_reg_get(NP2_R_DS), np2_reg_get(NP2_R_DX), name, (int)sizeof(name));
			if (FindFile(name)) {
				np2_reg_set(NP2_R_CX, 0x20); /* アーカイブ属性 */
				SetCf(0);
			} else {
				np2_reg_set(NP2_R_AX, 0x0002);
				SetCf(1);
			}
		} else {
			SetCf(0);
		}
		break;
	}
	case 0x44:
		/* デバイス情報取得。全部 DX=0 だとハンドル 0-2 がディスクファイルに見え、コンソール駆動ローダ（HOOT.EXE）がリダイレクト実行と判断し .ADV ドライバを読む前に抜ける。標準ハンドルは CON、残りはファイル。 */
		if (Al() == 0) {
			const uint16_t h = np2_reg_get(NP2_R_BX);
			np2_reg_set(NP2_R_DX, h < 3 ? (uint16_t)0x80D3 : (uint16_t)0x0042);
		}
		SetCf(0);
		break;
	case 0x48: {
		const uint16_t paras = np2_reg_get(NP2_R_BX);
		uint16_t seg = 0;
		if (Alloc(mem, paras, pspSeg_, &seg)) {
			np2_reg_set(NP2_R_AX, seg);
			SetCf(0);
		} else {
			np2_reg_set(NP2_R_AX, 0x0008);
			np2_reg_set(NP2_R_BX, MaxFreeBlock(mem));
			SetCf(1);
		}
		break;
	}
	case 0x49:
		FreeBlock(mem, np2_reg_get(NP2_R_ES));
		SetCf(0);
		break;
	case 0x4A: {
		uint16_t err = 0;
		if (Resize(mem, np2_reg_get(NP2_R_ES), np2_reg_get(NP2_R_BX), &err)) {
			SetCf(0);
		} else {
			np2_reg_set(NP2_R_AX, 0x0008);
			np2_reg_set(NP2_R_BX, err);
			SetCf(1);
		}
		break;
	}
	case 0x4B: {
		const uint8_t subfn = Al();
		char name[DOS98_NAME];
		ReadCstr(mem, np2_reg_get(NP2_R_DS), np2_reg_get(NP2_R_DX), name, (int)sizeof(name));
		const unsigned pb = DosLin(np2_reg_get(NP2_R_ES), np2_reg_get(NP2_R_BX));
		if (subfn == 0x03) {
			const uint16_t loadSeg = Rd16(mem, pb);
			const uint16_t reloc = Rd16(mem, pb + 2);
			const CEmuDos98File* file = FindFile(name);
			if (file && file->data) {
				LoadOverlay(mem, file->data, file->size, loadSeg, reloc);
				SetCf(0);
			} else {
				np2_reg_set(NP2_R_AX, 0x0002);
				SetCf(1);
			}
		} else if (subfn == 0x00) {
			/* ロードして実行。常駐音楽ドライバを spawn するシェルが使う。CS:IP は MZ 入口（PACKED stub 含む）。CONTINUE を返すと HLT 経路が子のスタックから IRET（LoadExe が既に SS:SP を置換）し #UD。 */
			const CEmuDos98File* file = FindFile(name);
			int ok = 0;
			if (file && file->data) {
				const int isExe = (file->size >= 2 && file->data[0] == 'M'
					&& file->data[1] == 'Z');
				ok = isExe
					? LoadExe(mem, file->data, file->size, "")
					: LoadCom(mem, file->data, file->size, "");
			}
			if (ok) {
				SetCf(0);
				return DOS98_EXEC;
			} else {
				np2_reg_set(NP2_R_AX, 0x0002);
				SetCf(1);
			}
		} else {
			np2_reg_set(NP2_R_AX, 0x0001);
			SetCf(1);
		}
		break;
	}
	case 0x2D: /* 時刻設定 — ここは時計を持たない。受け入れるだけ */
		SetAl(0);
		SetCf(0);
		break;
	case 0x34:
		/* InDOS フラグ。タイマフックを入れるドライバは ISR から DOS を触る前にこれを見る。ゴミポインタが非ゼロだとフックが何もしない。Sysvars 0x04 は予備のゼロバイト。 */
		np2_reg_set(NP2_R_ES, DOS98_SYSVARS_SEG);
		np2_reg_set(NP2_R_BX, 0x0004);
		mem[DosLin(DOS98_SYSVARS_SEG, 0x0004)] = 0;
		SetCf(0);
		break;
	case 0x36: {
		/* 空きディスク。約 1MB RAM ディスクとして報告。呼び出し側は「ログ／ダンプの余地があるか」だけ見る。 */
		np2_reg_set(NP2_R_AX, 4);      /* クラスタあたりセクタ */
		np2_reg_set(NP2_R_BX, 0x0400); /* 空きクラスタ */
		np2_reg_set(NP2_R_CX, 512);    /* セクタあたりバイト */
		np2_reg_set(NP2_R_DX, 0x0800); /* 総クラスタ */
		SetCf(0);
		break;
	}
	case 0x37:
		/* SWITCHAR。AL=0 取得、AL=1 設定。他は未定義。 */
		if (Al() == 0) {
			np2_reg_set(NP2_R_DX, (np2_reg_get(NP2_R_DX) & 0xFF00) | '/');
			SetAl(0);
		} else {
			SetAl(0);
		}
		SetCf(0);
		break;
	case 0x47: {
		/* カレントディレクトリを DS:SI へ。エミュレートドライブはルートだけなので空文字。書かないと呼び出し側がスタックゴミへファイル名を足し、開けなくなる。 */
		const uint16_t seg = np2_reg_get(NP2_R_DS);
		const uint16_t off = np2_reg_get(NP2_R_SI);
		mem[DosLin(seg, off)] = 0;
		np2_reg_set(NP2_R_AX, 0x0100);
		SetCf(0);
		break;
	}
	case 0x4E: {
		char pat[DOS98_NAME];
		ReadCstr(mem, np2_reg_get(NP2_R_DS), np2_reg_get(NP2_R_DX), pat,
			(int)sizeof(pat));
		/* DOS は裸の名前で照合。アーカイブは平坦なのでパス接頭はここでは興味なし。 */
		const char* base = pat;
		for (const char* p = pat; *p; p++)
			if (*p == '\\' || *p == '/' || *p == ':') base = p + 1;
		UpperCopy(findPat_, (int)sizeof(findPat_), base);
		findNext_ = 0;
		if (!FindMatch(mem)) {
			np2_reg_set(NP2_R_AX, 0x0012); /* これ以上ファイル無し */
			SetCf(1);
		} else {
			SetCf(0);
		}
		break;
	}
	case 0x4F:
		if (!FindMatch(mem)) {
			np2_reg_set(NP2_R_AX, 0x0012);
			SetCf(1);
		} else {
			SetCf(0);
		}
		break;
	case 0x4D:
		np2_reg_set(NP2_R_AX, 0);
		SetCf(0);
		break;
	case 0x59:
		np2_reg_set(NP2_R_AX, 0);
		np2_reg_set(NP2_R_BX, 0);
		SetCf(0);
		break;
	case 0x63: {
		/* DBCS リードバイト表。空（00,00）= SBCS。DS:SI は有効でなければならない。 */
		const unsigned tab = DosLin(DOS98_TRAMP_SEG, 0x0230);
		mem[tab] = 0;
		mem[tab + 1] = 0;
		np2_reg_set(NP2_R_DS, DOS98_TRAMP_SEG);
		np2_reg_set(NP2_R_SI, 0x0230);
		SetCf(0);
		break;
	}
	case 0x65:
		np2_reg_set(NP2_R_CX, 0);
		SetCf(0);
		break;
	case 0x58:
		/* 割り当て戦略／UMB リンク。格納するが実行しない。このアリーナに上位メモリは無いのでどの戦略も同じ。 */
		switch (Al()) {
		case 0x00: np2_reg_set(NP2_R_AX, allocStrategy_); break;
		case 0x01: allocStrategy_ = np2_reg_get(NP2_R_BX); break;
		case 0x02: np2_reg_set(NP2_R_AX, 0); break;
		case 0x03: break;
		default:
			np2_reg_set(NP2_R_AX, 0x0001);
			SetCf(1);
			return DOS98_CONTINUE;
		}
		SetCf(0);
		break;
	default:
		unhandledFn_[Ah()] = 1;
		SetCf(0);
		break;
	}
	return DOS98_CONTINUE;
}

/* 既に大文字化した 8.3 風名前へのワイルドカード。DOS 意味: '?' は 1 文字、'*' は名前または拡張子の末尾まで。 */
static int Dos98NameMatch(const char* pat, const char* name)
{
	/* フィールド毎に比較し "*.*" と "FOO.*" をシェル glob ではなく DOS のように振る舞わせる */
	char pn[13], pe[13], nn[13], ne[13];
	auto split = [](const char* s, char* stem, char* ext) {
		const char* dot = strrchr(s, '.');
		size_t n = dot ? (size_t)(dot - s) : strlen(s);
		if (n > 12) n = 12;
		memcpy(stem, s, n);
		stem[n] = 0;
		const char* e = dot ? dot + 1 : "";
		strncpy_s(ext, 13, e, _TRUNCATE);
	};
	split(pat, pn, pe);
	split(name, nn, ne);
	auto one = [](const char* p, const char* s) {
		for (;;) {
			if (*p == '*') return 1;
			if (!*p) return *s ? 0 : 1;
			if (!*s) {
				/* 末尾 '?' は短い名前にマッチする。DOS は 8/3 へパッドする */
				for (; *p; p++)
					if (*p != '?') return 0;
				return 1;
			}
			if (*p != '?' && *p != *s) return 0;
			p++;
			s++;
		}
	};
	return one(pn, nn) && one(pe, ne);
}

/* CEmuDos98::FindMatch の実装 */
int CEmuDos98::FindMatch(uint8_t* mem)
{
	for (; findNext_ < fileCount_; findNext_++) {
		char up[DOS98_NAME];
		UpperCopy(up, (int)sizeof(up), files_[findNext_].name);
		if (!Dos98NameMatch(findPat_, up)) continue;
		const unsigned d = DosLin(dtaSeg_, dtaOff_);
		memset(mem + d, 0, 0x2B);
		mem[d + 0x15] = 0x20; /* アーカイブ属性 */
		Wr16(mem, d + 0x16, 0x6000); /* 時刻 12:00:00 */
		Wr16(mem, d + 0x18, 0x2101); /* 日付 1996-08-01 */
		Wr16(mem, d + 0x1A, (uint16_t)(files_[findNext_].size & 0xFFFF));
		Wr16(mem, d + 0x1C, (uint16_t)(files_[findNext_].size >> 16));
		strncpy_s((char*)(mem + d + 0x1E), 13, up, _TRUNCATE);
		findNext_++;
		np2_reg_set(NP2_R_AX, 0);
		return 1;
	}
	return 0;
}

/* CEmuDos98::ServiceInt の実装 */
CEmuDos98Result CEmuDos98::ServiceInt(uint8_t* mem, uint8_t vec)
{
	/* traceOn_ 1 は新しい呼び出し、2 は古い方を残す。欠サービスで回るグルーはリングを埋め、スピン開始理由は末尾ではなく先頭が要る。 */
	Call* rec = NULL;
	/* hoot グルーは INT 18 AX=9801 でアイドルし数十万回呼ぶ。リングの他が埋もれる。 */
	const int idlePoll = (vec == 0x18 && np2_reg_get(NP2_R_AX) == 0x9801)
		|| (vec == 0x21 && Ah() == 0x06)
		|| (vec == 0x06);
		if (trapVec_ == 0 && mem
		&& (vec <= 0x07 || vec == 0x0C || vec == 0x0D)) {
		const unsigned f = DosLin(np2_reg_get(NP2_R_SS), np2_reg_get(NP2_R_SP));
		trapVec_ = vec;
		trapIp_ = Rd16(mem, f);
		trapCs_ = Rd16(mem, f + 2);
		/* IBM BIOS の IRET が #UD/#DE を同じ IP へ戻すと CPU がライブロック（s201 CODE.COM dosmiss=int06）。悪いバイトのあと再開する DOS abort stub のように故障オペコードを飛ばす。 */
		if (pcAtBios_ && (vec == 0x06 || vec == 0x00)) {
			const uint16_t skip = (vec == 0x00) ? 2u : 1u;
			Wr16(mem, f, (uint16_t)(trapIp_ + skip));
		}
	}
	if (!pcAtBios_ && vec == 0x06 && CEmuPc98ValkyKeepIrq0() && mem) {
		/* すべての #UD: 初回だけ飛ばすと次プレフィクス（GMD 64h）がループする */
		const unsigned f = DosLin(np2_reg_get(NP2_R_SS), np2_reg_get(NP2_R_SP));
		const uint16_t ip = Rd16(mem, f);
		const uint16_t cs = Rd16(mem, f + 2);
		uint16_t skip = 1;
		const unsigned lin = DosLin(cs, ip);
		if (lin < 0x200000u) {
			const uint8_t op = mem[lin];
			if (op == 0xC0 || op == 0xC1 || op == 0xE8 || op == 0xE9)
				skip = 3;
			else if (op == 0x0F)
				skip = 2;
		}
		Wr16(mem, f, (uint16_t)(ip + skip));
	}
	if (!idlePoll
		&& (traceOn_ == 1 || (traceOn_ == 2 && traceCount_ < kTraceMax))) {
		rec = &trace_[traceCount_++ % kTraceMax];
		rec->vec = vec;
		rec->ax = np2_reg_get(NP2_R_AX);
		rec->bx = np2_reg_get(NP2_R_BX);
		rec->cx = np2_reg_get(NP2_R_CX);
		rec->dx = np2_reg_get(NP2_R_DX);
		rec->cf = -1;
		rec->name[0] = 0;
		/* トランポリンの IRET フレームは呼び出し元 CS:IP */
		{
			const unsigned f = DosLin(np2_reg_get(NP2_R_SS), np2_reg_get(NP2_R_SP));
			rec->ip = Rd16(mem, f);
			rec->cs = Rd16(mem, f + 2);
		}
		/* AH=3D/4B/4E は DS:DX で対象を名指す。他はせず、無フィルタ読みはノイズ。 */
		const uint8_t ah = (uint8_t)(rec->ax >> 8);
		if (vec == 0x21 && (ah == 0x3d || ah == 0x4b || ah == 0x4e))
			ReadCstr(mem, np2_reg_get(NP2_R_DS), rec->dx, rec->name,
				(int)sizeof(rec->name));
	}
	else if (traceOn_ && !idlePoll)
		traceCount_++;
	const CEmuDos98Result res = ServiceIntInner(mem, vec);
	if (rec) {
		rec->rax = np2_reg_get(NP2_R_AX);
		rec->cf = (np2_reg_get(NP2_R_FLAGS) & FLAG_CF) ? 1 : 0;
	}
	return res;
}

/* CEmuDos98::ServiceIntInner の実装 */
CEmuDos98Result CEmuDos98::ServiceIntInner(uint8_t* mem, uint8_t vec)
{
	switch (vec) {
	case 0x20:
		FreeBlock(mem, pspSeg_);
		return DOS98_TERMINATED;
	case 0x27:
		return DOS98_RESIDENT;
	case 0x21:
		return Int21(mem);
	case 0x18:
		Int18();
		return DOS98_CONTINUE;
	/* INT 10h–16h: PC-98 ポート上の IBM 風呼び出し（FMXP INT 15、SCBIOS キーボード、AIL HOOT）。成功 no-op。CF セットは「BIOS 無し」だった。 */
	case 0x10:
		switch (Ah()) {
		case 0x0F:
			np2_reg_set(NP2_R_AX, 0x5003); /* 80 桁、モード 3 */
			np2_reg_set(NP2_R_BX, (uint16_t)(np2_reg_get(NP2_R_BX) & 0x00FF));
			break;
		case 0x03:
			np2_reg_set(NP2_R_CX, 0);
			np2_reg_set(NP2_R_DX, 0);
			break;
		default:
			break;
		}
		SetCf(0);
		return DOS98_CONTINUE;
	case 0x11:
		np2_reg_set(NP2_R_AX, Rd16(mem, 0x410));
		SetCf(0);
		return DOS98_CONTINUE;
	case 0x12:
		np2_reg_set(NP2_R_AX, Rd16(mem, 0x413));
		SetCf(0);
		return DOS98_CONTINUE;
	case 0x13:
		/* IBM ディスク BIOS。PC-98 は INT 1Bh。そこで 13h に答えるとトランポリンにベクタを残したグルーが驚く。 */
		if (!pcAtBios_) {
			unhandledVec_[vec] = 1;
			return DOS98_CONTINUE;
		}
		switch (Ah()) {
		case 0x00:
		case 0x01:
			SetAh(0);
			SetCf(0);
			break;
		case 0x08:
			np2_reg_set(NP2_R_AX, 0);
			np2_reg_set(NP2_R_BX, 0x0004); /* 1.44M 種別 */
			np2_reg_set(NP2_R_CX, 0x4F12); /* 80 シリンダ、18 セクタ */
			np2_reg_set(NP2_R_DX, 0x0101); /* 2 ヘッド、1 ドライブ */
			SetCf(0);
			break;
		case 0x15:
			SetAh(0); /* DASD 無し */
			SetCf(0);
			break;
		default:
			SetAh(0x01);
			SetCf(1);
			break;
		}
		return DOS98_CONTINUE;
	case 0x14:
		if (!pcAtBios_) {
			unhandledVec_[vec] = 1;
			return DOS98_CONTINUE;
		}
		/* 8250 BIOS。BDA に COM 基点が無いのでステータスは idle。受信は 3F8h で回らずタイムアウト。 */
		switch (Ah()) {
		case 0x02:
			SetAh(0x80);
			SetCf(1);
			break;
		default:
			SetAh(0x20);
			SetAl(0x10);
			SetCf(0);
			break;
		}
		return DOS98_CONTINUE;
	case 0x15:
		if (!pcAtBios_) {
			if (Ah() == 0x88)
				np2_reg_set(NP2_R_AX, 0);
			else
				SetAl(0);
			SetCf(0);
			return DOS98_CONTINUE;
		}
		switch (Ah()) {
		case 0x88:
			np2_reg_set(NP2_R_AX, 0); /* ISA 1MB、拡張無し */
			SetCf(0);
			break;
		case 0xC0:
			/* システム構成。CF=0 で ES:BX 未触だと wibarm/tfatman がランダムワードをモデル／サブモデルと読む。 */
			np2_reg_set(NP2_R_ES, 0xF000);
			np2_reg_set(NP2_R_BX, 0xE000);
			SetAh(0);
			SetCf(0);
			break;
		case 0x86: {
			const uint32_t us = ((uint32_t)np2_reg_get(NP2_R_CX) << 16)
				| (uint32_t)np2_reg_get(NP2_R_DX);
			uint32_t add = us / 54925u;
			if (add == 0) add = 1;
			uint32_t t = (uint32_t)Rd16(mem, 0x46C)
				| ((uint32_t)Rd16(mem, 0x46E) << 16);
			t += add;
			Wr16(mem, 0x46C, (uint16_t)t);
			Wr16(mem, 0x46E, (uint16_t)(t >> 16));
			SetAh(0);
			SetCf(0);
			break;
		}
		default:
			SetAh(0x86);
			SetCf(1);
			break;
		}
		return DOS98_CONTINUE;
	case 0x16:
		if (Ah() == 0x01) {
			SetAl(0);
			SetZf(1);
		} else if (Ah() == 0x02) {
			SetAl(mem[0x417]);
			SetZf(0);
		} else {
			SetAl(0);
			SetZf(0);
		}
		SetCf(0);
		return DOS98_CONTINUE;
	case 0x17:
		if (!pcAtBios_) {
			unhandledVec_[vec] = 1;
			return DOS98_CONTINUE;
		}
		SetAh(0x90); /* 選択済み、エラー無し */
		SetCf(0);
		return DOS98_CONTINUE;
	case 0x33:
		/* マウス。HOOT は AX=0 / CMP AX,FFFF。「未インストール」は AX=0。 */
		np2_reg_set(NP2_R_AX, 0);
		np2_reg_set(NP2_R_BX, 0);
		SetCf(0);
		return DOS98_CONTINUE;
	case 0x67:
		if (!pcAtBios_) {
			unhandledVec_[vec] = 1;
			return DOS98_CONTINUE;
		}
		/* EMS 無し。AH=40h 後に AH を触らないと「status OK」に見え、次 EMM 呼び出しが HLT stub を far-call する。 */
		SetAh(0x80);
		SetCf(1);
		return DOS98_CONTINUE;
	/* PC-98 ディスク BIOS。実メディアは無い。CF クリア＋AH=0 は非 DOS パック用に BootDos が置く IRET stub に合わせる（失敗読みは致命）。 */
	case 0x1B:
		np2_reg_set(NP2_R_AX, (uint16_t)(np2_reg_get(NP2_R_AX) & 0x00FF));
		SetCf(0);
		return DOS98_CONTINUE;
	/* INT 2Fh マルチプレックス。XMS 4300h は 4310h に制御ルーチン無しで AL=80h を返してはいけない — SCBIOS/LUDY は cmp al,80 しゴミを far-call する。「未インストール」で no-XMS 経路へ。 */
	case 0x2F: {
		const uint16_t ax = np2_reg_get(NP2_R_AX);
		if (ax == 0x4310) {
			SetCf(1);
			return DOS98_CONTINUE;
		}
		if (ax == 0x4300) {
			SetAl(0);
			SetCf(0);
			return DOS98_CONTINUE;
		}
		if ((ax & 0xFF00) == 0x1600) {
			SetAl(0);
			SetCf(0);
			return DOS98_CONTINUE;
		}
		SetAl(0);
		SetCf(0);
		return DOS98_CONTINUE;
	}
	case 0x09:
		/* キーボード IRQ1／BIOS キーセンス。スキャンコードはキューしない */
		SetAl(0);
		SetCf(0);
		return DOS98_CONTINUE;
	case 0x0A: case 0x0B: case 0x0C: case 0x0D:
	case 0x0E: case 0x0F:
		/* マスタ PIC IRQ 2–7。トランポリンへのソフト INT: 成功を返し、無い基板 ISR を「BIOS 無し」にしない。ハード配送は IvtHooked（セグメント != 0060）が要る。 */
		SetCf(0);
		return DOS98_CONTINUE;
	case 0x19:
		/* ブートストラップ。IPL ディスクは無い。no-op 戻りとして扱う */
		SetCf(0);
		return DOS98_CONTINUE;
	case 0x1D: case 0x1E: case 0x1F:
		/* ビデオ／ディスクパラメータ表 — 呼び出し可能サービスではない */
		SetCf(0);
		return DOS98_CONTINUE;
	case 0x08:
		/* ゲストがトランポリンを残したときの BIOS IRQ0: BDA tick を進める。DeliverIrqs はこれよりフック済み INT 08 または INT 1C を優先。 */
		{
			uint32_t t = (uint32_t)Rd16(mem, 0x46C)
				| ((uint32_t)Rd16(mem, 0x46E) << 16);
			t++;
			Wr16(mem, 0x46C, (uint16_t)(t & 0xffff));
			Wr16(mem, 0x46E, (uint16_t)(t >> 16));
			/* PC-98 BIOS 日次タイマは 0000:05A0（IBM 0040:006C ではない） */
			uint32_t t98 = (uint32_t)Rd16(mem, 0x5A0)
				| ((uint32_t)Rd16(mem, 0x5A2) << 16);
			t98++;
			Wr16(mem, 0x5A0, (uint16_t)(t98 & 0xffff));
			Wr16(mem, 0x5A2, (uint16_t)(t98 >> 16));
		}
		SetCf(0);
		return DOS98_CONTINUE;
	case 0x1C:
		SetCf(0);
		return DOS98_CONTINUE;
	case 0x23:
	case 0x24:
		SetAl(0); /* 致命エラー／Ctrl-C は無視 */
		SetCf(0);
		return DOS98_CONTINUE;
	case 0x29:
		SetCf(0);
		return DOS98_CONTINUE;
	/* INT 30h（CP/M 風／未使用）と INT 4Dh: MMD2.SYS init が INT D2 フック前にこれらをプローブ。トランポリンのみだと dosmiss=int30,intD2。 */
	case 0x30:
	case 0x4D:
		SetCf(0);
		return DOS98_CONTINUE;
	/* BIOS 時刻。AIL/Miles（HOOT.EXE .ADV ドライバ）はスピンループで INT 1Ah AH=00 をサンプルしてタイマ校正する。IRET のみのトランポリンは CX:DX が変わらず校正がゼロ除算。0040:006C の BDA カウンタは PIT が進める。 */
	case 0x1A:
		if (!pcAtBios_) {
			/* PC-98 CG BIOS。漢字プローブが IVT を歩かないよう ES:BP をゼロフォントへ */
			np2_reg_set(NP2_R_ES, DOS98_TRAMP_SEG);
			np2_reg_set(NP2_R_BP, 0x0210);
			SetCf(0);
			return DOS98_CONTINUE;
		}
		switch (Ah()) {
		case 0x00:
			np2_reg_set(NP2_R_CX, Rd16(mem, 0x46E));
			np2_reg_set(NP2_R_DX, Rd16(mem, 0x46C));
			SetAl(mem[0x470]);
			mem[0x470] = 0;
			SetCf(0);
			break;
		case 0x01:
			Wr16(mem, 0x46E, np2_reg_get(NP2_R_CX));
			Wr16(mem, 0x46C, np2_reg_get(NP2_R_DX));
			mem[0x470] = 0;
			SetCf(0);
			break;
		case 0x02: {
			/* 18.2 Hz tick から BCD 壁時計を導き、繰り返し読みが実機のように単調増加する */
			const unsigned t = ((unsigned)Rd16(mem, 0x46E) << 16)
				| (unsigned)Rd16(mem, 0x46C);
			const unsigned sec = (unsigned)((uint64_t)t * 10ull / 182ull);
			const unsigned hh = (sec / 3600u) % 24u;
			const unsigned mm = (sec / 60u) % 60u;
			const unsigned ss = sec % 60u;
			np2_reg_set(NP2_R_CX, (uint16_t)((Bcd8(hh) << 8) | Bcd8(mm)));
			np2_reg_set(NP2_R_DX, (uint16_t)(Bcd8(ss) << 8));
			SetCf(0);
			break;
		}
		case 0x03:
		case 0x05:
			SetCf(0);
			break;
		case 0x04:
			np2_reg_set(NP2_R_CX, 0x1996);
			np2_reg_set(NP2_R_DX, 0x1224);
			SetCf(0);
			break;
		case 0x07:
			SetCf(0);
			break;
		default:
			SetCf(1);
			break;
		}
		return DOS98_CONTINUE;
	/* INT 05h（BOUND／プリントスクリーン）: ここは本物サービスではない */
	case 0x05:
		SetCf(0);
		return DOS98_CONTINUE;
	/* PC-88VA BIOS（olteus MUSIC.EXE／MAP.EXE）。トランポリン単独 IRET は init を半完成のまま残す。パックが実際に出す呼び出しを stub する。 */
	case 0x83:
		if (Ah() == 0x25 || Ah() == 0x35)
			return Int21(mem);
		SetCf(0);
		return DOS98_CONTINUE;
	case 0x84:
	case 0x8B:
	case 0x8F:
	case 0x94:
		SetCf(0);
		return DOS98_CONTINUE;
	default:
		unhandledVec_[vec] = 1;
		return DOS98_CONTINUE;
	}
}

/* CEmuDos98::IretReturn の実装 */
void CEmuDos98::IretReturn(uint8_t* mem)
{
	const uint16_t ss = np2_reg_get(NP2_R_SS);
	const uint16_t sp = np2_reg_get(NP2_R_SP);
	const unsigned base = DosLin(ss, sp);
	const uint16_t retIp = Rd16(mem, base);
	const uint16_t retCs = Rd16(mem, base + 2);
	const uint16_t savedFlags = Rd16(mem, base + 4);
	const uint16_t live = np2_reg_get(NP2_R_FLAGS);
	const uint16_t status = (uint16_t)(FLAG_CF | FLAG_ZF);
	const uint16_t retFlags = (uint16_t)((savedFlags & ~status) | (live & status));
	np2_reg_set(NP2_R_SP, (uint16_t)(sp + 6));
	np2_set_cs_ip(retCs, retIp);
	np2_reg_set(NP2_R_FLAGS, retFlags);
}
