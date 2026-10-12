"""画速查“C++ 语法”2.2 的一张图：装饰器（3 个堆对象 + 指针 + 虚调用）vs Mixin（1 个对象，层是基类）。

用法:
  python tools/draw_mixin_fig.py --out-dir <博客仓库>/static/images/ref [--note-dir docs]

对象大小是 13-mixin-check.cpp 的 x86-64 实测（sizeof）。--note-dir 给了就把中文版也写一份（docs/mixin-vs-decorator.svg）。
"""
import argparse
import os

from draw_branch_figs import svg, text

T = {
    "zh": dict(
        title="同一个迭代器的两种叠法：DoubleValue → OddOnly → MatrixWalk",
        a="装饰器：3 个对象，各自 new 在堆上，靠 inner 指针连起来",
        b="Mixin：DoubleValue<OddOnly<MatrixWalk>> 是 1 个对象，每层是它的一个基类",
        heap="堆上一次分配",
        call="it->cell() 的一次调用",
        hops=["读 vptr → 读 vtable → 间接调用", "读 inner → 读 vptr → 间接调用", "读 inner → 读 vptr → 间接调用"],
        na=["每往下一层：多两三次读内存，外加一次间接调用。同一处调用的目标固定，通常猜得对，只多几个周期；猜错一次约 15–20 个周期（约 4–5 ns）。",
            "更大的代价：编译器不知道 inner 指向谁，没法把下一层内联进来，每层都是一次真正的函数调用。"],
        layers=["DoubleValue（不加成员，只改 cell()）", "OddOnly（不加成员，只改 next()）", "MatrixWalk"],
        nb=["没有 vptr、没有 inner 指针、没有间接调用：Base::cell()、Base::next() 都是编译期定好的直接调用，可以一路内联进调用方。",
            "内联后 cell() 是“读当前元素、乘 2”几条指令；next() 是一个循环，筛奇数的那条条件跳转还在（静态多态省掉的是选函数的跳转，不是数据上的判断）。"],
        size="sizeof = 32 字节，放在栈上",
    ),
    "en": dict(
        title="Two ways to stack the same iterator: DoubleValue → OddOnly → MatrixWalk",
        a="Decorator: 3 objects, each new'd on the heap, linked by inner pointers",
        b="Mixin: DoubleValue<OddOnly<MatrixWalk>> is 1 object; each layer is one of its base classes",
        heap="one heap allocation",
        call="one call to it->cell()",
        hops=["load vptr → load vtable → indirect call", "load inner → load vptr → indirect call", "load inner → load vptr → indirect call"],
        na=["Each layer down: two or three more memory loads plus an indirect call. The target at a given call site is fixed, so it's usually predicted right and costs a few cycles; a mispredict is about 15–20 cycles (about 4–5 ns).",
            "The bigger cost: the compiler doesn't know what inner points to, so it can't inline the next layer; every layer is a real function call."],
        layers=["DoubleValue (no members, changes cell() only)", "OddOnly (no members, changes next() only)", "MatrixWalk"],
        nb=["No vptr, no inner pointer, no indirect call: Base::cell() and Base::next() are direct calls fixed at compile time and inline all the way into the caller.",
            "After inlining, cell() is a few instructions (load the current element, multiply by 2); next() is a loop, and the conditional jump of the odd filter is still there (static polymorphism removes the jump that picks the function, not the decisions on the data)."],
        size="sizeof = 32 bytes, lives on the stack",
    ),
}


def wrap(s, n):
    """按字符数粗略折行（中文按 1 个字符 ≈ 2 个英文宽度算）。"""
    out, line, w = [], "", 0
    for ch in s:
        cw = 2 if ord(ch) > 0x2E80 else 1
        if w + cw > n and ch not in "，。；：、）”" and (ch == " " or cw == 2 or ord(line[-1:] or "a") > 0x2E80):
            out.append(line.rstrip())
            line, w = "", 0
            if ch == " ":
                continue
        line += ch
        w += cw
    if line:
        out.append(line)
    return out


def fig(lang):
    t = T[lang]
    W, H = 1240, 790
    o = [text(20, 34, t["title"], "h")]

    # ---- 上：装饰器
    o.append(text(20, 74, t["a"], "t"))
    names = ["DoubleValueDeco", "OddOnlyDeco", "MatrixWalkDeco"]
    fields = [["vptr", "inner"], ["vptr", "inner"], ["vptr", "rows_ / r_ / c_"]]
    sizes = ["16 B", "16 B", "40 B"]
    BX, BW, BY, BH, GAP = 40, 300, 92, 120, 110
    for i in range(3):
        x = BX + i * (BW + GAP)
        o.append(f'<rect x="{x}" y="{BY}" width="{BW}" height="{BH}" rx="8" fill="#ddf4ff" stroke="#54aeff" stroke-width="1.5"/>')
        o.append(text(x + 14, BY + 24, names[i], "c"))
        o.append(text(x + BW - 14, BY + 24, sizes[i], "k", "end"))
        for j, f in enumerate(fields[i]):
            fy = BY + 38 + j * 36
            fill = "#fff1c2" if f == "vptr" else "#ffffff"
            o.append(f'<rect x="{x + 14}" y="{fy}" width="{BW - 28}" height="28" rx="4" fill="{fill}" stroke="#afb8c1"/>')
            o.append(text(x + 26, fy + 19, f, "s"))
        o.append(text(x + BW / 2, BY + BH + 20, t["heap"], "k", "middle"))
        if i < 2:
            y = BY + 38 + 36 + 14
            o.append(f'<line x1="{x + BW - 20}" y1="{y}" x2="{x + BW + GAP - 4}" y2="{y}" stroke="#57606a" stroke-width="1.5" marker-end="url(#a)"/>')

    # 一次调用往下走的路径
    cy = 270
    o.append(text(20, cy, t["call"], "t"))
    for i in range(3):
        x = BX + i * (BW + GAP)
        o.append(f'<rect x="{x}" y="{cy + 12}" width="{BW}" height="34" rx="6" fill="#ffebe9" stroke="#ff8182"/>')
        o.append(text(x + BW / 2, cy + 34, t["hops"][i], "s", "middle"))
        if i < 2:
            o.append(f'<line x1="{x + BW + 6}" y1="{cy + 29}" x2="{x + BW + GAP - 6}" y2="{cy + 29}" stroke="#cf222e" stroke-width="1.5" marker-end="url(#a)"/>')
    y = cy + 76
    for para in t["na"]:
        for ln in wrap(para, 150):
            o.append(text(20, y, ln, "s"))
            y += 20
        y += 4

    # ---- 下：Mixin
    y0 = y + 26
    o.append(f'<line x1="20" y1="{y0 - 22}" x2="{W - 20}" y2="{y0 - 22}" stroke="#d0d7de"/>')
    o.append(text(20, y0, t["b"], "t"))
    ox, ow, oy, oh = 40, 760, y0 + 18, 196
    colors = [("#dafbe1", "#4ac26b"), ("#eefcf1", "#4ac26b"), ("#ffffff", "#4ac26b")]
    for k in range(3):
        x, yy, w, h = ox + k * 22, oy + k * 46, ow - k * 44, oh - k * 58
        o.append(f'<rect x="{x}" y="{yy}" width="{w}" height="{h}" rx="8" fill="{colors[k][0]}" stroke="{colors[k][1]}" stroke-width="1.5"/>')
        o.append(text(x + 14, yy + 26, t["layers"][k], "c"))
    fx = ox + 2 * 22 + 14
    fy = oy + 2 * 46 + 40
    for j, f in enumerate(["rows_", "r_", "c_"]):
        o.append(f'<rect x="{fx + j * 150}" y="{fy}" width="136" height="28" rx="4" fill="#ffffff" stroke="#afb8c1"/>')
        o.append(text(fx + j * 150 + 12, fy + 19, f, "s"))
    o.append(text(ox + ow, oy + oh + 20, t["size"], "k", "end"))
    nx, ny = ox + ow + 30, oy + 8
    for para in t["nb"]:
        for ln in wrap(para, 46):
            o.append(text(nx, ny, ln, "s"))
            ny += 20
        ny += 8
    H = max(oy + oh + 40, ny + 10)
    return svg(lang, W, int(H), o)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out-dir", required=True)
    ap.add_argument("--note-dir")
    a = ap.parse_args()
    os.makedirs(a.out_dir, exist_ok=True)
    for lang in ("zh", "en"):
        s = fig(lang)
        p = os.path.join(a.out_dir, f"mixin-vs-decorator.{lang}.svg")
        open(p, "w", encoding="utf-8").write(s)
        print(p)
        if lang == "zh" and a.note_dir:
            p = os.path.join(a.note_dir, "mixin-vs-decorator.svg")
            open(p, "w", encoding="utf-8").write(s)
            print(p)


if __name__ == "__main__":
    main()
