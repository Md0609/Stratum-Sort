CXX := g++
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -Iinclude

.PHONY: all test clean

all: build/drs_tests

build/drs_tests: tests/main.cpp include/Config.hpp include/Bin.hpp include/DRSMetrics.hpp include/DynamicRangeSort.hpp include/DynamicRangeSort.tpp tests/DatasetGenerator.hpp
	mkdir -p build
	$(CXX) $(CXXFLAGS) tests/main.cpp -o build/drs_tests

test: build/drs_tests
	./build/drs_tests

clean:
	rm -rf build
