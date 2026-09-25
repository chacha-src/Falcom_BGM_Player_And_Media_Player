#pragma once
// 実行時 CPU 判定。AVX2 が既定経路、AVX512 は OS が ZMM を保存できるときだけ。
// Win32 では AVX512 は使わない（Windows x86 では実質使えない）。

struct CpuIsa {
	int avx2;    // AVX + AVX2 + OS YMM
	int avx512;  // AVX512F+DQ+BW+VL + OS ZMM/opmask
};

const CpuIsa& CpuIsaGet();
int CpuIsaHasAvx2();
int CpuIsaHasAvx512();
