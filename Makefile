# If RACK_DIR is not defined when calling the Makefile, default to two directories above
RACK_DIR ?= ../..

# FLAGS will be passed to both the C and C++ compiler
FLAGS += -I./dependencies
CFLAGS +=
CXXFLAGS += -std=c++20

# `make BENCH=1` compiles in benchmark instrumentation (src/bench/Bench.hpp)
BENCH ?= 0
ifneq ($(BENCH),0)
	FLAGS += -DGTNOSFT_BENCH
endif

# libopus, built static into $(DEP_LOCAL). build-wsl.sh / build-msys.sh set
# DEP_LOCAL per environment, since they share this checkout. CI uses `dep`.
DEP_LOCAL ?= dep
OPUS_VERSION := 1.5.2
libopus := $(DEP_LOCAL)/lib/libopus.a
OBJECTS += $(libopus)
DEPS += $(libopus)
FLAGS += -I$(DEP_LOCAL)/include

# Careful about linking to shared libraries, since you can't assume much about the user's environment and library search path.
# Static libraries are fine, but they should be added to this plugin's build system.

# defines for ARCH_WIN, ARCH_LIN, ARCH_MAC
include $(RACK_DIR)/arch.mk

MACHINE = $(shell $(CC) -dumpmachine)
ifneq (, $(findstring mingw, $(MACHINE)))
	SOURCES += $(wildcard dependencies/oscpack/ip/win32/*.cpp)
	LDFLAGS += -lws2_32 -lwinmm -liphlpapi -lqwave
	LDFLAGS += -lopengl32
	LDFLAGS += -L$(RACK_DIR)/dep/lib
else
	SOURCES += $(wildcard dependencies/oscpack/ip/posix/*.cpp)
	LDFLAGS += -lstdc++
endif

ifdef ARCH_MAC
	LDFLAGS += -framework CoreFoundation -framework SystemConfiguration
endif


# Add .cpp files to the build
SOURCES += $(wildcard src/*.cpp)
SOURCES += $(wildcard src/bench/*.cpp)
SOURCES += $(wildcard src/texture/*.cpp)
SOURCES += $(wildcard src/util/*.cpp)
SOURCES += $(wildcard src/audio/*.cpp)
SOURCES += $(wildcard src/osc/*.cpp)
SOURCES += $(wildcard src/osc/MessagePacker/*.cpp)
SOURCES += $(wildcard src/osc/Bundler/*.cpp)
SOURCES += $(wildcard src/osc/ChunkedSend/*.cpp)
SOURCES += $(wildcard dependencies/oscpack/ip/*.cpp)
SOURCES += $(wildcard dependencies/oscpack/osc/*.cpp)

# Add files to the ZIP package when running `make dist`
# The compiled plugin and "plugin.json" are automatically added.
DISTRIBUTABLES += res
DISTRIBUTABLES += $(wildcard LICENSE*)
DISTRIBUTABLES += dependencies/oscpack/LICENSE
DISTRIBUTABLES += dependencies/qoi/LICENSE
DISTRIBUTABLES += dependencies/rapidhash/LICENSE
DISTRIBUTABLES += $(wildcard presets)

include $(RACK_DIR)/plugin.mk

CXXFLAGS := $(filter-out -std=c++11,$(CXXFLAGS))

OPUS_CMAKE_FLAGS := -DCMAKE_BUILD_TYPE=Release \
	-DOPUS_BUILD_SHARED_LIBRARY=OFF -DOPUS_BUILD_TESTING=OFF -DOPUS_BUILD_PROGRAMS=OFF \
	-DOPUS_INSTALL_PKG_CONFIG_MODULE=OFF -DOPUS_INSTALL_CMAKE_CONFIG_MODULE=OFF \
	-DOPUS_DRED=OFF -DOPUS_OSCE=OFF -DOPUS_FIXED_POINT=OFF \
	-DOPUS_STACK_PROTECTOR=OFF -DOPUS_FORTIFY_SOURCE=OFF \
	-DCMAKE_POSITION_INDEPENDENT_CODE=ON
ifdef ARCH_MAC
ifdef ARCH_ARM64
	OPUS_CMAKE_FLAGS += -DCMAKE_OSX_ARCHITECTURES=arm64
else
	OPUS_CMAKE_FLAGS += -DCMAKE_OSX_ARCHITECTURES=x86_64
endif
endif

$(libopus):
	cd $(DEP_LOCAL) && $(WGET) "https://downloads.xiph.org/releases/opus/opus-$(OPUS_VERSION).tar.gz"
	cd $(DEP_LOCAL) && $(SHA256) opus-$(OPUS_VERSION).tar.gz 65c1d2f78b9f2fb20082c38cbe47c951ad5839345876e46941612ee87f9a7ce1
	cd $(DEP_LOCAL) && $(UNTAR) opus-$(OPUS_VERSION).tar.gz
	cd $(DEP_LOCAL)/opus-$(OPUS_VERSION) && $(CMAKE) -B build $(OPUS_CMAKE_FLAGS)
	$(MAKE) -C $(DEP_LOCAL)/opus-$(OPUS_VERSION)/build
	$(MAKE) -C $(DEP_LOCAL)/opus-$(OPUS_VERSION)/build install

# audio sources include opus.h
$(filter build/src/audio/%,$(OBJECTS)): | $(libopus)

# rebuild everything when BENCH changes, since make doesn't track flags
# (but not libopus)
BENCH_STAMP := build/bench-$(BENCH).stamp
$(filter-out $(libopus),$(OBJECTS)): $(BENCH_STAMP)
$(BENCH_STAMP):
	@mkdir -p build
	@rm -f build/bench-*.stamp
	@touch $@
