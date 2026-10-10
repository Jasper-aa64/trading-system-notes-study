"""画 #12 分支预测轮 1 的三张示意图（SVG，中英文各一张）。

用法:
  python tools/draw_branch_figs.py --out-dir <博客仓库>/static/images/branch-prediction [--note-dir docs]

  pipeline-flush   流水线时间线：猜对 vs 猜错（错误路径作废、执行级空出几个周期）
  two-bit-counter  2 位饱和计数器的 4 个状态，加一个循环的例子
  sorted-strip     不排序 vs 排序后，这条 if 的方向和 2 位计数器猜错的位置（按计数器真的模拟出来）
  cmov-chain       轮 2：even_sum 每个元素的依赖链，猜对的分支 1 个周期 vs cmov 2 个周期
  hint-layout      轮 3：UNLIKELY 写对 vs LIKELY 写反时 process() 的机器码怎么排（GCC 13 -O2 真实输出）
  bsearch-timeline 轮 2：大数组二分查找最后 6 层的读，cmov 排队 vs 分支猜着先读 vs cmov + 预取两个候选（模型，不是实测）
--note-dir 给了就把中文版也写一份到那里（学习仓库的笔记用）。只用标准库。
"""
import argparse
import os
import random
from xml.sax.saxutils import escape

FONT = {"zh": "PingFang SC, Microsoft YaHei, Noto Sans CJK SC, sans-serif", "en": "Helvetica, Arial, sans-serif"}
STYLE = (".h{font-size:19px;font-weight:700;fill:#1f2328}.t{font-size:14px;font-weight:600;fill:#1f2328}"
         ".s{font-size:13px;fill:#57606a}.c{font-size:13px;font-weight:600;fill:#1f2328}.red{fill:#cf222e}"
         ".k{font-size:12px;fill:#57606a}")


def svg(lang, w, h, body):
    return (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {w} {h}" width="{w}" height="{h}" font-family="{FONT[lang]}">\n'
            '<defs><marker id="a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse">'
            '<path d="M0,0 L10,5 L0,10 z" fill="#57606a"/></marker>'
            '<pattern id="hatch" width="8" height="8" patternUnits="userSpaceOnUse" patternTransform="rotate(45)">'
            '<rect width="8" height="8" fill="#eaeef2"/><line x1="0" y1="0" x2="0" y2="8" stroke="#afb8c1" stroke-width="3"/></pattern>'
            f'<style>{STYLE}</style></defs>\n'
            f'<rect x="0" y="0" width="{w}" height="{h}" fill="#ffffff"/>\n' + "\n".join(body) + "\n</svg>\n")


def text(x, y, s, cls, anchor=None, extra=""):
    a = f' text-anchor="{anchor}"' if anchor else ""
    return f'<text x="{x}" y="{y}" class="{cls}"{a}{extra}>{escape(s)}</text>'


# ---------------------------------------------------------------- 1. 流水线时间线
PIPE = {
    "zh": dict(title="流水线：每格 = 一条指令在这个周期所在的级",
               stages=["取指", "译码", "执行", "退休"], cycle="周期",
               a="猜对：分支 B 后面的指令一个接一个进来，执行级每个周期都有活",
               b="猜错：B 在第 5 周期执行时发现猜错，W1、W2 作废，从第 6 周期起重新取 I3",
               bubble="执行级空了 2 个周期",
               legend=[("#ddf4ff", "#54aeff", "普通指令"), ("#fff1c2", "#d4a72c", "分支 B"), ("url(#hatch)", "#8c959f", "猜错路径上的指令（作废）")],
               note="这里只画了 4 级，所以只空出 2 个周期。真实 CPU 从取指到执行有 15–20 级，猜错一次空出约 15–20 个周期。"),
    "en": dict(title="Pipeline: each cell = the stage an instruction is in during that cycle",
               stages=["Fetch", "Decode", "Execute", "Retire"], cycle="Cycle",
               a="Predicted right: instructions after branch B keep flowing; Execute has work every cycle",
               b="Mispredicted: B executes in cycle 5 and finds the guess wrong; W1, W2 are discarded and I3 is fetched from cycle 6",
               bubble="Execute idles for 2 cycles",
               legend=[("#ddf4ff", "#54aeff", "ordinary instruction"), ("#fff1c2", "#d4a72c", "branch B"), ("url(#hatch)", "#8c959f", "wrong-path instruction (discarded)")],
               note="Only 4 stages are drawn, so only 2 cycles are lost. Real CPUs have 15–20 stages from fetch to execute, so one mispredict loses about 15–20 cycles."),
}


def pipeline(lang):
    t = PIPE[lang]
    W, H = 1260, 720
    L, CW, RH, NC = 130, 88, 40, 10
    o = [text(20, 34, t["title"], "h")]

    def panel(y0, cap, sched, flushed, bubble):
        o.append(text(20, y0, cap, "t"))
        hy = y0 + 26
        o.append(text(L - 12, hy, t["cycle"], "k", "end"))
        for c in range(NC):
            o.append(text(L + c * CW + CW / 2, hy, str(c + 1), "k", "middle"))
        for s, name in enumerate(t["stages"]):
            y = hy + 12 + s * (RH + 6)
            o.append(text(L - 12, y + 26, name, "c", "end"))
            for c in range(NC):
                o.append(f'<rect x="{L + c * CW + 3}" y="{y}" width="{CW - 6}" height="{RH}" rx="4" fill="#f6f8fa" stroke="#eaeef2"/>')
        for name, start in sched:
            for s in range(4):
                c = start + s
                if c > NC or (name, s) in flushed["cut"]:
                    continue
                y = hy + 12 + s * (RH + 6)
                x = L + (c - 1) * CW + 3
                if name.startswith("W"):
                    fill, stroke = "url(#hatch)", "#8c959f"
                elif name == "B":
                    fill, stroke = "#fff1c2", "#d4a72c"
                else:
                    fill, stroke = "#ddf4ff", "#54aeff"
                o.append(f'<rect x="{x}" y="{y}" width="{CW - 6}" height="{RH}" rx="4" fill="{fill}" stroke="{stroke}" stroke-width="1.3"/>')
                o.append(text(x + (CW - 6) / 2, y + 26, name, "c", "middle"))
                if name.startswith("W"):
                    o.append(f'<path d="M{x + 8},{y + 6} L{x + CW - 14},{y + RH - 6} M{x + CW - 14},{y + 6} L{x + 8},{y + RH - 6}" stroke="#cf222e" stroke-width="2"/>')
        if bubble:
            c0, c1 = bubble
            y = hy + 12 + 2 * (RH + 6)
            x0, x1 = L + (c0 - 1) * CW + 1, L + c1 * CW - 1
            o.append(f'<rect x="{x0}" y="{y - 2}" width="{x1 - x0}" height="{RH + 4}" rx="6" fill="none" stroke="#cf222e" stroke-width="2" stroke-dasharray="6 4"/>')
            o.append(text(L + NC * CW + 14, y + 26, t["bubble"], "c", extra=' style="fill:#cf222e"'))
        return hy + 12 + 4 * (RH + 6)

    order_a = [("I1", 1), ("I2", 2), ("B", 3), ("I3", 4), ("I4", 5), ("I5", 6), ("I6", 7), ("I7", 8)]
    end = panel(80, t["a"], order_a, {"cut": set()}, None)
    # 猜错：W1 在第 4 周期取指、第 5 周期译码；W2 第 5 周期取指；B 第 5 周期执行，发现猜错，W1/W2 后面的级都不再走
    order_b = [("I1", 1), ("I2", 2), ("B", 3), ("W1", 4), ("W2", 5), ("I3", 6), ("I4", 7), ("I5", 8)]
    cut = {("W1", 2), ("W1", 3), ("W2", 1), ("W2", 2), ("W2", 3)}
    end = panel(end + 50, t["b"], order_b, {"cut": cut}, (6, 7))
    # 图例
    ly = end + 36
    x = 20
    for fill, stroke, lab in t["legend"]:
        o.append(f'<rect x="{x}" y="{ly - 14}" width="28" height="18" rx="3" fill="{fill}" stroke="{stroke}"/>')
        o.append(text(x + 36, ly, lab, "s"))
        x += 60 + len(lab) * (14 if lang == "zh" else 7)
    o.append(text(20, ly + 30, t["note"], "s"))
    H = ly + 50
    return svg(lang, W, H, o)


# ---------------------------------------------------------------- 2. 2 位饱和计数器
CNT = {
    "zh": dict(title="2 位饱和计数器：每条分支一个，0–3，≥ 2 就猜“跳”",
               states=[("0", "强不跳"), ("1", "弱不跳"), ("2", "弱跳"), ("3", "强跳")],
               zone=("猜：不跳", "猜：跳"), up="跳了：+1", down="没跳：−1",
               ex="例：一个循环每次进去跳 7 次、最后不跳 1 次（退出），然后再进这个循环",
               rows=("实际", "计数器", "猜"), T="跳", N="不跳",
               ex_note="只在退出那一次猜错。计数器从 3 降到 2 还是猜“跳”，下一次进循环不会跟着错。"),
    "en": dict(title="2-bit saturating counter: one per branch, 0–3, predict “taken” at ≥ 2",
               states=[("0", "strong not"), ("1", "weak not"), ("2", "weak taken"), ("3", "strong taken")],
               zone=("predict: not taken", "predict: taken"), up="taken: +1", down="not taken: −1",
               ex="Example: a loop taken 7 times then not taken once (the exit), then entered again",
               rows=("actual", "counter", "guess"), T="T", N="N",
               ex_note="Only the exit is mispredicted. The counter drops from 3 to 2, still predicts taken, so re-entering the loop isn’t mispredicted too."),
}


def counter(lang):
    t = CNT[lang]
    W = 1000
    o = [text(20, 34, t["title"], "h")]
    xs = [170, 390, 610, 830]
    cy = 175
    o.append(f'<rect x="60" y="70" width="440" height="200" rx="12" fill="#f6f8fa" stroke="#d0d7de" stroke-dasharray="6 4"/>')
    o.append(f'<rect x="500" y="70" width="440" height="200" rx="12" fill="#ddf4ff" stroke="#54aeff" stroke-dasharray="6 4"/>')
    o.append(text(280, 92, t["zone"][0], "t", "middle"))
    o.append(text(720, 92, t["zone"][1], "t", "middle", ' style="fill:#0969da"'))
    for k in range(3):  # 跳：上弧往右；没跳：下弧往左
        x0, x1 = xs[k] + 46, xs[k + 1] - 46
        o.append(f'<path d="M{x0},{cy - 18} Q{(x0 + x1) / 2},{cy - 70} {x1},{cy - 18}" fill="none" stroke="#1a7f37" stroke-width="2" marker-end="url(#a)"/>')
        o.append(f'<path d="M{x1},{cy + 18} Q{(x0 + x1) / 2},{cy + 70} {x0},{cy + 18}" fill="none" stroke="#cf222e" stroke-width="2" marker-end="url(#a)"/>')
        o.append(text((x0 + x1) / 2, cy - 50, t["up"], "s", "middle", ' style="fill:#1a7f37"'))
        o.append(text((x0 + x1) / 2, cy + 62, t["down"], "s", "middle", ' style="fill:#cf222e"'))
    for x, (n, name) in zip(xs, t["states"]):
        o.append(f'<circle cx="{x}" cy="{cy}" r="44" fill="#ffffff" stroke="#57606a" stroke-width="1.8"/>')
        o.append(text(x, cy - 2, n, "h", "middle"))
        o.append(text(x, cy + 20, name, "k", "middle"))
    # 两头的自环
    o.append(f'<path d="M{xs[3] + 30},{cy - 32} C{xs[3] + 90},{cy - 80} {xs[3] + 110},{cy - 10} {xs[3] + 44},{cy + 2}" fill="none" stroke="#1a7f37" stroke-width="2" marker-end="url(#a)"/>')
    o.append(f'<path d="M{xs[0] - 30},{cy + 32} C{xs[0] - 90},{cy + 80} {xs[0] - 110},{cy + 10} {xs[0] - 44},{cy - 2}" fill="none" stroke="#cf222e" stroke-width="2" marker-end="url(#a)"/>')
    # 例子：循环
    y0 = 320
    o.append(text(20, y0, t["ex"], "t"))
    seq = [1] * 7 + [0] + [1] * 4
    c = 3
    L, CW = 110, 70
    for r, lab in enumerate(t["rows"]):
        o.append(text(L - 12, y0 + 44 + r * 40, lab, "c", "end"))
    for i, taken in enumerate(seq):
        x = L + i * CW
        pred = c >= 2
        ok = pred == bool(taken)
        o.append(f'<rect x="{x + 3}" y="{y0 + 20}" width="{CW - 6}" height="34" rx="4" fill="{"#ddf4ff" if taken else "#f6f8fa"}" stroke="{"#54aeff" if taken else "#afb8c1"}"/>')
        o.append(text(x + CW / 2, y0 + 42, t["T"] if taken else t["N"], "c", "middle"))
        o.append(text(x + CW / 2, y0 + 84, str(c), "c", "middle"))
        o.append(text(x + CW / 2, y0 + 124, (t["T"] if pred else t["N"]) + (" ✓" if ok else " ✗"), "c", "middle",
                      "" if ok else ' style="fill:#cf222e"'))
        if not ok:
            o.append(f'<rect x="{x + 2}" y="{y0 + 102}" width="{CW - 4}" height="32" rx="4" fill="none" stroke="#cf222e" stroke-width="2"/>')
        c = min(3, c + 1) if taken else max(0, c - 1)
    o.append(text(20, y0 + 170, t["ex_note"], "s"))
    return svg(lang, W, y0 + 195, o)


# ---------------------------------------------------------------- 3. 排序前后的方向条带
STRIP = {
    "zh": dict(title="if (data[c] % 2 == 0)：格子里是 data[c]，蓝 = 偶数（条件成立），灰 = 奇数；红框 = 2 位计数器猜错",
               un="不排序：奇偶随机，方向没有规律", so="排序后：一段偶数接一段奇数（这里是 36、37 的交界）",
               cnt="这 {n} 个里猜错 {m} 个",
               note="排序后每个值约 82 个、一共 200 段，只在段与段的交界处猜错 1–2 次；一遍 16384 次里错约 200–400 次，约 1%–2%。"),
    "en": dict(title="if (data[c] % 2 == 0): each cell is data[c]; blue = even (condition true), grey = odd; red box = the 2-bit counter guessed wrong",
               un="Unsorted: parity is random, the direction has no pattern", so="Sorted: a run of evens, then a run of odds (here the 36/37 boundary)",
               cnt="{m} wrong out of these {n}",
               note="Sorted, each value appears about 82 times in 200 runs, and only the run boundaries mispredict, 1–2 times each: about 200–400 per pass of 16384, about 1%–2%."),
}


def strip(lang):
    t = STRIP[lang]
    W = 1180
    N, CW = 20, 46
    L = 40
    o = [text(20, 34, t["title"], "t")]
    rng = random.Random(7)
    un = [rng.randrange(200) for _ in range(N)]
    so = [36] * 10 + [37] * 10

    def row(y, vals, cap, c0):
        o.append(text(L, y, cap, "t"))
        c, miss = c0, 0
        for i, v in enumerate(vals):
            even = v % 2 == 0
            pred = c >= 2
            x = L + i * CW
            o.append(f'<rect x="{x + 2}" y="{y + 14}" width="{CW - 4}" height="38" rx="4" fill="{"#ddf4ff" if even else "#f6f8fa"}" stroke="{"#54aeff" if even else "#afb8c1"}"/>')
            o.append(text(x + CW / 2, y + 38, str(v), "c", "middle"))
            if pred != even:
                miss += 1
                o.append(f'<rect x="{x}" y="{y + 12}" width="{CW}" height="42" rx="5" fill="none" stroke="#cf222e" stroke-width="2.5"/>')
            c = min(3, c + 1) if even else max(0, c - 1)
        o.append(text(L + N * CW + 14, y + 38, t["cnt"].format(n=N, m=miss), "c", extra=' style="fill:#cf222e"'))

    row(80, un, t["un"], 2)
    row(170, so, t["so"], 3)
    o.append(text(L, 268, t["note"], "s"))
    return svg(lang, W, 290, o)


# ---------------------------------------------------------------- 4. 依赖链：分支 vs cmov（轮 2）
CHAIN = {
    "zh": dict(title="even_sum 的循环里，sum 每次都要等上一次的结果：这条链有多长，每个元素就要多久",
               a="保留分支，猜对了：链上只有 add",
               a_off="load、and、je 不在链上：方向已经猜好，CPU 先往下走，条件晚点算出来再核对",
               b="cmov：链上是 add + cmove",
               b_off="cmove 要等 and 算出条件、add 算出 sum + x，才能决定新的 sum；下一个元素的 add 又要等它",
               el="第 {i} 个", cycle="周期",
               note="实测（1.5 节）：猜对时约 0.25 ns/个，约 1 个周期；cmov 约 0.44 ns/个，约 2 个周期，和数据有没有规律无关。"),
    "en": dict(title="In the even_sum loop, sum always waits for the previous result: the length of this chain is the time per element",
               a="Branch kept, predicted right: only add is on the chain",
               a_off="load, and, je are off the chain: the direction is already guessed, the CPU runs ahead and checks the condition later",
               b="cmov: add + cmove are on the chain",
               b_off="cmove needs the condition from and and sum + x from add before it can produce the new sum; the next element's add waits for it",
               el="elem {i}", cycle="cycle",
               note="Measured (Section 1.5): predicted right, about 0.25 ns per element, about 1 cycle; cmov about 0.44 ns, about 2 cycles, whatever the data looks like."),
}


def chain(lang):
    t = CHAIN[lang]
    W, L, CW, NC = 1100, 60, 110, 8
    o = [text(20, 34, t["title"], "t")]
    # 周期刻度
    for k in range(NC + 1):
        x = L + k * CW
        o.append(f'<line x1="{x}" y1="62" x2="{x}" y2="372" stroke="#eaeef2"/>')
        if k < NC:
            o.append(text(x + CW / 2, 66, f'{t["cycle"]} {k + 1}', "k", "middle"))

    def panel(y, cap, off, ops, per):
        o.append(text(L, y, cap, "t"))
        boxes = []
        for k, op in enumerate(ops):
            x = L + k * CW
            red = op == "cmove"
            o.append(f'<rect x="{x + 8}" y="{y + 16}" width="{CW - 16}" height="40" rx="6" fill="{"#fff1c2" if red else "#ddf4ff"}" stroke="{"#d4a72c" if red else "#54aeff"}" stroke-width="1.6"/>')
            o.append(text(x + CW / 2, y + 41, op, "c", "middle"))
            boxes.append(x)
        for x in boxes[:-1]:
            o.append(f'<line x1="{x + CW - 8}" y1="{y + 36}" x2="{x + CW + 8}" y2="{y + 36}" stroke="#57606a" stroke-width="1.6" marker-end="url(#a)"/>')
        for i in range(len(ops) // per):
            x0, x1 = L + i * per * CW + 10, L + (i + 1) * per * CW - 10
            o.append(f'<path d="M{x0},{y + 64} v6 H{x1} v-6" fill="none" stroke="#8c959f"/>')
            o.append(text((x0 + x1) / 2, y + 86, t["el"].format(i=i + 1), "k", "middle"))
        o.append(text(L, y + 112, off, "s"))

    panel(100, t["a"], t["a_off"], ["add"] * NC, 1)
    panel(250, t["b"], t["b_off"], ["add", "cmove"] * (NC // 2), 2)
    o.append(text(L, 400, t["note"], "s"))
    return svg(lang, W, 420, o)


# ---------------------------------------------------------------- 5. 二分查找的读：cmov vs 分支 vs 预取（轮 2）
BS = {
    "zh": dict(title="大数组二分查找的最后 6 层，每层都要从内存读一次。横轴：一格 = 一次内存延迟（约 80–120 ns）",
               rows=["cmov", "保留分支", "cmov + 预取"],
               subs=["下一层的地址要等这一层读回来", "猜一个方向，下一层的读先发出去", "下一层左右两个候选都先读"],
               ok="猜对", bad="猜错", two="×2", total="{n} 格",
               legend=[("#ddf4ff", "#54aeff", "这一层的读"), ("#dafbe1", "#4ac26b", "按猜的方向提前发的读"), ("url(#hatch)", "#8c959f", "猜错的读，作废"), ("#fff8c5", "#d4a72c", "预取：左右各读一个，一个白读")],
               note="这是按机制画的模型，不是实测：分支版假设一次只往前猜一层，猜对、猜错各一半；CPU 能往前猜更多层，猜对的层还能更多。"),
    "en": dict(title="The last 6 levels of a binary search over a large array; each level reads memory once. One column = one memory latency (about 80–120 ns)",
               rows=["cmov", "branch kept", "cmov + prefetch"],
               subs=["next address waits for this read", "guess a side, issue the next read early", "read both candidates of the next level"],
               ok="right", bad="wrong", two="×2", total="{n} columns",
               legend=[("#ddf4ff", "#54aeff", "this level's read"), ("#dafbe1", "#4ac26b", "read issued early on the guessed side"), ("url(#hatch)", "#8c959f", "wrong guess, discarded"), ("#fff8c5", "#d4a72c", "prefetch: both sides, one wasted")],
               note="A model drawn from the mechanism, not a measurement: the branch row guesses one level ahead and is right half the time; a real CPU can guess further ahead and overlap more."),
}


def bsearch(lang):
    t = BS[lang]
    W, L, CW = 1180, 250, 130
    o = [text(20, 34, t["title"], "t")]
    for k in range(7):
        x = L + k * CW
        o.append(f'<line x1="{x}" y1="56" x2="{x}" y2="440" stroke="#eaeef2"/>')
    fill = {"load": ("#ddf4ff", "#54aeff"), "early": ("#dafbe1", "#4ac26b"), "bad": ("url(#hatch)", "#8c959f"), "pref": ("#fff8c5", "#d4a72c")}
    rows = [
        [[("L1", "load")], [("L2", "load")], [("L3", "load")], [("L4", "load")], [("L5", "load")], [("L6", "load")]],
        [[("L1", "load"), ("L2 " + t["ok"], "early")], [("L3", "load"), ("L4 " + t["bad"], "bad")], [("L4", "load"), ("L5 " + t["ok"], "early")], [("L6", "load")]],
        [[("L1", "load"), ("L2 " + t["two"], "pref")], [("L3 " + t["two"], "pref"), ("L4 " + t["two"], "pref")], [("L5 " + t["two"], "pref"), ("L6 " + t["two"], "pref")]],
    ]
    for r, cells in enumerate(rows):
        y = 70 + r * 125
        o.append(text(20, y + 40, t["rows"][r], "t"))
        o.append(text(20, y + 62, t["subs"][r], "k"))
        for k, items in enumerate(cells):
            for j, (lab, kind) in enumerate(items):
                f, st = fill[kind]
                yy = y + 8 + j * 48
                o.append(f'<rect x="{L + k * CW + 6}" y="{yy}" width="{CW - 12}" height="40" rx="5" fill="{f}" stroke="{st}" stroke-width="1.5"/>')
                o.append(text(L + k * CW + CW / 2, yy + 25, lab, "c", "middle"))
        n = len(cells)
        o.append(text(L + n * CW + 12, y + 54, t["total"].format(n=n), "c", extra=' style="fill:#cf222e"'))
    x = 20
    for f, st, lab in t["legend"]:
        o.append(f'<rect x="{x}" y="452" width="18" height="14" rx="3" fill="{f}" stroke="{st}"/>')
        o.append(text(x + 24, 464, lab, "k"))
        x += 24 + len(lab) * (13 if lang == "zh" else 6.6) + 30
    o.append(text(20, 494, t["note"], "s"))
    return svg(lang, W, 512, o)


# ---------------------------------------------------------------- 6. 提示改的是布局（轮 3）
HL = {
    "zh": dict(title="同一个 process()，提示不同，GCC -O2 排出来的机器码（地址从上往下增大，省略了栈调整）",
               cols=["if (UNLIKELY(n <= 0))：提示对", "if (LIKELY(n <= 0))：提示写反"],
               hot="热路径", err="错误路径",
               rare="很少跳", always="每次都跳",
               fall="热路径顺着往下走，不用跳", jump="热路径每次都要跳过错误处理",
               note="两种排法预测器都猜得准：它看的是这条跳转的历史，不看提示。差别在热路径要不要跳：顺着走时取指不断，热代码挤在一起，占的缓存行少。"),
    "en": dict(title="The same process(), two different hints: the machine code GCC -O2 lays out (addresses grow downward; stack adjustments omitted)",
               cols=["if (UNLIKELY(n <= 0)): hint right", "if (LIKELY(n <= 0)): hint backwards"],
               hot="hot path", err="error path",
               rare="rarely taken", always="taken every time",
               fall="the hot path falls straight through", jump="the hot path jumps over the error code every time",
               note="The predictor guesses both layouts well: it uses this jump's history, not the hint. The difference is whether the hot path jumps: falling through keeps fetch going and packs the hot code into fewer cache lines."),
}


def hint_layout(lang):
    t = HL[lang]
    W = 1200
    o = [text(20, 34, t["title"], "t")]
    good = [("test %esi, %esi", None), ("jle .L9", "j"),
            ("movslq %esi, %rsi", "h"), ("movl (%rdi), %eax", "h"), ("addl -4(%rdi,%rsi,4), %eax", "h"), ("ret", "h"),
            (".L9:  call report_error", "e"), ("movl $-1, %eax", "e"), ("ret", "e")]
    bad = [("test %esi, %esi", None), ("jg .L11", "j"),
           ("call report_error", "e"), ("movl $-1, %eax", "e"), ("ret", "e"),
           (".L11:  movslq %esi, %rsi", "h"), ("movl (%rdx), %eax", "h"), ("addl -4(%rdx,%rsi,4), %eax", "h"), ("ret", "h")]
    RH, Y0 = 30, 96
    for c, (rows, cap, lab, kind) in enumerate([(good, t["cols"][0], t["rare"], "dash"), (bad, t["cols"][1], t["always"], "solid")]):
        x = 60 + c * 530
        o.append(text(x, 72, cap, "t", extra=' style="font-family:monospace"' if False else ""))
        for i, (ins, k) in enumerate(rows):
            y = Y0 + i * RH
            fill, st = {"h": ("#ddf4ff", "#54aeff"), "e": ("#f6f8fa", "#afb8c1"), "j": ("#fff1c2", "#d4a72c"), None: ("#ffffff", "#d0d7de")}[k]
            o.append(f'<rect x="{x}" y="{y}" width="300" height="{RH - 4}" rx="4" fill="{fill}" stroke="{st}"/>')
            o.append(f'<text x="{x + 10}" y="{y + 18}" class="c" style="font-family:Menlo,Consolas,monospace;font-weight:400">{escape(ins)}</text>')
        # 段落标签
        hs = [i for i, (_, k) in enumerate(rows) if k == "h"]
        es = [i for i, (_, k) in enumerate(rows) if k == "e"]
        for idxs, name, col in ((hs, t["hot"], "#0969da"), (es, t["err"], "#57606a")):
            y0, y1 = Y0 + idxs[0] * RH, Y0 + idxs[-1] * RH + RH - 4
            o.append(f'<path d="M{x + 308},{y0} h6 V{y1} h-6" fill="none" stroke="{col}"/>')
            o.append(text(x + 320, (y0 + y1) / 2 + 5, name, "s", extra=f' style="fill:{col}"'))
        # 跳转箭头：从 j 行到目标行，走左边
        tgt = 6 if c == 0 else 5
        yj, yt = Y0 + RH + RH / 2 - 2, Y0 + tgt * RH + RH / 2 - 2
        dash = ' stroke-dasharray="5 4"' if kind == "dash" else ""
        col = "#8c959f" if kind == "dash" else "#cf222e"
        o.append(f'<path d="M{x},{yj} h-28 V{yt} h24" fill="none" stroke="{col}" stroke-width="2"{dash} marker-end="url(#a)"/>')
        o.append(text(x - 34, (yj + yt) / 2, lab, "k", "end" if False else None, f' transform="rotate(-90 {x - 36} {(yj + yt) / 2})" text-anchor="middle" style="fill:{col}"'))
        o.append(text(x, Y0 + 9 * RH + 22, t["fall"] if c == 0 else t["jump"], "s", extra=f' style="fill:{"#0969da" if c == 0 else "#cf222e"}"'))
    o.append(text(20, Y0 + 9 * RH + 62, t["note"], "s"))
    return svg(lang, W, Y0 + 9 * RH + 84, o)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out-dir", required=True)
    ap.add_argument("--note-dir")
    a = ap.parse_args()
    figs = {"pipeline-flush": pipeline, "two-bit-counter": counter, "sorted-strip": strip, "cmov-chain": chain, "bsearch-timeline": bsearch, "hint-layout": hint_layout}
    os.makedirs(a.out_dir, exist_ok=True)
    for name, fn in figs.items():
        for lang in ("zh", "en"):
            s = fn(lang)
            p = os.path.join(a.out_dir, f"{name}.{lang}.svg")
            open(p, "w", encoding="utf-8").write(s)
            print(p)
            if lang == "zh" and a.note_dir:
                p = os.path.join(a.note_dir, f"branch-{name}.svg")
                open(p, "w", encoding="utf-8").write(s)
                print(p)


if __name__ == "__main__":
    main()
