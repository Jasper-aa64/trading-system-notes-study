// #19 直接操作内存二进制表示：验证程序
// 运行：g++ -std=c++23 -O2 -Wall 19-bits-check.cpp -o bits && ./bits
//       再加 -fno-strict-aliasing 对比第 2 组。
// 汇编：g++ -std=c++23 -O2 -mabi=sysv -S -fno-asynchronous-unwind-tables（只含 <bit> <cstdint> <cstring> 时可直接编）
// 本机：GCC 16.2（winlibs mingw64），x86-64。
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <variant>

#define NOINLINE __attribute__((noinline))

// 1. 源 :6-:14 原样：用 uint64_t* 改 double 的最高位
int run_source() {
  double x = 100;
  const auto orig_x = x;

  auto x_as_ui = (uint64_t *) (&x);
  *x_as_ui |= 0x8000000000000000;

  printf("orig_x:%0.2f x:%0.2f &x:%p &x_as_ui:%p\n", orig_x, x, (void*)&x, (void*)x_as_ui);
  return 0;
}

// 2. 隔着函数边界，严格别名就出错了
NOINLINE double alias(double* d, uint64_t* u) {
  *d = 1.0;
  *u = 0;          // 编译器认为 uint64_t* 和 double* 不会指向同一个对象
  return *d;       // 于是直接返回 1.0，不重新读
}

// 3. 编译器自己就用位运算
NOINLINE double neg(double x) { return -x; }
NOINLINE double absd(double x) { return std::fabs(x); }
NOINLINE double negabs(double x) { return -std::fabs(x); }

// 4. 正确的写法：memcpy / std::bit_cast
NOINLINE uint64_t bits_memcpy(double x) { uint64_t u; std::memcpy(&u, &x, sizeof u); return u; }
NOINLINE uint64_t bits_cast(double x) { return std::bit_cast<uint64_t>(x); }
NOINLINE double set_sign(double x) { return std::bit_cast<double>(std::bit_cast<uint64_t>(x) | 0x8000000000000000); }
NOINLINE double read_px(const unsigned char* p) { double d; std::memcpy(&d, p, sizeof d); return d; }
NOINLINE uint32_t read_be32(const unsigned char* p) { uint32_t v; std::memcpy(&v, p, sizeof v); return std::byteswap(v); }

// 5. union 双关（源 :18-:26）
union FloatInt { float f; uint32_t i; };
NOINLINE float union_pun(float f) { FloatInt x; x.f = f; x.i |= 0x80000000; return x.f; }

// 6. variant：一块地方放几种类型之一（源 :31、:47）
union U3 { int i; float f; std::array<char, 16> a; };
using V3 = std::variant<int, float, std::array<char, 16>>;

int main() {
  run_source();

  double x = 100.0;
  double r = alias(&x, (uint64_t*)&x);
  printf("alias(): returned %g, memory now holds %g\n", r, x);

  printf("bits(100.0) = 0x%016llx\n", (unsigned long long)bits_cast(100.0));
  printf("bits_memcpy(100.0) = 0x%016llx\n", (unsigned long long)bits_memcpy(100.0));
  printf("set_sign(100.0) = %g, negabs(100.0) = %g, neg(-3) = %g, absd(-3) = %g\n", set_sign(100.0), negabs(100.0), neg(-3), absd(-3));
  printf("union_pun(2.0f) = %g\n", union_pun(2.0f));

  unsigned char buf[16] = {0};
  double px = 101.25; std::memcpy(buf + 3, &px, 8);          // 故意放在不对齐的位置
  printf("read_px(buf + 3) = %g\n", read_px(buf + 3));
  unsigned char be[4] = {0x00, 0x00, 0x01, 0x00};             // 大端的 256
  printf("read_be32 = %u\n", read_be32(be));

  printf("sizeof(U3) = %zu, sizeof(V3) = %zu\n", sizeof(U3), sizeof(V3));
  V3 v = 3.5f;
  std::visit([](auto&& a) { if constexpr (std::is_same_v<std::decay_t<decltype(a)>, float>) printf("visit: float %g\n", a); else printf("visit: other\n"); }, v);
  try { (void)std::get<int>(v); } catch (const std::bad_variant_access&) { printf("get<int> on a float: bad_variant_access\n"); }
}
