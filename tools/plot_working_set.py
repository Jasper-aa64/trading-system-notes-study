"""把 11-补-working-set-bench 的 CSV 画成工作集台阶的实测图（SVG，中英文各一张）。

用法:
  python tools/plot_working_set.py ws.csv --machine "AMD Ryzen 5 5600GT" \
      --l1 32K --l2 512K --l3 16M --out-dir <目录> [--name working-set-measured]

输出 <目录>/<name>.zh.svg 和 <目录>/<name>.en.svg。只用标准库。
"""
import argparse
import csv
import math
import os


def parse_size(s):
    s = s.strip().upper().rstrip("B").rstrip("I")
    mul = {"K": 1 << 10, "M": 1 << 20, "G": 1 << 30}
    return int(float(s[:-1]) * mul[s[-1]]) if s[-1] in mul else int(s)


def fmt_size(b):
    for unit, v in (("MiB", 1 << 20), ("KiB", 1 << 10)):
        if b >= v:
            x = b / v
            return f"{x:g} {unit}"
    return f"{b} B"


TEXT = {
    "zh": dict(
        font="PingFang SC, Microsoft YaHei, Noto Sans CJK SC, sans-serif",
        title="实测：反复顺序求和一个 int 数组（{m}，一个核）",
        xl="工作集大小（对数坐标）",
        yl="一个核每秒读多少（GB/s，对数坐标）",
        foot="每个大小测 5 次取最快；4 个向量累加器；测的是这段循环，不是硬件峰值。",
    ),
    "en": dict(
        font="Helvetica, Arial, sans-serif",
        title="Measured: summing an int array over and over ({m}, one core)",
        xl="Working-set size (log scale)",
        yl="Bytes read per second by one core (GB/s, log scale)",
        foot="Best of 5 runs per size; 4 vector accumulators; measures this loop, not the hardware peak.",
    ),
}


def render(rows, machine, caps, lang):
    t = TEXT[lang]
    W, H = 1100, 550
    L, R, T, B = 90, 1060, 70, 470
    xs = [math.log2(b) for b, _ in rows]
    x0, x1 = math.floor(min(xs)), math.ceil(max(xs))
    vals = [g for _, g in rows]
    ylo = 10 ** math.floor(math.log10(min(vals)))
    yhi = 10 ** math.ceil(math.log10(max(vals)))
    y0, y1 = math.log10(ylo), math.log10(yhi)

    def X(lb):
        return L + (lb - x0) / (x1 - x0) * (R - L)

    def Y(g):
        return B - (math.log10(g) - y0) / (y1 - y0) * (B - T)

    o = [
        f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W} {H}" width="{W}" height="{H}" font-family="{t["font"]}">',
        "<style>.h{font-size:19px;font-weight:700;fill:#1f2328}.ax{stroke:#57606a;stroke-width:1.2}"
        ".g{stroke:#d0d7de;stroke-width:1}.tk{font-size:12.5px;fill:#57606a}.lab{font-size:13.5px;fill:#1f2328}"
        ".cap{stroke:#9a6700;stroke-width:1.3;stroke-dasharray:5 4}.capt{font-size:12.5px;fill:#9a6700;font-weight:600}"
        ".cur{stroke:#0969da;stroke-width:2.5;fill:none}.pt{fill:#0969da}.ft{font-size:12px;fill:#57606a}</style>",
        f'<rect x="0" y="0" width="{W}" height="{H}" fill="#ffffff"/>',
        f'<text class="h" x="{L}" y="36">{t["title"].format(m=machine)}</text>',
    ]
    d = 1
    while d <= yhi:
        for k in (1, 2, 5):
            v = d * k
            if ylo <= v <= yhi:
                o.append(f'<line class="g" x1="{L}" y1="{Y(v):.1f}" x2="{R}" y2="{Y(v):.1f}"/>')
                o.append(f'<text class="tk" x="{L - 10}" y="{Y(v) + 4:.1f}" text-anchor="end">{v:g}</text>')
        d *= 10
    for lb in range(x0, x1 + 1, 2):
        o.append(f'<text class="tk" x="{X(lb):.1f}" y="{B + 20}" text-anchor="middle">{fmt_size(2 ** lb)}</text>')
    o.append(f'<line class="ax" x1="{L}" y1="{B}" x2="{R}" y2="{B}"/><line class="ax" x1="{L}" y1="{T}" x2="{L}" y2="{B}"/>')
    for name, c in caps:
        if x0 < math.log2(c) < x1:
            cx = X(math.log2(c))
            o.append(f'<line class="cap" x1="{cx:.1f}" y1="{T + 10}" x2="{cx:.1f}" y2="{B}"/>')
            o.append(f'<text class="capt" x="{cx + 6:.1f}" y="{T + 24}">{name} {fmt_size(c)}</text>')
    path = "M" + " L".join(f"{X(math.log2(b)):.1f},{Y(g):.1f}" for b, g in rows)
    o.append(f'<path class="cur" d="{path}"/>')
    for b, g in rows:
        o.append(f'<circle class="pt" cx="{X(math.log2(b)):.1f}" cy="{Y(g):.1f}" r="2.6"/>')
    o.append(f'<text class="lab" x="{(L + R) / 2}" y="{B + 46}" text-anchor="middle">{t["xl"]}</text>')
    o.append(f'<text class="lab" transform="translate(28,{(T + B) / 2}) rotate(-90)" text-anchor="middle">{t["yl"]}</text>')
    o.append(f'<text class="ft" x="{R}" y="{H - 14}" text-anchor="end">{t["foot"]}</text>')
    o.append("</svg>")
    return "\n".join(o) + "\n"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("csv")
    ap.add_argument("--machine", required=True)
    ap.add_argument("--l1", required=True)
    ap.add_argument("--l2", required=True)
    ap.add_argument("--l3", required=True)
    ap.add_argument("--out-dir", required=True)
    ap.add_argument("--name", default="working-set-measured")
    a = ap.parse_args()
    with open(a.csv, newline="") as f:
        rows = [(int(r["bytes"]), float(r["gbps"])) for r in csv.DictReader(f)]
    caps = [("L1D", parse_size(a.l1)), ("L2", parse_size(a.l2)), ("L3", parse_size(a.l3))]
    os.makedirs(a.out_dir, exist_ok=True)
    for lang in ("zh", "en"):
        p = os.path.join(a.out_dir, f"{a.name}.{lang}.svg")
        with open(p, "w", encoding="utf-8") as f:
            f.write(render(rows, a.machine, caps, lang))
        print(p)


if __name__ == "__main__":
    main()
