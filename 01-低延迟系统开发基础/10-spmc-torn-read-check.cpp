// 10-spmc-torn-read-check.cpp — torn reads in the source SPMCQueue (round 1, section "what it does not guarantee").
// Deliberately harsh: CNT=4 and a producer writing as fast as it can, 3 s per case. Three cases:
//   1) source writer, plain read   2) source writer + re-check idx after the copy   3) writer invalidates idx first + re-check.
// SPMCQueue is copied verbatim from the source note (lines 7-59); SPMCQueueFixed differs only in write().
// Build (Windows/MinGW): g++ -std=c++17 -O2 -pthread 10-spmc-torn-read-check.cpp
// x86-only result: in strict C++ the data words would also have to be (relaxed) atomics.
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <malloc.h>
#include <thread>
#define private public
#include <atomic>

template<class T, uint32_t CNT>
class SPMCQueue
{
    public:
    static_assert(CNT && !(CNT & (CNT - 1)), "CNT must be a power of 2");
    struct Reader
    {
        operator bool() const { return q; }
        T* read() {
            auto& blk = q->blks[next_idx % CNT];
            uint32_t new_idx = ((std::atomic<uint32_t>*)&blk.idx)->load(std::memory_order_acquire);
            if (int(new_idx - next_idx) < 0) return nullptr;
            next_idx = new_idx + 1;
            return &blk.data;
        }

        T* readLast() {
            T* ret = nullptr;
            while (T* cur = read()) ret = cur;
            return ret;
        }

        SPMCQueue<T, CNT>* q = nullptr;
        uint32_t next_idx;
    };

    Reader getReader() {
        Reader reader;
        reader.q = this;
        reader.next_idx = write_idx + 1;
        return reader;
    }

    template<typename Writer>
        void write(Writer writer) {
        auto& blk = blks[++write_idx % CNT];
        writer(blk.data);
        ((std::atomic<uint32_t>*)&blk.idx)->store(write_idx, std::memory_order_release);
    }

    private:
    friend class Reader;
    struct alignas(64) Block
    {
        uint32_t idx = 0;
        T data;
    } blks[CNT];

    alignas(128) uint32_t write_idx = 0;
};
#include <atomic>

template<class T, uint32_t CNT>
class SPMCQueueFixed
{
    public:
    static_assert(CNT && !(CNT & (CNT - 1)), "CNT must be a power of 2");
    struct Reader
    {
        operator bool() const { return q; }
        T* read() {
            auto& blk = q->blks[next_idx % CNT];
            uint32_t new_idx = ((std::atomic<uint32_t>*)&blk.idx)->load(std::memory_order_acquire);
            if (int(new_idx - next_idx) < 0) return nullptr;
            next_idx = new_idx + 1;
            return &blk.data;
        }

        T* readLast() {
            T* ret = nullptr;
            while (T* cur = read()) ret = cur;
            return ret;
        }

        SPMCQueueFixed<T, CNT>* q = nullptr;
        uint32_t next_idx;
    };

    Reader getReader() {
        Reader reader;
        reader.q = this;
        reader.next_idx = write_idx + 1;
        return reader;
    }

    template<typename Writer>
        void write(Writer writer) {
        auto& blk = blks[++write_idx % CNT];
        ((std::atomic<uint32_t>*)&blk.idx)->store(0, std::memory_order_relaxed);   // invalidate first
        std::atomic_thread_fence(std::memory_order_release);
        writer(blk.data);
        ((std::atomic<uint32_t>*)&blk.idx)->store(write_idx, std::memory_order_release);
    }

    private:
    friend class Reader;
    struct alignas(64) Block
    {
        uint32_t idx = 0;
        T data;
    } blks[CNT];

    alignas(128) uint32_t write_idx = 0;
};

template <class Q> Q* make_zeroed() {
  void* p = _aligned_malloc(sizeof(Q), alignof(Q));
  std::memset(p, 0, sizeof(Q));
  return static_cast<Q*>(p);
}

struct Big { uint64_t v[16]; };

template <template <class, uint32_t> class QT>
void torn_test(const char* name, bool validated) {
  using Q = QT<Big, 4>;
  auto* q = make_zeroed<Q>();
  auto r = q->getReader();
  std::atomic<bool> stop{false};
  std::thread prod([&] {
    uint64_t i = 0;
    while (!stop.load(std::memory_order_relaxed)) {
      ++i;
      q->write([&](Big& b) { for (int k = 0; k < 16; ++k) ((volatile uint64_t*)b.v)[k] = i; });
    }
  });
  uint64_t accepted = 0, torn = 0, discarded = 0;
  auto end = std::chrono::steady_clock::now() + std::chrono::seconds(3);
  while (std::chrono::steady_clock::now() < end) {
    Big* p = r.read();
    if (!p) continue;
    uint32_t my_idx = r.next_idx - 1;
    uint64_t local[16];
    for (int k = 0; k < 16; ++k) local[k] = ((volatile uint64_t*)p->v)[k];
    if (validated) {
      std::atomic_thread_fence(std::memory_order_acquire);
      uint32_t after = ((std::atomic<uint32_t>*)&q->blks[my_idx % 4].idx)->load(std::memory_order_relaxed);
      if (after != my_idx) { ++discarded; continue; }
    }
    bool bad = false;
    for (int k = 1; k < 16; ++k) if (local[k] != local[0]) bad = true;
    ++accepted;
    if (bad) ++torn;
  }
  stop.store(true);
  prod.join();
  std::printf("%-58s accepted %10llu | torn among accepted %8llu | discarded by re-check %10llu\n", name,
              (unsigned long long)accepted, (unsigned long long)torn, (unsigned long long)discarded);
}

int main() {
  torn_test<SPMCQueue>("source writer, plain read", false);
  torn_test<SPMCQueue>("source writer, re-check idx after copy", true);
  torn_test<SPMCQueueFixed>("writer invalidates idx first, re-check idx after copy", true);
  return 0;
}
