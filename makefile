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
CXXFLAGS := -O3 -march=native -fno-finite-math-only -funroll-loops -flto -std=c++20 -DNDEBUG

ifeq ($(OS),Windows_NT)
  ARCH := $(PROCESSOR_ARCHITECTURE)
else
  ARCH := $(shell uname -m)
endif

IS_ARM := $(filter ARM arm64 aarch64 arm%,$(ARCH))

ifeq ($(IS_ARM),)
  CXXFLAGS += -static -fuse-ld=lld
endif


# Default target executable name and evaluation file path
EXE      ?= Chaos$(EXE_EXT)

# Source and object files
SRCS     := $(wildcard ./src/*.cpp)
OBJS     := $(SRCS:.cpp=.o)

# Default target
all: $(EXE)

# Link the executable
$(EXE): $(SRCS)
	$(CXX) $(CXXFLAGS) $(SRCS) ./external/fmt/format.cc $(GIT_HEAD_COMMIT_ID_DEF) -o $@

# Debug build
.PHONY: debug
debug: clean
debug: CXXFLAGS = -march=native -std=c++23 -O2 -fno-inline -fno-inline-functions -ldl -ggdb -DDEBUG -fsanitize=address -no-pie -fno-pie  -fsanitize=undefined -fno-finite-math-only -fno-omit-frame-pointer -DBOOST_STACKTRACE_USE_ADDR2LINE -D_GLIBCXX_DEBUG -D_GLIBCXX_DEBUG_PEDANTIC -Wall -Wextra
debug: all

# Debug build
.PHONY: profile
profile: clean
profile: CXXFLAGS = -O3 -g -march=native -fno-inline-functions -fno-finite-math-only -funroll-loops -flto -fuse-ld=lld -std=c++20 -fno-omit-frame-pointer -static -DNDEBUG
profile: all

# Force rebuild
.PHONY: force
force: clean all

# Clean up
.PHONY: clean
clean:
	$(RM) $(EXE)
	$(RM) Chaos.exp
	$(RM) Chaos.lib
	$(RM) Chaos.pdb
