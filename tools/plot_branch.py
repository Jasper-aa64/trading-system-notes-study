"""把 12-branch-bench 的 CSV 画成“分支可猜程度 vs 速度”的实测图（SVG，中英文各一张）。

用法:
  python tools/plot_branch.py branch.csv cmov.csv --machine "AMD Ryzen 5 5600GT" --out-dir <目录> [--name branch-predictability]

横轴是数组里偶数的比例 p（0%–100%），纵轴是每个元素花的 ns；每个 CSV 一条线，
p = 50% 那组数排序以后的结果画成一个空心点。只用标准库。
"""
import argparse
import csv
import math
import os

TEXT = {
    "zh": dict(
        font="PingFang SC, Microsoft YaHei, Noto Sans CJK SC, sans-serif",
        title="实测：if (data[c] % 2 == 0) 的速度和数组里偶数的比例（{m}，一个核）",
        xl="数组里偶数的比例 p（随机打乱）",
        yl="每个元素花的时间（ns）",
        names={"branch": "保留分支，{n} 个数", "cmov": "-O2 默认（if 变成 cmov），{n} 个数"},
        sorted="p = 50% 排序后",
        foot="同一组数反复跑约 6700 万个元素，每个 p 测 5 次取最快；测的是这段循环，不是硬件峰值。",
    ),
    "en": dict(
        font="Helvetica, Arial, sans-serif",
        title="Measured: if (data[c] % 2 == 0) speed vs the share of even numbers ({m}, one core)",
        xl="Share of even numbers p (shuffled)",
        yl="Time per element (ns)",
        names={"branch": "branch kept, {n} numbers", "cmov": "-O2 default (if becomes cmov), {n} numbers"},
        sorted="p = 50%, sorted",
        foot="The same numbers are re-run for about 67 million elements, best of 5 per p; measures this loop, not the hardware peak.",
    ),
}
COLORS = ["#cf222e", "#fb8f44", "#0969da", "#54aeff"]


def fmt_n(n):
    return f"{n >> 20}M" if n >= 1 << 20 and n % (1 << 20) == 0 else (f"{n >> 10}K" if n % 1024 == 0 else str(n))


def nice_max(v):
    e = 10 ** math.floor(math.log10(v))
    for k in (1, 1.5, 2, 2.5, 3, 4, 5, 6, 8, 10):
        if k * e >= v:
            return k * e


def render(series, machine, lang):
    t = TEXT[lang]
    W, H = 1100, 560
    L, R, T, B = 90, 1060, 70, 460
    ymax = nice_max(max(v for s in series.values() for v in list(s["pts"].values()) + [s["sorted"] or 0]) * 1.1)

    def X(p):
        return L + p / 100 * (R - L)

    def Y(v):
        return B - v / ymax * (B - T)

    o = [
        f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W} {H}" width="{W}" height="{H}" font-family="{t["font"]}">',
        "<style>.h{font-size:19px;font-weight:700;fill:#1f2328}.ax{stroke:#57606a;stroke-width:1.2}"
        ".g{stroke:#d0d7de;stroke-width:1}.tk{font-size:12.5px;fill:#57606a}.lab{font-size:13.5px;fill:#1f2328}"
        ".ft{font-size:12px;fill:#57606a}.lg{font-size:13.5px;fill:#1f2328}</style>",
        f'<rect x="0" y="0" width="{W}" height="{H}" fill="#ffffff"/>',
        f'<text class="h" x="{L}" y="36">{t["title"].format(m=machine)}</text>',
    ]
    for k in range(6):
        v = ymax * k / 5
        o.append(f'<line class="g" x1="{L}" y1="{Y(v):.1f}" x2="{R}" y2="{Y(v):.1f}"/>')
        o.append(f'<text class="tk" x="{L - 10}" y="{Y(v) + 4:.1f}" text-anchor="end">{v:g}</text>')
    for p in range(0, 101, 10):
        o.append(f'<text class="tk" x="{X(p):.1f}" y="{B + 20}" text-anchor="middle">{p}%</text>')
    o.append(f'<line class="ax" x1="{L}" y1="{B}" x2="{R}" y2="{B}"/><line class="ax" x1="{L}" y1="{T}" x2="{L}" y2="{B}"/>')
    ly = T + 10
    for k, ((name, n), s) in enumerate(series.items()):
        c = COLORS[k % len(COLORS)]
        pts = sorted(s["pts"].items())
        o.append(f'<path d="M' + " L".join(f"{X(p):.1f},{Y(v):.1f}" for p, v in pts) + f'" stroke="{c}" stroke-width="2.5" fill="none"/>')
        for p, v in pts:
            o.append(f'<circle cx="{X(p):.1f}" cy="{Y(v):.1f}" r="3" fill="{c}"/>')
        if s["sorted"] is not None:
            o.append(f'<circle cx="{X(50):.1f}" cy="{Y(s["sorted"]):.1f}" r="7" fill="#ffffff" stroke="{c}" stroke-width="2.5"/>')
        o.append(f'<line x1="{R - 330}" y1="{ly}" x2="{R - 300}" y2="{ly}" stroke="{c}" stroke-width="2.5"/>')
        o.append(f'<text class="lg" x="{R - 292}" y="{ly + 5}">{t["names"].get(name, name).format(n=fmt_n(n))}</text>')
        ly += 24
    o.append(f'<circle cx="{R - 315}" cy="{ly}" r="7" fill="#ffffff" stroke="#57606a" stroke-width="2.5"/>')
    o.append(f'<text class="lg" x="{R - 292}" y="{ly + 5}">{t["sorted"]}</text>')
    o.append(f'<text class="lab" x="{(L + R) / 2}" y="{B + 46}" text-anchor="middle">{t["xl"]}</text>')
    o.append(f'<text class="lab" transform="translate(30,{(T + B) / 2}) rotate(-90)" text-anchor="middle">{t["yl"]}</text>')
    o.append(f'<text class="ft" x="{R}" y="{H - 14}" text-anchor="end">{t["foot"]}</text>')
    o.append("</svg>")
    return "\n".join(o) + "\n"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("csv", nargs="+")
    ap.add_argument("--machine", required=True)
    ap.add_argument("--out-dir", required=True)
    ap.add_argument("--name", default="branch-predictability")
    a = ap.parse_args()
    series = {}
    for path in a.csv:  # 每个 CSV 里可以是一个或多个 (variant, n)
        with open(path, newline="") as f:
            for r in csv.DictReader(f):
                s = series.setdefault((r["variant"], int(r["n"])), {"pts": {}, "sorted": None})
                if r["p"] == "sorted":
                    s["sorted"] = float(r["ns_per_elem"])
                else:
                    s["pts"][int(r["p"])] = float(r["ns_per_elem"])
    os.makedirs(a.out_dir, exist_ok=True)
    for lang in ("zh", "en"):
        p = os.path.join(a.out_dir, f"{a.name}.{lang}.svg")
        with open(p, "w", encoding="utf-8") as f:
            f.write(render(series, a.machine, lang))
        print(p)


if __name__ == "__main__":
    main()
