# ============================================================
# Dynamic Range Sort - build system
# ============================================================
# THREE configurations, differing in two orthogonal switches:
#
#   DRS_ENABLE_METRICS - compiles in the instrumentation. Undefined means
#                        DRSMetrics, the metrics() accessor and the
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
#                        to measure performance; see the production/
#                        research overhead comparison in `make overhead`.
#
# -O3 was measured 5-8% faster than -O2 on this project's hot path, in a
# controlled alternating comparison. -march=native and -flto showed no
# further consistent gain and cost portability, so neither is default.
# See docs/BENCHMARKS.md if you want to re-check on your own hardware.
CXX := g++
WARN_FLAGS := -Wall -Wextra
OPT_FLAGS := -O3
PROD_CXXFLAGS := -std=c++17 $(OPT_FLAGS) $(WARN_FLAGS) -DNDEBUG
TEST_CXXFLAGS := -std=c++17 $(OPT_FLAGS) $(WARN_FLAGS)
RESEARCH_CXXFLAGS := -std=c++17 $(OPT_FLAGS) $(WARN_FLAGS) -DDRS_ENABLE_METRICS
INCLUDES := -Iinclude -Ibenchmarks -Ianalysis -Iexperiments -Idatasets

# Embedded into every binary so SystemInfo (and the docs) can report the
# exact flags used to build it, instead of guessing.
BUILD_FLAGS_DEFINE_PROD := -DDRS_CXXFLAGS='"$(PROD_CXXFLAGS) $(INCLUDES)"'
BUILD_FLAGS_DEFINE_TEST := -DDRS_CXXFLAGS='"$(TEST_CXXFLAGS) $(INCLUDES)"'
BUILD_FLAGS_DEFINE_RESEARCH := -DDRS_CXXFLAGS='"$(RESEARCH_CXXFLAGS) $(INCLUDES)"'

ALGO_HEADERS := include/drs/Config.hpp include/drs/DRSMetrics.hpp \
                include/drs/DynamicRangeSort.hpp include/drs/DynamicRangeSort.tpp
VERSION_HEADERS := experiments/legacy/DRSv1.hpp experiments/legacy/DRSv2.hpp \
                   experiments/legacy/DRSv3.hpp experiments/legacy/DRSv6_experimental.hpp
COMMON_HEADERS := $(ALGO_HEADERS) benchmarks/SystemInfo.hpp benchmarks/BenchmarkRunner.hpp \
                   analysis/Statistics.hpp datasets/DatasetGenerator.hpp

# One-off research programs. Each answered a single question during the
# design and is kept so that its number can be re-derived rather than
# taken on trust. They are not part of the library and are not run by
# `make all`, but they ARE built by it: a study whose source no longer
# compiles cannot be re-run, which makes its published result unverifiable.
STUDY_SOURCES := analysis/ComplexityReview.cpp \
                 benchmarks/ImprovementCeiling.cpp \
                 experiments/AdversarialCoreSweep.cpp \
                 experiments/AdversaryOverfitCheck.cpp \
                 experiments/CardinalityAnomaly.cpp \
                 experiments/CardinalityAnomalyProbe.cpp \
                 experiments/EmptyBinDecomposition.cpp \
                 experiments/LambdaTauPlaneSweep.cpp \
                 experiments/LambdaTauPotential.cpp \
                 experiments/SortedCertificatePotential.cpp \
                 experiments/TargetSweep.cpp
STUDY_BINARIES := $(patsubst %.cpp,build/study_%,$(notdir $(STUDY_SOURCES)))

.PHONY: all test contract benchmarks experiments analysis overhead baseline profile studies sanitizers clean

all: build/drs_tests build/drs_contract build/drs_contract_research build/drs_benchmarks build/drs_experiments build/drs_analysis \
     build/drs_overhead_production build/drs_overhead_research build/drs_baseline \
     build/drs_profile build/drs_sanitizers $(STUDY_BINARIES)

# ---- Correctness binaries: TEST configuration, assertions ACTIVE ----------
build/drs_tests: tests/main.cpp $(COMMON_HEADERS)
	mkdir -p build
	$(CXX) $(TEST_CXXFLAGS) $(INCLUDES) $(BUILD_FLAGS_DEFINE_TEST) tests/main.cpp -o build/drs_tests

# ---- Research-configuration binaries (need DRS_ENABLE_METRICS) ------------
build/drs_benchmarks: benchmarks/main.cpp $(COMMON_HEADERS)
	mkdir -p build
	$(CXX) $(RESEARCH_CXXFLAGS) $(INCLUDES) $(BUILD_FLAGS_DEFINE_RESEARCH) benchmarks/main.cpp -o build/drs_benchmarks

build/drs_experiments: experiments/main.cpp $(COMMON_HEADERS) $(VERSION_HEADERS) \
                       experiments/TargetStrategies.hpp experiments/BinSizeHistogram.hpp \
                       experiments/DisorderMetrics.hpp experiments/LocalityExperiment.hpp \
                       experiments/VersionComparison.hpp experiments/SubdivisionQualityAnalysis.hpp
	mkdir -p build
	$(CXX) $(RESEARCH_CXXFLAGS) $(INCLUDES) $(BUILD_FLAGS_DEFINE_RESEARCH) experiments/main.cpp -o build/drs_experiments

build/drs_analysis: analysis/main.cpp $(COMMON_HEADERS)
	mkdir -p build
	$(CXX) $(RESEARCH_CXXFLAGS) $(INCLUDES) $(BUILD_FLAGS_DEFINE_RESEARCH) analysis/main.cpp -o build/drs_analysis

# ---- Cost of the instrumentation: one source file, compiled twice, --------
# ---- once per configuration. ----------------------------------------------
build/drs_overhead_production: benchmarks/InstrumentationOverhead.cpp $(COMMON_HEADERS)
	mkdir -p build
	$(CXX) $(PROD_CXXFLAGS) $(INCLUDES) $(BUILD_FLAGS_DEFINE_PROD) benchmarks/InstrumentationOverhead.cpp -o build/drs_overhead_production

build/drs_overhead_research: benchmarks/InstrumentationOverhead.cpp $(COMMON_HEADERS)
	mkdir -p build
	$(CXX) $(RESEARCH_CXXFLAGS) $(INCLUDES) $(BUILD_FLAGS_DEFINE_RESEARCH) benchmarks/InstrumentationOverhead.cpp -o build/drs_overhead_research

# ---- Reference baseline over the full dataset battery ---------------------
# Times every dataset against std::sort and prints the deterministic
# counters alongside, so a change can be judged by work done and not only
# by the clock.
build/drs_baseline: benchmarks/ReferenceBaseline.cpp $(COMMON_HEADERS)
	mkdir -p build
	$(CXX) $(RESEARCH_CXXFLAGS) $(INCLUDES) $(BUILD_FLAGS_DEFINE_RESEARCH) benchmarks/ReferenceBaseline.cpp -o build/drs_baseline

# ---- Per-phase time breakdown and heap-allocation count -------------------
build/drs_profile: benchmarks/PhaseProfile.cpp $(COMMON_HEADERS)
	mkdir -p build
	$(CXX) $(RESEARCH_CXXFLAGS) $(INCLUDES) $(BUILD_FLAGS_DEFINE_RESEARCH) benchmarks/PhaseProfile.cpp -o build/drs_profile

# ---- Contract tests for the public API -------------------------------------
# Built in BOTH configurations: production validates what a caller gets,
# research pins the documented differences between the two.
build/drs_contract: tests/api_contract.cpp $(ALGO_HEADERS)
	mkdir -p build
	$(CXX) $(TEST_CXXFLAGS) $(INCLUDES) $(BUILD_FLAGS_DEFINE_TEST) tests/api_contract.cpp -o build/drs_contract

build/drs_contract_research: tests/api_contract.cpp $(ALGO_HEADERS)
	mkdir -p build
	$(CXX) $(RESEARCH_CXXFLAGS) $(INCLUDES) $(BUILD_FLAGS_DEFINE_RESEARCH) tests/api_contract.cpp -o build/drs_contract_research

# ---- Range-arithmetic limits under ASan/UBSan ------------------------------
# Separate from `make test` because the sanitizers make it far slower; it is
# the suite that exercises spans reaching the whole key universe, which is
# exactly where an overflow would hide.
build/drs_sanitizers: tests/edge_sanitizers.cpp $(ALGO_HEADERS) datasets/DatasetGenerator.hpp
	mkdir -p build
	$(CXX) -std=c++17 -O1 -g -fsanitize=address,undefined $(WARN_FLAGS) $(INCLUDES) \
	    $(BUILD_FLAGS_DEFINE_TEST) tests/edge_sanitizers.cpp -o build/drs_sanitizers

# ---- One-off research programs --------------------------------------------
# Built in the research configuration: every one of them reads the metrics.
build/study_%: experiments/%.cpp $(COMMON_HEADERS)
	mkdir -p build
	$(CXX) $(RESEARCH_CXXFLAGS) $(INCLUDES) $(BUILD_FLAGS_DEFINE_RESEARCH) $< -o $@

build/study_%: benchmarks/%.cpp $(COMMON_HEADERS)
	mkdir -p build
	$(CXX) $(RESEARCH_CXXFLAGS) $(INCLUDES) $(BUILD_FLAGS_DEFINE_RESEARCH) $< -o $@

build/study_%: analysis/%.cpp $(COMMON_HEADERS)
	mkdir -p build
	$(CXX) $(RESEARCH_CXXFLAGS) $(INCLUDES) $(BUILD_FLAGS_DEFINE_RESEARCH) $< -o $@

studies: $(STUDY_BINARIES)

sanitizers: build/drs_sanitizers
	./build/drs_sanitizers

test: build/drs_tests build/drs_contract build/drs_contract_research
	./build/drs_tests
	@echo
	./build/drs_contract
	@echo
	./build/drs_contract_research

contract: build/drs_contract build/drs_contract_research
	./build/drs_contract
	./build/drs_contract_research

baseline: build/drs_baseline
	./build/drs_baseline

profile: build/drs_profile
	./build/drs_profile

benchmarks: build/drs_benchmarks
	./build/drs_benchmarks

experiments: build/drs_experiments
	./build/drs_experiments

analysis: build/drs_analysis
	./build/drs_analysis

overhead: build/drs_overhead_production build/drs_overhead_research
	./build/drs_overhead_production
	@echo
	./build/drs_overhead_research

clean:
	rm -rf build
