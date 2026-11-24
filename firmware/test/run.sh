#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")"
c++ -std=c++17 -I ../brainos test_host.cpp -o test_host
./test_host
