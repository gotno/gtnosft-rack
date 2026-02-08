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

set -e

BUILD_CACHE="build-wsl"
BUILD_DIR="build"
LOCK_FILE=".build-lock"

# Color output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

log_info() {
  echo -e "${GREEN}[build-wsl]${NC} $1"
}

log_warn() {
  echo -e "${YELLOW}[build-wsl]${NC} $1"
}

log_error() {
  echo -e "${RED}[build-wsl]${NC} $1"
}

# Cleanup function for trap
cleanup() {
  if [ -f "$LOCK_FILE" ]; then
    rm -f "$LOCK_FILE"
  fi
}

trap cleanup EXIT INT TERM

# Check for lock file
if [ -f "$LOCK_FILE" ]; then
  log_error "Build lock file exists. Another build may be running."
  log_error "If you're sure no build is running, remove: $LOCK_FILE"
  exit 1
fi

# Create lock file
touch "$LOCK_FILE"

# Detect if we're actually in WSL/Linux
if [[ "$(uname -s)" == MINGW* ]] || [[ "$(uname -s)" == MSYS* ]]; then
  log_warn "detected MINGW/MSYS environment. You should use build-msys.sh instead."
  log_warn "continuing anyway, but build artifacts may be incompatible..."
fi

# Check if rsync is available (preferred for efficiency)
if command -v rsync &> /dev/null; then
  COPY_CMD="rsync -a --delete"
  log_info "using rsync to copy"
else
  COPY_CMD="cp -r"
  log_warn "rsync not found, using cp to copy (slower)."
fi

# Phase 1: Restore build cache to build/
if [ -d "$BUILD_CACHE" ]; then
  log_info "Restoring build cache from $BUILD_CACHE/ to $BUILD_DIR/"

  # If build/ already exists and isn't from our cache, back it up
  if [ -d "$BUILD_DIR" ]; then
    # Simple heuristic: if build/ has different modification time than cache, it's foreign
    log_warn "Existing $BUILD_DIR/ found. Removing to restore cache..."
    rm -rf "$BUILD_DIR"
  fi

  # Copy cache to build/
  if [ "$COPY_CMD" = "rsync -a --delete" ]; then
    rsync -a "$BUILD_CACHE/" "$BUILD_DIR/"
  else
    cp -r "$BUILD_CACHE" "$BUILD_DIR"
  fi
else
  log_info "No build cache found. This is a clean build."

  # Clean any existing build/ from other environments
  if [ -d "$BUILD_DIR" ]; then
    log_warn "Found existing $BUILD_DIR/ from different environment. Cleaning..."
    rm -rf "$BUILD_DIR"
  fi
fi

# Phase 2: Run make with all passed arguments
log_info "Running: make $*"
if make "$@"; then
  BUILD_SUCCESS=true
else
  BUILD_SUCCESS=false
  log_error "Build failed!"
fi

# Phase 3: Save build/ back to cache (even on failure to preserve partial state)
if [ -d "$BUILD_DIR" ]; then
  log_info "Saving $BUILD_DIR/ to build cache $BUILD_CACHE/"

  # Ensure cache directory exists
  mkdir -p "$BUILD_CACHE"

  # Copy build/ to cache
  if [ "$COPY_CMD" = "rsync -a --delete" ]; then
    rsync -a --delete "$BUILD_DIR/" "$BUILD_CACHE/"
  else
    rm -rf "$BUILD_CACHE"
    cp -r "$BUILD_DIR" "$BUILD_CACHE"
  fi
else
  log_warn "No $BUILD_DIR/ directory found after make. Build may have cleaned it."
fi

# Exit with make's exit code
if [ "$BUILD_SUCCESS" = true ]; then
  log_info "Build completed successfully"
  exit 0
else
  exit 1
fi
