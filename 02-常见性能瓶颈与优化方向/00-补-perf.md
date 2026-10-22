# 00补 perf：怎么用、看哪些指标（专题）

> 状态：2026-10-22 写完，发成博客专题 [低时延交易 —— perf：怎么用、看哪些指标](https://jasper-aa64.github.io/zh/posts/low-latency-perf-profiling/)（[英文](https://jasper-aa64.github.io/posts/low-latency-perf-profiling/)），不占编号，`chapter: 2`。不是源笔记里的条目，不出检验题。用户转来写作要求，要求每个数字都出自真跑的输出。

## 实测环境

- 一台拿不到硬件计数器的 x86-64 虚拟机。
- 内核 `3.10.0-1160.119.1.el7.x86_64`，perf `3.10.0-1160.119.1.el7.x86_64.debug`，GCC 12.2.0，编译参数 `-O2 -g -fno-omit-frame-pointer`。
- 用 root 运行，`perf_event_paranoid` 是 2。
- 程序和输出只放在 `/tmp` 下新建的临时目录，跑完删掉。

## 文件

| 文件 | 内容 |
|---|---|
| [perf_demo.cpp](00-补-perf-实测/perf_demo.cpp) | 演示程序：`sum_serial` 单累加器求和（热点）、`refresh` 每轮改 256 个价格（冷函数）、`sum_4acc` 四个累加器（修法）、`check` 模式 |
| [run.sh](00-补-perf-实测/run.sh) + [out/](00-补-perf-实测/out) | 正式那次，博客里的数都出自这次：探测 → `perf stat -r 10` → record 和 report → annotate → strip 对照 → check → 修完再 stat、record、annotate → objdump |
| [extra.sh](00-补-perf-实测/extra.sh) + [extra/](00-补-perf-实测/extra) | 补测：不加 `-g` 时函数名还在、annotate 没有源码行；`--call-graph dwarf` 时调用链里有 `main` |
| [extra2.sh](00-补-perf-实测/extra2.sh) + [extra2/](00-补-perf-实测/extra2) | 补测：`-gdwarf-4` 时 BFD 报错从 9 行降到 1 行，两种情况调用链都对，所以博客没写“加 `-gdwarf-4` 能避开” |
| [first-run/](00-补-perf-实测/first-run) | 第一次，程序放在 `/tmp/perf-demo-20261022/`：report 里全是地址，annotate 报没有样本。`debug.sh`、`debug2.sh` 依次排除了 DWARF 版本、build id、编译器版本，`-vv` 把来源标成 `j`（JIT） |
| [second-run/](00-补-perf-实测/second-run) | 第二次，换到 `/tmp/demo-perf-20261022/` 后名字都出来了，但 annotate 用的是全名 `perf annotate --stdio -i perf.serial.data 'sum_serial(float const*, int)'`，报没有样本。只留了这两份输出，其余和正式那次重复 |

## 结果（博客第 4 部分）

| | 单累加器 | 四累加器 |
|---|---|---|
| `perf stat -r 10` 耗时 | 0.54486 ± 0.00755 s（1.39%） | 0.16404 ± 0.00933 s（5.69%） |
| report 的 Self | `sum_serial` 98.04%，`refresh` 0.90% | `sum_4acc` 93.17%，`refresh` 4.10% |
| annotate | 100% 记在 `addss` 后面的 `add` 上 | 73.87% 记在 `addps` 后面的 `cmp` 上，25.20% 在 `movups` 上 |
| `check` | 414822.4688 | 414822.3750（double 参考值 414822.4000） |

两次耗时之比约 3.32。± 是 10 次平均值的标准误。

## 碰到的坑（都写进了博客）

1. 路径以 `/tmp/perf-` 开头：6.11 之前的 perf 把这种文件当 JIT 的符号文件 `/tmp/perf-<pid>.map` 读（`tools/perf/util/symbol.c`，v3.10 第 774 行、v6.10 第 1802 行；修复是 b0979f008f13，从 v6.11 起）。博客 2.2。
2. annotate 的样本落在慢指令的下一条（skid）。Intel 的“串行累加”规则要单条浮点指令占 40% 以上，照字面套会漏。博客 4.5。
3. 叶子函数没建栈帧：加了 `-fno-omit-frame-pointer`，`sum_serial` 开头也没有 `push %rbp`，按帧指针回溯少了 `main` 这一层；`--call-graph dwarf` 能补回来。博客 4.4。
4. `perf annotate` 的符号名要写 report 里显示的短名 `sum_serial`；写全名 `sum_serial(float const*, int)` 会报“has no samples”（`second-run/`）。这一条只在这里记，博客没写。
5. `perf stat -r` 的 ± 是均值的标准误（`tools/perf/util/stat.c` 的 `stddev_stats()`），单次之间的差别约再乘 $\sqrt{10}$。博客 4.3。
6. 硬件事件 `<not supported>` 时 perf stat 的退出码仍是 0。博客 4.2。

## 出处核对

- 判读线：`intel/intel-performance-skills` 提交 3e33aec 的 `skills/linux-perf/SKILL.md`、`references/flow-a.md`、`flow-b.md`、`flow-c.md`、`flow-d.md`、`building-blocks.md`（MIT，见仓库的 `COPYRIGHT.md`）。
- Intel 原文有两处和内核文档、实测对不上，博客按后者写：`SKILL.md` 说 paranoid ≥ 2 时硬件事件要 root（内核文档里 2 只禁止普通用户采内核态）；说 `-g` 让 report 显示函数名（函数名来自符号表，`extra/report_nog.txt` 不加 `-g` 也有名字）。
- 其余：内核文档 `Documentation/admin-guide/sysctl/kernel.rst`；perf_event_open(2) 手册页（paranoid 各级、`PERF_COUNT_SW_CPU_CLOCK`、`precise_ip`）；perf 文档 `perf-record.txt`、`perf-list.txt`、`perf-c2c.txt`、`callchain-overhead-calculation.txt`、`jit-interface.txt`；`include/uapi/linux/perf_event.h` 的 `PERF_MEM_SNOOP_HITM`。

## 没测到的

IPC、缓存缺失率、分支失败率和 c2c 都要硬件计数器，这台虚拟机拿不到，博客第 3 部分只给了命令和判读线。等用户给一台有硬件计数器的机器，再补实测。
