#pragma once
// 実行時 CPU 判定。ビルドは AVX2。512bit は OS が ZMM を保存できるときだけ。
// Win32 では 512bit は使わない（Windows x86 では実質使えない）。

struct CpuIsa {
	int avx2;       // AVX + AVX2 + OS YMM
	int avx512;     // AVX512F+DQ+BW+VL + OS ZMM/opmask
	int avx10;      // AVX10.1 以上（256bit 以上）+ OS YMM
	int avx10_512;  // AVX10 が 512bit を列挙 + OS ZMM/opmask
};

const CpuIsa& CpuIsaGet();
int CpuIsaHasAvx2();
int CpuIsaHasAvx512();
int CpuIsaHasAvx10();
int CpuIsaHasAvx10_512();
