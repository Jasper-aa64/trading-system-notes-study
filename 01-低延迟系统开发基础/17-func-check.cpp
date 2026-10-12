// #17 关于函数的潜在优化（轻讲）：源 :5-198 的程序（逐字，两处改正见注释）+ 纯函数属性和返回值传递的验证。
//   g++ -std=c++17 -O2 -Wall 17-func-check.cpp && ./a.out
//   g++ -std=c++17 -O2 -fno-elide-constructors 17-func-check.cpp && ./a.out   // 只有 NRVO 那一处多出一次移动，prvalue 的消除不受影响
//   clang++ -std=c++17 -O2 17-func-check.cpp && ./a.out                       // 条件返回的 named 分支：Clang 做了 NRVO，GCC 13 是一次移动
//   g++ -std=c++17 -O2 -S -o - 17-func-check.cpp | c++filt                   // 看 Func2 / G2 / P2 / C2 各 call 几次，make_snap 写进 (%rdi)
// 源里的析构函数写成了全角“～Tracer”，编不过；已按实测改成 ~。
#include <iostream>
#include <string>
#include <vector>
#include <memory>

// 用于追踪对象生命周期的基类
class Tracer {
public:
    Tracer(const std::string& name = "default") : name_(name) {   // 源 :13 没有默认参数，createObject<Tracer>() 编不过；已按实测改正
        std::cout << "[" << name_ << "] Default constructor @ " << this << "\n";
    }

    Tracer(const Tracer& other) : name_(other.name_ + "_copy") {
        std::cout << "[" << name_ << "] Copy constructor from " << &other << " to " << this << "\n";
    }

    Tracer(Tracer&& other) noexcept : name_(std::move(other.name_) + "_moved") {
        std::cout << "[" << name_ << "] Move constructor from " << &other << " to " << this << "\n";
    }

    ~Tracer() {
        std::cout << "[" << name_ << "] Destructor @ " << this << "\n";
    }

    Tracer& operator=(const Tracer& other) {
        if (this != &other) {
            name_ = other.name_ + "_copy_assigned";
            std::cout << "[" << name_ << "] Copy assignment from " << &other << " to " << this << "\n";
        }
        return *this;
    }

    Tracer& operator=(Tracer&& other) noexcept {
        if (this != &other) {
            name_ = std::move(other.name_) + "_move_assigned";
            std::cout << "[" << name_ << "] Move assignment from " << &other << " to " << this << "\n";
        }
        return *this;
    }

    const std::string& name() const { return name_; }

private:
    std::string name_;
};

// 不可拷贝也不可移动的类（C++17 之前无法返回这类对象）
class NonCopyableNonMovable {
public:
    NonCopyableNonMovable(const std::string& id) : id_(id) {
        std::cout << "[NonCopyableNonMovable] Constructor: " << id_ << " @ " << this << "\n";
    }

    ~NonCopyableNonMovable() {
        std::cout << "[NonCopyableNonMovable] Destructor: " << id_ << " @ " << this << "\n";
    }

    // 禁止拷贝和移动
    NonCopyableNonMovable(const NonCopyableNonMovable&) = delete;
    NonCopyableNonMovable& operator=(const NonCopyableNonMovable&) = delete;
    NonCopyableNonMovable(NonCopyableNonMovable&&) = delete;
    NonCopyableNonMovable& operator=(NonCopyableNonMovable&&) = delete;

private:
    std::string id_;
};

// 原始代码中的结构
struct LargeClass {
    int i;
    char c;
    double d;
};

// 基本 RVO：返回临时对象（C++17 起强制拷贝消除）
auto rvoExample(int i, char c, double d) {
    return LargeClass{i, c, d};  // 不要在这里使用 std::move，会阻止编译器进行完全优化
}

// NRVO：返回命名对象（不是强制的，取决于编译器）
Tracer nrvoExample(const std::string& name) {
    Tracer obj(name);  // 命名局部对象
    return obj;        // 现代编译器通常会优化掉拷贝/移动
}

// 混合返回路径（可能阻止 NRVO）
Tracer conditionalReturn(bool flag) {
    if (flag) {
        return Tracer("temporary");  // 临时对象 - 更容易优化
    } else {
        Tracer named("named");       // 命名对象
        return named;                // 可能优化，取决于编译器
    }
}

// 多返回点（现代编译器通常能处理）
Tracer multipleReturns(int value) {
    if (value < 0) return Tracer("negative");
    if (value == 0) return Tracer("zero");
    return Tracer("positive");  // 所有路径返回相同类型
}

// C++17 强制拷贝消除示例：返回不可拷贝/不可移动对象
NonCopyableNonMovable createNonCopyableNonMovable() {
    return NonCopyableNonMovable("uncopyable_object");  // C++17 前无法编译
}

// 无法应用 RVO 的情况：返回参数
Tracer returnParameter(Tracer param) {
    // 通常无法对参数应用 RVO，因为 param 已经是现有对象
    return param;  // 会调用移动构造函数（如果有）或拷贝构造函数
}

// 无法应用 RVO 的情况：返回静态对象
Tracer& getStaticTracer() {
    static Tracer instance("static_instance");
    return instance;  // 返回引用，不涉及 RVO
}

// 模板函数中的 RVO
template <typename T>
T createObject() {
    return T();  // 模板中的 RVO，取决于实例化类型
}

// 返回 std::vector（大型对象）的示例
std::vector<int> createLargeVector() {
    std::vector<int> vec(1000, 42);  // 创建大向量
    return vec;  // NRVO 适用，或 C++11 后会使用移动语义
}

// 有副作用的计数器（注意：依赖拷贝副作用是危险的）
class Counter {
public:
    Counter() { ++instance_count_; }
    Counter(const Counter&) { ++instance_count_; }
    ~Counter() { --instance_count_; }

    static int instance_count() { return instance_count_; }

private:
    static int instance_count_;
};
int Counter::instance_count_ = 0;

Counter createCounter() {
    return Counter();  // RVO 会影响 instance_count_ 的值
}

int run_source() {
    std::cout << "===== 基本 RVO 示例 =====\n";
    LargeClass lc_obj = rvoExample(10, 'c', 3.14);

    std::cout << "\n===== NRVO 示例 =====\n";
    Tracer nrvo_obj = nrvoExample("nrvo_test");

    std::cout << "\n===== 条件返回示例 =====\n";
    Tracer cond_obj1 = conditionalReturn(true);
    Tracer cond_obj2 = conditionalReturn(false);

    std::cout << "\n===== 多返回点示例 =====\n";
    Tracer multi_obj1 = multipleReturns(-1);
    Tracer multi_obj2 = multipleReturns(0);
    Tracer multi_obj3 = multipleReturns(1);

    std::cout << "\n===== C++17 强制拷贝消除示例 =====\n";
    auto non_copyable = createNonCopyableNonMovable();  // C++17 前无法编译

    std::cout << "\n===== 无法应用 RVO 的情况：返回参数 =====\n";
    Tracer param_obj = returnParameter(Tracer("parameter"));

    std::cout << "\n===== 无法应用 RVO 的情况：返回静态对象 =====\n";
    Tracer& static_ref = getStaticTracer();
    std::cout << "Static object name: " << static_ref.name() << "\n";

    std::cout << "\n===== 模板函数中的 RVO =====\n";
    auto template_obj = createObject<Tracer>();

    std::cout << "\n===== 大型对象返回示例 =====\n";
    auto large_vec = createLargeVector();
    std::cout << "Vector size: " << large_vec.size() << "\n";

    std::cout << "\n===== 依赖拷贝副作用的危险示例 =====\n";
    {
        Counter c1 = createCounter();
        std::cout << "Counter instances after RVO: " << Counter::instance_count() << "\n";
    }

    // 演示强制拷贝（禁用 RVO）的情况
    std::cout << "\n===== 禁用 RVO 的情况（如果编译时使用 -fno-elide-constructors） =====\n";
    Tracer forced_copy = Tracer("forced_copy");  // 如果使用 -fno-elide-constructors，这会产生额外拷贝

    return 0;
}

// 2. 纯函数属性：const 只调一次；不带属性调两次；pure 中间写了全局变量就得再调一次
double Func1(double) __attribute__((const));
__attribute__((noipa)) double Func1(double x) { return x * 0.5; }   // noipa：不让编译器看函数体自己推断，只按声明的属性
__attribute__((noinline)) double Func2(double x) { return Func1(x) * Func1(x) + 1.; }   // 1 次 call
double G1(double);
__attribute__((noipa)) double G1(double x) { return x * 0.5; }
__attribute__((noinline)) double G2(double x) { return G1(x) * G1(x) + 1.; }      // 2 次 call
double P1(double) __attribute__((pure));
double g_state = 0;
__attribute__((noipa)) double P1(double x) { return x + g_state; }
__attribute__((noinline)) double P2(double x) { double a = P1(x); g_state = 1; return a * P1(x); }  // 2 次：P1 可能读 g_state
__attribute__((noinline)) double C2(double x) { double a = Func1(x); g_state = 1; return a * Func1(x); }  // 1 次：const 不读内存

// 1.〔补〕64 字节的返回值：调用方把自己那块内存的地址放在 %rdi 传进来，函数直接写进去
struct Snapshot { double bid[4]; double ask[4]; };
__attribute__((noinline)) Snapshot make_snap(double px) {
    Snapshot s;
    for (int k = 0; k < 4; k++) { s.bid[k] = px - k; s.ask[k] = px + k; }
    return s;
}

int main() {
    run_source();
    int bad = 0;
    std::cout << "\n===== check =====\n";
    if (Func2(4.0) != 5.0 || G2(4.0) != 5.0) ++bad;
    if (P2(1.0) != 2.0) ++bad;
    if (C2(4.0) != 4.0) ++bad;          // a = 1 + 0，之后 g_state = 1，第二次 P1 = 2
    Snapshot s = make_snap(100.0);
    if (s.bid[3] != 97.0 || s.ask[3] != 103.0) ++bad;
    std::cout << (bad ? "MISMATCH" : "all ok") << "\n";
    return bad != 0;
}
