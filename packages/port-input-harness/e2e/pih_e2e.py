#!/usr/bin/env python3
"""port-input-harness, end to end: plays a port's scripted run on virtual controllers and checks the game saw what
the script said. Game-agnostic: the port is an adapter module (see README.md, "An adapter").

  python pih_e2e.py --adapter <adapter.py> --profiles <profiles dir> [--work DIR] [--only a,b] [--quick]
                    [--opt key=value]...

For every controller profile (and some mixed pairs), the adapter runs the port's scripted run with that controller
plugged in, and the runner checks:

  transparency   every input the port read equals the baseline's (the same script played straight into the port),
                 and the port's state on every tick (a hash log, whatever the port calls one) is byte-identical:
                 the controller was invisible, the run played the same
  hot-plug       player 1's controller is unplugged in the middle of the run while the script is pressing buttons
                 on it, and another kind is plugged in later: inputs equal the baseline's before the gap, are empty
                 in it, equal the baseline's again after; player 2's are never disturbed
  menus          (adapter has menu cases) each leaves a menu with one button: "backs" must go back, "stays" must not
  prompts        (adapter has pictures; has_prompts() says whether the port draws any) the screen drawn with a controller plugged in equals the screen drawn with
                 that controller's family forced, and the families the adapter lists look different from each other

Prints a line per check and a table of profiles, and exits 1 if any failed. Python 3.8 or later, no packages; runs the
same on Windows (PowerShell or cmd), macOS and Linux (Bash or zsh): it only starts the port's own program.
"""
import argparse
import importlib.util
import os
import sys


class Run:
    """What one run of the port gives back.

    ok      the run finished (exit status 0)
    state   bytes: the port's state on every tick, any form, compared for equality
    reads   {(tick, player): int}: what the port read from each player's controller on each tick
    """

    def __init__(self, ok, state=b"", reads=None, note=""):
        self.ok = ok
        self.state = state
        self.reads = reads or {}
        self.note = note


def profile_files(directory):
    """[(name, family)] from the *.profile files, in name order."""
    out = []
    for fn in sorted(os.listdir(directory)):
        if not fn.endswith(".profile"):
            continue
        name = fam = None
        with open(os.path.join(directory, fn), encoding="utf-8") as f:
            for line in f:
                t = line.split("#", 1)[0].split()
                if len(t) == 2 and t[0] == "name":
                    name = t[1]
                elif len(t) == 2 and t[0] == "family":
                    fam = t[1]
        if name:
            out.append((name, fam or "other"))
    return out


def load_adapter(path, opts):
    spec = importlib.util.spec_from_file_location("pih_adapter", path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod.Adapter(opts)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--adapter", required=True)
    ap.add_argument("--profiles", required=True)
    ap.add_argument("--work", default="pih-e2e-work")
    ap.add_argument("--only", default="")
    ap.add_argument("--quick", action="store_true", help="the profiles alone: no mixed pairs, no hot-plug")
    ap.add_argument("--opt", action="append", default=[], help="key=value for the adapter")
    args = ap.parse_args()

    work = os.path.abspath(args.work)
    os.makedirs(work, exist_ok=True)
    opts = dict(o.split("=", 1) for o in args.opt)
    ad = load_adapter(args.adapter, opts)
    profiles = profile_files(args.profiles)
    if args.only:
        keep = set(args.only.split(","))
        profiles = [p for p in profiles if p[0] in keep]
    names = [p[0] for p in profiles]
    family = dict(profiles)

    results = []

    def check(name, ok, why=""):
        results.append((name, ok))
        print(("pass  " if ok else "FAIL  ") + name + ("" if ok else ": " + why))

    base = ad.baseline(work)
    if not base.ok:
        print("the baseline run failed: %s" % base.note)
        return 1
    n_pressed = sum(1 for (t, p), v in base.reads.items() if p == 0 and v)
    print("baseline: %d reads, %d with a button down on player 1" % (len(base.reads), n_pressed))

    # transparency, with each profile in both places and some mixed pairs
    combos = [(n, n) for n in names]
    if not args.quick and len(names) > 1:
        combos += [(names[i], names[(i + 3) % len(names)]) for i in range(0, len(names), 2)]
    for a, b in combos:
        tag = "%s+%s" % (a, b)
        r = ad.with_controllers(work, tag, [a, b], [])
        if not r.ok:
            check("%s: runs" % tag, False, r.note or "the run failed")
            continue
        bad = [k for k in base.reads if r.reads.get(k) != base.reads[k]]
        check("%s: every input the port read equals the script's (%d reads)" % (tag, len(base.reads)), not bad,
              "first difference at tick %s player %s: %s, not %s" % (bad[0][0], bad[0][1], r.reads.get(bad[0]), base.reads[bad[0]])
              if bad else "")
        check("%s: the port's state on every tick equals the baseline's" % tag, r.state == base.state, "the runs differ")

    # hot-plug
    if not args.quick and len(names) >= 3:
        after = int(opts.get("hotplug_after", 1200))
        gap_len = int(opts.get("hotplug_gap", 40))
        start = next((t for (t, p), v in sorted(base.reads.items()) if p == 0 and v and t > after), None)
        if start is None:
            check("hot-plug: the script presses buttons on player 1 after tick %d" % after, False, "it doesn't")
        else:
            back = start + gap_len
            first, second = names[0], names[1 % len(names)]
            third = names[2]
            r = ad.with_controllers(work, "hotplug", [first, second], [(start, 0, None), (back, 0, third)])
            if not r.ok:
                check("hot-plug: runs", False, r.note or "the run failed")
            else:
                ticks = sorted({t for t, p in base.reads})
                lost = [t for t in ticks if r.reads.get((t, 0)) == 0 and base.reads[(t, 0)] != 0]
                lo, hi = (min(lost), max(lost)) if lost else (None, None)
                before = [t for t in ticks if t < start and r.reads.get((t, 0)) != base.reads[(t, 0)]]
                gap = [t for t in ticks if start + 2 <= t <= back - 1 and r.reads.get((t, 0)) != 0]
                late = [t for t in ticks if t > back + 3 and r.reads.get((t, 0)) != base.reads[(t, 0)]]
                other = [t for t in ticks if r.reads.get((t, 1)) != base.reads[(t, 1)]]
                check("hot-plug: player 1's input stops when the controller goes (ticks %s to %s)" % (lo, hi),
                      bool(lost) and start - 2 <= lo <= start + 3, "lost ticks: %s" % lost[:5])
                check("hot-plug: it returns with the new controller (%s), in by tick %s" % (third, hi),
                      bool(lost) and hi <= back + 3, "last lost tick %s, plugged at %d" % (hi, back))
                check("hot-plug: player 1 reads nothing while no controller is plugged in (ticks %d to %d)" %
                      (start + 2, back - 1), not gap, "reads at %s" % gap[:5])
                check("hot-plug: before the gap every input equals the script's", not before, "differs at %s" % before[:5])
                check("hot-plug: after the new controller is in every input equals the script's", not late,
                      "differs at %s" % late[:5])
                check("hot-plug: player 2's controller is never disturbed", not other, "differs at %s" % other[:5])

    # menus
    for case, want in getattr(ad, "menu_cases", lambda: [])():
        for n in names:
            back = ad.menu(work, "menu-%s-%s" % (case, n), n, case)
            if back is None:
                check("menu: %s: a %s gets into the menu" % (case, n), False, "the run never reached it")
                continue
            check("menu: %s: a %s %s" % (case, n, "backs out" if want == "backs" else "stays in"), back == (want == "backs"),
                  "it %s" % ("backed out" if back else "stayed"))

    # prompts
    pic = getattr(ad, "picture", None)
    if pic is not None and getattr(ad, "has_prompts", lambda: True)():
        forced = {}
        for n in names:
            fam = family[n]
            got = pic(work, "prompts-%s" % n, profile=n, family=None)
            if fam not in forced:
                forced[fam] = pic(work, "prompts-forced-%s" % fam, profile=None, family=fam)
            check("prompts: a %s shows the %s prompts the family forced shows" % (n, fam),
                  got is not None and got == forced[fam], "the pictures differ" if got else "no picture")
        d = [f for f in getattr(ad, "distinct_families", lambda: [])() if f in forced]
        for i, a in enumerate(d):
            for b in d[i + 1:]:
                check("prompts: %s and %s are drawn differently" % (a, b), forced[a] != forced[b], "the same picture")

    print()
    print("%d of %d checks passed" % (sum(1 for _, ok in results if ok), len(results)))
    return 0 if all(ok for _, ok in results) else 1


if __name__ == "__main__":
    sys.exit(main())
