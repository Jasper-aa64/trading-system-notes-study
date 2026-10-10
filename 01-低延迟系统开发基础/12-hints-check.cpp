// #12 轮 3：提示与分离。验证各段代码能编过、结果对，并用 -S 看布局。
//   g++ -std=c++20 -O2 -Wall -Wextra 12-hints-check.cpp && ./a.out
//   g++ -std=c++20 -O2 -S -o - 12-hints-check.cpp   // 看 process / process_wrong 的布局、.text.unlikely
// PGO 的实验见笔记轮 3 第 3 节（另一个小程序，命令写在笔记里）。不计时。
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <random>
#include <string>
#include <type_traits>
#include <vector>

#define NOINLINE __attribute__((noinline))
#define LIKELY(x) __builtin_expect(!!(x), 1)
#define UNLIKELY(x) __builtin_expect(!!(x), 0)

// 1. 提示改的是布局：UNLIKELY 时热路径顺着往下走，LIKELY 写反时热路径要跳过去
static int g_errors;
NOINLINE void report_error(int n) { g_errors += n <= 0; }
NOINLINE int process(const int* q, int n) {
    if (UNLIKELY(n <= 0)) {
        report_error(n);
        return -1;
    }
    return q[0] + q[n - 1];
}
NOINLINE int process_wrong(const int* q, int n) {
    if (LIKELY(n <= 0)) {          // 提示写反了
        report_error(n);
        return -1;
    }
    return q[0] + q[n - 1];
}
NOINLINE int process_attr(const int* q, int n) {
    if (n <= 0) [[unlikely]] {
        report_error(n);
        return -1;
    }
    return q[0] + q[n - 1];
}

// 2. 冷热分离（源 :438-454）：cold 函数进 .text.unlikely，调用它的那条路径被拆成 xxx.cold
struct Packet {
    int type;
    int len;
    bool is_valid() const { return len > 0; }
};
enum MsgType { TRADE = 1 };
static long long g_traded, g_dropped;
__attribute__((noinline, cold))
void handle_slow_path(const Packet& pkt) {
    g_dropped += 1 + 0 * pkt.len;   // 真实系统里：记日志、计数、丢包
}
NOINLINE void process_packet_refactored(const Packet& pkt) {
    if (!pkt.is_valid() || pkt.type != TRADE) {
        return handle_slow_path(pkt);
    }
    g_traded += pkt.len;
}

// 4. enable_if：源 :228-238 把条件放进默认模板实参，两个模板签名相同，GCC 报 redefinition；
//    要放进非类型模板参数（= 0），或者用 C++20 的 requires
template <typename T, std::enable_if_t<std::is_integral_v<T>, int> = 0>
const char* kind(T) { return "integral"; }
template <typename T, std::enable_if_t<!std::is_integral_v<T>, int> = 0>
const char* kind(T) { return "other"; }

template <typename T> requires std::is_integral_v<T>
const char* kind20(T) { return "integral"; }
template <typename T>
const char* kind20(T) { return "other"; }

// 4. if constexpr：没选中的分支不实例化，所以里面可以写对 T 不成立的代码
template <typename T>
std::size_t get_size(const T& t) {
    if constexpr (requires { t.size(); }) return t.size();
    else return 0;
}

// 4. std::conditional_t：按编译期条件选类型（源 :312-324）
template <typename T>
using TypeSelector = std::conditional_t<std::is_integral_v<T>, int32_t, double>;
static_assert(std::is_same_v<TypeSelector<int>, int32_t>);
static_assert(std::is_same_v<TypeSelector<float>, double>);

// 4. concepts（源 :359-373）
template <typename T>
concept Arithmetic = requires(T a, T b) {
    { a + b } -> std::same_as<T>;
};
template <Arithmetic T>
T add(T a, T b) { return a + b; }

// 5. 半静态分支的最简单替代：标志在循环里不变时，把 if 提到循环外（loop unswitching）
static long long g_a, g_b;
NOINLINE void handle_a(int x) { g_a += x; }
NOINLINE void handle_b(int x) { g_b += x; }
NOINLINE void run_unswitched(const int* xs, int n, bool use_strategy_a) {
    if (use_strategy_a) {
        for (int i = 0; i < n; ++i) handle_a(xs[i]);
    } else {
        for (int i = 0; i < n; ++i) handle_b(xs[i]);
    }
}

// 6. 编译期决策树（源 :597-681，逐字）
#if defined(__GNUC__) || defined(__clang__)
#define HFT_LIKELY(x)   __builtin_expect(!!(x), 1)
#define HFT_FORCE_INLINE [[gnu::always_inline]] inline
#else
#define HFT_LIKELY(x)   (x)
#define HFT_FORCE_INLINE inline
#endif

namespace hft::ct_tree {

struct MarketContext {
    double mid_price{};
    double obi{};
    double volatility{};
    int32_t position{};
};

enum class ActionType : uint8_t { NONE, BUY, SELL, CLOSE };

template<ActionType A>
struct ActionNode {
    HFT_FORCE_INLINE static ActionType evaluate(const MarketContext&) { return A; }
};

template<long ThresX1000>
struct IsHighVol {
    HFT_FORCE_INLINE static bool check(const MarketContext& ctx) {
        return ctx.volatility > static_cast<double>(ThresX1000) / 1000.0;
    }
};

template<long ThresX1000>
struct IsOBIBullish {
    HFT_FORCE_INLINE static bool check(const MarketContext& ctx) {
        return ctx.obi > static_cast<double>(ThresX1000) / 1000.0;
    }
};

template<int MaxPos>
struct IsPositionSafe {
    HFT_FORCE_INLINE static bool check(const MarketContext& ctx) {
        return std::abs(ctx.position) < MaxPos;
    }
};

template<typename Cond, typename Left, typename Right>
struct DecisionNode {
    HFT_FORCE_INLINE static ActionType evaluate(const MarketContext& ctx) {
        if (HFT_LIKELY(Cond::check(ctx))) return Left::evaluate(ctx);
        return Right::evaluate(ctx);
    }
};

template<typename Root>
struct CompiledStrategy {
    HFT_FORCE_INLINE static ActionType execute(const MarketContext& ctx) {
        return Root::evaluate(ctx);
    }
};

template<long Threshold>
using MomentumBlock = DecisionNode<
    IsOBIBullish<Threshold>,
    ActionNode<ActionType::BUY>,
    DecisionNode<
        IsOBIBullish<-Threshold>,
        ActionNode<ActionType::NONE>,
        ActionNode<ActionType::SELL>
    >
>;

using ExampleStrategy = DecisionNode<
    IsPositionSafe<100>,
    DecisionNode<
        IsHighVol<500>,
        ActionNode<ActionType::NONE>,
        MomentumBlock<200>
    >,
    ActionNode<ActionType::CLOSE>
>;

} // namespace hft::ct_tree

using namespace hft::ct_tree;
NOINLINE ActionType decide(const MarketContext& c) {
    return CompiledStrategy<ExampleStrategy>::execute(c);   // -O2：3 条条件跳转 + 1 个 setbe
}

// 6. 同一个策略的另两种写法：手写嵌套 if（GCC -O2 也是 3 条条件跳转 + 1 个 setbe，和上面的树一样），
//    以及运行时用虚函数拼的树（每层一次经函数指针的间接调用 + 一次虚调用 + 读子节点指针）
NOINLINE ActionType decide_hand(const MarketContext& c) {
    if (std::abs(c.position) >= 100) return ActionType::CLOSE;
    if (c.volatility > 0.5) return ActionType::NONE;
    if (c.obi > 0.2) return ActionType::BUY;
    if (c.obi > -0.2) return ActionType::NONE;
    return ActionType::SELL;
}

struct Node {
    virtual ~Node() = default;
    virtual ActionType evaluate(const MarketContext& ctx) const = 0;
};
struct Leaf : Node {
    ActionType action;
    explicit Leaf(ActionType a) : action(a) {}
    ActionType evaluate(const MarketContext&) const override { return action; }
};
struct Decision : Node {
    bool (*check)(const MarketContext&);
    std::unique_ptr<Node> left, right;
    Decision(bool (*c)(const MarketContext&), std::unique_ptr<Node> l, std::unique_ptr<Node> r)
        : check(c), left(std::move(l)), right(std::move(r)) {}
    ActionType evaluate(const MarketContext& ctx) const override {
        return check(ctx) ? left->evaluate(ctx) : right->evaluate(ctx);
    }
};
static std::unique_ptr<Node> build_runtime_tree() {   // 真实系统里从配置文件读出来再拼
    auto leaf = [](ActionType a) { return std::make_unique<Leaf>(a); };
    auto momentum = std::make_unique<Decision>(
        [](const MarketContext& c) { return c.obi > 0.2; }, leaf(ActionType::BUY),
        std::make_unique<Decision>([](const MarketContext& c) { return c.obi > -0.2; },
                                   leaf(ActionType::NONE), leaf(ActionType::SELL)));
    auto inner = std::make_unique<Decision>(
        [](const MarketContext& c) { return c.volatility > 0.5; }, leaf(ActionType::NONE), std::move(momentum));
    return std::make_unique<Decision>(
        [](const MarketContext& c) { return std::abs(c.position) < 100; }, std::move(inner), leaf(ActionType::CLOSE));
}

int main() {
    int bad = 0;
    int q[3] = {4, 5, 6};
    if (process(q, 3) != 10 || process(q, 0) != -1) ++bad;
    if (process_wrong(q, 3) != 10 || process_attr(q, 3) != 10 || process_attr(q, -1) != -1) ++bad;

    process_packet_refactored(Packet{TRADE, 7});
    process_packet_refactored(Packet{2, 7});
    process_packet_refactored(Packet{TRADE, 0});
    if (g_traded != 7 || g_dropped != 2) ++bad;

    if (std::string(kind(1)) != "integral" || std::string(kind(2.5)) != "other") ++bad;
    if (std::string(kind20(1)) != "integral" || std::string(kind20(2.5)) != "other") ++bad;
    if (get_size(std::string("hello")) != 5 || get_size(42) != 0) ++bad;
    if (add(2, 3) != 5 || add(1.5, 2.0) != 3.5) ++bad;

    std::vector<int> xs = {1, 2, 3};
    run_unswitched(xs.data(), 3, true);
    run_unswitched(xs.data(), 3, false);
    if (g_a != 6 || g_b != 6) ++bad;

    // 决策树：仓位超限 -> CLOSE；高波动 -> NONE；OBI > 0.2 -> BUY；OBI > -0.2 -> NONE；否则 SELL
    if (decide({0, 0.0, 0.0, 150}) != ActionType::CLOSE) ++bad;
    if (decide({0, 0.0, 0.9, 10}) != ActionType::NONE) ++bad;
    if (decide({0, 0.5, 0.1, 10}) != ActionType::BUY) ++bad;
    if (decide({0, 0.0, 0.1, 10}) != ActionType::NONE) ++bad;
    if (decide({0, -0.5, 0.1, -10}) != ActionType::SELL) ++bad;

    auto rt = build_runtime_tree();
    std::mt19937 rng(9);
    for (int t = 0; t < 10000; ++t) {
        MarketContext c{0, (int(rng() % 200) - 100) / 100.0, (rng() % 100) / 100.0, int(rng() % 300) - 150};
        if (decide(c) != decide_hand(c) || decide(c) != rt->evaluate(c)) ++bad;
    }

    std::printf("%s\n", bad ? "MISMATCH" : "all ok");
    return bad != 0;
}
