#pragma once
/* x64lib 共通の実行時 ISA 判定（ヘッダのみ。各 TU に static で埋め、LNK2005 を避ける）。
 * AVX2 が既定経路。AVX512 は AVX512F+DQ+BW+VL かつ OS が ZMM/opmask を保存できるときだけ。
 * Win32 では常に 0（Windows x86 では実質使えない）。 */

#if defined(_MSC_VER) || defined(__INTEL_LLVM_COMPILER)
#include <intrin.h>
#endif

#if defined(__cplusplus)
inline
#else
static __inline
#endif
int x64lib_has_avx512(void)
{
	static int ready;
	static int has;
	if (ready) {
		return has;
	}
	has = 0;
#if defined(_WIN64) && (defined(_M_X64) || defined(__x86_64__))
	{
		int info[4] = { 0, 0, 0, 0 };
		int info7[4] = { 0, 0, 0, 0 };
		int osxsave, avx, avx2, avx512f, avx512dq, avx512bw, avx512vl, ymm_ok, zmm_ok;
		unsigned long long xcr0 = 0;
		__cpuid(info, 1);
		osxsave = (info[2] & (1 << 27)) != 0;
		avx = (info[2] & (1 << 28)) != 0;
		if (osxsave) {
			xcr0 = _xgetbv(0);
		}
		ymm_ok = ((xcr0 & 0x6ull) == 0x6ull);
		zmm_ok = ((xcr0 & 0xE6ull) == 0xE6ull);
		__cpuidex(info7, 7, 0);
		avx2 = (info7[1] & (1 << 5)) != 0;
		avx512f = (info7[1] & (1 << 16)) != 0;
		avx512dq = (info7[1] & (1 << 17)) != 0;
		avx512bw = (info7[1] & (1 << 30)) != 0;
		avx512vl = (info7[1] & (1 << 31)) != 0;
		if (avx && avx2 && ymm_ok && avx512f && avx512dq && avx512bw && avx512vl && zmm_ok) {
			has = 1;
		}
	}
#endif
	ready = 1;
	return has;
}
