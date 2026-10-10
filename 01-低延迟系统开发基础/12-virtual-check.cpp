// 博客速查页“C++ 语法：虚函数”（content/ref/cpp-syntax）的验证程序，由 #12 轮 3 第 6 节引出。
//   g++ -std=c++20 -O2 -Wall -Wextra 12-virtual-check.cpp && ./a.out      // 期望输出 5 3 2 2 10 16 8
//   g++ -std=c++20 -O2 -S -o - 12-virtual-check.cpp                        // run: movq (%rdi),%rax; jmp *16(%rax)
// GCC 13.3：final / 局部对象 / CRTP 都内联；variant 是读编号 + 2 次比较跳转。不计时。
#include <cstdio>
#include <memory>
#include <variant>
#include <vector>
struct Order {
    virtual ~Order() = default;
    virtual int handle() const = 0;   // 纯虚函数：每个子类自己实现
    int qty = 1;
};
struct LimitOrder : Order {
    int handle() const override { return qty * 2; }
};
struct MarketOrder final : Order {          // final：不会再有子类
    int handle() const override { return qty * 3; }
};
int run(const Order* o) { return o->handle(); }                 // 虚调用
int run_market(const MarketOrder* o) { return o->handle(); }     // final：编译器知道就是这一个
int run_local() { LimitOrder l; return run(&l); }                // 编译器看得见具体类型

// CRTP
template <typename Derived>
struct OrderBase {
    int handle() const { return static_cast<const Derived*>(this)->handle_impl(); }
};
struct Limit2 : OrderBase<Limit2> { int qty = 1; int handle_impl() const { return qty * 2; } };
template <typename T> int run_crtp(const OrderBase<T>& o) { return o.handle(); }
int use_crtp(const Limit2& l) { return run_crtp(l); }

// variant
struct L { int qty; int handle() const { return qty * 2; } };
struct M { int qty; int handle() const { return qty * 3; } };
struct S { int qty; int handle() const { return qty * 5; } };
using AnyOrder = std::variant<L, M, S>;
int run_variant(const AnyOrder& o) { return std::visit([](const auto& x) { return x.handle(); }, o); }

int main() {
    std::vector<std::unique_ptr<Order>> v;
    v.push_back(std::make_unique<LimitOrder>());
    v.push_back(std::make_unique<MarketOrder>());
    int s = 0;
    for (auto& o : v) s += run(o.get());
    MarketOrder m; Limit2 l2;
    std::vector<AnyOrder> av{L{1}, M{1}, S{1}};
    int sv = 0; for (auto& o : av) sv += run_variant(o);
    std::printf("%d %d %d %d %d %zu %zu\n", s, run_market(&m), run_local(), use_crtp(l2), sv, sizeof(LimitOrder), sizeof(AnyOrder));
}
