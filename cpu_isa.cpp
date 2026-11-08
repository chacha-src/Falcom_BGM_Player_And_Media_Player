#include "cpu_isa.h"
#include <string.h>

#if defined(_MSC_VER) || defined(__INTEL_LLVM_COMPILER)
#include <intrin.h>
#endif

static CpuIsa g_isa;
static int g_ready;

static void CpuIsaDetect()
{
	memset(&g_isa, 0, sizeof(g_isa));
#if defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || defined(__x86_64__)
	int leaf0[4] = { 0, 0, 0, 0 };
	__cpuid(leaf0, 0);
	const int maxLeaf = leaf0[0];
	int info[4] = { 0, 0, 0, 0 };
	__cpuid(info, 1);
	const int osxsave = (info[2] & (1 << 27)) != 0;
	const int avx = (info[2] & (1 << 28)) != 0;
	g_isa.fma = (info[2] & (1 << 12)) ? 1 : 0;
	g_isa.sse42 = (info[2] & (1 << 20)) ? 1 : 0;
	g_isa.popcnt = (info[2] & (1 << 23)) ? 1 : 0;
	g_isa.aes = (info[2] & (1 << 25)) ? 1 : 0;
	g_isa.pclmul = (info[2] & (1 << 1)) ? 1 : 0;
	unsigned long long xcr0 = 0;
	if (osxsave)
		xcr0 = _xgetbv(0);
	const int ymm_ok = ((xcr0 & 0x6ull) == 0x6ull);
	const int zmm_ok = ((xcr0 & 0xE6ull) == 0xE6ull);
	int info7[4] = { 0, 0, 0, 0 };
	if (maxLeaf >= 7)
		__cpuidex(info7, 7, 0);
	const int avx2 = (info7[1] & (1 << 5)) != 0;
	g_isa.bmi1 = (info7[1] & (1 << 3)) ? 1 : 0;
	g_isa.bmi2 = (info7[1] & (1 << 8)) ? 1 : 0;
	g_isa.sha = (info7[1] & (1 << 29)) ? 1 : 0;
	const int avx512f = (info7[1] & (1 << 16)) != 0;
	const int avx512dq = (info7[1] & (1 << 17)) != 0;
	const int avx512bw = (info7[1] & (1 << 30)) != 0;
	const int avx512vl = (info7[1] & (1 << 31)) != 0;
	g_isa.avx512_ifma = (info7[1] & (1 << 21)) ? 1 : 0;
	g_isa.avx512_vbmi = (info7[2] & (1 << 1)) ? 1 : 0;
	g_isa.avx512_vbmi2 = (info7[2] & (1 << 6)) ? 1 : 0;
	g_isa.avx512_gfni = (info7[2] & (1 << 8)) ? 1 : 0;
	g_isa.avx512_vaes = (info7[2] & (1 << 9)) ? 1 : 0;
	g_isa.avx512_vpclmul = (info7[2] & (1 << 10)) ? 1 : 0;
	g_isa.avx512_vnni = (info7[2] & (1 << 11)) ? 1 : 0;
	g_isa.avx512_bitalg = (info7[2] & (1 << 12)) ? 1 : 0;
	g_isa.avx512_vpopcntdq = (info7[2] & (1 << 14)) ? 1 : 0;
	g_isa.avx512_fp16 = (info7[3] & (1 << 23)) ? 1 : 0;
	g_isa.avx2 = (avx && avx2 && ymm_ok) ? 1 : 0;
#if defined(_WIN64) || defined(__x86_64__)
	g_isa.avx512 = (g_isa.avx2 && avx512f && avx512dq && avx512bw && avx512vl && zmm_ok) ? 1 : 0;
#else
	g_isa.avx512 = 0;
#endif
	int avx10_leaf = 0;
	int avx10_ver = 0;
	int avx10_256 = 0;
	int avx10_512bit = 0;
	if (maxLeaf >= 7 && info7[0] >= 1) {
		int info71[4] = { 0, 0, 0, 0 };
		__cpuidex(info71, 7, 1);
		g_isa.avx_vnni = (info71[0] & (1 << 4)) ? 1 : 0;
		g_isa.avx512_bf16 = (info71[0] & (1 << 5)) ? 1 : 0;
		avx10_leaf = (info71[3] & (1 << 19)) != 0;
	}
	if (avx10_leaf && maxLeaf >= 0x24) {
		int info24[4] = { 0, 0, 0, 0 };
		__cpuidex(info24, 0x24, 0);
		avx10_ver = info24[1] & 0xFF;
		avx10_256 = (info24[1] & (1 << 17)) != 0;
		avx10_512bit = (info24[1] & (1 << 18)) != 0;
	}
	g_isa.avx10_ver = avx10_ver;
	g_isa.avx10 = (g_isa.avx2 && avx10_ver >= 1 && (avx10_256 || avx10_512bit)) ? 1 : 0;
#if defined(_WIN64) || defined(__x86_64__)
	g_isa.avx10_512 = (g_isa.avx10 && avx10_512bit && zmm_ok) ? 1 : 0;
#else
	g_isa.avx10_512 = 0;
#endif
	if (!g_isa.avx512 && !g_isa.avx10_512) {
		g_isa.avx512_vnni = 0;
		g_isa.avx512_vbmi = 0;
		g_isa.avx512_vbmi2 = 0;
		g_isa.avx512_ifma = 0;
		g_isa.avx512_vpopcntdq = 0;
		g_isa.avx512_bitalg = 0;
		g_isa.avx512_bf16 = 0;
		g_isa.avx512_gfni = 0;
		g_isa.avx512_vaes = 0;
		g_isa.avx512_vpclmul = 0;
		g_isa.avx512_fp16 = 0;
	}
	if (!g_isa.avx2) {
		g_isa.avx_vnni = 0;
		g_isa.fma = 0;
	}
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

int CpuIsaWide512()
{
	const CpuIsa& isa = CpuIsaGet();
	return (isa.avx512 || isa.avx10_512) ? 1 : 0;
}

const char* CpuIsaKernelName()
{
	const CpuIsa& isa = CpuIsaGet();
	if (isa.avx10_512) return "AVX10/512";
	if (isa.avx512) return "AVX512";
	if (isa.avx2 || isa.avx10) return "AVX2";
	return "scalar";
}

static void extras_add(char* out, int* n, int cap, const char* t)
{
	int len = 0;
	while (t[len]) ++len;
	if (*n + len + 1 >= cap) return;
	for (int i = 0; i < len; ++i) out[(*n)++] = t[i];
	out[(*n)++] = ' ';
	out[*n] = 0;
}

int CpuIsaFormatExtras(char* out, int cap)
{
	if (!out || cap < 2) return 0;
	out[0] = 0;
	const CpuIsa& isa = CpuIsaGet();
	int n = 0;
	if (isa.fma) extras_add(out, &n, cap, "FMA");
	if (isa.bmi1) extras_add(out, &n, cap, "BMI1");
	if (isa.bmi2) extras_add(out, &n, cap, "BMI2");
	if (isa.popcnt) extras_add(out, &n, cap, "POPCNT");
	if (isa.aes) extras_add(out, &n, cap, "AES");
	if (isa.pclmul) extras_add(out, &n, cap, "PCLMUL");
	if (isa.sha) extras_add(out, &n, cap, "SHA");
	if (isa.avx_vnni) extras_add(out, &n, cap, "AVX-VNNI");
	if (isa.avx512_vnni) extras_add(out, &n, cap, "AVX512-VNNI");
	if (isa.avx512_vbmi) extras_add(out, &n, cap, "AVX512-VBMI");
	if (isa.avx512_vbmi2) extras_add(out, &n, cap, "AVX512-VBMI2");
	if (isa.avx512_ifma) extras_add(out, &n, cap, "AVX512-IFMA");
	if (isa.avx512_vpopcntdq) extras_add(out, &n, cap, "AVX512-VPOPCNTDQ");
	if (isa.avx512_bitalg) extras_add(out, &n, cap, "AVX512-BITALG");
	if (isa.avx512_bf16) extras_add(out, &n, cap, "AVX512-BF16");
	if (isa.avx512_gfni) extras_add(out, &n, cap, "GFNI");
	if (isa.avx512_vaes) extras_add(out, &n, cap, "VAES");
	if (isa.avx512_vpclmul) extras_add(out, &n, cap, "VPCLMULQDQ");
	if (isa.avx512_fp16) extras_add(out, &n, cap, "AVX512-FP16");
	if (isa.avx10_ver > 0) {
		char ver[24];
		ver[0] = 'A'; ver[1] = 'V'; ver[2] = 'X'; ver[3] = '1'; ver[4] = '0'; ver[5] = '.';
		int v = isa.avx10_ver;
		if (v > 9) v = 9;
		ver[6] = (char)('0' + v);
		ver[7] = 0;
		extras_add(out, &n, cap, ver);
	}
	return n;
}
