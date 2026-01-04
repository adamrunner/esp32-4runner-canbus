cmake_minimum_required(VERSION 3.16)
project(can_signal_tests C)

# Use C11 standard
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Enable warnings
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Unity test framework
add_library(unity STATIC
    unity/unity.c
)
target_include_directories(unity PUBLIC unity)

# CAN signal library under test
add_library(can_signal STATIC
    ../components/can_signal/src/can_signal.c
)
target_include_directories(can_signal PUBLIC
    ../components/can_signal/include
)

# Test executable
add_executable(test_can_signal
    test_can_signal.c
)
target_link_libraries(test_can_signal
    can_signal
    unity
)

# Enable CTest
enable_testing()
add_test(NAME can_signal_tests COMMAND test_can_signal)
