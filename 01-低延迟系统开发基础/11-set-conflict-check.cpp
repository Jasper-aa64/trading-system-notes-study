// #11 轮 3:按列走矩阵,行宽(步长)是 2 的幂时的组冲突。
// 对应笔记"轮 3 第 2 节"的 int m[64][1024] 例子,把行宽扫一遍:
//   每种行宽都只走前 1024 列、64 行,访问次数相同,只有"相邻两行的地址差"不同。
//   对照组:同样行宽再多填 128 B(一条 M1 缓存行)。
// 编译运行:clang++ -std=c++20 -O2 11-set-conflict-check.cpp -o 11-set-conflict-check && ./11-set-conflict-check
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#if defined(__APPLE__)
#include <pthread.h>
#include <sys/qos.h>
#endif

constexpr int kRows = 64;
constexpr int kCols = 1024;   // 每次只走前 1024 列
constexpr int kPasses = 200;  // 一次计时里重复整张表的次数
constexpr int kTrials = 7;    // 取最快的一次

static long long column_walk(const int* m, size_t row_ints) {
    long long sum = 0;
    for (int j = 0; j < kCols; ++j)
        for (int i = 0; i < kRows; ++i)
            sum += m[i * row_ints + j];   // 每步地址 + row_ints*4 字节
    return sum;
}

static long long row_walk(const int* m, size_t row_ints) {
    long long sum = 0;
    for (int i = 0; i < kRows; ++i)
        for (int j = 0; j < kCols; ++j)
            sum += m[i * row_ints + j];
    return sum;
}

template <class F>
static double ns_per_access(F walk, const int* m, size_t row_ints, long long& check) {
    double best = 1e30;
    for (int t = 0; t < kTrials; ++t) {
        long long s = 0;
        auto t0 = std::chrono::steady_clock::now();
        for (int p = 0; p < kPasses; ++p) {
            asm volatile("" : : "r"(m) : "memory");  // 告诉编译器内存可能变了,每一遍都得真读
            s += walk(m, row_ints);
        }
        auto t1 = std::chrono::steady_clock::now();
        double ns = std::chrono::duration<double, std::nano>(t1 - t0).count();
        best = std::min(best, ns / (double(kPasses) * kRows * kCols));
        check = s;
    }
    return best;
}

int main() {
#if defined(__APPLE__)
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);  // 尽量跑在大核上
#endif
    const size_t strides[] = {1024, 2048, 4096, 8192, 16384, 32768};  // 行宽,字节
    std::printf("%-22s %14s %14s\n", "行宽(相邻两行地址差)", "按列 ns/次", "按行 ns/次");
    for (size_t bytes : strides) {
        for (size_t pad : {size_t(0), size_t(128)}) {
            size_t row_ints = (bytes + pad) / sizeof(int);
            size_t total = row_ints * kRows;
            int* m = static_cast<int*>(std::aligned_alloc(16384, ((total * sizeof(int) + 16383) / 16384) * 16384));
            for (size_t k = 0; k < total; ++k) m[k] = int(k & 7);
            long long c1 = 0, c2 = 0;
            double col = ns_per_access(column_walk, m, row_ints, c1);
            double row = ns_per_access(row_walk, m, row_ints, c2);
            if (c1 != c2) { std::printf("结果不一致\n"); return 1; }
            char label[64];
            std::snprintf(label, sizeof label, "%zu%s", bytes, pad ? " + 128" : "");
            std::printf("%-22s %14.3f %14.3f\n", label, col, row);
            std::free(m);
        }
    }
}
