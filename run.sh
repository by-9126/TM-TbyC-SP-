#!/bin/bash
# Build and run the order system from the folder that contains this script.
cd "$(dirname "$0")" || exit 1
mkdir -p input output
gcc -Wall -Wextra -pthread -o main main.c || { echo "Build failed."; exit 1; }
./main
