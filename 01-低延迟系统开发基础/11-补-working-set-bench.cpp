// 11补 轮 2 第 1 节：工作集台阶的实测。反复顺序求和一个 int 数组，数组从 4 KiB 加到 256 MiB，
// 每个大小输出“一个核每秒读多少字节”。输出 CSV（bytes,gbps），交给 tools/plot_working_set.py 画图。
//
// 构建（GCC 或 Clang，x86-64 或 ARM 都行）：
//   g++ -std=c++20 -O3 -march=native -Wall -Wextra 11-补-working-set-bench.cpp -o ws-bench
// Apple clang（M1）不认 -march=native 时改成 -mcpu=native。
// 运行：./ws-bench > ws.csv（约 10–60 秒；跑的时候别开别的重活）
//
// 测的是“这段循环”能跑多快，不是硬件峰值：
// - 用 4 个互不依赖的向量累加器（GCC/Clang 的 vector_size 扩展），每轮读 128 B，避免一条加法依赖链先成瓶颈；
// - 每个大小先热身一遍，再测 5 次取最快的一次；每次至少读 1 GiB，计时用 steady_clock；
// - Windows / Linux 上把线程绑到 2 号逻辑核（Windows 下 0/1 是同一个物理核的两个 SMT 线程，2 是另一个物理核）。
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#if defined(_WIN32)
#include <windows.h>
#elif defined(__linux__)
#include <pthread.h>
#include <sched.h>
#endif

typedef int v8i __attribute__((vector_size(32)));

static void pin_thread() {
#if defined(_WIN32)
    SetThreadAffinityMask(GetCurrentThread(), DWORD_PTR(1) << 2);
#elif defined(__linux__)
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(2, &set);
    pthread_setaffinity_np(pthread_self(), sizeof set, &set);
#endif
}

// a 按 64 B 对齐，n 是 32 的倍数（每轮 4 个向量 × 8 个 int）
static int sum(const int* a, size_t n) {
    v8i s0 = {}, s1 = {}, s2 = {}, s3 = {};
    for (size_t i = 0; i < n; i += 32) {
        v8i x0, x1, x2, x3;
        std::memcpy(&x0, a + i, 32);
        std::memcpy(&x1, a + i + 8, 32);
        std::memcpy(&x2, a + i + 16, 32);
        std::memcpy(&x3, a + i + 24, 32);
        s0 += x0; s1 += x1; s2 += x2; s3 += x3;
    }
    v8i s = s0 + s1 + s2 + s3;
    int r = 0;
    for (int k = 0; k < 8; ++k) r += s[k];
    return r;
}

static void* alloc64(size_t bytes) {
#if defined(_WIN32)
    return _aligned_malloc(bytes, 64);   // MinGW / MSVC 没有 std::aligned_alloc
#else
    return std::aligned_alloc(64, bytes);
#endif
}

static void free64(void* p) {
#if defined(_WIN32)
    _aligned_free(p);
#else
    std::free(p);
#endif
}

int main() {
    pin_thread();
    const size_t max_bytes = size_t(256) << 20;
    int* a = static_cast<int*>(alloc64(max_bytes));
    for (size_t i = 0; i < max_bytes / sizeof(int); ++i) a[i] = int(i & 7);  // 先把每一页都写过，测的时候不缺页

    // 4 KiB 到 256 MiB，每翻一倍取 4 个点（2^(k/4)），对齐到 128 B
    std::vector<size_t> sizes;
    for (int k = 12 * 4; k <= 28 * 4; ++k) {
        double b = std::pow(2.0, k / 4.0);
        size_t s = (size_t(b) + 127) / 128 * 128;
        if (sizes.empty() || s != sizes.back()) sizes.push_back(s);
    }

    volatile int sink = 0;
    std::printf("bytes,gbps\n");
    for (size_t bytes : sizes) {
        const size_t n = bytes / sizeof(int);
        const size_t reps = std::max<size_t>(1, (size_t(1) << 30) / bytes);
        sink = sink + sum(a, n);  // 热身：把这个大小的数据先搬进能装下它的那一层
        double best = 1e30;
        for (int trial = 0; trial < 5; ++trial) {
            auto t0 = std::chrono::steady_clock::now();
            int acc = 0;
            for (size_t r = 0; r < reps; ++r) acc += sum(a, n);
            auto t1 = std::chrono::steady_clock::now();
            sink = sink + acc;
            best = std::min(best, std::chrono::duration<double>(t1 - t0).count());
        }
        std::printf("%zu,%.2f\n", bytes, double(bytes) * double(reps) / best / 1e9);
        std::fflush(stdout);
    }
    free64(a);
    return sink == 12345 ? 1 : 0;  // 只为让编译器保留 sink
}
