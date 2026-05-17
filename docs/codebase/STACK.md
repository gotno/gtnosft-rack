# Technology Stack

## Core Sections (Required)

### 1) Runtime Summary

| Area | Value | Evidence |
|------|-------|----------|
| Primary language | C++20 | `Makefile` (`CXXFLAGS += -std=c++20`) |
| Runtime + version | VCV Rack SDK 2.6.4 (plugin, not standalone binary) | `.github/workflows/build-plugin.yml` |
| Package manager | None — dependencies are vendored | `dependencies/` |
| Module/build system | GNU Make + VCV Rack `plugin.mk` | `Makefile` |

### 2) Production Frameworks and Dependencies

| Dependency | Version | Role in system | Evidence |
|------------|---------|----------------|----------|
| VCV Rack SDK | 2.6.4 | Plugin API — modules, widgets, engine, patch management | `Makefile` (`RACK_DIR`), `plugin.json` |
| oscpack | vendored (no version tag) | OSC message encoding/decoding and UDP send/receive | `dependencies/oscpack/` |
| qoi | vendored (single header) | Lossless pixel data compression for texture chunks | `dependencies/qoi/qoi.h` |
| rapidhash | vendored (single header) | Fast 64-bit hashing for texture ID deduplication | `dependencies/rapidhash/rapidhash.h` |
| stb_image_write | vendored (single header) | PNG encoding for debug image export | `dependencies/stb_image_write.h` |
| NanoVG / OpenGL | Provided by Rack SDK | Off-screen framebuffer rendering of module widgets | `src/texture/Renderer.cpp` |

### 3) Development Toolchain

| Tool | Purpose | Evidence |
|------|---------|----------|
| GitHub Actions | CI — cross-platform plugin builds | `.github/workflows/build-plugin.yml` |
| `ghcr.io/qno/rack-plugin-toolchain-win-linux` | Docker build image for win-x64 and lin-x64 | `.github/workflows/build-plugin.yml` |
| macOS native runner | CI build for mac-x64 and mac-arm64 | `.github/workflows/build-plugin.yml` |
| `build-wsl.sh` / `build-msys.sh` | Local developer build scripts (WSL and MSYS2) | `build-wsl.sh`, `build-msys.sh` |

No linter, formatter, or test runner is configured.

### 4) Key Commands

```bash
# Build (requires RACK_DIR pointing to Rack SDK)
make dep && make dist

# Local WSL build
./build-wsl.sh

# Local MSYS2 (Windows) build
./build-msys.sh

# CI: cross-platform build via rack-plugin-toolchain
make plugin-build-win-x64
make plugin-build-lin-x64
```

### 5) Environment and Config

- Config sources: `plugin.json` (plugin manifest / version), `Makefile` (compiler flags, sources)
- Required env vars: `RACK_DIR` — path to Rack SDK; set automatically in CI by toolchain
- Deployment/runtime constraints: compiled as a shared library (`.vcvplugin` zip) loaded by VCV Rack at runtime; no standalone process

### 6) Evidence

- `Makefile`
- `plugin.json`
- `.github/workflows/build-plugin.yml`
- `dependencies/` (all vendored libs)
