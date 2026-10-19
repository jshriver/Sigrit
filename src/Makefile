# Sigrit chess engine — Linux, macOS and Windows (MSYS2/MinGW), GCC or clang
#
#   make              optimized build for this CPU (O3 + LTO, net embedded) -> ./sigrit
#   make ARCH=x86-64-v3   build for another machine (see ARCH list below)
#   make pgo          profile-guided build (instrument, run bench, rebuild; GCC only)
#   make debug        -O1 -g with AddressSanitizer + UBSan -> ./sigrit-debug
#   make perft        perft test driver -> ./perft_test
#   make bench        build, then run the node-count bench
#   make print-arch   print the best ARCH this machine can run
#   make print-version  print the version string
#   make clean
#
# ARCH:
#   native       (default) detect this CPU and tune for it
#   x86-64-v4    AVX-512 machines (Intel Skylake-X/Ice Lake+, AMD Zen 4+)   alias: x86-64-avx512
#   x86-64-v3    AVX2 + BMI2 machines (Intel Haswell+, AMD Zen 3+)           alias: x86-64-bmi2
#   armv8        64-bit ARM (Apple Silicon, Raspberry Pi 4/5, Graviton, Snapdragon)
# x86-64-v3 is the minimum on x86. AMD Zen 1/2 run it, slowly (their PEXT is microcoded).
#
# Variables: EXE=name  ARCH=...  TUNE=cpu (adds -mtune)  NET=path/to/net.nnue
#            STATIC=yes|no (default yes; no on macOS)  LTO=yes|no  BENCH_DEPTH=13
#            VERSION=x.y  V=1 (show full commands)
# Intermediate files go to build/, which is removed once the binary is done.
#
# Version: `git describe --tags`, without a leading "v". On tag v1.2 that is "1.2"; after more
# commits "1.2-3-gabc1234"; with uncommitted changes "-dirty" is appended; with no tags at all
# the short commit hash; outside a git checkout "dev".

# Options take their default unless given on the command line: same-named environment
# variables are ignored (some build environments export ARCH=x86_64, ...).
# (Written for GNU make 3.81, which macOS ships: no `undefine`.)
default = $(if $(filter undefined environment,$(origin $(1))),$(eval $(1) := $(2)))

$(call default,ARCH,native)
$(call default,TUNE,)
$(call default,NET,../nets/net.nnue)
$(call default,LTO,yes)
$(call default,BENCH_DEPTH,13)
$(call default,OBJDIR,build/obj-$(ARCH))

# ---------------------------------------------------------------- OS and compiler
UNAME_S := $(shell uname -s 2>/dev/null)
ifeq ($(OS),Windows_NT)
  TARGET_OS := windows
  EXE_EXT   := .exe
else ifeq ($(UNAME_S),Darwin)
  TARGET_OS := macos
else
  TARGET_OS := linux
endif
$(call default,EXE,sigrit$(EXE_EXT))
DEBUG_EXE := $(basename $(EXE))-debug$(EXE_EXT)
# macOS does not support fully static binaries
$(call default,STATIC,$(if $(filter macos,$(TARGET_OS)),no,yes))

CXX_VERSION := $(shell $(CXX) --version 2>/dev/null)
ifneq ($(findstring clang,$(CXX_VERSION)),)
  COMPILER := clang
  LTO_FLAG := -flto
else
  COMPILER := gcc
  LTO_FLAG := -flto=auto
endif

$(call default,VERSION,$(or $(patsubst v%,%,$(shell git describe --tags --always --dirty 2>/dev/null)),dev))

# ---------------------------------------------------------------- CPU / ARCH selection
HOST_DEFS := $(shell $(CXX) -dM -E -x c++ /dev/null 2>/dev/null)
ifneq ($(findstring __aarch64__,$(HOST_DEFS)),)
  NATIVE_CLASS := armv8
else
  NATIVE_DEFS := $(shell $(CXX) -march=native -dM -E -x c++ /dev/null 2>/dev/null)
  NATIVE_CPU  := $(shell $(CXX) -march=native -Q --help=target 2>/dev/null | awk '$$1 == "-march=" { print $$2; exit }')
  has = $(findstring $(1),$(NATIVE_DEFS))
  ifneq ($(and $(call has,__AVX512BW__),$(call has,__BMI2__)),)
    NATIVE_CLASS := x86-64-v4
  else ifneq ($(and $(call has,__AVX2__),$(call has,__BMI2__)),)
    NATIVE_CLASS := x86-64-v3
  else
    NATIVE_CLASS := unsupported
  endif
endif

comma := ,
ARCH_NAME := $(patsubst x86-64-avx512,x86-64-v4,$(patsubst x86-64-bmi2,x86-64-v3,$(ARCH)))
ifeq ($(ARCH_NAME),native)
  ARCH_CLASS := $(NATIVE_CLASS)
  ARCH_DESC  := native ($(if $(NATIVE_CPU),$(NATIVE_CPU)$(comma) )$(NATIVE_CLASS) class)
  ifeq ($(NATIVE_CLASS),armv8)
    ARCH_FLAGS := $(if $(filter macos,$(TARGET_OS)),-mcpu=apple-m1,-mcpu=native)
  else
    ARCH_FLAGS := -march=native
  endif
else
  ARCH_CLASS := $(ARCH_NAME)
  ARCH_DESC  := $(ARCH_NAME)
  ifeq ($(ARCH_NAME),x86-64-v4)
    ARCH_FLAGS := -march=x86-64-v4
  else ifeq ($(ARCH_NAME),x86-64-v3)
    ARCH_FLAGS := -march=x86-64-v3
  else ifeq ($(ARCH_NAME),armv8)
    # every Apple Silicon Mac is at least an M1; elsewhere plain ARMv8-A (NEON is part of it)
    ARCH_FLAGS := $(if $(filter macos,$(TARGET_OS)),-mcpu=apple-m1,-march=armv8-a)
  endif
endif

NO_ARCH_GOALS := clean help print-arch print-version
ifeq ($(filter $(NO_ARCH_GOALS),$(MAKECMDGOALS)),)
  ifeq ($(ARCH_CLASS),unsupported)
    $(error ❌ This CPU ($(NATIVE_CPU)) lacks BMI2/AVX2. Sigrit needs Intel Haswell / AMD Zen 3 or newer)
  endif
  ifeq ($(filter $(ARCH_CLASS),x86-64-v4 x86-64-v3 armv8),)
    $(error ❌ Unknown ARCH=$(ARCH). Use native, x86-64-v4, x86-64-v3 or armv8 (see 'make help'))
  endif
  ifeq ($(MAKELEVEL),0)
    $(info 🔍 Building Sigrit $(VERSION) for $(ARCH_DESC) on $(TARGET_OS) with $(COMPILER))
    ifneq ($(filter znver1 znver2,$(NATIVE_CPU)),)
      $(info ⚠️  $(NATIVE_CPU) has slow (microcoded) PEXT: expect a much lower speed than on Zen 3+)
    endif
  endif
endif

# ---------------------------------------------------------------- flags
DEFINES  ?= -DNDEBUG
CPPFLAGS += $(DEFINES) -Ifathom -DNET_PATH='"$(NET)"' -DENGINE_VERSION='"$(VERSION)"'
OPTFLAGS ?= -O3 $(ARCH_FLAGS) $(if $(TUNE),-mtune=$(TUNE))
ifeq ($(LTO),yes)
OPTFLAGS += $(LTO_FLAG)
endif
CXXFLAGS += -std=c++17 $(OPTFLAGS) -MMD -MP $(EXTRA)
CFLAGS   += -std=gnu11 $(OPTFLAGS) -MMD -MP -Wno-unused-function $(EXTRA)
LDFLAGS  += $(OPTFLAGS) $(EXTRA)
ifeq ($(STATIC),yes)
LDFLAGS += -static
endif
LDLIBS += -lpthread

SRCS := $(filter-out perft_test.cpp,$(wildcard *.cpp))
OBJS := $(SRCS:%.cpp=$(OBJDIR)/%.o) $(OBJDIR)/tbprobe.o

# remove build/ when the top-level make is done (sub-makes of pgo/debug leave it alone)
CLEANUP = @echo "🧹 Removing build/"; rm -rf build

ifeq ($(V),1)
Q :=
else
Q := @
endif

.PHONY: all build pgo debug perft bench clean help print-arch print-version

all: build

build: $(EXE)

$(EXE): $(OBJS)
	@echo "🔗 Linking $@"
	$(Q)$(CXX) $(LDFLAGS) -o $@ $(OBJS) $(LDLIBS)
	@echo "✅ Built $@ ($$(du -h $@ | cut -f1)) for $(ARCH_DESC) — ready to play! ♟️"
ifeq ($(MAKELEVEL),0)
	$(CLEANUP)
endif

$(OBJDIR)/%.o: %.cpp | $(OBJDIR)
	@echo "🔧 Compiling $<"
	$(Q)$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

# Fathom (Syzygy probing); tbprobe.c #includes tbchess.c
$(OBJDIR)/tbprobe.o: fathom/tbprobe.c | $(OBJDIR)
	@echo "🏛️  Compiling Fathom (Syzygy tablebases)"
	$(Q)$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

# the network is .incbin'd into nnue.o (path relative to src/, where the compiler runs)
$(OBJDIR)/nnue.o: $(NET)

$(OBJDIR):
	$(Q)mkdir -p $@

PGO_DIR := $(abspath build/pgo)

pgo:
ifeq ($(COMPILER),clang)
	$(error ❌ make pgo uses GCC's profile flags; use GCC or a plain 'make')
endif
	@echo "📈 PGO stage 1/3: instrumented build"
	$(Q)rm -rf $(PGO_DIR) build/pgo-gen-$(ARCH) build/pgo-use-$(ARCH)
	$(Q)$(MAKE) --no-print-directory EXE=build/pgo-instr$(EXE_EXT) OBJDIR=build/pgo-gen-$(ARCH) \
		EXTRA="-fprofile-generate -fprofile-dir=$(PGO_DIR)" build
	@echo "🏃 PGO stage 2/3: profiling with bench $(BENCH_DEPTH)"
	$(Q)./build/pgo-instr$(EXE_EXT) bench $(BENCH_DEPTH)
	@echo "🚀 PGO stage 3/3: optimized build"
	$(Q)$(MAKE) --no-print-directory EXE=$(EXE) OBJDIR=build/pgo-use-$(ARCH) \
		EXTRA="-fprofile-use -fprofile-dir=$(PGO_DIR) -fprofile-partial-training -Wno-missing-profile" build
	@echo "🎉 PGO build done: ./$(EXE)"
	$(CLEANUP)

debug:
	$(Q)$(MAKE) --no-print-directory EXE=$(DEBUG_EXE) OBJDIR=build/debug-$(ARCH) STATIC=no LTO=no \
		OPTFLAGS="-O1 -g $(ARCH_FLAGS) -fno-omit-frame-pointer -fsanitize=address,undefined" \
		DEFINES= build
	$(CLEANUP)

perft: perft_test$(EXE_EXT)

PERFT_SRCS := perft_test.cpp bitboard.cpp position.cpp movegen.cpp

perft_test$(EXE_EXT): $(PERFT_SRCS) $(wildcard *.h)
	@echo "🧮 Building perft_test"
	$(Q)$(CXX) -std=c++17 -O3 $(ARCH_FLAGS) -DNDEBUG -o $@ $(PERFT_SRCS)
	@echo "✅ Built $@ — usage: ./perft_test <file.epd> [maxdepth]"

bench: build
	@echo "⏱️  Running bench $(BENCH_DEPTH)"
	$(Q)./$(EXE) bench $(BENCH_DEPTH)

# best ARCH this machine can run (used by CI to decide which builds it can test)
print-arch:
	@echo $(NATIVE_CLASS)

print-version:
	@echo $(VERSION)

clean:
	@echo "🧹 Cleaning up"
	$(Q)rm -rf build $(EXE) $(DEBUG_EXE) perft_test$(EXE_EXT)

help:
	@sed -n '2,27p' Makefile | sed 's/^# \{0,1\}//'

-include $(OBJS:.o=.d)
