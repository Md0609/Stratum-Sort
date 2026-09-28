cmake_minimum_required(VERSION 3.14)
project(StratumSort VERSION 0.10.0 LANGUAGES CXX)

# ---- The library -----------------------------------------------------------
# Header-only. Consumers need nothing but the include path.
add_library(stratumsort INTERFACE)
add_library(StratumSort::stratumsort ALIAS stratumsort)
target_include_directories(stratumsort INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_compile_features(stratumsort INTERFACE cxx_std_17)

# Only build tests and examples when this is the top-level project, so that
# add_subdirectory() consumers get just the library.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(STRATUMSORT_IS_TOP_LEVEL ON)
else()
    set(STRATUMSORT_IS_TOP_LEVEL OFF)
endif()
option(STRATUMSORT_BUILD_TESTS    "Build the test suites" ${STRATUMSORT_IS_TOP_LEVEL})
option(STRATUMSORT_BUILD_EXAMPLES "Build the examples"    ${STRATUMSORT_IS_TOP_LEVEL})
option(STRATUMSORT_BUILD_BENCHMARKS "Build the cross-platform benchmark" OFF)

if(STRATUMSORT_BUILD_BENCHMARKS)
    # benchmarks/stratum_bench.cpp: the benchmark CI runs on every platform.
    # It reports the flags it was built with; for a multi-config generator
    # the Release flags are the ones it is meant to be built with.
    add_executable(stratum_bench benchmarks/stratum_bench.cpp)
    target_link_libraries(stratum_bench PRIVATE stratumsort)
    target_include_directories(stratum_bench PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks ${CMAKE_CURRENT_SOURCE_DIR}/datasets)
    set(_stratum_bench_flags "${CMAKE_CXX_FLAGS} ${CMAKE_CXX_FLAGS_RELEASE}")
    string(STRIP "${_stratum_bench_flags}" _stratum_bench_flags)
    target_compile_definitions(stratum_bench PRIVATE
        "STRATUM_CXXFLAGS=\"${_stratum_bench_flags} (CMake, ${CMAKE_CXX_COMPILER_ID})\""
        "STRATUM_BUILD_CONFIG=\"$<CONFIG>\"")
endif()

if(STRATUMSORT_BUILD_EXAMPLES)
    add_executable(stratum_example examples/basic.cpp)
    target_link_libraries(stratum_example PRIVATE stratumsort)
endif()

if(STRATUMSORT_BUILD_TESTS)
    enable_testing()

    # MSVC's Debug configuration defaults to _ITERATOR_DEBUG_LEVEL=2, which
    # adds iterator-invalidation bookkeeping to every std::vector operation.
    # This library indexes vectors in its innermost loops, so the effect is
    # not the usual 2-5x: the suite went from 30 seconds in Release to over
    # three hours in Debug on a CI runner, without ever failing.
    #
    # Level 1 is kept, not 0. It retains the bounds check on operator[] and
    # on iterators - the check that actually matters for this code, and the
    # one that would catch a bad bucket index - and drops only the
    # invalidation tracking. Nothing about the library's own assertions
    # changes: the test sources #undef NDEBUG, so every internal invariant
    # is still verified in every configuration, on every platform.
    if(MSVC)
        add_compile_definitions($<$<CONFIG:Debug>:_ITERATOR_DEBUG_LEVEL=1>)
    endif()

    # The tests need the internal assertions on. That is NOT arranged here:
    # CMake appends -DNDEBUG from CMAKE_CXX_FLAGS_<CONFIG> after any target
    # flag, so no target option can win. Each test source instead does
    # `#undef NDEBUG` before its first include, which works under every
    # build system. Verified: a Release build here still runs the checks.
    function(stratum_add_test name source)
        add_executable(${name} ${source})
        target_link_libraries(${name} PRIVATE stratumsort)
        target_include_directories(${name} PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/datasets)
        add_test(NAME ${name} COMMAND ${name})
    endfunction()

    # ---- ODR guard ---------------------------------------------------
    # STRATUM_ENABLE_METRICS changes the layout of StratumSort, so the class
    # is tagged with an inline namespace whose name depends on the macro.
    # Two checks, both expressed as builds rather than as programs to run,
    # so they work identically with GCC, Clang and MSVC and depend on no
    # shell, no nm and no particular linker message:
    #
    #   odr_guard_agrees    both halves built the same way  -> MUST build
    #   odr_guard_mismatch  the halves disagree             -> MUST NOT build
    #
    # The first is the control. Without it the second could pass for the
    # wrong reason - any build error at all would look like success.
    add_executable(stratum_odr_agrees EXCLUDE_FROM_ALL
        tests/odr_guard_lib.cpp tests/odr_guard_main.cpp)
    target_link_libraries(stratum_odr_agrees PRIVATE stratumsort)

    add_library(stratum_odr_half_with_metrics OBJECT EXCLUDE_FROM_ALL
        tests/odr_guard_lib.cpp)
    target_link_libraries(stratum_odr_half_with_metrics PRIVATE stratumsort)
    target_compile_definitions(stratum_odr_half_with_metrics PRIVATE STRATUM_ENABLE_METRICS)

    add_executable(stratum_odr_mismatch EXCLUDE_FROM_ALL
        tests/odr_guard_main.cpp $<TARGET_OBJECTS:stratum_odr_half_with_metrics>)
    target_link_libraries(stratum_odr_mismatch PRIVATE stratumsort)

    add_test(NAME odr_guard_agrees COMMAND ${CMAKE_COMMAND}
             --build ${CMAKE_BINARY_DIR} --target stratum_odr_agrees --config $<CONFIG>)
    add_test(NAME odr_guard_mismatch COMMAND ${CMAKE_COMMAND}
             --build ${CMAKE_BINARY_DIR} --target stratum_odr_mismatch --config $<CONFIG>)
    set_tests_properties(odr_guard_mismatch PROPERTIES WILL_FAIL TRUE)

    stratum_add_test(stratum_tests        tests/main.cpp)
    stratum_add_test(stratum_contract     tests/api_contract.cpp)
    stratum_add_test(stratum_fast_division tests/fast_division.cpp)
    stratum_add_test(stratum_stability    tests/stability.cpp)

    # Exhaustive over all 2^32 floats in optimised configurations (~11 s);
    # every 7th pattern in Debug, where the same walk takes minutes.
    add_executable(stratum_float_keys tests/float_keys.cpp)
    target_link_libraries(stratum_float_keys PRIVATE stratumsort)
    add_test(NAME stratum_float_keys
             COMMAND stratum_float_keys $<IF:$<CONFIG:Debug>,7,1>)

    find_package(Threads REQUIRED)
    stratum_add_test(stratum_concurrency  tests/concurrency.cpp)
    target_link_libraries(stratum_concurrency PRIVATE Threads::Threads)

    # Same source, instrumented: pins the documented differences between the
    # release and research configurations.
    stratum_add_test(stratum_contract_research tests/api_contract.cpp)
    target_compile_definitions(stratum_contract_research PRIVATE STRATUM_ENABLE_METRICS)
endif()

include(GNUInstallDirs)
install(DIRECTORY include/stratum DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
