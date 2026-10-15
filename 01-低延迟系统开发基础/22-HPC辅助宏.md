# 22 HPC 辅助宏：每个宏都替编译器做了一个决定

> 源笔记：[`22-hpc辅助宏.md`](../../trading-system-notes/chinese/01-low-latency/22-hpc辅助宏.md)
>
> **轻讲版**（2026-10-15）：这条是 ⚪ 优先级，一遍讲完，不出检验题。
> 博客：和 #19、#20 合成一篇，[#13 类型双关、函数调用与编译提示](https://jasper-aa64.github.io/zh/posts/type-punning-calls-compiler-hints/) 第 3 节（轻讲）。
> 验证程序：[22-hpc-macro-check.cpp](22-hpc-macro-check.cpp)（本机 GCC 16.2，`-std=c++23 -O2 -mabi=sysv`，只看汇编）。GCC 文档原文在 Common Function Attributes 的 `optimize` 条目下（2026-10-15 用浏览器核对）。

### 源笔记定位

| 内容 | 锚点 |
|---|---|
| `HPC_FAST_BEGIN` / `HPC_FAST_END`（按区域改优化选项） | [源 :10](../../trading-system-notes/chinese/01-low-latency/22-hpc辅助宏.md:10) |
| `HPC_ALIGN(N)` | [源 :23](../../trading-system-notes/chinese/01-low-latency/22-hpc辅助宏.md:23) |
| `hpc::bit_cast_ptr` / `bit_cast_ref` | [源 :37](../../trading-system-notes/chinese/01-low-latency/22-hpc辅助宏.md:37) |

核心句：**`HPC_FAST_BEGIN` 的优化选项是真生效的，但 GCC 不在优化选项不同的函数之间内联，热路径上的小函数反而变成真调用，GCC 文档也说 `optimize` 只该用来调试；`HPC_ALIGN` 有标准的 `alignas` 代替；`bit_cast_ref` 是指针强转再解引用，违反严格别名。**

## 1. `HPC_FAST_BEGIN`：按区域改优化选项

源 :11–:12 用 `GCC push_options` 加 `GCC optimize ("O3,unroll-loops,associative-math,no-signed-zeros")`。

- **效果是真的**：区域里的 float 求和实测编成 `addps`（向量化，还展开了），区域外同一个循环是 `addss`；`associative-math` 没配 `no-trapping-math` 也生效了。浮点结合律放开会改变结果，见 [#18](18-强度削弱优化及算术优化.md) 的 3.2。
- **代价是内联**：GCC 不把优化选项不同的函数互相内联。区域里调区域外的 `static inline float sq(float)`、区域外调区域里的 `cube`，实测都是真的 `call`。热路径上的小函数（取字段、算下标）因此进不去，包一层宏可能更慢。
- **GCC 文档**：`optimize` 属性的说明里写明它只该用于调试，不适合生产代码（`#pragma GCC optimize` 等价于给后面的函数加这个属性）。要更激进的选项，给整个文件或构建目标加编译选项。

## 2. `HPC_ALIGN(N)`

源 :24–:28 按编译器换成 `__attribute__((aligned(N)))` / `__declspec(align(N))` / `alignas(N)`。C++11 起 `alignas(N)` 三家编译器都认，用不着宏。按缓存行对齐的用处见 [#11](11-内存对齐与典型内存布局优化.md)。

## 3. `bit_cast_ptr` / `bit_cast_ref`

源 :35 的注释说 `bit_cast_ptr` 是“类型安全的 reinterpret_cast 替代品”，函数体却就是 `reinterpret_cast`；`bit_cast_ref`（源 :47）再解引用，拿 `To` 类型的引用读 `From` 类型的内存，正是 [#19](19-直接操作内存二进制表示.md) 第 3 节违反严格别名的写法。要按位转换用 `std::bit_cast` 或 `memcpy`（编出来一条 `movq`）；只要一个指针时 `reinterpret_cast` 本身合法，只是不能拿它读写不是那个类型的对象。

## 与其他主题的连接

- **#19 直接操作内存二进制表示**（同在博客 #13）：`bit_cast_ref` 的问题就是严格别名。
- **#20 无法内联的函数调用**（同在博客 #13）：内联被挡住后，参数照调用约定传。
- **#18 强度削弱及算术优化**（博客 #12）：`associative-math` 放开以后浮点结果会变，`-ffast-math` 还会把 `isnan` 编没。
- **#12 分支预测**（博客 #7）：`likely` / `unlikely` 这类分支提示宏在那里讲过。
