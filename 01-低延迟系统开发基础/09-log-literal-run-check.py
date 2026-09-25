# 对比 Logger::log() 的两种写法:源笔记(逐字符 push)vs 改进版(连续字面量攒成一段再 push)。
# 没有 C++ 编译器,这里把两段逻辑逐行翻成 Python,检查输出文本、错误情形是否一致,并统计 push 次数。
import random
import sys

sys.stdout.reconfigure(encoding="utf-8")
CAP = 255  # LogElement::u_.s 是 char[256],留 1 字节放结尾的 '\0'


def render(pieces):
    return "".join(str(p[1]) for p in pieces)


def orig_log(fmt, args):
    out = []

    def go(i, args):
        n = len(fmt)
        while i < n:
            if fmt[i] == "%":
                nxt = fmt[i + 1] if i + 1 < n else ""
                if nxt == "%":
                    i += 1                      # ++s,下面再把第二个 % 当普通字符推
                else:
                    if not args:
                        return "missing"        # FATAL("missing arguments to log()")
                    out.append(("v", args[0]))  # pushValue(value)
                    return go(i + 1, args[1:])  # log(s + 1, args...)
            out.append(("c", fmt[i]))           # pushValue(*s++)
            i += 1
        return "extra" if args else None        # 模板版走到串尾还有参数 → FATAL("extra ...")

    err = go(0, list(args))
    return out, err


def new_log(fmt, args):
    out = []

    def push_literal(p):
        for k in range(0, len(p), CAP):         # 一个 STRING 元素最多 255 个字符,更长就切段
            out.append(("s", p[k:k + CAP]))

    def go(i, args):
        n, run = len(fmt), i
        while i < n:
            if fmt[i] == "%":
                nxt = fmt[i + 1] if i + 1 < n else ""
                if nxt == "%":
                    push_literal(fmt[run:i + 1])  # 这一段收到第一个 % 为止(含它)
                    i += 2
                    run = i
                    continue
                if not args:
                    return "missing"
                push_literal(fmt[run:i])          # 遇到占位符:先推 % 之前的整段文字
                out.append(("v", args[0]))
                return go(i + 1, args[1:])
            i += 1
        if args:
            return "extra"
        push_literal(fmt[run:i])                  # 收尾:最后一段文字
        return None

    err = go(0, list(args))
    return out, err


def count_placeholders(fmt):
    i, k, n = 0, 0, len(fmt)
    while i < n:
        if fmt[i] == "%":
            if i + 1 < n and fmt[i + 1] == "%":
                i += 2
                continue
            k += 1
        i += 1
    return k


def check(fmt, args):
    (o1, e1), (o2, e2) = orig_log(fmt, args), new_log(fmt, args)
    assert e1 == e2, (fmt, args, e1, e2)
    if e1 is None:
        assert render(o1) == render(o2), (fmt, args, render(o1), render(o2))
        assert all(len(p[1]) <= CAP for p in o2 if p[0] == "s")
    return len(o1), len(o2)


if __name__ == "__main__":
    ex = "Order Executed, id=%, price=%\n"
    a, b = check(ex, [42, 3.14])
    print(f"示例 {ex!r}: 原版 {a} 次 push,改进版 {b} 次 push;每次 264 B → {a*264} B vs {b*264} B")

    random.seed(1)
    alphabet = ["a", "b", " ", "x", "%", "%", "\n"]
    n_ok = n_err = 0
    for _ in range(300000):
        fmt = "".join(random.choice(alphabet) for _ in range(random.randint(0, 40)))
        if random.random() < 0.7:
            args = list(range(count_placeholders(fmt)))
        else:
            args = [random.randint(0, 9) for _ in range(random.randint(0, 4))]
        check(fmt, args)
        if orig_log(fmt, args)[1] is None:
            n_ok += 1
        else:
            n_err += 1
    print(f"随机格式串 300000 组:正常 {n_ok} 组输出完全一致,报错情形 {n_err} 组两版报错类型一致")

    for fmt, args in [("", []), ("%%", []), ("%%%", [1]), ("a%%b%%c", []), ("%", [1]), ("%%%%", []), ("x" * 600, []),
                      ("x" * 255 + "%", [7]), ("x" * 256 + "%%" + "y" * 300, []), ("%%x%", [5])]:
        a, b = check(fmt, args)
        print(f"边界 {fmt[:12]!r}{'…' if len(fmt) > 12 else ''} (len={len(fmt)}, args={len(args)}): 原版 {a} 次,改进版 {b} 次,输出/报错一致")
    o, _ = new_log("x" * 600, [])
    print("600 个字符的字面量切成", [len(p[1]) for p in o], "(每段 ≤255)")
