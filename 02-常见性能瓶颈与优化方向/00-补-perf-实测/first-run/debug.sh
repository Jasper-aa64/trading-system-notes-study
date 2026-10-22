#!/usr/bin/env bash
# 查 perf 为什么解析不出 perf_demo 的符号：看 ELF 和 DWARF 版本，再比三种编法
set -u
cd /tmp/perf-demo-20261022 || exit 1
mkdir -p dbg
{
    echo "== file"; file perf_demo
    echo "== ELF type"; readelf -h perf_demo | grep -E "Type|Machine"
    echo "== DWARF version of the first CU"; readelf --debug-dump=info perf_demo 2>&1 | grep -m2 -E "Version"
    echo "== symbols"; nm -C perf_demo | grep -E " (sum_|refresh|main)"
    echo "== sections"; readelf -S -W perf_demo | grep -E "\.(text|symtab|debug_info|debug_line|note)"
    echo "== ld"; ld --version | head -1
    echo "== g++ -print-prog-name=ld"; g++ -print-prog-name=ld
    echo "== elfutils"; rpm -q elfutils-libelf elfutils-libs 2>&1
} > dbg/elfinfo.txt 2>&1

perf report --stdio -v -i perf.serial.data -g none > dbg/report_serial_verbose.txt 2>&1

# a：不加 -g
g++ -O2 -fno-omit-frame-pointer -o demo_nog perf_demo.cpp
perf record -e cpu-clock -o perf.nog.data ./demo_nog 1 > dbg/record_nog.txt 2>&1
perf report --stdio -i perf.nog.data > dbg/report_nog.txt 2>&1

# b：-g，但调试信息用 DWARF 4
g++ -O2 -g -gdwarf-4 -fno-omit-frame-pointer -o demo_dw4 perf_demo.cpp
perf record -e cpu-clock -o perf.dw4.data ./demo_dw4 1 > dbg/record_dw4.txt 2>&1
perf report --stdio -i perf.dw4.data > dbg/report_dw4.txt 2>&1
echo "debug.sh done"
