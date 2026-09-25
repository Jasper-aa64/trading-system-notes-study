# lock-free queue 及 micro-batching —— 从环形队列本体到生产可用的组件

> 源笔记:[`09-lock-free-queue及micro-batching.md`](../../trading-system-notes/chinese/01-low-latency/09-lock-free-queue及micro-batching.md)
>
> 这一章内容量大,按 established 节奏拆成多轮。轮 1:SPSC 环形队列本体。

### 源笔记定位

| 内容 | 锚点 |
|---|---|
| 无锁三件套(LOCK 前缀 / MESI / 内存屏障) | [源笔记:4](../../trading-system-notes/chinese/01-low-latency/09-lock-free-queue及micro-batching.md:4) |
| `LFQueue<T>` SPSC 环形队列 | [源笔记:15](../../trading-system-notes/chinese/01-low-latency/09-lock-free-queue及micro-batching.md:15) |
| SPSC 缓存友好优化(本地缓存索引副本) | [源笔记:136](../../trading-system-notes/chinese/01-low-latency/09-lock-free-queue及micro-batching.md:136) |
| 基于 LFQueue 的 Logger | [源笔记:186](../../trading-system-notes/chinese/01-low-latency/09-lock-free-queue及micro-batching.md:186) |
| MicroBatchProcessor 动态微批处理 | [源笔记:419](../../trading-system-notes/chinese/01-low-latency/09-lock-free-queue及micro-batching.md:419) |
| double-mmap 环形缓冲区跨尾写入优化 | [源笔记:542](../../trading-system-notes/chinese/01-low-latency/09-lock-free-queue及micro-batching.md:542) |

---

## 轮 1:SPSC 环形队列本体

### 无锁三件套,先认个门

源笔记开头点了三个名字,其实都是 §5 已经讲透的东西换了个场合出现:

- **LOCK 前缀指令**——`std::atomic` 的 `fetch_add`/`compare_exchange` 这类读改写操作,底层落地就是一条带 `LOCK` 前缀的 x86 指令,保证"读-改-写"这三步对其他核不可分割地原子发生。
- **MESI 协议**——§5 轮 1/2 讲的"缓存行状态机",这里第一次点名对上正式名字(Modified/Exclusive/Shared/Invalid)。
- **内存屏障**——就是 §5 轮 3 的 acquire/release、轮 2 的 fence。

这三样不是新概念,是 §5 词汇表第一次被正式点名,后面直接用。

### `LFQueue<T>` 整体结构

主角是 `LFQueue<T>`——一个 SPSC(单生产者单消费者)无锁环形队列。底层是固定大小的 `std::vector<T> store_`,大小 `capacity_` 永远是 2 的幂,构造时用 `round_up_to_power_of_2` 把用户给的数量向上取整。

**为什么容量必须是 2 的幂**:索引要不断往前走、又要落回数组范围内,普通做法是 `index % capacity`,但取模在大多数架构上是真正慢的 ALU 操作(几十个 cycle 量级)。如果 `capacity` 是 2 的幂,`mask_ = capacity_ - 1` 在二进制上就是一串低位全 1 的数(比如 `capacity=8 → mask_=0b111`),`index & mask_` 对**任意** `index` 都跟 `index % capacity` 结果完全相等,但只要 1 个 cycle 的位运算——这正是 #8 手写汇编表里"整数除常数可改乘+移位"那条思路的极致版本:直接把除法从需求里抹掉,而不是优化它。换成非 2 的幂(比如 `capacity=6`),这个等价关系不成立,老老实实还得用 `%`。

`round_up_to_power_of_2` 本身(`v |= v>>1; v |= v>>2; ...`)是把最高位以下的所有位依次填成 1,再 +1 进位到下一个 2 的幂——记住"这是找下一个 2 的幂的标准位技巧"就够。

### 生产者接口:两步,不是一步

接口分两步,不是一步到位的 `enqueue(T)`:

- **`getNextToWriteTo()`**——返回队列内部槽位的指针(不是拷贝一份出来),调用者直接往这个指针指向的内存里写数据。这样设计是为了省一次拷贝:如果接口是 `enqueue(const T&)`,数据还要先拷进队列内部;现在是直接在目标位置写,少一趟搬运。
- **`updateWriteIndex()`**——写完数据后单独调用,把索引往前推一格,真正"发布"这次写入。
- **`tryGetNextToWriteTo()`**——判断"下一个槽位是不是已经追上了消费者还没读完的位置"(队列满)。
- **`getNextToWriteTo()`**——满的时候不放弃,而是 `_mm_pause()`(告诉 CPU"我在自旋等,别把流水线资源全押上去")然后重试,是个自旋等待接口。

### 消费者接口:对称的两步

- **`getNextToRead()`**——返回下一个未读槽位的指针(队列空时返回 `nullptr`)。
- **`updateReadIndex()`**——读完之后调用,推进 `next_read_index_`。

这里多了第三个原子量 `num_elements_`,由生产者 `fetch_add`、消费者 `fetch_sub`,专门回答"队列里现在有几个元素"——是否值得单独维护,轮 1 自检题里有讨论,见附一 Q4。

### memory_order 是怎么选的——接回 §5

- `next_write_index_.load(relaxed)` ——读**自己**刚写过的东西,不需要跨线程可见性保证。
- `next_read_index_.load(acquire)` ——读**对方线程**写的东西,需要。
- `next_write_index_.store(..., release)` 和 `num_elements_.fetch_add(1, release)` ——配对的是消费者那边 `num_elements_.load(acquire)`。release 保证"往槽位里写数据"这个普通(非原子)内存操作不会被重排到 release store 之后;acquire 保证消费者一旦看到计数变化,就一定能看到对应的数据已经真正写进去了。

这就是一对活生生的 happens-before 边,跟 §5 轮 3 讲的"配对"是同一件事,只是这次是在真实产品代码里长出来的。

### 〔补〕一个真实 bug:运算符优先级

顺手抓到的,不是编的,是读代码读出来的。`tryGetNextToWriteTo()` 里这一行([源笔记:39](../../trading-system-notes/chinese/01-low-latency/09-lock-free-queue及micro-batching.md:39)):

```cpp
if (UNLIKELY((current_write + 1) & mask_ == current_read & mask_)) {
```

意图很清楚:"下一个写位置绕回环里的位置,如果等于当前读位置绕回环里的位置,说明满了",应该是 `((current_write + 1) & mask_) == (current_read & mask_)`。

但 C++ 里 `&`(按位与)的优先级比 `==`(相等比较)**更低**——经典反直觉设计,大多数人凭直觉觉得 `&` 该像乘法一样绑得更紧,标准里正好相反。真实解析顺序是:

```
(current_write + 1) & (mask_ == current_read) & mask_
```

`mask_ == current_read` 先算,几乎永远是 `false`(0),于是整个表达式几乎恒等于 0(严格说:读索引单调递增、只会经过 `mask_` 这个值一次,只有它恰好等于 `mask_` 且 `current_write + 1` 是奇数的那一小段时间里表达式才是 1,可以忽略)。**这个"队列满了吗"的判断几乎永远是假的**——生产者会一直认为队列没满,哪怕已经绕了一整圈追上消费者还没读的数据,直接覆盖没读完的旧数据,而不是正确地拒绝写入。类底部的 `is_full()`([源笔记:94](../../trading-system-notes/chinese/01-low-latency/09-lock-free-queue及micro-batching.md:94))是一模一样的写法,同一个坑。

修法:加括号。

> **记住这个模式**:凡是看到 `&`/`|` 挨着 `==`/`!=`/`<` 这类比较符没加括号,先停下来查优先级——这类坑在真实 C/C++ 代码里相当常见。

---

## 附一:轮 1 自检题与你的答案

**Q1:为什么这里要把队列容量向上取整成 2 的幂?换来的具体好处是什么,值得吗?**
你的答案:可能避免开销大的模运算,值得。
批注:方向对(避免昂贵的取模,值得),但少了机制:为什么"2 的幂"specifically 能让 `&` 替代 `%`——`mask_ = capacity_ - 1` 在 2 的幂下是一串连续的 1,`index & mask_` 才会对任意 `index` 都等于 `index % capacity`;换成非 2 的幂这个等价关系就不成立了。答案停在结论,没落到这步机制。

**Q2:`tryGetNextToWriteTo()` 里,读自己线程写的 `next_write_index_` 用了 `relaxed`,读对方线程写的 `next_read_index_` 却用了 `acquire`——这个差别背后的道理是什么?**
你的答案:自己线程写的本身不会乱序,有 store forwarding;读对方线程写的用 `acquire`,可以保证先清空自己的 Invalidate Queue,防止读到别的线程改过、但本线程还没同步到 L1 的值。
批注:**acquire 那半句是你自己的综合,而且我没这么展开讲过**——把 §5 轮 2 的"收件箱"机制直接搬来用在这里,方向完全对。**relaxed 那半句因果层次接错了**:store forwarding(STLF)是真实机制,但它回答的是"同线程读自己刚写的东西为什么快",不是"为什么 relaxed 就够用"。真正原因是 `next_write_index_` 从头到尾只有生产者一个线程读也只有它写,C++ 内存模型保证单线程对同一原子量的操作永远按程序序被自己看到,不管标什么 memory_order 这条保证都在。STLF 是硬件怎么让这次读很快的**实现细节**,relaxed 为什么"正确"是**内存模型的单线程程序序保证**——两层不同的东西,跟轮 2 Q1"机制对但收口差一步"是同一类问题。

**Q3:这个运算符优先级 bug,具体会造成什么后果?**
未展开作答——本人反馈这道题问法有问题("感觉偏题了"),回看确实如此:讲解正文里已经把"生产者永远以为没满、覆盖未读数据"这个后果讲完了,再单独问等于要求复述,不是一道要求自己综合的题,是出题出弱了,以后不重复这类问法。根因(位运算优先级低,没注意)已经在讨论里确认无误。

**Q4:`num_elements_` 是单独维护的原子计数器,而不是靠 `next_write_index_ - next_read_index_` 现算——为什么要多维护这一个量?**
你的答案:提出"要不就两个原子量就够了";并转述外部说法"极致 HFT 系统设计者往往会省掉这第三个原子量",求核实。
批注:方向和外部说法都对,补机制:`fetch_add`/`fetch_sub` 是 RMW,执行前必须把 `num_elements_` 所在缓存行抢到独占状态(Modified/Exclusive),生产者每次 enqueue 抢一次、消费者每次 dequeue 又抢一次——是 §5 轮 4"独占权来回打"的重现,只是这次不是伪共享(`num_elements_` 自己有独立缓存行),是两个线程真要改同一个变量。改成两个原子量、`size()` 现算,好处是 `size()` 变成两次普通 `load`——load 只需缓存行处于 Shared 状态就够,不需要抢独占权。

〔补〕顺手在源文件发现一个相关的坑:被注释掉的替代版 `size()`([源笔记:84-89](../../trading-system-notes/chinese/01-low-latency/09-lock-free-queue及micro-batching.md:84))写的是 `h = write_index.load(); t = read_index.load(); return (h + capacity_ - t) & mask_;`——先读 `h`、再读 `t`,顺序反了。反例(**2026-09-20 更正**:原文把调用者写成"消费者自己",这不成立——消费者在自己执行 `size()` 的过程中,自己的 `t` 不会动):由**第三个线程**(监控/统计线程)调用时,先采到 `h1=10`,之后生产者继续推进、消费者也追到 `t2=11`(合法,`t≤h` 随时成立),`h1 - t2 = -1`——不带掩码会下溢成巨大的无符号数,这版带 `& mask_`,则会算成一个接近"满"的错值。安全顺序应该反过来:**先读 `t`(落后的那个),后读 `h`(领先的那个)**——`h` 单调不减且始终 ≥ `t`,后读到的 `h` 一定 ≥ 先读到的 `t` 那一刻的真实 `h`,所以 `h - t ≥ 0`,对任何线程调用都成立。(如果调用者只会是生产者或消费者自己,两种读序都不会读出负数——自己的那个游标在调用期间不会动;固定先读 `t` 是给"任何线程都可能调"的公共 `size()` 准备的。三种调用者我用一个小的交错穷举模型核对过,见附三 Q1。)这段代码是注释掉、没跑在生产环境里的,所以不算真炸的 bug,但如果自己手写一个 SPSC 队列的 `size()`,这个采样顺序是经典的面试追问坑。

---

## 轮 2:SPSC 缓存友好优化 + double-mmap 环形缓冲区

### 问题:轮 1 的队列每次操作都要跨核读一次对方的索引

轮 1 里 `tryGetNextToWriteTo()` 每次调用都要 `next_read_index_.load(acquire)`——哪怕队列还空着一大半,离"满"还远得很,生产者照样每次都要去看一眼消费者的索引。消费者那边同理,每次 `dequeue` 都要读生产者写的索引。这是真实的跨核缓存行流量,不是免费的。

### 修法:本地缓存对方的游标——呼应 §5 已经预告过的"影子副本"

这个技巧其实已经见过一次了:[05 笔记第 513 行](05-内存模型与缓存及流水线.md:513)"批量通知 / 影子副本"那节,当时用的例子是 LMAX Disruptor 的 `cached_tail`。#9 这里是同一个模式,换了个名字(`head_cached_`/`tail_cached_`)、落进了一个真实队列类里:

```cpp
// 仅示意关键路径(源笔记自己标注是示意,命名跟 LFQueue 实际字段不同)
bool enqueue(const T& item) {
    const size_t t = tail_.load(std::memory_order_relaxed);
    const size_t next_t = (t + 1) & mask_;

    if (next_t == head_cached_) {                         // 本地额度用完才看一眼
        head_cached_ = head_.load(std::memory_order_acquire);
        if (next_t == head_cached_) return false;          // 真满
    }

    buffer_[t] = item;
    tail_.store(next_t, std::memory_order_release);
    return true;
}
```

`head_cached_` 是生产者私有的一个普通变量(不是原子量,只有生产者自己碰),大多数调用只跟这个本地变量比一下,零跨核流量;只有当本地缓存"看起来"可能追上了,才真正付一次 `acquire` 的跨核读去确认。

### 为什么这样做是安全的:缓存只会往"保守"方向错

`head_cached_` 可能过期,但只会偏旧——消费者的索引只会单调往前走,不会后退,所以本地缓存的值永远 ≤ 真实的当前值。用这个偏旧的值去判断"是不是碰到边界了",结果只会是:提前触发一次"可能满了"的检查(其实还没真满),从来不会漏掉一次真正该拒绝的写入——真正做出"满/不满"这个决定的,永远是重新读到的那次真实 `acquire` 值,本地缓存只负责"要不要现在就去付这次跨核读的钱",不负责下最终结论。

### double-mmap:把跨尾写入变成一次连续拷贝

环形缓冲区写数据时,如果要写的这一段正好跨过缓冲区物理末尾,常规做法得拆成两段拷贝——对批量/向量化的 `memcpy` 来说,这种拆分比一次连续拷贝慢。

**思路**:把同一块物理内存在虚拟地址空间里连续映射两次,让缓冲区在地址上看起来是两倍长的连续区间。写操作从当前位置开始写,哪怕写过了"物理末尾",落进的是第二次映射的地址,而第二次映射背后是**同一块物理页**——跨尾这件事对写代码的人完全透明,一次 `memcpy` 搞定。

具体三步:

1. `mmap(..., PROT_NONE, ...)` 预留 `2*N` 大小的一段虚拟地址,只占位置,不能真正读写。
2. `memfd_create` 建一个 `N` 字节大小的匿名内存文件(纯内存里的"文件",没有磁盘路径),`ftruncate` 定好大小。
3. 用同一个 fd,`mmap(..., MAP_SHARED | MAP_FIXED, fd, 0)` **两次**,分别映射到预留区间的前半段和后半段。

```cpp
// reserve = mmap(nullptr, 2*N, PROT_NONE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
// fd = memfd_create(...); ftruncate(fd, N);
// mmap(reserve,     N, PROT_READ|PROT_WRITE, MAP_SHARED|MAP_FIXED, fd, 0);
// mmap(reserve + N, N, PROT_READ|PROT_WRITE, MAP_SHARED|MAP_FIXED, fd, 0);
```

**为什么非要 `memfd_create`,不能直接两次 `mmap(MAP_ANONYMOUS)`**:`MAP_ANONYMOUS` 映射背后没有一个可以被"再次引用"的对象——每次匿名映射都会拿到一批全新、独立的物理页,两次匿名映射不会共享同一批页。要让两个不同的虚拟地址背后是同一批物理页,必须有个有名字、可以被多次 `mmap` 指向的东西——这正是文件描述符的作用。这跟 #7 的 SysV 共享内存(`shmget`)是同一个道理,只是那里的场景是跨进程共享,这里是同一进程里自己映射两次做地址技巧。

**这一套 `mmap` 调用发生在什么时候**:全在环形缓冲区**构造的时候**,一次性做完;之后热路径上的 `enqueue`/`dequeue` 只是普通的内存写,没有任何系统调用。这是这一章第三次出现同一个模式了(§6 轮 2 的 `M_MMAP_MAX`、#7 的 `HugePageAllocator`、现在这里)。

**代价**(源笔记自己列的):`N` 一般要页对齐;映射失败要完整回滚(`munmap`/`close`);更适合"跨尾频繁、拷贝本身是主要成本"的场景;额外的映射会增加页表项和 TLB 压力——直接接回 #7:同一块物理内存现在被两套虚拟地址覆盖,是拿 TLB 压力换"跨尾写入不用拆两段"的吞吐,不是纯赚。

---

## 附二:轮 2 自检题与你的答案

**Q1:`head_cached_` 有可能读到过期值——这会不会导致"本该拒绝的写入被错误放行,覆盖了消费者还没读的数据"?为什么?**
你的答案:生产者缓存的值就算是旧的,在追上这个旧值之前状态都是对的;追上时分两种情况——消费者其实没再消费,那就是真满;消费者已经消费了,那就真正 acquire 一次准确判断,并且重新缓存。
批注:两个分支拆得完全正确,是自己独立分出来的("真满" vs "假满要重查"),没有照抄原文。补一层没展开的"为什么":为什么恰好是"追上 `head_cached_` 那一刻"才是该重新读的时间点——因为消费者真实进度只会单调往前走不会后退,所以从缓存那一刻到现在,真实 head 只可能 ≥ `head_cached_`;只要 `next_t` 没追到 `head_cached_`,就不可能撞上真实 head,追到了才第一次有可能撞上,这时候才值得付一次 acquire 的代价确认。

**Q2:double-mmap 这套 `mmap`/`memfd_create` 调用,会不会像 §6 轮 2 提醒过的那样构成热路径风险?**
你的答案:不会,这是构造时的冷路径。
批注:结论对,但这道题问法有问题——正文里已经把"这些调用全在构造时,热路径只有普通内存写"讲完了,这题基本是要求复述,跟轮 1 Q3 是同一类设计缺陷。轮 1 说过以后不再问这类题,这次还是出现了,如实记下,以后出题前先自查是否已经在正文里把答案讲完。

**Q3:为什么不能直接用两次 `MAP_ANONYMOUS` 的 `mmap` 完成"同一块物理内存映射两次"这件事?**
你的答案:直接用没有文件描述符来锚定同一块物理内存/文件。
批注:对,"用文件描述符锚定同一块物理内存"是自己的压缩表达,不是照抄原文,抓住了真正机制(需要一个能被多次引用的锚点,而不是每次 `mmap` 各拿一批全新物理页)。跟 #7 的 SysV 共享内存(`shmget`)是同一件事的另一种做法。

---

## 轮 3:Logger 与 MicroBatchProcessor —— 把 LFQueue 用进真实组件

> 状态:讲解已归档;检验 Q1–Q3 已答并批改,见**附三**。本节对源笔记的 `log()` 做了一处改进(改进版写在本笔记里,**源笔记文件没有改动**——改了会让所有 `:行号` 锚点漂移)。

### Logger:把日志从热路径搬到后台线程

日志这件事很尴尬:trading 逻辑跑在热路径上,但写日志(尤其是落盘)天然慢——不能让它挡住热路径,但又不能不记。解法就是轮 1 的 `LFQueue<T>`:热路径只管往队列里塞一条记录,真正干活(格式化、写文件)交给独立的后台线程。源码见 [源笔记:186](../../trading-system-notes/chinese/01-low-latency/09-lock-free-queue及micro-batching.md:186)。

### `LogElement`——队列里存的最小单位

```cpp
struct LogElement {
    LogType type_ = LogType::CHAR;
    union {
        char c; int i; long l; long long ll;
        unsigned u; unsigned long ul; unsigned long long ull;
        float f; double d;
        char s[256];
    } u_;
};
```

`union` 的大小取决于最大的成员——`char s[256]`——再加对齐,一个 `LogElement` 是 **264 字节**(按 x86-64 对齐规则算的;这台机器没有 C++ 编译器,没有真编译)。哪怕只存一个 `char`,也占 264 字节。

这是**代价**:`LFQueue<T>` 底层是 `std::vector<T> store_`,要求元素定长,所以不管实际存的是一个 `int` 还是一整段字符串,都按"最大可能的那种"占位。跟轮 1"容量为什么要是 2 的幂"是同一类"为了环形队列的机制,牺牲一点别的东西"的取舍。

### 生产者侧与消费者侧

- **生产者**:各种类型的 `pushValue` 重载(`char`/`int`/`double`/`const char*`……)拼出对应类型的 `LogElement`,最后都落到这一个,直接复用轮 1 的两步接口:

```cpp
auto pushValue(const LogElement &log_element) noexcept {
    *(queue_.getNextToWriteTo()) = log_element;   // 源笔记:309
    queue_.updateWriteIndex();
}
```

- **消费者**:`flushQueue()` 跑在独立线程上,排空一批就 `flush()` 一次,然后睡 10 ms 再看——不是纯自旋,因为这个线程本来就不追求纳秒级延迟,睡一觉换 CPU 时间是合理的:

```cpp
while (running_) {
    for (auto next = queue_.getNextToRead(); queue_.size() && next; next = queue_.getNextToRead()) {
        switch (next->type_) { /* 按类型写入 file_ */ }
        queue_.updateReadIndex();
    }
    file_.flush();
    std::this_thread::sleep_for(10ms);
}
```

- **协议**很简单:队列里每个元素 = "往输出流里追加一小段内容",消费者顺序读、顺序写。SPSC 只有一个生产者线程,所以一条日志的各个元素在流里是连续的(一个 `Logger` 实例只能被一个线程写)。

### 析构:先排空,再停线程

```cpp
~Logger() {
    while (queue_.size()) { std::this_thread::sleep_for(1s); }   // ① 先等队列排空
    running_ = false;                                            // ② 再让后台线程退出
    logger_thread_->join();
    file_.close();
}
```

不是简单粗暴地 `running_ = false` 就完事:先确认已经压进去的日志都被后台线程写完,再收线程。这是个值得记住的"优雅关闭"顺序:先确认在制品清空,再收线程,不是反过来。

### 〔补〕`log()` 逐字符 push:一条日志 ≈ 30 次 push

`log(const char *s, const T &value, A... args)` 是递归可变参模板,按 `%` 做格式串替换([源笔记:359](../../trading-system-notes/chinese/01-low-latency/09-lock-free-queue及micro-batching.md:359))。它把字面量文本**逐字符** `pushValue(*s++)` 压进队列,只有碰到 `%` 才 `pushValue(value)`,然后递归处理剩下的参数。

注意这个 Logger 的占位符是**单个 `%`**(类型由参数的 C++ 类型决定),不是 printf 的 `%d`——写成 `%d` 的话,`d` 会被当作普通字符原样输出。

以 `log("Order Executed, id=%, price=%\n", id, price)` 为例:

| 部分 | push 次数 | 每次压进队列的是什么 |
|---|---|---|
| `Order Executed, id=`(19 个字符) | 19 | 每个字符一个 `LogElement`(类型 CHAR) |
| 第一个 `%` → `id` | 1 | 一个 INTEGER 元素 |
| `, price=`(8 个字符) | 8 | 8 个 CHAR 元素 |
| 第二个 `%` → `price` | 1 | 一个 DOUBLE 元素 |
| `\n` | 1 | 一个 CHAR 元素 |

合计 **30 次 push**。每次 push 有两件事:拷贝一个 264 字节的 `LogElement`;`updateWriteIndex()` 里一次 release store 加一次 `num_elements_.fetch_add`(带 `lock` 前缀的指令,x86 上自带全屏障)。

这带来三个问题(数字都是从代码算出来的,没有实测):

1. **热路径成本**:一条日志 30 次拷贝 + 30 次 `lock` 指令。消费者线程一直在读 `num_elements_`,生产者每次 `fetch_add` 都要把这条缓存行抢回来——轮 1 Q4 讨论的"独占权来回打",被放大了 30 倍。
2. **缓存污染**:一条日志写约 7.7 KiB,约 124 个缓存行,而有用信息只有几十个字节(每个 CHAR 元素的 264 字节里有用的约 2 字节,不到 1%)。这会挤占热路径自己的 L1 缓存(L1 数据缓存一般 32–48 KB)。
3. **有效缓冲缩水**:`LOG_QUEUE_SIZE = 8M` 个槽听着很大,但一条日志占 30 个槽,实际只够缓冲约 28 万条日志。消费者一卡就满,`getNextToWriteTo()` 自旋,热路径被卡住(见下面〔补〕wait-free 一节)。

### 改进版 `log()`:段压入(只改 `log()`,消费者一行不动)

思路:扫描格式串时不再每个字符推一次,而是把两个占位符之间的**连续字面量**攒成一段,遇到占位符(或串尾)时一次推一个 `STRING` 元素;`%%` 转义出来的 `%` 并进当前这一段。消费者本来就会处理 `STRING`(`file_ << next->u_.s`),所以协议不变。

```cpp
// 需要 #include <cstring>(memcpy)。下面两行放在 LogElement 定义之后:
constexpr size_t kMaxStr = 255;                        // LogElement::u_.s 是 char[256],留 1 字节放结尾的 '\0'
static_assert(sizeof(LogElement::u_) >= kMaxStr + 1);

// ---- 以下放进 Logger 类里,紧跟在各个 pushValue 重载之后 ----

/// 把一段连续的字面量文字推进队列:一个 STRING 元素最多装 255 个字符,更长就切成多段。
void pushLiteral(const char *p, size_t n) noexcept {
  while (n > 0) {
    const size_t chunk = n < kMaxStr ? n : kMaxStr;
    LogElement *slot = queue_.getNextToWriteTo();      // 直接写进队列槽位,不先在栈上拼临时元素
    slot->type_ = LogType::STRING;
    memcpy(slot->u_.s, p, chunk);
    slot->u_.s[chunk] = '\0';
    queue_.updateWriteIndex();
    p += chunk;
    n -= chunk;
  }
}

template<typename T, typename... A>
auto log(const char *s, const T &value, A... args) noexcept {
  const char *run = s;                                 // 当前这段字面量的起点
  while (*s) {
    if (*s == '%') {
      if (UNLIKELY(*(s + 1) == '%')) {                 // "%%" → 一个字面的 '%'
        pushLiteral(run, static_cast<size_t>(s - run) + 1);   // 这一段收到第一个 '%' 为止(含它)
        s += 2;                                        // 两个 '%' 都跳过
        run = s;
        continue;
      }
      pushLiteral(run, static_cast<size_t>(s - run));  // 遇到占位符:先把它之前的整段文字一次推进去
      pushValue(value);                                // 再推参数值
      log(s + 1, args...);                             // 剩下的交给递归
      return;
    }
    ++s;
  }
  FATAL("extra arguments provided to log()");
}

auto log(const char *s) noexcept {
  const char *run = s;
  while (*s) {
    if (*s == '%') {
      if (UNLIKELY(*(s + 1) == '%')) {
        pushLiteral(run, static_cast<size_t>(s - run) + 1);
        s += 2;
        run = s;
        continue;
      }
      FATAL("missing arguments to log()");
    }
    ++s;
  }
  pushLiteral(run, static_cast<size_t>(s - run));      // 收尾:最后一段文字
}
```

- **效果**:上面的示例从 30 次 push 降到 5 次(文字段、值、文字段、值、`\n`);每条日志占队列 7.7 KiB → 1.3 KiB(5 × 264 B)。
- **顺带省掉**原来 `pushValue(const char*)` 里"在栈上拼临时元素 + `strncpy` 填满 255 字节"的两趟工作,`pushLiteral` 直接写进槽位。
- **没解决的**:`STRING` 元素仍是 264 字节,5 次 push 也要拷 1.3 KiB;想再压缩,要改成变长字节记录(见下一节的方向)。
- **验证(只验证逻辑等价,不验证 C++ 语法)**:这段 C++ **没有编译过**。我把原版(逐字符)和改进版(分段)的逻辑逐行翻成 Python([09-log-literal-run-check.py](09-log-literal-run-check.py)),对比 30 万组随机格式串(22.2 万组正常用例的输出完全一致,7.7 万组报错用例两版的报错类型一致)以及边界情形(空串、`%%`、`%%%`、600 字符长字面量按 255 切成 255+255+90 等)。

### 另外两个方向(不是 drop-in,要改协议)

- **传指针**:格式串是字面量,C++ 标准保证它整个程序运行期间都有效(常见实现放在 `.rodata`),所以热路径可以只压一个 8 字节的指针加各参数值(3 次 push),后台线程拿指针去读整串。但这**不只是改 `log()`**:消费者得自己解析格式串、按 `%` 去队列里取后面的参数元素,而 `LFQueue` 一次只能发布一个槽,消费者可能读到"指针已到、参数还没到"的半条记录——需要"一次申请多个槽、写完一起发布",或改成变长记录。这个办法的适用范围有前提(只对格式串成立,参数要拷字节),见附三 Q3。
- **编译期解析格式串(NanoLog 的做法)**:格式串是编译期常量,拆分工作在编译期做完;运行时只记录"格式串编号 + 参数的二进制值",拼文本推迟到离线。NanoLog 的 README 说它靠"编译期抽出静态信息、热路径只记动态部分、格式化推迟到离线"达到中位延迟约 7 ns(它自己测的,没复现);日志是二进制的,要用单独的 decompressor 还原成文本;有预处理器版(要把 Python 脚本接进构建链)和 C++17 版两种用法。
- **Quill**([odygrd/quill](https://github.com/odygrd/quill))的 README 说:前端线程把参数编码后入队,格式化和 I/O 在后台线程;队列可选 bounded/unbounded、blocking/dropping,并能监控丢弃条数和阻塞次数——正好印证〔补〕里的判断:有界队列满了只能丢或者等,成熟的库把它做成可配置项。(README 里它内部具体怎么存格式串,我没读到,不下结论。)

### MicroBatchProcessor:按积压量动态调整批大小

源笔记的说法([源笔记:419](../../trading-system-notes/chinese/01-low-latency/09-lock-free-queue及micro-batching.md:419)):批太小发挥不出批量优势,批太大又增加延迟,所以**根据队列积压量动态调整批大小**:

| 积压量 | 判定 | 批大小 |
|---|---|---|
| ≤ 10(`LOW_BACKLOG_THRESHOLD`) | 不忙,追求低延迟 | 1(`MIN_BATCH_SIZE`) |
| ≤ 100(`HIGH_BACKLOG_THRESHOLD`) | 中等 | 100(`NORMAL_BATCH_SIZE`) |
| \> 100 | 已经落后,追求吞吐 | 1000(`MAX_BATCH_SIZE`) |

`collect_messages()` 的实际行为是"**最多**拿这么多,不够就拿现有的":

```cpp
while (collected < target_size && message_queue.dequeue(msg)) {
    buffer.push_back(msg);
    collected++;
}
```

它不会傻等到攒够 `target_size` 个才处理——`dequeue` 一旦返回空(`false`)就立刻停,拿到多少处理多少。

另外,类里声明了 `LOW_BACKLOG_TIMEOUT_US`/`HIGH_BACKLOG_TIMEOUT_US` 两个"超时"常量,但翻遍 `run()`/`determine_batch_size()`/`collect_messages()`,没有任何地方用到它们——名字暗示的是"限时等待攒批",代码实际只做了"按积压量调批量上限",是策略描述和实现之间没完全对齐的一处缺口。

### `message_queue_size`——和轮 1 的 `num_elements_` 同一个模式

```cpp
void add_message(const Message& msg) {
    message_queue.enqueue(msg);
    message_queue_size.fetch_add(1, std::memory_order_relaxed);   // 生产者侧
}
// run() 里处理完一批之后:
message_queue_size.fetch_sub(batch_buffer.size(), std::memory_order_relaxed);  // 消费者侧
```

生产者每次 `add_message` 都 `fetch_add`,消费者每处理完一批都 `fetch_sub`——跟轮 1 讨论过的 `num_elements_` 是一模一样的机制:一个独立维护的原子计数器,两边都要抢它所在缓存行的独占权。能不能也像轮 1 Q4 那样省掉、改成现算?见检验题 Q1。

### 〔补〕一个真实 bug:尖括号和圆括号顺序反了

构造函数([源笔记:464](../../trading-system-notes/chinese/01-low-latency/09-lock-free-queue及micro-batching.md:464))这一行:

```cpp
explicit MicroBatchProcessor(std::function<void(const std::vector<Message>&>) func, QueueType& queue)
```

`std::function<...>` 里,函数类型自己的 `)`(关闭参数列表)应该在 `std::function` 的 `>`(关闭模板参数)**之前**。源笔记写成了 `&>)`,顺序反了,正确应该是 `&)>`。这行代码本身编译不过。

跟轮 1 那个运算符优先级 bug 是同一类教训:嵌套的 `<...>` 和 `(...)` 混在一起时,open 的顺序和 close 的顺序必须严格对称(后开的先关)。

### 检验题(已答,见附三)

1. `message_queue_size` 能不能像轮 1 的 `num_elements_` 一样省掉、靠现算得到?跟轮 1 那个场景比,这里有什么本质不一样的地方(提示:看看 `MicroBatchProcessor` 对 `QueueType` 到底提出了什么接口要求)?
2. 上面那个 `std::function` 声明写错了,正确的写法应该是什么?
3. **(新)** 传指针的办法之所以行得通,靠的是格式串是字面量。如果某个参数是 `std::string`(比如 `logger.log("symbol=%\n", sym)`),能不能也只压它的指针?为什么?如果不能,你会怎么处理?

---

## 附三:轮 3 自检题与你的答案

**Q1:`message_queue_size` 能不能像轮 1 的 `num_elements_` 一样省掉、靠现算得到?跟轮 1 那个场景比,这里有什么本质不一样的地方(提示:看看 `MicroBatchProcessor` 对 `QueueType` 到底提出了什么接口要求)?**
你的答案:可以省掉。它重新引入了 Cache Ping-Pong 争用——生产者 `add_message` 时 `fetch_add(1)`,消费者处理完一批后 `fetch_sub(batch_size)`;底层队列本来就维护 head(写指针)和 tail(读指针),真实积压量就是 head - tail,直接调队列的 safe `size()`(内部先读 tail、后读 head 采样)。
批注:**方向对,迁移得好;补一个你的答案默认的前提。**

- **对的部分**:"抢独占权来回打"接回 §5 轮 4;"先读 tail、后读 head"是附一〔补〕里的采样顺序,你在新场景里主动用上了——这就是"把学过的规则搬到新地方"。
- **默认的前提**(我的提示指向的点,正文里没讲清楚,不算你漏):轮 1 的 `num_elements_` 在 `LFQueue` **内部**,`size()` 现算直接碰得到 head/tail;这里计数器在队列**外面**,而且 `MicroBatchProcessor` 是泛型的——它对 `QueueType` 只用了两个操作:`enqueue(msg)`([源笔记:469](../../trading-system-notes/chinese/01-low-latency/09-lock-free-queue及micro-batching.md:469))和 `bool dequeue(msg)`([源笔记:533](../../trading-system-notes/chinese/01-low-latency/09-lock-free-queue及micro-batching.md:533)),**看不到 head/tail**。所以"直接调 safe `size()`"默认了一个**现在还不存在**的接口。想省掉计数器,得先把"提供 O(1)、无锁的近似 `size()`"写进对 `QueueType` 的要求(C++20 可以用 `concept` 表达),不满足的队列就不能拿来当 `QueueType`;独立计数器是"不改接口"换来的代价——泛型组件不知道底层队列长什么样时,只能自己数。
- **措辞收紧**:"物理上严格等于 head - tail"——两次 `load` 不在同一瞬间,读到的是**一对近似快照**,不是某一瞬间的精确值。这里只用来在三个档位里挑一个,近似足够(`collect_messages` 本来就是"有多少拿多少")。
- **顺带更正附一〔补〕(已改)**:那里的反例把调用者写成"消费者自己",不成立——消费者在自己执行 `size()` 的过程中,自己的 `t` 不会动。我用一个小的交错穷举模型核对了三种调用者(`H`/`T` 两个单调游标,`T ≤ H` 恒成立,生产者最多推 4 步、消费者最多追 4 步;脚本:[09-size-read-order-check.py](09-size-read-order-check.py)):

| 调用者 | 先读 head、再读 tail | 先读 tail、再读 head |
|---|---|---|
| 第三个线程(监控/统计) | **会出负数**(读 head=0 → 生产者推 4 步、消费者追 4 步 → 读 tail=4,差 −4) | 不会(最小 0) |
| 消费者自己 | 不会 | 不会 |
| 生产者自己 | 不会 | 不会 |

所以 `run()` 里(消费者自己调)读序无所谓;但 `size()` 是队列的公共接口,任何线程都可能调,应该固定成"先读 tail、后读 head"——你答案里的顺序就是这一版。(模型只验证"读序会不会读出负数"这一件事,按顺序一致的交错建模,不涉及内存序。)

〔补〕**独立计数器其实也不是严格值**(推演,没有实测):`add_message` 里 `enqueue`([源笔记:469](../../trading-system-notes/chinese/01-low-latency/09-lock-free-queue及micro-batching.md:469))和 `fetch_add`([:470](../../trading-system-notes/chinese/01-low-latency/09-lock-free-queue及micro-batching.md:470))是两步。消费者可能在生产者 `fetch_add` 之前就 `dequeue` 到这条消息、处理完并 `fetch_sub`([:490](../../trading-system-notes/chinese/01-low-latency/09-lock-free-queue及micro-batching.md:490));`size_t` 是无符号的,计数器会瞬间下溢成一个极大值,直到生产者的 `fetch_add` 补上。这一轮 `run()` 会误判成"大积压",选 1000 档。后果很小(`collect_messages` 只拿现有的),但说明它本来也只是近似值——改成现算并没有丢掉什么精度。

**Q2:上面那个 `std::function` 声明写错了,正确的写法应该是什么?**
你的答案:`function<void(const std::vector<Message>&)>`(省略了 `std::`)。
批注:对。这是上面〔补〕里讲过的点,你写出了完整正确的声明;`std::` 省略不影响。

**Q3:传指针的办法之所以行得通,靠的是格式串是字面量。如果某个参数是 `std::string`(比如 `logger.log("symbol=%\n", sym)`),能不能也只压它的指针?为什么?如果不能,你会怎么处理?**
你的答案:不能,这种 `"…"` 的字符串是字面量,不是 string;没懂。
批注:**"不能"对;你抓的区分(字面量 ≠ `std::string`)方向也对。"没懂"是合理的——这题要用到的"生命周期"这一层我没讲过就问了,是出题的问题,不是你的问题。**正文只说了"字面量整个程序期间有效",没有接上"消费者是**晚一点、在另一个线程**才去读"。补讲在这里:

1. **推指针 = 只把 8 字节地址放进队列**,字符本身留在原处,后台线程稍后再去那个地址读。"稍后"包括排队时间和 `flushQueue()` 最长 10 ms 的睡眠。所以要安全,到消费者去读的那一刻必须同时满足:(a)那个地址上的字节还没被释放;(b)内容没被改。
2. **字面量满足**:字符存在程序映像的只读数据段(常见是 `.rodata`),标准规定字符串字面量是静态存储期——整个程序运行期间存在,而且不可修改。判据不是"它写在引号里",而是"活得比消费者久、而且不变"。
3. **`std::string` 不满足**:字符存在它自己的缓冲里(短串放在对象内部,叫 SSO;长串在堆上),而对象通常是局部变量:

```cpp
void onFill(const Fill& f) {
    std::string sym = f.symbol();      // 局部对象
    logger.log("symbol=%\n", sym);     // 假设只压 sym 缓冲的地址
}                                      // ← 返回,sym 析构:缓冲被释放(短串则是栈空间被下一次调用覆盖)
// ……最长 10 ms 后,后台线程按那个地址去读 → 读到已释放/已被覆盖的内存(未定义行为)
```

   就算对象还活着,生产者之后的 `sym = …`/`append`(可能重新分配缓冲)也会和后台线程的读取撞在一起——数据竞争。
4. **`const char*` 也不一定是字面量**:`s.c_str()`、栈上的 `char buf[32]` 一样不能只推指针。
5. **怎么办:在生产者线程、对象还活着的时候,把字节拷进队列**。源码本来就是这么做的:`pushValue(const std::string&)` → `pushValue(const char*)` → `strncpy` 进 `STRING` 元素的 `char s[256]`([源笔记:349-357](../../trading-system-notes/chinese/01-low-latency/09-lock-free-queue及micro-batching.md:349))。代价:一个元素最多装 255 个字符,更长的会被**静默截断**(`strncpy(…, sizeof(l.u_.s) - 1)`);想不丢内容,可以像改进版 `pushLiteral` 那样切成多个 `STRING` 元素。

所以"传指针"这条改法**天然只对格式串成立**(它是编译期常量);参数,尤其是字符串,照旧拷值。

〔接回 #10〕讲 #10 时顺带接回这一条:那里的共享内存队列元素是 `char SecurityID[31]` 这种定长数组,由 `strcpy` 拷进去([源笔记:85](../../trading-system-notes/chinese/01-low-latency/10-spmc共享内存无锁队列应用.md:85)、[:125](../../trading-system-notes/chinese/01-low-latency/10-spmc共享内存无锁队列应用.md:125)),写入方和读取方是通过同名共享内存(`shm_open` + `mmap(0, …, MAP_SHARED)`,[:73](../../trading-system-notes/chinese/01-low-latency/10-spmc共享内存无锁队列应用.md:73))通信的不同进程([:141](../../trading-system-notes/chinese/01-low-latency/10-spmc共享内存无锁队列应用.md:141))——同一个判据在跨进程时更严,每个进程把同一块共享内存映射在各自的地址上,块里的指针到了对方进程就指错了;字面量的地址取决于两边是不是同一个程序映像、有没有被 ASLR 重定位,不能依赖(2026-09-24 更正:这里原来写的是"连字面量的地址通常也不能用";实测同一个 exe 起的两个进程里字面量地址相同,所以只能说"不能依赖",见 #10 轮 2 第 2 节)。(到时接回讲解,不单独出题。)

---

## 〔补〕SPSC 队列算不算 wait-free?——进度保证词汇 + 一次事实核对

> 起因(2026-09-20):你带了外部材料来核对——一段说"SPSC 无锁队列是 wait-free,用它写日志生产者永远不会被拖下水"的话,加两个知乎链接(搜索页没读到;文章《超越 lock-free 的是 wait-free》读到了全文,[链接](https://zhuanlan.zhihu.com/p/2003478515148928531))。
>
> wait-free 本身是源笔记 **一/#30**([30-wait-free编程.md](../../trading-system-notes/chinese/01-low-latency/30-wait-free编程.md))的内容,按编号排在后面,到时完整讲一轮;那篇文章讲的正是 #30 里同一个 sticky counter 例子。这里只补**现在就用得上的两块**:进度保证的词汇,以及那段话哪些对、哪些说过头。不出检验题。

### 1. 四档进度保证(从弱到强)

| 档位 | 保证什么 | 典型形态 |
|---|---|---|
| **Blocking(阻塞式)** | 什么都不保证:别的线程被挂起/抢占,你可能被无限期拖住 | 持锁线程被调度走,等锁的全部停摆;自旋等别的线程满足某个条件 |
| **Obstruction-free** | 只要其他线程都暂停,单独一个线程能在有限步内完成;互相干扰时可能谁都完不成(活锁) | 学术上的弱档,实战里很少专门提 |
| **Lock-free(无锁)** | **系统整体**永远有进展——总有某个线程能在有限步内完成;但**某个具体线程**可能一直输,即允许饥饿 | CAS 重试循环 `while (!compare_exchange(...))` |
| **Wait-free(无等待)** | **每个线程**都在有限步内完成,不受其他线程快慢或挂起影响,没有无界重试 | 没有循环的固定步数操作;或靠 helping 消灭重试(#30 的 sticky counter) |

一句话:lock-free 保证"**有人**能前进",wait-free 保证"**每个人**都能前进"。所以"无锁"不等于"不等待"(知乎文章评论区那句"lock free 不等于 no wait"说的就是这个)。

### 2. "有限步"不等于"有限时间"

wait-free 保证的是:**我自己**执行的步数有上界,不管别的线程多慢、被挂起、甚至死了。它**不**保证我自己的线程不被操作系统抢占、不缺页、不发生缓存缺失。所以要做到"耗时绝对可预测",是三层一起:

- **算法层**(本节):wait-free,至少不阻塞在别的线程上。
- **OS 层**:绑核 + 隔离核 + 实时优先级,让**自己的线程**不被抢占(#1/#2/#3/#4)。
- **内存/硬件层**:不缺页(`mlockall`,§6 轮 2)、缓存行不被别的核抢(`alignas(64)`,§5 轮 4)、热路径不进内核。

wait-free 保证"别人拖不了你",另外两层保证"你自己不被拖"。

### 3. `LFQueue` 里哪些操作是 wait-free,哪个不是

| 操作 | 为什么 | 进度保证 |
|---|---|---|
| `tryGetNextToWriteTo()` + `updateWriteIndex()`(生产者;满了返回 `nullptr`) | 固定次数的原子操作(几次 load/store,加 `updateWriteIndex` 里一次 `fetch_add`),没有循环,完成与否不取决于消费者 | wait-free——前提是"满"当作失败返回,不等 |
| `getNextToWriteTo()`(生产者;满了 `_mm_pause()` 自旋重试,[源笔记:45](../../trading-system-notes/chinese/01-low-latency/09-lock-free-queue及micro-batching.md:45)) | `while (true)` 等消费者腾出位置 | **阻塞式**:消费者不动,它就永远返回不了 |
| `getNextToRead()`(消费者;空了返回 `nullptr`) | 读两个原子量,没有循环 | wait-free |
| `num_elements_` 的 `fetch_add`/`fetch_sub` | 一条原子 RMW 指令 | x86 上是单条 `lock xadd`,算 wait-free;ARM 若没有 LSE 扩展(ARMv8.1 起才有单指令原子加),编译器会生成 load-exclusive/store-exclusive 重试循环,严格说退化成 lock-free——知乎文章评论区第一条讲的就是这个 |

两个推论:

- **"SPSC 是 wait-free"是对接口说的,不是对数据结构说的。** wait-free 是靠把"满/空怎么办"这个决定**推给调用者**换来的:`try` 版失败即返回,调用者去决定丢、重试还是阻塞;`getNextToWriteTo()` 自旋版等于把这个决定内置成了"阻塞"。
- **轮 1 Q4 那个优化(去掉 `num_elements_`)顺带让这个性质更干净。** 去掉之后两端热路径只剩普通 load/store,没有任何 RMW,wait-free 不再依赖硬件有没有单指令原子加。

### 4. 那段话哪些对、哪些说过头

- **"核心目的是绝不让生产者(热路径)阻塞或等待"** —— **对,这是设计目标。** 下面几条讲的是它在什么条件下才成立。
- **"互斥锁竞争 → `futex` 挂起 → 上下文切换,1000~10000 ns"** —— 方向对,数量级可用(是量级不是常数,我没在你的机器上测过)。更要命的是**什么时候被唤醒不归你管**,取决于调度器;如果持锁的是低优先级后台线程,就是 #2 讲过的**优先级反转**的现成场景。
- **"SPSC 队列是 wait-free,不需要 CAS 重试"** —— **对,但有前提**:走"满了就失败返回"的接口。`getNextToWriteTo()` 那个自旋版是阻塞式(见 §3)。
- **"push = 一次赋值 + 一次轻量 barrier,固定 1~3 ns"** —— **量级是最好情况,不是保证。** 那是"数据和索引都在本核 L1、对端没在抢缓存行"时的数字(我没测过)。x86 上 release store 就是一条普通 `mov`,没有额外的 fence 指令(§5 轮 3);"缓存行刷新"的说法不准确——没有谁在"刷新",是 store buffer 和一致性协议在后台搬。影子副本(轮 2)、`alignas(64)`(§5 轮 4)是在帮你**接近**这个最好情况,不是它自动成立。
- **"即使后台线程卡死或崩了,生产者也绝不会被拖下水"** —— **说过头。** 只在队列没满时成立:队列有界,满了要么丢(`try` 版返回 `nullptr`),要么等(自旋版把热路径卡住)。
  - Logger 的 `pushValue` 走的正是自旋版([源笔记:309](../../trading-system-notes/chinese/01-low-latency/09-lock-free-queue及micro-batching.md:309))。它靠 `LOG_QUEUE_SIZE = 8M` 个元素把"满"推到几乎碰不到:每个 `LogElement` 是 264 字节(`char s[256]` 撑大的 union 加对齐),整个队列约 **2.06 GiB**——这是**缓解**,不是保证。而且 `log()` 是逐字符 push,一条日志要占约 30 个槽,8M 个槽实际只够缓冲约 28 万条日志,"几乎碰不到"要大打折扣(见轮 3 Q3)。(264 字节是按 x86-64 对齐规则算的;这台机器没有 C++ 编译器,不是真编译出来的。)
  - 顺带:轮 1 那个 `&`/`==` 优先级 bug 会让"满"判断**几乎恒假**,生产者于是几乎永远走不到阻塞分支——代价是直接覆盖没读的数据。如果"永不卡"是这么得来的,那是靠**写坏数据**换的,不是设计上的正确性。
- **"用无锁队列不是为了让写日志变快,而是让耗时绝对可预测"** —— **这半句是最值得记的话**:优化目标是**尾延迟/确定性**,不是均值。知乎文章开头讲的 P99/P999 论点是同一件事(衔接 二/#5、一/#32)。而"压到 2 ns 以内、永不卡顿"是过头的:一条日志要 push 的元素个数,乘上每个元素 264 字节,总成本显然不是 2 ns 这个量级(这点和轮 3 Q3 相关,先不展开)。

### 5. 后面怎么接

- **一/#30 wait-free 编程**(🔴):按编号顺序,到时完整讲一轮,接着这里讲 helping / bit stealing / 线性一致性。源笔记只给了 wait-free 的 sticky counter,**没有** lock-free 对照版、也没有四档梯度和 SPSC,这些由本节补上。预读稿已写:[30-wait-free编程.md](30-wait-free编程.md)(未讲、无检验,含 Python 穷举验证);[进度追踪](../进度追踪.md) 的 #30 行也已更新。
- **三/#2 实现线程安全队列**(🟡):多生产者用 CAS 抢槽位的版本(如[源笔记:857](../../trading-system-notes/chinese/03-simple-practice/02-实现线程安全队列.md:857)的 `compare_exchange_weak` 重试循环)是 lock-free 但非 wait-free 的典型形态,可以和本节并排看;按学习顺序在 一+二 之后。

---

## 与其他主题的连接

- **§5(内存模型/缓存/流水线)** —— relaxed/acquire/release 的选择、"独占权来回打"的机制,直接复用 §5 轮 2/轮 3/轮 4 的词汇表,这里是第一次在真实产品代码里落地。轮 2 的"影子副本"技巧则是 §5 轮 2/4 边界追问里预告过的 LMAX Disruptor 手法首次在真实队列类里落地([05 笔记:513](05-内存模型与缓存及流水线.md:513))。
- **#8(内联及内联汇编)** —— `index & mask_` 替代 `index % capacity`,是手写汇编表"整数除常数可改乘+移位"思路的极致版本。
- **#7(大页内存)/§6 轮 2** —— double-mmap 的"一次性构造期 `mmap`、零热路径系统调用"模式,和 `HugePageAllocator`、`M_MMAP_MAX` 是同一条线上的第三次重现;额外页表项/TLB 压力的代价直接接回 #7 的 TLB 内容。
- **#30(wait-free 编程,待讲)/#1–#4/§5/§6** —— 〔补〕把"耗时可预测"拆成三层:算法层(wait-free,#30 会完整讲)+ OS 层(绑核/隔离/实时优先级,#1–#4)+ 内存/硬件层(`mlockall` §6、`alignas(64)` §5)。持锁的后台线程 + 高优先级热路径,就是 #2 优先级反转的现成场景。
- **§5 轮 4(缓存行/L1)/轮 1 Q4(`num_elements_`)** —— 轮 3 的 `log()` 逐字符 push,一条日志写约 124 个缓存行、有用字节不到 1%,是缓存污染的现成案例;每次 push 一次 `fetch_add`,则把轮 1 Q4 的"独占权来回打"放大了 30 倍。改进版把 30 次 push 降到 5 次。
- **一/#10(spmc 共享内存队列,待讲)** —— 轮 3 Q3 的判据("被指的东西要活得比读取者久、且不变")在这里更严:元素放在跨进程共享的内存里,只能是自包含的定长数据(如 `char SecurityID[31]`,[源笔记:85](../../trading-system-notes/chinese/01-low-latency/10-spmc共享内存无锁队列应用.md:85)),不能存指针。讲 #10 时接回这一点(见附三〔接回 #10〕)。
