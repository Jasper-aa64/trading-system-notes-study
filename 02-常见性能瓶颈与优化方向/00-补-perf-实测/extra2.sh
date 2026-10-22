#!/usr/bin/env bash
# 补测：调试信息改成 DWARF 4 以后，按 DWARF 回溯时还报不报 BFD 的错
set -u
cd /tmp/demo-perf-20261022 || exit 1
mkdir -p extra2
g++ -O2 -g -gdwarf-4 -fno-omit-frame-pointer -o perf_demo_dw4 perf_demo.cpp
perf record -e cpu-clock --call-graph dwarf -o perf.dw4.data ./perf_demo_dw4 1 > extra2/record_dw4_dwarf.txt 2>&1
perf report --stdio -i perf.dw4.data > extra2/report_dw4_dwarf.txt 2>&1
echo "extra2.sh done"
