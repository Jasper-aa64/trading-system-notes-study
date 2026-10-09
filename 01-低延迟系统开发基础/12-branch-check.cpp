// 12 分支优化与分支预测 · 轮 1 验证：源笔记“排序 vs 不排序”那段循环（源 :484-491）能编译、两种数据的结果一致，
// 并说明怎么看编译器有没有把分支留下来。只验证正确性，不计时。
//
// 构建：g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic 12-branch-check.cpp -o 12-branch-check
// 看汇编：g++ -O2 -S -o - 12-branch-check.cpp | grep -A20 even_sum
//   GCC 13.3（x86-64）从 -O1 起就把内层的 if 变成 and + cmove，内层循环里没有条件跳转，排不排序一样快；
//   -O3 还会向量化。想在实测里看到分支预测失败的代价，要加
//   -fno-if-conversion -fno-if-conversion2 -fno-tree-vectorize，内层才会留下 je。
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <vector>

// 源 :484-491 的循环体（去掉计时；data/arraySize 改成参数）
long long even_sum(const int* data, unsigned arraySize, unsigned reps) {
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

int main() {
    const unsigned arraySize = 16384;
    std::vector<int> data(arraySize);
    std::srand(1);
    for (unsigned c = 0; c < arraySize; ++c) data[c] = std::rand() % 200;

    long long unsorted = even_sum(data.data(), arraySize, 10);
    std::sort(data.begin(), data.end());
    long long sorted = even_sum(data.data(), arraySize, 10);

    // 排序后有多少段“连续相同奇偶”：每段边界上分支方向才会变
    unsigned runs = 1;
    for (unsigned c = 1; c < arraySize; ++c) runs += (data[c] % 2) != (data[c - 1] % 2);
    std::printf("不排序 %lld，排序 %lld，%s；排序后奇偶连续段 %u 段，平均每段 %.1f 个\n",
                unsorted, sorted, unsorted == sorted ? "一致" : "不一致", runs, double(arraySize) / runs);
    return unsorted == sorted ? 0 : 1;
}
