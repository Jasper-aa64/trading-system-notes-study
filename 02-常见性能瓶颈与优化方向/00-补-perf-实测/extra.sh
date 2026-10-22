#!/usr/bin/env bash
# 补测：不加 -g 时函数名还在不在；按 DWARF 回溯时调用链里有没有 main
set -u
cd /tmp/demo-perf-20261022 || exit 1
mkdir -p extra
g++ -O2 -fno-omit-frame-pointer -o perf_demo_nog perf_demo.cpp
perf record -e cpu-clock -o perf.nog.data ./perf_demo_nog 1 > extra/record_nog.txt 2>&1
perf report --stdio -i perf.nog.data > extra/report_nog.txt 2>&1
perf annotate --stdio -i perf.nog.data sum_serial > extra/annotate_nog.txt 2>&1
perf record -e cpu-clock --call-graph dwarf -o perf.dwarf.data ./perf_demo 1 > extra/record_dwarf.txt 2>&1
perf report --stdio -i perf.dwarf.data > extra/report_dwarf.txt 2>&1
echo "extra.sh done"
