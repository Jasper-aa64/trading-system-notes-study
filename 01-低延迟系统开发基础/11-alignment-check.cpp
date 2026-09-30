// 11-alignment-check.cpp —— 验证 #11 轮 1 讲到的对齐 / 填充 / 取整数字,不是基准测试。
//
// 编译:g++ -std=c++20 -O1 -Wall -Wextra -Wpedantic 11-alignment-check.cpp -o 11-alignment-check
// 运行:./11-alignment-check                 布局、取整公式、过对齐类型的 new/delete(安全)
//       ./11-alignment-check plain-delete   源码 :79/:81 的写法:aligned new 配普通 delete
//       ./11-alignment-check aligned-delete 正确配对:显式析构 + 带 align_val_t 的 operator delete
//
// 实测环境:g++ 16.2.0(WinLibs,MinGW-w64 UCRT,posix threads),Windows 10,x86-64。
// 这台机器的 MinGW 没有 std::aligned_alloc(编译报 'aligned_alloc' is not a member of 'std'),
// 所以源码 :51-65 的 demonstrate_aligned_alloc 在本机没法跑,笔记里对应处标"未验证";取整公式单独验证。
//
// 内嵌的源码结构体逐字来自 ../../trading-system-notes/chinese/01-low-latency/11-内存对齐与典型内存布局优化.md,
// 下面每处都标了源行号;"我的例子"是笔记为了讲填充规则自己加的,不是源码。

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <new>
#include <string>

// ---- 源 :25-30(逐字)----
struct DefaultAlignedStruct {
    char a;      // 1 byte
    // 3 bytes padding
    int b;       // 4 bytes
    double c;    // 8 bytes
}; // sizeof = 16, alignof = 8

// ---- 源 :38-40(逐字)----
struct alignas(32) AlignasType {
    int data[5]; // 5 * 4 = 20 bytes. 总大小补齐至32字节。
};

// ---- 源 :69-75(逐字)----
class AlignedNewObject {
public:
    AlignedNewObject() { std::cout << "  -> AlignedNewObject 构造\n"; }
    ~AlignedNewObject() { std::cout << "  -> AlignedNewObject 析构\n"; }
private:
    alignas(16) float simd_data[4];
};

// ---- 源 :86-92(逐字)----
#pragma pack(push, 1) // 设置1字节对齐
struct PackedStruct {
    char a;      // 1 byte
    int b;       // 4 bytes
    double c;    // 8 bytes
}; // sizeof = 1+4+8=13, alignof = 1
#pragma pack(pop) // 恢复默认对齐

// ---- 我的例子(不是源码):同样四个成员,顺序不同,sizeof 不同 ----
struct S_bad  { char a; double b; char c; int d; };
struct S_good { double b; int d; char a; char c; };
// 12 字节、alignof 4:按自己的对齐摆放,会不会跨 64 字节的缓存行?
struct Twelve { int x, y, z; };
struct alignas(16) Twelve16 { int x, y, z; };
// 类型自己就过对齐:C++17 起普通 new/delete 会自动走带对齐的版本
struct alignas(64) OverAligned { char x[64]; };
// 类型上 alignas(64):sizeof 自动补成 64 的倍数(100 字节数据 -> 128,即两整条行)
struct alignas(64) Big { char x[100]; };
// 成员 / 变量上 alignas(64) 只管起点,不改 sizeof:b 紧跟在 a 后面,和 a 的尾巴挤在同一条行
struct Two   { alignas(64) char a[100]; char b; };
// 给 b 也加 alignas(64),b 另起一行
struct Three { alignas(64) char a[100]; alignas(64) char b; };

#define OFF(T, m) static_cast<unsigned>(offsetof(T, m))

// 向上取整到 a 的倍数:除法版对任意 a 都对;掩码版只对 a 是 2 的幂才对
static std::size_t up_div(std::size_t n, std::size_t a)  { return ((n + a - 1) / a) * a; }  // 源 :56 的写法
static std::size_t up_mask(std::size_t n, std::size_t a) { return (n + a - 1) & ~(a - 1); } // 源 :157 / :164 的写法

// 编译期钉住笔记里写的数字:哪个不对,这个文件就编译不过
static_assert(sizeof(DefaultAlignedStruct) == 16 && alignof(DefaultAlignedStruct) == 8);
static_assert(offsetof(DefaultAlignedStruct, b) == 4 && offsetof(DefaultAlignedStruct, c) == 8);
static_assert(sizeof(S_bad) == 24 && offsetof(S_bad, b) == 8 && offsetof(S_bad, c) == 16 && offsetof(S_bad, d) == 20);
static_assert(sizeof(S_good) == 16 && offsetof(S_good, d) == 8 && offsetof(S_good, a) == 12 && offsetof(S_good, c) == 13);
static_assert(sizeof(AlignasType) == 32 && alignof(AlignasType) == 32);
static_assert(sizeof(PackedStruct) == 13 && alignof(PackedStruct) == 1);
static_assert(offsetof(PackedStruct, b) == 1 && offsetof(PackedStruct, c) == 5);
static_assert(sizeof(Twelve) == 12 && alignof(Twelve) == 4 && sizeof(Twelve16) == 16 && alignof(Twelve16) == 16);
static_assert(sizeof(OverAligned) == 64 && alignof(OverAligned) == 64);
static_assert(sizeof(Big) == 128 && alignof(Big) == 64);
static_assert(offsetof(Two, b) == 100 && sizeof(Two) == 128);
static_assert(offsetof(Three, b) == 128 && sizeof(Three) == 192);

static void print_layout() {
    std::printf("[layout]\n");
    std::printf("  DefaultAlignedStruct (src :25-30): a@%u b@%u c@%u  sizeof=%zu alignof=%zu\n",
                OFF(DefaultAlignedStruct, a), OFF(DefaultAlignedStruct, b), OFF(DefaultAlignedStruct, c),
                sizeof(DefaultAlignedStruct), alignof(DefaultAlignedStruct));
    std::printf("  S_bad  {char a; double b; char c; int d}: a@%u b@%u c@%u d@%u  sizeof=%zu alignof=%zu\n",
                OFF(S_bad, a), OFF(S_bad, b), OFF(S_bad, c), OFF(S_bad, d), sizeof(S_bad), alignof(S_bad));
    std::printf("  S_good {double b; int d; char a; char c}: b@%u d@%u a@%u c@%u  sizeof=%zu alignof=%zu\n",
                OFF(S_good, b), OFF(S_good, d), OFF(S_good, a), OFF(S_good, c), sizeof(S_good), alignof(S_good));
    std::printf("  AlignasType (src :38-40): sizeof=%zu alignof=%zu  (payload 20 B)\n",
                sizeof(AlignasType), alignof(AlignasType));
    std::printf("  PackedStruct (src :86-92): a@%u b@%u c@%u  sizeof=%zu alignof=%zu\n",
                OFF(PackedStruct, a), OFF(PackedStruct, b), OFF(PackedStruct, c),
                sizeof(PackedStruct), alignof(PackedStruct));

    // 变量上的 alignas(src :45):只改地址,不改 sizeof;alignof(变量) 是 GCC 扩展
    alignas(64) char buffer[100];
    std::printf("  alignas(64) char buffer[100] (src :45): addr%%64=%u sizeof=%zu "
                "alignof(decltype(buffer))=%zu __alignof__(buffer)=%zu\n",
                static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(buffer) % 64), sizeof(buffer),
                alignof(decltype(buffer)), static_cast<std::size_t>(__alignof__(buffer)));

    std::printf("  alignof(max_align_t)=%zu  __STDCPP_DEFAULT_NEW_ALIGNMENT__=%zu  "
                "hardware_destructive_interference_size=%zu\n",
                alignof(std::max_align_t), static_cast<std::size_t>(__STDCPP_DEFAULT_NEW_ALIGNMENT__),
                std::hardware_destructive_interference_size);
}

static void print_straddle() {
    // 一条缓存行 64 B;结构体按自己的对齐摆,起点只能是对齐的倍数。数一数有几种起点会横跨两条行。
    auto count = [](std::size_t size, std::size_t align) {
        int total = 0, straddle = 0;
        for (std::size_t off = 0; off < 64; off += align) { ++total; if (off + size > 64) ++straddle; }
        return std::pair<int, int>{straddle, total};
    };
    auto t12 = count(sizeof(Twelve), alignof(Twelve));
    auto t16 = count(sizeof(Twelve16), alignof(Twelve16));
    auto d8 = count(8, 8);
    auto i4 = count(4, 4);
    std::printf("[straddle] 起点落在行内每个对齐位置时,横跨两条行的个数 / 位置总数\n");
    std::printf("  double (size 8, align 8): %d / %d ; int (size 4, align 4): %d / %d\n",
                d8.first, d8.second, i4.first, i4.second);
    std::printf("  Twelve (size 12, align 4): %d / %d ; Twelve16 (size 16, align 16): %d / %d\n",
                t12.first, t12.second, t16.first, t16.second);
}

static void print_line_sharing() {
    // 行号 = 偏移 / 64。对象的最后一个字节和下一个东西的第一个字节在不在同一条行,决定它们抢不抢这条行。
    std::printf("[line sharing] 行号 = 偏移 / 64\n");
    std::printf("  Big {alignas(64) char x[100]}: sizeof=%zu = %zu 条整行(x 占第 0、1 行;下一个对象从偏移 %zu 起)\n",
                sizeof(Big), sizeof(Big) / 64, sizeof(Big));
    std::printf("  Two {alignas(64) char a[100]; char b}: a 的最后一个字节在第 %u 行, b@%u 在第 %u 行 -> %s\n",
                (OFF(Two, a) + 99) / 64, OFF(Two, b), OFF(Two, b) / 64,
                (OFF(Two, a) + 99) / 64 == OFF(Two, b) / 64 ? "同一条行(共用)" : "不同行");
    std::printf("  Three {alignas(64) char a[100]; alignas(64) char b}: a 的最后一个字节在第 %u 行, b@%u 在第 %u 行 -> %s\n",
                (OFF(Three, a) + 99) / 64, OFF(Three, b), OFF(Three, b) / 64,
                (OFF(Three, a) + 99) / 64 == OFF(Three, b) / 64 ? "同一条行(共用)" : "不同行");
}

static void print_roundup() {
    std::printf("[round-up]\n");
    std::printf("  up_div(200,64)=%zu up_mask(200,64)=%zu ; up_mask(300,64)=%zu\n",
                up_div(200, 64), up_mask(200, 64), up_mask(300, 64));
    int mism64 = 0, mism48 = 0;
    std::size_t first48 = 0;
    for (std::size_t n = 0; n <= 100000; ++n) {
        if (up_div(n, 64) != up_mask(n, 64)) ++mism64;
        if (up_div(n, 48) != up_mask(n, 48)) { if (!mism48) first48 = n; ++mism48; }
    }
    std::printf("  n in [0,100000]: mask 与 div 结果不同的个数  a=64 -> %d ; a=48 -> %d (最先出现在 n=%zu: div=%zu mask=%zu)\n",
                mism64, mism48, first48, up_div(first48, 48), up_mask(first48, 48));

    // AlignedAllocator(src :147-172):≤16 KB 走 64 B 档,更大的走 2 MiB 档,按 2 MiB 取整后整块清零
    const std::size_t small_a = 64, big_a = std::size_t(1) << 21;
    std::printf("  AlignedAllocator 取整(src :155-164): 16 KiB 请求 -> 小档 sz=%zu ; 20 KiB 请求 -> 大档 sz=%zu (%.1f MiB)\n",
                up_mask(16 * 1024, small_a), up_mask(20 * 1024, big_a), up_mask(20 * 1024, big_a) / 1048576.0);
    std::printf("  同一处:2 MiB + 1 字节 -> %zu MiB\n", up_mask(big_a + 1, big_a) >> 20);
}

static void over_aligned_new_delete() {
    auto* p = new OverAligned;   // C++17:类型自己过对齐,new 自动走 operator new(size_t, align_val_t)
    unsigned m = static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(p) % 64);
    delete p;                    // 自动走配套的带对齐 operator delete
    std::printf("[over-aligned type] new OverAligned: addr%%64=%u ; 普通 delete 正常返回\n", m);
}

static int source_aligned_new(bool aligned_delete) {
    constexpr std::size_t alignment = 128;
    auto* obj = new (std::align_val_t{alignment}) AlignedNewObject(); // 源 :79
    std::printf("[source :79-81] addr%%128=%u sizeof=%zu alignof(type)=%zu\n",
                static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(obj) % alignment),
                sizeof(*obj), alignof(AlignedNewObject));
    std::fflush(stdout);
    std::cout.flush();
    if (aligned_delete) {
        obj->~AlignedNewObject();                                   // 先显式析构
        ::operator delete(obj, std::align_val_t{alignment});        // 再还给同一族的 aligned 释放函数
        std::printf("  用 ::operator delete(obj, align_val_t{128}) 释放:正常返回\n");
    } else {
        delete obj;                                                 // 源 :81 的写法
        std::printf("  用普通 delete 释放(源 :81 的写法):正常返回\n");
    }
    return 0;
}

int main(int argc, char** argv) {
    const std::string mode = argc > 1 ? argv[1] : "";
    if (mode == "plain-delete")   return source_aligned_new(false);
    if (mode == "aligned-delete") return source_aligned_new(true);
    print_layout();
    print_straddle();
    print_line_sharing();
    print_roundup();
    over_aligned_new_delete();
    return 0;
}
