#pragma once
#include <stdint.h>

/* PC-98 PMD／糊 stub 再ホスト用の最小 MS-DOS（hootrip MiniDos 移植） */

enum {
	DOS98_TRAMP_SEG = 0x0060,
	DOS98_CALL_RET_OFF = 0x0200,
	DOS98_SYSVARS_SEG = 0x0050,
	DOS98_LOL_OFF = 0x0010,
	/* 環境ブロック（PSP:002C）。トランポリン 0x60:0000..01FF（lin 0x600..0x7FF）の上に置く。
	   0x70 は重なって消えた。1 パラグラフ下に本物のアリーナヘッダが要る: 環境を縮める TSR が
	   そこからサイズを読む（OPNDRV.COM はトランポリン末尾をパラグラフ数と誤読し、自分のコードへ
	   ゴミをコピーした）。0x7F:0000 はトランポリン表の最終パラグラフ（INT F8-FF stub）。
	   PC-98 は使わないので素の IRET にする（InstallTrampolines）。環境自体を動かすと
	   固定番地で触るタイトルが壊れた。 */
	DOS98_ENV_MCB_SEG = 0x007F,
	DOS98_ENV_SEG = 0x0080,
	DOS98_ARENA_START = 0x1000,
	DOS98_ARENA_END = 0xA000,
	DOS98_IDLE_SEG = 0x00A0,
	DOS98_FILE_MAX = 1024,
	DOS98_HANDLE_MAX = 256,
	DOS98_NAME = 96
};

enum CEmuDos98Result {
	DOS98_CONTINUE = 0,
	DOS98_TERMINATED = 1,
	DOS98_RESIDENT = 2,
	/* INT 21 AH=4B/00 は既に CS:IP を子へ切替済み。IRET しない */
	DOS98_EXEC = 3
};

struct CEmuDos98File {
	char name[DOS98_NAME];
	unsigned char* data;
	unsigned size;
};

struct CEmuDos98Handle {
	int used;
	char name[DOS98_NAME];
	unsigned pos;
};

class CEmuDos98 {
public:
	CEmuDos98();
	~CEmuDos98();

	void Reset();
	void InstallTrampolines(uint8_t* mem);
	void InitArena(uint8_t* mem);
	void InstallDosStructures(uint8_t* mem, unsigned memKb = 640,
		const char* blaster = "BLASTER=A220 I5 D1 H5 T6");

	void AddFile(const char* name, const unsigned char* data, unsigned size);
	void SetHandle(uint16_t handle, const char* name);
	void SetHandleText(uint16_t handle, const char* text);

	int ResolveProgram(const char* name, const unsigned char** outData, unsigned* outSize, int* outIsExe) const;
	int LoadCom(uint8_t* mem, const unsigned char* image, unsigned imageSize, const char* tail);
	int LoadExe(uint8_t* mem, const unsigned char* image, unsigned imageSize, const char* tail);
	void LoadOverlay(uint8_t* mem, const unsigned char* image, unsigned imageSize, uint16_t loadSeg, uint16_t reloc) const;
	/* .SYS キャラクタデバイスを載せる。ロードセグメント（CS=loadSeg）を返す */
	int LoadDeviceImage(uint8_t* mem, const char* name, uint16_t* outSeg,
		uint16_t* outStratOff, uint16_t* outIntrOff,
		unsigned extraParas = 0) const;

	int TrapVector(uint16_t cs, uint16_t ip, uint8_t* outVec) const;
	CEmuDos98Result ServiceInt(uint8_t* mem, uint8_t vec);
	CEmuDos98Result ServiceIntInner(uint8_t* mem, uint8_t vec);
	void IretReturn(uint8_t* mem);

	/* INT 1Ah は PC/AT では時刻 BIOS、PC-98 では CG BIOS。ホスト機種がオプトインする */
	void SetPcAtBios(int on) { pcAtBios_ = on ? 1 : 0; }

	int VectorInstalled(uint8_t vec) const;
	uint16_t PspSeg() const { return pspSeg_; }
	const CEmuDos98File* FindFile(const char* name) const;
	int AllocBlock(uint8_t* mem, uint16_t paras, uint16_t* outSeg);

	unsigned readLogCount_;
	uint16_t readLogHandle_[32];
	unsigned readLogBytes_[32];

	/* 未実装の DOS コールは「成功・何もしない」を返す。動いているように見えてドライバが無音になる。
	   落ちたサービスを記録し、何アーカイブが必要とするか順位を付ける。 */
	uint8_t unhandledFn_[256];  /* INT 21h の AH */
	uint8_t unhandledVec_[256]; /* その他 INT ベクタ */
	/* 最初の CPU 例外（INT 00/06/07/0C/0D）の CS:IP（IRET フレーム） */
	uint8_t trapVec_;
	uint16_t trapCs_, trapIp_;
	/* INT 18h は PC-98 キーボード／CRT BIOS。AH=00/01 だけ応答し、他はレジスタそのまま。
	   落ちた AH を INT 21h と同様に数える。 */
	uint8_t unhandledInt18_[256];

	/* 任意のコールログ。興味があるのは短い窓 — 常駐ドライバが再生トリガから初ノートまでに
	   聞いたもの。ホストが必要時に武装する小さなリングで足りる。 */
	struct Call {
		uint8_t vec;
		uint16_t ax, bx, cx, dx; /* 入口 */
		uint16_t rax;            /* 戻り */
		uint16_t cs, ip;         /* 呼び出し元（逆アセンブル用） */
		int8_t cf;
		char name[24];
	};
	void TraceReset(int on) { traceOn_ = on; traceCount_ = 0; }
	/* ブート時トレースは Reset() を生き延びる必要がある。シェル実行はホストが DOS オブジェクトを
	   握る前に Reset する。 */
	static void TraceDefault(int on) { traceDefault_ = on; }
	/* リング: ブートは数千コール。興味は末尾なので最新 kTraceMax を残し、真の総数を報告する */
	unsigned TraceTotal() const { return traceCount_; }
	unsigned TraceCount() const
	{
		return traceCount_ < kTraceMax ? traceCount_ : (unsigned)kTraceMax;
	}
	const Call& TraceAt(unsigned i) const
	{
		if (traceOn_ == 2) return trace_[i];
		const unsigned n = TraceCount();
		return trace_[(traceCount_ - n + i) % kTraceMax];
	}

private:
	void FreeFiles();
	void UpperCopy(char* dst, int dstCap, const char* src) const;
	CEmuDos98File* FindFileMut(const char* name);
	uint16_t AllocHandle();
	void WriteMcb(uint8_t* mem, uint16_t seg, uint8_t sig, uint16_t owner, uint16_t size) const;
	uint16_t MaxFreeBlock(const uint8_t* mem) const;
	int Alloc(uint8_t* mem, uint16_t paras, uint16_t owner, uint16_t* outSeg) const;
	void FreeBlock(uint8_t* mem, uint16_t dataSeg) const;
	void Coalesce(uint8_t* mem) const;
	int Resize(uint8_t* mem, uint16_t dataSeg, uint16_t paras, uint16_t* errMax) const;
	void BuildPsp(uint8_t* mem, uint16_t pspSeg, uint16_t memTop, const char* tail, uint16_t envSeg) const;
	CEmuDos98Result Int21(uint8_t* mem);
	void Int18();
	void ReadCstr(const uint8_t* mem, uint16_t seg, uint16_t off, char* out, int outCap) const;
	void SetCf(int on);
	void SetAl(uint8_t v);
	uint8_t Ah() const;
	uint8_t Al() const;

	CEmuDos98File files_[DOS98_FILE_MAX];
	int fileCount_;
	CEmuDos98Handle handles_[DOS98_HANDLE_MAX];
	uint16_t nextHandle_;
	uint16_t pspSeg_;
	uint16_t dtaSeg_;
	uint16_t dtaOff_;
	uint8_t installed_[256];
	uint16_t instSeg_[256];
	uint16_t instOff_[256];

	/* AH=4E/4F 状態。検索集合はアーカイブのファイル一覧なので、パターンと files_ カーソルがハンドル全体 */
	int FindMatch(uint8_t* mem);
	char findPat_[DOS98_NAME];
	int findNext_;
	uint16_t allocStrategy_;
	int pcAtBios_;

	enum { kTraceMax = 512 };
	static int traceDefault_;
	int traceOn_;
	unsigned traceCount_;
	Call trace_[kTraceMax];
};
