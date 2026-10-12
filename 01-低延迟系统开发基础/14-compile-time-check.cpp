// #14 编译期多态及编译期计算（轻讲）：验证笔记里的代码能编过、结果对。
//   g++ -std=c++23 -O2 -Wall -Wextra 14-compile-time-check.cpp && ./a.out
//   g++ -std=c++23 -O2 -S -o - 14-compile-time-check.cpp   // 看 twice_rt、kPow10、g_counter 放在哪
// 预期输出：buy sell / roar quack / int double / 4 8 / 32 / 1000 / 28 / all ok
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <type_traits>

#define NOINLINE __attribute__((noinline))

// 1.3 模板特化（源 :107-124，补全了 Order / OrderType 和函数体）：每个枚举值一份实现，编译期选
enum class OrderType { BUY, SELL };
struct Order { int qty; };
static const char* g_last;

template <OrderType Type>
struct OrderProcessor;                 // 主模板只声明不定义：没有特化的类型直接编译报错

template <>
struct OrderProcessor<OrderType::BUY> {
    static void process(const Order&) { g_last = "buy"; }
};

template <>
struct OrderProcessor<OrderType::SELL> {
    static void process(const Order&) { g_last = "sell"; }
};

template <OrderType Type>
void processOrder(const Order& order) {
    OrderProcessor<Type>::process(order);
}

// 1.4 if constexpr（源 :130-135）：源写的 static_assert(false) 在 GCC 13 / Clang 18 上能过（P2593 按缺陷修正追溯到旧标准），
//     更早的编译器会不管有没有走到都报错；要兼容旧编译器就写成依赖 Animal 的条件
struct Bear { const char* roar() const { return "roar"; } };
struct Duck { const char* quack() const { return "quack"; } };
template <typename> inline constexpr bool always_false = false;

template <typename Animal>
const char* speak(const Animal& a) {
    if constexpr (std::is_same_v<Animal, Bear>) return a.roar();
    else if constexpr (std::is_same_v<Animal, Duck>) return a.quack();
    else static_assert(always_false<Animal>, "unsupported animal");
}

// 1.5 重载：编译器按实参类型选函数，最简单的编译期多态
const char* describe(int) { return "int"; }
const char* describe(double) { return "double"; }

// 2.2 constexpr 函数：放在要求常量的地方才一定在编译期算；参数是运行时的值就在运行时算
constexpr int twice(int x) { return x + x; }
constexpr int four = twice(2);                    // constexpr 变量：必须编译期算出来
static_assert(four == 4);
NOINLINE int twice_rt(int n) { return twice(n); } // 运行时调用：编出来是一条 addl/leal

// 2.3 consteval：每次调用都必须在编译期算
consteval int get_dim() { return 4; }

// 2.5 非类型模板参数：Dim 编译期定，循环次数是常量（源 :281 的 const int 去掉了，NTTP 顶层 const 没有意义）
template <int Dim>
struct Vector {
    float data[Dim];
    float dot(const Vector& other) const {
        float sum = 0.0f;
        for (int i = 0; i < Dim; i++) sum += data[i] * other.data[i];
        return sum;
    }
};
const int vec_dim = 4;                 // 整型 const、用常量初始化：可以当模板实参
Vector<vec_dim> v1{{1, 2, 3, 4}}, v2{{1, 1, 1, 1}};
Vector<get_dim()> v3{{2, 2, 2, 2}};
// int n = 4; const int runtime_dim = n; Vector<runtime_dim> v4;   // 编译错误：runtime_dim 不是常量表达式

// 2.4 constinit：静态变量的初始化必须在编译期完成，但变量本身不是 const，之后可以改
constexpr int somefunc() { return 27; }    // 必须是 constexpr，否则 constinit 那行编译失败
constinit int g_counter = somefunc();      // 进 .data，程序一启动就是 27，没有运行时初始化
// constinit int g_bad = std::rand();      // 编译错误：初始化器不是常量表达式

// 2.6 if consteval（C++23）：同一个函数，编译期和运行期走不同实现
constexpr std::size_t my_strlen(const char* s) {
    if consteval {
        std::size_t n = 0;
        while (s[n]) ++n;
        return n;
    } else {
        return std::strlen(s);
    }
}
static_assert(my_strlen("hello") == 5);

// 3. 编译期生成查找表：constexpr 函数在编译期填满数组，数组进 .rodata
constexpr std::array<uint64_t, 20> make_pow10() {
    std::array<uint64_t, 20> t{};
    uint64_t v = 1;
    for (auto& x : t) { x = v; v *= 10; }
    return t;
}
constexpr auto kPow10 = make_pow10();
NOINLINE uint64_t pow10(unsigned e) { return kPow10[e]; }   // 一条带下标的 mov

int main() {
    int bad = 0;
    processOrder<OrderType::BUY>(Order{1});
    std::printf("%s ", g_last);
    if (std::strcmp(g_last, "buy")) ++bad;
    processOrder<OrderType::SELL>(Order{1});
    std::printf("%s\n", g_last);
    if (std::strcmp(g_last, "sell")) ++bad;

    std::printf("%s %s\n", speak(Bear{}), speak(Duck{}));
    std::printf("%s %s\n", describe(1), describe(1.0));

    volatile int n = 4;
    std::printf("%d %d\n", four, twice_rt(n));
    if (twice_rt(n) != 8) ++bad;

    float d = v1.dot(v2) * 1 + v3.dot(v2) * 0 + 22;   // 10 + 22
    std::printf("%g\n", d);
    if (d != 32.0f) ++bad;

    std::printf("%llu\n", static_cast<unsigned long long>(pow10(3)));
    if (pow10(3) != 1000) ++bad;

    ++g_counter;                       // constinit 变量可以改
    std::printf("%d\n", g_counter);
    if (g_counter != 28 || my_strlen(g_last) != 4) ++bad;

    std::printf("%s\n", bad ? "MISMATCH" : "all ok");
    return bad != 0;
}
