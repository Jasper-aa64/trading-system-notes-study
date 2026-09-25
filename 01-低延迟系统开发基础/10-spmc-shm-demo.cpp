// 10-spmc-shm-demo.cpp — the SPMC queue across two processes, and the first-lap page faults (轮 2).
//   reader / producer : start `10-spmc-shm-demo reader` first (in the background), then `10-spmc-shm-demo producer`.
//                       A Windows named file mapping (CreateFileMapping + MapViewOfFile) stands in for shm_open + mmap;
//                       the POSIX calls themselves were not run on this machine.
//   firstlap          : a 64 MiB queue, two laps of writes, with and without writing one byte to every 4 KiB page beforehand.
// SPMCQueue and Lev2Trans are copied verbatim from the source note (lines 7-59 and 96-107).
// Build: g++ -std=c++17 -O2 -pthread 10-spmc-shm-demo.cpp
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

struct Msg {                 // deliberately carries two pointers, to see what the other process makes of them
  char tag[8];
  int seq;
  const char* lit;           // address of a string literal in the WRITER's process
  const void* self;          // address of this very block in the WRITER's view of the mapping
};
using MsgQueue = SPMCQueue<Msg, 1024>;

static void* map_named(const char* name, size_t bytes, bool* created) {
  HANDLE h = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, (DWORD)((uint64_t)bytes >> 32), (DWORD)bytes, name);
  if (!h) { std::printf("CreateFileMapping failed: %lu\n", GetLastError()); std::exit(1); }
  if (created) *created = (GetLastError() != ERROR_ALREADY_EXISTS);
  void* p = MapViewOfFile(h, FILE_MAP_ALL_ACCESS, 0, 0, bytes);   // handle stays open on purpose: the mapping lives as long as one handle does
  if (!p) { std::printf("MapViewOfFile failed: %lu\n", GetLastError()); std::exit(1); }
  return p;
}

static int producer() {
  bool created;
  auto* q = (MsgQueue*)map_named("spmc_demo_msgqueue", sizeof(MsgQueue), &created);
  std::printf("[producer pid %lu] view at %p, sizeof(queue) = %zu, created the object here: %s\n", GetCurrentProcessId(), (void*)q, sizeof(MsgQueue), created ? "yes" : "no (the reader did)");
  Sleep(1500);                                   // let the reader attach: getReader() starts at write_idx + 1
  const char* literal = "AAPL";
  for (int seq = 1; seq <= 3; ++seq) {
    q->write([&](Msg& m) {
      std::strcpy(m.tag, "AAPL");
      m.seq = seq;
      m.lit = literal;
      m.self = &m;
    });
    Sleep(50);
  }
  std::printf("[producer] wrote 3 messages; my literal \"AAPL\" lives at %p\n", (const void*)literal);
  Sleep(1500);
  return 0;
}

static int reader() {
  bool created;
  auto* q = (MsgQueue*)map_named("spmc_demo_msgqueue", sizeof(MsgQueue), &created);
  std::printf("[reader   pid %lu] view at %p, created the object here: %s\n", GetCurrentProcessId(), (void*)q, created ? "yes" : "no");
  auto r = q->getReader();
  const char* my_literal = "AAPL";
  int got = 0;
  auto end = Clock::now() + std::chrono::seconds(5);
  while (Clock::now() < end && got < 3) {
    Msg* m = r.read();
    if (!m) continue;
    ++got;
    std::printf("[reader] got seq=%d tag=%s | m->self=%p vs my address of this block=%p (%s) | m->lit=%p vs my own \"AAPL\"=%p (%s)\n",
                m->seq, m->tag, m->self, (const void*)m, m->self == (const void*)m ? "same" : "DIFFERENT",
                (const void*)m->lit, (const void*)my_literal, m->lit == my_literal ? "same" : "DIFFERENT");
  }
  std::printf("[reader] received %d of 3 messages across the process boundary\n", got);
  return 0;
}

// first lap over a fresh 64 MiB queue: page faults on first touch, and what pre-touching does about them
static int firstlap(bool pretouch, int id) {
  using Q = SPMCQueue<Lev2Trans, 524288>;
  double ghz = tsc_per_ns();
  char name[64];
  std::snprintf(name, sizeof name, "spmc_demo_lap_%lu_%d", GetCurrentProcessId(), id);
  auto* q = (Q*)map_named(name, sizeof(Q), nullptr);
  pin(0);
  if (pretouch) for (size_t off = 0; off < sizeof(Q); off += 4096) ((volatile char*)q)[off] = 0;   // WRITE-touch every 4 KiB page
  std::printf("%s: queue %zu B at %p\n", pretouch ? "pre-touched (one write per 4 KiB page before the first message)" : "fresh mapping, no pre-touch", sizeof(Q), (void*)q);
  std::vector<uint32_t> cyc(524288);
  for (int lap = 1; lap <= 2; ++lap) {
    uint64_t total = 0;
    for (uint32_t i = 0; i < 524288; ++i) {
      uint64_t a = __rdtsc();
      q->write([&](Lev2Trans& m) {
        std::memcpy(m.SecurityID, "600000", 7);
        m.TradeTime = (int)i; m.TickType = 'T'; m.Price = (double)i; m.Volume = (long long)i;
        m.MainSeq = 1; m.SubSeq = (long long)i; m.BuyNo = (long long)i; m.SellNo = (long long)i;
      });
      uint64_t b = __rdtsc();
      cyc[i] = (uint32_t)(b - a);
      total += b - a;
    }
    std::vector<uint32_t> s(cyc);
    std::sort(s.begin(), s.end());
    std::printf("  lap %d: mean %7.1f ns/write | median %6.1f | p99 %7.1f | p99.9 %8.1f | max %9.1f ns\n", lap,
                double(total) / 524288.0 / ghz, s[s.size() / 2] / ghz, s[s.size() * 99 / 100] / ghz, s[s.size() * 999 / 1000] / ghz, s.back() / ghz);
  }
  return 0;
}

int main(int argc, char** argv) {
  const char* mode = argc > 1 ? argv[1] : "";
  if (!std::strcmp(mode, "producer")) return producer();
  if (!std::strcmp(mode, "reader")) return reader();
  if (!std::strcmp(mode, "firstlap")) { firstlap(false, 1); firstlap(true, 2); return 0; }
  std::printf("usage: spmc-shm-demo producer | reader | firstlap\n");
  return 2;
}
