// 10-spmc-behavior-check.cpp — checks the claims in 10-spmc共享内存无锁队列应用.md (round 1):
//   sizes of the structs / Block / queue, empty read, first write, history not replayed,
//   lapped reader (CNT=8, 20 writes), readLast, and the signed sequence compare across the 32-bit wrap.
// SPMCQueue and Lev2* are copied verbatim from the source note (lines 7-59 and 83-107).
// Build (Windows/MinGW): g++ -std=c++17 -O2 -pthread 10-spmc-behavior-check.cpp
// On Linux swap <malloc.h>/_aligned_malloc for <cstdlib>/aligned_alloc. `#define private public` is test-only.
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <malloc.h>
#include <thread>
#include <vector>
#define private public          // test-only: peek at Block to print sizes and to re-check idx
#pragma once
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
// Level2 逐笔委托行情
struct Lev2Order {
    char SecurityID[31]; // 股票代码
    int TradeTime;       // 交易所时间
    char TickType;       // 报单类型
    char Side;           // 报单方向
    double Price;        // 价格
    long long Volume;    // 数量(股)
    int MainSeq;         // 主通道号
    long long SubSeq;    // 次通道号
    long long No;        // 报单号
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

template <class Q> Q* make_zeroed() {           // mimic a fresh zero-filled shared-memory segment: the object is never constructed
  void* p = _aligned_malloc(sizeof(Q), alignof(Q));
  std::memset(p, 0, sizeof(Q));
  return static_cast<Q*>(p);
}

struct P { uint64_t seq; };
struct Big { uint64_t v[16]; };

int main() {
  // ---------------- sizes ----------------
  using TradeQueue = SPMCQueue<Lev2Trans, 524288>;
  using OrderQueue = SPMCQueue<Lev2Order, 524288>;
  std::printf("sizeof(Lev2Trans)=%zu  sizeof(Lev2Order)=%zu\n", sizeof(Lev2Trans), sizeof(Lev2Order));
  std::printf("Block<Lev2Trans>=%zu (align %zu)  Block<Lev2Order>=%zu\n", sizeof(TradeQueue::Block), alignof(TradeQueue::Block), sizeof(OrderQueue::Block));
  std::printf("sizeof(TradeQueue)=%zu B = %.2f MiB | sizeof(OrderQueue)=%zu B = %.2f MiB | alignof(queue)=%zu\n",
              sizeof(TradeQueue), sizeof(TradeQueue) / 1048576.0, sizeof(OrderQueue), sizeof(OrderQueue) / 1048576.0, alignof(TradeQueue));
  std::printf("offsetof write_idx region: queue size minus blocks = %zu B\n", sizeof(TradeQueue) - 524288 * sizeof(TradeQueue::Block));

  // ---------------- empty / first write / history ----------------
  {
    auto* q = make_zeroed<SPMCQueue<P, 8>>();
    auto r = q->getReader();
    std::printf("empty queue: read() is %s\n", r.read() == nullptr ? "nullptr" : "NOT null");
    q->write([&](P& p) { p.seq = 1; });
    P* p = r.read();
    std::printf("after 1 write: read() -> seq %llu; next read() is %s\n", p ? (unsigned long long)p->seq : 0ull, r.read() == nullptr ? "nullptr" : "NOT null");
    for (uint64_t i = 2; i <= 5; ++i) q->write([&](P& x) { x.seq = i; });
    auto late = q->getReader();
    std::printf("reader created after 5 writes: first read() is %s (history is not replayed)\n", late.read() == nullptr ? "nullptr" : "NOT null");
    q->write([&](P& x) { x.seq = 6; });
    P* l = late.read();
    std::printf("  ...after the next write it reads seq %llu\n", l ? (unsigned long long)l->seq : 0ull);
  }

  // ---------------- lapped reader (CNT = 8, 20 writes before the reader looks) ----------------
  {
    auto* q = make_zeroed<SPMCQueue<P, 8>>();
    auto r = q->getReader();
    for (uint64_t i = 1; i <= 20; ++i) q->write([&](P& x) { x.seq = i; });
    std::printf("lapped reader (CNT=8, 20 writes): got seqs:");
    while (P* p = r.read()) std::printf(" %llu", (unsigned long long)p->seq);
    std::printf("   -> messages 1..16 were never delivered, and nothing told the reader\n");
  }

  // ---------------- readLast ----------------
  {
    auto* q = make_zeroed<SPMCQueue<P, 8>>();
    auto r = q->getReader();
    for (uint64_t i = 1; i <= 5; ++i) q->write([&](P& x) { x.seq = i; });
    P* p = r.readLast();
    std::printf("readLast after 5 writes -> seq %llu; then read() is %s\n", p ? (unsigned long long)p->seq : 0ull, r.read() == nullptr ? "nullptr" : "NOT null");
  }

  // ---------------- signed sequence compare across 32-bit wrap ----------------
  {
    uint32_t next = 0xFFFFFFFEu, fresh = 0xFFFFFFFEu, after_wrap = 1u;
    std::printf("wrap check: (int)(fresh-next)=%d  (int)(after_wrap-next)=%d  (naive after_wrap < next is %s)\n",
                (int)(fresh - next), (int)(after_wrap - next), after_wrap < next ? "true = WRONG 'not yet written'" : "false");
  }

  return 0;
}
