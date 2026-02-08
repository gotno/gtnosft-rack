#!/bin/bash
# WSL2/Linux build wrapper for gtnosft-rack
# This script manages a separate build cache (build-wsl/) to avoid conflicts
# with the MSYS2 Windows build environment that shares this repository.
#
# THIS SCRIPT WAS PRIMARILY WRITTEN BY COPILOT
#
# Usage: ./build-wsl.sh [make arguments]
# Examples:
#   ./build-wsl.sh          # build the plugin
#   ./build-wsl.sh clean    # clean build artifacts
#   ./build-wsl.sh install  # install the plugin
#   ./build-wsl.sh dist     # create distribution package

set -euo pipefail

# Configuration for this build environment
readonly BUILD_CACHE="build-wsl"
readonly BUILD_NAME="build-wsl"

# Get the directory where this script is located
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Source common build library
# shellcheck source=build-common.sh
source "$SCRIPT_DIR/build-common.sh"

# Validate we're in the expected environment
validate_environment "wsl"

# Run the build with all passed arguments
run_build "$@"
