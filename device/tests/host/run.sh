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

cc -std=c11 -Wall -Wextra -Werror \
  -I"$device_dir/components/bridge_protocol/include" \
  "$test_dir/test_bridge_protocol.c" \
  "$device_dir/components/bridge_protocol/bridge_protocol.c" \
  -o "$out_dir/test_bridge_protocol"
"$out_dir/test_bridge_protocol"

cc -std=c11 -Wall -Wextra -Werror \
  -I"$device_dir/components/ip_bridge/include" \
  -I"$device_dir/components/bridge_protocol/include" \
  "$test_dir/test_ipv4_packet.c" \
  "$device_dir/components/ip_bridge/ipv4_packet.c" \
  -o "$out_dir/test_ipv4_packet"
"$out_dir/test_ipv4_packet"

cc -std=c11 -Wall -Wextra -Werror \
  -I"$device_dir/components/wallpaper/include" \
  "$test_dir/test_wallpaper_format.c" \
  "$device_dir/components/wallpaper/wallpaper_format.c" \
  -o "$out_dir/test_wallpaper_format"
"$out_dir/test_wallpaper_format"

c++ -std=c++20 -Wall -Wextra -Werror \
  -I"$device_dir/components/board_hal" \
  "$test_dir/test_button_state.cpp" \
  -o "$out_dir/test_button_state"
"$out_dir/test_button_state"

c++ -std=c++20 -Wall -Wextra -Werror \
  -I"$device_dir/components/wallpaper/include" \
  "$test_dir/test_wallpaper_schedule.cpp" \
  -o "$out_dir/test_wallpaper_schedule"
"$out_dir/test_wallpaper_schedule"
