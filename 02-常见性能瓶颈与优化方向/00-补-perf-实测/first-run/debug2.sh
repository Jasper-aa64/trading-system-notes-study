#!/usr/bin/env bash
# 第二轮：二进制带不带 build id，系统自带的老 GCC 编出来的又怎样
set -u
cd /tmp/perf-demo-20261022 || exit 1
mkdir -p dbg2
perf report --stdio -vv -i perf.nog.data -g none > dbg2/report_nog_vv.txt 2>&1

# c：GCC 12.2 + 显式加 build id
g++ -O2 -g -fno-omit-frame-pointer -Wl,--build-id -o demo_bid perf_demo.cpp
{ echo "== demo_bid"; readelf -n demo_bid; echo "== perf_demo"; readelf -n perf_demo; } > dbg2/notes.txt 2>&1
perf record -e cpu-clock -o perf.bid.data ./demo_bid 1 > dbg2/record_bid.txt 2>&1
perf report --stdio -i perf.bid.data > dbg2/report_bid.txt 2>&1

# d：系统自带的 g++
{ echo "== which g++"; which g++; echo "== /usr/bin/g++"; /usr/bin/g++ --version | head -1; } > dbg2/sysgcc.txt 2>&1
/usr/bin/g++ -std=c++11 -O2 -fno-omit-frame-pointer -o demo_sys perf_demo.cpp > dbg2/build_sys.txt 2>&1
{ echo "== demo_sys"; readelf -n demo_sys; } >> dbg2/notes.txt 2>&1
perf record -e cpu-clock -o perf.sys.data ./demo_sys 1 > dbg2/record_sys.txt 2>&1
perf report --stdio -i perf.sys.data > dbg2/report_sys.txt 2>&1
echo "debug2.sh done"
