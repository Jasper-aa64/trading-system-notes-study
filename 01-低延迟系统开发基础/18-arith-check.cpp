// #18 强度削弱优化及算术优化：验证程序
// 汇编（按 Linux x86-64 的调用约定出，寄存器和 Linux 上一致）：
//   g++ -std=c++23 -O2 -mabi=sysv -S -masm=att -fno-asynchronous-unwind-tables 18-arith-check.cpp
//   再加 -ffast-math 对比第 6、7 组。
// 运行（第 9 组的数值结果）：
//   g++ -std=c++23 -O2 18-arith-check.cpp -o arith && ./arith
// 本机：GCC 16.2（winlibs mingw64），x86-64，没有计时，周期数是帮助理解的范围。
#include <cmath>
#include <cstdint>
#include <cstdio>

#define NOINLINE __attribute__((noinline))

// 1. 乘以常数（源 :58、:78）
NOINLINE int mul8(int x) { return x * 8; }
NOINLINE int mul5(int x) { return x * 5; }
NOINLINE int mul3(int x) { return x * 3; }
NOINLINE int mul100(int x) { return x * 100; }

// 2. 除以 2 的幂、对 2 的幂取余：无符号和有符号（源 :60、:61）
NOINLINE unsigned udiv4(unsigned x) { return x / 4; }
NOINLINE int sdiv4(int x) { return x / 4; }
NOINLINE int sdiv4_assume(int x) { [[assume(x >= 0)]]; return x / 4; }
NOINLINE unsigned umod512(unsigned x) { return x % 512; }
NOINLINE int smod512(int x) { return x % 512; }

// 3. 除以不是 2 的幂的常数，和运行时才知道的除数（源 :46）
NOINLINE unsigned udiv100(unsigned x) { return x / 100; }
NOINLINE unsigned udivn(unsigned x, unsigned n) { return x / n; }

// 4. 环形下标绕回（源 :154、:164，N = 100）
NOINLINE int wrap_mod(int x) { const int N = 100; return (x + 1) % N; }
NOINLINE int wrap_cmp(int x) { const int N = 100; return (x == N - 1) ? 0 : x + 1; }
NOINLINE unsigned wrap_mod_n(unsigned x, unsigned n) { return (x + 1) % n; }

// 5. 浮点除以常数（源 :90、:94）
NOINLINE float fdiv100(float g) { return g / 100.0f; }
NOINLINE float fdiv4(float g) { return g / 4.0f; }
NOINLINE float fmul_inv100(float g) { const float inv_100 = 1.0f / 100.0f; return g * inv_100; }

// 6. 提公因子：整数编译器会做，浮点不做（源 :285、:299/:308）
NOINLINE int ifactor(int a, int x, int y) { return a * x + a * y; }
NOINLINE float ffactor(float a, float x, float y) { return a * x + a * y; }

// 7. float 和 double 混用（源 :215 全是常量，换成变量才看得出差别）
NOINLINE float scale_src() { float scale = sqrt(2.0) * 3.14159; return scale; }
NOINLINE float mix_double(float x) { return x * 3.14159; }
NOINLINE float pure_float(float x) { return x * 3.14159f; }

// 8. 公共子表达式、布尔代数（源 :271、:287、:288）
NOINLINE float cse_src(int i) { float x = (i * i) + (i * i) + sqrtf(i * i); return x; }
NOINLINE bool and_or(bool a, bool b, bool c) { return (a && b) || (a && c); }
NOINLINE bool and_or2(bool a, bool b, bool c) { return a && (b || c); }

int main() {
    // 9a. 有符号除法向 0 取整，算术右移向负无穷取整
    volatile int m7 = -7, m1 = -1;
    std::printf("-7 / 4 = %d, -7 >> 2 = %d\n", m7 / 4, m7 >> 2);
    std::printf("-1 %% 512 = %d, -1 & 511 = %d\n", m1 % 512, m1 & 511);

    // 9b. g / 100 和 g * 0.01f 在哪些整数 g 上不一样
    int diff = 0, first = 0;
    for (int k = 1; k <= 10000; ++k) {
        volatile float g = (float)k;
        float a = g / 100.0f, b = g * (1.0f / 100.0f);
        if (a != b) {
            if (first < 3) std::printf("g=%d: g/100=%.9g  g*0.01f=%.9g\n", k, a, b);
            ++first; ++diff;
        }
    }
    std::printf("1..10000 里不一样的个数: %d\n", diff);

    // 9c. 19.99f * 5（源 :127）
    volatile float price = 19.99f;
    std::printf("19.99f = %.9g, 19.99f * 5 = %.9g\n", (double)price, (double)(price * 5));

    // 9d. 价格换成分（tick）时截断会差 1
    int bad_mul = 0, bad_div = 0;
    for (int c = 0; c < 10000; ++c) {
        volatile double px = c / 100.0;                // 最接近 c/100 的 double，和写字面量一样
        if ((int)(px * 100) != c) { if (bad_mul < 3) std::printf("(int)(%.2f * 100) = %d\n", (double)px, (int)(px * 100)); ++bad_mul; }
        if ((int)(px / 0.01) != c) ++bad_div;
    }
    std::printf("0.00..99.99 里 (int)(px*100) 差 1 的个数: %d, (int)(px/0.01) 差 1 的个数: %d\n", bad_mul, bad_div);
    volatile double p29 = 0.29;
    std::printf("0.29 * 100 = %.17g, (int) -> %d, llround -> %lld\n", p29 * 100, (int)(p29 * 100), (long long)std::llround(p29 * 100));

    // 让上面的函数留在目标文件里
    volatile int vi = 37; volatile unsigned vu = 37; volatile float vf = 3.0f;
    std::printf("%d %d %d %d %u %d %d %u %d %u %u %d %d %u %g %g %g %d %g %g %g %g %g %d %d\n",
        mul8(vi), mul5(vi), mul3(vi), mul100(vi), udiv4(vu), sdiv4(vi), sdiv4_assume(vi), umod512(vu), smod512(vi),
        udiv100(vu), udivn(vu, 7), wrap_mod(vi), wrap_cmp(vi), wrap_mod_n(vu, 7), fdiv100(vf), fdiv4(vf), fmul_inv100(vf),
        ifactor(vi, 2, 3), ffactor(vf, 2, 3), scale_src(), mix_double(vf), pure_float(vf), cse_src(vi),
        (int)and_or(vi & 1, vi & 2, vi & 4), (int)and_or2(vi & 1, vi & 2, vi & 4));
}
