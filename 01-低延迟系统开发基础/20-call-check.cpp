// #20 无法内联的函数调用优化：验证程序（只看汇编）
// 汇编：g++ -std=c++23 -O2 -mabi=sysv -S -fno-asynchronous-unwind-tables 20-call-check.cpp
//       -O3 -fopt-info-vec-optimized 看 scale_ref 的“因为可能重叠而分版本”。
// 本机：GCC 16.2（winlibs mingw64），x86-64；-mabi=sysv 让寄存器和 Linux 上一致。

#include <cstdint>
#define NOINLINE __attribute__((noinline))
// 参数：标量按值 vs const&
NOINLINE int use_val(int x) { return x * 3; }
NOINLINE int use_ref(const int& x) { return x * 3; }
int call_val(int a) { return use_val(a + 1); }
int call_ref(int a) { return use_ref(a + 1); }
// const& 参数可能和别的指针重叠，循环里每轮都要重读
NOINLINE void scale_ref(double* v, int n, const double& f) { for (int i = 0; i < n; ++i) v[i] *= f; }
NOINLINE void scale_val(double* v, int n, double f) { for (int i = 0; i < n; ++i) v[i] *= f; }
// 结构体按值：16 字节以内拆进寄存器，再大就走内存
struct Small { double px; int64_t qty; };
struct Big { double px; int64_t qty; int64_t id; };
NOINLINE double f_small(Small s) { return s.px * s.qty; }
NOINLINE double f_big(Big b) { return b.px * b.qty; }
double call_small(double p, int64_t q) { return f_small({p, q}); }
double call_big(double p, int64_t q) { return f_big({p, q, 7}); }
// 递归：尾调用变成跳转
struct Node { int val; Node* left; Node* right; };
NOINLINE void visit(Node*);
void preorder(Node* root) { if (root) { visit(root); preorder(root->left); preorder(root->right); } }
