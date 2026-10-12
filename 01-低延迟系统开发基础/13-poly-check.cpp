// 速查页“C++ 语法”第 3 条：静态多态与动态多态。验证页面里的代码能编过、结果对。
//   g++ -std=c++20 -O2 -Wall -Wextra 13-poly-check.cpp && ./a.out
//   g++ -std=c++20 -O2 -S -o - 13-poly-check.cpp   // 看 run_loop<Fast> 里有没有 call
// 预期输出：30 60 / 9 / all ok
#include <cstdio>
#include <memory>
#include <string>
#include <variant>
#include <vector>

#define NOINLINE __attribute__((noinline))

// 1. 运行时从几个“已经写好、编译好”的类型里挑一个：分派在循环外做一次
struct Fast { int apply(int x) const { return x * 2; } };
struct Safe { int apply(int x) const { return x * 4; } };

template <typename Algo>
NOINLINE long long run_loop(const int* xs, int n) {
    Algo a;
    long long s = 0;
    for (int i = 0; i < n; ++i) s += a.apply(xs[i]);   // 编译期已知是哪个 apply，直接内联
    return s;
}

long long run(const std::string& cfg, const int* xs, int n) {
    if (cfg == "fast") return run_loop<Fast>(xs, n);     // 只在这里判断一次
    if (cfg == "safe") return run_loop<Safe>(xs, n);
    return 0;
}

// 2. 几个维度一起分派：std::visit 对两个 variant 自动生成 3 × 3 = 9 份组合
struct A1 {}; struct A2 {}; struct A3 {};
struct B1 {}; struct B2 {}; struct B3 {};
using AlgoV = std::variant<A1, A2, A3>;
using MemV  = std::variant<B1, B2, B3>;

static int g_combos[3][3];
template <typename A, typename B>
NOINLINE void run_combo() {   // 每一种 <A, B> 是一份独立的代码
    constexpr int ia = std::is_same_v<A, A1> ? 0 : std::is_same_v<A, A2> ? 1 : 2;
    constexpr int ib = std::is_same_v<B, B1> ? 0 : std::is_same_v<B, B2> ? 1 : 2;
    ++g_combos[ia][ib];
}

void dispatch(const AlgoV& a, const MemV& m) {
    std::visit([](auto x, auto y) { run_combo<decltype(x), decltype(y)>(); }, a, m);
}

// 3. 动态多态：集合开放，新类型（比如插件里来的）不用改这里
struct Strategy {
    virtual ~Strategy() = default;
    virtual int apply(int x) const = 0;
};
struct Twice : Strategy { int apply(int x) const override { return x * 2; } };

int main() {
    int bad = 0;
    int xs[] = {1, 2, 3, 4, 5};
    long long f = run("fast", xs, 5), s = run("safe", xs, 5);
    std::printf("%lld %lld\n", f, s);
    if (f != 30 || s != 60 || run("x", xs, 5) != 0) ++bad;

    AlgoV as[] = {A1{}, A2{}, A3{}};
    MemV ms[] = {B1{}, B2{}, B3{}};
    for (auto& a : as)
        for (auto& m : ms) dispatch(a, m);
    int n = 0;
    for (auto& row : g_combos)
        for (int c : row) n += (c == 1);
    std::printf("%d\n", n);
    if (n != 9) ++bad;

    std::vector<std::unique_ptr<Strategy>> v;
    v.push_back(std::make_unique<Twice>());
    if (v[0]->apply(21) != 42) ++bad;

    std::printf("%s\n", bad ? "MISMATCH" : "all ok");
    return bad != 0;
}
