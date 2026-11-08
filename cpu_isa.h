#pragma once
// 実行時 CPU 判定。ビルドは AVX2。512bit は OS が ZMM を保存できるときだけ。
// Win32 では 512bit は使わない（Windows x86 では実質使えない）。
// AVX2 ビルドでは出せない拡張（AVX512* / AVX10 / AVX-VNNI / GFNI / VAES など）もここで拾い、
// 専用 TU のカーネルが実行時に選ぶ。

struct CpuIsa {
	int avx2;       // AVX + AVX2 + OS YMM
	int avx512;     // AVX512F+DQ+BW+VL + OS ZMM/opmask
	int avx10;      // AVX10.1 以上（256bit 以上）+ OS YMM
	int avx10_512;  // AVX10 が 512bit を列挙 + OS ZMM/opmask
	int avx10_ver;  // leaf 24 の AVX10 バージョン。0=なし
	int fma;
	int bmi1;
	int bmi2;
	int popcnt;
	int sse42;
	int aes;
	int pclmul;
	int sha;
	int avx_vnni;           // 256bit VNNI（AVX512 無しでも）
	int avx512_vnni;
	int avx512_vbmi;
	int avx512_vbmi2;
	int avx512_ifma;
	int avx512_vpopcntdq;
	int avx512_bitalg;
	int avx512_bf16;
	int avx512_gfni;
	int avx512_vaes;
	int avx512_vpclmul;
	int avx512_fp16;
};

const CpuIsa& CpuIsaGet();
int CpuIsaHasAvx2();
int CpuIsaHasAvx512();
int CpuIsaHasAvx10();
int CpuIsaHasAvx10_512();
int CpuIsaWide512(); // avx512 または avx10_512。カーネルが 512bit を使ってよいとき
const char* CpuIsaKernelName(); // "AVX10/512" / "AVX512" / "AVX2" / "scalar"
int CpuIsaFormatExtras(char* out, int cap); // "FMA BMI2 AVX-VNNI ..." を書く。戻り値は文字数

// 根本の ISA クラス。CpuIsaGet と同じ検出結果をメソッドで返す。
class SimdCpu {
public:
	static const CpuIsa& Get() { return CpuIsaGet(); }
	static int Avx2() { return CpuIsaHasAvx2(); }
	static int Avx512() { return CpuIsaHasAvx512(); }
	static int Avx10() { return CpuIsaHasAvx10(); }
	static int Avx10_512() { return CpuIsaHasAvx10_512(); }
	static int Wide512() { return CpuIsaWide512(); }
	static const char* KernelName() { return CpuIsaKernelName(); }
};
