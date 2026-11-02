cmake_minimum_required(VERSION 3.20.0)

get_filename_component(TRACKER_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
option(D4_TIMING_ONLY "Medir tiempos sin consola ni barrido de pilas" OFF)
option(D4_I2C_TEST "Inyeccion I2C y traza, solo en el ensayo" OFF)
if(D4_I2C_TEST)
    set(D4_TIMING_ONLY ON)
endif()
set(CONF_FILE "${TRACKER_ROOT}/confs/prj_nucleo_wl55jc.conf")
set(DTC_OVERLAY_FILE "${TRACKER_ROOT}/boards/nucleo_wl55jc.overlay")
if(NOT D4_TIMING_ONLY)
    list(APPEND CONF_FILE "${CMAKE_CURRENT_SOURCE_DIR}/prj.conf")
    list(APPEND DTC_OVERLAY_FILE "${CMAKE_CURRENT_SOURCE_DIR}/app.overlay")
endif()
if(D4_I2C_TEST)
    list(APPEND DTC_OVERLAY_FILE "${CMAKE_CURRENT_SOURCE_DIR}/i2c_fault.overlay")
endif()

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(tracker_acquisition_test)

target_include_directories(app PRIVATE "${TRACKER_ROOT}/include")
if(D4_I2C_TEST)
    target_sources(app PRIVATE src/i2c_main.c "${TRACKER_ROOT}/src/build_checks.c")
    target_include_directories(app PRIVATE "${TRACKER_ROOT}/src/acquisition" "${TRACKER_ROOT}/src/drivers")
    target_include_directories(app PRIVATE "${ZEPHYR_BASE}/drivers/i2c")
    foreach(function acquisition_imu_update imu_read imu_check_configuration imu_init
                     imu_recover_bus environment_init environment_start environment_read
                     i2c_stm32_transaction)
        target_link_options(zephyr_interface INTERFACE "-Wl,--wrap=${function}")
    endforeach()
elseif(D4_TIMING_ONLY)
    target_sources(app PRIVATE src/timing_main.c "${TRACKER_ROOT}/src/build_checks.c")
else()
    target_sources(app PRIVATE src/main.c)
endif()
target_sources(app PRIVATE
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
