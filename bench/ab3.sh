#!/bin/bash
# Diagnostic: main, the first memo version (fuzz/radix_router_v1.h) and the current header, interleaved.
set -euo pipefail
mkdir -p .scratch
g++ -std=c++20 -O2 -I. -Ifuzz -DNS=radix -DHDR='"radix_router.h"' bench/ab.cc -o .scratch/b_cur
g++ -std=c++20 -O2 -I. -Ifuzz -DNS=radix_v1 -DHDR='"radix_router_v1.h"' bench/ab.cc -o .scratch/b_v1
g++ -std=c++20 -O2 -I. -Ifuzz -DNS=radix_main -DHDR='"radix_router_main.h"' bench/ab.cc -o .scratch/b_main
for i in $(seq "${ROUNDS:-8}"); do
	echo "round $i main $(.scratch/b_main | cut -d' ' -f1) v1 $(.scratch/b_v1 | cut -d' ' -f1) cur $(.scratch/b_cur | cut -d' ' -f1) main2 $(.scratch/b_main | cut -d' ' -f1)"
done
