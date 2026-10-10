// 12 分支优化与分支预测：分支有多“可猜”和速度的关系（实测用，给博客画图）。
// 同一个 even_sum 循环（源 :484-491），数组里偶数占的比例 p 从 0% 扫到 100%，每个 p 输出每个元素花多少 ns；
// p = 50% 那组再排序测一次。输出 CSV（variant,n,p,ns_per_elem），p 为 sorted 表示排序后。
// 数组长度 n 从命令行给（默认 16384，和源笔记一样）。同一组数要反复跑很多遍，n 太小时预测器能把整串方向背下来
// （本机 Ryzen 5 5600GT 上 n = 16384 时 p = 50% 只慢了约 0.2 ns/个），所以要再用一个大的 n（如 1048576）对照。
//
// 构建两个版本（GCC；Clang 没有 -fno-if-conversion，用 Clang 的话只测第一个）：
//   g++ -std=c++20 -O2 12-branch-bench.cpp -o bb-cmov
//   g++ -std=c++20 -O2 -fno-if-conversion -fno-if-conversion2 -fno-tree-vectorize -DBENCH_VARIANT=\"branch\" 12-branch-bench.cpp -o bb-branch
// 运行：./bb-branch 16384 > b16k.csv; ./bb-branch 1048576 > b1m.csv（cmov 版同理），每次约 10–60 秒
// 只测一个 p：./bb-branch 65536 50（第二个参数是 p，用来扫“数组多长预测器就背不下来”）
// （宏名不能叫 VARIANT：Windows 的 oaidl.h 里有同名类型。）
// 第一个版本里编译器把 if 变成 and + cmov（或向量化），没有条件跳转；第二个版本保留 je，才看得到猜错的代价。
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>
#if defined(_WIN32)
#include <windows.h>
#elif defined(__linux__)
#include <pthread.h>
#include <sched.h>
#endif

#ifndef BENCH_VARIANT
#define BENCH_VARIANT "cmov"
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
    const unsigned n = unsigned(data.size());
    const unsigned reps = std::max(1u, (1u << 26) / n);     // 每次约 6700 万个元素
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

int main(int argc, char** argv) {
    pin_thread();
    const unsigned arraySize = argc > 1 ? unsigned(std::strtoul(argv[1], nullptr, 10)) : 16384;
    std::mt19937 rng(1);
    std::vector<int> data(arraySize);
    std::printf("variant,n,p,ns_per_elem\n");
    const int p_only = argc > 2 ? std::atoi(argv[2]) : -1;
    for (int p = 0; p <= 100; p += 5) {
        if (p_only >= 0 && p != p_only) continue;
        // 每个元素以概率 p% 是偶数：0–199 里随机取，再按需要调奇偶
        for (auto& x : data) {
            int v = int(rng() % 200);
            bool even = int(rng() % 100) < p;
            x = (v & ~1) | (even ? 0 : 1);
        }
        std::printf("%s,%u,%d,%.3f\n", BENCH_VARIANT, arraySize, p, ns_per_elem(data));
        std::fflush(stdout);
        if (p == 50) {
            std::vector<int> sorted = data;
            std::sort(sorted.begin(), sorted.end());
            std::printf("%s,%u,sorted,%.3f\n", BENCH_VARIANT, arraySize, ns_per_elem(sorted));
            std::fflush(stdout);
        }
    }
    return 0;
}
