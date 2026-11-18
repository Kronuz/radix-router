#!/bin/bash
# A/B/A/B plus A/A of main's matcher against the current one. Run from the repo root.
set -euo pipefail
mkdir -p .scratch
g++ -std=c++20 -O2 -I. -Ifuzz -DNS=radix -DHDR='"radix_router.h"' bench/ab.cc -o .scratch/ab_fix
g++ -std=c++20 -O2 -I. -Ifuzz -DNS=radix_main -DHDR='"radix_router_main.h"' bench/ab.cc -o .scratch/ab_main
for i in 1 2 3; do
	echo "main  $(.scratch/ab_main)"
	echo "fix   $(.scratch/ab_fix)"
	echo "main2 $(.scratch/ab_main)"
done
