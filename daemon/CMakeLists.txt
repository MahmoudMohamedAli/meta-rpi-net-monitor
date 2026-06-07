cmake_minimum_required(VERSION 3.16)
project(rpi-net-monitor-daemon VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── Compiler flags ─────────────────────────────────────────────────────────
add_compile_options(
    -Wall -Wextra -Wpedantic
    -Wno-unused-parameter
    $<$<CONFIG:Release>:-O2>
    $<$<CONFIG:Debug>:-g>
    $<$<CONFIG:Debug>:-O0>
    $<$<CONFIG:Debug>:-fsanitize=address,undefined>
)
add_link_options(
    $<$<CONFIG:Debug>:-fsanitize=address>
    $<$<CONFIG:Debug>:-fsanitize=undefined>
)

# ── Library: netmon-core (all business logic, no main) ────────────────────
add_library(netmon-core STATIC
    src/link_fsm.cpp
    src/proc_net_reader.cpp
    src/udp_publisher.cpp
)
target_include_directories(netmon-core PUBLIC include)
target_link_libraries(netmon-core PUBLIC pthread)

# ── Executable: netmon ────────────────────────────────────────────────────
add_executable(netmon src/main.cpp)
target_link_libraries(netmon PRIVATE netmon-core)

# ── Installation ──────────────────────────────────────────────────────────
include(GNUInstallDirs)
install(TARGETS netmon RUNTIME DESTINATION ${CMAKE_INSTALL_SBINDIR})

# ── Unit tests (optional, requires GTest) ─────────────────────────────────
option(BUILD_TESTS "Build unit tests" OFF)
if(BUILD_TESTS)
    find_package(GTest REQUIRED)
    enable_testing()

    add_executable(test_link_fsm tests/test_link_fsm.cpp)
    target_link_libraries(test_link_fsm PRIVATE netmon-core GTest::gtest_main)
    add_test(NAME test_link_fsm COMMAND test_link_fsm)

    add_executable(test_ring_buffer tests/test_ring_buffer.cpp)
    target_link_libraries(test_ring_buffer PRIVATE netmon-core GTest::gtest_main)
    add_test(NAME test_ring_buffer COMMAND test_ring_buffer)
endif()
