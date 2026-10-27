#include "cpu_isa.h"

#if defined(_MSC_VER) || defined(__INTEL_LLVM_COMPILER)
#include <intrin.h>
#endif

static CpuIsa g_isa;
static int g_ready;

static void CpuIsaDetect()
{
	g_isa.avx2 = 0;
	g_isa.avx512 = 0;
	g_isa.avx10 = 0;
	g_isa.avx10_512 = 0;
#if defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || defined(__x86_64__)
	int leaf0[4] = { 0, 0, 0, 0 };
	__cpuid(leaf0, 0);
	const int maxLeaf = leaf0[0];
	int info[4] = { 0, 0, 0, 0 };
	__cpuid(info, 1);
	const int osxsave = (info[2] & (1 << 27)) != 0;
	const int avx = (info[2] & (1 << 28)) != 0;
	unsigned long long xcr0 = 0;
	if (osxsave) {
		xcr0 = _xgetbv(0);
	}
	const int ymm_ok = ((xcr0 & 0x6ull) == 0x6ull);
	// XMM=1 YMM=2 opmask=0x20 ZMM_hi256=0x40 ZMM_hi16=0x80
	const int zmm_ok = ((xcr0 & 0xE6ull) == 0xE6ull);
	int info7[4] = { 0, 0, 0, 0 };
	if (maxLeaf >= 7)
		__cpuidex(info7, 7, 0);
	const int avx2 = (info7[1] & (1 << 5)) != 0;
	const int avx512f = (info7[1] & (1 << 16)) != 0;
	const int avx512dq = (info7[1] & (1 << 17)) != 0;
	const int avx512bw = (info7[1] & (1 << 30)) != 0;
	const int avx512vl = (info7[1] & (1 << 31)) != 0;
	g_isa.avx2 = (avx && avx2 && ymm_ok) ? 1 : 0;
#if defined(_WIN64) || defined(__x86_64__)
	g_isa.avx512 = (g_isa.avx2 && avx512f && avx512dq && avx512bw && avx512vl && zmm_ok) ? 1 : 0;
#else
	g_isa.avx512 = 0;
#endif
	// AVX10: leaf 7 subleaf 1 EDX[19]、詳細は leaf 24 EBX（version とベクトル長）。
	int avx10_leaf = 0;
	int avx10_ver = 0;
	int avx10_256 = 0;
	int avx10_512bit = 0;
	if (maxLeaf >= 7 && info7[0] >= 1) {
		int info71[4] = { 0, 0, 0, 0 };
		__cpuidex(info71, 7, 1);
		avx10_leaf = (info71[3] & (1 << 19)) != 0;
	}
	if (avx10_leaf && maxLeaf >= 0x24) {
		int info24[4] = { 0, 0, 0, 0 };
		__cpuidex(info24, 0x24, 0);
		avx10_ver = info24[1] & 0xFF;
		avx10_256 = (info24[1] & (1 << 17)) != 0;
		avx10_512bit = (info24[1] & (1 << 18)) != 0;
	}
	g_isa.avx10 = (g_isa.avx2 && avx10_ver >= 1 && (avx10_256 || avx10_512bit)) ? 1 : 0;
#if defined(_WIN64) || defined(__x86_64__)
	g_isa.avx10_512 = (g_isa.avx10 && avx10_512bit && zmm_ok) ? 1 : 0;
#else
	g_isa.avx10_512 = 0;
#endif
#endif
}

const CpuIsa& CpuIsaGet()
{
	if (!g_ready) {
		CpuIsaDetect();
		g_ready = 1;
	}
	return g_isa;
}

int CpuIsaHasAvx2()
{
	return CpuIsaGet().avx2;
}

int CpuIsaHasAvx512()
{
	return CpuIsaGet().avx512;
}

int CpuIsaHasAvx10()
{
	return CpuIsaGet().avx10;
}

int CpuIsaHasAvx10_512()
{
	return CpuIsaGet().avx10_512;
}
