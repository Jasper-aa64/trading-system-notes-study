// 10-spmc-producer-vs-readers-check.cpp — does the SPMC producer get slower when readers are added? (附一 Q1)
//   Full speed: producer ns/write with 0..5 busy-polling readers (ring of 4096 blocks = 512 KiB, and the source's 524288 blocks = 64 MiB).
//   Paced (one write per ~2 us): time of the write() call itself, median / p99 / p99.9.
// SPMCQueue and Lev2Trans are copied verbatim from the source note (lines 7-59 and 96-107).
// Windows-only (SetThreadAffinityMask): producer on logical CPU 0, readers on 2, 4, 6, ... On a 6-core/12-thread Ryzen the two SMT threads
// of a core are numbered 0/1, 2/3, ..., so these are distinct physical cores; adjust the numbering for another machine.
// Build: g++ -std=c++17 -O2 -pthread 10-spmc-producer-vs-readers-check.cpp        (takes about 100 s to run)
// Numbers measured on 2026-09-24 (AMD Ryzen 5 5600GT, Windows): see 附一 in 10-spmc共享内存无锁队列应用.md.
#include <windows.h>
#include <malloc.h>
#include <x86intrin.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

using Clock = std::chrono::steady_clock;

static void pin(int cpu) { SetThreadAffinityMask(GetCurrentThread(), (DWORD_PTR)1 << cpu); }

static double tsc_per_ns() {
  auto t0 = Clock::now();
  uint64_t c0 = __rdtsc();
  while (Clock::now() - t0 < std::chrono::milliseconds(300)) {}
  uint64_t c1 = __rdtsc();
  auto t1 = Clock::now();
  return double(c1 - c0) / std::chrono::duration<double, std::nano>(t1 - t0).count();
}

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

// Level2 逐笔成交行情
struct Lev2Trans {
    char SecurityID[31]; // 股票代码
    int TradeTime;       // 交易所时间
    char TickType;       // 报单类型
    double Price;        // 价格
    long long Volume;    // 数量(股)
    int MainSeq;         // 主通道号
    long long SubSeq;    // 次通道号
    long long BuyNo;     // 买方报单号
    long long SellNo;    // 卖方报单号
};

static std::atomic<uint64_t> g_sink{0};

template <class Q> Q* make_zeroed() {
  void* p = _aligned_malloc(sizeof(Q), 128);
  std::memset(p, 0, sizeof(Q));            // also touches every page, so no page faults are measured later
  return static_cast<Q*>(p);
}

template <class Q>
static void reader_loop(Q* q, int cpu, bool touch, std::atomic<bool>& stop, std::atomic<int>& ready) {
  pin(cpu);
  auto r = q->getReader();
  ready.fetch_add(1);
  uint64_t sink = 0;
  while (!stop.load(std::memory_order_relaxed)) {
    Lev2Trans* p = r.read();
    if (p && touch) sink += p->Volume + p->SellNo;   // Volume sits in the block's first cache line, SellNo in its second
  }
  g_sink.fetch_add(sink);
}

template <class Q>
static void fill(Q* q, uint64_t n) {
  q->write([&](Lev2Trans& m) {
    std::memcpy(m.SecurityID, "600000", 7);
    m.TradeTime = (int)n; m.TickType = 'T'; m.Price = (double)n; m.Volume = (long long)n;
    m.MainSeq = 1; m.SubSeq = (long long)n; m.BuyNo = (long long)n; m.SellNo = (long long)n;
  });
}

// readers sit on logical CPUs 2, 4, 6, ... (Windows numbers the two SMT threads of a core 0/1, 2/3, ...), producer on CPU 0
template <uint32_t CNT>
static double throughput_ns_per_write(int readers, bool touch, double seconds) {
  using Q = SPMCQueue<Lev2Trans, CNT>;
  Q* q = make_zeroed<Q>();
  std::atomic<bool> stop{false};
  std::atomic<int> ready{0};
  std::vector<std::thread> rs;
  for (int i = 0; i < readers; ++i) rs.emplace_back(reader_loop<Q>, q, 2 + 2 * i, touch, std::ref(stop), std::ref(ready));
  while (ready.load() < readers) {}
  pin(0);
  uint64_t n = 0;
  auto t0 = Clock::now();
  auto deadline = t0 + std::chrono::duration<double>(seconds);
  do {
    for (int k = 0; k < 4096; ++k) fill(q, ++n);
  } while (Clock::now() < deadline);
  auto t1 = Clock::now();
  stop.store(true);
  for (auto& t : rs) t.join();
  _aligned_free(q);
  return std::chrono::duration<double, std::nano>(t1 - t0).count() / double(n);
}

// one write every ~2 us: how long does the write() call itself take?
template <uint32_t CNT>
static void paced_write_ns(int readers, double tsc_ghz, double out[3]) {
  using Q = SPMCQueue<Lev2Trans, CNT>;
  Q* q = make_zeroed<Q>();
  std::atomic<bool> stop{false};
  std::atomic<int> ready{0};
  std::vector<std::thread> rs;
  for (int i = 0; i < readers; ++i) rs.emplace_back(reader_loop<Q>, q, 2 + 2 * i, true, std::ref(stop), std::ref(ready));
  while (ready.load() < readers) {}
  pin(0);
  const int N = 300000;
  std::vector<uint32_t> cyc;
  cyc.reserve(N);
  uint64_t period = (uint64_t)(2000.0 * tsc_ghz);
  uint64_t next = __rdtsc();
  for (int i = 1; i <= N; ++i) {
    next += period;
    while (__rdtsc() < next) {}
    uint64_t a = __rdtsc();
    fill(q, (uint64_t)i);
    uint64_t b = __rdtsc();
    cyc.push_back((uint32_t)(b - a));
  }
  stop.store(true);
  for (auto& t : rs) t.join();
  _aligned_free(q);
  std::sort(cyc.begin(), cyc.end());
  out[0] = cyc[cyc.size() / 2] / tsc_ghz;
  out[1] = cyc[cyc.size() * 99 / 100] / tsc_ghz;
  out[2] = cyc[cyc.size() * 999 / 1000] / tsc_ghz;
}

template <uint32_t CNT>
static void section(const char* label) {
  std::printf("%s\n", label);
  std::printf("  readers | producer ns/write at full speed (3 runs: min / median / max)\n");
  std::printf("          |  readers only poll (touch stamp)     | readers also read the payload\n");
  for (int n = 0; n <= 5; ++n) {
    double a[3], b[3];
    for (int r = 0; r < 3; ++r) {
      a[r] = throughput_ns_per_write<CNT>(n, false, 1.0);
      b[r] = (n == 0) ? a[r] : throughput_ns_per_write<CNT>(n, true, 1.0);
    }
    std::sort(a, a + 3);
    std::sort(b, b + 3);
    if (n == 0) std::printf("  %7d | %7.1f / %7.1f / %7.1f              | (no readers)\n", n, a[0], a[1], a[2]);
    else std::printf("  %7d | %7.1f / %7.1f / %7.1f              | %7.1f / %7.1f / %7.1f\n", n, a[0], a[1], a[2], b[0], b[1], b[2]);
    std::fflush(stdout);
  }
}

int main() {
  double tsc_ghz = tsc_per_ns();
  std::printf("TSC = %.3f GHz\n", tsc_ghz);
  section<4096>("ring of 4096 blocks x 128 B = 512 KiB (fits in a core's L2)");
  section<524288>("ring of 524288 blocks x 128 B = 64 MiB (the source's real CAPACITY; larger than the 16 MiB L3)");
  std::printf("\npaced producer (one write every ~2 us, ring of 4096, readers read the payload): time of the write() call itself\n");
  std::printf("  readers | median ns | p99 ns | p99.9 ns\n");
  for (int n = 0; n <= 5; ++n) {
    double o[3];
    paced_write_ns<4096>(n, tsc_ghz, o);
    std::printf("  %7d | %9.1f | %6.1f | %8.1f\n", n, o[0], o[1], o[2]);
    std::fflush(stdout);
  }
  std::printf("(sink %llu)\n", (unsigned long long)g_sink.load());
  return 0;
}
