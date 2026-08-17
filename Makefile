# ============================================================
# Stratum Sort - build system
# ============================================================
# Two halves, deliberately separate:
#
#   PRODUCT   include/, tests/, examples/, benchmarks/timings.cpp
#             This is what `make package` ships. Header-only library plus
#             the tests and one benchmark a user can run themselves.
#
#   RESEARCH  research/
#             Derivations, audits, adversaries, one-off studies. Built by
#             `make research`, never shipped. A study whose source no
#             longer compiles cannot be re-run, so `make research` exists
#             to keep them honest - but it is not part of `make all`.
#
# THREE build configurations, differing in two orthogonal switches:
#
#   STRATUM_ENABLE_METRICS - compiles in the instrumentation. Undefined
#                        means SortMetrics, the metrics() accessor and the
#                        introspection API do not exist in the object code
#                        at all, not merely that they are disabled.
#   NDEBUG             - compiles OUT the internal assertions.
#
#   PROD_CXXFLAGS     - release: no metrics, no assertions. This is what a
#                        caller gets, and the only configuration whose
#                        timings are meaningful.
#   TEST_CXXFLAGS     - release code with ASSERTIONS ON. The invariants
#                        (index in range, leaves tiling [0,n), partition
#                        preconditions) are only checked here. One of them
#                        sits in the innermost loop of the distribution
#                        pass, so this configuration is measurably slower
#                        - about 5% on distribution-dominated inputs. It
#                        is for correctness, never for measurement.
#   RESEARCH_CXXFLAGS - metrics on, assertions on. A laboratory build,
#                        several times slower than release. Never use it
#                        to measure performance.
#
# -O3 was measured 5-8% faster than -O2 on this project's hot path, in a
# controlled alternating comparison. -march=native and -flto showed no
# further consistent gain and cost portability, so neither is default.
# See research/BENCHMARKS.md to re-check on your own hardware.

VERSION := 1.0.0
PKGNAME := stratumsort-v$(VERSION)

CXX := g++
WARN_FLAGS := -Wall -Wextra
OPT_FLAGS  := -O3
STD        := -std=c++17

PROD_CXXFLAGS     := $(STD) $(OPT_FLAGS) $(WARN_FLAGS) -DNDEBUG
TEST_CXXFLAGS     := $(STD) $(OPT_FLAGS) $(WARN_FLAGS)
RESEARCH_CXXFLAGS := $(STD) $(OPT_FLAGS) $(WARN_FLAGS) -DSTRATUM_ENABLE_METRICS

PROD_INC     := -Iinclude -Idatasets -Ibenchmarks
RESEARCH_INC := $(PROD_INC) -Iresearch/tools -Iresearch/analysis -Iresearch/experiments

# Embedded into every binary so SystemInfo can report the exact flags used
# to build it, instead of guessing.
DEF_PROD     := -DSTRATUM_CXXFLAGS='"$(PROD_CXXFLAGS) $(PROD_INC)"'
DEF_TEST     := -DSTRATUM_CXXFLAGS='"$(TEST_CXXFLAGS) $(PROD_INC)"'
DEF_RESEARCH := -DSTRATUM_CXXFLAGS='"$(RESEARCH_CXXFLAGS) $(RESEARCH_INC)"'

ALGO_HEADERS := include/stratum/Config.hpp include/stratum/Metrics.hpp \
                include/stratum/StratumSort.hpp include/stratum/StratumSort.tpp

.PHONY: all test contract sanitizers fuzz timings examples \
        research studies package clean help

# ============================================================
# PRODUCT
# ============================================================
all: build/tests build/contract build/contract_research build/sanitizers \
     build/fuzz build/timings build/example_basic

# ---- Correctness: TEST configuration, assertions ACTIVE --------------------
build/tests: tests/main.cpp $(ALGO_HEADERS) datasets/DatasetGenerator.hpp
	@mkdir -p build
	$(CXX) $(TEST_CXXFLAGS) $(PROD_INC) $(DEF_TEST) $< -o $@

# Built in BOTH configurations: release validates what a caller gets,
# research pins the documented differences between the two.
build/contract: tests/api_contract.cpp $(ALGO_HEADERS)
	@mkdir -p build
	$(CXX) $(TEST_CXXFLAGS) $(PROD_INC) $(DEF_TEST) $< -o $@

build/contract_research: tests/api_contract.cpp $(ALGO_HEADERS)
	@mkdir -p build
	$(CXX) $(RESEARCH_CXXFLAGS) $(PROD_INC) $(DEF_RESEARCH) $< -o $@

# ---- Range-arithmetic limits under ASan/UBSan ------------------------------
# Separate from `make test` because the sanitizers make it far slower; it is
# the suite that exercises spans reaching the whole key universe, which is
# exactly where an overflow would hide.
build/sanitizers: tests/edge_sanitizers.cpp $(ALGO_HEADERS) datasets/DatasetGenerator.hpp
	@mkdir -p build
	$(CXX) $(STD) -O1 -g -fsanitize=address,undefined $(WARN_FLAGS) $(PROD_INC) $(DEF_TEST) $< -o $@

# ---- Differential fuzz against std::sort -----------------------------------
build/fuzz: tests/differential_fuzz.cpp $(ALGO_HEADERS)
	@mkdir -p build
	$(CXX) $(STD) -O1 -g -fsanitize=address,undefined $(WARN_FLAGS) $(PROD_INC) $(DEF_TEST) $< -o $@

# ---- The ONLY binary whose clock may be quoted -----------------------------
build/timings: benchmarks/timings.cpp $(ALGO_HEADERS) benchmarks/SystemInfo.hpp datasets/DatasetGenerator.hpp
	@mkdir -p build
	$(CXX) $(PROD_CXXFLAGS) $(PROD_INC) $(DEF_PROD) $< -o $@

build/example_basic: examples/basic.cpp $(ALGO_HEADERS)
	@mkdir -p build
	$(CXX) $(PROD_CXXFLAGS) -Iinclude $(DEF_PROD) $< -o $@

test: build/tests build/contract build/contract_research
	./build/tests
	@echo
	./build/contract
	@echo
	./build/contract_research

contract: build/contract build/contract_research
	./build/contract
	./build/contract_research

sanitizers: build/sanitizers
	./build/sanitizers

# 20000 random cases by default; pass N=... for a longer soak.
fuzz: build/fuzz
	./build/fuzz $(N)

timings: build/timings
	./build/timings

examples: build/example_basic
	./build/example_basic

# ============================================================
# RESEARCH - never shipped, but kept compiling
# ============================================================
RESEARCH_SOURCES := $(wildcard research/tools/*.cpp) \
                    $(wildcard research/analysis/*.cpp) \
                    $(wildcard research/experiments/*.cpp)
RESEARCH_BINARIES := $(patsubst %.cpp,build/research_%,$(notdir $(RESEARCH_SOURCES)))

build/research_%: research/tools/%.cpp
	@mkdir -p build
	$(CXX) $(RESEARCH_CXXFLAGS) $(RESEARCH_INC) $(DEF_RESEARCH) $< -o $@

build/research_%: research/analysis/%.cpp
	@mkdir -p build
	$(CXX) $(RESEARCH_CXXFLAGS) $(RESEARCH_INC) $(DEF_RESEARCH) $< -o $@

build/research_%: research/experiments/%.cpp
	@mkdir -p build
	$(CXX) $(RESEARCH_CXXFLAGS) $(RESEARCH_INC) $(DEF_RESEARCH) $< -o $@

research studies: $(RESEARCH_BINARIES)
	@echo "$(words $(RESEARCH_BINARIES)) research programs built (not shipped)."

# ============================================================
# PACKAGE - the downloadable artefact
# ============================================================
# Ships the library and what a user needs to build, test and try it.
# Deliberately EXCLUDES research/: proofs, audits, adversaries, design
# history and one-off studies are readable in the repository, not part of
# the download. The file list is explicit rather than an exclude list, so
# a new research document cannot leak into the package by default.
PACKAGE_FILES := \
	include/stratum/StratumSort.hpp \
	include/stratum/StratumSort.tpp \
	include/stratum/Config.hpp \
	include/stratum/Metrics.hpp \
	tests/main.cpp \
	tests/api_contract.cpp \
	tests/edge_sanitizers.cpp \
	tests/differential_fuzz.cpp \
	datasets/DatasetGenerator.hpp \
	benchmarks/timings.cpp \
	benchmarks/SystemInfo.hpp \
	examples/basic.cpp \
	docs/usage.md \
	CMakeLists.txt \
	Makefile \
	LICENSE \
	README.md

package: $(PACKAGE_FILES)
	@rm -rf dist/$(PKGNAME) dist/$(PKGNAME).zip
	@mkdir -p dist/$(PKGNAME)
	@for f in $(PACKAGE_FILES); do \
	    mkdir -p dist/$(PKGNAME)/$$(dirname $$f); \
	    cp $$f dist/$(PKGNAME)/$$f; \
	done
	@cd dist && zip -qr $(PKGNAME).zip $(PKGNAME)
	@echo "dist/$(PKGNAME).zip"
	@unzip -l dist/$(PKGNAME).zip

clean:
	rm -rf build dist

help:
	@echo "Product:  make all | test | sanitizers | fuzz | timings | examples"
	@echo "Package:  make package        -> dist/$(PKGNAME).zip"
	@echo "Research: make research       (built, never shipped)"
