#ifndef DRONE_TRACKER_MODEL_H
#define DRONE_TRACKER_MODEL_H

#include <stdbool.h>
#include <stdint.h>

/*
 *  model.h incluye todas las estructuras de datos que se van a usar para transmitir los datos
 *  a través de todo el firmware
 */

/* 
 *  Calidad del ultimo resultado de una familia de medidas.
 *  last_good_ms = -1 : Nunca hubo medida usable
 *  valid = true / last_good_ms = ts : calidad correcta con ts
 *  valid = false / last_good_ms = ts_anterior : Falló una lectura posterior
 */
typedef struct {
    bool valid;
    int64_t last_good_ms;
} SampleQuality;

/* 
 *  Vector de tres componentes; la unidad la indica el campo que lo usa
 */
typedef struct {
    float x;
    float y;
    float z;
} Vec3f;

/* 
 *  Aceleracion con gravedad y giro expresado en grados por segundo. 
 *  range_limited indica alcanzar el umbral de saturación
 *  calibration_pending indica que aún falta la calibración inicial del giroscopio
 *  clipped: Hubo que recortar la luz al rango 0-100%
 */
typedef struct {
    Vec3f accel_mps2;
    Vec3f gyro_dps;

    SampleQuality quality;
    bool range_limited;
    bool calibration_pending;
} ImuData;

/* 
 *  Luz relativa a la calibracion del montaje
 */
typedef struct {
    float light_pct;

    SampleQuality quality;
    bool clipped;
} LightData;

/* 
 *  Temperatura y humedad obtenidas en una misma operacion completa
 */
typedef struct {
    float temperature_c;
    float humidity_pct;

    SampleQuality quality;
} EnvironmentData;

/* 
 *  Cuaternion Hamilton [w, x, y, z], del sensor al marco local
 */
typedef struct {
    float w;
    float x;
    float y;
    float z;
} Quaternionf;

/*
*   Resultado publicado del estimador.
*   q: la orientación del sensor respecto al sistema local
*   velocity_mps: velocidad (ojo, es un vector Vec3f)
*   position_m: posicion desde el origen inicial (ojo, es un vector Vec3f)
*
*   El valor inicial de NavigationView tiene 
*       q = [1, 0, 0, 0]
*       velocity_mps, position_m = 0 (0 vector Vec3f)
*       valid = false
*/
typedef struct {
    Quaternionf q;
    Vec3f velocity_mps;
    Vec3f position_m;

    bool valid;
} NavigationView;

/* 
 *  Enum para acceder al array!
 *      Ej: snapshot.stats.metric[STATS_TEMP].mean
 *
 *  Orden para las cinco metricas de NORMAL
 *  STATS_LIGHT -> Luz en porcentaje
 *  STATS_TEMP -> Temperatura en °C
 *  STATS_RH -> Humedad relativa en porcentaje
 *  STATS_ACCEL_NORM -> aceleracion en m/s2
 *  STATS_GYRO_NORM -> Magnitud de giro en degree/s
 *  STATS_METRIC_COUNT -> 5, determina el tamaño del array
 */
typedef enum {
    STATS_LIGHT = 0,
    STATS_TEMP,
    STATS_RH,
    STATS_ACCEL_NORM,
    STATS_GYRO_NORM,
    STATS_METRIC_COUNT
} StatsMetric;

/* 
 *  Estadisticas finales de una metrica.
 */
typedef struct {
    float mean;
    float min;
    float max;
    uint32_t count;
} MetricStats;

/*
 * Ultima ventana completa de estadisticas.
 */
typedef struct {
    bool ready;
    int64_t begin_ms;
    int64_t end_ms;

    MetricStats metric[STATS_METRIC_COUNT];
} StatsResult;

/* 
 *  Copia coherente que acquisition entrega a sus consumidores. Incluye estadísticas
 *
 *  moving -> movimiento confirmado. Eso junto con la salud del 
 *      sistema decide IDLE, FLYING o PROBLEM
 *  progress -> Aumenta al completar una adquisición, aun si fallan los sensores
 *      Con esto se peude distinguir un hilo trabajando de uno bloqueado 
 */
typedef struct {
    ImuData imu;
    LightData light;
    EnvironmentData environment;

    NavigationView nav;
    bool moving;

    StatsResult stats;
    uint32_t progress;
} AcquisitionSnapshot;

/* 
 *  Datos publicados por el servicio GPS. 
 *   
 *  lat_udeg, lon_udeg -> enteros en microgrados (latitud y longitud)
 *  alt_mm -> entero en milímetros (altura)
 *  utc_ms_of_day -> hora del gps desde medianoche
 *  last_nmea_ms, last_fix_ms -> instantes de reloj del MCU, desde el arranque
 *      (-1 en el arranque)
 *
 *  Indicadores de mensaje correcto:
 *      last_nmeas_ms actualizado
 *      Posición usable -> fix_valid = true, se actualiza last_fix_ms
 *      Ultima GGA con hora válida -> utc_valid = true
 *
 *      Ojo! Puede haber comunicación pero no posición. Entonces fix_valid = false
 *      no genera un reporte de daño en el enlace
 *   
 */
typedef struct {
    int32_t lat_udeg;
    int32_t lon_udeg;
    int32_t alt_mm;
    uint8_t satellites;

    /* Hora UTC: milisegundos desde medianoche. */
    uint32_t utc_ms_of_day;
    bool utc_valid;

    bool fix_valid;

    /* Tiempos desde el arranque; -1 si nunca hubo resultado. */
    int64_t last_nmea_ms;
    int64_t last_fix_ms;

    uint32_t progress;
} GpsSnapshot;

/* 
    Modo de funcionamiento seleccionado por el usuario. 
    El modo reporta cómo funciona y reporta con la aplicación
*/
typedef enum {
    APP_MODE_TEST = 0,
    APP_MODE_NORMAL,
    APP_MODE_SIMU
} AppMode;

/* 
    Estado global decidido por la aplicacion.
    En qué situación está el sistema
*/
typedef enum {
    TRACKER_STATE_IDLE = 0,
    TRACKER_STATE_FLYING,
    TRACKER_STATE_PROBLEM
} TrackerState;

/* 
 *  Bits definidos por el protocolo SIMU. 
 *  Los flags permiten establecer varias condiciones simultaneamente.
 *  Para combinarlas, se debe utilizar el operador | (OR de bits)
 *  ej: uint16_t flags = TRACKER_FLAG_INERTIAL_ONLY | TRACKER_FLAG_YAW_UNRELIABLE;
 */
typedef enum {
    TRACKER_FLAG_IMU_INVALID         = 0x0001,
    TRACKER_FLAG_GPS_INVALID         = 0x0002,
    TRACKER_FLAG_LIGHT_WARNING       = 0x0004,
    TRACKER_FLAG_ENV_INVALID         = 0x0008,
    TRACKER_FLAG_IMPACT              = 0x0010,
    TRACKER_FLAG_CALIBRATION_PENDING = 0x0020,
    TRACKER_FLAG_INERTIAL_ONLY       = 0x0040,
    TRACKER_FLAG_YAW_UNRELIABLE      = 0x0080
} TrackerFlag;

/* Decision final que comparten LED y formateadores. */
typedef struct {
    TrackerState state;
    uint16_t flags;
} TrackerStatus;

#endif