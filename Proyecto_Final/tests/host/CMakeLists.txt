cmake_minimum_required(VERSION 3.20)
project(drone_pure_tests C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
enable_testing()

add_executable(motion_tests
    test_motion.c
    ../../src/algorithms/motion.c
)

add_executable(orientation_tests
    test_estimator.c
    ../../src/algorithms/estimator.c
)
foreach(test_target IN ITEMS motion_tests orientation_tests)
    target_include_directories(${test_target} PRIVATE
        ../../include
        ../../src/algorithms
    )

    if(MSVC)
        target_compile_options(${test_target} PRIVATE /W4 /WX)
    else()
        target_compile_options(${test_target} PRIVATE -Wall -Wextra -Werror)
        target_link_libraries(${test_target} PRIVATE m)
    endif()
endforeach()

add_test(NAME motion_tests COMMAND motion_tests)

add_test(NAME orientation_tests COMMAND orientation_tests)
