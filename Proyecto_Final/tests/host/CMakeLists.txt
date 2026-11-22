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
)
target_include_directories(acquisition_environment_tests PRIVATE
    fakes
    ../../src/acquisition
    ../../src/drivers
)

add_executable(statistics_tests
    test_statistics.c
    ../../src/algorithms/statistics.c
)

add_executable(simu_frame_tests
    test_simu_frame.c
    ../../src/telemetry/simu_frame.c
)
target_include_directories(simu_frame_tests PRIVATE ../../src/telemetry)

add_executable(human_report_tests
    test_human_report.c
    ../../src/telemetry/human_report.c
)

add_executable(tracker_status_tests
    test_tracker_status.c
    ../../src/app/tracker_status.c
)
target_include_directories(tracker_status_tests PRIVATE ../../src/app)

foreach(test_target IN ITEMS motion_tests orientation_tests acquisition_imu_tests acquisition_environment_tests statistics_tests simu_frame_tests human_report_tests tracker_status_tests)
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

add_test(NAME statistics_tests COMMAND statistics_tests)

add_test(NAME simu_frame_tests COMMAND simu_frame_tests)
add_test(NAME human_report_tests COMMAND human_report_tests)
add_test(NAME tracker_status_tests COMMAND tracker_status_tests)
