// #14 轮 1 CRTP：验证笔记和博客里的代码能编过、结果对，并用 -S 对比 CRTP 和虚函数编出来的指令。
//   g++ -std=c++20 -O2 -Wall -Wextra 14-crtp-check.cpp && ./a.out
//   g++ -std=c++20 -O2 -S -o - 14-crtp-check.cpp      // 看 run_crtp / run_virtual
//   clang++ -std=c++23 -O2 -DDEDUCING_THIS 14-crtp-check.cpp && ./a.out   // 第 6 节：C++23 deducing this（GCC 14 起、Clang 18 支持，GCC 13 不支持）
//   g++ -std=c++20 -fsyntax-only -DWRONG_BASE 14-crtp-check.cpp           // 第 4.1 节：基类写错，加了私有构造后编译报错
// 预期输出：Derived1… / Derived2… / 6 9 / 1 0 1 / 2 1 / all ok
#include <cstdio>
#include <iostream>
#include <memory>

#define NOINLINE __attribute__((noinline))

// 1. 源 :10-51（逐字）
// 基类模板
template <typename Derived>
class Base {
public:
    // 调用派生类的具体实现
    void doSomething() {
        static_cast<Derived*>(this)->doSomethingImpl();
    }
};

// 派生类
class Derived1 : public Base<Derived1> {
public:
    // 派生类的具体实现
    void doSomethingImpl() {
        std::cout << "Derived1 is doing something." << std::endl;
    }
};

// 另一个派生类
class Derived2 : public Base<Derived2> {
public:
    // 派生类的具体实现
    void doSomethingImpl() {
        std::cout << "Derived2 is doing something." << std::endl;
    }
};

// 辅助函数，接受基类引用并调用其doSomething方法
template <typename T>
void performAction(Base<T>& obj) {
    obj.doSomething();
}

// 2. CRTP vs 虚函数：同一个 handle()，看编出来的指令
template <typename Derived>
struct FeedBase {
    int on_tick(int px) { return static_cast<Derived*>(this)->on_tick_impl(px); }
};
struct BinanceFeed : FeedBase<BinanceFeed> {
    int mult = 2;
    int on_tick_impl(int px) { return px * mult; }
};
template <typename T>
NOINLINE int run_crtp(FeedBase<T>& f, int px) { return f.on_tick(px); }   // movl + imull，没有 call

struct IFeed {
    virtual ~IFeed() = default;
    virtual int on_tick(int px) = 0;
};
struct OkxFeed : IFeed {
    int mult = 3;
    int on_tick(int px) override { return px * mult; }
};
NOINLINE int run_virtual(IFeed& f, int px) { return f.on_tick(px); }      // 读 vptr、读 vtable，和 OkxFeed::on_tick 比一下：是就走内联版，不是就间接跳转

// 3.2 往子类里注入功能：子类只写 ==，基类免费给出 !=
template <typename Derived>
struct EqualityOps {
    friend bool operator!=(const Derived& a, const Derived& b) { return !(a == b); }
};
struct Price : EqualityOps<Price> {
    long ticks;
    friend bool operator==(const Price& a, const Price& b) { return a.ticks == b.ticks; }
};

// 3.3 每个子类一份独立的静态计数：Base<A> 和 Base<B> 是两个类型，静态成员各一份
template <typename Derived>
struct InstanceCounter {
    static inline int alive = 0;
    InstanceCounter() { ++alive; }
    InstanceCounter(const InstanceCounter&) { ++alive; }
    ~InstanceCounter() { --alive; }
};
struct OrderA : InstanceCounter<OrderA> {};
struct OrderB : InstanceCounter<OrderB> {};

// 4.1 基类写错：用私有构造 + friend Derived 在编译期挡住
template <typename Derived>
class SafeBase {
public:
    void run() { static_cast<Derived*>(this)->run_impl(); }
private:
    SafeBase() = default;
    friend Derived;                    // 只有 Derived 能构造 SafeBase<Derived>
};
struct GoodA : SafeBase<GoodA> { void run_impl() {} };
#ifdef WRONG_BASE
struct BadB : SafeBase<GoodA> { void run_impl() {} };   // 抄错了基类：编译报错（构造函数是私有的）
BadB bad_b;
#endif

// 6. C++23 deducing this：不用把子类写进模板参数
#ifdef DEDUCING_THIS
struct FeedBase23 {
    template <typename Self>
    int on_tick(this Self&& self, int px) { return self.on_tick_impl(px); }
};
struct BybitFeed : FeedBase23 {
    int on_tick_impl(int px) { return px * 4; }
};
#endif

int main() {
    int bad = 0;
    Derived1 d1;
    Derived2 d2;
    performAction(d1);
    performAction(d2);

    BinanceFeed b;
    OkxFeed o;
    int x = run_crtp(b, 3), y = run_virtual(o, 3);
    std::printf("%d %d\n", x, y);
    if (x != 6 || y != 9) ++bad;

    Price p1{{}, 100}, p2{{}, 101}, p3{{}, 100};
    std::printf("%d %d %d\n", p1 != p2, p1 != p3, p1 == p3);
    if (!(p1 != p2) || (p1 != p3)) ++bad;

    OrderA a1, a2;
    {
        OrderB b1;
        OrderA a3;
    }
    OrderB b2;
    std::printf("%d %d\n", InstanceCounter<OrderA>::alive, InstanceCounter<OrderB>::alive);
    if (InstanceCounter<OrderA>::alive != 2 || InstanceCounter<OrderB>::alive != 1) ++bad;

    GoodA g;
    g.run();

#ifdef DEDUCING_THIS
    BybitFeed bb;
    if (bb.on_tick(3) != 12) ++bad;
#endif

    std::printf("%s\n", bad ? "MISMATCH" : "all ok");
    return bad != 0;
}
