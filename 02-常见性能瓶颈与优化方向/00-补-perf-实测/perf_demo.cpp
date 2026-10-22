#include <cstdio>
#include <cstring>
#include <vector>

// 冷函数：每轮改写 256 个价格，模拟新行情进来
__attribute__((noinline)) void refresh(float* px, int n, int round) {
    const int start = (round * 256) & (n - 1);  // n 是 2 的幂
    for (int j = 0; j < 256; ++j)
        px[start + j] = 100.0f + 0.01f * static_cast<float>((round + j) & 255);
}

// 热点：只有一个累加器，每次加法都要等上一次加完
__attribute__((noinline)) float sum_serial(const float* a, int n) {
    float s = 0.0f;
    for (int i = 0; i < n; ++i) s += a[i];
    return s;
}

// 修法：四个累加器，四条加法链互不等待
__attribute__((noinline)) float sum_4acc(const float* a, int n) {
    float s0 = 0.0f, s1 = 0.0f, s2 = 0.0f, s3 = 0.0f;
    int i = 0;
    for (; i + 4 <= n; i += 4) {
        s0 += a[i];
        s1 += a[i + 1];
        s2 += a[i + 2];
        s3 += a[i + 3];
    }
    for (; i < n; ++i) s0 += a[i];
    return (s0 + s1) + (s2 + s3);
}

int main(int argc, char** argv) {
    const char* mode = argc > 1 ? argv[1] : "1";
    const int n = 4096;  // 16 KiB，放得进 L1D
    const int rounds = 100000;
    std::vector<float> px(n);
    for (int i = 0; i < n; ++i) px[i] = 100.0f + 0.01f * static_cast<float>(i & 255);

    if (std::strcmp(mode, "check") == 0) {  // 同一份数据：两种求和，加一个 double 参考值
        double ref = 0.0;
        for (float x : px) ref += x;
        std::printf("sum_serial = %.4f\nsum_4acc   = %.4f\ndouble ref = %.4f\n",
                    sum_serial(px.data(), n), sum_4acc(px.data(), n), ref);
        return 0;
    }
    const bool fixed = mode[0] == '4';
    double total = 0.0;
    for (int r = 0; r < rounds; ++r) {
        refresh(px.data(), n, r);
        total += fixed ? sum_4acc(px.data(), n) : sum_serial(px.data(), n);
    }
    std::printf("%s: total = %.6e\n", fixed ? "sum_4acc" : "sum_serial", total);
}
