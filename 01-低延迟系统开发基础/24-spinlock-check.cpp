// #24 自定义自旋锁：验证程序（轮 1）
// 运行：g++ -std=c++20 -O2 -Wall -Wextra -pthread 24-spinlock-check.cpp -o spin && ./spin
// 本机：GCC 16.2（winlibs mingw64，posix 线程），x86-64，零警告。
// 前面五段是博客 #16 里的代码（逐字一致）；最后一段是测试：8 个线程各做 20 万次 lock / ++counter / unlock，计数应为 1600000。
// 只检查正确性，不计时。TtasSpinLock 直接调了 _mm_pause，只能在 x86 上编；cpu_relax 的 ARM 分支没在 ARM 上编译过。
#include <atomic>

class SleepingMutex {  // 0 = 没锁，1 = 锁着、没人等，2 = 锁着、可能有人在睡
public:
    void lock() {
        int c = 0;
        if (state_.compare_exchange_strong(c, 1, std::memory_order_acquire)) return;  // 快路径：一次 CAS
        if (c != 2) c = state_.exchange(2, std::memory_order_acquire);
        while (c != 0) {
            state_.wait(2, std::memory_order_relaxed);  // 还是 2 就进内核睡（Linux 上是 futex）
            c = state_.exchange(2, std::memory_order_acquire);
        }
    }
    void unlock() {
        if (state_.exchange(0, std::memory_order_release) == 2)
            state_.notify_one();  // 可能有人在睡，才进内核叫醒一个
    }

private:
    std::atomic<int> state_{0};
};

#include <atomic>

class CasSpinLock {
public:
    void lock() {
        int expected = 0;
        while (!locked_.compare_exchange_weak(expected, 1, std::memory_order_acquire,
                                              std::memory_order_relaxed)) {
            expected = 0;  // 失败时 expected 被改成了当前值 1，改回 0 再试
        }
    }
    void unlock() { locked_.store(0, std::memory_order_release); }

private:
    std::atomic<int> locked_{0};
};

#include <atomic>
#include <immintrin.h>  // _mm_pause，见 1.6

class TtasSpinLock {
public:
    void lock() {
        for (;;) {
            if (!locked_.exchange(true, std::memory_order_acquire)) return;  // 先抢一次：没人争时只有这一次读改写
            while (locked_.load(std::memory_order_relaxed)) _mm_pause();    // 抢不到就只读，看着空了再回去抢
        }
    }
    void unlock() { locked_.store(false, std::memory_order_release); }

private:
    alignas(64) std::atomic<bool> locked_{false};  // 独占一条缓存行
};

#if defined(__x86_64__) || defined(__i386__)
#include <immintrin.h>
inline void cpu_relax() { _mm_pause(); }
#elif defined(__aarch64__)
inline void cpu_relax() { asm volatile("yield" ::: "memory"); }
#else
inline void cpu_relax() {}
#endif

#include <algorithm>
#include <atomic>

class BackoffSpinLock {
public:
    void lock() {
        for (int backoff = 1;; backoff = std::min(backoff * 2, kMaxBackoff)) {
            if (!locked_.exchange(true, std::memory_order_acquire)) return;
            for (int i = 0; i < backoff; ++i) cpu_relax();  // 抢输了：先歇一会儿，再去看（cpu_relax 见 1.6）
            while (locked_.load(std::memory_order_relaxed)) cpu_relax();
        }
    }
    void unlock() { locked_.store(false, std::memory_order_release); }

private:
    static constexpr int kMaxBackoff = 64;
    alignas(64) std::atomic<bool> locked_{false};
};

#include <cstdio>
#include <thread>
#include <vector>

template <class L>
bool run(const char* name) {
    static L lock;
    static long long counter = 0;
    constexpr int kThreads = 8, kIters = 200000;
    std::vector<std::thread> ts;
    for (int t = 0; t < kThreads; ++t)
        ts.emplace_back([] {
            for (int i = 0; i < kIters; ++i) {
                lock.lock();
                ++counter;
                lock.unlock();
            }
        });
    for (auto& th : ts) th.join();
    bool ok = counter == 1LL * kThreads * kIters;
    std::printf("%-16s counter = %lld  %s\n", name, counter, ok ? "ok" : "FAIL");
    return ok;
}

int main() {
    static_assert(alignof(TtasSpinLock) == 64 && sizeof(TtasSpinLock) == 64);
    bool ok = run<SleepingMutex>("SleepingMutex");
    ok &= run<CasSpinLock>("CasSpinLock");
    ok &= run<TtasSpinLock>("TtasSpinLock");
    ok &= run<BackoffSpinLock>("BackoffSpinLock");
    return ok ? 0 : 1;
}
