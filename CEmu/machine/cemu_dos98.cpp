#include "StdAfx.h"
#include "cemu_dos98.h"
#include "../vendor/np2/np2ffi.h"
#include <stdlib.h>
#include <string.h>

enum {
	FLAG_CF = 0x0001,
	FLAG_ZF = 0x0040
};

static unsigned DosLin(uint16_t seg, uint16_t off)
{
	return ((unsigned)seg << 4) + (unsigned)off;
}

static uint16_t Rd16(const uint8_t* mem, unsigned lin)
{
	return (uint16_t)(mem[lin] | (mem[lin + 1] << 8));
}

static void Wr16(uint8_t* mem, unsigned lin, uint16_t v)
{
	mem[lin] = (uint8_t)(v & 0xff);
	mem[lin + 1] = (uint8_t)(v >> 8);
}

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
	readLogCount_ = 0;
}

CEmuDos98::~CEmuDos98()
{
	FreeFiles();
}

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
	readLogCount_ = 0;
}

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

CEmuDos98File* CEmuDos98::FindFileMut(const char* name)
{
	return const_cast<CEmuDos98File*>(FindFile(name));
}

void CEmuDos98::AddFile(const char* name, const unsigned char* data, unsigned size)
{
	if (!name || fileCount_ >= DOS98_FILE_MAX) return;
	/* size==0 allowed (HOOT "NULL" timbre sentinel). data may be NULL then. */
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

void CEmuDos98::SetHandle(uint16_t handle, const char* name)
{
	if (handle >= DOS98_HANDLE_MAX || !name) return;
	handles_[handle].used = 1;
	UpperCopy(handles_[handle].name, DOS98_NAME, name);
	/* basename only */
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

void CEmuDos98::InstallTrampolines(uint8_t* mem)
{
	if (!mem) return;
	const unsigned base = DosLin(DOS98_TRAMP_SEG, 0);
	for (unsigned v = 0; v < 256; v++) {
		mem[base + v * 2] = 0xF4;     /* HLT */
		mem[base + v * 2 + 1] = 0xCF; /* IRET */
		Wr16(mem, v * 4, (uint16_t)(v * 2));
		Wr16(mem, v * 4 + 2, DOS98_TRAMP_SEG);
	}
}

void CEmuDos98::WriteMcb(uint8_t* mem, uint16_t seg, uint8_t sig, uint16_t owner, uint16_t size) const
{
	const unsigned l = DosLin(seg, 0);
	mem[l] = sig;
	Wr16(mem, l + 1, owner);
	Wr16(mem, l + 3, size);
	for (unsigned i = 5; i < 16; i++)
		mem[l + i] = 0;
}

void CEmuDos98::InitArena(uint8_t* mem)
{
	if (!mem) return;
	const uint16_t size = (uint16_t)(DOS98_ARENA_END - DOS98_ARENA_START - 1);
	WriteMcb(mem, DOS98_ARENA_START, (uint8_t)'Z', 0, size);
}

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

	/* IBM PC BIOS data area @ 0040:0000 — HOOT/AIL divide by memsize. */
	memset(mem + 0x400, 0, 0x100);
	Wr16(mem, 0x410, 0x0021); /* equipment: diskette + 80x25 + game port */
	if (memKb < 64) memKb = 64;
	if (memKb > 640) memKb = 640;
	Wr16(mem, 0x413, (uint16_t)memKb);
	mem[0x449] = 0x03;        /* video mode 80x25 color */
	Wr16(mem, 0x44A, 80);     /* columns */
	Wr16(mem, 0x463, 0x3D4);  /* CRT port */
	Wr16(mem, 0x46C, 0);      /* timer ticks low */
	Wr16(mem, 0x46E, 0);      /* timer ticks high */

	/* PSP:002C environment — BLASTER must match emulated SB ports/IRQ. */
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
		mem[envLin + pos] = 0;
	}
}

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

static int DosInArena(uint16_t seg)
{
	return seg > DOS98_ARENA_START && seg < DOS98_ARENA_END;
}

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

void CEmuDos98::FreeBlock(uint8_t* mem, uint16_t dataSeg) const
{
	if (!DosInArena(dataSeg)) return;
	const uint16_t mcb = (uint16_t)(dataSeg - 1);
	Wr16(mem, DosLin(mcb, 0) + 1, 0);
	Coalesce(mem);
}

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
		if (size >= (uint16_t)(paras + 1)) {
			const uint16_t remSeg = (uint16_t)(mcb + 1 + paras);
			const uint16_t remSize = (uint16_t)(size - paras - 1);
			WriteMcb(mem, remSeg, sig == (uint8_t)'Z' ? (uint8_t)'Z' : (uint8_t)'M', 0, remSize);
			WriteMcb(mem, mcb, (uint8_t)'M', owner, paras);
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
	/* DOS command tail includes leading space when args present */
	mem[p + 0x80] = (uint8_t)n;
	for (int i = 0; i < n; i++)
		mem[p + 0x81 + i] = (uint8_t)t[i];
	mem[p + 0x81 + n] = 0x0D;
}

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
	/* Keep EXEPACK/PACKED trailer past the MZ-declared load size. */
	if (imageBytes < imageSize)
		imageBytes = imageSize;
	const unsigned loadSize = (imageBytes > hdrSize) ? (imageBytes - hdrSize) : 0;
	const unsigned loadParas = (loadSize + 15u) / 16u;
	/* DOS loads EXE into the largest free block (maxAlloc often FFFF).
	   MSC CRT checks memTop−SS; allocating only minAlloc leaves ~9 paras
	   and HOOT aborts with "Abnormal program termination". */
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
	/* Zero PSP+image+BSS — MSC CRT aborts on garbage BSS ("Abnormal program termination"). */
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
	uint16_t* outStratOff, uint16_t* outIntrOff) const
{
	if (!mem || !name || !outSeg) return 0;
	*outSeg = 0;
	if (outStratOff) *outStratOff = 0;
	if (outIntrOff) *outIntrOff = 0;
	const CEmuDos98File* file = FindFile(name);
	if (!file || !file->data || file->size < 18) return 0;
	const unsigned paras = (file->size + 15u) / 16u + 1u;
	uint16_t seg = 0;
	if (!Alloc(mem, (uint16_t)paras, 8, &seg) || !seg) return 0;
	memcpy(mem + DosLin(seg, 0), file->data, file->size);
	*outSeg = seg;
	if (outStratOff)
		*outStratOff = (uint16_t)(file->data[6] | (file->data[7] << 8));
	if (outIntrOff)
		*outIntrOff = (uint16_t)(file->data[8] | (file->data[9] << 8));
	return 1;
}

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
		/* Packed ADVH/EXEPACK stubs often sit past the MZ-declared size
		   (dragon_c_98: header says 18356 body bytes, file has 18408).
		   Truncating drops the decrypt/jmp-0010 stub and F1 never hooks. */
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

int CEmuDos98::AllocBlock(uint8_t* mem, uint16_t paras, uint16_t* outSeg)
{
	return Alloc(mem, paras, pspSeg_ ? pspSeg_ : 8, outSeg);
}

int CEmuDos98::TrapVector(uint16_t cs, uint16_t ip, uint8_t* outVec) const
{
	if (cs != DOS98_TRAMP_SEG || ip >= 512 || (ip & 1)) return 0;
	if (outVec) *outVec = (uint8_t)(ip / 2);
	return 1;
}

int CEmuDos98::VectorInstalled(uint8_t vec) const
{
	return installed_[vec] ? 1 : 0;
}

uint8_t CEmuDos98::Ah() const
{
	return (uint8_t)(np2_reg_get(NP2_R_AX) >> 8);
}

uint8_t CEmuDos98::Al() const
{
	return (uint8_t)(np2_reg_get(NP2_R_AX) & 0xff);
}

void CEmuDos98::SetAl(uint8_t v)
{
	uint16_t ax = np2_reg_get(NP2_R_AX);
	np2_reg_set(NP2_R_AX, (uint16_t)((ax & 0xff00) | v));
}

void CEmuDos98::SetCf(int on)
{
	uint16_t f = np2_reg_get(NP2_R_FLAGS);
	if (on) f = (uint16_t)(f | FLAG_CF);
	else f = (uint16_t)(f & ~FLAG_CF);
	np2_reg_set(NP2_R_FLAGS, f);
}

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

void CEmuDos98::Int18()
{
	const uint8_t f = Ah();
	if (f == 0x00 || f == 0x01)
		np2_reg_set(NP2_R_AX, 0);
}

CEmuDos98Result CEmuDos98::Int21(uint8_t* mem)
{
	const uint8_t f = Ah();
	switch (f) {
	case 0x00:
		/* Program terminate — free the PSP arena so the next shell
		   (olteus after MUSIC.EXE, etc.) can Alloc again. */
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
		np2_reg_set(NP2_R_BX, Rd16(mem, (unsigned)v * 4u));
		np2_reg_set(NP2_R_ES, Rd16(mem, (unsigned)v * 4u + 2u));
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
			uint16_t h = nextHandle_++;
			if (h >= DOS98_HANDLE_MAX) h = (uint16_t)(DOS98_HANDLE_MAX - 1);
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
		const unsigned count = np2_reg_get(NP2_R_CX);
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
		const uint32_t off = ((uint32_t)np2_reg_get(NP2_R_CX) << 16) | np2_reg_get(NP2_R_DX);
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
		/* Get/set file attributes — AIL/HOOT probes before open. */
		if (Al() == 0) {
			char name[DOS98_NAME];
			ReadCstr(mem, np2_reg_get(NP2_R_DS), np2_reg_get(NP2_R_DX), name, (int)sizeof(name));
			if (FindFile(name)) {
				np2_reg_set(NP2_R_CX, 0x20); /* archive */
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
		if (Al() == 0)
			np2_reg_set(NP2_R_DX, 0);
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
			/* Load-and-execute. Used by shells that spawn resident music
			   drivers; CS:IP land on the MZ entry (incl. PACKED stub). */
			const CEmuDos98File* file = FindFile(name);
			if (file && file->data && LoadExe(mem, file->data, file->size, "")) {
				SetCf(0);
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
	default:
		SetCf(0);
		break;
	}
	return DOS98_CONTINUE;
}

CEmuDos98Result CEmuDos98::ServiceInt(uint8_t* mem, uint8_t vec)
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
	/* PC-88VA BIOS (olteus MUSIC.EXE / MAP.EXE). Trampoline alone IRETs and
	   leaves init half-done; stub the calls the packs actually issue. */
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
		return DOS98_CONTINUE;
	}
}

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
