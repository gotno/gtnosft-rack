# If RACK_DIR is not defined when calling the Makefile, default to two directories above
RACK_DIR ?= ../..

# FLAGS will be passed to both the C and C++ compiler
FLAGS += -I./dependencies
FLAGS += -I./dependencies/asio
CFLAGS +=
CXXFLAGS += -std=c++20

# Careful about linking to shared libraries, since you can't assume much about the user's environment and library search path.
# Static libraries are fine, but they should be added to this plugin's build system.

# defines for ARCH_WIN, ARCH_LIN, ARCH_MAC
include $(RACK_DIR)/arch.mk

MACHINE = $(shell $(CC) -dumpmachine)
ifneq (, $(findstring mingw, $(MACHINE)))
	SOURCES += $(wildcard dependencies/oscpack/ip/win32/*.cpp)
	LDFLAGS += -lws2_32 -lwinmm -liphlpapi
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
SOURCES += $(wildcard src/osc/*.cpp)
SOURCES += $(wildcard src/osc/MessagePacker/*.cpp)
SOURCES += $(wildcard src/osc/ChunkedSend/*.cpp)
SOURCES += $(wildcard dependencies/oscpack/ip/*.cpp)
SOURCES += $(wildcard dependencies/oscpack/osc/*.cpp)

# Add files to the ZIP package when running `make dist`
# The compiled plugin and "plugin.json" are automatically added.
DISTRIBUTABLES += res
DISTRIBUTABLES += $(wildcard LICENSE*)
DISTRIBUTABLES += dependencies/oscpack/LICENSE
DISTRIBUTABLES += dependencies/qoi/LICENSE
DISTRIBUTABLES += dependencies/asio/LICENSE_1_0.txt
DISTRIBUTABLES += $(wildcard presets)

include $(RACK_DIR)/plugin.mk

CXXFLAGS := $(filter-out -std=c++11,$(CXXFLAGS))
