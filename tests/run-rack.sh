#!/bin/bash
# Headless Linux Rack-linked tests; no window or graphics context is created.
# Build the plugin with ./build-wsl.sh first.
# Usage: tests/run-rack.sh [name filter]
set -euo pipefail
cd "$(dirname "$0")/.."

RACK_DIR="$(realpath "${RACK_DIR:-../..}")"
if [[ ! -f plugin.so || ! -f "$RACK_DIR/libRack.so" ]]; then
  echo "requires plugin.so and RACK_DIR/libRack.so; run ./build-wsl.sh first" >&2
  exit 1
fi

mkdir -p build-tests
g++ -std=c++20 -O1 -g -Wall -Wno-deprecated \
  -I"$RACK_DIR/include" -I"$RACK_DIR/dep/include" -Itests -Isrc -Idependencies \
  tests/main.cpp tests/rack/*.cpp \
  -L. -l:plugin.so -L"$RACK_DIR" -lRack \
  -Wl,-rpath,"$PWD" -Wl,-rpath,"$RACK_DIR" -o build-tests/rack
build-tests/rack "$@"
