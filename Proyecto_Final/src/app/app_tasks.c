#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "app_tasks.h"
#include "../drivers/gps.h"
#include "../drivers/led.h"
#include "../drivers/light.h"
#include "../drivers/environment.h"
#include "../drivers/imu.h"

#define LIGHT_RAW_DARK   4062
#define LIGHT_RAW_BRIGHT 128


void app_tasks_run(App_State state, bool entering)
{
    GPS_Struct gps_data = {0};
    LightReading light_data = {0};
    EnvironmentReading environment_data = {0};
    ImuReading imu_data = {0};

    int16_t raw = 0;
    int ret;
    int err;

    // Limpiamos la cola de mensajes de la aplicacion
    printk("\n\n\n\n\n\n");

    // Acciones periódicas del estado
    switch (state) {

        case STATE_TEST:

            printk("MODE: TEST\n");

            err = environment_start();

            if (err != 0 ) {
                    printk("\n\n\n ERROR reading environment data \n\n\n");
            }

            if (gps_has_fix()) {
                err = gps_get_data(&gps_data);
                
                if (err != 0 ) {
                    printk("\n\n\n ERROR reading GPS data \n\n\n");
                }

            }

            printf("GPS: fix=%d sats=%02d lat=%+09.6f lon=%+010.6f alt=%+08.1f m\n",
                gps_data.fix_quality,
                gps_data.satellites,
                gps_data.latitude,
                gps_data.longitude,
                gps_data.altitude);

            ret = light_read_raw(&raw);

            if (ret != 0 ) {
                printk("\n\n\n ERROR reading light raw sensor data \n\n\n");
            }

            ret = light_raw_to_percent(raw,
                            LIGHT_RAW_DARK,
                            LIGHT_RAW_BRIGHT,
                            &light_data);

            if (ret != 0 ) {
                printk("\n\n\n ERROR reading light sensor percentage data \n\n\n");
            }

            printf("LIGHT: %05.1f %%\n", light_data.light_pct);

            environment_read(&environment_data);

            uint32_t temp = (uint32_t)(environment_data.temperature_c * 100.0f + 0.5f);
            uint32_t hum = (uint32_t)(environment_data.humidity_pct * 10.0f + 0.5f);

            printk("ENVIRONMENT: temp=%02u.%02u C humidity=%03u.%01u %%\n",
                temp / 100, temp % 100,
                hum / 10, hum % 10
            );

            /*ret = imu_read(&imu_data);
            if (ret != 0) {
                 printk("\n\n\n ERROR reading IMU data \n\n\n");
            }

            int32_t ax = (int32_t)(imu_data.accel_mps2.x * 1000.0f);
            int32_t ay = (int32_t)(imu_data.accel_mps2.y * 1000.0f);
            int32_t az = (int32_t)(imu_data.accel_mps2.z * 1000.0f);

            int32_t gx = (int32_t)(imu_data.gyro_dps.x * 1000.0f);
            int32_t gy = (int32_t)(imu_data.gyro_dps.y * 1000.0f);
            int32_t gz = (int32_t)(imu_data.gyro_dps.z * 1000.0f);

            printk("IMU ACCEL: X=%d Y=%d Z=%d (m/s2 x1000)\n",
                ax, ay, az);

            printk("IMU GYRO: X=%d Y=%d Z=%d (dps x1000)\n",
                gx, gy, gz);

            printk("IMU RANGE LIMITED: %s\n",
                imu_data.range_limited ? "YES" : "NO");*/

            break;

        case STATE_NORMAL:

            printk("MODE: NORMAL\n");

            if (gps_get_data(&gps_data) == 0 && gps_data.valid) {
                printk("NORMAL: Lat: %.6f | Lon: %.6f\n",
                    gps_data.latitude,
                    gps_data.longitude);
            }
            break;

        case STATE_SIMU:
            printk("MODE: SIMU\n");
            // Funcionalidad de simulacion
            break;
        }
}

int64_t app_tasks_get_interval(App_State state)
{
    switch (state) {

    case STATE_TEST:
        return 2000;

    case STATE_NORMAL:
        return 1000;

    case STATE_SIMU:
        return 1000;

    default:
        return 1000;
    }
}