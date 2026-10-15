// #22 HPC 辅助宏：验证程序（只看汇编）
// 汇编：g++ -std=c++23 -O2 -mabi=sysv -S -fno-asynchronous-unwind-tables 22-hpc-macro-check.cpp
// 本机：GCC 16.2（winlibs mingw64），x86-64。
// 结果：sum_hpc 编成 addps（向量化），sum_plain 是 addss；in_region / outside 里的小函数都没被内联（call sq / call cube）。

float sum_plain(const float* a, int n) { float s = 0; for (int i = 0; i < n; ++i) s += a[i]; return s; }

#pragma GCC push_options
#pragma GCC optimize ("O3,unroll-loops,associative-math,no-signed-zeros")
float sum_hpc(const float* a, int n) { float s = 0; for (int i = 0; i < n; ++i) s += a[i]; return s; }
#pragma GCC pop_options
#pragma GCC push_options
#pragma GCC optimize ("O3,associative-math,no-signed-zeros,no-trapping-math")
float sum_fixed(const float* a, int n) { float s = 0; for (int i = 0; i < n; ++i) s += a[i]; return s; }
#pragma GCC pop_options

static inline float sq(float x) { return x * x; }            // 区域外定义（默认选项）
#pragma GCC push_options
#pragma GCC optimize ("O3,unroll-loops,associative-math,no-signed-zeros")
float in_region(float x) { return sq(x) + 1.0f; }            // 区域里调区域外的小函数
static inline float cube(float x) { return x * x * x; }       // 区域里定义
#pragma GCC pop_options
float outside(float x) { return cube(x) + 2.0f; }             // 区域外调区域里的小函数
