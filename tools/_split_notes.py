#!/usr/bin/env python3
"""Split trading-system-notes markdown into chapter/section files with index."""

from __future__ import annotations

import argparse
import re
import shutil
from pathlib import Path

ROOT = Path(__file__).resolve().parent

LOCALES: dict[str, dict] = {
    "zh": {
        "source": "trading-system-notes-Chinese.full.md",
        "out": "chinese",
        "skip_headings": {"目录"},
        "chapters": {
            "低延迟系统开发基础": "01-low-latency",
            "常见性能瓶颈与优化方向": "02-performance-bottlenecks",
            "简单实践": "03-simple-practice",
            "交易数据处理": "04-trading-data",
            "业务逻辑设计-系统": "05-system-design",
            "业务逻辑设计-计算": "06-computation-design",
            "数字货币": "07-crypto",
        },
        "index_title": "# 交易系统开发笔记（中文 · 分章索引）",
        "source_link": "完整单文件版：[`trading-system-notes-Chinese.full.md`](../trading-system-notes-Chinese.full.md)",
        "blurb": "> 按 **7 大章 · 94 小节** 拆分，便于分主题阅读与全文检索。",
        "sections_header": "## 章节",
        "table_header": ("| 章 | 小节数 | 入口 |", "|---|---:|---|"),
        "quick_header": "## 快速跳转",
        "chapter_readme_back": "[← 返回总索引](../README.md)",
        "chapter_toc_header": "## 本章目录",
        "nav_back_chapter": "← 返回 `{title}` 目录",
        "nav_back_index": "返回总索引",
    },
    "en": {
        "source": "trading-system-notes-English.full.md",
        "out": "english",
        "skip_headings": {"Table of Contents"},
        "chapters": {
            "Low latency system development basics": "01-low-latency",
            "Common performance bottlenecks and optimization directions": "02-performance-bottlenecks",
            "Simple practice": "03-simple-practice",
            "Transaction data processing": "04-trading-data",
            "Business logic design-system": "05-system-design",
            "Business logic design-calculation": "06-computation-design",
            "digital currency": "07-crypto",
        },
        "index_title": "# Trading System Notes (English · split index)",
        "source_link": "Full single file: [`trading-system-notes-English.full.md`](../trading-system-notes-English.full.md)",
        "blurb": "> Split into **7 chapters · 94 sections** for topic-based reading and search.",
        "sections_header": "## Chapters",
        "table_header": ("| Chapter | Sections | Index |", "|---|---:|---|"),
        "quick_header": "## Quick links",
        "chapter_readme_back": "[← Back to index](../README.md)",
        "chapter_toc_header": "## Sections",
        "nav_back_chapter": "← Back to `{title}` index",
        "nav_back_index": "Back to main index",
    },
}


def slugify(title: str) -> str:
    """Turn '### 1. CPU affinity and NUMA architecture' into '01-cpu-affinity-and-numa-architecture'."""
    m = re.match(r"^###\s*(\d+)\.\s*(.+)$", title.strip())
    if not m:
        num, rest = "00", title.strip().lstrip("#").strip()
    else:
        num, rest = m.group(1).zfill(2), m.group(2)

    rest = re.sub(r"\*\*", "", rest)
    rest = rest.replace("（", "-").replace("）", "").replace("(", "-").replace(")", "")
    rest = rest.replace("、", "-").replace("，", "-").replace("：", "-").replace(":", "-")
    rest = rest.replace("＜-＞", "-").replace("<->", "-").replace("/", "-")
    rest = rest.replace(" ", "-").replace("--", "-")

    ascii_parts: list[str] = []
    cjk_buf: list[str] = []
    for ch in rest:
        if ch.isascii() and (ch.isalnum() or ch == "-"):
            if cjk_buf:
                ascii_parts.append("".join(cjk_buf))
                cjk_buf = []
            ascii_parts.append(ch.lower())
        elif "\u4e00" <= ch <= "\u9fff":
            cjk_buf.append(ch)
        elif ch == "-":
            if cjk_buf:
                ascii_parts.append("".join(cjk_buf))
                cjk_buf = []
            ascii_parts.append("-")
    if cjk_buf:
        ascii_parts.append("".join(cjk_buf))

    slug = "".join(ascii_parts)
    slug = re.sub(r"-+", "-", slug).strip("-")
    if len(slug) > 48:
        slug = slug[:48].rstrip("-")
    return f"{num}-{slug}" if slug else num


def is_skippable_heading(title: str) -> bool:
    title = title.strip()
    return not title or title.startswith("<!--") and title.endswith("-->")


def iter_headings(lines: list[str], skip: set[str]) -> list[tuple[int, int, str, str]]:
    in_fence = False
    out: list[tuple[int, int, str, str]] = []
    for i, line in enumerate(lines):
        if line.startswith("```"):
            in_fence = not in_fence
            continue
        if in_fence:
            continue
        m = re.match(r"^(#{2,3})\s+(.+)$", line)
        if m and len(m.group(1)) in (2, 3):
            title = m.group(2).strip()
            if title in skip or is_skippable_heading(title):
                continue
            out.append((i, len(m.group(1)), line, title))
    return out


def split_source(source: Path, chapter_slugs: dict[str, str], skip: set[str]) -> dict:
    lines = source.read_text(encoding="utf-8").splitlines(keepends=True)
    headings = iter_headings(lines, skip)

    first_chapter = next(i for i, lvl, _, _ in headings if lvl == 2)
    preamble = "".join(lines[:first_chapter])

    chapters: list[dict] = []
    chapter_starts = [h for h in headings if h[1] == 2]

    for ci, (start_idx, _, _, chapter_title) in enumerate(chapter_starts):
        end_idx = chapter_starts[ci + 1][0] if ci + 1 < len(chapter_starts) else len(lines)
        chapter_headings = [h for h in headings if start_idx <= h[0] < end_idx and h[1] == 3]
        slug = chapter_slugs.get(chapter_title, f"{ci + 1:02d}-chapter")
        sections: list[dict] = []

        for si, (sec_start, _, _, sec_title) in enumerate(chapter_headings):
            sec_end = chapter_headings[si + 1][0] if si + 1 < len(chapter_headings) else end_idx
            body = "".join(lines[sec_start:sec_end])
            sections.append(
                {
                    "filename": f"{slugify('### ' + sec_title)}.md",
                    "title": sec_title,
                    "content": body,
                }
            )

        chapters.append({"title": chapter_title, "slug": slug, "sections": sections})

    return {"preamble": preamble, "chapters": chapters}


def write_outputs(data: dict, out_dir: Path, cfg: dict) -> None:
    if out_dir.exists():
        shutil.rmtree(out_dir)
    out_dir.mkdir(parents=True)

    (out_dir / "00-preface.md").write_text(data["preamble"].rstrip() + "\n", encoding="utf-8")

    index_lines = [
        cfg["index_title"],
        "",
        "Source: [zzxscodes/trading-system-notes](https://github.com/zzxscodes/trading-system-notes)",
        "",
        cfg["source_link"],
        "",
        cfg["blurb"],
        "",
        cfg["sections_header"],
        "",
        cfg["table_header"][0],
        cfg["table_header"][1],
    ]

    for ch in data["chapters"]:
        ch_dir = out_dir / ch["slug"]
        ch_dir.mkdir(parents=True)
        ch_readme = [
            f"# {ch['title']}",
            "",
            cfg["chapter_readme_back"],
            "",
            cfg["chapter_toc_header"],
            "",
        ]

        for sec in ch["sections"]:
            path = ch_dir / sec["filename"]
            nav_ch = cfg["nav_back_chapter"].format(title=ch["title"])
            nav = (
                f"\n---\n\n[{nav_ch}](README.md) · "
                f"[{cfg['nav_back_index']}](../README.md)\n"
            )
            path.write_text(sec["content"].rstrip() + nav, encoding="utf-8")
            ch_readme.append(f"- [{sec['title']}]({sec['filename']})")

        ch_readme.append("")
        (ch_dir / "README.md").write_text("\n".join(ch_readme), encoding="utf-8")
        n = len(ch["sections"])
        index_lines.append(
            f"| {ch['title']} | {n} | [{ch['slug']}/README.md]({ch['slug']}/README.md) |"
        )

    index_lines.extend(["", cfg["quick_header"], ""])
    for ch in data["chapters"]:
        index_lines.append(f"- **{ch['title']}** — [{ch['slug']}/]({ch['slug']}/README.md)")

    (out_dir / "README.md").write_text("\n".join(index_lines).rstrip() + "\n", encoding="utf-8")


def run_locale(locale: str) -> None:
    cfg = LOCALES[locale]
    source = ROOT / cfg["source"]
    out_dir = ROOT / cfg["out"]
    if not source.exists():
        raise SystemExit(f"Source not found: {source}")
    data = split_source(source, cfg["chapters"], cfg["skip_headings"])
    write_outputs(data, out_dir, cfg)
    total = sum(len(c["sections"]) for c in data["chapters"])
    print(f"[{locale}] Wrote {len(data['chapters'])} chapters, {total} sections under {out_dir}")


def main() -> None:
    parser = argparse.ArgumentParser(description="Split trading-system-notes by locale.")
    parser.add_argument(
        "locale",
        nargs="?",
        default="all",
        choices=["zh", "en", "all"],
        help="Locale to split (default: all)",
    )
    args = parser.parse_args()
    targets = list(LOCALES) if args.locale == "all" else [args.locale]
    for loc in targets:
        run_locale(loc)


if __name__ == "__main__":
    main()
