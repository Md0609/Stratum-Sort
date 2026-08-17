cmake_minimum_required(VERSION 3.14)
project(StratumSort VERSION 0.9.0 LANGUAGES CXX)

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

if(STRATUMSORT_BUILD_EXAMPLES)
    add_executable(stratum_example examples/basic.cpp)
    target_link_libraries(stratum_example PRIVATE stratumsort)
endif()

if(STRATUMSORT_BUILD_TESTS)
    enable_testing()

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

    stratum_add_test(stratum_tests        tests/main.cpp)
    stratum_add_test(stratum_contract     tests/api_contract.cpp)

    # Same source, instrumented: pins the documented differences between the
    # release and research configurations.
    stratum_add_test(stratum_contract_research tests/api_contract.cpp)
    target_compile_definitions(stratum_contract_research PRIVATE STRATUM_ENABLE_METRICS)
endif()

include(GNUInstallDirs)
install(DIRECTORY include/stratum DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
