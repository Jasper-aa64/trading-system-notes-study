// 12 分支优化与分支预测：分支有多“可猜”和速度的关系（实测用，给博客画图）。
// 同一个 even_sum 循环（源 :484-491），数组里偶数占的比例 p 从 0% 扫到 100%，每个 p 输出每个元素花多少 ns；
// 最后一行是把 p = 50% 的数组排序以后再测。输出 CSV（variant,p,ns_per_elem），p 为 sorted 表示排序后。
//
// 构建两个版本（GCC；Clang 没有 -fno-if-conversion，用 Clang 的话只测第一个）：
//   g++ -std=c++20 -O2 12-branch-bench.cpp -o bb-cmov
//   g++ -std=c++20 -O2 -fno-if-conversion -fno-if-conversion2 -fno-tree-vectorize -DVARIANT=\"branch\" 12-branch-bench.cpp -o bb-branch
// 运行：./bb-cmov > cmov.csv; ./bb-branch > branch.csv（各约 10–30 秒）
// 第一个版本里编译器把 if 变成 and + cmov（或向量化），没有条件跳转；第二个版本保留 je，才看得到猜错的代价。
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <random>
#include <vector>
#if defined(_WIN32)
#include <windows.h>
#elif defined(__linux__)
#include <pthread.h>
#include <sched.h>
#endif

#ifndef VARIANT
#define VARIANT "cmov"
#endif

static void pin_thread() {
#if defined(_WIN32)
    SetThreadAffinityMask(GetCurrentThread(), DWORD_PTR(1) << 2);  // 2 号逻辑核：和 0/1 不是同一个物理核
#elif defined(__linux__)
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(2, &set);
    pthread_setaffinity_np(pthread_self(), sizeof set, &set);
#endif
}

// 源 :484-491 的循环体（去掉计时；data/arraySize 改成参数）
__attribute__((noinline)) long long even_sum(const int* data, unsigned arraySize, unsigned reps) {
    long long evenSum = 0;
    for (unsigned i = 0; i < reps; ++i) {
        for (unsigned c = 0; c < arraySize; ++c) {
            if (data[c] % 2 == 0) {
                evenSum += data[c];
            }
        }
    }
    return evenSum;
}

static volatile long long sink;

static double ns_per_elem(const std::vector<int>& data) {
    const unsigned n = unsigned(data.size()), reps = 4000;  // 每次约 6500 万个元素
    sink = even_sum(data.data(), n, 10);                    // 热身
    double best = 1e30;
    for (int trial = 0; trial < 5; ++trial) {
        auto t0 = std::chrono::steady_clock::now();
        sink = even_sum(data.data(), n, reps);
        auto t1 = std::chrono::steady_clock::now();
        best = std::min(best, std::chrono::duration<double, std::nano>(t1 - t0).count());
    }
    return best / (double(n) * reps);
}

int main() {
    pin_thread();
    const unsigned arraySize = 16384;
    std::mt19937 rng(1);
    std::vector<int> data(arraySize);
    std::printf("variant,p,ns_per_elem\n");
    for (int p = 0; p <= 100; p += 5) {
        // 每个元素以概率 p% 是偶数：0–199 里随机取，再按需要调奇偶
        for (auto& x : data) {
            int v = int(rng() % 200);
            bool even = int(rng() % 100) < p;
            x = (v & ~1) | (even ? 0 : 1);
        }
        std::printf("%s,%d,%.3f\n", VARIANT, p, ns_per_elem(data));
        std::fflush(stdout);
        if (p == 50) {
            std::vector<int> sorted = data;
            std::sort(sorted.begin(), sorted.end());
            std::printf("%s,sorted,%.3f\n", VARIANT, ns_per_elem(sorted));
            std::fflush(stdout);
        }
    }
    return 0;
}
