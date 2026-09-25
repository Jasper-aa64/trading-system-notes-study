# wait-free 编程 —— 从 lock-free 的 CAS 循环到"有限步完成"

> 源笔记:[`30-wait-free编程.md`](../../trading-system-notes/chinese/01-low-latency/30-wait-free编程.md)
>
> **状态:预读稿(2026-09-20)——还没在对话里讲,没有检验题。** 按编号讲到 #30 时仍走完整一轮(讲透 + 检验),届时把检验附在文末。这份稿子由三部分拼成:源笔记、一篇知乎文章([《超越 lock-free 的是 wait-free》](https://zhuanlan.zhihu.com/p/2003478515148928531),2026-02-07),以及我自己的推导和验证。文章的解释没有照搬,不准确的地方已标出(第 3.4 节、第 5 节)。

### 源笔记定位

| 内容 | 锚点 |
|---|---|
| 定义:每线程有限步完成,强于 lock-free | [源笔记:2](../../trading-system-notes/chinese/01-low-latency/30-wait-free编程.md:2) |
| Helping 协作 | [源笔记:3](../../trading-system-notes/chinese/01-low-latency/30-wait-free编程.md:3) |
| 状态编码:高位放标志位 | [源笔记:4](../../trading-system-notes/chinese/01-low-latency/30-wait-free编程.md:4) |
| 线性一致性 | [源笔记:5](../../trading-system-notes/chinese/01-low-latency/30-wait-free编程.md:5) |
| 适用场景 / 取舍 | [源笔记:6](../../trading-system-notes/chinese/01-low-latency/30-wait-free编程.md:6) · [源笔记:7](../../trading-system-notes/chinese/01-low-latency/30-wait-free编程.md:7) |
| Wait-Free Sticky Counter 代码 | [源笔记:11](../../trading-system-notes/chinese/01-low-latency/30-wait-free编程.md:11) |

---

## 0. 先备知识

四档进度保证(blocking / obstruction-free / lock-free / wait-free)和"有限步不等于有限时间",在 [09 笔记〔补〕§1、§2](09-lock-free-queue及micro-batching.md:191) 里已经讲过,这里不重复。本节只用一句话:**lock-free 保证"有人"能前进,wait-free 保证"每个人"都能前进。**

## 1. 这个例子在解决什么:sticky counter

一个"粘"在零上的计数器,三个操作:

- **`increment_if_not_zero()`**:还没归零就加一,成功返回 `true`;已经归零就拒绝,返回 `false`。
- **`decrement()`**:减一。**恰好把它减到零的那一次**返回 `true`,其余都返回 `false`。
- **`read()`**:读当前值。

"粘"的意思是:一旦归零就永远是零,不许复活。

现实对应是引用计数:`weak_ptr::lock()` 要做的事就是"强引用计数还没归零才能加一";`decrement()` 返回 `true` 的那个线程负责销毁对象。(标准库具体怎么实现的,我没有在本机核对源码,不下结论。)

整个设计要守住一条硬约束:**销毁只发生一次**——多一个 `true` 是重复销毁,少一个 `true` 是泄漏。

## 2. 先看 lock-free 版(源笔记没有,补作对照)

```cpp
struct LockFreeStickyCounter {
    std::atomic<uint64_t> n{1};

    bool increment_if_not_zero() {
        uint64_t cur = n.load();
        while (cur != 0) {
            if (n.compare_exchange_weak(cur, cur + 1)) return true;  // 失败时 cur 被刷成最新值
        }
        return false;
    }

    bool decrement() { return n.fetch_sub(1) == 1; }
};
```

- `decrement` 只是一次 `fetch_sub`,没有循环。
- `increment_if_not_zero` 里的 CAS 循环就是 lock-free 但**不是** wait-free 的那一处:别的线程一直在改 `n` 的话,这个线程理论上可以一直 CAS 失败,重试次数没有上界。
- 竞争不激烈时这个版本很轻,一次 CAS 就完事。要防的只是尾部:对 P99.9 敏感的地方,"理论上可能无限重试"就是风险点。

## 3. wait-free 版:怎么把循环去掉

### 3.1 想法:借高位当"状态贴纸"

计数只用低 62 位,高两位当标志:第 63 位 `is_zero`(已经钉死为零),第 62 位 `helped`(有读者替减法线程钉过零)。这样**一次原子操作既改数值、又带状态**,不用"先读、再判断、再 CAS 重试"。

### 3.2 源笔记的代码,加上步骤标注

为便于阅读省略了 `memory_order` 参数(源代码用的是 acquire / acq_rel)。

```cpp
static constexpr uint64_t is_zero = 1ull << 63;
static constexpr uint64_t helped  = 1ull << 62;
std::atomic<uint64_t> counter{1};                      // 低 62 位是计数

bool increment_if_not_zero() {
    return (counter.fetch_add(1) & is_zero) == 0;      // 先加后看:旧值没有 is_zero 才算成功
}

bool decrement() {
    if (counter.fetch_sub(1) == 1) {                   // ① 旧值是 1 → 是我把它减到 0 的,进入"窗口"
        uint64_t expect = 0;
        if (counter.compare_exchange_strong(expect, is_zero))
            return true;                               // ② 把瞬时的 0 钉成永久的零,我来宣告
        else if ((expect & helped) &&                  // ③ 钉失败,但看到 helped:有读者替我钉过了
                 (counter.exchange(is_zero) & helped))
            return true;                               //    exchange 抢认领权:只有第一个取走 helped 的算数
    }
    return false;                                      // 没减到零,或者被别人加回去了
}

uint64_t read() {
    uint64_t val = counter.load();
    if (val == 0 && counter.compare_exchange_strong(val, is_zero | helped))
        return 0;                                      // 读到瞬时的 0:不干等,帮忙钉零(带 helped)
    return (val & is_zero) ? 0 : (val & ~(is_zero | helped));
}
```

- **`increment_if_not_zero`**:无条件 `fetch_add(1)`,事后看旧值有没有 `is_zero` 位。没有 CAS 循环。
- **`read`**:读到 0 说明有线程正处在"减到零、还没钉死"的窗口里,`read` **不干等**,顺手帮它把零钉死。
- **`decrement`**:三步,①②③ 见注释;每个操作最多做 `fetch` 一次、CAS 一次、`exchange` 一次,步数有上界。

### 3.3 核心难点:物理上的 0 不等于逻辑上的 0

`decrement` 里 `fetch_sub` 之后到"钉零"之前有一段窗口:内存里是 0,但**逻辑上还没归零**。窗口里可能发生三种事,整个算法就是围绕它设计的:

| 窗口里发生了什么 | 结果 |
|---|---|
| 没人打扰 | `decrement` 自己 CAS 钉零成功,返回 `true` |
| `increment` 进来把 0 加回 1(合法:它看到旧值没有 `is_zero` 位) | `decrement` 的 CAS 失败,值里也没有 `helped` → 返回 `false`。逻辑上当作这次减发生在那次加**之后** |
| `read` 进来看到 0 | `read` 帮忙钉零(带 `helped`)并返回 0;`decrement` 的 CAS 失败,但看到 `helped` → 去认领 |

### 3.4 `exchange` 那一步到底在干什么(纠正文章的说法)

文章把这一步解释成核实 `helped` 标记确实是冲自己来的、而不是随机数据。这个解释不准确:标志位里没有"随机数据",`helped` 也是匿名的,不存在"冲谁来的"。

真正的作用是**认领权只发一张**。窗口里可能同时有不止一个 `decrement` 都以为"是我减到零的"——比如:线程 T1 先减到 0;线程 T2 把它加回 1、又马上减回 0;此时 T1、T2 都在窗口里。读者只会立一个 `helped`,T1 和 T2 的 CAS 都失败,并且都看到这个 `helped`。如果"看到就直接返回 `true`",就是两个线程都去销毁对象。

`exchange(is_zero)` 是原子的"取走并清除":谁先执行,谁拿回带 `helped` 的旧值 → 返回 `true`;后执行的拿回的旧值已经没有 `helped` → 返回 `false`。所以它是 test-and-clear 式的认领,不是身份核实。第 5 节的变体 M1 验证了这一点。

## 4. 线性一致性怎么读

- 源笔记一句话:用逻辑顺序解释交错,CAS 失败是因为别人改了值时,直接判定先后顺序并返回,不需要自旋([源笔记:5](../../trading-system-notes/chinese/01-low-latency/30-wait-free编程.md:5))。
- 我的读法:把每个操作排成某个先后次序,只要各操作的返回值和"一个顺序执行的 sticky counter"一致就行。窗口情形里 `decrement` 的 CAS 因为 `increment` 而失败,就把这次 `decrement` 排在那次 `increment` 之后,于是"没减到零,返回 `false`"是自洽的——用**排序**代替了**重试**。
- 这个说法有边界,见第 5 节的发现。

## 5. 我做的验证(有限规模的穷举,不是证明)

为了确认上面几点(尤其是 `exchange` 的作用,以及"线性一致性"的说法),我用 Python 把三个操作拆成"每步一次原子操作",对小场景穷举所有线程交错,对每一种交错检查两件事:

1. 结果能不能对应某个**合法的先后顺序**(尊重"A 在 B 开始之前已经结束"这种先后关系)。
2. 所有引用都释放之后,是不是**恰好一个** `decrement` 返回 `true`,并且最终状态钉死为零。

脚本:[`30-sticky-counter-model-check.py`](30-sticky-counter-model-check.py),用法 `python 30-sticky-counter-model-check.py base|m1|m2|m3`(输出里 Z 表示 `is_zero` 位,H 表示 `helped` 位)。场景:① 1 个减 + 1 个(加,成功后再减)+ 1 个读;② 同上但 2 个读;③ 1 个减 + 2 个读(无加);④ 1 个减 + 2 个(加,成功后再减)+ 1 个读。分别有 60 / 1178 / 36 / 2004 种交错。

**结果:**

| 版本 | 检查内容 | 结果 |
|---|---|---|
| 源笔记原版 | 加/减的返回值可线性化 + 恰一个 `true` + 最终钉零 | 四个场景 **0 违规** |
| 源笔记原版 | 再加上:`read` 的"是不是零"可线性化 | **0 违规** |
| 源笔记原版 | 再加上:`read` 返回的**非零数值也要精确** | **有反例**:①1/60、②32/1178、④44/2004(③没有并发 `increment`,0) |
| 变体 M1:`decrement` 看到 `helped` 就直接返回 `true`(没有 `exchange`) | 恰一个 `true` | 反例:**两个** `decrement` 都返回 `true`(重复销毁) |
| 变体 M2:`read` 看到 0 直接返回 0(不帮忙钉零) | `read` 的零判断 | 反例:`read` 返回 0 之后,`increment` 还能成功(这个零不稳定) |
| 变体 M3:`decrement` 的 CAS 失败后不看 `helped` | 恰一个 `true` | 反例:**所有** `decrement` 都返回 `false`(没人销毁,泄漏) |

也就是说三处设计(读者帮忙钉零、`decrement` 识别 `helped`、`exchange` 认领)每一处都承重:去掉任何一处,穷举立刻找到反例。

**关于 `read()` 的精确非零值(我自己得到的,文章和源笔记都没有说)。** 最小反例(初始计数 1):

1. T1 `decrement`:`fetch_sub`,1→0(T1 成为"减到零"的候选)。
2. T2 `increment`:`fetch_add`,0→1,旧值没有 `is_zero`,成功(复活)。
3. T3 `read`:`load` 得 1,返回 1。
4. T2 `decrement`:`fetch_sub`,1→0(T2 也成了候选)。
5. T1 `decrement`:CAS(0→`is_zero`)成功,返回 `true`。
6. T2 `decrement`:CAS 失败(已经是 `is_zero`,没有 `helped`),返回 `false`。

`increment` 在 `read` 之前完成、`read` 在 T2 的 `decrement` 之前完成,而 T1 拿到了"最后一个"的 `true`——合法排序里 T1 必须排在 T2 的 `decrement` 之后,那么 `read` 那一刻的逻辑值应该是 2;但 `read` 看到的是物理净值 1。

结论:**`increment`、`decrement`、"是否为零"是可靠的,但 `read()` 返回的非零数值只能当近似值。** 这跟标准库对 `shared_ptr::use_count()` 的态度一致(我印象里 cppreference 说多线程下结果是近似的,没在本机核对)。

**边界:** 只覆盖 ≤4 个线程、每线程 ≤2 个操作的小场景;原子操作按顺序一致建模,没有建模 acquire/release 的弱序;"没找到反例"不等于证明。上面这条反例是真的(我手工核对过)。

## 6. 取舍与硬件依赖

- **买到的是"有上界",不是"更快"。** wait-free 版每个操作最多 1 次 `fetch`、1 次 CAS、1 次 `exchange`;竞争不激烈时 lock-free 版一次 `fetch_add`/CAS 就完事,更轻。所以源笔记说:读多写少、对 P99/P999 敏感的场景才值得;写密集或不在乎尾延迟时,lock-free 或自旋锁可能更好([源笔记:6](../../trading-system-notes/chinese/01-low-latency/30-wait-free编程.md:6)、[:7](../../trading-system-notes/chinese/01-low-latency/30-wait-free编程.md:7))。跟 09〔补〕§4 是同一句话:优化目标是尾延迟/确定性,不是均值。
- **"没有循环"的前提是每个原子操作都是一条不会失败重试的指令。** x86 上 `fetch_add`/`fetch_sub`(`lock xadd`)、`exchange`(`xchg`)、`compare_exchange_strong`(`lock cmpxchg`)都是。ARM 在没有 LSE 扩展(ARMv8.1 之前)时会把它们编成 load-exclusive/store-exclusive 的重试循环——那"wait-free"严格说退化成 lock-free。文章评论区第一条质疑的就是这个。
- 文章评论区另一条质疑:全文没有 benchmark。"wait-free 不一定更快"没有数据支撑,只是理论推断。

## 7. 参考(第三方,未全部核对)

- 已读:知乎《超越 lock-free 的是 wait-free》(链接见开头)。
- 同页推荐栏里的两篇,标题来自页面,**我没有打开读**:《一个介于 wait-free 和 lock-free 的高性能 MPSC 队列》《当 Lock-Free 还不够:延迟可能更低的 Wait-Free 算法》。后者在页面上标注出处为 Daniel Anderson,*When Lock-Free Still Isn't Enough*,CppCon 2024;这个例子应是出自该演讲——我没有核对原演讲。

---

## 与其他主题的连接

- **[09 笔记〔补〕](09-lock-free-queue及micro-batching.md:185)** —— 四档梯度、`LFQueue` 各操作分别属于哪一档。
- **三/#2 实现线程安全队列**([源笔记:857](../../trading-system-notes/chinese/03-simple-practice/02-实现线程安全队列.md:857)) —— 多生产者用 CAS 抢槽位,是 lock-free 但非 wait-free 的典型形态。
- **§5(内存模型)** —— 源笔记这段代码全用 acquire / acq_rel,是不是"最弱够用",讲 #30 时可以拿 §5 轮 3 的"最弱够用三步"来检验(本稿第 5 节的穷举没有覆盖这一点)。
