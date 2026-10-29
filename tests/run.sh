#!/bin/bash
# Builds and runs the Rack-free unit tests (packet, encoder, pacer).
# Needs libopus first: ./build-wsl.sh dep (or ./build-msys.sh dep).
# Usage: tests/run.sh [name filter]
set -euo pipefail
cd "$(dirname "$0")/.."

if [[ -z "${DEP_LOCAL:-}" ]]; then
  case "$(uname -s)" in
    MINGW*|MSYS*) DEP_LOCAL=dep-msys ;;
    *) DEP_LOCAL=dep-wsl ;;
  esac
fi
if [[ ! -f "$DEP_LOCAL/lib/libopus.a" ]]; then
  echo "missing $DEP_LOCAL/lib/libopus.a; run ./build-wsl.sh dep first" >&2
  exit 1
fi

sources=(tests/*.cpp)
for f in src/audio/Packet.cpp src/audio/AudioEncoder.cpp src/osc/ChunkPacer.cpp; do
  if [[ -f "$f" ]]; then sources+=("$f"); fi
done

mkdir -p build-tests
g++ -std=c++20 -O1 -g -Wall -Isrc -I"$DEP_LOCAL/include" \
  "${sources[@]}" "$DEP_LOCAL/lib/libopus.a" -lm -lpthread -o build-tests/unit
build-tests/unit "$@"
