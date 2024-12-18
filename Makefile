# If RACK_DIR is not defined when calling the Makefile, default to two directories above
RACK_DIR ?= ../..

# FLAGS will be passed to both the C and C++ compiler
FLAGS +=
CFLAGS +=
CXXFLAGS += -std=c++17

# Careful about linking to shared libraries, since you can't assume much about the user's environment and library search path.
# Static libraries are fine, but they should be added to this plugin's build system.

include $(RACK_DIR)/arch.mk

MACHINE = $(shell $(CC) -dumpmachine)
ifneq (, $(findstring mingw, $(MACHINE)))
	SOURCES += $(wildcard dep/oscpack/ip/win32/*.cpp) 
	LDFLAGS += -lws2_32 -lwinmm
	LDFLAGS += -lopengl32
	LDFLAGS += -L$(RACK_DIR)/dep/lib
else
	SOURCES += $(wildcard dep/oscpack/ip/posix/*.cpp) 
endif

# Add .cpp files to the build
SOURCES += $(wildcard src/*.cpp)
SOURCES += $(wildcard dep/oscpack/ip/*.cpp)
SOURCES += $(wildcard dep/oscpack/osc/*.cpp)

# Add files to the ZIP package when running `make dist`
# The compiled plugin and "plugin.json" are automatically added.
DISTRIBUTABLES += res
DISTRIBUTABLES += $(wildcard LICENSE*)
DISTRIBUTABLES += dep/oscpack/LICENSE
DISTRIBUTABLES += $(wildcard presets)

include $(RACK_DIR)/plugin.mk

CXXFLAGS := $(filter-out -std=c++11,$(CXXFLAGS))
