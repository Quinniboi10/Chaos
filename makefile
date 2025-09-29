DEFAULT_VALUE_NET = Chaos_10.value
DEFAULT_POLICY_NET = Chaos_09.policy

# Detect Operating System
ifeq ($(OS),Windows_NT)
    # Windows settings
    RM := del /F /Q
    EXE_EXT := .exe
else
    # Unix/Linux settings
    RM := rm -f
    EXE_EXT :=
endif

# Short commit id of HEAD (thanks to Weiss for this!)
GIT_HEAD_COMMIT_ID_RAW := $(shell git rev-parse --short HEAD)
ifneq ($(GIT_HEAD_COMMIT_ID_RAW),)
GIT_HEAD_COMMIT_ID_DEF := -DGIT_HEAD_COMMIT_ID=\""$(GIT_HEAD_COMMIT_ID_RAW)"\"
else
GIT_HEAD_COMMIT_ID_DEF :=
endif

# Compiler and flags
CXX      := clang++
CXXFLAGS := -O3 -fno-finite-math-only -funroll-loops -flto -std=c++20 -DNDEBUG

ifeq ($(OS),Windows_NT)
  ARCH := $(PROCESSOR_ARCHITECTURE)
else
  ARCH := $(shell uname -m)
endif

IS_ARM := $(filter ARM arm64 aarch64 arm%,$(ARCH))

ifeq ($(IS_ARM),)
  LINKFLAGS := -fuse-ld=lld -pthread
  ARCHFLAGS := -march=native
else
  LINKFLAGS :=
  ARCHFLAGS := -mcpu=native
endif


# Default target executable name and evaluation file path
EXE      ?= Chaos$(EXE_EXT)

# Source and object files
SRCS     := $(wildcard ./src/*.cpp)
SRCS     += ./external/fmt/format.cpp
OBJS     := $(SRCS:.cpp=.o)

# Default target
all: downloadV
all: downloadP
all: $(EXE)

# Build the objects
%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(ARCHFLAGS) $(GIT_HEAD_COMMIT_ID_DEF) -DVALUEFILE="\"$(VALUEFILE)\"" -DPOLICYFILE="\"$(POLICYFILE)\"" -c $< -o $@

CXXFLAGS += -MMD -MP
DEPS := $(OBJS:.o=.d)
-include $(DEPS)

# Link the executable
$(EXE): $(OBJS)
	$(CXX) $(CXXFLAGS) $(OBJS) $(LINKFLAGS) -o $@

# Net repo URL
NET_BASE_URL := https://git.nocturn9x.space/Quinniboi10/Chaos-Nets/raw/branch/main

# Net file names
VALUEFILE  ?= $(DEFAULT_VALUE_NET)
POLICYFILE ?= $(DEFAULT_POLICY_NET)

# Files for make clean
CLEAN_STUFF := $(EXE) Chaos.exp Chaos.lib Chaos.pdb $(OBJS) $(DEPS) $(DEFAULT_POLICY_NET) $(DEFAULT_VALUE_NET)
ifeq ($(OS),Windows_NT)
    CLEAN_STUFF := $(subst /,\\,$(CLEAN_STUFF))
endif

# Downlaod if the net is not specified
ifeq ($(VALUEFILE),$(DEFAULT_VALUE_NET))
downloadV: $(DEFAULT_VALUE_NET)
else
downloadV:
	@echo "VALUEFILE is set to '$(VALUEFILE)', skipping download."
endif

ifeq ($(POLICYFILE),$(DEFAULT_POLICY_NET))
downloadP: $(DEFAULT_POLICY_NET)
else
downloadP:
	@echo "POLICYFILE is set to '$(POLICYFILE)', skipping download."
endif

# Rules to create the files if they don't exist
$(DEFAULT_VALUE_NET):
	curl -L -o $@ $(NET_BASE_URL)/$@

$(DEFAULT_POLICY_NET):
	curl -L -o $@ $(NET_BASE_URL)/$@

# Release (static) build
.PHONY: release
release: CXXFLAGS += -static
release: all

# TUI build
.PHONY: tui
tui: CXXFLAGS  += -DENABLE_TUI
tui: LINKFLAGS += -lncursesw
tui: all

# Debug build
.PHONY: debug
debug: CXXFLAGS = -std=c++23 -O2 -fno-inline-functions -flto -ggdb -DDEBUG -fsanitize=address -fsanitize=undefined -fno-finite-math-only -fno-omit-frame-pointer -rdynamic -DBOOST_STACKTRACE_USE_ADDR2LINE -D_GLIBCXX_DEBUG -D_GLIBCXX_DEBUG_PEDANTIC -Wall -Wextra
debug: all

# Debug build
.PHONY: profile
profile: CXXFLAGS = -O3 -g -fno-finite-math-only -funroll-loops -flto -std=c++20 -fno-omit-frame-pointer -DNDEBUG
profile: all

# Force rebuild
.PHONY: force
force: clean
force: all

# Clean up
.PHONY: clean
clean:
	$(RM) $(CLEAN_STUFF)
