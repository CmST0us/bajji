#!/usr/bin/env bash
set -euo pipefail

test_dir="$(cd "$(dirname "$0")" && pwd)"
device_dir="$(cd "$test_dir/../.." && pwd)"
out_dir="$(mktemp -d)"
trap 'rm -rf "$out_dir"' EXIT

c++ -std=c++20 -Wall -Wextra -Werror \
  -I"$device_dir/components/board_hal" \
  "$test_dir/test_board_math.cpp" \
  -o "$out_dir/test_board_math"
"$out_dir/test_board_math"
