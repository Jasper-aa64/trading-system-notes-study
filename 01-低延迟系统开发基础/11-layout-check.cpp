// 11 内存对齐与典型内存布局优化 · 轮 2 验证:布局大小、偏移、跨行枚举
// 结构体逐字来自源笔记(标行号);static_assert 钉住笔记里写的每个大小和偏移。
// 构建:g++ -std=c++20 -O1 -Wall -Wextra -Wpedantic 11-layout-check.cpp -o 11-layout-check
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

static_assert(sizeof(void*) == 8, "LP64/x86-64");

// 源 :208-218
struct Point1 { float x, y, z; };
struct Point2 { float x[1000]; float y[1000]; float z[1000]; };

// 源 :402-419(省略 "其他信息" 注释)
struct RiskProfile { uint32_t max_order_size; double max_position_value; };
struct Client { uint32_t client_id; uint32_t risk_profile_id; };
struct Order { uint64_t order_id; uint32_t client_id; uint32_t quantity; double price; };

// 源 :447-459
struct EnrichedOrder {
    uint64_t order_id; uint32_t quantity; double price;
    uint32_t client_id; uint32_t max_order_size; double max_position_value;
};

// 源 :482-490
struct alignas(64) OptimalOrder {
    uint64_t price; uint32_t quantity; uint32_t orderId; uint64_t timestamp; char symbol[8];
};
// 我的对照(不是源码):同样的成员,只把对齐改成 32
struct alignas(32) OrderHalfLine {
    uint64_t price; uint32_t quantity; uint32_t orderId; uint64_t timestamp; char symbol[8];
};
// 我的对照:同样的成员,不写 alignas
struct OrderPlain {
    uint64_t price; uint32_t quantity; uint32_t orderId; uint64_t timestamp; char symbol[8];
};

// 源 :503-512(NAME_LENGTH = 32)
struct First { int nIndex; char Name[32]; };
struct Second { int nIndex; const char* Name; };

static_assert(sizeof(Point1) == 12 && alignof(Point1) == 4);
static_assert(sizeof(Point2) == 12000);
static_assert(sizeof(RiskProfile) == 16 && sizeof(Client) == 8 && sizeof(Order) == 24);
static_assert(sizeof(EnrichedOrder) == 40 && alignof(EnrichedOrder) == 8);
static_assert(offsetof(EnrichedOrder, quantity) == 8 && offsetof(EnrichedOrder, price) == 16);
static_assert(offsetof(EnrichedOrder, client_id) == 24 && offsetof(EnrichedOrder, max_order_size) == 28);
static_assert(offsetof(EnrichedOrder, max_position_value) == 32);
static_assert(offsetof(OptimalOrder, symbol) + sizeof(OptimalOrder::symbol) == 32);
static_assert(sizeof(OptimalOrder) == 64 && alignof(OptimalOrder) == 64);
static_assert(sizeof(OrderHalfLine) == 32 && alignof(OrderHalfLine) == 32);
static_assert(sizeof(OrderPlain) == 32 && alignof(OrderPlain) == 8);
static_assert(sizeof(First) == 36 && alignof(First) == 4);
static_assert(sizeof(Second) == 16 && alignof(Second) == 8);

// 数组首地址对 64 取模 = base,第 i 个元素占 [base+i*size, base+(i+1)*size),
// 算有几个元素跨两条缓存行;只统计一个周期(lcm(size,64)/size 个元素)。
static void straddle(const char* name, size_t size, size_t base) {
    size_t period = 1;
    while ((period * size) % 64 != 0) ++period;
    size_t bad = 0;
    for (size_t i = 0; i < period; ++i) {
        size_t off = (base + i * size) % 64;
        if (off + size > 64) ++bad;
    }
    std::printf("  %-28s size=%-3zu base%%64=%-2zu  跨行 %zu / %zu(一个周期)\n", name, size, base, bad, period);
}

int main() {
    std::printf("跨行枚举(数组首地址的偏移 base):\n");
    straddle("Point1[]", sizeof(Point1), 0);
    straddle("First[](char Name[32])", sizeof(First), 0);
    straddle("Second[](const char*)", sizeof(Second), 0);
    straddle("OrderPlain[](alignof 8)", sizeof(OrderPlain), 0);
    straddle("OrderPlain[](malloc 16 对齐)", sizeof(OrderPlain), 16);
    straddle("OrderHalfLine[](alignas 32)", sizeof(OrderHalfLine), 0);
    straddle("OptimalOrder[](alignas 64)", sizeof(OptimalOrder), 0);

    std::printf("\n只读 x 的缓存行数(数组首地址 64 对齐):\n");
    std::printf("  AoS Point1[1000]:  %zu 字节 -> %zu 条行\n", sizeof(Point1) * 1000, (sizeof(Point1) * 1000 + 63) / 64);
    std::printf("  SoA float x[1000]: %zu 字节 -> %zu 条行\n", sizeof(float) * 1000, (sizeof(float) * 1000 + 63) / 64);

    std::string s;
    std::printf("\nstd::string:sizeof=%zu,空串 capacity=%zu(libstdc++ 的小字符串缓冲)\n", sizeof(s), s.capacity());
    return 0;
}
