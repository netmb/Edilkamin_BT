#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
TEST_BUILD_DIR="$(mktemp -d "${TMPDIR:-/tmp}/edilkamin-tests.XXXXXX")"
trap 'rm -rf "$TEST_BUILD_DIR"' EXIT
CXX="${CXX:-c++}"
JSON_INCLUDE="${ARDUINOJSON_INCLUDE:-.pio/libdeps/wemos_d1_mini32/ArduinoJson/src}"
if [[ ! -f "$JSON_INCLUDE/ArduinoJson.h" ]]; then
  echo "ArduinoJson headers missing: run 'pio pkg install -e wemos_d1_mini32' first."
  exit 1
fi
FLAGS=(-std=c++11 -Wall -Wextra -Werror -fsanitize=address,undefined -Isrc)
"$CXX" "${FLAGS[@]}" tests/fan_control_test.cpp -o "$TEST_BUILD_DIR/fan-control"
"$TEST_BUILD_DIR/fan-control"
"$CXX" "${FLAGS[@]}" -Itests/stubs -I"$JSON_INCLUDE" tests/fan_discovery_test.cpp -o "$TEST_BUILD_DIR/fan-discovery"
"$TEST_BUILD_DIR/fan-discovery"
"$CXX" "${FLAGS[@]}" tests/stove_protocol_test.cpp -o "$TEST_BUILD_DIR/stove-protocol"
"$TEST_BUILD_DIR/stove-protocol"
"$CXX" "${FLAGS[@]}" -I"$JSON_INCLUDE" tests/sensor_discovery_test.cpp -o "$TEST_BUILD_DIR/sensor-discovery"
"$TEST_BUILD_DIR/sensor-discovery"
python3 tests/extract_firmware.py "$TEST_BUILD_DIR/firmware_under_test.inc"
"$CXX" "${FLAGS[@]}" -Itests/stubs -I"$JSON_INCLUDE" -I"$TEST_BUILD_DIR" tests/firmware_flow_test.cpp -o "$TEST_BUILD_DIR/firmware-flow"
"$TEST_BUILD_DIR/firmware-flow"
