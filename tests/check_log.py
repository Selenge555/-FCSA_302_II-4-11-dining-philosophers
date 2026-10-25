#!/usr/bin/env python3
"""
check_log.py -- dp-ийн гаралтыг (JSON Lines) бие даан шалгагч.

Шалгах инвариантууд:
  1. Эхний event config, сүүлийн event end. Хугацаа буурахгүй.
  2. Philosopher бүрийн төлөвийн шилжилт хууль ёсны:
       thinking -> hungry, hungry -> hungry (naive: fork авах), hungry -> eating,
       eating -> thinking.
  3. Mutual exclusion: зэргэлдээ хоёр philosopher зэрэг идэхгүй.
  4. Fork: нэг fork нэг эзэнтэй; эзэн нь зөвхөн зэргэлдээ (k эсвэл k-1)
     philosopher; эзэнтэй fork "тавигдахгүйгээр" өөр эзэнд шилжихгүй.
  5. Eating philosopher хоёр fork-оо эзэмшинэ; thinking philosopher fork эзэмшихгүй.
     monitor алгоритмд hungry philosopher ч fork эзэмшихгүй.
  6. end.meals нь eating event-ийн тоотой тэнцүү.

Ашиглалт:
  check_log.py [--min-meals K] [--expect-deadlock | --allow-deadlock] [файл]
  (файл өгөөгүй бол stdin)
"""
import argparse
import json
import sys


def check(lines, min_meals=0, expect_deadlock=False, allow_deadlock=False):
    errors = []

    def err(i, msg):
        errors.append(f"мөр {i}: {msg}")

    events = []
    for i, raw in enumerate(lines, 1):
        raw = raw.strip()
        if not raw:
            continue
        try:
            events.append((i, json.loads(raw)))
        except json.JSONDecodeError as e:
            err(i, f"JSON буруу ({e})")
    if errors:
        return errors, None
    if not events:
        return ["гаралт хоосон"], None

    first, last = events[0][1], events[-1][1]
    if first.get("type") != "config":
        err(events[0][0], "эхний event нь config биш")
        return errors, None
    if last.get("type") != "end":
        err(events[-1][0], "сүүлийн event нь end биш (программ дутуу гарсан)")
        return errors, None

    n = first["n"]
    naive = first["algorithm"] == "naive"
    state = ["thinking"] * n
    owner = [-1] * n
    eats = [0] * n
    seen_init = [False] * n
    prev_t = 0
    saw_deadlock = False
    legal = {
        ("thinking", "hungry"), ("hungry", "hungry"),
        ("hungry", "eating"), ("eating", "thinking"),
    }

    for i, ev in events[1:]:
        typ = ev.get("type")
        t = ev.get("t", 0)
        if t < prev_t:
            err(i, f"хугацаа буурсан ({prev_t} -> {t})")
        prev_t = max(prev_t, t)
        if typ == "deadlock":
            saw_deadlock = True
            continue
        if typ == "end":
            continue
        if typ != "state":
            err(i, f"үл мэдэгдэх event төрөл '{typ}'")
            continue

        pid, new = ev["id"], ev["state"]
        forks = ev["forks"]
        if not (0 <= pid < n) or len(forks) != n:
            err(i, "id эсвэл forks-ийн урт буруу")
            continue

        if not seen_init[pid]:
            # эхний төлөв thinking, fork-гүй
            seen_init[pid] = True
            if new != "thinking":
                err(i, f"P{pid}-ийн эхний төлөв thinking биш ({new})")
        elif (state[pid], new) not in legal:
            err(i, f"P{pid}: хууль бус шилжилт {state[pid]} -> {new}")

        for k in range(n):
            a, b = owner[k], forks[k]
            if b not in (-1, k, (k - 1) % n):
                err(i, f"fork {k}-ийн эзэн P{b} зэргэлдээ биш")
            if a != -1 and b != -1 and a != b:
                err(i, f"fork {k} тавигдахгүйгээр P{a} -> P{b} шилжсэн")
        owner = list(forks)
        state[pid] = new
        if new == "eating":
            eats[pid] += 1

        # Mutual exclusion (n=2 үед хоёр нь хөрш)
        for p in range(n):
            if state[p] == "eating" and state[(p + 1) % n] == "eating" and (p + 1) % n != p:
                err(i, f"зэргэлдээ P{p} ба P{(p + 1) % n} зэрэг идэж байна")
        for p in range(n):
            held = [k for k in range(n) if owner[k] == p]
            if state[p] == "eating" and sorted(held) != sorted({p, (p + 1) % n}):
                err(i, f"P{p} идэж байхдаа хоёр fork-оо эзэмшээгүй (эзэмшсэн: {held})")
            if state[p] == "thinking" and held:
                err(i, f"P{p} thinking байхдаа fork {held} барьсан")
            if state[p] == "hungry" and held and not naive:
                err(i, f"monitor: P{p} hungry байхдаа fork {held} барьсан")

    if last["meals"] != eats:
        errors.append(f"end.meals {last['meals']} != eating event-ийн тоо {eats}")
    if expect_deadlock and not saw_deadlock:
        errors.append("deadlock хүлээсэн боловч илрээгүй")
    if not expect_deadlock and not allow_deadlock and (saw_deadlock or last["reason"] == "deadlock"):
        errors.append("deadlock хүлээгээгүй боловч илэрсэн")
    if min_meals and not expect_deadlock and not allow_deadlock:
        for p, m in enumerate(last["meals"]):
            if m < min_meals:
                errors.append(f"starvation: P{p} зөвхөн {m} удаа идсэн (доод хязгаар {min_meals})")

    return errors, {"n": n, "events": len(events), "meals": last["meals"],
                    "reason": last["reason"], "algorithm": first["algorithm"]}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("file", nargs="?")
    ap.add_argument("--min-meals", type=int, default=0)
    ap.add_argument("--expect-deadlock", action="store_true")
    ap.add_argument("--allow-deadlock", action="store_true",
                    help="deadlock гарсан ч алдаа гэж үзэхгүй (инвариантыг л шалгана)")
    a = ap.parse_args()
    if a.file:
        lines = open(a.file, encoding="utf-8").read().splitlines()
    else:
        lines = sys.stdin.read().splitlines()
    errors, info = check(lines, a.min_meals, a.expect_deadlock, a.allow_deadlock)
    if errors:
        for e in errors[:20]:
            print("FAIL:", e)
        if len(errors) > 20:
            print(f"... нийт {len(errors)} алдаа")
        sys.exit(1)
    print(f"OK: {info['algorithm']} n={info['n']} events={info['events']} "
          f"reason={info['reason']} meals={info['meals']}")


if __name__ == "__main__":
    main()
