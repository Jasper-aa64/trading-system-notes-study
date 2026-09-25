// 10-spmc-resume-check.cpp — restarting readers (轮 2, pitfall 3) and whether std::string is self-contained.
//   1) getReader() after a restart vs resuming from a saved next_idx   2) a saved cursor more than one lap behind
//   3) an old reader against a recreated (all-zero) queue               4) where std::string's data pointer points
// SPMCQueue is copied verbatim from the source note (lines 7-59). Portable except for _aligned_malloc (use aligned_alloc on Linux).
// Build: g++ -std=c++17 -O2 10-spmc-resume-check.cpp
#include <malloc.h>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

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

struct Msg { uint64_t seq; };
using Q = SPMCQueue<Msg, 8>;                      // CNT = 8 so that "one lap" is easy to reach

static Q* fresh() {                               // what a newly created shared-memory object looks like: all zero
  void* p = _aligned_malloc(sizeof(Q), 128);
  std::memset(p, 0, sizeof(Q));
  return static_cast<Q*>(p);
}
static void write_n(Q* q, uint64_t from, uint64_t to) {
  for (uint64_t s = from; s <= to; ++s) q->write([&](Msg& m) { m.seq = s; });
}
static Q::Reader reader_at(Q* q, uint32_t next_idx) {
  Q::Reader r;
  r.q = q;
  r.next_idx = next_idx;
  return r;
}
static void show(const char* what, Q::Reader& r) {
  std::printf("  %-58s ->", what);
  int n = 0;
  while (Msg* m = r.read()) { std::printf(" %llu", (unsigned long long)m->seq); ++n; }
  if (!n) std::printf(" (nothing)");
  std::printf("\n");
}

int main() {
  std::printf("1) a reader restarts while the producer keeps writing (CNT = 8)\n");
  Q* q = fresh();
  auto a = q->getReader();
  write_n(q, 1, 5);
  show("reader A, before it 'crashes'", a);
  uint32_t saved = a.next_idx;                                   // what a recorder could have saved to disk
  write_n(q, 6, 9);                                              // producer keeps going while A is down (4 messages, fewer than one lap)
  auto b = q->getReader();
  write_n(q, 10, 10);
  show("restart with getReader()", b);
  auto c = reader_at(q, saved);
  show("restart with the saved next_idx", c);

  std::printf("\n2) the saved cursor is more than one lap behind\n");
  write_n(q, 11, 30);
  auto d = reader_at(q, saved);
  uint32_t before = d.next_idx;
  Msg* first = d.read();
  std::printf("  saved next_idx = %u, first message returned = %llu, so read() skipped %llu (a multiple of CNT = 8: %s)\n", before,
              (unsigned long long)first->seq, (unsigned long long)(first->seq - before), ((first->seq - before) % 8 == 0) ? "yes" : "no");
  show("...and what follows", d);
  std::printf("  (the ring still physically holds messages 23..30; the reader landed on 30 and does not go back for 23..29)\n");

  std::printf("\n3) the queue is recreated (all zero) but an old reader keeps its cursor\n");
  Q* q2 = fresh();
  auto old = reader_at(q2, 1000);
  write_n(q2, 1, 999);
  show("old reader (next_idx = 1000), new producer has written 1..999", old);
  write_n(q2, 1000, 1002);
  show("...after the new producer reaches 1000", old);

  std::printf("\n4) is a std::string self-contained? where does its data pointer point?\n");
  std::string s("600000");
  const char* obj_begin = reinterpret_cast<const char*>(&s);
  const char* obj_end = obj_begin + sizeof(s);
  std::printf("  short string (SSO): &s = %p, s.data() = %p, data pointer lies inside the string object itself: %s\n", (const void*)&s, (const void*)s.data(),
              (s.data() >= obj_begin && s.data() < obj_end) ? "yes" : "no");
  std::string big(200, 'x');
  const char* bb = reinterpret_cast<const char*>(&big);
  std::printf("  long string:        &s = %p, s.data() = %p, data pointer lies inside the string object itself: %s (it points into the heap)\n", (const void*)&big, (const void*)big.data(),
              (big.data() >= bb && big.data() < bb + sizeof(big)) ? "yes" : "no");
  std::printf("  sizeof(std::string) = %zu\n", sizeof(std::string));
  return 0;
}
