# 用法:python 30-sticky-counter-model-check.py base|m1|m2|m3(说明见 30-wait-free编程.md §5)
import sys
sys.stdout.reconfigure(encoding='utf-8')

IS_ZERO = 1 << 63
HELPED = 1 << 62
FLAGS = IS_ZERO | HELPED


def fmt(v):
    parts = []
    if v & IS_ZERO:
        parts.append("Z")
    if v & HELPED:
        parts.append("H")
    low = v & ~FLAGS
    return ("+".join(parts) + "|" + str(low)) if parts else str(low)


class Mem:
    def __init__(self, v):
        self.v = v


class Op:
    def __init__(self, tid):
        self.tid = tid
        self.first = None
        self.last = None
        self.res = None
        self.done = False

    def finish(self, r):
        self.res = r
        self.done = True


class Inc(Op):
    kind = "I"

    def step(self, m, tr):
        old = m.v
        m.v += 1
        self.finish((old & IS_ZERO) == 0)
        tr.append(f"T{self.tid} 加:fetch_add 旧值={fmt(old)} 现值={fmt(m.v)} 返回 {self.res}")


class Dec(Op):
    kind = "D"

    def __init__(self, tid, variant="orig"):
        super().__init__(tid)
        self.pc = 0
        self.variant = variant

    def step(self, m, tr):
        if self.pc == 0:
            old = m.v
            m.v -= 1
            if old == 1:
                self.pc = 1
                tr.append(f"T{self.tid} 减:fetch_sub 旧值=1 现值={fmt(m.v)}(是我减到 0 的,去宣告)")
            else:
                self.finish(False)
                tr.append(f"T{self.tid} 减:fetch_sub 旧值={fmt(old)} 现值={fmt(m.v)} 不是最后一个,返回 false")
        elif self.pc == 1:
            if m.v == 0:
                m.v = IS_ZERO
                self.finish(True)
                tr.append(f"T{self.tid} 减:CAS(0→Z) 成功 现值={fmt(m.v)} 返回 true")
            else:
                e = m.v
                if (e & HELPED) and self.variant != "no_help_check":
                    if self.variant == "no_exchange":
                        self.finish(True)
                        tr.append(f"T{self.tid} 减:CAS(0→Z) 失败读到 {fmt(e)},带 H,直接返回 true(变体:没有 exchange)")
                    else:
                        self.pc = 2
                        tr.append(f"T{self.tid} 减:CAS(0→Z) 失败读到 {fmt(e)},带 H,去 exchange 认领")
                else:
                    self.finish(False)
                    tr.append(f"T{self.tid} 减:CAS(0→Z) 失败读到 {fmt(e)},不带 H,返回 false")
        else:
            old = m.v
            m.v = IS_ZERO
            self.finish(bool(old & HELPED))
            tr.append(
                f"T{self.tid} 减:exchange(Z) 取回旧值 {fmt(old)} 现值={fmt(m.v)} "
                f"{'旧值带 H → 认领成功,返回 true' if old & HELPED else '旧值已不带 H(被别人认领了) → 返回 false'}"
            )


class Read(Op):
    kind = "R"

    def __init__(self, tid, variant="orig"):
        super().__init__(tid)
        self.pc = 0
        self.variant = variant

    def _value(self, v):
        return 0 if (v & IS_ZERO) else (v & ~FLAGS)

    def step(self, m, tr):
        if self.pc == 0:
            val = m.v
            if val == 0:
                if self.variant == "no_help":
                    self.finish(0)
                    tr.append(f"T{self.tid} 读:load 得 0,直接返回 0(变体:不帮忙固化)")
                else:
                    self.pc = 1
                    tr.append(f"T{self.tid} 读:load 得 0,去帮忙固化")
            else:
                self.finish(self._value(val))
                tr.append(f"T{self.tid} 读:load 得 {fmt(val)} 返回 {self.res}")
        else:
            if m.v == 0:
                m.v = IS_ZERO | HELPED
                self.finish(0)
                tr.append(f"T{self.tid} 读:CAS(0→Z+H) 成功 现值={fmt(m.v)} 返回 0")
            else:
                v = m.v
                self.finish(self._value(v))
                tr.append(f"T{self.tid} 读:CAS(0→Z+H) 失败读到 {fmt(v)} 返回 {self.res}")


class Thread:
    def __init__(self, tid, script):
        self.tid = tid
        self.g = script(tid)
        self.finished = False
        self.cur = None
        try:
            self.cur = next(self.g)
        except StopIteration:
            self.finished = True

    def resume_with(self, res):
        try:
            self.cur = self.g.send(res)
        except StopIteration:
            self.cur = None
            self.finished = True


class State:
    def __init__(self, init, scripts):
        self.m = Mem(init)
        self.threads = [Thread(i + 1, s) for i, s in enumerate(scripts)]
        self.step = 0
        self.trace = []
        self.ops = []

    def run_step(self, i):
        t = self.threads[i]
        op = t.cur
        if op.first is None:
            op.first = self.step
        op.step(self.m, self.trace)
        if op.done:
            op.last = self.step
            self.ops.append(op)
            t.resume_with(op.res)
        self.step += 1


def linearizable(ops, init, rmode='exact'):
    if rmode == 'skip':
        ops = [o for o in ops if o['kind'] != 'R']
    n = len(ops)
    pred = [[j for j in range(n) if ops[j]["last"] < ops[i]["first"]] for i in range(n)]
    full = (1 << n) - 1

    def rec(mask, c):
        if mask == full:
            return True
        for i in range(n):
            if mask & (1 << i):
                continue
            if any(not (mask & (1 << j)) for j in pred[i]):
                continue
            k, r = ops[i]["kind"], ops[i]["res"]
            if k == "I":
                exp, nc = (False, c) if c == 0 else (True, c + 1)
                if exp != r:
                    continue
            elif k == "D":
                if c == 0:
                    continue
                nc = c - 1
                if (nc == 0) != r:
                    continue
            else:
                if rmode == 'bool':
                    if (r == 0) != (c == 0):
                        continue
                elif r != c:
                    continue
                nc = c
            if rec(mask | (1 << i), nc):
                return True
        return False

    return rec(0, init)


def check(s, init, rmode='exact'):
    ops = [dict(kind=o.kind, first=o.first, last=o.last, res=o.res, tid=o.tid) for o in s.ops]
    if not linearizable(ops, init, rmode):
        return False, "不可线性化(结果无法对应任何合法的先后顺序)"
    if not (s.m.v & IS_ZERO):
        return False, "全部引用都释放了,但最终状态没有固化为零"
    return True, ""


def explore(init, scripts, rmode='exact'):
    stats = {"leaves": 0, "viol": 0, "first": None}

    def rec(prefix):
        s = State(init, scripts)
        for i in prefix:
            s.run_step(i)
        runnable = [i for i, t in enumerate(s.threads) if not t.finished]
        if not runnable:
            stats["leaves"] += 1
            ok, why = check(s, init, rmode)
            if not ok:
                stats["viol"] += 1
                if stats["first"] is None:
                    stats["first"] = (why, list(s.trace), [(o.kind, o.tid, o.res) for o in s.ops])
            return
        for i in runnable:
            rec(prefix + [i])

    rec([])
    return stats


def scenarios(vD, vR):
    def dec(tid):
        yield Dec(tid, vD)

    def inc_then_dec(tid):
        ok = yield Inc(tid)
        if ok:
            yield Dec(tid, vD)

    def rd(tid):
        yield Read(tid, vR)

    return {
        "S1 初始1:1减 + 1(加→减) + 1读": (1, [dec, inc_then_dec, rd]),
        "S2 初始1:1减 + 1(加→减) + 2读": (1, [dec, inc_then_dec, rd, rd]),
        "S3 初始1:1减 + 2读(无加)": (1, [dec, rd, rd]),
        "S4 初始1:1减 + 2(加→减) + 1读": (1, [dec, inc_then_dec, inc_then_dec, rd]),
    }


def run(label, vD, vR, only=None, show=True, rmode='exact'):
    print(f"\n=== {label}  (减={vD}, 读={vR}) ===")
    for name, (init, scripts) in scenarios(vD, vR).items():
        if only and not any(name.startswith(o) for o in only):
            continue
        st = explore(init, scripts, rmode)
        print(f"{name}: 共 {st['leaves']} 种交错,违规 {st['viol']} 种")
        if st["first"] and show:
            why, trace, res = st["first"]
            print("  首个反例:", why)
            for line in trace:
                print("   ", line)
            print("    各操作结果(种类,线程,返回值):", res)
            show = False


if __name__ == "__main__":
    which = sys.argv[1] if len(sys.argv) > 1 else "all"
    if which == "base":
        for rm in ("skip", "bool", "exact"):
            run("原版(源笔记 #30 的代码)", "orig", "orig", rmode=rm)
    if which == "m1":
        run("变体 M1:decrement 看到 helped 就直接返回 true,不做 exchange 认领", "no_exchange", "orig", only=["S1", "S2", "S4"], rmode="skip")
    if which == "m2":
        run("变体 M2:read 看到 0 直接返回 0,不帮忙固化", "orig", "no_help", only=["S1", "S2", "S4"], rmode="bool")
    if which == "m3":
        run("变体 M3:decrement 的 CAS 失败后不检查 helped,一律返回 false", "no_help_check", "orig", only=["S1", "S2", "S3"], rmode="skip")
