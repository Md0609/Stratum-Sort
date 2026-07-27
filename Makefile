# ============================================================
# DRS v7 build system
# ============================================================
# Two distinct configurations, per ANALYSIS_v7.md section 2:
#
#   PROD_CXXFLAGS     - no DRS_ENABLE_METRICS. DRSMetrics, the metrics()
#                        accessor, and debugPartitionOnly() do not exist
#                        in the resulting object code at all (verified
#                        with `nm`, not just "disabled" - see
#                        ANALYSIS_v7.md).
#   RESEARCH_CXXFLAGS - defines DRS_ENABLE_METRICS. Full instrumentation,
#                        used by benchmarks/, experimentos/ and analisis/,
#                        which all call .metrics()/.debugPartitionOnly().
#
# -O3 replaces -O2 as of v7: a controlled, alternating-trial comparison
# (ANALYSIS_v7.md section 8) measured -O3 5-8% faster than -O2 on this
# project's hot path, consistently. -march=native and -flto were also
# measured and did not show a further consistent improvement beyond -O3
# in that comparison, so neither is part of the default build (both cost
# portability - -march=native binaries do not run on other CPUs - for a
# benefit that did not clear noise here); see ANALYSIS_v7.md for the raw
# numbers if you want to re-check on your own hardware.
CXX := g++
WARN_FLAGS := -Wall -Wextra
OPT_FLAGS := -O3
PROD_CXXFLAGS := -std=c++17 $(OPT_FLAGS) $(WARN_FLAGS)
RESEARCH_CXXFLAGS := -std=c++17 $(OPT_FLAGS) $(WARN_FLAGS) -DDRS_ENABLE_METRICS
INCLUDES := -Ialgoritmo -Ibenchmarks -Ianalisis -Iexperimentos -Idatasets

# Embedded into every binary so SystemInfo (and the docs) can report the
# exact flags used to build it, instead of guessing.
BUILD_FLAGS_DEFINE_PROD := -DDRS_CXXFLAGS='"$(PROD_CXXFLAGS) $(INCLUDES)"'
BUILD_FLAGS_DEFINE_RESEARCH := -DDRS_CXXFLAGS='"$(RESEARCH_CXXFLAGS) $(INCLUDES)"'

ALGO_HEADERS := algoritmo/Config.hpp algoritmo/DRSMetrics.hpp algoritmo/DynamicRangeSort.hpp \
                algoritmo/DynamicRangeSort.tpp
VERSION_HEADERS := algoritmo/versions/DRSv1.hpp algoritmo/versions/DRSv2.hpp \
                   algoritmo/versions/DRSv3.hpp algoritmo/versions/DRSv6_experimental.hpp
COMMON_HEADERS := $(ALGO_HEADERS) benchmarks/SystemInfo.hpp benchmarks/BenchmarkRunner.hpp \
                   analisis/Statistics.hpp datasets/DatasetGenerator.hpp

.PHONY: all test benchmarks experiments analysis overhead baseline clean

all: build/drs_tests build/drs_benchmarks build/drs_experiments build/drs_analysis \
     build/drs_overhead_production build/drs_overhead_research build/drs_baseline_v8

# ---- Production-configuration binary (correctness only, no metrics) -------
build/drs_tests: tests/main.cpp $(COMMON_HEADERS)
	mkdir -p build
	$(CXX) $(PROD_CXXFLAGS) $(INCLUDES) $(BUILD_FLAGS_DEFINE_PROD) tests/main.cpp -o build/drs_tests

# ---- Research-configuration binaries (need DRS_ENABLE_METRICS) ------------
build/drs_benchmarks: benchmarks/main.cpp $(COMMON_HEADERS)
	mkdir -p build
	$(CXX) $(RESEARCH_CXXFLAGS) $(INCLUDES) $(BUILD_FLAGS_DEFINE_RESEARCH) benchmarks/main.cpp -o build/drs_benchmarks

build/drs_experiments: experimentos/main.cpp $(COMMON_HEADERS) $(VERSION_HEADERS) \
                       experimentos/TargetStrategies.hpp experimentos/BinSizeHistogram.hpp \
                       experimentos/DisorderMetrics.hpp experimentos/LocalityExperiment.hpp \
                       experimentos/VersionComparison.hpp experimentos/SubdivisionQualityAnalysis.hpp
	mkdir -p build
	$(CXX) $(RESEARCH_CXXFLAGS) $(INCLUDES) $(BUILD_FLAGS_DEFINE_RESEARCH) experimentos/main.cpp -o build/drs_experiments

build/drs_analysis: analisis/main.cpp $(COMMON_HEADERS)
	mkdir -p build
	$(CXX) $(RESEARCH_CXXFLAGS) $(INCLUDES) $(BUILD_FLAGS_DEFINE_RESEARCH) analisis/main.cpp -o build/drs_analysis

# ---- Production vs research overhead (section 3): same source file, --------
# ---- compiled twice, once per configuration. ------------------------------
build/drs_overhead_production: benchmarks/ProductionVsResearch.cpp $(COMMON_HEADERS)
	mkdir -p build
	$(CXX) $(PROD_CXXFLAGS) $(INCLUDES) $(BUILD_FLAGS_DEFINE_PROD) benchmarks/ProductionVsResearch.cpp -o build/drs_overhead_production

build/drs_overhead_research: benchmarks/ProductionVsResearch.cpp $(COMMON_HEADERS)
	mkdir -p build
	$(CXX) $(RESEARCH_CXXFLAGS) $(INCLUDES) $(BUILD_FLAGS_DEFINE_RESEARCH) benchmarks/ProductionVsResearch.cpp -o build/drs_overhead_research

# ---- Linea base de v8 en la maquina actual (SPEC_v9.md, paso 0) ------------
# Mide v8 sin modificarlo, sobre los ocho datasets historicos mas los dos
# anadidos en el paso 0, y comprueba explicitamente los dos defectos que
# SPEC_v9 predice y que ningun dataset anterior activaba.
build/drs_baseline_v8: benchmarks/BaselineV8.cpp $(COMMON_HEADERS)
	mkdir -p build
	$(CXX) $(RESEARCH_CXXFLAGS) $(INCLUDES) $(BUILD_FLAGS_DEFINE_RESEARCH) benchmarks/BaselineV8.cpp -o build/drs_baseline_v8

test: build/drs_tests
	./build/drs_tests

baseline: build/drs_baseline_v8
	./build/drs_baseline_v8

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
