# CPU 亲和性与 NUMA 架构 —— 让一个核只为你一个线程工作

> 源笔记:[`01-cpu亲和性及numa架构.md`](../../trading-system-notes/chinese/01-low-latency/01-cpu亲和性及numa架构.md)(注意源文件名用「及」)
>
> 这份是叙述版,把三轮讲解合成一篇连贯的。三轮分别是:
> **绑核与 CPU 隔离** → **超线程与频率/节能锁定** → **NUMA 架构**。
>
> 标了「补」的地方是原笔记没有、我加的背景;其余是原笔记有的,只是讲开了。

### 源笔记定位

| 内容 | 锚点 |
|---|---|
| isolcpus | [源笔记:3](../../trading-system-notes/chinese/01-low-latency/01-cpu亲和性及numa架构.md:2) |
| taskset | [源笔记:11](../../trading-system-notes/chinese/01-low-latency/01-cpu亲和性及numa架构.md:11) |
| cpusets | [源笔记:78](../../trading-system-notes/chinese/01-low-latency/01-cpu亲和性及numa架构.md:80) |
| 四者对比表 | [源笔记:128](../../trading-system-notes/chinese/01-low-latency/01-cpu亲和性及numa架构.md:129) |
| `pthread_setaffinity_np` 代码 | [源笔记:20](../../trading-system-notes/chinese/01-low-latency/01-cpu亲和性及numa架构.md:21) |
| sched vs pthread 注释 | [源笔记:65](../../trading-system-notes/chinese/01-low-latency/01-cpu亲和性及numa架构.md:66) |
| CPU Pinning 注意事项 | [源笔记:137](../../trading-system-notes/chinese/01-low-latency/01-cpu亲和性及numa架构.md:138) |
| 超线程 | [源笔记:143](../../trading-system-notes/chinese/01-low-latency/01-cpu亲和性及numa架构.md:144) |
| 关闭 CPU 节能(C-States & P-States) | [源笔记:186](../../trading-system-notes/chinese/01-low-latency/01-cpu亲和性及numa架构.md:183) |
| 系统的 NUMA 拓扑 | [源笔记:356](../../trading-system-notes/chinese/01-low-latency/01-cpu亲和性及numa架构.md:353) |
| 显式 NUMA 感知内存分配 / libnuma 表 | [源笔记:484](../../trading-system-notes/chinese/01-low-latency/01-cpu亲和性及numa架构.md:487) |

---

## 开篇:你在跟什么较劲

先把目标函数摆正,后面所有手段才有意义。

HFT 调优,敌人**不是平均延迟,是尾延迟和抖动**。假设行情剧烈波动那 100µs 里,你的策略线程被调度器抢走一次、L1/L2 被别的任务冲掉一次,这一下慢 30µs——你的报单就排在别人后面进了交易所撮合队列,成交价往你不利的方向偏一档,这就是被逆向选择(adverse selection)吃掉。所以对我们来说,**平均 2µs、偶尔蹦到 50µs,比一直稳定 5µs 更糟**。稳定可预测,比快但会抖,值钱。

这一章讲的所有东西——绑核、隔离、关超线程、锁频率、NUMA 布局——服务的是同一句话:

> **让你的热点线程,成为它那个物理核上唯一会运行的东西,而且永远如此。**

追求的是确定性,不是吞吐。想清楚这句,下面每一步都是它的推论。

---

## 一、绑核与 CPU 隔离

### 1. 为什么线程会乱跑,以及乱跑的代价

Linux 的调度器(CFS,6.6 之后是 EEVDF)默认会为了负载均衡,在核之间迁移线程。它看到 3 号核空了、8 号核挤了两个任务,就把一个任务挪到 3 号核去——从"公平地利用所有核"这个角度看,这是对的。但对你的热点线程,这一次迁移是灾难。

灾难在哪?**L1、L2、TLB 都是每个物理核私有的。**你的线程在 7 号核上跑了一阵,订单簿的热数据、策略代码的指令、地址翻译条目,全都躺在 7 号核的 L1/L2 里。调度器把它挪到 12 号核——12 号核的 L1/L2 里没有这些东西,于是接下来几万条指令,每一次访存都是 miss:要么从 L3 拉(约 40 个周期),要么从内存拉(约 200–300 个周期)。实测下来,一次迁移会带来**几十微秒的"退化窗口"**,在这个窗口里你的线程一直在重新预热缓存,干正事的速度只有平时的一小半。如果迁移还跨了 socket(从 0 号 socket 的核挪到 1 号 socket 的核),那更糟——数据现在变成了"远程内存",每次访问长期加价约 50%,这个后面第三节讲。

绑核(pinning / affinity)就是对调度器说:**这个线程只准在这个核上跑,不许挪。**它把上面的迁移代价直接归零。

但注意一件事:**绑核只解决"线程被挪走"这一个问题,不解决"核上还有别人"这个问题。**你把线程钉在 3 号核上,3 号核上原本的周期性时钟中断、软中断、内核线程、别的用户进程……一个都没少,它们照样会周期性地抢占你。所以绑核是**必要但不充分**,它得配一整套隔离手段。

### 2. 隔离栈:一层一层把噪音赶走

想象 3 号核在"裸机、什么都不配"的状态下,一秒钟之内会被打断多少次:你的热点线程要跑,别的用户线程也可能被调度上来,内核线程(`kworker`、`ksoftirqd`)要干活,1000Hz 的周期时钟中断雷打不动地敲,网卡来一个包就是一次硬中断,硬中断又触发软中断,RCU 子系统的回调也要找个核处理……

隔离,就是一层一层地把这些噪音源单独关掉。每一层对应一个具体的噪音:

**`isolcpus=3`(内核启动参数)** —— 把 3 号核从调度器的"通用分配池"里摘出去。以后调度器不会再主动把任务往 3 号核上放,被唤醒的任务也不会默认落到这里。历史上它还会把这个核踢出"调度域(sched domain)",让它不参与负载均衡计算。

这里有一个**几乎人人踩过的误解**要说清楚:`isolcpus` 不是一道权限墙,它**不阻止你显式地把线程 pin 上去**。它只是让"自动的、默认的"调度行为绕开这个核。标准用法恰恰是:先用 `isolcpus` 把 3–7 号核清空(没人会自动来),然后你的程序用亲和性接口,把热点线程一个一个"点名"放上去,做到一个线程独占一个核。所以自检里那道题——`isolcpus=2-7` 之后 `taskset -c 4 ./app` 能不能上 4 号核——答案是**能**,而且这就是它该有的用法。

**`nohz_full=3`(内核启动参数)** —— 关掉周期时钟中断。普通情况下内核每个核每秒被时钟中断敲 100 到 1000 次,用来做时间片记账、统计之类的事。`nohz_full` 让这个核在**"核上恰好只有 1 个可运行任务"时**停掉这个 tick。

划重点:**恰好 1 个**。一旦你往这个核上放了第二个可运行线程,内核就没法用一个 tick 同时给两个线程记账,tick 立刻回来。所以 `nohz_full` 和"一个线程独占一个核"是死绑在一起的——这也是为什么前面说 `isolcpus` 清场之后要**一线程一核**,不是随便塞。

**`rcu_nocbs=3`(内核启动参数)** —— RCU(read-copy-update)是内核里大量使用的一种同步机制,它的"回收"阶段有回调要在某个核上执行。`rcu_nocbs` 把 3 号核的这些回调,甩给别的核上专门的 `rcuop/N` 线程去做。3 号核就不用为 RCU 停下来了。顺带一提,`nohz_full` 会自动帮你把它覆盖的核加进 `rcu_nocbs`,所以这俩经常一起出现。

**`irqaffinity=0,1`(内核启动参数)+ `/proc/irq/N/smp_affinity`** —— 设备中断(网卡、硬盘)默认往哪些核送。把默认掩码设成只有 0、1 号核,再对具体的 IRQ 号做精细绑定,就能保证网卡来包时打的是 0/1 号核,不打你的热点核。这块是第 3 篇笔记(中断绑定)的主题,这里只需要知道它是隔离栈的一环。

**还有一类东西你关不掉,只能"饿死"。** 像 `ksoftirqd/3`、`kworker/3`、`migration/3` 这种带核号的**per-CPU 内核线程**,是每个核天生自带的,`isolcpus` 删不掉它们。但它们是"有活才醒"的:`ksoftirqd/3` 处理的是 3 号核上的软中断,你把网卡中断绑走了,3 号核就不再产生网络软中断,`ksoftirqd/3` 自然一直睡。`kworker/3` 处理的是派发到 3 号核的工作项,你把 RCU 回调挪走了、把中断挪走了,派发到 3 号核的活儿趋近于零,`kworker/3` 也就不醒。**思路不是"杀掉线程",是"掐掉它的活儿来源,让它一直睡"。**

所有非隔离核(通常就是 0 号、有时加 1 号)在这套布局里有个名字叫**管家核(housekeeping core)**——你不是消灭了系统的噪音,你是把噪音**全都赶到管家核上集中处理**,换来热点核的绝对安静。

**最后,还有一点不可消除的残渣。** 即使上面全配好,`nohz_full` 的核上还会有约 1Hz 的一个残余定时中断;别的进程做地址空间变更(`munmap`/`mprotect`)时,如果和你共享地址空间,会给你发 TLB shootdown 的核间中断(IPI);偶发的调度 IPI;NMI watchdog(可以用 `nmi_watchdog=0` 关);还有 SMI——这个是固件层面的系统管理中断,操作系统完全看不见,一次可能几十到几百微秒,只能在 BIOS 里想办法。这些东西的量级和频率,是你用 `cyclictest` / `oslat` 测出来的"地板",再往下就得换硬件、调 BIOS 了。

### 3. 四种手段的区别:isolcpus / cpuset / taskset / 亲和性 syscall

[源笔记的对比表](../../trading-system-notes/chinese/01-low-latency/01-cpu亲和性及numa架构.md:129)列了四个,但表格看完容易记不住谁是谁。用一条线索串:**它们分别工作在哪个层级,以及"隔离强度"到底指什么。**

`isolcpus` 是**内核级**的,开机参数,一旦生效整个系统都认。它的隔离是"强"的——因为它真的把核从调度池里拿走了。代价是静态,改一次要重启。

`cpuset`(cgroup 的一个子系统)也是**内核级**、也是"强"隔离,但它是**动态**的:你通过一个虚拟文件系统接口,运行时就能划出一个 CPU + 内存节点的池子,把进程丢进去或拿出来。它比 `isolcpus` 多一个能力——**能一起划内存节点**,这在 NUMA 场景很有用。内核文档现在其实推荐用 `cpuset` + `nohz_full` 取代 `isolcpus`,但生产环境里 `isolcpus` 还是大量在用,就因为它简单、可靠、开机即生效、不会被误配。

`taskset` 是**进程级**的命令行工具,`pthread_setaffinity_np` / `sched_setaffinity` 是**线程级**的接口。这两个的隔离是"**弱**"的,而"弱"的准确含义是:它们只约束"**我**这个进程/线程只能在这些核上跑",**它们不阻止别的线程也跑到这些核上来**。你用 `taskset -c 3 ./app` 把自己限制在 3 号核,系统里别的任务照样可以被调度到 3 号核,把你挤一边。所以真隔离必须靠 `isolcpus` 或 `cpuset` 先把核清空,`taskset` / 亲和性接口只负责"点名入座"这一步。

`sched_setaffinity` 和 `pthread_setaffinity_np` 的区别小,但面试爱问:前者是**原始系统调用**,用 TID 标识线程(传 `0` 表示"我自己");后者是 **glibc 的封装**,内部还是调前者,用 `pthread_t` 句柄标识线程,名字里的 `_np` 是 "non-portable"(glibc 扩展,不是 POSIX 标准)。实务上手里有 `pthread_t` 就用后者,要绑当前线程直接 `sched_setaffinity(0, ...)`。

### 4. 读代码:一段"绑核并启动线程"的封装,和它的四个坑

源笔记给了一段封装([源笔记:20](../../trading-system-notes/chinese/01-low-latency/01-cpu亲和性及numa架构.md:21) 附近),精简后是这样:

```cpp
inline auto setThreadCore(int core_id) noexcept {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);              // 掩码清零
    CPU_SET(core_id, &cpuset);      // 只置 core_id 这一位
    return (pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset) == 0);
}
```

这半段没问题:`cpu_set_t` 是个位图(默认 1024 位),`CPU_ZERO` / `CPU_SET` 是操作它的宏;对 `pthread_self()` 调用就是"把我自己钉到 core_id";`noexcept` + 返回 `bool` 而不是抛异常,是热路径代码的风格。

问题在外层这段:

```cpp
template<typename T, typename... A>
inline auto createAndStartThread(int core_id, const std::string &name, T &&func, A &&... args) noexcept {
    auto t = new std::thread([&]() {
        if (core_id >= 0 && !setThreadCore(core_id)) { /* 打印错误; exit(EXIT_FAILURE) */ }
        std::forward<T>(func)((std::forward<A>(args))...);
    });
    std::this_thread::sleep_for(1s);   // 赌"亲和性已生效 + 线程已迁移到位"
    return t;
}
```

面试如果给你这段问"看看有什么问题",要能挑出四点:

**坑一,悬垂引用,这是真 bug。** lambda 用 `[&]` 按引用捕获了 `core_id`、`name`、`args...`,而这个 lambda 可能在 `createAndStartThread` 已经返回之后才真正开始执行。那时候函数栈帧没了,这些引用全部悬垂,lambda 里的 `std::forward` 转发的是垃圾。正确做法是按值捕获,或者用 `std::tuple` 把参数打包进去再 `std::apply` 展开。

**坑二,`sleep_for(1s)` 是脏 hack。** 它想解决的是一个竞态:主线程 `return t` 的时候,子线程的 `func` 可能已经开始跑了,**但 `setThreadCore` 还没执行完或还没生效**。作者用"睡 1 秒,赌这 1 秒内亲和性设好、调度器也迁移到位了"来掩盖它。正确做法是握手:用 `std::latch ready{1}`,子线程设完亲和性后 `ready.count_down()`,主线程 `ready.wait()` 之后再返回。顺带,睡 1 秒还白白拖慢了启动。

**坑三,`new std::thread` 裸指针,不 delete,泄漏。** 而且把一个 `joinable` 的 thread 指针返回给调用方,谁负责 `join` / `detach` 没有约定;`std::thread` 析构时如果还 joinable,会直接 `std::terminate` 掀桌子。

**坑四,迁移时机不对。** 线程是先在"创建者所在的核"上出生、开始跑,然后才在入口处调 `setThreadCore` 迁到目标核。这第一次迁移的缓存冷启动,你还是吃了。更彻底的做法是 `pthread_attr_setaffinity_np`——设好线程属性,让线程**一出生就在目标核上**,连第一次迁移都省掉。

### 5. 一个具体的布局建议:避开 0 号核

0 号核是 Linux 的 boot CPU,天然是各种管家活动的吸附点:RCU 的 grace-period 内核线程、默认的 workqueue、一部分没法迁移的 IRQ、NMI watchdog、计时/时钟维护、`kworker`……即使你把别的核都 `isolcpus` 了,这些东西还是集中在 0 号核(有时溢出到 1 号)。

所以标准布局是:**管家活动关在 0 号(和 1 号)核,热点线程放别处**;在双路机器上,热点核还要放在**网卡所在的那个 NUMA 节点**上(再讲究一点,让 CPU、内存、网卡 PCIe 三者都在同一个节点)。用 `lscpu -e=CPU,CORE,SOCKET` 看清楚逻辑核、物理核、socket 的对应关系,再挑核。

---

## 二、超线程与频率:核自己会给你使绊子

上一节是把"别人"从你的核上赶走。这一节的问题不一样:**核本身**有几个特性,会让你这个独占了整核的线程,延迟照样抖。

### 6. 超线程:一个物理核假装成两个

超线程(Intel 叫 HT,通用叫 SMT)让一个物理核对外呈现成两个逻辑核。操作系统看到的是 `CPU 0` 和 `CPU 48`,但它俩其实是**同一个物理核**的两套寄存器状态。当一条流水线因为等内存而空转时,核可以切去执行另一套状态的指令,把空转填上——对吞吐是好事,数据中心典型能多榨 15%–30%。

但这两个逻辑核**共享几乎所有真正重要的东西**:执行单元(ALU、FPU)、L1、L2、store buffer、TLB、分支预测器。于是问题来了:你把热点线程 pin 到 `CPU 0`,自以为独占了一个核;结果 `CPU 48` 上被调度了另一个任务,它一跑 AVX,浮点单元被它占着,你的线程要用就得等;它访存的数据把你俩共享的 L1 冲掉了,你的热数据得重新加载。你"独占核"的前提被 sibling 破坏了。

**怎么确认开没开、哪两个核是 sibling:**

```bash
lscpu -e                                    # 看 CORE 列,同号即 sibling
cat /sys/devices/system/cpu/cpu0/topology/thread_siblings_list   # 直接列出 CPU0 的兄弟
cat /sys/devices/system/cpu/smt/active      # 1 = SMT 开着
```

`lscpu -e` 输出里如果 `CPU 0` 和 `CPU 48` 的 `CORE` 号相同,它俩就是一个物理核的两个线程。

**怎么关:**最彻底是 BIOS 里全局关 HT,或者内核启动加 `nosmt`。不想全局关,可以逐个把 sibling 下线:

```bash
echo 0 > /sys/devices/system/cpu/cpu48/online   # 让 CPU48 消失,CPU0 独享整个物理核
```

验证方法:关之前跑一轮 `cyclictest`,关之后再跑一轮,对比 max 和尖峰个数——sibling 的干扰关掉之后,尾部会明显收窄。

### 7. C-states:核在打盹,你得先把它拍醒

C-state 是 CPU 的**空闲省电状态**。C0 是正在干活;C1 / C1E 是轻度 halt;C3、C6 是深度睡眠——越深,省的电越多,但**唤醒要的时间也越长**。C6 会把 L1、L2 都刷掉、把核心电压降下来,从 C6 醒回 C0 大概要 30 到 100 微秒。

这对我们意味着什么:如果你的线程是"来一个行情包处理一下,然后短暂空闲"这种节奏,空闲那一下核可能就滑进了 C6;下一个包到的时候,前 30–100µs 全花在"把核唤醒"上了。这是一种非常隐蔽的尾延迟来源,因为它只在"刚空闲过"的时候发作。

**三层修法,从粗到细:**

1. **`idle=poll`(内核启动参数)** —— 最粗暴:核空闲时不进任何 C-state,就空转轮询。延迟最低,但功耗和发热拉满,整机风扇狂转,一般只在真正极致的场景用。
2. **限制最深 C-state** —— `processor.max_cstate=1` + `intel_idle.max_cstate=1`(启动参数),或运行时对每个状态 `echo 1 > /sys/devices/system/cpu/cpuN/cpuidle/stateX/disable`。允许浅睡(C1,唤醒可忽略),禁止深睡。
3. **`/dev/cpu_dma_latency`(PM QoS)** —— 打开这个设备文件,写一个 32 位整数 `0` 进去,**并且保持文件描述符不关闭**。这相当于告诉内核"整个系统能容忍的唤醒延迟是 0µs",内核据此就不会让核进深 C-state。fd 一关,约束就解除。这是程序里可以做的、比启动参数灵活的办法。

### 8. P-states:核在变速,你得把它焊死

P-state 管的是**频率和电压**。这里有两个东西在动:

**一个是 governor(调频策略)。**默认的 `powersave` governor 会根据负载慢慢升频——你的线程突然来活,它不是立刻满频,是"看到负载上来了,升一档,再看看,再升一档",这个爬坡过程你的前几个包就是在低频上跑完的。改成 `performance` governor,直接焊在最高的非睿频频率上,不爬坡。

**另一个是 Turbo(睿频 / opportunistic frequency)。**这是"如果散热和功耗有余量,就超过标称频率跑"。听起来是好事,但它的问题是**频率变成了一个变量**:取决于当前有几个核在忙、机箱多热、你的指令里有没有 AVX-512(跑重 AVX 会触发降频"许可")。频率一变,你的每一段代码的执行时间就跟着变,这就是抖动。所以 HFT 的做法通常是**关掉 Turbo,让核稳定跑在一个固定的基频上**——牺牲峰值速度,换每一次执行时间都一样。

```bash
cpupower frequency-set -g performance          # governor 焊到 performance
cpupower frequency-set -d 3.5GHz -u 3.5GHz      # 下限=上限,频率钉死
echo 1 > /sys/devices/system/cpu/intel_pstate/no_turbo   # 关睿频
# 想完全手动控制,启动加 intel_pstate=disable,回退到 acpi-cpufreq
```

### 9. 这一节收尾

超线程、C-state、P-state,三件事的共同点是:它们都是**为"平均效率 / 省电 / 峰值吞吐"设计的机制,而这些目标和"每一次执行时间都一样"是冲突的**。热点核的处理方式统一是——**满血冻结**:关掉 sibling、禁止深睡、锁死频率。你主动放弃这个核的一部分能力和全部弹性,换它绝对可预测。

---

## 三、NUMA:内存不是均匀的

前两节把 CPU 这边收拾干净了。第三节换一个维度:**内存**。在多路服务器上,"访问内存"这个动作的耗时,取决于你访问的是哪一块内存。

### 10. 两个 socket,一条 UPI

一台双路服务器,物理上是两颗 CPU,每颗 CPU 直接连着一部分内存条。CPU 0 访问"挂在自己身上"的内存,叫**本地访问**,大约 90ns。CPU 0 要访问"挂在 CPU 1 身上"的内存,请求得先走 CPU 之间的互联总线(Intel 叫 UPI,AMD 叫 Infinity Fabric)绕到 CPU 1,再由 CPU 1 的内存控制器去取,回来再绕一趟——**远程访问**,大约 140ns。

`numactl --hardware` 能看到这个,它会打印一个 `node distances` 矩阵:本地是 `10`,远程是 `21`,比值 2.1 就是那个延迟倍数的近似。

关键在于:这个差价**不是偶尔发作的尖峰,是每一次远程访问都固定要付的税**。如果你的热点线程跑在 CPU 0,而它的订单簿数据不小心分配到了 node 1,那么它整个生命周期里每一次碰这个订单簿,都慢那 50ns。这不是抖动,是**基线整体抬高**。(这也是自检第 2 题的答案:NUMA 是拓扑属性、是持续的、是乘性的基线偏移;C-state 是状态切换成本、是事件触发的、是离散的尖峰——两者形状完全不同。)

<!-- ┌─────────────────────────────────────────────────────────────────┐ -->
<!-- 下面这张 SVG 是核隔离 + NUMA 布局的拓扑图,浅色/深色主题自适应 -->

<svg viewBox="0 0 760 470" xmlns="http://www.w3.org/2000/svg" role="img" aria-label="双路服务器的核隔离与 NUMA 布局:管家核集中噪音,热点核独占,网卡中断打在管家核,跨 socket 访问经 UPI 加价" style="max-width:100%;height:auto;font-family:ui-sans-serif,system-ui,'Segoe UI',sans-serif">
  <style>
    .bg    { fill: #fbfaf7; }
    .panel { fill: #ffffff; stroke: #d9d4c7; stroke-width: 1.5; }
    .ink   { fill: #1c1b18; }
    .muted { fill: #6b6558; }
    .cLine { stroke: #b3ab98; stroke-width: 1.5; }
    .core       { fill: #f1efe8; stroke: #c8c1ad; stroke-width: 1.2; }
    .coreHouse  { fill: #f6ddd6; stroke: #c98a76; stroke-width: 1.2; }
    .coreHot    { fill: #dcecc6; stroke: #6f8f3f; stroke-width: 2; }
    .coreLabel  { fill: #3a372f; font-size: 11px; }
    .dram  { fill: #eef1f4; stroke: #b9c2cc; stroke-width: 1.2; }
    .nic   { fill: #e7e2f0; stroke: #9a8cc0; stroke-width: 1.2; }
    .upi   { stroke: #8a8474; stroke-width: 2.5; stroke-dasharray: 1 0; }
    .irq   { stroke: #c15b3f; stroke-width: 1.8; marker-end: url(#ah); }
    .title { fill: #1c1b18; font-size: 13px; font-weight: 700; }
    .cap   { fill: #6b6558; font-size: 10.5px; }
    @media (prefers-color-scheme: dark) {
      .bg    { fill: #17161b; }
      .panel { fill: #201f26; stroke: #3a3945; }
      .ink   { fill: #e9e7ef; }
      .muted { fill: #a19caf; }
      .cLine { stroke: #55525f; }
      .core       { fill: #2a2933; stroke: #47454f; }
      .coreHouse  { fill: #4a2f2c; stroke: #8f5a4c; }
      .coreHot    { fill: #33421f; stroke: #8fb257; }
      .coreLabel  { fill: #d7d3c8; }
      .dram  { fill: #23262c; stroke: #3f4650; }
      .nic   { fill: #2e2940; stroke: #6a5c95; }
      .upi   { stroke: #9a9384; }
      .irq   { stroke: #e0795b; }
      .title { fill: #e9e7ef; }
      .cap   { fill: #a19caf; }
    }
  </style>
  <defs>
    <marker id="ah" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse">
      <path d="M0 0L10 5L0 10z" fill="#c15b3f"/>
    </marker>
  </defs>
  <rect class="bg" x="0" y="0" width="760" height="470" rx="10"/>

  <text class="title" x="24" y="30">一台双路服务器:噪音全赶到管家核,热点核独占</text>

  <!-- SOCKET 0 -->
  <rect class="panel" x="24" y="48" width="330" height="196" rx="8"/>
  <text class="muted" x="40" y="70" font-size="12" font-weight="700">SOCKET 0  ·  NUMA node 0</text>

  <rect class="coreHouse" x="40"  y="82" width="70" height="42" rx="5"/>
  <text class="coreLabel" x="49" y="99">core 0</text><text class="cap" x="49" y="115">管家</text>
  <rect class="coreHouse" x="118" y="82" width="70" height="42" rx="5"/>
  <text class="coreLabel" x="127" y="99">core 1</text><text class="cap" x="127" y="115">管家</text>
  <rect class="core" x="196" y="82" width="66" height="42" rx="5"/>
  <text class="coreLabel" x="205" y="99">core 2</text>
  <rect class="coreHot" x="270" y="82" width="76" height="42" rx="5"/>
  <text class="coreLabel" x="279" y="99" font-weight="700">core 3</text><text class="cap" x="279" y="115">热点线程</text>

  <rect class="core" x="40"  y="132" width="66" height="38" rx="5"/><text class="coreLabel" x="49" y="155">core 4</text>
  <rect class="core" x="114" y="132" width="66" height="38" rx="5"/><text class="coreLabel" x="123" y="155">core 5</text>
  <rect class="core" x="188" y="132" width="66" height="38" rx="5"/><text class="coreLabel" x="197" y="155">core 6</text>
  <rect class="core" x="262" y="132" width="84" height="38" rx="5"/><text class="coreLabel" x="271" y="155">core 7</text>

  <rect class="dram" x="40" y="182" width="306" height="46" rx="6"/>
  <text class="ink" x="52" y="201" font-size="11.5" font-weight="700">本地 DRAM (node 0)</text>
  <text class="cap" x="52" y="218">core 3 访问这里 ≈ 90ns(本地)</text>

  <!-- SOCKET 1 -->
  <rect class="panel" x="406" y="48" width="330" height="196" rx="8"/>
  <text class="muted" x="422" y="70" font-size="12" font-weight="700">SOCKET 1  ·  NUMA node 1</text>
  <rect class="core" x="422" y="82" width="66" height="42" rx="5"/><text class="coreLabel" x="431" y="99">core 8</text>
  <rect class="core" x="496" y="82" width="66" height="42" rx="5"/><text class="coreLabel" x="505" y="99">core 9</text>
  <rect class="core" x="570" y="82" width="70" height="42" rx="5"/><text class="coreLabel" x="579" y="99">core 10</text>
  <rect class="core" x="648" y="82" width="72" height="42" rx="5"/><text class="coreLabel" x="657" y="99">core 11</text>
  <rect class="core" x="422" y="132" width="66" height="38" rx="5"/><text class="coreLabel" x="431" y="155">core 12</text>
  <rect class="core" x="496" y="132" width="66" height="38" rx="5"/><text class="coreLabel" x="505" y="155">core 13</text>
  <rect class="core" x="570" y="132" width="70" height="38" rx="5"/><text class="coreLabel" x="579" y="155">core 14</text>
  <rect class="core" x="648" y="132" width="72" height="38" rx="5"/><text class="coreLabel" x="657" y="155">core 15</text>
  <rect class="dram" x="422" y="182" width="306" height="46" rx="6"/>
  <text class="ink" x="434" y="201" font-size="11.5" font-weight="700">本地 DRAM (node 1)</text>
  <text class="cap" x="434" y="218">core 3 访问这里 ≈ 140ns(经 UPI 绕一圈)</text>

  <!-- UPI link -->
  <line class="upi" x1="354" y1="146" x2="406" y2="146"/>
  <text class="muted" x="360" y="138" font-size="10.5" font-weight="700">UPI</text>

  <!-- NIC -->
  <rect class="nic" x="24" y="300" width="150" height="60" rx="8"/>
  <text class="ink" x="40" y="324" font-size="12" font-weight="700">网卡 NIC</text>
  <text class="cap" x="40" y="342">PCIe 挂在 socket 0</text>
  <text class="cap" x="40" y="356">→ 热点核放 node 0</text>

  <!-- IRQ arrows to housekeeping cores -->
  <path class="irq" d="M120 300 C 120 260, 90 180, 75 126"/>
  <path class="irq" d="M150 300 C 175 250, 165 180, 153 126"/>
  <text class="cap" x="120" y="285" fill="#c15b3f">硬中断 → 管家核(irqaffinity=0,1)</text>

  <!-- core3 callout -->
  <rect class="panel" x="250" y="288" width="486" height="150" rx="8"/>
  <text class="title" x="266" y="312" font-size="12.5">core 3 上到底发生了什么</text>
  <text class="cap" x="266" y="334" font-size="11">isolcpus=3   调度器不再往这里放任何任务(但你可以显式 pin)</text>
  <text class="cap" x="266" y="352" font-size="11">nohz_full=3  核上只剩这一个线程 → 1000Hz 周期 tick 停掉</text>
  <text class="cap" x="266" y="370" font-size="11">rcu_nocbs=3  RCU 回调甩给别的核的 rcuop 线程</text>
  <text class="cap" x="266" y="388" font-size="11">irqaffinity   网卡硬中断打在 core 0/1,不打这里</text>
  <text class="cap" x="266" y="406" font-size="11">pin + attr    线程一出生就在 core 3,连第一次迁移都不发生</text>
  <text class="cap" x="266" y="426" font-size="11" font-weight="700">剩下:约 1Hz 残余 tick、偶发 IPI、SMI —— 这是地板</text>
</svg>

<!-- 图注 -->
> 图:管家核(core 0/1)吸走网卡中断和内核杂活;core 3 被三个启动参数清空后 pin 上热点线程;跨 socket 访问 node 1 的内存要经 UPI,每次约 +50ns。

### 11. first-touch:page 跟着"第一个写它的核"走,不是跟着 malloc 走

这是 NUMA 里最反直觉、也最容易踩的一条。

你 `malloc(1GB)` 的时候,Linux **并没有真的给你 1GB 物理内存**,它只是在你的地址空间里划了一段虚拟地址。真正的物理页,是在你**第一次写入**某一页的时候才分配的(缺页中断触发)。而分配时,内核用的策略默认是 **first-touch**:这一页物理内存,放在**执行"第一次写入"的那个核所在的 NUMA 节点**上。

后果:如果你在主线程里 `malloc` 一大块、顺便 `memset` 清零,那么这块内存**全部落在主线程当时所在的节点**。之后你把工作线程绑到另一个节点的核上去用这块内存——每一次访问都是远程的。正确的模式是**谁用谁初始化**:分配之后不要碰,让最终使用它的那个工作线程,在它自己的核上完成第一次写入,page 就落在对的节点上了。

### 12. membind / preferred / interleave:三种绑定策略

`numactl` 和 `libnuma`([源笔记:484](../../trading-system-notes/chinese/01-low-latency/01-cpu亲和性及numa架构.md:487))提供三种内存绑定策略,区别在"节点不够用时怎么办":

- **`--membind=0`(严格绑定)** —— 只从 node 0 分配,**node 0 满了就失败 / OOM,哪怕 node 1 还有一大把空闲内存也不用**。而且在真正 OOM 之前,内核会先疯狂跑 `kswapd` 去回收 node 0 的内存,这个回收过程本身就是一大波抖动。所以用严格绑定,你必须自己盯着目标节点的空闲内存,并且扩容要对称加(两个节点一起加),**而不是**遇到问题就松成 `--preferred`——那样等于放弃了 NUMA 局部性保证。(这就是自检第 1 题。)
- **`--preferred=0`(软偏好)** —— 优先从 node 0 分配,不够就退到别的节点。不会 OOM,但你也失去了"数据一定在本地"的保证。
- **`--interleave=all`(交织)** —— 页面轮流分配到各个节点。这个不是为延迟设计的,是为**带宽**:一个要横扫大量内存的分析型任务,交织能把内存带宽压力摊到所有内存控制器上。HFT 热路径一般不用。

`libnuma` 里对应的接口是 `numa_alloc_onnode()`、`numa_run_on_node()`、`numa_set_localalloc()`;底层系统调用是 `mbind(2)`、`set_mempolicy(2)`、`move_pages(2)`。诊断用 `numastat -p <pid>`,看每个节点分了多少、`numa_miss` / `numa_foreign` 这两个计数器涨不涨(涨就说明有远程分配发生)。

### 13. "NUMA 之前":最好的 NUMA 优化是让问题不出现

前面这些都是"数据已经共享、已经要跨线程"之后的补救。真正低延迟的系统,第一选择是让 NUMA 这个问题**根本不出现**:

- 热路径**单线程 + 绑核**,数据私有——没有共享,就没有跨节点的问题。
- 需要多个工作线程时,**share-nothing**:每个线程一份自己的状态,绑在自己节点的核上,用自己节点的内存,线程之间只通过消息传递(无锁队列)交换必要的东西。
- 这样每个线程都是"本地访问",NUMA 距离矩阵里那个 `21` 你永远碰不到。

这个思路和后面几篇的主题是连着的:share-nothing 的"不共享"要靠 §6 内存池(每线程一个池)、§9 §10 的无锁队列(替代共享锁)来落地。

---

## 附一:自检题与你的答案

### 轮 1

**Q1. 绑了核仍有约 20µs 的周期性毛刺,给 3 个原因 + 排查 + 解决。**

| 原因 | 排查 | 解决 |
|---|---|---|
| 核没被 `isolcpus`/`cpuset` 清场,别的线程也被调度上来 | `ps -eLo psr,comm`、`perf sched`、`cat /proc/sched_debug` | grub 加 `isolcpus`+`nohz_full`+`rcu_nocbs`,或 cpuset 独占 |
| 周期 tick 没关(没配 `nohz_full`,或核上有第 2 个可运行线程) | `cat /proc/interrupts` 看 `LOC` 在涨 | 配 `nohz_full` 且严格一线程一核 |
| 绑到了 HT sibling / 0 号核 / 远端 NUMA 节点 | `lscpu -e`、`numactl --hardware` | 物理核独占、避开 0/1、放网卡所在节点 |
| 网卡硬/软中断打在热点核 | `cat /proc/interrupts` 按核看、`mpstat -P ALL 1` 看 `%irq`/`%soft` | `irqaffinity=` + `/proc/irq/N/smp_affinity` 绑走 |
| 缺页 / TLB shootdown / 内存被换出 | `/usr/bin/time -v` 看 page faults、`perf stat -e tlb:tlb_flush` | `mlockall` + 预触碰 + 大页 |

**Q2. `sleep_for(1s)` 遮盖了哪个竞态?**

主线程 `return t` 之后,子线程的 `func` 可能已经开始跑,**但 `setThreadCore` 还没执行完或还没生效**。sleep 是赌这 1 秒内亲和性设好、调度器迁移到位。设失败又不 `exit` 的话,线程会在错误的核上把热数据/指令/TLB 加载一遍,等真迁走时全部作废重来。正确做法:`std::latch ready{1}`,子线程设完亲和性 `count_down`,主线程 `wait` 再返回;或者 `pthread_attr_setaffinity_np` 让线程出生即在目标核。

**Q3. `isolcpus=2-7` 后 `taskset -c 4 ./myapp` 能不能上 4 号核?**

**能。**`isolcpus` 只是把这些核从调度器的自动负载均衡 / 默认放置里摘掉,它不是权限屏障,不阻止进程用 `taskset` / `sched_setaffinity` 显式点名。标准用法就是 `isolcpus` 先清场 → 亲和性接口把热点线程点名放上去 → 一线程一核独占。

### 轮 2

**Q. 怎么确认超线程开着?怎么定位 sibling?关掉之后怎么验证有效?**

你的答:用指令确认——`lscpu -e` 看 `CORE` 列,或直接读 `thread_siblings_list`;要么 BIOS 关 HT,要么逐个 `echo 0 > .../cpuN/online` 选择性关;关前关后各跑一轮看尖峰有没有收窄。

补:`cat /sys/devices/system/cpu/smt/active` 返回 1 就是 SMT 开着。选择性下线适合"我只需要清净几个热点核、别的核还想要 HT 吞吐"的场景。

### 轮 3

**Q1. 严格 `--membind=0`,node 0 满了会怎样?为什么不能松成 `--preferred`?**

你的答:会 OOM,哪怕 node 1 还有空闲——因为严格绑定只认 node 0。OOM 之前 `kswapd` 疯狂回收 node 0,这段回收就是抖动。修法是盯着 node 0 的空闲内存 + 对称扩容,不是松绑成 `--preferred`(那等于放弃 NUMA 局部性)。

**Q2. NUMA 远程访问的延迟,和 C-state 唤醒的延迟,形状上有什么本质区别?**

你的答:NUMA 是拓扑属性 = 持久的 = 乘性的基线偏移(每次远程访问都固定加价);C-state 是状态切换成本 = 事件触发的 = 离散的尖峰(只在刚空闲过、核睡下去了才发作)。两者在延迟直方图上一个是整体右移,一个是长尾上多出几个孤立的高点。

---

## 附二:面试快问快答

> 这一节是压缩过的备查,不是学习材料。真正理解靠上面的正文。

| 问 | 答法骨架 |
|---|---|
| 为什么要绑核? | 缓存/TLB 热度 + NUMA 局部性 + 去调度抖动 + 确定性;强调**为尾延迟不是吞吐** |
| `isolcpus` vs `cpuset`? | 静态启动 vs 动态 cgroup;`isolcpus` 还影响调度域;文档现推荐 `cpuset`+`nohz_full`,生产仍大量用 `isolcpus` |
| 被 `isolcpus` 隔离的核上还剩什么? | tick(除非 `nohz_full`)、RCU 回调(除非 `rcu_nocbs`)、IRQ(除非绑走)、per-CPU kthread、TLB shootdown IPI、偶发调度 IPI、SMI → 引出"怎么测隔离干净"(§32)和"系统静默"(§4) |
| `sched_setaffinity` vs `pthread_setaffinity_np`? | 系统调用 vs glibc 封装;TID(传 0 = 自己)vs `pthread_t`;`_np` = 非 POSIX |
| 绑了核就够了吗? | 不够。完整栈:`isolcpus` + `nohz_full` + `rcu_nocbs` + IRQ 绑走 + `mlockall` + 大页 + busy-poll + 关 HT + 锁频 + 限 C-state |
| 绑核后延迟反而偶发变高? | 绑到了 sibling / 0 号核 / 远端节点;或核没清场别的线程也来了;或 `nohz_full` 核上放了第二个线程,tick 回来了 |
| first-touch 是什么? | 物理页放在"第一次写它的核"所在节点,不是 `malloc` 调用者的节点;所以要"谁用谁初始化" |
| `--membind` vs `--preferred`? | 严格(满了 OOM)vs 软(满了退让);热路径要局部性用 membind + 自己盯容量,不要图省事用 preferred |
| HT 两个 sibling 共享什么? | 执行单元、L1、L2、store buffer、TLB、分支预测器;基本等于"共享一个核的一切,只有寄存器状态是两份" |
| 为什么关 Turbo? | Turbo 让频率变成变量(取决于活跃核数、温度、AVX 许可),频率一变执行时间就抖;锁死基频换确定性 |

---

## 与其他主题的连接

- **§3 中断绑定** —— `irqaffinity` / `/proc/irq/N/smp_affinity` 的展开,以及 softirq 归属那个坑。
- **§4 系统静默配置** —— 把这一篇的隔离栈整理成一份可执行的开机清单。
- **§5 内存模型与缓存** —— 这一篇讲"数据在哪个节点",§5 讲"数据在哪一级缓存、缓存行怎么被 ping-pong"。
- **§6 内存池** —— share-nothing 里"每线程私有内存"靠它落地。
- **§32 延迟测量** —— `cyclictest` / `oslat` / `perf`,验证上面每一步到底有没有效果。
