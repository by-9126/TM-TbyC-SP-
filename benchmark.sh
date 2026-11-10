#!/bin/bash
# Multi-threaded E-Commerce Order System Benchmark Script
cd "$(dirname "$0")" || exit 1

# Ensure binary is built
if [ ! -f ./main ]; then
    echo "Compiling main..."
    make || gcc -Wall -Wextra -pthread -O2 -o main main.c || exit 1
fi

echo "=========================================================="
echo " Starting Automated Multi-threaded Benchmark"
echo "=========================================================="
./main --benchmark 3

