#!/bin/bash
# Common build wrapper library for gtnosft-rack
# Shared logic for managing separate build caches to avoid conflicts
# between WSL2/Linux and MSYS2/MinGW build environments.
#
# THIS SCRIPT WAS PRIMARILY WRITTEN BY COPILOT
# (and refactored by its friend the simplifier agent)
#
# This library should be sourced by build-wsl.sh and build-msys.sh

# Prevent direct execution
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
  echo "Error: This script should not be executed directly."
  echo "Source it from build-wsl.sh or build-msys.sh instead."
  exit 1
fi

# Validate required variables are set by caller
: "${BUILD_CACHE:?Error: BUILD_CACHE must be set before sourcing build-common.sh}"
: "${BUILD_NAME:?Error: BUILD_NAME must be set before sourcing build-common.sh}"

# Constants
readonly BUILD_DIR="build"
readonly LOCK_FILE=".build-lock"

# Color output constants
readonly RED='\033[0;31m'
readonly GREEN='\033[0;32m'
readonly YELLOW='\033[1;33m'
readonly NC='\033[0m' # No Color

# Logging functions with configurable prefix
log_info() {
  echo -e "${GREEN}[${BUILD_NAME}]${NC} $1"
}

log_warn() {
  echo -e "${YELLOW}[${BUILD_NAME}]${NC} $1"
}

log_error() {
  echo -e "${RED}[${BUILD_NAME}]${NC} $1"
}

# Cleanup function for trap
cleanup() {
  if [[ -f "$LOCK_FILE" ]]; then
    rm -f "$LOCK_FILE"
  fi
}

# Set up trap for cleanup
trap cleanup EXIT INT TERM

# Check and create lock file
acquire_build_lock() {
  if [[ -f "$LOCK_FILE" ]]; then
    log_error "Build lock file exists. Another build may be running."
    log_error "If you're sure no build is running, remove: $LOCK_FILE"
    exit 1
  fi
  touch "$LOCK_FILE"
}

# Detect and validate build environment
# Parameters:
#   $1: expected_env - "wsl" or "msys"
validate_environment() {
  local expected_env="$1"
  local uname_result
  uname_result="$(uname -s)"

  case "$expected_env" in
    wsl)
      if [[ "$uname_result" == MINGW* ]] || [[ "$uname_result" == MSYS* ]]; then
        log_warn "Detected MINGW/MSYS environment. You should use build-msys.sh instead."
        log_warn "Continuing anyway, but build artifacts may be incompatible..."
      fi
      ;;
    msys)
      if [[ "$uname_result" != MINGW* ]] && [[ "$uname_result" != MSYS* ]]; then
        log_warn "Not detected as MINGW/MSYS environment. You may want build-wsl.sh instead."
        log_warn "Continuing anyway, but build artifacts may be incompatible..."
      fi
      ;;
    *)
      log_error "Internal error: unknown environment type '$expected_env'"
      exit 1
      ;;
  esac
}

# Determine optimal copy command
setup_copy_command() {
  if command -v rsync &> /dev/null; then
    COPY_CMD="rsync"
    log_info "Using rsync for file operations"
  else
    COPY_CMD="cp"
    log_warn "rsync not found, using cp (slower). Consider installing rsync."
  fi
}

# Copy files using the detected copy command
# Parameters:
#   $1: source directory
#   $2: destination directory
copy_build_dir() {
  local src="$1"
  local dst="$2"

  case "$COPY_CMD" in
    rsync)
      rsync -a --delete "$src/" "$dst/"
      ;;
    cp)
      rm -rf "$dst"
      cp -r "$src" "$dst"
      ;;
  esac
}

# Restore build cache to build/ directory
restore_build_cache() {
  if [[ -d "$BUILD_CACHE" ]]; then
    log_info "Restoring build cache from $BUILD_CACHE/ to $BUILD_DIR/"

    if [[ -d "$BUILD_DIR" ]]; then
      log_warn "Existing $BUILD_DIR/ found. Removing to restore cache..."
      rm -rf "$BUILD_DIR"
    fi

    copy_build_dir "$BUILD_CACHE" "$BUILD_DIR"
  else
    log_info "No build cache found. This is a clean build."

    if [[ -d "$BUILD_DIR" ]]; then
      log_warn "Found existing $BUILD_DIR/ from different environment. Cleaning..."
      rm -rf "$BUILD_DIR"
    fi
  fi
}

# Execute make with passed arguments
# Parameters: all arguments are passed to make
run_make() {
  log_info "Running: make $*"

  # Use explicit variable instead of relying on exit code
  local build_result=0
  make "$@" || build_result=$?

  if [[ $build_result -eq 0 ]]; then
    return 0
  else
    log_error "Build failed with exit code $build_result"
    return $build_result
  fi
}

# Save build/ directory back to cache
save_build_cache() {
  if [[ -d "$BUILD_DIR" ]]; then
    log_info "Saving $BUILD_DIR/ to build cache $BUILD_CACHE/"

    mkdir -p "$BUILD_CACHE"
    copy_build_dir "$BUILD_DIR" "$BUILD_CACHE"
  else
    log_warn "No $BUILD_DIR/ directory found after make. Build may have cleaned it."
  fi
}

# Main build orchestration function
# Parameters: all arguments are passed to make
# Returns: make's exit code
run_build() {
  local build_result=0

  # Phase 1: Setup
  acquire_build_lock
  setup_copy_command

  # Phase 2: Restore cache
  restore_build_cache

  # Phase 3: Run make (capture exit code but don't exit yet)
  run_make "$@" || build_result=$?

  # Phase 4: Save cache (even on failure to preserve partial state)
  save_build_cache

  # Phase 5: Report results
  if [[ $build_result -eq 0 ]]; then
    log_info "Build completed successfully"
  fi

  return $build_result
}
