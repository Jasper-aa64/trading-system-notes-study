// #16 指针访存优化（轻讲）：验证笔记里的代码结果对，并用 -S 看 __restrict 和几种“链”编出来的指令。
//   g++ -std=c++20 -O2 -Wall -Wextra 16-ptr-check.cpp && ./a.out
//   g++ -std=c++20 -O2 -S -o - 16-ptr-check.cpp | c++filt     // 看 func / func_restrict / chase_*
//   g++ -std=c++20 -O2 -Wall -DSOURCE_ENCODE 16-ptr-check.cpp  // 源 :157 的 encode：-Wshift-count-overflow，member/contract 丢失
// 预期输出：restrict 7 7 / chain 1024 1024 1024 / encode 12345 67 890 / compress ok / all ok
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <numeric>
#include <random>

#define NOINLINE __attribute__((noinline))

// 1. 源 :10-23（逐字，去掉注释）：不带 restrict 时 -O2 每轮重读 *b；带了以后 *b 只读一次
NOINLINE void func(int *a, int *b, int n) {
    for (int i = 0; i < n; ++i) {
        a[i] = *b;
    }
}
NOINLINE void func_restrict(int *__restrict a, int *__restrict b, int n) {
    for (int i = 0; i < n; ++i) {
        a[i] = *b;
    }
}

// 2. 三种链：4 字节下标、8 字节指针、3 字节紧凑下标。每一步的地址都要等上一步读回来（pointer chasing）
constexpr int N = 1024;
int q[N];
NOINLINE int chase_int(int k, int steps) {                 // movslq (%rcx,%rax,4), %rax：下标 × 4 在寻址里做，不是乘法指令
    for (int i = 0; i < steps; i++) k = q[k];
    return k;
}
struct Node { Node* ptr; };
Node nodes[N];
NOINLINE Node* chase_ptr(Node* k, int steps) {             // movq (%rax), %rax
    for (int i = 0; i < steps; i++) k = k->ptr;
    return k;
}
uint8_t q24[N * 3];
NOINLINE int chase_24(int k, int steps) {                  // 源 :133-139：每步 3 次读 + 移位拼接
    const int MASK_24 = (1 << 24) - 1;
    for (int i = 0; i < steps; i++) {
        int next_val = q24[k] | (q24[k + 1] << 8) | (q24[k + 2] << 16);
        k = next_val & MASK_24;
    }
    return k;
}

// 2.4 源 :150-168：位域是 12 / 20 位时先提升成 int，<< 32、<< 44 是未定义行为；已按实测改正（先转成 uint64_t）
struct CompositeIndex {
    uint64_t client : 32;
    uint64_t member : 12;
    uint64_t contract : 20;
};
uint64_t encode(CompositeIndex idx) {
#ifdef SOURCE_ENCODE
    return (idx.client) | (idx.member << 32) | (idx.contract << 44);
#else
    return uint64_t(idx.client) | (uint64_t(idx.member) << 32) | (uint64_t(idx.contract) << 44);
#endif
}
CompositeIndex decode(uint64_t val) {
    return {
        .client = val & 0xFFFFFFFF,
        .member = (val >> 32) & 0xFFF,
        .contract = (val >> 44) & 0xFFFFF
    };
}

// 3. 指针压缩：存“相对基址的偏移 >> 对齐位数”，0 留给空指针，所以编码 = 偏移 + 1
template <typename T>
struct Compressed32 {
    T* base;
    uint32_t shift;                                         // log2(alignof(T))
    uint32_t pack(const T* p) const {
        if (!p) return 0;
        return uint32_t(((reinterpret_cast<uintptr_t>(p) - reinterpret_cast<uintptr_t>(base)) >> shift) + 1);
    }
    T* unpack(uint32_t c) const {
        if (c == 0) return nullptr;
        return reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(base) + (uintptr_t(c - 1) << shift));
    }
};
struct alignas(16) Order { char pad[16]; };

int main() {
    int bad = 0;

    int a[10], b = 7;
    func(a, &b, 10);
    int x = a[9];
    func_restrict(a, &b, 10);
    std::printf("restrict %d %d\n", x, a[9]);
    if (x != 7 || a[9] != 7) ++bad;

    // 源 :46-54 的构造：随机排列连成一个环（std::random_shuffle 在 C++17 已删除，换成 std::shuffle）
    int p[N];
    std::iota(p, p + N, 0);
    std::shuffle(p, p + N, std::mt19937(42));
    int k = p[N - 1];
    for (int i = 0; i < N; i++) k = q[k] = p[i];
    Node* kp = &nodes[p[N - 1]];
    for (int i = 0; i < N; i++) { kp->ptr = &nodes[p[i]]; kp = kp->ptr; }
    int k3 = p[N - 1] * 3;
    for (int i = 0; i < N; i++) {
        int next = p[i] * 3;
        q24[k3] = next & 0xFF; q24[k3 + 1] = (next >> 8) & 0xFF; q24[k3 + 2] = (next >> 16) & 0xFF;
        k3 = next;
    }
    // 从同一个起点走，第几步回到起点：三种链都应该是 N（一个大环）
    int start = p[N - 1], n1 = 0, n2 = 0, n3 = 0;
    for (int c = chase_int(start, 1), s = 1; ; c = chase_int(c, 1), ++s) if (c == start) { n1 = s; break; }
    for (Node* c = chase_ptr(&nodes[start], 1); ; c = chase_ptr(c, 1)) { ++n2; if (c == &nodes[start]) break; }
    for (int c = chase_24(start * 3, 1), s = 1; ; c = chase_24(c, 1), ++s) if (c == start * 3) { n3 = s; break; }
    std::printf("chain %d %d %d\n", n1, n2, n3);
    if (n1 != N || n2 != N || n3 != N) ++bad;

    CompositeIndex info = decode(encode({12345, 67, 890}));
    std::printf("encode %llu %llu %llu\n", (unsigned long long)info.client,
                (unsigned long long)info.member, (unsigned long long)info.contract);
    if (info.client != 12345 || info.member != 67 || info.contract != 890) ++bad;

    static Order pool[4096];
    Compressed32<Order> cz{pool, 4};
    bool cz_ok = cz.unpack(cz.pack(&pool[0])) == &pool[0] && cz.unpack(cz.pack(&pool[4095])) == &pool[4095]
              && cz.unpack(cz.pack(nullptr)) == nullptr && cz.pack(&pool[0]) != 0;
    std::printf("compress %s\n", cz_ok ? "ok" : "MISMATCH");
    if (!cz_ok) ++bad;

    std::printf("%s\n", bad ? "MISMATCH" : "all ok");
    return bad != 0;
}
