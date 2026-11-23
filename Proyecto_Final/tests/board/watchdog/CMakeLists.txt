cmake_minimum_required(VERSION 3.20.0)

# Ensayo P4-14: firmware de produccion completo mas blocker.c, que bloquea un hilo
# a los 10 s. Caso: -DWDT_CASE=acquisition | gps | main | irq
set(WDT_CASE "acquisition" CACHE STRING "Hilo que se bloquea")

get_filename_component(TRACKER_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
set(CONF_FILE "${TRACKER_ROOT}/confs/prj_nucleo_wl55jc.conf")
set(DTC_OVERLAY_FILE "${TRACKER_ROOT}/boards/nucleo_wl55jc.overlay")

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(tracker_watchdog_test)

target_include_directories(app PRIVATE "${TRACKER_ROOT}/include" "${TRACKER_ROOT}/src")
target_compile_definitions(app PRIVATE WDT_CASE_${WDT_CASE}=1)
target_sources(app PRIVATE
    src/blocker.c
    "${TRACKER_ROOT}/src/main.c"
    "${TRACKER_ROOT}/src/app/tracker_status.c"
    "${TRACKER_ROOT}/src/app/board_ui.c"
    "${TRACKER_ROOT}/src/app/output.c"
    "${TRACKER_ROOT}/src/telemetry/human_report.c"
    "${TRACKER_ROOT}/src/telemetry/simu_frame.c"
    "${TRACKER_ROOT}/src/build_checks.c"
    "${TRACKER_ROOT}/src/drivers/light.c"
    "${TRACKER_ROOT}/src/drivers/imu.c"
    "${TRACKER_ROOT}/src/drivers/environment.c"
    "${TRACKER_ROOT}/src/algorithms/imu_calibration.c"
    "${TRACKER_ROOT}/src/algorithms/motion.c"
    "${TRACKER_ROOT}/src/algorithms/estimator.c"
    "${TRACKER_ROOT}/src/algorithms/statistics.c"
    "${TRACKER_ROOT}/src/acquisition/acquisition.c"
    "${TRACKER_ROOT}/src/acquisition/acquisition_imu.c"
    "${TRACKER_ROOT}/src/acquisition/acquisition_light.c"
    "${TRACKER_ROOT}/src/acquisition/acquisition_environment.c"
    "${TRACKER_ROOT}/src/gps/gps.c"
    "${TRACKER_ROOT}/src/gps/nmea.c"
)
target_link_libraries(app PRIVATE m)
