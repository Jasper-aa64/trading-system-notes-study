// #12 轮 2：消除分支的几种写法，验证结果一致，并用 -S 看编译器生成了什么。
//   g++ -std=c++20 -O2 -Wall -Wextra 12-branchless-check.cpp && ./a.out
//   g++ -std=c++20 -O2 -S -o - 12-branchless-check.cpp   // 看各函数的汇编
// 只验证正确性和指令形态，不计时（计时见 12-branch-bench.cpp）。
#include <cstdio>
#include <cstdint>
#include <random>
#include <vector>

#define NOINLINE __attribute__((noinline))

// 2.1 三元：编译器用 cmp + cmovg 选值，没有跳转
NOINLINE int pick(int input, int threshold, int value1, int value2) {
    return (input > threshold) ? value1 : value2;
}

// 2.1 两边有一边要读内存、可能读不了：编译器不敢变成 cmov
NOINLINE int load_or_zero(const int* p) {
    return p ? *p : 0;
}

// 2.2 sign：比较结果本身就是 0 或 1（源 :110-119）
NOINLINE int sign_branchy(int x) {
    if (x > 0) return 1;
    if (x < 0) return -1;
    return 0;
}
NOINLINE int sign_branchless(int x) {
    return (x > 0) - (x < 0);
}

// 2.2 掩码选择：cond 为真时 mask 全 1，为假时全 0
NOINLINE int select_mask(bool cond, int a, int b) {
    int mask = -static_cast<int>(cond);  // 1 -> 0xFFFFFFFF，0 -> 0
    return (a & mask) | (b & ~mask);
}

// 2.2 even_sum 的无分支写法：x 是偶数时加 x，否则加 0
NOINLINE long long even_sum_mask(const int* data, unsigned n) {
    long long sum = 0;
    for (unsigned c = 0; c < n; ++c) {
        int x = data[c];
        sum += x & -static_cast<int>((x & 1) == 0);
    }
    return sum;
}
NOINLINE long long even_sum_if(const int* data, unsigned n) {
    long long sum = 0;
    for (unsigned c = 0; c < n; ++c)
        if (data[c] % 2 == 0) sum += data[c];
    return sum;
}

// 2.3 值查表：按订单类型取手续费率（万分之几），没有分支
constexpr int fee_bps[3] = {2, 5, 10};
NOINLINE int fee_lookup(unsigned type) { return fee_bps[type]; }

// 2.3 函数指针表（源 :79-95）：一次间接调用
struct Order { unsigned type; int qty; };
static long long handled[3];
NOINLINE void handle_type_0(const Order& o) { handled[0] += o.qty; }
NOINLINE void handle_type_1(const Order& o) { handled[1] += o.qty; }
NOINLINE void handle_type_2(const Order& o) { handled[2] += o.qty; }
using HandlerFunc = void (*)(const Order&);
constexpr HandlerFunc handlers[] = {handle_type_0, handle_type_1, handle_type_2};
NOINLINE void process_order(const Order& order) {
    if (order.type < 3) {
        handlers[order.type](order);
    }
}

// 2.4 switch：case 连续，每个 case 做不同的事 -> 跳转表（jmp *表(,%rax,8)）
NOINLINE void on_msg(unsigned kind, const Order& o) {
    switch (kind) {
        case 0: handle_type_0(o); break;
        case 1: handle_type_1(o); break;
        case 2: handle_type_2(o); break;
        case 3: handle_type_0(o); handle_type_1(o); break;
        case 4: handle_type_2(o); handle_type_2(o); break;
        default: break;
    }
}
// 2.4 switch：每个 case 只返回一个值 -> 编译器直接换成值查表，连跳转表都没有
NOINLINE int fee_switch(unsigned type) {
    switch (type) {
        case 0: return 2;
        case 1: return 5;
        case 2: return 10;
        case 3: return 20;
        case 4: return 30;
        default: return 0;
    }
}
// 2.4 switch：case 稀疏 -> 一串比较
NOINLINE int sparse_switch(int v) {
    switch (v) {
        case 1: return 7;
        case 10: return 3;
        case 1000: return 9;
        default: return 0;
    }
}

// 2.5 循环展开：每 4 个元素才有一次回跳
NOINLINE long long sum_plain(const int* a, unsigned n) {
    long long s = 0;
    for (unsigned i = 0; i < n; ++i) s += a[i];
    return s;
}
NOINLINE long long sum_unroll4(const int* a, unsigned n) {
    long long s0 = 0, s1 = 0, s2 = 0, s3 = 0;
    unsigned i = 0;
    for (; i + 4 <= n; i += 4) {
        s0 += a[i];
        s1 += a[i + 1];
        s2 += a[i + 2];
        s3 += a[i + 3];
    }
    for (; i < n; ++i) s0 += a[i];
    return s0 + s1 + s2 + s3;
}

int main() {
    int bad = 0;
    for (int x = -3; x <= 3; ++x)
        if (sign_branchy(x) != sign_branchless(x)) ++bad;
    for (int c = 0; c < 2; ++c)
        if (select_mask(c, 11, -22) != (c ? 11 : -22)) ++bad;
    if (pick(5, 3, 1, 2) != 1 || pick(1, 3, 1, 2) != 2) ++bad;
    int v = 42;
    if (load_or_zero(&v) != 42 || load_or_zero(nullptr) != 0) ++bad;

    std::mt19937 rng(1);
    std::vector<int> d(16384);
    for (int& x : d) x = static_cast<int>(rng() % 200) - 100;  // 带负数，检查负奇数也对
    if (even_sum_mask(d.data(), d.size()) != even_sum_if(d.data(), d.size())) ++bad;
    if (sum_plain(d.data(), 16383) != sum_unroll4(d.data(), 16383)) ++bad;

    for (unsigned t = 0; t < 3; ++t)
        if (fee_lookup(t) != fee_switch(t)) ++bad;
    if (sparse_switch(1000) != 9 || sparse_switch(5) != 0) ++bad;

    for (unsigned t = 0; t < 5; ++t) process_order(Order{t, 1});  // type 3、4 被边界检查挡掉
    for (unsigned k = 0; k < 6; ++k) on_msg(k, Order{0, 1});
    std::printf("handled = %lld %lld %lld\n", handled[0], handled[1], handled[2]);
    if (handled[0] != 3 || handled[1] != 3 || handled[2] != 4) ++bad;

    std::printf("%s\n", bad ? "MISMATCH" : "all ok");
    return bad != 0;
}
