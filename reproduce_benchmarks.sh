#!/usr/bin/env bash
# =====================================================================
#  Reproduces every number that appears in the report.
#
#  Usage:   ./reproduce_benchmarks.sh [output_dir] [wordlist]
#  Default: ./reproduce_benchmarks.sh report/data english.txt
#
#  Every command is echoed before it runs, so each figure and table in
#  the report can be traced back to the exact invocation that made it.
# =====================================================================
set -euo pipefail

OUT="${1:-report/data}"
WORDLIST="${2:-english.txt}"
SEED="000102030405060708090a0b0c0d0e0f"          # default seed, recorded in every CSV row
PROBES="./cmake-build-release/HashMapsProbes"
PLAIN="./cmake-build-release/HashMaps"

# --------------------------------------------------------------- checks
[ -f "$WORDLIST" ] || { echo "ERROR: wordlist '$WORDLIST' not found" >&2; exit 1; }

echo "=== Building both targets ==============================================="
cmake -S . -B cmake-build-release -DCMAKE_BUILD_TYPE=Release > /dev/null
cmake --build cmake-build-release --target HashMaps HashMapsProbes -j > /dev/null
echo "ok: $PROBES (counters on), $PLAIN (counters off)"
echo

mkdir -p "$OUT"

run() {                                    # run <label> <outfile> <cmd...>
    local label="$1"; local outfile="$2"; shift 2
    echo "--- $label"
    echo "    \$ $*"
    "$@" > "$outfile"
    echo "    -> $outfile ($(($(wc -l < "$outfile") - 1)) data rows)"
    echo
}

echo "=== Report Section 6.1-6.6: main sweep, n = 65536 ======================="
echo "    Tables 1, 2, 5 and Figures 6, 7, 9 all come from this single file."
run "probe counts (instrumented build)" "$OUT/wordlist-sweep-probes.csv" \
    "$PROBES" --csv-wordlist-sweep "$WORDLIST" 65536 "$SEED"
run "CPU times (uninstrumented build)" "$OUT/wordlist-sweep-time.csv" \
    "$PLAIN" --csv-wordlist-sweep "$WORDLIST" 65536 "$SEED"

echo "=== Report Section 6.8: Elastic lookup strategies, n = 4096 =============="
echo "    Table 3. Capacity is deliberately small: the two blind strategies"
echo "    do quadratic work and would take minutes at n = 65536."
run "three lookup strategies on one table" "$OUT/elastic-lookup-comparison.csv" \
    "$PROBES" --csv-elastic-lookup-comparison "$WORDLIST" 4096 0.125 "$SEED"

echo "=== Report Section 6.9: constant c sweep, n = 16384 ====================="
echo "    Table 4 and Figure 10."
run "c from 0.25 to 100" "$OUT/c-sweep.csv" \
    "$PROBES" --csv-c-sweep 0.125 "$SEED"

echo "=== Cross-check: generated keys instead of a wordlist ==================="
echo "    Not in the report, but confirms the results do not depend on the"
echo "    English wordlist. Same protocol, deterministic synthetic keys."
run "generated-key sweep, probes" "$OUT/load-sweep-probes.csv" \
    "$PROBES" --csv-load-sweep all "$SEED"
run "generated-key sweep, times" "$OUT/load-sweep-time.csv" \
    "$PLAIN" --csv-load-sweep all "$SEED"

# --------------------------------------------------------------- plot data
echo "=== Converting CSV to the .dat files pgfplots reads ====================="
python3 - "$OUT" <<'PYEOF'
import csv, os, sys
D = sys.argv[1]
def rows(f): return list(csv.DictReader(open(os.path.join(D, f))))

for src, tag in [('load-sweep-probes.csv','gen'), ('wordlist-sweep-probes.csv','wl')]:
    if not os.path.exists(os.path.join(D, src)): continue
    data = {}
    for r in rows(src):
        data.setdefault(r['implementation'], []).append(r)
    for impl, rs in data.items():
        with open(f'{D}/{tag}-{impl}.dat','w') as fh:
            fh.write('load insavg insmax lkavg lkmax neg\n')
            for r in rs:
                fh.write(f"{float(r['load_factor'])*100:.6f} "
                         f"{float(r['insertion_probe_average']):.4f} "
                         f"{r['insertion_probe_maximum']} "
                         f"{float(r['positive_lookup_probe_average']):.4f} "
                         f"{r['positive_lookup_probe_maximum']} "
                         f"{r['negative_lookup_probes']}\n")
        print(f"    -> {D}/{tag}-{impl}.dat")

if os.path.exists(os.path.join(D,'c-sweep.csv')):
    with open(f'{D}/csweep.dat','w') as fh:
        fh.write('c insavg lkavg case3 fallback case1 case2\n')
        for r in rows('c-sweep.csv'):
            fh.write(f"{float(r['c']):.4f} {float(r['insertion_probe_average']):.4f} "
                     f"{float(r['positive_lookup_probe_average']):.4f} "
                     f"{r['elastic_case_three_insertions']} {r['elastic_case_one_fallbacks']} "
                     f"{r['elastic_case_one_insertions']} {r['elastic_case_two_insertions']}\n")
    print(f"    -> {D}/csweep.dat")
PYEOF
echo

# --------------------------------------------------------------- summary
echo "=== Summary: the numbers quoted in the report ==========================="
python3 - "$OUT" <<'PYEOF'
import csv, os, sys, math
D = sys.argv[1]
p = os.path.join(D,'wordlist-sweep-probes.csv')
if not os.path.exists(p):
    sys.exit(0)
rows = list(csv.DictReader(open(p)))
print(f"{'impl':9} {'load%':>9} {'ins avg':>9} {'ins max':>8} {'srch avg':>10} {'srch max':>9} {'neg':>9}  correct")
for r in rows:
    print(f"{r['implementation']:9} {float(r['load_factor'])*100:9.4f} "
          f"{float(r['insertion_probe_average']):9.2f} {int(r['insertion_probe_maximum']):8d} "
          f"{float(r['positive_lookup_probe_average']):10.2f} "
          f"{int(r['positive_lookup_probe_maximum']):9d} {int(r['negative_lookup_probes']):9d}"
          f"     {'YES' if r['correct']=='1' else 'NO'}")

print()
print("Section 6.7, measured average divided by the growth each theorem predicts.")
print("A flat column means the measurement follows that shape.")
print(f"{'L':>3} {'ctrl avg':>9} {'/(1+2^L)/2':>11} {'elastic':>9} {'/L':>8} {'funnel':>8} {'/L':>7} {'a*b':>6} {'max/a*b':>8}")
by = {}
for r in rows:
    L = round(-math.log2(float(r['delta'])))
    by.setdefault(L, {})[r['implementation']] = r
for L in sorted(by):
    g = by[L]
    if not all(k in g for k in ('standard','elastic','funnel')): continue
    c  = float(g['standard']['positive_lookup_probe_average'])
    e  = float(g['elastic']['positive_lookup_probe_average'])
    f  = float(g['funnel']['positive_lookup_probe_average'])
    fm = int(g['funnel']['insertion_probe_maximum'])
    knuth = (1 + 2**L) / 2
    a = math.ceil(4*L) + 10; b = math.ceil(2*L); ab = a*b
    print(f"{L:3d} {c:9.2f} {c/knuth:11.2f} {e:9.1f} {e/L:8.1f} {f:8.2f} {f/L:7.2f} {ab:6d} {fm/ab:8.2f}")
PYEOF
echo
echo "Done. All outputs in: $OUT"
