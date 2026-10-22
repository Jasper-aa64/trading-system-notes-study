#!/usr/bin/env bash
# perf 专题的实测：在临时目录里编译、探测、采样，文本输出写进 out/（perf.data 不拷走，它的头里有机器信息）
set -u
D=/tmp/perf-demo-20261022
cd "$D" || exit 1
mkdir -p out
CXXFLAGS="-O2 -g -fno-omit-frame-pointer"
{
    echo "kernel: $(uname -r)"
    echo "perf: $(perf --version)"
    echo "compiler: $(g++ --version | head -1)"
    echo "flags: $CXXFLAGS"
    echo "perf_event_paranoid: $(cat /proc/sys/kernel/perf_event_paranoid)"
    echo "uid: $(id -u)"
} > out/env.txt
g++ $CXXFLAGS -o perf_demo perf_demo.cpp > out/build.txt 2>&1 || { echo "build failed"; cat out/build.txt; exit 1; }

# 0. 探测：硬件计数器拿得到吗
perf stat -e cycles,instructions -- /bin/true > out/probe.txt 2>&1
echo "exit code: $?" >> out/probe.txt

# 1. 总账：重复 10 次
perf stat -r 10 -o out/stat_serial.txt ./perf_demo 1 > out/stat_serial.stdout 2>&1

# 2. 热点函数
perf record -e cpu-clock -g -o perf.serial.data ./perf_demo 1 > out/record_serial.txt 2>&1
perf report --stdio -i perf.serial.data > out/report_serial.txt 2>&1
perf report --stdio -i perf.serial.data -g none > out/report_serial_flat.txt 2>&1

# 3. 热点指令
perf annotate --stdio -i perf.serial.data sum_serial > out/annotate_serial.txt 2>&1

# 符号表删掉以后，报告长什么样
strip -o perf_demo.stripped perf_demo
perf record -e cpu-clock -o perf.stripped.data ./perf_demo.stripped 1 > out/record_stripped.txt 2>&1
perf report --stdio -i perf.stripped.data > out/report_stripped.txt 2>&1

# 4. 修掉热点：先核结果，再比耗时
./perf_demo check > out/check.txt 2>&1
perf stat -r 10 -o out/stat_4acc.txt ./perf_demo 4 > out/stat_4acc.stdout 2>&1
perf record -e cpu-clock -g -o perf.4acc.data ./perf_demo 4 > out/record_4acc.txt 2>&1
perf report --stdio -i perf.4acc.data -g none > out/report_4acc_flat.txt 2>&1
perf annotate --stdio -i perf.4acc.data sum_4acc > out/annotate_4acc.txt 2>&1
objdump -d --no-show-raw-insn perf_demo | awk '/<sum_serial>:/,/^$/' > out/objdump_sum_serial.txt
objdump -d --no-show-raw-insn perf_demo | awk '/<sum_4acc>:/,/^$/' > out/objdump_sum_4acc.txt
echo "run.sh done"
