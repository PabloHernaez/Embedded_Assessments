#ifndef DRONE_TRACKER_ACQUISITION_IMU_H
#define DRONE_TRACKER_ACQUISITION_IMU_H

#include "model.h"
#include "../algorithms/gyro_calibration.h"
#include "../algorithms/motion.h"

typedef struct {
    bool configured;
    bool bus_interrupted; /* Error o recuperación física en esta vuelta. */
    uint8_t consecutive_errors;
    bool review_pending;
    int64_t next_review_ticks;
    int64_t last_init_ticks;
    int64_t next_configuration_check_ticks;

    ImuData data;
    /* Magnitudes de la última muestra utilizable tras calibrar; mismas unidades. */
    float accel_norm_mps2;
    float gyro_norm_dps;
    NavigationView nav;
    bool moving;

    GyroCalibration calibration;
    Vec3f calibration_accel_sum;
    Quaternionf initial_q;
    bool navigation_time_pending;

    MotionDetector motion;
    int64_t previous_imu_ticks;

    int64_t capture_begin_ticks;
    int64_t sample_ticks;
} AcquisitionImu;

/**
 * Prepara el contexto IMU y realiza un intento de configuración del hardware.
 * Entradas: ctx válido y escribible; alimentación estabilizada y contexto de hilo.
 * Retorno: ninguno (void); ctx->configured indica el resultado del driver.
 * Modifica: sobrescribe todo *ctx con el estado inicial y configura la IMU.
 */
void acquisition_imu_init(AcquisitionImu *ctx);

/**
 * Atiende una lectura y el procesamiento IMU, movimiento y navegación.
 * Entradas: ctx válido, inicializado y usado por un único hilo.
 * Retorno: true si hay datos/calidad que publicar o se atiende revisión/init;
 *          false durante la espera de reintento o NO_NEW. false no garantiza ctx sin cambios.
 * Modifica: estado de lectura/procesamiento y tiempos de *ctx. NO_NEW fecha
 *           el intento; conserva medidas y procesamiento.
 *           Reinicia bus_interrupted en cada vuelta. Los fallos conservan los
 *           últimos valores utilizables y programan revisión/init cuando corresponde.
 */
bool acquisition_imu_update(AcquisitionImu *ctx);

/**
 * Comprueba PWR/configuración cada 2 s cuando termina el resto del trabajo I2C.
 * Entradas: ctx válido e inicializado; bus_allowed exige que no haya fallado
 *           una operación previa de esta vuelta. Uso desde el hilo de adquisición.
 * Retorno: true si se invalida calidad y hay que publicar; false si no toca,
 *          no está permitido o la configuración coincide.
 * Modifica: plazos de revisión, configured y calidad/continuidad ante anomalías.
 *           Conserva medidas/fecha, trayectoria y calibración terminada. No acumula.
 */
bool acquisition_imu_check_configuration(AcquisitionImu *ctx, bool bus_allowed);

#endif
