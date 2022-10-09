cmake_minimum_required(VERSION 3.17)

enable_testing()

find_package(Catch2 3 REQUIRED)

add_executable(tests)

target_link_libraries(tests
    PRIVATE
        ray-tracer-lib
        Catch2::Catch2WithMain
)

set_target_properties(tests
    PROPERTIES
        CXX_STANDARD 17
)

target_sources(tests
    PRIVATE
        test_Bounds.cpp
        test_main.cpp
)

include(CTest)
include(Catch)
catch_discover_tests(tests)
