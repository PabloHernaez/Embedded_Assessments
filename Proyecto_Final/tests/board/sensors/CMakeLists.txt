cmake_minimum_required(VERSION 3.20.0)

get_filename_component(TRACKER_ROOT
    "${CMAKE_CURRENT_SOURCE_DIR}/../../.."
    ABSOLUTE
)

# Primero la configuracion comun; despues los ajustes del ensayo.
set(CONF_FILE
    "${TRACKER_ROOT}/confs/prj_nucleo_wl55jc.conf"
    "${CMAKE_CURRENT_SOURCE_DIR}/prj.conf"
)

set(DTC_OVERLAY_FILE
    "${TRACKER_ROOT}/boards/nucleo_wl55jc.overlay"
    "${CMAKE_CURRENT_SOURCE_DIR}/app.overlay"
)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(tracker_sensor_test)

target_include_directories(app PRIVATE
    "${TRACKER_ROOT}/include"
    "${TRACKER_ROOT}/src/drivers"
    "${TRACKER_ROOT}/src/algorithms"
)

target_sources(app PRIVATE
    src/main.c
    "${TRACKER_ROOT}/src/drivers/imu.c"
    "${TRACKER_ROOT}/src/drivers/environment.c"
    "${TRACKER_ROOT}/src/algorithms/gyro_calibration.c"
    "${TRACKER_ROOT}/src/algorithms/motion.c"
)

target_link_libraries(app PRIVATE m)
