#pragma once
/* x64lib 共通の実行時 ISA 判定（ヘッダのみ。各 TU に static で埋め、LNK2005 を避ける）。
 * ビルドは AVX2。AVX512 は F+DQ+BW+VL かつ OS が ZMM/opmask を保存できるとき。
 * AVX10 は leaf 7.1 EDX[19] と leaf 24 のベクトル長。512bit は Win32 では常に 0。 */

#if defined(_MSC_VER) || defined(__INTEL_LLVM_COMPILER)
#include <intrin.h>
#endif

#if defined(__cplusplus)
inline
#else
static __inline
#endif
void x64lib_isa_probe(int* avx2, int* avx512, int* avx10, int* avx10_512)
{
	if (avx2) *avx2 = 0;
	if (avx512) *avx512 = 0;
	if (avx10) *avx10 = 0;
	if (avx10_512) *avx10_512 = 0;
#if defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || defined(__x86_64__)
	{
		int leaf0[4] = { 0, 0, 0, 0 };
		int info[4] = { 0, 0, 0, 0 };
		int info7[4] = { 0, 0, 0, 0 };
		int osxsave, avx, has2, has512, has10, has10_512;
		int avx512f, avx512dq, avx512bw, avx512vl, ymm_ok, zmm_ok;
		int maxLeaf, avx10_leaf, avx10_ver, avx10_256, avx10_512bit;
		unsigned long long xcr0 = 0;
		__cpuid(leaf0, 0);
		maxLeaf = leaf0[0];
		__cpuid(info, 1);
		osxsave = (info[2] & (1 << 27)) != 0;
		avx = (info[2] & (1 << 28)) != 0;
		if (osxsave)
			xcr0 = _xgetbv(0);
		ymm_ok = ((xcr0 & 0x6ull) == 0x6ull);
		zmm_ok = ((xcr0 & 0xE6ull) == 0xE6ull);
		if (maxLeaf >= 7)
			__cpuidex(info7, 7, 0);
		has2 = (avx && ((info7[1] & (1 << 5)) != 0) && ymm_ok) ? 1 : 0;
		avx512f = (info7[1] & (1 << 16)) != 0;
		avx512dq = (info7[1] & (1 << 17)) != 0;
		avx512bw = (info7[1] & (1 << 30)) != 0;
		avx512vl = (info7[1] & (1 << 31)) != 0;
		has512 = 0;
		has10 = 0;
		has10_512 = 0;
#if defined(_WIN64) && (defined(_M_X64) || defined(__x86_64__))
		if (has2 && avx512f && avx512dq && avx512bw && avx512vl && zmm_ok)
			has512 = 1;
#endif
		avx10_leaf = 0;
		avx10_ver = 0;
		avx10_256 = 0;
		avx10_512bit = 0;
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
		if (has2 && avx10_ver >= 1 && (avx10_256 || avx10_512bit))
			has10 = 1;
#if defined(_WIN64) && (defined(_M_X64) || defined(__x86_64__))
		if (has10 && avx10_512bit && zmm_ok)
			has10_512 = 1;
#endif
		if (avx2) *avx2 = has2;
		if (avx512) *avx512 = has512;
		if (avx10) *avx10 = has10;
		if (avx10_512) *avx10_512 = has10_512;
	}
#else
	(void)avx2;
	(void)avx512;
	(void)avx10;
	(void)avx10_512;
#endif
}

#if defined(__cplusplus)
inline
#else
static __inline
#endif
int x64lib_has_avx2(void)
{
	static int ready, v;
	if (!ready) {
		x64lib_isa_probe(&v, 0, 0, 0);
		ready = 1;
	}
	return v;
}

#if defined(__cplusplus)
inline
#else
static __inline
#endif
int x64lib_has_avx512(void)
{
	static int ready, v;
	if (!ready) {
		x64lib_isa_probe(0, &v, 0, 0);
		ready = 1;
	}
	return v;
}

#if defined(__cplusplus)
inline
#else
static __inline
#endif
int x64lib_has_avx10(void)
{
	static int ready, v;
	if (!ready) {
		x64lib_isa_probe(0, 0, &v, 0);
		ready = 1;
	}
	return v;
}

#if defined(__cplusplus)
inline
#else
static __inline
#endif
int x64lib_has_avx10_512(void)
{
	static int ready, v;
	if (!ready) {
		x64lib_isa_probe(0, 0, 0, &v);
		ready = 1;
	}
	return v;
}
