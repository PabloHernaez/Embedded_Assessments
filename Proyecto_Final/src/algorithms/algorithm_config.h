#ifndef DRONE_TRACKER_ALGORITHM_CONFIG_H
#define DRONE_TRACKER_ALGORITHM_CONFIG_H

/* Umbrales y constantes de calibracion, movimiento y navegacion (registro D3). */

#define TRACKER_GRAVITY_MPS2 9.80665f

/* Calibracion inicial: muestras seguidas en reposo para el sesgo del giro y g0. */
#define TRACKER_CALIBRATION_SAMPLES      100U
#define TRACKER_GYRO_CALIBRATION_MAX_DPS 5.0f /* giro sin descontar sesgo */

/* Reposo: | |a| - g | < 0,3 m/s2 y |w| < 3 grados/s durante 2 s. */
#define TRACKER_REST_ACCEL_DELTA_MPS2 0.3f
#define TRACKER_REST_GYRO_MAX_DPS     3.0f
#define TRACKER_REST_CONFIRM_MS       2000

/* Movimiento: | |a| - g | > 0,8 m/s2 o |w| > 8 grados/s durante 300 ms. */
#define TRACKER_MOTION_ACCEL_DELTA_MPS2 0.8f
#define TRACKER_MOTION_GYRO_MIN_DPS     8.0f
#define TRACKER_MOTION_CONFIRM_MS       300

/* Hueco maximo entre muestras para integrar; mas largo, dt = 0. */
#define TRACKER_IMU_MAX_GAP_MS 50U

/* Correccion del acelerometro medida en seis posturas: (medida - offset) * escala. */
#define TRACKER_ACCEL_OFFSET_X  0.12409f
#define TRACKER_ACCEL_OFFSET_Y (-0.06164f)
#define TRACKER_ACCEL_OFFSET_Z (-0.05113f)
#define TRACKER_ACCEL_SCALE_X   0.9964143f
#define TRACKER_ACCEL_SCALE_Y   0.9966736f
#define TRACKER_ACCEL_SCALE_Z   0.9891509f

#endif
