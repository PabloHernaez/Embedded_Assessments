#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <math.h>

#include "imu.h"
#include "environment.h"
#include "gyro_calibration.h"

int main(void)
{
    /* Estabilizacion de alimentacion antes del primer acceso I2C. */
    k_sleep(K_MSEC(100));

    printk("\nEnsayo MPU6050 + Si7021\n");

    int ret = imu_init();

    printk("imu_init: %d\n", ret);
    if (ret < 0) {
        return 0;
    }

    ret = environment_init();

    printk("environment_init: %d\n", ret);
    if (ret < 0) {
        return 0;
    }

    GyroCalibration calibration;
    gyro_calibration_init(&calibration);

    int64_t next_calibration_report_ms = 0;

    printk("Calibrando: mantener el MPU quieto y nivelado.\n");

    while (true) {
        
        ImuReading imu;
        ret = imu_read(&imu);

        if (!calibration.ready) {
            if (ret == IMU_NEW && !imu.range_limited) {
                
                /*Norma de la aceleración [Raíz de la suma de los cuadrados de cada eje]*/
                float accel_norm = sqrtf(
                    imu.accel_mps2.x * imu.accel_mps2.x +
                    imu.accel_mps2.y * imu.accel_mps2.y +
                    imu.accel_mps2.z * imu.accel_mps2.z
                );

                /*Norma del giro [Raíz de la suma de los cuadrados de cada eje]*/
                float gyro_norm = sqrtf(
                    imu.gyro_dps.x * imu.gyro_dps.x +
                    imu.gyro_dps.y * imu.gyro_dps.y +
                    imu.gyro_dps.z * imu.gyro_dps.z
                );

                gyro_calibration_update( &calibration, &imu.gyro_dps, accel_norm, gyro_norm );

            } else if (ret < 0 || (ret == IMU_NEW && imu.range_limited)) {
                
                gyro_calibration_update(&calibration, NULL, 0.0f, 0.0f);
            }

            int64_t now_ms = k_uptime_get();

            if (calibration.ready) {
                printk(
                    "Sesgo gyro: %.3f %.3f %.3f deg/s\n",
                    (double)calibration.bias_dps.x,
                    (double)calibration.bias_dps.y,
                    (double)calibration.bias_dps.z
                );
            } else if (now_ms >= next_calibration_report_ms) {
                printk(
                    "Calibrando: %u/%u (imu=%d)\n",
                    (unsigned int)calibration.count,
                    TRACKER_GYRO_CALIBRATION_SAMPLES,
                    ret
                );

                next_calibration_report_ms = now_ms + 1000;
            }

            k_sleep(K_MSEC(20));
            continue;
        }

        if (ret == IMU_NEW) {
            printk(
                "A[m/s2]: %.3f %.3f %.3f | "
                "G[deg/s]: %.3f %.3f %.3f | SAT=%d\n",
                (double)imu.accel_mps2.x,
                (double)imu.accel_mps2.y,
                (double)imu.accel_mps2.z,
                (double)imu.gyro_dps.x,
                (double)imu.gyro_dps.y,
                (double)imu.gyro_dps.z,
                (int)imu.range_limited
            );

        printk(
            "G corregido[deg/s]: %.3f %.3f %.3f\n",
            (double)(imu.gyro_dps.x - calibration.bias_dps.x),
            (double)(imu.gyro_dps.y - calibration.bias_dps.y),
            (double)(imu.gyro_dps.z - calibration.bias_dps.z)
        );
            
        } else {
            printk("imu_read: %d (0 = sin muestra nueva)\n", ret);
        }

        /* Tras un error de bus, terminar esta vuelta. */
        if (ret < 0) {
            k_sleep(K_SECONDS(2));
            continue;
        }

        ret = environment_start();

        if (ret == 0) {
            /*
             * Desde que termina F5, k_msleep redondea los 40 ms hacia arriba
             * y el kernel compensa el tick parcial del timeout relativo.
             * No hace falta sumar otro tick en este hilo de ensayo.
             */
            k_msleep(ENVIRONMENT_CONVERSION_WAIT_MS);

            EnvironmentReading environment;

            ret = environment_read(&environment);

            if (ret == 0) {
                printk(
                    "Temperatura: %.2f C | Humedad: %.2f %%\n",
                    (double)environment.temperature_c,
                    (double)environment.humidity_pct
                );
            }
        }

        if (ret < 0) {
            printk("ENV error: %d\n", ret);
        }

        k_sleep(K_SECONDS(2));
    }

    return 0;
}
