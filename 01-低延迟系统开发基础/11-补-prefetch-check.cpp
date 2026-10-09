// 11补 轮 3 验证：笔记和博客第 3 部分里软件预取那段代码能编译、结果和不预取时一样。
// 只验证正确性，不计时（预取距离是按 Little 定律推的，不是这个程序测的）。
// 构建：g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic 11-补-prefetch-check.cpp -o 11-补-prefetch-check
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>

struct Slot { uint64_t id; int64_t qty; };

static uint64_t hash(uint64_t x) { return x * 0x9E3779B97F4A7C15ull; }

static int64_t total = 0;
static void process(const Slot& s) { total += s.qty; }

// 一批订单 id，逐个查哈希表里的槽位；提前 D 个把槽位预取进来
void lookup_batch(const uint64_t* ids, size_t n, const Slot* slots, uint64_t mask, size_t D) {
    for (size_t i = 0; i < n; ++i) {
        if (i + D < n) __builtin_prefetch(&slots[hash(ids[i + D]) & mask]);
        process(slots[hash(ids[i]) & mask]);
    }
}

int main() {
    const uint64_t mask = (1u << 20) - 1;
    std::vector<Slot> slots(mask + 1);
    for (uint64_t i = 0; i <= mask; ++i) slots[i] = {i, int64_t(i % 7)};
    std::vector<uint64_t> ids(100000);
    for (size_t i = 0; i < ids.size(); ++i) ids[i] = i * 31 + 7;

    lookup_batch(ids.data(), ids.size(), slots.data(), mask, 0);
    int64_t no_prefetch = total;
    total = 0;
    lookup_batch(ids.data(), ids.size(), slots.data(), mask, 20);
    std::printf("不预取 %lld，提前 20 个预取 %lld，%s\n", (long long)no_prefetch, (long long)total,
                no_prefetch == total ? "一致" : "不一致");
    return no_prefetch == total ? 0 : 1;
}
