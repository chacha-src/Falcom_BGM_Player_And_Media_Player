/* AVX10/512 経路。機械語は AVX-512 と同じ幅。レガシー AVX512F ビットが無くても leaf 24 が 512bit ならこちら。 */
#define SimdKernels_Copy_AVX512 SimdKernels_Copy_AVX10
#define SimdKernels_Zero_AVX512 SimdKernels_Zero_AVX10
#define SimdKernels_AddF32_AVX512 SimdKernels_AddF32_AVX10
#define SimdKernels_MulF32_AVX512 SimdKernels_MulF32_AVX10
#define SimdKernels_MaddF32_AVX512 SimdKernels_MaddF32_AVX10
#include "simd_kernels_avx512.cpp"
#undef SimdKernels_Copy_AVX512
#undef SimdKernels_Zero_AVX512
#undef SimdKernels_AddF32_AVX512
#undef SimdKernels_MulF32_AVX512
#undef SimdKernels_MaddF32_AVX512
