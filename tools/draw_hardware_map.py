"""画博客速查页的硬件地图（SVG，中英文各一张）。

用法:
  python tools/draw_hardware_map.py --out-dir <博客仓库>/static/images/ref --page-dir <博客仓库>/static/maps

输出静态图 hardware-map.{zh,en}.svg，和交互版 hardware-map.{zh,en}.html（点框看说明、按路径逐步高亮、
蓝字链到文章；文中的“硬件地图”链接指向交互版）。只用标准库。
以后讲到新的硬件层，改下面 T 里对应框的文字（蓝字 = 在哪里讲过），重新生成即可。
"""
import argparse
import json
import os
import re
from xml.sax.saxutils import escape

W, H = 1600, 1950

T = {
    "zh": dict(
        font="PingFang SC, Microsoft YaHei, Noto Sans CJK SC, sans-serif",
        title="硬件地图：一次访存会经过哪些层",
        sub1="蓝字 = 在哪里讲过（#N = 交易系统笔记 #N，MESI = 专题，速查 N.N = 速查页的小节）。数字都是量级。",
        sub2="实线箭头 = 指令和数据怎么走；红色虚线 = 分支猜错后冲刷、重新取指；灰色虚线 = 核与核之间的一致性消息。",
        sock0="CPU 插槽 0（socket）= 一个 NUMA 节点",
        sock0_tag="#1 绑核 / 隔离 · NUMA",
        core0="核心 0（core）",
        core0_tag="2 个硬件线程共用这个核（SMT，超线程，#1）",
        laneA="前端（front end）：取指令",
        laneB="后端（back end）：乱序执行",
        laneC="访存（memory）：load / store",
        A1=("分支预测器（BPU）", ["BTB · RAS · 方向预测器", "猜下一条指令在哪"], ["#7 分支预测"]),
        A2=("取指（fetch）", ["按预测的地址从 L1I 取指令", "猜错的路径上取的全部作废"], ["#7 推测执行"]),
        A3=("译码（decode）", ["指令 → 微操作（µop）"], []),
        A4=("L1I 32 KiB（指令缓存）", ["存指令，不存数据"], ["#6 函数分组"]),
        B1=("重命名 + ROB", ["ROB（reorder buffer，重排序缓冲）", "记着还没退休的指令"], ["速查 2.3 ROB 与 MLP"]),
        B2=("调度 → 执行单元", ["ALU 算数 · AGU 算地址", "分支在这里算出真假"], ["#2 依赖链 · 多累加器"]),
        B3=("退休（retire）", ["按程序顺序提交结果"], []),
        B4=("寄存器（register file）", [], ["#2 寄存器溢出", "#6 存储类别"]),
        C1=("dTLB（数据 TLB，地址翻译）", ["虚拟地址 → 物理地址"], ["#6 2 MiB 大页 · #3 TLB shootdown", "速查 2.1 TLB 覆盖范围"]),
        SB=("Store buffer", ["存储缓冲", "写先在这排队，", "按顺序写进 L1D"], ["#2 store buffer", "#2 store-to-load", "转发"]),
        IQ=("Invalidate queue", ["失效队列", "别的核发来的", "失效请求先排队"], ["#2 invalidate queue", "#2 acquire"]),
        C4=("L1D 32 KiB · 8 路（数据缓存）", ["LFB（line fill buffer，行填充缓冲）"], ["#6 对齐 / 跨行 · 组相联", "速查 2.3 LFB"]),
        L2=("L2 512 KiB · 8 路（本核私有，指令和数据共用）", ["#6 关键步长 64 KiB · #2 L1/L2 私有"]),
        line1="缓存和内存之间按 64 B 一整条“缓存行”搬运",
        line2="#2 · #6 一次访问碰几条行",
        arr_store="store",
        arr_uop="µop",
        arr_commit="提交",
        arr_inv="别的核要写某一行：发失效请求，排进核心 0 的 invalidate queue",
        arr_coh="一致性",
        others="核心 1 … 核心 5（结构和核心 0 相同）",
        core_n="核心 {n}",
        mini=["前端", "后端", "L1I · L1D", "L2"],
        mesi_t="MESI：单写者 / 多读者",
        mesi_s=["同一条缓存行可以在多个核的 L1/L2 里各有一份副本。", "谁要写，就得先让别人的副本作废（发失效请求）。"],
        mesi_tags=["MESI 专题", "#2 伪共享（两个核抢同一行）", "#4 缓存游标 · #5 读者越多写越贵", "#6 alignas(64) 让对象独占一行"],
        priv_t="每个核各有自己的 L1、L2",
        priv_s="（TLB、store buffer 也各有一份）",
        l3=("L3 16 MiB · 16 路（全部核共享）", "#2 L3 共享 · #6 高位哈希选组"),
        mc=("内存控制器：把连续地址轮流分到各个通道（交织）", "#6 多通道均衡（行数和通道数互质）"),
        dram="内存条（DRAM）",
        ch0="通道 0", ch0_s="DIMM → rank → 多个 bank", bank="一个 bank = 行 × 列的表",
        rowbuf="行缓冲：当前打开的那一行",
        bank_s=["命中已打开的行 ~15 ns", "换一行（行冲突）~40 ns", "刷新：每 ~7.8 µs 一次，", "卡 300–500 ns → P99 毛刺"],
        ch1="通道 1", ch_same="结构同通道 0", ch1_s=["各通道能同时工作，", "带宽 ≈ 通道数 × 单通道"],
        ch2="通道 2 …",
        sock1="CPU 插槽 1",
        sock1_s=["自己的核、L3、内存控制器和内存", "访问另一个插槽的内存要过互连，更慢"],
        sock1_tags=["#1 NUMA：线程和内存放同一节点", "速查 2.1 first-touch 放置"],
        qpi="插槽间互连",
        nic=("网卡 → 中断 → 某个核", "#1 中断绑到非热路径的核", "（操作系统层，不是缓存层）"),
        addr_h="一个地址的各段（4 KiB 页，L1D 32 KiB 8 路，64 B 行）",
        addr=["虚拟页号（bit 12 以上）→ TLB 翻译", "组号 bit 6–11", "行内偏移 bit 0–5"],
        addr_tags=["大页：页号变少，TLB 装得下", "#6 相差 4 KiB 倍数 → 同一组", "#6 对齐 = 这几位为 0"],
        addr_n=["页内偏移 bit 0–11：翻译前后不变，所以 L1 能一边翻译一边选组（VIPT）",
                "→ L1 的“组数 × 行大小”被卡在 4 KiB，做大只能加路数"],
        lat_h="访问延迟量级", lat_s="周期按约 4 GHz 折算。每往下一层，大约慢 3–10 倍。",
        lat_cols=["访问时间", "容量", "谁来管理"],
        lat_rows=[
            ("寄存器", "~1 周期（< 1 ns）", "约 1 KB", "编译器"),
            ("L1D 缓存", "4–5 周期（~1 ns）", "32–48 KiB / 核", "硬件"),
            ("L2 缓存", "~12–20 周期（~3–5 ns）", "0.5–2 MiB / 核", "硬件"),
            ("L3 缓存", "~40–120 周期（~10–30 ns）", "约 10–500 MiB，全部核共享", "硬件"),
            ("本地内存", "~300–500 周期（~70–120 ns）", "约 16 GB 到 4 TB", "操作系统（页表）"),
            ("远端 NUMA 内存", "~500–800 周期（~120–200 ns）", "另一个插槽的内存", "操作系统 + 你绑的位置"),
            ("NVMe SSD", "10–100 µs", "TB 级", "操作系统（文件系统）"),
        ],
        zones=["核内", "芯片内", "芯片外", "外部设备"],
        lat_note="数字都是帮助理解的量级，不对应哪台机器。",
        foot="图随文章更新：后续文章讲到新的硬件层时再往上补标注。",
    ),
    "en": dict(
        font="Helvetica, Arial, sans-serif",
        title="Hardware map: the layers one memory access passes through",
        sub1="Blue = where it is covered (#N = Trading System Notes #N; MESI = the MESI topic post; Ref N.N = a section of the Reference page). Numbers are rough magnitudes.",
        sub2="Solid arrows = how instructions and data flow; red dashed = flush and refetch after a branch mispredict; grey dashed = coherence messages between cores.",
        sock0="CPU socket 0 = one NUMA node",
        sock0_tag="#1 pinning / isolation · NUMA",
        core0="Core 0",
        core0_tag="2 hardware threads share this core (SMT, #1)",
        laneA="Front end: fetch instructions",
        laneB="Back end: out-of-order execution",
        laneC="Memory: loads and stores",
        A1=("Branch predictor (BPU)", ["BTB · RAS · direction predictor", "guesses where the next instruction is"], ["#7 branch prediction"]),
        A2=("Fetch", ["reads code from L1I at the predicted", "address; a wrong path is thrown away"], ["#7 speculative execution"]),
        A3=("Decode", ["instructions → micro-ops (µops)"], []),
        A4=("L1I 32 KiB (instruction cache)", ["holds code, not data"], ["#6 function grouping"]),
        B1=("Rename + ROB", ["ROB = reorder buffer:", "instructions not yet retired"], ["Ref 2.3 ROB and MLP"]),
        B2=("Schedule → execution units", ["ALU math · AGU addresses", "branches are resolved here"], ["#2 dependency chains"]),
        B3=("Retire", ["commits results in program order"], []),
        B4=("Register file", [], ["#2 register spills", "#6 storage classes"]),
        C1=("dTLB (data TLB, translation)", ["virtual address → physical address"], ["#6 2 MiB pages · #3 TLB shootdown", "Ref 2.1 TLB reach"]),
        SB=("Store buffer", ["stores queue here,", "then drain to L1D", "in order"], ["#2 store buffer", "#2 store-to-load", "forwarding"]),
        IQ=("Invalidate queue", ["invalidations from", "other cores wait", "here"], ["#2 invalidate queue", "#2 acquire"]),
        C4=("L1D 32 KiB · 8-way (data cache)", ["LFB (line fill buffer)"], ["#6 alignment · sets", "Ref 2.3 LFB"]),
        L2=("L2 512 KiB · 8-way (private to the core, code and data)", ["#6 critical stride 64 KiB · #2 private L1/L2"]),
        line1="Caches and memory move data in whole 64 B cache lines",
        line2="#2 · #6 how many lines one access touches",
        arr_store="store",
        arr_uop="µops",
        arr_commit="commit",
        arr_inv="another core wants to write a line: invalidations land in core 0's invalidate queue",
        arr_coh="MESI",
        others="Core 1 … Core 5 (same structure as core 0)",
        core_n="Core {n}",
        mini=["Front", "Back", "L1I · L1D", "L2"],
        mesi_t="MESI: one writer or many readers",
        mesi_s=["One cache line can have a copy in several cores' L1/L2.", "To write it, a core must first invalidate the others."],
        mesi_tags=["MESI topic post", "#2 false sharing (two cores fight over a line)", "#4 cached cursor · #5 more readers, costlier writes", "#6 alignas(64) gives an object its own line"],
        priv_t="Every core has its own L1 and L2",
        priv_s="(and its own TLB and store buffer)",
        l3=("L3 16 MiB · 16-way (shared by all cores)", "#2 shared L3 · #6 hashed set selection"),
        mc=("Memory controller: spreads consecutive addresses across channels (interleaving)", "#6 channel balance (line count coprime with channel count)"),
        dram="DIMMs (DRAM)",
        ch0="Channel 0", ch0_s="DIMM → rank → many banks", bank="A bank = a table of rows × columns",
        rowbuf="Row buffer: the row now open",
        bank_s=["hit the open row ~15 ns", "switch rows (conflict) ~40 ns", "refresh every ~7.8 µs", "stalls 300–500 ns → P99 spikes"],
        ch1="Channel 1", ch_same="same as channel 0", ch1_s=["channels work in parallel:", "bandwidth ≈ channels × one channel"],
        ch2="Channel 2 …",
        sock1="CPU socket 1",
        sock1_s=["its own cores, L3, controller and memory", "remote memory goes over the interconnect"],
        sock1_tags=["#1 NUMA: thread and memory on the same node", "Ref 2.1 first-touch placement"],
        qpi="socket interconnect",
        nic=("NIC → interrupt → some core", "#1 steer IRQs away from hot cores", "(an OS layer, not a cache layer)"),
        addr_h="The fields of one address (4 KiB pages, L1D 32 KiB 8-way, 64 B lines)",
        addr=["Virtual page number (bits 12+) → TLB", "Set index bits 6–11", "Line offset bits 0–5"],
        addr_tags=["Huge pages: fewer page numbers, fit in the TLB", "#6 4 KiB multiples apart → same set", "#6 aligned = these bits are 0"],
        addr_n=["Page offset bits 0–11 don't change in translation, so L1 picks the set while the TLB translates (VIPT)",
                "→ L1's sets × line size is capped at 4 KiB; it can only grow by adding ways"],
        lat_h="Access latency magnitudes", lat_s="Cycles assume about 4 GHz. Each level down is roughly 3–10× slower.",
        lat_cols=["Access time", "Capacity", "Managed by"],
        lat_rows=[
            ("Registers", "~1 cycle (< 1 ns)", "about 1 KB", "compiler"),
            ("L1D cache", "4–5 cycles (~1 ns)", "32–48 KiB / core", "hardware"),
            ("L2 cache", "~12–20 cycles (~3–5 ns)", "0.5–2 MiB / core", "hardware"),
            ("L3 cache", "~40–120 cycles (~10–30 ns)", "about 10–500 MiB, shared", "hardware"),
            ("Local DRAM", "~300–500 cycles (~70–120 ns)", "about 16 GB to 4 TB", "OS (page tables)"),
            ("Remote NUMA", "~500–800 cycles (~120–200 ns)", "another socket's memory", "OS + where you pin"),
            ("NVMe SSD", "10–100 µs", "TB scale", "OS (file system)"),
        ],
        zones=["in core", "on chip", "off chip", "devices"],
        lat_note="Numbers are magnitudes for intuition, not any specific machine.",
        foot="The map grows with the series: new hardware layers get labels as later posts cover them.",
    ),
}

STYLE = """
      .t  { font-size: 15px; font-weight: 600; fill: #1f2328; }
      .s  { font-size: 12px; fill: #57606a; }
      .h  { font-size: 17px; font-weight: 700; fill: #1f2328; }
      .tag{ font-size: 12px; fill: #0969da; }
      .lab{ font-size: 11.5px; fill: #57606a; }
      .lane-h { font-size: 13px; font-weight: 700; }
      .box   { fill: #ffffff; stroke: #8c959f; stroke-width: 1.2; }
      .core  { fill: #f6f8fa; stroke: #57606a; stroke-width: 1.5; }
      .sock  { fill: #fbfbfc; stroke: #1f2328; stroke-width: 2; }
      .cache { fill: #ddf4ff; stroke: #54aeff; stroke-width: 1.2; }
      .buf   { fill: #fff8c5; stroke: #d4a72c; stroke-width: 1.2; }
      .mem   { fill: #ffebe9; stroke: #ff8182; stroke-width: 1.2; }
      .line  { stroke: #555; stroke-width: 1.5; fill: none; }
      .flush { stroke: #cf222e; stroke-width: 1.5; fill: none; stroke-dasharray: 6 4; }
      .coh   { stroke: #6e7781; stroke-width: 1.5; fill: none; stroke-dasharray: 5 3; }
"""

LANES = [  # x, w, 背景, 边框, 标题色
    (60, 280, "#f7f2ff", "#c8a8ff", "#8250df"),
    (400, 280, "#eefbf1", "#8ddb9c", "#1a7f37"),
    (730, 290, "#eef6ff", "#8cc4ff", "#0969da"),
]
ROW_Y = [190, 290, 390, 490]
ROW_H = 80

# 蓝字里的 “#N / MESI / 速查 N.N” 链到哪篇文章（交互版里可点）
POSTS = {
    "1": "cpu-affinity-core-isolation-numa",
    "2": "memory-ordering-false-sharing-dependency-chains",
    "3": "hot-path-memory-allocators",
    "4": "lock-free-queue-logger-micro-batching",
    "5": "spmc-shared-memory-broadcast-ring",
    "6": "alignment-layout-cache-dram-geometry",
    "7": "branch-prediction-branch-optimization",
    "MESI": "low-latency-mesi-cache-coherence",
}
REF_ANCHORS = {"1.1": "load", "1.2": "store", "2.1": "latency", "2.2": "bandwidth",
               "2.3": "in-flight", "2.4": "appetite", "2.5": "dram", "2.6": "cross-core"}
REF_RE = re.compile(r"#(\d+)|MESI|(?:速查|Ref) (\d\.\d)")


def ref_url(m, lang):
    pre = "/zh" if lang == "zh" else ""
    if m.group(1):
        slug = POSTS.get(m.group(1))
        return f"{pre}/posts/{slug}/" if slug else None
    if m.group(0) == "MESI":
        return f"{pre}/posts/{POSTS['MESI']}/"
    anchor = REF_ANCHORS.get(m.group(2))
    return f"{pre}/ref/cpu-memory/#{anchor}" if anchor else f"{pre}/ref/cpu-memory/"


def tag_links(s, lang):
    """把一行蓝字按 “ · ” 切段，每段第一个引用决定它链到哪，链到同一处的相邻段合并。返回 [(文字, url 或 None)]。"""
    out = []
    for k, seg in enumerate(s.split(" · ")):
        m = REF_RE.search(seg)
        url = ref_url(m, lang) if m else (out[-1][1] if out else None)  # 没写引用的段跟着前一段
        if out and url == out[-1][1]:
            out[-1] = (out[-1][0] + " · " + seg, url)
        else:
            out.append(((" · " if k else "") + seg, url))
    return out


# 交互版：点框看说明；按路径逐步高亮。每一步 = (要亮的框和箭头, 说明)
UI = {
    "zh": dict(
        page_title="硬件地图",
        lead="点一个框看它是什么、在哪篇讲过；选一条路径，一步一步看指令和数据怎么走。蓝字可以直接点开对应的文章。",
        other_lang=("English", "hardware-map.en.html"),
        static=("静态图", "/images/ref/hardware-map.zh.svg"),
        ref=("速查页", "/zh/ref/cpu-memory/"),
        prev="上一步", next="下一步", exit="退出", step="第 {i} / {n} 步",
        panel_empty="点图里的任意一个框。",
        zoom=("看原尺寸", "适应宽度"),
        panel_links="在哪里讲过",
        tours={
            "load": ("一次 load 走到内存", [
                (["exec"], "执行单元算出要读的地址（虚拟地址）。"),
                (["e_exec_dtlb", "dtlb"], "dTLB 把虚拟地址翻译成物理地址。命中不额外花时间；没命中要走页表，约 15–30 ns。"),
                (["e_dtlb_l1d", "l1d"], "L1D 按组号选组、比对标签。命中约 1 ns；没命中先在 LFB 登记一项，再往下问。"),
                (["e_l1d_l2", "l2"], "L2 是本核私有的，命中约 3–5 ns。"),
                (["e_l2_l3", "l3"], "L3 是全部核共享的，命中约 10–30 ns。"),
                (["e_l3_mc", "mc"], "都没命中，请求交给内存控制器，它按地址选通道。"),
                (["e_mc_ch0", "ch0"], "通道里按 rank、bank、行找到这条行。整条 64 B 行原路返回，沿路放进各级缓存，一共约 80–120 ns。"),
            ]),
            "store": ("一次 store", [
                (["exec", "e_exec_sb", "sb"], "store 执行时把地址和数据写进 store buffer，后面的指令不用等它写进缓存。"),
                (["retire", "e_exec_retire"], "这条 store 退休以后，才允许真正写出去（猜错路径上的 store 不能写出去）。"),
                (["e_sb_l1d", "l1d"], "store buffer 按顺序把写交给 L1D。行不在 L1D，就要先把整条行读回来（RFO），再改其中几个字节。"),
                (["others", "e_inv", "iq", "e_iq_l1d"], "要写的行如果别的核也有副本，得先让它们作废（MESI）。反过来，别的核要写时，发来的失效请求排进核心 0 的 invalidate queue。"),
            ]),
            "branch": ("分支猜错", [
                (["bpu"], "分支预测器猜：这条分支跳不跳、跳到哪（BTB、RAS、方向预测器）。"),
                (["e_bpu_fetch", "fetch", "e_fetch_decode", "decode", "e_decode_rob", "rob"], "前端不等结果，按猜的方向一路取指、译码，把微操作送进后端。"),
                (["e_rob_exec", "exec"], "分支到了执行单元才算出真假。猜对了，什么都不用做。"),
                (["e_flush", "fetch"], "猜错了：错误路径上的微操作全部作废，从正确地址重新取指。损失约 15–20 周期（约 4–5 ns）。"),
            ]),
            "insn": ("指令从哪来", [
                (["l2", "e_l1i_l2", "l1i"], "指令和数据一样存在内存里，经 L2 进入 L1I（指令缓存）。"),
                (["e_l1i_fetch", "fetch"], "取指按预测的地址从 L1I 读指令。"),
                (["e_fetch_decode", "decode"], "译码把指令拆成微操作（µop）。"),
                (["e_decode_rob", "rob", "e_rob_exec", "exec"], "微操作进 ROB 登记，等操作数到齐就乱序执行。"),
                (["e_exec_retire", "retire", "e_retire_regs", "regs"], "按程序顺序退休，结果提交到寄存器。"),
            ]),
        },
    ),
    "en": dict(
        page_title="Hardware Map",
        lead="Click a box to see what it is and where it is covered. Pick a path to step through how instructions and data move. Blue labels open the posts.",
        other_lang=("中文", "hardware-map.zh.html"),
        static=("Static image", "/images/ref/hardware-map.en.svg"),
        ref=("Reference", "/ref/cpu-memory/"),
        prev="Back", next="Next", exit="Exit", step="Step {i} of {n}",
        panel_empty="Click any box in the map.",
        zoom=("Actual size", "Fit width"),
        panel_links="Covered in",
        tours={
            "load": ("A load that goes to memory", [
                (["exec"], "The execution unit computes the address to read (a virtual address)."),
                (["e_exec_dtlb", "dtlb"], "The dTLB translates it to a physical address. A hit costs nothing extra; a miss walks the page table, about 15–30 ns."),
                (["e_dtlb_l1d", "l1d"], "L1D picks the set and compares tags. A hit is about 1 ns; a miss first takes an LFB entry, then asks further down."),
                (["e_l1d_l2", "l2"], "L2 is private to the core; a hit is about 3–5 ns."),
                (["e_l2_l3", "l3"], "L3 is shared by all cores; a hit is about 10–30 ns."),
                (["e_l3_mc", "mc"], "A miss everywhere goes to the memory controller, which picks the channel from the address."),
                (["e_mc_ch0", "ch0"], "Inside the channel, rank, bank and row locate the line. The whole 64 B line comes back the same way and is filled into each cache level; about 80–120 ns in total."),
            ]),
            "store": ("A store", [
                (["exec", "e_exec_sb", "sb"], "When a store executes, its address and data go into the store buffer; later instructions don't wait for the cache."),
                (["retire", "e_exec_retire"], "Only after the store retires may it actually be written out (stores on a mispredicted path never are)."),
                (["e_sb_l1d", "l1d"], "The store buffer drains to L1D in order. If the line isn't in L1D, the whole line is read first (RFO) before a few bytes change."),
                (["others", "e_inv", "iq", "e_iq_l1d"], "If other cores hold copies of the line, they must be invalidated first (MESI). The other way round, invalidations from other cores queue in core 0's invalidate queue."),
            ]),
            "branch": ("A branch mispredict", [
                (["bpu"], "The branch predictor guesses whether the branch is taken and where it goes (BTB, RAS, direction predictor)."),
                (["e_bpu_fetch", "fetch", "e_fetch_decode", "decode", "e_decode_rob", "rob"], "The front end doesn't wait: it keeps fetching and decoding down the guessed path and feeds µops to the back end."),
                (["e_rob_exec", "exec"], "The branch is resolved only in the execution unit. A correct guess costs nothing."),
                (["e_flush", "fetch"], "A wrong guess flushes every µop on the wrong path and refetches from the right address, losing about 15–20 cycles (about 4–5 ns)."),
            ]),
            "insn": ("Where instructions come from", [
                (["l2", "e_l1i_l2", "l1i"], "Code lives in memory like data and comes through L2 into L1I (the instruction cache)."),
                (["e_l1i_fetch", "fetch"], "Fetch reads instructions from L1I at the predicted address."),
                (["e_fetch_decode", "decode"], "Decode turns instructions into micro-ops (µops)."),
                (["e_decode_rob", "rob", "e_rob_exec", "exec"], "µops are entered in the ROB and execute out of order once their operands are ready."),
                (["e_exec_retire", "retire", "e_retire_regs", "regs"], "They retire in program order and commit results to the registers."),
            ]),
        },
    ),
}

STYLE = """
      .t  { font-size: 15px; font-weight: 600; fill: #1f2328; }
      .s  { font-size: 12px; fill: #57606a; }
      .h  { font-size: 17px; font-weight: 700; fill: #1f2328; }
      .tag{ font-size: 12px; fill: #0969da; }
      .lab{ font-size: 11.5px; fill: #57606a; }
      .lane-h { font-size: 13px; font-weight: 700; }
      .box   { fill: #ffffff; stroke: #8c959f; stroke-width: 1.2; }
      .core  { fill: #f6f8fa; stroke: #57606a; stroke-width: 1.5; }
      .sock  { fill: #fbfbfc; stroke: #1f2328; stroke-width: 2; }
      .cache { fill: #ddf4ff; stroke: #54aeff; stroke-width: 1.2; }
      .buf   { fill: #fff8c5; stroke: #d4a72c; stroke-width: 1.2; }
      .mem   { fill: #ffebe9; stroke: #ff8182; stroke-width: 1.2; }
      .line  { stroke: #555; stroke-width: 1.5; fill: none; }
      .flush { stroke: #cf222e; stroke-width: 1.5; fill: none; stroke-dasharray: 6 4; }
      .coh   { stroke: #6e7781; stroke-width: 1.5; fill: none; stroke-dasharray: 5 3; }
"""


class S:
    def __init__(self, lang, interactive):
        self.o = []
        self.lang = lang
        self.live = interactive
        self.nodes = {}  # id -> {title, lines, tags: [[(文字, url)]]}

    def add(self, s):
        self.o.append("  " + s)

    def text(self, x, y, s, cls, anchor=None, style=None):
        a = f' text-anchor="{anchor}"' if anchor else ""
        st = f' style="{style}"' if style else ""
        self.add(f'<text x="{x}" y="{y}" class="{cls}"{a}{st}>{escape(s)}</text>')

    def tag(self, x, y, s, node=None):
        parts = tag_links(s, self.lang)
        if node:
            self.nodes[node]["tags"].append(parts)
        if not self.live:
            return self.text(x, y, s, "tag")
        inner = "".join(
            f'<a href="{u}" target="_blank" rel="noopener"><tspan>{escape(p)}</tspan></a>' if u else f"<tspan>{escape(p)}</tspan>"
            for p, u in parts)
        self.add(f'<text x="{x}" y="{y}" class="tag">{inner}</text>')

    def rect(self, x, y, w, h, cls=None, rx=6, fill=None, stroke=None, dash=False):
        c = f' class="{cls}"' if cls else ""
        f = f' fill="{fill}"' if fill else ""
        k = f' stroke="{stroke}"' if stroke else ""
        d = ' stroke-dasharray="6 4"' if dash else ""
        self.add(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{rx}"{c}{f}{k}{d}/>')

    def path(self, d, cls="line", start=False, end=True, marker="arr", eid=None):
        ms = f' marker-start="url(#{marker})"' if start else ""
        me = f' marker-end="url(#{marker})"' if end else ""
        i = f' id="{eid}"' if eid and self.live else ""
        e = " edge" if eid and self.live else ""
        self.add(f'<path d="{d}" class="{cls}{e}"{i}{ms}{me}/>')

    def open(self, nid, title, lines=()):
        self.nodes[nid] = {"title": title, "lines": list(lines), "tags": []}
        if self.live:
            self.add(f'<g id="{nid}" class="node" tabindex="0" role="button" aria-label="{escape(title)}">')

    def close(self):
        if self.live:
            self.add("</g>")

    def card(self, nid, x, y, w, h, cls, spec):
        """一个框：标题 + 灰字若干行 + 蓝字若干行，从上往下排。"""
        title, subs, tags = spec
        self.open(nid, title, subs)
        self.rect(x, y, w, h, cls)
        cy = y + 23
        self.text(x + 12, cy, title, "t")
        for s in subs:
            cy += 18
            self.text(x + 12, cy, s, "s")
        for t in tags:
            cy += 17
            self.tag(x + 12, cy, t, nid)
        self.close()


def draw(lang, interactive):
    t = T[lang]
    g = S(lang, interactive)
    g.add(f'<rect x="0" y="0" width="{W}" height="{H}" fill="#ffffff"/>')
    g.text(20, 38, t["title"], "h", style="font-size:23px")
    g.text(20, 60, t["sub1"], "s")
    g.text(20, 78, t["sub2"], "s")

    # ===== 插槽 0 =====
    g.rect(20, 92, 1560, 853, "sock", rx=10)
    g.text(36, 116, t["sock0"], "h")
    g.tag(36 + (330 if lang == "zh" else 270), 116, t["sock0_tag"])

    # ----- 核心 0：三条泳道 -----
    g.rect(40, 128, 1000, 617, "core", rx=8)
    g.text(56, 152, t["core0"], "t")
    g.tag(56 + (100 if lang == "zh" else 60), 152, t["core0_tag"])
    for (x, w, bg, bd, hc), name in zip(LANES, ("laneA", "laneB", "laneC")):
        g.rect(x - 8, 162, w + 16, 418, rx=8, fill=bg, stroke=bd, dash=True)
        g.text(x, 182, t[name], "lane-h", style=f"fill:{hc}")

    ax, bx, cx = LANES[0][0], LANES[1][0], LANES[2][0]
    aw, bw, cw = LANES[0][1], LANES[1][1], LANES[2][1]
    r1, r2, r3, r4 = ROW_Y
    # 前端
    g.card("bpu", ax, r1, aw, ROW_H, "box", t["A1"])
    g.card("fetch", ax, r2, aw, ROW_H, "box", t["A2"])
    g.card("decode", ax, r3, aw, ROW_H, "box", t["A3"])
    g.card("l1i", ax, r4, aw, ROW_H, "cache", t["A4"])
    # 后端
    g.card("rob", bx, r1, bw, ROW_H, "box", t["B1"])
    g.card("exec", bx, r2, bw, ROW_H, "box", t["B2"])
    g.card("retire", bx, r3, bw, ROW_H, "box", t["B3"])
    g.card("regs", bx, r4, bw, ROW_H, "box", t["B4"])
    # 访存
    sbw = (cw - 14) // 2
    g.card("dtlb", cx, r1, cw, ROW_H, "buf", t["C1"])
    g.card("sb", cx, r2, sbw, 180, "buf", t["SB"])
    g.card("iq", cx + cw - sbw, r2, sbw, 180, "buf", t["IQ"])
    g.card("l1d", cx, r4, cw, ROW_H, "cache", t["C4"])
    # L2
    g.open("l2", t["L2"][0])
    g.rect(ax, 605, cx + cw - ax, 64, "cache")
    g.text(ax + 12, 630, t["L2"][0], "t")
    g.tag(ax + 12, 652, t["L2"][1][0], "l2")
    g.close()
    g.text(56, 702, t["line1"], "s")
    g.tag(56, 720, t["line2"])

    mid = lambda r: r + ROW_H // 2
    # 前端内部：预测 → 取指 → 译码
    g.path(f"M{ax + 140},{r1 + ROW_H} L{ax + 140},{r2 - 2}", eid="e_bpu_fetch")
    g.path(f"M{ax + 140},{r2 + ROW_H} L{ax + 140},{r3 - 2}", eid="e_fetch_decode")
    # L1I → 取指（从左边绕上去）
    g.path(f"M{ax},{mid(r4)} L{ax - 4},{mid(r4)} L{ax - 4},{mid(r2) + 10} L{ax - 2},{mid(r2) + 10}", eid="e_l1i_fetch")
    # 译码 → ROB（µop）
    gx = ax + aw + 30
    g.path(f"M{ax + aw},{mid(r3)} L{gx},{mid(r3)} L{gx},{mid(r1)} L{bx - 2},{mid(r1)}", eid="e_decode_rob")
    g.text(gx + 4, mid(r1) + 60, t["arr_uop"], "lab")
    # 后端内部
    g.path(f"M{bx + 140},{r1 + ROW_H} L{bx + 140},{r2 - 2}", eid="e_rob_exec")
    g.path(f"M{bx + 140},{r2 + ROW_H} L{bx + 140},{r3 - 2}", eid="e_exec_retire")
    g.path(f"M{bx + 140},{r3 + ROW_H} L{bx + 140},{r4 - 2}", eid="e_retire_regs")
    g.text(bx + 148, r4 - 6, t["arr_commit"], "lab")
    # 猜错：执行 → 取指（红色虚线）
    g.path(f"M{bx},{mid(r2) + 18} L{ax + aw + 2},{mid(r2) + 18}", cls="flush", marker="arrR", eid="e_flush")
    # 执行 → dTLB（load / store 的地址），执行 → store buffer（store 的数据）
    hx = bx + bw + 25
    g.path(f"M{bx + bw},{r2 + 22} L{hx},{r2 + 22} L{hx},{mid(r1)} L{cx - 2},{mid(r1)}", eid="e_exec_dtlb")
    g.path(f"M{bx + bw},{r2 + 58} L{cx - 2},{r2 + 58}", eid="e_exec_sb")
    g.text(bx + bw + 6, r2 + 52, t["arr_store"], "lab")
    # dTLB、store buffer、invalidate queue → L1D
    g.path(f"M{cx + cw // 2},{r1 + ROW_H} L{cx + cw // 2},{r4 - 2}", eid="e_dtlb_l1d")
    g.path(f"M{cx + sbw // 2},{r2 + 180} L{cx + sbw // 2},{r4 - 2}", eid="e_sb_l1d")
    g.path(f"M{cx + cw - sbw // 2},{r2 + 180} L{cx + cw - sbw // 2},{r4 - 2}", eid="e_iq_l1d")
    # L1I、L1D → L2
    g.path(f"M{ax + 140},{r4 + ROW_H} L{ax + 140},{603}", start=True, eid="e_l1i_l2")
    g.path(f"M{cx + cw // 2},{r4 + ROW_H} L{cx + cw // 2},{603}", start=True, eid="e_l1d_l2")

    # ----- 核心 1…5 -----
    ox, ow = 1060, 500
    g.open("others", t["mesi_t"], t["mesi_s"])
    g.rect(ox, 128, ow, 617, "core", rx=8)
    g.text(ox + 16, 152, t["others"], "t")
    for i in range(5):
        y = 168 + i * 46
        g.rect(ox + 20, y, ow - 40, 38, "box", rx=5)
        g.text(ox + 34, y + 24, t["core_n"].format(n=i + 1), "t", style="font-size:13px")
        mx = ox + 100
        for k, (lab, w) in enumerate(zip(t["mini"], (70, 70, 110, 80))):
            if k == 0:
                g.rect(mx, y + 7, w, 24, rx=4, fill="#f7f2ff", stroke="#c8a8ff")
            elif k == 1:
                g.rect(mx, y + 7, w, 24, rx=4, fill="#eefbf1", stroke="#8ddb9c")
            else:
                g.rect(mx, y + 7, w, 24, "cache", rx=4)
            g.text(mx + w / 2, y + 23, lab, "s", anchor="middle")
            mx += w + 10
    my = 448
    g.text(ox + 20, my, t["mesi_t"], "t")
    for k, s in enumerate(t["mesi_s"]):
        g.text(ox + 20, my + 24 + k * 18, s, "s")
    for k, s in enumerate(t["mesi_tags"]):
        g.tag(ox + 20, my + 72 + k * 18, s, "others")
    g.close()
    g.open("priv", t["priv_t"], [t["priv_s"]])
    g.rect(ox + 20, 605, ow - 40, 64, "cache")
    g.text(ox + 34, 630, t["priv_t"], "t")
    g.text(ox + 34, 652, t["priv_s"], "s")
    g.close()
    # 一致性：别的核的失效请求进 invalidate queue；L2 之间的一致性
    iqy = 412
    g.path(f"M{ox + ow - 20},{iqy} L{cx + cw + 2},{iqy}", cls="coh", marker="arrG", eid="e_inv")
    g.text(ox + 20, iqy - 6, t["arr_inv"], "lab")
    g.path(f"M{cx + cw + 2},637 L{ox + 18},637", cls="coh", start=True, marker="arrG", eid="e_coh")
    g.text((cx + cw + ox + 20) / 2, 628, t["arr_coh"], "lab", anchor="middle", style="font-size:10.5px")

    # ----- L3、内存控制器 -----
    g.open("l3", t["l3"][0])
    g.rect(40, 770, 1520, 64, "cache")
    g.text(56, 795, t["l3"][0], "t")
    g.tag(56, 817, t["l3"][1], "l3")
    g.close()
    g.path(f"M{cx + 200},669 L{cx + 200},768", start=True, eid="e_l2_l3")
    g.path(f"M{ox + ow // 2},669 L{ox + ow // 2},768", start=True, eid="e_priv_l3")
    g.open("mc", t["mc"][0])
    g.rect(40, 860, 1520, 64, "mem")
    g.text(56, 885, t["mc"][0], "t")
    g.tag(56, 907, t["mc"][1], "mc")
    g.close()
    g.path("M800,834 L800,858", start=True, eid="e_l3_mc")

    # ===== 内存条 =====
    g.text(20, 985, t["dram"], "h")
    g.tag(20 + (130 if lang == "zh" else 135), 985, "#6")
    chs = [(40, 340), (400, 320), (740, 320)]
    for k, (x, w) in enumerate(chs):
        g.path(f"M{x + w // 2},924 L{x + w // 2},998", eid=f"e_mc_ch{k}")
    x, w = chs[0]
    g.open("ch0", t["ch0"], [t["ch0_s"], t["bank"], t["rowbuf"]] + t["bank_s"])
    g.nodes["ch0"]["tags"].append(tag_links("#6", lang))
    g.rect(x, 1000, w, 240, "mem", rx=8)
    g.text(x + 16, 1024, t["ch0"], "t")
    g.text(x + 16, 1046, t["ch0_s"], "s")
    g.rect(x + 16, 1060, w - 32, 165, "box")
    g.text(x + 28, 1082, t["bank"], "t")
    g.rect(x + 28, 1094, w - 56, 30, "buf")
    g.text(x + 38, 1114, t["rowbuf"], "s")
    for k, s in enumerate(t["bank_s"]):
        g.text(x + 28, 1146 + k * 19, s, "s")
    g.close()
    x, w = chs[1]
    g.open("ch1", t["ch1"], [t["ch_same"]] + t["ch1_s"])
    g.rect(x, 1000, w, 240, "mem", rx=8)
    g.text(x + 16, 1024, t["ch1"], "t")
    g.text(x + 16, 1046, t["ch_same"], "s")
    for k, s in enumerate(t["ch1_s"]):
        g.text(x + 16, 1080 + k * 20, s, "s")
    g.close()
    x, w = chs[2]
    g.open("ch2", t["ch2"], [t["ch_same"]])
    g.rect(x, 1000, w, 240, "mem", rx=8)
    g.text(x + 16, 1024, t["ch2"], "t")
    g.text(x + 16, 1046, t["ch_same"], "s")
    g.close()

    # ===== 插槽 1、网卡（右下）=====
    sx, sw = 1090, 490
    g.open("sock1", t["sock1"], t["sock1_s"])
    g.rect(sx, 1000, sw, 140, "sock", rx=10)
    g.text(sx + 16, 1026, t["sock1"], "h")
    for k, s in enumerate(t["sock1_s"]):
        g.text(sx + 16, 1050 + k * 19, s, "s")
    for k, s in enumerate(t["sock1_tags"]):
        g.tag(sx + 16, 1096 + k * 19, s, "sock1")
    g.close()
    g.path(f"M{sx + 380},947 L{sx + 380},998", start=True, eid="e_qpi")
    g.text(sx + 390, 978, t["qpi"], "lab")
    g.open("nic", t["nic"][0], [t["nic"][2]])
    g.rect(sx, 1160, sw, 80, "box", rx=8)
    g.text(sx + 16, 1185, t["nic"][0], "t")
    g.tag(sx + 16, 1206, t["nic"][1], "nic")
    g.text(sx + 16, 1226, t["nic"][2], "s")
    g.close()

    # ===== 地址各段 =====
    ay = 1290
    g.text(20, ay, t["addr_h"], "h")
    segs = [(40, 700, "#fff8c5", "#d4a72c"), (740, 400, "#ddf4ff", "#54aeff"), (1140, 420, "#ffffff", "#8c959f")]
    for (x, w, f, k), s, tg in zip(segs, t["addr"], t["addr_tags"]):
        g.add(f'<rect x="{x}" y="{ay + 15}" width="{w}" height="44" fill="{f}" stroke="{k}"/>')
        g.text(x + 16, ay + 42, s, "t")
        g.tag(x + 16, ay + 80, tg)
    g.path(f"M740,{ay + 94} L740,{ay + 104} L1560,{ay + 104} L1560,{ay + 94}", end=False)
    g.text(760, ay + 126, t["addr_n"][0], "s")
    g.text(760, ay + 144, t["addr_n"][1], "s")

    # ===== 延迟阶梯 =====
    ly = 1470
    g.text(20, ly, t["lat_h"], "h")
    g.text(20, ly + 20, t["lat_s"], "s")
    tx0, tx1, c1, c2 = 340, 1560, 780, 1160
    top = ly + 34
    g.add(f'<rect x="{tx0}" y="{top}" width="{tx1 - tx0}" height="42" fill="#7f9db9"/>')
    for cxm, s in zip(((tx0 + c1) / 2, (c1 + c2) / 2, (c2 + tx1) / 2), t["lat_cols"]):
        g.text(cxm, top + 27, s, "t", anchor="middle", style="font-size:16px;font-weight:700;fill:#ffffff")
    n = len(t["lat_rows"])
    bottom = top + 42 + 46 * n
    for i, (name, a, b, c) in enumerate(t["lat_rows"]):
        y = top + 42 + 46 * i
        g.add(f'<rect x="{tx0}" y="{y}" width="{tx1 - tx0}" height="46" fill="{"#e3ebf3" if i % 2 == 0 else "#f1f5f9"}"/>')
        g.add(f'<line x1="{tx0}" y1="{y}" x2="{tx1}" y2="{y}" stroke="#ffffff" stroke-width="3"/>')
        for cxm, s in zip(((tx0 + c1) / 2, (c1 + c2) / 2, (c2 + tx1) / 2), (a, b, c)):
            g.text(cxm, y + 29, s, "t", anchor="middle", style="font-size:15px;font-weight:400")
        g.add(f'<rect x="170" y="{y + 6}" width="150" height="34" rx="3" fill="#7f9db9" stroke="#5d7d9c"/>')
        g.text(245, y + 28, name, "t", anchor="middle", style="font-size:13px;font-weight:400;fill:#ffffff")
        if i:
            g.path(f"M245,{y - 6} L245,{y + 6}", start=True)
    for xx in (c1, c2):
        g.add(f'<line x1="{xx}" y1="{top}" x2="{xx}" y2="{bottom}" stroke="#ffffff" stroke-width="3"/>')
    row = lambda i: top + 42 + 46 * i
    zone_mid = [row(0) + 23, (row(1) + row(4)) / 2, (row(4) + row(6)) / 2, row(6) + 23]
    for zm, s in zip(zone_mid, t["zones"]):
        g.text(95, zm + 5, s, "t", anchor="middle", style="font-size:14px;font-weight:400")
    for i in (1, 4, 6):
        g.add(f'<line x1="30" y1="{row(i)}" x2="330" y2="{row(i)}" stroke="#54aeff" stroke-width="1.2" stroke-dasharray="6 4"/>')
    g.text(tx0, bottom + 26, t["lat_note"], "tag")
    g.text(20, H - 20, t["foot"], "s")

    head = [
        f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W} {H}" width="{W}" height="{H}" font-family="{t["font"]}">',
        "  <defs>",
    ]
    for mid_, col in (("arr", "#555"), ("arrR", "#cf222e"), ("arrG", "#6e7781")):
        head.append(f'    <marker id="{mid_}" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse">')
        head.append(f'      <path d="M0,0 L10,5 L0,10 z" fill="{col}"/>')
        head.append("    </marker>")
    head.append("    <style>" + STYLE + "    </style>")
    head.append("  </defs>")
    return "\n".join(head + g.o + ["</svg>"]) + "\n", g.nodes


PAGE_CSS = """
:root { --bg:#f6f7f9; --card:#ffffff; --fg:#1f2328; --muted:#57606a; --line:#d0d7de; --accent:#0969da; --hl:#bf3989; }
@media (prefers-color-scheme: dark) { :root:not([data-theme="light"]) { --bg:#1c1a19; --card:#26231f; --fg:#ece6df; --muted:#b3aaa0; --line:#463f38; --accent:#e9a0b4; --hl:#ff7bb0; } }
:root[data-theme="dark"] { --bg:#1c1a19; --card:#26231f; --fg:#ece6df; --muted:#b3aaa0; --line:#463f38; --accent:#e9a0b4; --hl:#ff7bb0; }
* { box-sizing: border-box; }
body { margin:0; background:var(--bg); color:var(--fg); font: 15px/1.6 FONT; -webkit-tap-highlight-color: transparent; }
header { padding: 14px 16px 10px; display:flex; flex-wrap:wrap; gap:8px 16px; align-items:baseline; }
h1 { font-size: 22px; margin: 0; }
header .lead { flex: 1 1 420px; margin:0; color:var(--muted); font-size:14px; }
header nav { display:flex; gap:12px; font-size:14px; }
a { color: var(--accent); }
.tours { display:flex; flex-wrap:wrap; gap:8px; padding: 0 16px 10px; }
.tours button, .stepbar button { font: inherit; font-size:14px; padding: 5px 12px; border-radius: 999px; border:1px solid var(--line); background:var(--card); color:var(--fg); cursor:pointer; }
.tours button[aria-pressed="true"] { border-color: var(--hl); color: var(--hl); }
.tours .zoom { margin-left: auto; }
.map.full svg { width: 1600px; max-width: none; }
.layout { display:grid; grid-template-columns: minmax(0,1fr) 340px; gap: 12px; padding: 0 16px 16px; align-items:start; }
.map { background:#ffffff; border:1px solid var(--line); border-radius:10px; overflow:auto; max-height: calc(100vh - 140px); }
.map svg { display:block; width:100%; height:auto; min-width: 1000px; }
.side { position: sticky; top: 12px; display:flex; flex-direction:column; gap:12px; }
.card { background:var(--card); border:1px solid var(--line); border-radius:10px; padding: 12px 14px; }
.card h2 { font-size:17px; margin:0 0 6px; }
.card p { margin: 4px 0; color: var(--muted); font-size:14px; }
.card h3 { font-size:13px; margin:10px 0 4px; color:var(--muted); font-weight:600; }
.card ul { margin:0; padding-left: 18px; font-size:14px; }
.stepbar { display:none; }
.stepbar.on { display:block; }
.stepbar .count { font-size:13px; color:var(--muted); }
.stepbar .cap { margin: 6px 0 10px; font-size:15px; color:var(--fg); }
.stepbar .btns { display:flex; gap:8px; }
.node { cursor:pointer; outline:none; }
.node:hover > rect:first-of-type, .node:focus-visible > rect:first-of-type { stroke:#bf3989; stroke-width:2.5; }
.node.sel > rect:first-of-type { stroke:#bf3989; stroke-width:3; }
svg .tag a tspan { text-decoration: underline; text-decoration-color: rgba(9,105,218,.35); }
svg .tag a:hover tspan { fill:#bf3989; }
svg.dim .node, svg.dim .edge { opacity:.18; transition: opacity .25s; }
svg.dim .node.on, svg.dim .edge.on { opacity:1; }
svg .edge.on { stroke:#bf3989; stroke-width:3; }
svg.dim .node.on > rect:first-of-type { stroke:#bf3989; stroke-width:3; }
@media (max-width: 900px) {
  .layout { grid-template-columns: 1fr; }
  .side { position: static; order: -1; }
  .map { max-height: 70vh; }
}
@media (prefers-reduced-motion: reduce) { svg.dim .node, svg.dim .edge { transition:none; } }
"""

PAGE_JS = """
const DATA = __DATA__;
const svg = document.querySelector('.map svg');
const panel = document.getElementById('panel');
const bar = document.getElementById('stepbar');
let tour = null, step = 0;
function esc(s){ return s.replace(/[&<>"]/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c])); }
function show(id){
  svg.querySelectorAll('.node.sel').forEach(n => n.classList.remove('sel'));
  const n = DATA.nodes[id]; if (!n) return;
  document.getElementById(id).classList.add('sel');
  let h = '<h2>' + esc(n.title) + '</h2>' + n.lines.map(l => '<p>' + esc(l) + '</p>').join('');
  const links = [];
  n.tags.forEach(line => line.forEach(([txt, url]) => { if (url) links.push('<li><a href="' + url + '" target="_blank" rel="noopener">' + esc(txt.replace(/^ · /, '')) + '</a></li>'); }));
  if (links.length) h += '<h3>' + esc(DATA.ui.panel_links) + '</h3><ul>' + links.join('') + '</ul>';
  panel.innerHTML = h;
}
svg.querySelectorAll('.node').forEach(g => {
  g.addEventListener('click', e => { if (e.target.closest('a')) return; show(g.id); });
  g.addEventListener('keydown', e => { if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); show(g.id); } });
});
function render(){
  svg.querySelectorAll('.on').forEach(x => x.classList.remove('on'));
  document.querySelectorAll('.tours button[data-k]').forEach(b => b.setAttribute('aria-pressed', b.dataset.k === tour ? 'true' : 'false'));
  if (!tour) { svg.classList.remove('dim'); bar.classList.remove('on'); return; }
  const steps = DATA.ui.tours[tour][1];
  svg.classList.add('dim'); bar.classList.add('on');
  for (let i = 0; i <= step; i++) steps[i][0].forEach(id => { const el = document.getElementById(id); if (el) el.classList.add('on'); });
  bar.querySelector('.count').textContent = DATA.ui.step.replace('{i}', step + 1).replace('{n}', steps.length);
  bar.querySelector('.cap').textContent = steps[step][1];
  bar.querySelector('.prev').disabled = step === 0;
  bar.querySelector('.next').disabled = step === steps.length - 1;
  const last = steps[step][0].map(id => document.getElementById(id)).filter(el => el && el.classList.contains('node')).pop();
  if (last) { show(last.id); last.scrollIntoView({block:'nearest', inline:'nearest', behavior: matchMedia('(prefers-reduced-motion: reduce)').matches ? 'auto' : 'smooth'}); }
}
document.querySelector('.tours .zoom').addEventListener('click', e => {
  const on = document.querySelector('.map').classList.toggle('full');
  e.currentTarget.setAttribute('aria-pressed', on ? 'true' : 'false');
  e.currentTarget.textContent = on ? DATA.ui.zoom[1] : DATA.ui.zoom[0];
});
document.querySelectorAll('.tours button[data-k]').forEach(b => b.addEventListener('click', () => { tour = tour === b.dataset.k ? null : b.dataset.k; step = 0; render(); }));
bar.querySelector('.prev').addEventListener('click', () => { step--; render(); });
bar.querySelector('.next').addEventListener('click', () => { step++; render(); });
bar.querySelector('.exit').addEventListener('click', () => { tour = null; render(); });
document.addEventListener('keydown', e => {
  if (!tour) return;
  const n = DATA.ui.tours[tour][1].length;
  if (e.key === 'ArrowRight' && step < n - 1) { step++; render(); }
  if (e.key === 'ArrowLeft' && step > 0) { step--; render(); }
  if (e.key === 'Escape') { tour = null; render(); }
});
const h = location.hash.slice(1);
if (DATA.ui.tours[h]) { tour = h; step = 0; render(); }
try { const t = localStorage.getItem('theme'); if (t) document.documentElement.dataset.theme = t; } catch (e) {}
"""


def page(lang):
    t, u = T[lang], UI[lang]
    svg, nodes = draw(lang, True)
    tours = "".join(f'<button type="button" data-k="{k}" aria-pressed="false">{escape(v[0])}</button>' for k, v in u["tours"].items())
    data = json.dumps({"nodes": nodes, "ui": u}, ensure_ascii=False).replace("</", "<\\/")
    html_lang = "zh-CN" if lang == "zh" else "en"
    return f"""<!doctype html>
<html lang="{html_lang}">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>{escape(u["page_title"])}</title>
<link rel="icon" href="/images/logo.svg?v=20261008-rounded-brain-1">
<style>{PAGE_CSS.replace("FONT", t["font"])}</style>
</head>
<body>
<header>
<h1>{escape(u["page_title"])}</h1>
<p class="lead">{escape(u["lead"])}</p>
<nav><a href="{u["other_lang"][1]}">{escape(u["other_lang"][0])}</a><a href="{u["ref"][1]}">{escape(u["ref"][0])}</a><a href="{u["static"][1]}">{escape(u["static"][0])}</a></nav>
</header>
<div class="tours">{tours}<button type="button" class="zoom" aria-pressed="false">{escape(u["zoom"][0])}</button></div>
<div class="layout">
<div class="map">
{svg}</div>
<aside class="side">
<div class="card stepbar" id="stepbar"><div class="count"></div><div class="cap"></div><div class="btns"><button type="button" class="prev">{escape(u["prev"])}</button><button type="button" class="next">{escape(u["next"])}</button><button type="button" class="exit">{escape(u["exit"])}</button></div></div>
<div class="card" id="panel"><p>{escape(u["panel_empty"])}</p></div>
</aside>
</div>
<script>{PAGE_JS.replace("__DATA__", data)}</script>
</body>
</html>
"""


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out-dir", required=True, help="静态 SVG 的目录（博客 static/images/ref）")
    ap.add_argument("--page-dir", help="交互版 HTML 的目录（博客 static/maps）")
    a = ap.parse_args()
    os.makedirs(a.out_dir, exist_ok=True)
    for lang in ("zh", "en"):
        p = os.path.join(a.out_dir, f"hardware-map.{lang}.svg")
        with open(p, "w", encoding="utf-8") as f:
            f.write(draw(lang, False)[0])
        print(p)
        if a.page_dir:
            os.makedirs(a.page_dir, exist_ok=True)
            p = os.path.join(a.page_dir, f"hardware-map.{lang}.html")
            with open(p, "w", encoding="utf-8") as f:
                f.write(page(lang))
            print(p)


if __name__ == "__main__":
    main()
