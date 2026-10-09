// 11补 轮 2 验证：笔记和博客第 2 部分里的两段写内存代码能编译、结果正确。
// 只验证正确性，不计时（带宽数字是推导的范围，不是这个程序测的）。
// 构建：g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic 11-补-write-check.cpp -o 11-补-write-check
#include <immintrin.h>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// 普通写：每条目的行先 RFO 读回来，改完以后被踢出时再写回去。
void plain_zero(int* dst, size_t n) {
    for (size_t i = 0; i < n; ++i) dst[i] = 0;
}

// 流式写（non-temporal）：不读回、不进缓存，凑满 64 B 直接写到内存。
// 要求 dst 按 16 B 对齐，bytes 是 64 的倍数（整条行写满）。
void stream_zero(void* dst, size_t bytes) {
    __m128i z = _mm_setzero_si128();
    auto* p = static_cast<__m128i*>(dst);
    for (size_t i = 0; i < bytes / 16; ++i) _mm_stream_si128(p + i, z);
    _mm_sfence();   // 流式写是弱序的：发布“写完了”之前要先 sfence
}

int main() {
    const size_t bytes = 1 << 20;
    auto* a = static_cast<int*>(std::aligned_alloc(64, bytes));
    auto* b = static_cast<int*>(std::aligned_alloc(64, bytes));
    std::memset(a, 0xff, bytes);
    std::memset(b, 0xff, bytes);
    plain_zero(a, bytes / sizeof(int));
    stream_zero(b, bytes);
    size_t bad = 0;
    for (size_t i = 0; i < bytes / sizeof(int); ++i) bad += (a[i] != 0) + (b[i] != 0);
    std::printf("plain_zero / stream_zero 写 %zu 字节，非零元素 %zu 个\n", bytes, bad);
    std::free(a);
    std::free(b);
    return bad != 0;
}
