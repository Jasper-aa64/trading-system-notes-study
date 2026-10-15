// #21 缓存预取预热和向量化（向量化部分）：验证程序
// 运行：g++ -std=c++20 -O2 -fopenmp-simd 21-simd-check.cpp -o simd && ./simd
// 汇编：分别加 -O2 / -O3 / -O3 -mavx2 看 add_arrays 用 xmm 还是 ymm；-fopenmp-simd 看 sum_omp 是不是 addps。
// 本机：GCC 16.2（winlibs mingw64），Ryzen 5 5600GT（支持 AVX2、FMA）。
#include <cmath>
#include <cstdio>
#include <immintrin.h>
#include <limits>
#include <vector>

// 1. 最简单的循环：编译器自己向量化
void add_arrays(float* c, const float* a, const float* b, int n) {
    for (int i = 0; i < n; i++) c[i] = a[i] + b[i];
}

// 2. float 求和：默认不能重排；omp simd reduction 只给这一个循环放开
float sum_plain(const float* a, int n) { float s = 0; for (int i = 0; i < n; i++) s += a[i]; return s; }
float sum_omp(const float* a, int n) {
    float s = 0;
    #pragma omp simd reduction(+:s)
    for (int i = 0; i < n; i++) s += a[i];
    return s;
}

// 3. 手写 AVX2 + FMA 点积（n 是 8 的倍数），只给这一个函数开 AVX2
__attribute__((target("avx2,fma")))
float dot_avx2(const float* a, const float* b, int n) {
    __m256 acc = _mm256_setzero_ps();
    for (int i = 0; i < n; i += 8)
        acc = _mm256_fmadd_ps(_mm256_loadu_ps(a + i), _mm256_loadu_ps(b + i), acc);
    __m128 s = _mm_add_ps(_mm256_castps256_ps128(acc), _mm256_extractf128_ps(acc, 1));
    s = _mm_add_ps(s, _mm_movehl_ps(s, s));
    s = _mm_add_ss(s, _mm_movehdup_ps(s));
    return _mm_cvtss_f32(s);
}
float dot_scalar(const float* a, const float* b, int n) { float s = 0; for (int i = 0; i < n; i++) s += a[i] * b[i]; return s; }

// 4. 运行时选：CPU 支持 AVX2 才走 dot_avx2
float dot(const float* a, const float* b, int n) {
    static const bool has_avx2 = __builtin_cpu_supports("avx2") && __builtin_cpu_supports("fma");
    return has_avx2 ? dot_avx2(a, b, n) : dot_scalar(a, b, n);
}

// 5. 一排数里有没有 NaN：比较出掩码，再收成一个整数
__attribute__((target("avx2")))
int count_nan8(const float* p) {
    __m256 x = _mm256_loadu_ps(p);
    __m256 m = _mm256_cmp_ps(x, x, _CMP_UNORD_Q);   // NaN 和自己比较是“无序”
    return __builtin_popcount(_mm256_movemask_ps(m));
}

int main() {
    const int n = 1024;
    std::vector<float> a(n), b(n), c(n);
    for (int i = 0; i < n; i++) { a[i] = 0.001f * i; b[i] = 1.0f + 0.0001f * i; }
    add_arrays(c.data(), a.data(), b.data(), n);
    std::printf("add c[10] = %g\n", c[10]);
    std::printf("sum_plain = %.9g, sum_omp = %.9g\n", sum_plain(a.data(), n), sum_omp(a.data(), n));
    std::printf("avx2 %d fma %d\n", __builtin_cpu_supports("avx2"), __builtin_cpu_supports("fma"));
    std::printf("dot = %.9g, dot_scalar = %.9g\n", dot(a.data(), b.data(), n), dot_scalar(a.data(), b.data(), n));
    float v[8] = {1, std::numeric_limits<float>::quiet_NaN(), 3, 4, std::numeric_limits<float>::quiet_NaN(), 6, 7, 8};
    std::printf("count_nan8 = %d\n", count_nan8(v));
    float nan_sum = 0; for (float x : v) nan_sum += x;
    std::printf("sum with NaN = %g\n", nan_sum);
}
