// #12 轮 3 第 3 节：PGO 的坑。训练时热路径（send_order）一次没跑，GCC 会把它当冷代码。
// 只看段名（.text.hot / .text.unlikely），不计时。profile 文件名跟 -o 走，两步要用同一个 -o。
//   g++ -O2 -fprofile-generate 12-pgo-check.cpp -o pgo && ./pgo 1e9            # 阈值很大：一次不下单
//   g++ -O2 -fprofile-use -save-temps 12-pgo-check.cpp -o pgo                   # 汇编留在 pgo-12-pgo-check.s
//   grep -B3 '^_Z10send_order.*:$' pgo-12-pgo-check.s                          # .section .text.unlikely
//   （训练时跑 ./pgo -1e9：每条都下单，模拟 dummy 执行 -> send_order 进 .text.hot）
//   （第二步加 -fprofile-partial-training：没跑到的代码照常优化，不挪进 .text.unlikely）
// GCC 13.3 实测如上。
#include <cstdio>
#include <cstdlib>
struct Quote { double bid, ask; };
static double g_fair;
static long g_orders;
__attribute__((noinline)) void send_order(const Quote& q, double edge) {
    // 真实系统里这里组包、写网卡；这里只算点东西
    double px = q.ask + edge * 0.5;
    for (int i = 0; i < 4; ++i) px = px * 0.999 + g_fair * 0.001;
    g_orders += static_cast<long>(px) & 1;
}
__attribute__((noinline)) void on_quote(const Quote& q, double threshold) {
    g_fair = g_fair * 0.99 + (q.bid + q.ask) * 0.005;
    double edge = g_fair - q.ask;
    if (edge > threshold) send_order(q, edge);   // 真实行情里很少成立
}
int main(int argc, char** argv) {
    double threshold = argc > 1 ? std::atof(argv[1]) : 1e9;
    std::srand(1);
    for (int i = 0; i < 2000000; ++i) {
        double m = 100 + (std::rand() % 100) * 0.01;
        on_quote(Quote{m - 0.01, m + 0.01}, threshold);
    }
    std::printf("orders=%ld\n", g_orders);
}
