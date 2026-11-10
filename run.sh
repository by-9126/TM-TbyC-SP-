#!/bin/bash
# Build and run the order system from the folder that contains this script.
cd "$(dirname "$0")" || exit 1
mkdir -p input output

if command -v make >/dev/null 2>&1; then
    make
else
    gcc -Wall -Wextra -pthread -O2 -o main main.c || { echo "Build failed."; exit 1; }
fi

./main "$@"
