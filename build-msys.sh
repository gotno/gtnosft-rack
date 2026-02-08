#!/bin/bash
# MSYS2/MinGW build wrapper for gtnosft-rack
# This script manages a separate build cache (build-msys/) to avoid conflicts
# with the WSL2 Linux build environment that shares this repository.
#
# THIS SCRIPT WAS PRIMARILY WRITTEN BY COPILOT
#
# Usage: ./build-msys.sh [make arguments]
# Examples:
#   ./build-msys.sh          # build the plugin
#   ./build-msys.sh clean    # clean build artifacts
#   ./build-msys.sh install  # install the plugin
#   ./build-msys.sh dist     # create distribution package

set -euo pipefail

# Configuration for this build environment
readonly BUILD_CACHE="build-msys"
readonly BUILD_NAME="build-msys"

# Get the directory where this script is located
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Source common build library
# shellcheck source=build-common.sh
source "$SCRIPT_DIR/build-common.sh"

# Validate we're in the expected environment
validate_environment "msys"

# Run the build with all passed arguments
run_build "$@"
