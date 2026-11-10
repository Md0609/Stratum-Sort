#!/usr/bin/env python3
"""G3 verdict table from the raw CSV of ab_compare.sh (objective.md section 5).

Standard library only. Reads one row per (session, repetition, side,
type, dataset, size) and prints the fixed G3 table:

  dataset x size, ratio against std::sort, ratio against the previous
  version, min-max range, and a verdict gana / empate / pierde.

Definitions (objective.md section 4, written down so they can be checked):

  ratio vs std::sort  per repetition, Stratum Sort time / std::sort time on
                      the same input in the same process. Both sorts are
                      timed back to back, so the pair shares machine state.
  ratio B/A           per repetition, B's Stratum Sort time / A's Stratum
                      Sort time from the same (session, repetition) slot,
                      which ab_compare.sh runs back to back (ABBA order).
  median              median over ALL repetitions of ALL sessions.
  range               min-max of the per-SESSION medians. With 3 sessions
                      it is the spread between independent sessions, which
                      is what "the ranges of 3 sessions do not overlap 1.0"
                      asks for.
  gana                median <= 1 - u  AND  the whole range is below 1.0
  pierde              median >= 1 + u  AND  the whole range is above 1.0
  empate              anything else
  u                   6 %, except ManyRepeated 18 % and Concentrated 12 %
                      (BENCHMARKS.md). --floors adds measured floors; the
                      larger of the two is used (objective.md section 4).

G3 rejects (objective.md section 5) when, for some row,
  (a) B/A is "pierde": B is slower than A beyond the noise floor, or
  (b) A was "gana" against std::sort and B is not.

Exit status: 0 = G3 passes, 1 = G3 rejects or some sort disagreed with
std::sort, 2 = unusable input.
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


def summarize(per_session):
    """per_session: {session: [ratios]} -> (median, [session medians])."""
    pooled = [x for xs in per_session.values() for x in xs]
    sess = [statistics.median(xs) for _, xs in sorted(per_session.items()) if xs]
    return statistics.median(pooled), sess


def fmt_range(sess):
    return "%.2f–%.2f" % (min(sess), max(sess))


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("raw", help="raw.csv written by ab_compare.sh")
    ap.add_argument("--meta", help="meta.txt written by ab_compare.sh")
    ap.add_argument("--floors", help="CSV 'dataset,threshold' of measured noise floors (fractions)")
    args = ap.parse_args()

    floors = dict(KNOWN_FLOORS)
    if args.floors:
        with open(args.floors, newline="") as f:
            for row in csv.reader(f):
                if not row or row[0].startswith("#") or row[0] == "dataset":
                    continue
                floors[row[0]] = max(floors.get(row[0], DEFAULT_THRESHOLD), float(row[1]))

    std_ratio = defaultdict(lambda: defaultdict(lambda: defaultdict(list)))  # key->side->session->[r]
    stratum_ms = {}  # (key, side, session, rep) -> ms
    n_real = defaultdict(dict)  # key -> side -> n_real
    na = defaultdict(set)  # key -> {sides}
    keys = OrderedDict()
    mismatches = []
    sessions = set()

    with open(args.raw, newline="") as f:
        for row in csv.DictReader(f):
            key = (row["type"], row["dataset"], int(row["n"]))
            keys.setdefault(key, None)
            side, sess, rep = row["side"], int(row["session"]), int(row["rep"])
            sessions.add(sess)
            if row["status"] == "na":
                na[key].add(side)
                continue
            if row["status"] != "ok":
                mismatches.append((key, side, sess, rep))
            a, b = float(row["stratum_ms"]), float(row["std_ms"])
            if b > 0:
                std_ratio[key][side][sess].append(a / b)
            stratum_ms[(key, side, sess, rep)] = a
            n_real[key][side] = int(row["n_real"])

    if not keys:
        print("ab_table: no data in %s" % args.raw, file=sys.stderr)
        return 2

    # Sort sizes ascending within each type, keeping the battery order of
    # the datasets as they first appeared.
    order = {}
    for i, k in enumerate(keys):
        order.setdefault((k[0], k[1]), i)
    rows = sorted(keys, key=lambda k: (k[0], k[2], order[(k[0], k[1])]))

    if args.meta:
        with open(args.meta) as f:
            for line in f:
                print("<!-- %s -->" % line.strip())
    print()
    print("| Dataset | n | tipo | A/std | B/std | rango B/std | veredicto B/std "
          "| B/A | rango B/A | veredicto B/A | umbral | G3 |")
    print("|---|---:|---|---:|---:|---|---|---:|---|---|---:|---|")

    counts = defaultdict(int)
    rejects = []
    notes = []
    for key in rows:
        typ, ds, n = key
        u = floors.get(ds, DEFAULT_THRESHOLD)
        if "A" not in std_ratio[key] or "B" not in std_ratio[key]:
            reason = "n/a (familia no construible en este n)" if na[key] else "sin datos"
            if na[key] and len(na[key]) == 1:
                reason = "n/a solo en %s (lambda o D distintos)" % ",".join(sorted(na[key]))
                notes.append("%s n=%d %s: %s" % (ds, n, typ, reason))
            print("| %s | %d | %s | – | – | – | %s | – | – | – | %.0f %% | – |"
                  % (ds, n, typ, reason, u * 100))
            continue

        a_med, a_sess = summarize(std_ratio[key]["A"])
        b_med, b_sess = summarize(std_ratio[key]["B"])
        va = verdict(a_med, a_sess, u)
        vb = verdict(b_med, b_sess, u)

        ba = defaultdict(list)
        for (k, side, sess, rep), ms in stratum_ms.items():
            if k == key and side == "A" and (k, "B", sess, rep) in stratum_ms and ms > 0:
                ba[sess].append(stratum_ms[(k, "B", sess, rep)] / ms)
        ba_med, ba_sess = summarize(ba)
        vba = verdict(ba_med, ba_sess, u)
        counts[vba] += 1

        g3 = "ok"
        why = []
        if vba == "pierde":
            why.append("(a) empeora frente a A")
        if va == "gana" and vb != "gana":
            why.append("(b) deja de ganar")
        if why:
            g3 = "RECHAZA " + " ".join(why)
            rejects.append("%s n=%d %s: %s" % (ds, n, typ, " ".join(why)))
        mark = ""
        if n_real[key].get("A") != n_real[key].get("B"):
            mark = "*"
            notes.append("%s n=%d %s: n_real A=%s B=%s (adversario reconstruido con otra "
                         "configuración: entradas distintas)" % (ds, n, typ, n_real[key].get("A"),
                                                                n_real[key].get("B")))
        print("| %s%s | %d | %s | %.2f | %.2f | %s | %s | %.3f | %s | %s | %.0f %% | %s |"
              % (ds, mark, n, typ, a_med, b_med, fmt_range(b_sess), vb, ba_med,
                 fmt_range(ba_sess), vba, u * 100, g3))

    print()
    print("B/A: %d empate, %d gana, %d pierde   (sesiones: %d)"
          % (counts["empate"], counts["gana"], counts["pierde"], len(sessions)))
    if len(sessions) < 3:
        print("AVISO: menos de 3 sesiones. El §4 exige 3 sesiones de 9 repeticiones; con %d el "
              "rango es solo el de %d mediana(s) de sesión y los veredictos son orientativos."
              % (len(sessions), len(sessions)))
    for note in notes:
        print("nota: " + note)
    status = 0
    if mismatches:
        print("ERROR: %d ordenaciones NO coinciden con std::sort, p. ej. %s"
              % (len(mismatches), mismatches[0]))
        status = 1
    if rejects:
        print("G3: RECHAZA (%d filas)" % len(rejects))
        for r in rejects:
            print("  - " + r)
        status = 1
    else:
        print("G3: PASA (ninguna fila empeora frente a A ni deja de ganar)")
    return status


if __name__ == "__main__":
    sys.exit(main())
