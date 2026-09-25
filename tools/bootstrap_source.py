#!/usr/bin/env python3
"""Rebuild the source notes that every `:line` anchor in this folder points to.

Links in these notes look like `../../trading-system-notes/chinese/01-low-latency/10-....md:23` (from a
sub-folder) or `../trading-system-notes/chinese/...` (from the repository root): the source notes live in
a folder named `trading-system-notes/` NEXT TO this repository's directory. That folder is a clone of the
upstream repo (zzxscodes/trading-system-notes) and is NOT stored in this repository. Its `chinese/`
sub-folder is not in upstream either: it is generated from upstream's single-file Chinese notes by
`_split_notes.py` (next to this script), so the line numbers only match if that exact split is rebuilt.

This script clones upstream at the pinned commit, copies the single-file Chinese notes to the
`*.full.md` name the splitter reads, and runs the splitter.

    python tools/bootstrap_source.py [--upstream <url-or-local-path>]

It never overwrites anything: if the target folder already exists it only reports whether it is usable
(pinned commit checked out + chinese/ present) and exits 0 (usable) or 1 (not).
"""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
from pathlib import Path

UPSTREAM = "https://github.com/zzxscodes/trading-system-notes.git"
PIN = "9a8f2f6240a21682deaf7b5437594f4bffeff00e"  # 2026-07-24, "add FIX zero-copy FieldView / tag-indexed lookup"

HERE = Path(__file__).resolve().parent
DEST = HERE.parent.parent / "trading-system-notes"  # sibling of this repository's directory
SPLITTER = HERE / "_split_notes.py"


def git_head(repo: Path) -> str:
    if not (repo / ".git").exists():
        return "(not a git clone)"
    return subprocess.run(["git", "rev-parse", "HEAD"], cwd=repo, check=True, capture_output=True, text=True).stdout.strip()


def check_existing() -> int:
    head = git_head(DEST)
    split_ok = (DEST / "chinese" / "01-low-latency").is_dir()
    ok = head == PIN and split_ok
    print(f"{DEST} already exists: HEAD {head[:12]} (pinned {PIN[:12]}), "
          f"chinese/ {'present' if split_ok else 'MISSING'} -> {'usable' if ok else 'NOT usable'}")
    if not ok:
        print("Move or delete that folder yourself and re-run; this script never overwrites.")
    return 0 if ok else 1


def main() -> int:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--upstream", default=UPSTREAM, help="clone source (default: the GitHub URL)")
    args = ap.parse_args()

    if DEST.exists():
        return check_existing()

    subprocess.run(["git", "clone", "--quiet", args.upstream, str(DEST)], check=True)
    subprocess.run(["git", "checkout", "--quiet", PIN], cwd=DEST, check=True)
    shutil.copyfile(DEST / "trading-system-notes-Chinese.md", DEST / "trading-system-notes-Chinese.full.md")
    shutil.copyfile(SPLITTER, DEST / "_split_notes.py")
    subprocess.run([sys.executable, "_split_notes.py", "zh"], cwd=DEST, check=True)
    print(f"done: {DEST}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
