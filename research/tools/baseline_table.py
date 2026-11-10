#!/usr/bin/env python3
"""Baseline CSV and per-dataset noise floors from N sessions of `timings --csv`.

Standard library only. Used by `make linux-baseline` (SS-1, objective.md
section 8): each input file is ONE session, i.e. one process of the same
release `build/timings` binary run with `--csv --reps 9`. Every repetition
times Stratum Sort and then std::sort on copies of the same input, so the
two are alternated inside the session.

Definitions (the same as research/tools/ab_table.py, objective.md section 4):

  ratio          per repetition, Stratum Sort ms / std::sort ms.
  median         median over ALL repetitions of ALL sessions.
  range          min-max of the per-SESSION medians.
  gana           median <= 1 - u  AND  the whole range is below 1.0
  pierde         median >= 1 + u  AND  the whole range is above 1.0
  empate         anything else
  u              the larger of the built-in threshold (6 %, ManyRepeated
                 18 %, Concentrated 12 %) and the floor measured here.

Noise floor of a dataset ("3 runs of the same binary", objective.md
section 4). The sessions run the same binary on the same inputs, so any
difference between them is noise. For every (type, n) row with
n >= --floor-min-n, the spread of the per-session medians is
max/min - 1, taken both of the Stratum Sort time (what a B/A comparison
sees) and of the ratio against std::sort (what a verdict sees). The floor
of the dataset is the largest of those spreads over its rows. n = 10^3 is
left out by default: the whole sort takes microseconds there, and the meta
B only asks it not to lose.

Outputs:
  --out     the baseline: '#' lines (system, flags, commit, command) and one
            row per (type, dataset, n).
  --floors  'dataset,threshold,...' - the format FLOORS= of `make ab` reads
            (it uses the first two columns and takes the larger of it and
            the built-in threshold, exactly as here).

Exit status: 0 = ok, 1 = some sort disagreed with std::sort, 2 = unusable input.
"""
import argparse
import csv
import statistics
import sys
from collections import OrderedDict, defaultdict

DEFAULT_THRESHOLD = 0.06
KNOWN_FLOORS = {"ManyRepeated": 0.18, "Concentrated": 0.12}


def verdict(median, session_medians, u):
    if median <= 1.0 - u and max(session_medians) < 1.0:
        return "gana"
    if median >= 1.0 + u and min(session_medians) > 1.0:
        return "pierde"
    return "empate"


def spread(values):
    lo = min(values)
    return max(values) / lo - 1.0 if lo > 0 else float("inf")


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("sessions", nargs="+", help="one `timings --csv` output per session")
    ap.add_argument("--out", required=True, help="baseline CSV to write")
    ap.add_argument("--floors", required=True, help="per-dataset floors CSV to write")
    ap.add_argument("--meta", action="append", default=[],
                    help="extra '# key: value' line for the header (repeatable)")
    ap.add_argument("--floor-min-n", type=int, default=10000,
                    help="smallest n that counts towards the noise floor (default 1e4)")
    args = ap.parse_args()

    header = []      # '#' lines of the first session (SystemInfo, lambda, t, D)
    keys = OrderedDict()
    ratios = defaultdict(lambda: defaultdict(list))   # key -> session -> [ratio]
    s_ms = defaultdict(lambda: defaultdict(list))     # key -> session -> [stratum ms]
    t_ms = defaultdict(lambda: defaultdict(list))     # key -> session -> [std ms]
    n_real = {}
    na = set()
    mismatches = []

    for sess, path in enumerate(args.sessions, start=1):
        with open(path, newline="") as f:
            lines = f.read().splitlines()
        comments = [l for l in lines if l.startswith("#")]
        if sess == 1:
            header = comments
        for row in csv.DictReader(l for l in lines if l and not l.startswith("#")):
            key = (row["type"], row["dataset"], int(row["n"]))
            keys.setdefault(key, None)
            if row["status"] == "na":
                na.add(key)
                continue
            if row["status"] != "ok":
                mismatches.append((path, key, row["rep"]))
            a, b = float(row["stratum_ms"]), float(row["std_ms"])
            n_real[key] = int(row["n_real"])
            s_ms[key][sess].append(a)
            t_ms[key][sess].append(b)
            if b > 0:
                ratios[key][sess].append(a / b)

    if not keys:
        print("baseline_table: no data", file=sys.stderr)
        return 2
    nsess = len(args.sessions)

    # Pass 1: noise floors per dataset.
    floor = {}
    worst = {}
    for key in keys:
        typ, ds, n = key
        if key in na or n < args.floor_min_n or len(ratios[key]) < 2:
            continue
        sm = [statistics.median(v) for v in s_ms[key].values()]
        rm = [statistics.median(v) for v in ratios[key].values()]
        f = max(spread(sm), spread(rm))
        if f > floor.get(ds, -1.0):
            floor[ds] = f
            worst[ds] = "%s n=%d" % (typ, n)

    # Pass 2: one row per (type, dataset, n), sizes ascending within a type,
    # datasets in battery order.
    order = {}
    for i, k in enumerate(keys):
        order.setdefault(k[1], i)
    rows = sorted(keys, key=lambda k: (k[0], k[2], order[k[1]]))

    counts = defaultdict(int)
    with open(args.out, "w", newline="") as out:
        for line in header:
            out.write(line + "\n")
        for m in args.meta:
            out.write("# " + m + "\n")
        out.write("# sessions=%d floor_min_n=%d; ratio = Stratum Sort / std::sort; range = "
                  "min-max of the per-session medians; threshold = max(built-in, measured floor); "
                  "release build only\n" % (nsess, args.floor_min_n))
        w = csv.writer(out, lineterminator="\n")
        w.writerow(["type", "dataset", "n", "n_real", "reps", "ratio_median", "ratio_min",
                    "ratio_max", "stratum_ms_median", "std_ms_median", "session_spread",
                    "threshold", "verdict_linux", "verdict_m4"])
        for key in rows:
            typ, ds, n = key
            u = max(KNOWN_FLOORS.get(ds, DEFAULT_THRESHOLD), floor.get(ds, 0.0))
            if key in na or not ratios[key]:
                w.writerow([typ, ds, n, "", 0, "", "", "", "", "", "", "%.3f" % u, "n/a",
                            "pendiente"])
                counts["n/a"] += 1
                continue
            pooled = [x for v in ratios[key].values() for x in v]
            sess_med = [statistics.median(v) for _, v in sorted(ratios[key].items())]
            med = statistics.median(pooled)
            v = verdict(med, sess_med, u)
            counts[v] += 1
            sm = [statistics.median(x) for x in s_ms[key].values()]
            w.writerow([typ, ds, n, n_real[key], len(pooled), "%.4f" % med,
                        "%.4f" % min(sess_med), "%.4f" % max(sess_med),
                        "%.6f" % statistics.median([x for y in s_ms[key].values() for x in y]),
                        "%.6f" % statistics.median([x for y in t_ms[key].values() for x in y]),
                        "%.4f" % spread(sm), "%.3f" % u, v, "pendiente"])

    with open(args.floors, "w", newline="") as f:
        w = csv.writer(f, lineterminator="\n")
        f.write("# Linux noise floors per dataset: max over (type, n >= %d) of the spread "
                "(max/min - 1) of the per-session medians of %d runs of the same binary.\n"
                % (args.floor_min_n, nsess))
        f.write("# Usable as `make ab FLOORS=<this file>` (first two columns).\n")
        w.writerow(["dataset", "threshold", "builtin", "applied", "worst_row"])
        for ds in sorted(floor, key=lambda d: order[d]):
            builtin = KNOWN_FLOORS.get(ds, DEFAULT_THRESHOLD)
            w.writerow([ds, "%.3f" % floor[ds], "%.3f" % builtin,
                        "%.3f" % max(builtin, floor[ds]), worst[ds]])

    print("baseline: %d rows (%s), %d sessions -> %s"
          % (len(rows), ", ".join("%d %s" % (c, k) for k, c in sorted(counts.items())),
             nsess, args.out))
    above = [ds for ds in floor if floor[ds] > KNOWN_FLOORS.get(ds, DEFAULT_THRESHOLD)]
    print("floors: %d datasets, %d above the built-in threshold%s -> %s"
          % (len(floor), len(above), (": " + ", ".join(above)) if above else "", args.floors))
    if nsess < 3:
        print("AVISO: menos de 3 sesiones; objective.md §4 exige 3.")
    if mismatches:
        print("ERROR: %d ordenaciones NO coinciden con std::sort, p. ej. %s"
              % (len(mismatches), mismatches[0]))
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
