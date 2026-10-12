// #15 循环优化（轻讲）：验证笔记里的代码结果对，并用 -S / -fopt-info 看编译器自己做了哪些循环变换。
//   g++ -std=c++20 -O3 -Wall -Wextra 15-loop-check.cpp && ./a.out
//   g++ -std=c++20 -O3 -S -o - 15-loop-check.cpp | c++filt      // 看 vecdot_basic / vecdot_acc4 / batchnorm_slow
//   g++ -std=c++20 -O3 -c 15-loop-check.cpp -fopt-info-vec -fopt-info-loop   // 哪些循环被向量化、哪个被 unswitch
// 预期输出：dot 70 70 70 / bn ok / ind ok / addsub ok / transpose ok / collapse ok / sentinel 5 / all ok
#include <cmath>
#include <cstdio>
#include <vector>

#define NOINLINE __attribute__((noinline))

// 2.4 / 3.2 源 :38-44（逐字）：基础点积，-O3 下乘法向量化了，但 4 个加法仍按顺序一个接一个
NOINLINE float aussie_vecdot_basic_n(float v1[], float v2[], int n) {
    float sum = 0.0f;
    for (int i = 0; i < n; i++) {
        sum += v1[i] * v2[i];
    }
    return sum;
}

// 2.4 源 :56-71（逐字）：4 级展开，但只有一个 sum，加法链没变短
NOINLINE float aussie_vecdot_unroll4(float v1[], float v2[], int n) {
    float sum = 0.0f;
    int i = 0;
    // 主循环：4 级展开，每次处理 4 个元素
    for (; i < n - 3; i += 4) {
        sum += v1[i] * v2[i];
        sum += v1[i+1] * v2[i+1];
        sum += v1[i+2] * v2[i+2];
        sum += v1[i+3] * v2[i+3];
    }
    // 处理剩余元素（0~3 个）
    for (; i < n; i++) {
        sum += v1[i] * v2[i];
    }
    return sum;
}

// 3.2 四个累加器：四条加法链互不依赖，-O3 合成一条 addps
NOINLINE float vecdot_acc4(const float* v1, const float* v2, int n) {
    float s0 = 0, s1 = 0, s2 = 0, s3 = 0;
    int i = 0;
    for (; i + 3 < n; i += 4) {
        s0 += v1[i] * v2[i];
        s1 += v1[i + 1] * v2[i + 1];
        s2 += v1[i + 2] * v2[i + 2];
        s3 += v1[i + 3] * v2[i + 3];
    }
    for (; i < n; i++) s0 += v1[i] * v2[i];
    return (s0 + s1) + (s2 + s3);
}

// 2.2 强度削弱：10 * i 是归纳变量，-O3 照样向量化（{0,1,2,3} + 4，再移位加法 ×10）
NOINLINE void fill_10i(int* a, int n) {
    for (int i = 0; i < n; i++) a[i] = 10 * i;
}
// 2.2 手写的强度削弱：-O2 下和 fill_10i 编出同一个循环（addl $10）；-O3 向量化成 {0,10,20,30} + 40
NOINLINE void fill_10i_sr(int* a, int n) {
    int x = 0;
    for (int i = 0; i < n; i++) {
        a[i] = x;
        x += 10;
    }
}

// 2.5 源 :25 说的“循环分布”其实叫 loop unswitching：循环里判断一个不变的条件，-O3 把它提到循环外、拆成两个循环
NOINLINE void add_or_sub(float* a, const float* b, int n, bool do_add) {
    for (int i = 0; i < n; i++) {
        if (do_add) a[i] += b[i];
        else        a[i] -= b[i];
    }
}

// 2.5 手写的 unswitching：-O2 下判断只做一次，但仍是逐个 addss / subss（-O2 不向量化）；-O3 和 add_or_sub 一样拆开并向量化
NOINLINE void add_or_sub_unswitched(float* a, const float* b, int n, bool do_add) {
    if (do_add) {
        for (int i = 0; i < n; i++) a[i] += b[i];
    } else {
        for (int i = 0; i < n; i++) a[i] -= b[i];
    }
}

// 4.3 源 :124-145（逐字）：-O3 下 slow 版本身就向量化（subps + divps），拆成两遍反而多扫一遍内存
NOINLINE void batchnorm_slow(float v[], int n, float mean, float var, float eps) {
    float denom = sqrtf(var + eps);
    for (int i = 0; i < n; i++) {
        v[i] = (v[i] - mean) / denom; // 减均值 + 除方差，单循环多操作
    }
}
NOINLINE void batchnorm_fast(float v[], int n, float mean, float var, float eps) {
    float neg_mean = -mean;
    float inv_denom = 1.0f / sqrtf(var + eps);

    // 循环1：减均值（仅加法，可向量化）
    for (int i = 0; i < n; i++) {
        v[i] += neg_mean;
    }

    // 循环2：乘方差倒数（仅乘法，可向量化）
    for (int i = 0; i < n; i++) {
        v[i] *= inv_denom;
    }
}
// 真正省时间的是“除法换成乘倒数”，一遍做完（结果和除法可能差最后一位）
NOINLINE void batchnorm_onepass_mul(float v[], int n, float mean, float var, float eps) {
    float inv_denom = 1.0f / sqrtf(var + eps);
    for (int i = 0; i < n; i++) v[i] = (v[i] - mean) * inv_denom;
}

// 4.2 分块要有“重复使用”才有用：转置按块走，读和写都留在缓存里
constexpr int N = 256, T = 16;
NOINLINE void transpose_tiled(const float* a, float* b) {
    for (int i = 0; i < N; i += T)
        for (int j = 0; j < N; j += T)
            for (int ti = i; ti < i + T; ti++)
                for (int tj = j; tj < j + T; tj++)
                    b[tj * N + ti] = a[ti * N + tj];
}

// 4.4 扁平化：每行 cols 个数，cols 运行时才知道。嵌套版 -O3 只向量化内层，cols = 3 时向量主循环一次都进不去
NOINLINE void scale_nested(float* m, int rows, int cols, float s) {
    for (int i = 0; i < rows; i++)
        for (int j = 0; j < cols; j++)
            m[i * cols + j] *= s;
}
// 4.4 并成一层：一条 rows * cols 长的循环，mulps 一次 4 个
NOINLINE void scale_flat(float* m, int rows, int cols, float s) {
    int total = rows * cols;
    for (int k = 0; k < total; k++)
        m[k] *= s;
}

// 5.2 哨兵：末尾多留一格放一个负数，循环里只判断“是不是负数”，不再判断 i < n
NOINLINE int find_first_negative(int* a, int n) {   // a 至少有 n + 1 格
    int saved = a[n];
    a[n] = -1;
    int i = 0;
    while (a[i] >= 0) ++i;
    a[n] = saved;
    return i;                                       // == n 表示没找到
}

int main() {
    int bad = 0;

    float v1[7] = {1, 2, 3, 4, 5, 6, 7}, v2[7] = {1, 1, 1, 1, 1, 1, 7};
    float d1 = aussie_vecdot_basic_n(v1, v2, 7), d2 = aussie_vecdot_unroll4(v1, v2, 7), d3 = vecdot_acc4(v1, v2, 7);
    std::printf("dot %g %g %g\n", d1, d2, d3);
    if (d1 != 70 || d2 != 70 || d3 != 70) ++bad;

    std::vector<float> x(1000), y(1000), z(1000);
    for (int i = 0; i < 1000; i++) x[i] = y[i] = z[i] = 0.5f * i;
    batchnorm_slow(x.data(), 1000, 3.0f, 4.0f, 0.0f);
    batchnorm_fast(y.data(), 1000, 3.0f, 4.0f, 0.0f);
    batchnorm_onepass_mul(z.data(), 1000, 3.0f, 4.0f, 0.0f);
    bool bn_ok = true;
    for (int i = 0; i < 1000; i++)
        if (std::fabs(x[i] - y[i]) > 1e-4f || std::fabs(x[i] - z[i]) > 1e-4f) bn_ok = false;
    std::printf("bn %s\n", bn_ok ? "ok" : "MISMATCH");
    if (!bn_ok) ++bad;

    std::vector<int> a(100);
    fill_10i(a.data(), 100);
    bool ind_ok = a[0] == 0 && a[37] == 370 && a[99] == 990;
    std::vector<int> a2(100);
    fill_10i_sr(a2.data(), 100);
    if (a2 != a) ind_ok = false;
    std::printf("ind %s\n", ind_ok ? "ok" : "MISMATCH");
    if (!ind_ok) ++bad;

    float p[5] = {1, 2, 3, 4, 5}, q[5] = {1, 1, 1, 1, 1};
    add_or_sub(p, q, 5, true);
    add_or_sub(p, q, 5, false);
    add_or_sub(p, q, 5, false);
    bool as_ok = p[0] == 0 && p[4] == 4;
    float p2[5] = {1, 2, 3, 4, 5};
    add_or_sub_unswitched(p2, q, 5, true);
    add_or_sub_unswitched(p2, q, 5, false);
    add_or_sub_unswitched(p2, q, 5, false);
    if (p2[0] != 0 || p2[4] != 4) as_ok = false;
    std::printf("addsub %s\n", as_ok ? "ok" : "MISMATCH");
    if (!as_ok) ++bad;

    std::vector<float> m(N * N), t(N * N);
    for (int i = 0; i < N * N; i++) m[i] = float(i);
    transpose_tiled(m.data(), t.data());
    bool tr_ok = true;
    for (int i = 0; i < N; i++)
        for (int j = 0; j < N; j++)
            if (t[j * N + i] != m[i * N + j]) tr_ok = false;
    std::printf("transpose %s\n", tr_ok ? "ok" : "MISMATCH");
    if (!tr_ok) ++bad;

    std::vector<float> sn(301), sf(301);
    for (int i = 0; i < 301; i++) sn[i] = sf[i] = float(i);
    scale_nested(sn.data(), 100, 3, 2.0f);
    scale_flat(sf.data(), 100, 3, 2.0f);
    bool sc_ok = sn == sf && sn[299] == 598 && sn[300] == 300;
    std::printf("collapse %s\n", sc_ok ? "ok" : "MISMATCH");
    if (!sc_ok) ++bad;

    int s[9] = {3, 1, 4, 1, 5, -9, 2, 6, 0};        // 前 8 格是数据，第 9 格留给哨兵
    int pos = find_first_negative(s, 8);
    int none[4] = {1, 2, 3, 0};
    std::printf("sentinel %d\n", pos);
    if (pos != 5 || find_first_negative(none, 3) != 3 || s[8] != 0) ++bad;

    std::printf("%s\n", bad ? "MISMATCH" : "all ok");
    return bad != 0;
}
