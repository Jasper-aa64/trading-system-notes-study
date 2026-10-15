// #23 双数组加原子索引：验证程序
// 运行：g++ -std=c++20 -O2 23-double-buffer-check.cpp -o dbuf && ./dbuf
// 本机：GCC 16.2（winlibs mingw64，posix 线程），Ryzen 5 5600GT。
// 写线程不停 update，每次把整个数组填成同一个值 k；读线程每次 read() 拷一份，检查是否全等。
// 源 :10 的 DoubleBuffer 原样（只去掉了打印）：写的一方连续更新两次，就会写到读的一方还在拷的那个数组。
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>

template<typename T>
class DoubleBuffer {
public:
    DoubleBuffer(size_t size) : currentIndex(0) {
        buffers[0] = std::vector<T>(size);
        buffers[1] = std::vector<T>(size);
    }
    void update(const std::vector<T>& newData) {
        size_t nextIndex = 1 - currentIndex.load(std::memory_order_relaxed);
        std::copy(newData.begin(), newData.end(), buffers[nextIndex].begin());
        std::atomic_thread_fence(std::memory_order_release);
        currentIndex.store(nextIndex, std::memory_order_relaxed);
    }
    std::vector<T> read() const {
        size_t index = currentIndex.load(std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_acquire);
        return buffers[index];
    }
private:
    std::vector<T> buffers[2];
    std::atomic<size_t> currentIndex;
};

int main() {
    const size_t N = 4096;                       // 16 KiB 的 int
    DoubleBuffer<int> buf(N);
    std::atomic<bool> stop{false};
    std::thread writer([&] {
        std::vector<int> data(N);
        for (int k = 1; !stop.load(std::memory_order_relaxed); ++k) {
            std::fill(data.begin(), data.end(), k);
            buf.update(data);
        }
    });
    long reads = 0, torn = 0;
    auto t0 = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - t0 < std::chrono::seconds(2)) {
        auto d = buf.read();
        ++reads;
        if (!std::all_of(d.begin(), d.end(), [&](int x) { return x == d[0]; })) ++torn;
    }
    stop = true;
    writer.join();
    std::printf("reads %ld, torn (mixed old/new values) %ld\n", reads, torn);
}
