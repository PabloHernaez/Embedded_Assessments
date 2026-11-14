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

add_executable(acquisition_imu_tests
    test_acquisition_imu.c
    ../../src/acquisition/acquisition_imu.c
    ../../src/algorithms/gyro_calibration.c
    ../../src/algorithms/motion.c
    ../../src/algorithms/estimator.c
)
target_include_directories(acquisition_imu_tests PRIVATE
    fakes
    ../../src/acquisition
    ../../src/drivers
)

add_executable(acquisition_environment_tests
    test_acquisition_environment.c
    ../../src/acquisition/acquisition_environment.c
    ../../src/algorithms/statistics.c
)
target_include_directories(acquisition_environment_tests PRIVATE
    fakes
    ../../src/acquisition
    ../../src/drivers
)

add_executable(acquisition_statistics_tests
    test_acquisition_statistics.c
    ../../src/acquisition/acquisition_imu.c
    ../../src/acquisition/acquisition_environment.c
    ../../src/algorithms/gyro_calibration.c
    ../../src/algorithms/motion.c
    ../../src/algorithms/estimator.c
    ../../src/algorithms/statistics.c
)
target_include_directories(acquisition_statistics_tests PRIVATE
    fakes_coordinator
    ../../src/acquisition
    ../../src/drivers
)

add_executable(acquisition_recovery_tests
    test_acquisition_recovery.c
    ../../src/acquisition/acquisition_imu.c
    ../../src/algorithms/gyro_calibration.c
    ../../src/algorithms/motion.c
    ../../src/algorithms/estimator.c
)
target_include_directories(acquisition_recovery_tests PRIVATE
    fakes
    ../../src/acquisition
    ../../src/drivers
)

foreach(test_target IN ITEMS motion_tests orientation_tests acquisition_imu_tests acquisition_environment_tests acquisition_statistics_tests acquisition_recovery_tests)
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

add_test(NAME acquisition_imu_tests COMMAND acquisition_imu_tests)

add_test(NAME acquisition_environment_tests COMMAND acquisition_environment_tests)

add_test(NAME acquisition_statistics_tests COMMAND acquisition_statistics_tests)

add_test(NAME acquisition_recovery_tests COMMAND acquisition_recovery_tests)
