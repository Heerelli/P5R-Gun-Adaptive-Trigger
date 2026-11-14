#!/bin/sh
# Builds the host tests and disassembles the generated hook stub.
set -e
cd "$(dirname "$0")/.."
g++ -std=c++17 -Wall -Wextra -I src tests/test_effects.cpp -o /tmp/p5r_trigger_test
/tmp/p5r_trigger_test /tmp/p5r_stub.bin
objdump -D -b binary -mi386:x86-64 -M intel /tmp/p5r_stub.bin | tail -n +8
