#!/bin/bash
# Interleaved A/B of main's matcher against the current one, with main2 as the A/A.
# Prints per-round ns and run-level geometric-mean ratios with a 95% interval. Run from the repo root.
set -euo pipefail
mkdir -p .scratch
g++ -std=c++20 -O2 -I. -Ifuzz -DNS=radix -DHDR='"radix_router.h"' bench/ab.cc -o .scratch/ab_fix
g++ -std=c++20 -O2 -I. -Ifuzz -DNS=radix_main -DHDR='"radix_router_main.h"' bench/ab.cc -o .scratch/ab_main
rounds=${ROUNDS:-10}
for i in $(seq "$rounds"); do
	if (( i % 2 )); then a=$(.scratch/ab_main); b=$(.scratch/ab_fix); c=$(.scratch/ab_main)
	else c=$(.scratch/ab_main); b=$(.scratch/ab_fix); a=$(.scratch/ab_main); fi
	echo "round $i main ${a% *} fix ${b% *} main2 ${c% *}"
done | tee .scratch/ab.txt
awk '/^round/ { n++; lf[n]=log($6/$4); lm[n]=log($8/$4) }
END {
	split("fix/main main2/main", name, " ")
	for (k = 1; k <= 2; k++) {
		s = 0; for (i = 1; i <= n; i++) { s += (k == 1 ? lf[i] : lm[i]) }
		m = s / n; v = 0
		for (i = 1; i <= n; i++) { d = (k == 1 ? lf[i] : lm[i]) - m; v += d * d }
		se = sqrt(v / (n - 1) / n)
		printf "%s %.3f [%.3f, %.3f] over %d rounds\n", name[k], exp(m), exp(m - 2.26 * se), exp(m + 2.26 * se), n
	}
}' .scratch/ab.txt
