import sys
sys.stdout.reconfigure(encoding='utf-8')

# 单调游标 H(写,只有生产者改)、T(读,只有消费者改),任何时刻 T <= H。
# 调用者做两次 load(先读 order[0],再读 order[1]),两次之间生产者/消费者可以继续推进。
# 调用者身份:
#   third    第三个线程:两次 load 之间 P、C 都可能动
#   consumer 消费者自己调用:两次 load 之间 C 不会动(它正在执行 size())
#   producer 生产者自己调用:两次 load 之间 P 不会动
def explore(caller, order, npush=4, npop=4):
    stats = {"leaves": 0, "min": None, "ex": None}

    def rec(H, T, p, c, stage, v1, trace):
        if p > 0 and not (stage == 1 and caller == "producer"):
            rec(H + 1, T, p - 1, c, stage, v1, trace + [f"P(H={H+1})"])
        if c > 0 and T < H and not (stage == 1 and caller == "consumer"):
            rec(H, T + 1, p, c - 1, stage, v1, trace + [f"C(T={T+1})"])
        if stage == 0:
            v = H if order[0] == "H" else T
            rec(H, T, p, c, 1, v, trace + [f"读{order[0]}={v}"])
        elif stage == 1:
            v2 = H if order[1] == "H" else T
            h = v1 if order[0] == "H" else v2
            t = v1 if order[0] == "T" else v2
            d = h - t
            stats["leaves"] += 1
            if stats["min"] is None or d < stats["min"]:
                stats["min"] = d
                stats["ex"] = trace + [f"读{order[1]}={v2}", f"h-t={d}"]

    rec(0, 0, npush, npop, 0, None, [])
    return stats

for caller in ("third", "consumer", "producer"):
    for order in (("H", "T"), ("T", "H")):
        s = explore(caller, order)
        tag = "先读" + order[0] + "再读" + order[1]
        print(f"调用者={caller:8s} {tag}: 共 {s['leaves']} 种交错, 最小 h-t = {s['min']}"
              + ("  ← 出现负数" if s["min"] < 0 else ""))
        if s["min"] < 0:
            print("    最小反例:", " → ".join(s["ex"]))
