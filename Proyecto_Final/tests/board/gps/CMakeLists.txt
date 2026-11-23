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
project(tracker_gps_test)

target_include_directories(app PRIVATE "${TRACKER_ROOT}/include")
target_sources(app PRIVATE
    src/main.c
    "${TRACKER_ROOT}/src/gps/gps.c"
    "${TRACKER_ROOT}/src/gps/nmea.c"
)
target_link_libraries(app PRIVATE m)
