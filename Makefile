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

VERSION := 0.10.0
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
DEF_PROD     := -DSTRATUM_CXXFLAGS='"$(PROD_CXXFLAGS) $(PROD_INC)"' -DSTRATUM_BUILD_CONFIG='"release (Makefile)"'
DEF_TEST     := -DSTRATUM_CXXFLAGS='"$(TEST_CXXFLAGS) $(PROD_INC)"'
DEF_RESEARCH := -DSTRATUM_CXXFLAGS='"$(RESEARCH_CXXFLAGS) $(RESEARCH_INC)"'

ALGO_HEADERS := include/stratum/Config.hpp include/stratum/Metrics.hpp \
                include/stratum/StratumSort.hpp include/stratum/StratumSort.tpp \
                include/stratum/KeyTraits.hpp include/stratum/Workspace.hpp \
                include/stratum/Sort.hpp \
                include/stratum/detail/Engine.hpp include/stratum/detail/FastDivision.hpp

.PHONY: all test contract sanitizers tsan fuzz timings bench examples \
        research research-perf studies package package-verify clean help

# ============================================================
# PRODUCT
# ============================================================
# `make all` deliberately excludes the sanitizer suites: they need a
# linkable ASan, which Homebrew GCC does not provide on macOS. A build
# target that fails because an optional tool is missing is a broken build
# target. Run them explicitly with `make sanitizers` / `make fuzz`.
all: build/tests build/contract build/contract_research build/fast_division \
     build/stability build/float_keys build/concurrency \
     build/timings build/stratum_bench build/example_basic

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

# The reciprocal division must be EXACTLY the hardware one: every guarantee
# of the algorithm is a statement about floor(offset / width).
build/fast_division: tests/fast_division.cpp include/stratum/detail/FastDivision.hpp
	@mkdir -p build
	$(CXX) $(TEST_CXXFLAGS) $(PROD_INC) $(DEF_TEST) $< -o $@

build/fast_division_san: tests/fast_division.cpp include/stratum/detail/FastDivision.hpp
	@mkdir -p build
	$(CXX) $(STD) -O1 -g -fsanitize=address,undefined $(WARN_FLAGS) $(PROD_INC) $(DEF_TEST) $< -o $@

# Stability with identifiable payloads, key/value records, enums, chars.
build/stability: tests/stability.cpp $(ALGO_HEADERS) datasets/DatasetGenerator.hpp
	@mkdir -p build
	$(CXX) $(TEST_CXXFLAGS) $(PROD_INC) $(DEF_TEST) $< -o $@

# The float/double key map: exhaustive over all 2^32 floats.
build/float_keys: tests/float_keys.cpp $(ALGO_HEADERS)
	@mkdir -p build
	$(CXX) $(TEST_CXXFLAGS) $(PROD_INC) $(DEF_TEST) $< -o $@

# One sorter, many threads, one workspace each.
build/concurrency: tests/concurrency.cpp $(ALGO_HEADERS)
	@mkdir -p build
	$(CXX) $(TEST_CXXFLAGS) $(PROD_INC) $(DEF_TEST) $< -o $@ -pthread

# The same under ThreadSanitizer: no output was wrong is not the same claim
# as no access raced.
build/concurrency_tsan: tests/concurrency.cpp $(ALGO_HEADERS)
	@mkdir -p build
	$(CXX) $(STD) -O1 -g -fsanitize=thread $(WARN_FLAGS) $(PROD_INC) $(DEF_TEST) $< -o $@ -pthread

tsan: build/concurrency_tsan
	./build/concurrency_tsan

build/stability_san: tests/stability.cpp $(ALGO_HEADERS) datasets/DatasetGenerator.hpp
	@mkdir -p build
	$(CXX) $(STD) -O1 -g -fsanitize=address,undefined $(WARN_FLAGS) $(PROD_INC) $(DEF_TEST) $< -o $@

# The whole contract suite, sanitized: it reaches every path of the engine,
# including the ones only 0.11.0 has (presorted, counting, automatic).
build/contract_san: tests/api_contract.cpp $(ALGO_HEADERS)
	@mkdir -p build
	$(CXX) $(STD) -O1 -g -fsanitize=address,undefined $(WARN_FLAGS) $(PROD_INC) $(DEF_TEST) $< -o $@

build/float_keys_san: tests/float_keys.cpp $(ALGO_HEADERS)
	@mkdir -p build
	$(CXX) $(STD) -O1 -g -fsanitize=address,undefined $(WARN_FLAGS) $(PROD_INC) $(DEF_TEST) $< -o $@

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

# The cross-platform benchmark (CI runs it on Linux, macOS and Windows).
BENCH_HEADERS := benchmarks/SystemInfo.hpp benchmarks/AllocationTracker.hpp \
                 benchmarks/BenchDatasets.hpp datasets/DatasetGenerator.hpp
build/stratum_bench: benchmarks/stratum_bench.cpp $(ALGO_HEADERS) $(BENCH_HEADERS)
	@mkdir -p build
	$(CXX) $(PROD_CXXFLAGS) $(PROD_INC) $(DEF_PROD) $< -o $@

bench: build/stratum_bench
	./build/stratum_bench --suite $(or $(SUITE),quick)

build/example_basic: examples/basic.cpp $(ALGO_HEADERS)
	@mkdir -p build
	$(CXX) $(PROD_CXXFLAGS) -Iinclude $(DEF_PROD) $< -o $@

test: build/tests build/contract build/contract_research build/fast_division \
      build/stability build/float_keys build/concurrency
	./build/tests
	@echo
	./build/fast_division
	@echo
	./build/float_keys
	@echo
	./build/stability
	@echo
	./build/concurrency
	@echo
	./build/contract
	@echo
	./build/contract_research

contract: build/contract build/contract_research
	./build/contract
	./build/contract_research

# float_keys under the sanitizers walks every 101st float pattern: the
# exhaustive walk is `make test`'s job, the sanitizers are here for the
# sorting code around it.
sanitizers: build/sanitizers build/fast_division_san build/stability_san build/float_keys_san \
            build/contract_san
	./build/sanitizers
	./build/fast_division_san
	./build/stability_san
	./build/float_keys_san 101
	./build/contract_san

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

# Release-configuration research programs. Everything above is built with
# the metrics on; these are not, because what they measure - wall clock and
# allocator traffic - is exactly what the instrumentation distorts. They may
# include the frozen 0.10.0 implementation (research/baselines) to compare
# against it in one process, alternating, which is the only comparison this
# project accepts.
PERF_SOURCES  := $(wildcard research/perf/*.cpp)
PERF_BINARIES := $(patsubst research/perf/%.cpp,build/perf_%,$(PERF_SOURCES))

build/perf_%: research/perf/%.cpp $(ALGO_HEADERS) benchmarks/AllocationTracker.hpp
	@mkdir -p build
	$(CXX) $(PROD_CXXFLAGS) $(PROD_INC) -Iresearch/baselines $(DEF_PROD) $< -o $@

research-perf: $(PERF_BINARIES)

research studies: $(RESEARCH_BINARIES) $(PERF_BINARIES)
ifeq ($(strip $(RESEARCH_SOURCES)),)
	@echo "No research/ directory here - this is the distributed package."
	@echo "The design record and the proof live in the project repository."
else
	@echo "$(words $(RESEARCH_BINARIES)) research programs built (not shipped)."
endif

# ============================================================
# PACKAGE - the downloadable artefact
# ============================================================
# Ships the library and what a user needs to build, test and try it.
# Deliberately EXCLUDES research/: proofs, audits, adversaries, design
# history and one-off studies are readable in the repository, not part of
# the download.
#
# The list is EXPLICIT, never an exclude pattern. A new file anywhere in
# the tree - research/, build/, a scratch note - cannot appear in the
# archive unless someone adds its path here on purpose. `make
# package-verify` proves that by planting decoys and failing if any of
# them survives.
#
# The archive's README is packaging/README.md, NOT the repository README:
# the repository one links into research/, which is not shipped, and a
# package whose front page has dead links is a broken package.
PACKAGE_FILES := \
	include/stratum/StratumSort.hpp \
	include/stratum/StratumSort.tpp \
	include/stratum/Config.hpp \
	include/stratum/Metrics.hpp \
	include/stratum/KeyTraits.hpp \
	include/stratum/Workspace.hpp \
	include/stratum/Sort.hpp \
	include/stratum/detail/Engine.hpp \
	include/stratum/detail/FastDivision.hpp \
	tests/main.cpp \
	tests/api_contract.cpp \
	tests/edge_sanitizers.cpp \
	tests/differential_fuzz.cpp \
	tests/fast_division.cpp \
	tests/float_keys.cpp \
	tests/stability.cpp \
	tests/concurrency.cpp \
	tests/odr_guard_lib.cpp \
	tests/odr_guard_main.cpp \
	datasets/DatasetGenerator.hpp \
	benchmarks/timings.cpp \
	benchmarks/stratum_bench.cpp \
	benchmarks/SystemInfo.hpp \
	benchmarks/AllocationTracker.hpp \
	benchmarks/BenchDatasets.hpp \
	examples/basic.cpp \
	docs/usage.md \
	CHANGELOG.md \
	CMakeLists.txt \
	Makefile \
	LICENSE

PKGDIR := dist/$(PKGNAME)

# ---- Reproducibility -------------------------------------------------------
# Two runs over the same tree must produce the same bytes, so that a
# published SHA-256 identifies the contents rather than the moment of
# packaging. A ZIP is not reproducible by default: each entry carries the
# file's modification time, recorded in LOCAL time, plus optional
# platform-specific extra fields. Four things have to be pinned.
#
#   1. Timestamps.  `cp` stamps each staged copy with "now". Every entry is
#      re-stamped to PKG_TIMESTAMP afterwards.
#   2. Timezone.    The stored time is local, so the same instant packaged
#      in CEST and in UTC yields different bytes. TZ is forced to UTC at
#      both the touch and the zip step.
#   3. Extra fields. macOS `cp` carries extended attributes across
#      (com.apple.provenance and friends), and zip would store them. They
#      are stripped from the staging directory, and -X drops uid/gid and
#      any remaining attribute blocks.
#   4. Entry order. `zip -r` walks the directory in readdir order, which is
#      not guaranteed stable. The entry list is sorted explicitly under the
#      C locale and fed to zip with -@.
#
# Override PKG_TIMESTAMP to re-stamp a release. The default is a fixed date,
# not the build date, so the hash depends only on the contents.
PKG_TIMESTAMP ?= 202601010000.00

package: $(PACKAGE_FILES) packaging/README.md
	@rm -rf $(PKGDIR) dist/$(PKGNAME).zip
	@mkdir -p $(PKGDIR)
	@for f in $(PACKAGE_FILES); do \
	    mkdir -p $(PKGDIR)/$$(dirname $$f); \
	    cp $$f $(PKGDIR)/$$f; \
	done
	@cp packaging/README.md $(PKGDIR)/README.md
	@xattr -cr $(PKGDIR) 2>/dev/null || true
	@TZ=UTC find $(PKGDIR) -exec touch -t $(PKG_TIMESTAMP) {} +
	@cd dist && find $(PKGNAME) | LC_ALL=C sort | TZ=UTC zip -qX@ $(PKGNAME).zip
	@echo "dist/$(PKGNAME).zip"
	@unzip -l dist/$(PKGNAME).zip

# Proves the package cannot leak. Plants decoys in every directory that
# must never ship, rebuilds the archive, and fails if any survives. Also
# checks the shipped README has no link to a path the archive lacks.
package-verify: package
	@echo "--- planting decoys ---"
	@mkdir -p research/experiments build
	@echo "leak" > research/DECOY-doc.md
	@echo "leak" > research/experiments/DECOY-study.cpp
	@echo "leak" > research/history/DECOY-note.md
	@echo "leak" > build/DECOY-artifact.o
	@echo "leak" > DECOY-scratch.tmp
	@$(MAKE) --no-print-directory package >/dev/null
	@rm -f research/DECOY-doc.md research/experiments/DECOY-study.cpp \
	       research/history/DECOY-note.md build/DECOY-artifact.o DECOY-scratch.tmp
	@if unzip -l dist/$(PKGNAME).zip | grep -qi decoy; then \
	    echo "FAIL: a decoy reached the archive"; exit 1; \
	else echo "  no decoy reached the archive"; fi
	@if unzip -l dist/$(PKGNAME).zip | grep -qiE "research/|history/|/build/|\.tmp|\.o$$"; then \
	    echo "FAIL: archive contains an excluded path"; exit 1; \
	else echo "  no research/, build/ or scratch path in the archive"; fi
	@echo "--- checking the shipped README is self-contained ---"
	@cd $(PKGDIR) && miss=0; \
	  for l in $$(grep -o "](\([^)]*\))" README.md | tr -d '])(' | grep -v "^http" | grep -v "^#"); do \
	    [ -e "$$l" ] || { echo "  DEAD LINK: $$l"; miss=1; }; \
	  done; \
	  [ $$miss -eq 0 ] && echo "  every link in the shipped README resolves inside the archive" || exit 1
	@echo "package-verify: OK"

clean:
	rm -rf build dist

help:
	@echo "Product:  make all | test | sanitizers | fuzz | timings | bench [SUITE=quick|ci|lambda|full] | examples"
	@echo "Package:  make package | package-verify   -> dist/$(PKGNAME).zip"
	@echo "Research: make research       (built, never shipped)"
