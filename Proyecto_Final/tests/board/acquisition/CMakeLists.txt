cmake_minimum_required(VERSION 3.20.0)

get_filename_component(TRACKER_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
set(CONF_FILE
    "${TRACKER_ROOT}/confs/prj_nucleo_wl55jc.conf"
    "${CMAKE_CURRENT_SOURCE_DIR}/prj.conf"
)
set(DTC_OVERLAY_FILE
    "${TRACKER_ROOT}/boards/nucleo_wl55jc.overlay"
    "${CMAKE_CURRENT_SOURCE_DIR}/app.overlay"
)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(tracker_acquisition_test)

target_include_directories(app PRIVATE "${TRACKER_ROOT}/include")
target_sources(app PRIVATE
    src/main.c
    "${TRACKER_ROOT}/src/drivers/light.c"
    "${TRACKER_ROOT}/src/drivers/imu.c"
    "${TRACKER_ROOT}/src/drivers/environment.c"
    "${TRACKER_ROOT}/src/algorithms/gyro_calibration.c"
    "${TRACKER_ROOT}/src/algorithms/motion.c"
    "${TRACKER_ROOT}/src/algorithms/estimator.c"
    "${TRACKER_ROOT}/src/algorithms/statistics.c"
    "${TRACKER_ROOT}/src/acquisition/acquisition.c"
    "${TRACKER_ROOT}/src/acquisition/acquisition_imu.c"
    "${TRACKER_ROOT}/src/acquisition/acquisition_light.c"
    "${TRACKER_ROOT}/src/acquisition/acquisition_environment.c"
)
target_compile_options(app PRIVATE -Wall -Wextra -Werror)
target_link_libraries(app PRIVATE m)
