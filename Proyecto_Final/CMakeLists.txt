cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(upm_zephyr_project)

target_include_directories(app PRIVATE include)
target_sources(app PRIVATE
    src/main.c
    src/build_checks.c
    src/drivers/light.c
    src/drivers/imu.c
    src/drivers/environment.c
    src/algorithms/gyro_calibration.c
    src/algorithms/motion.c
)

target_link_libraries(app PRIVATE m)
