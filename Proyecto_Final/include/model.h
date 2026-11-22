#ifndef DRONE_TRACKER_MODEL_H
#define DRONE_TRACKER_MODEL_H

#include <stdbool.h>
#include <stdint.h>

/* Tipos compartidos entre adquisicion, GPS, aplicacion y formatos. */

/*
 * Calidad de la ultima medida de una familia:
 *   last_good_ms = -1           nunca hubo una medida utilizable
 *   valid = true                la ultima lectura es buena y es de last_good_ms
 *   valid = false               la ultima lectura fallo; el valor y la fecha son los de la ultima buena
 */
typedef struct {
    bool valid;
    int64_t last_good_ms;
} SampleQuality;

/* Vector de tres componentes; la unidad la da el nombre del campo. */
typedef struct {
    float x;
    float y;
    float z;
} Vec3f;

/* Aceleracion con gravedad (m/s2) y giro sin sesgo (grados/s), ejes del sensor. */
typedef struct {
    Vec3f accel_mps2;
    Vec3f gyro_dps;
    SampleQuality quality;
    bool range_limited;       /* algun eje saturado */
    bool calibration_pending; /* aun no hay sesgo del giroscopio */
} ImuData;

/* Luz relativa a la calibracion del montaje, 0-100 %. */
typedef struct {
    float light_pct;
    SampleQuality quality;
    bool clipped;             /* el codigo ADC salio de las referencias y se recorto */
} LightData;

/* Temperatura y humedad de una misma medida del Si7021. */
typedef struct {
    float temperature_c;
    float humidity_pct;
    SampleQuality quality;
} EnvironmentData;

/* Cuaternion Hamilton [w, x, y, z]; el campo que lo contiene dice entre que marcos. */
typedef struct {
    float w;
    float x;
    float y;
    float z;
} Quaternionf;

/*
 * Navegacion inercial, toda en los ejes del sensor al terminar la calibracion
 * (orientacion inicial cero, SR4). Z es altura si la placa arranca nivelada.
 *   q   sensor actual -> ejes iniciales; de aqui salen ROLL/PITCH/YAW
 */
typedef struct {
    Quaternionf q;
    Vec3f velocity_mps;
    Vec3f position_m;
} NavigationView;

/* Indices de las cinco metricas de NORMAL: stats.metric[STATS_TEMP].mean, ... */
typedef enum {
    STATS_LIGHT = 0,  /* % */
    STATS_TEMP,       /* grados C */
    STATS_RH,         /* % */
    STATS_ACCEL_NORM, /* |a| en m/s2 */
    STATS_GYRO_NORM,  /* |w| en grados/s */
    STATS_METRIC_COUNT
} StatsMetric;

typedef struct {
    float mean;
    float min;
    float max;
    uint32_t count;
} MetricStats;

/* Ultima ventana completa de 30 s; ready = false hasta cerrar la primera. */
typedef struct {
    bool ready;
    int64_t begin_ms;
    int64_t end_ms;
    MetricStats metric[STATS_METRIC_COUNT];
} StatsResult;

/* Copia coherente que entrega acquisition_snapshot_get(). */
typedef struct {
    ImuData imu;
    LightData light;
    EnvironmentData environment;
    NavigationView nav;
    bool moving;              /* movimiento confirmado (FLYING si no hay PROBLEM) */
    StatsResult stats;
} AcquisitionSnapshot;

/*
 * Copia que entrega gps_snapshot_get(). Enlace, fix y hora son independientes:
 * puede haber NMEA sin posicion, y eso no es un fallo del enlace.
 */
typedef struct {
    int32_t lat_udeg;         /* microgrados */
    int32_t lon_udeg;
    int32_t alt_mm;
    uint8_t satellites;
    uint32_t utc_ms_of_day;   /* ms desde medianoche UTC */
    bool utc_valid;
    bool fix_valid;           /* ultima GGA con calidad 1 o 2 */
    int64_t last_nmea_ms;     /* ultima NMEA valida; -1 si ninguna */
    int64_t last_fix_ms;      /* ultima posicion valida; -1 si ninguna */
} GpsSnapshot;

/* Modos de B1, en orden circular. */
typedef enum {
    APP_MODE_TEST = 0,
    APP_MODE_NORMAL,
    APP_MODE_SIMU
} AppMode;

/* Estado del tracker (SR7). */
typedef enum {
    TRACKER_STATE_IDLE = 0,
    TRACKER_STATE_FLYING,
    TRACKER_STATE_PROBLEM
} TrackerState;

/* Bits del campo FLAGS de la trama SIMU (tabla 5). */
typedef enum {
    TRACKER_FLAG_IMU_INVALID         = 0x0001,
    TRACKER_FLAG_GPS_INVALID         = 0x0002,
    TRACKER_FLAG_LIGHT_WARNING       = 0x0004,
    TRACKER_FLAG_ENV_INVALID         = 0x0008,
    TRACKER_FLAG_IMPACT              = 0x0010, /* no se detectan impactos: siempre 0 */
    TRACKER_FLAG_CALIBRATION_PENDING = 0x0020,
    TRACKER_FLAG_INERTIAL_ONLY       = 0x0040,
    TRACKER_FLAG_YAW_UNRELIABLE      = 0x0080
} TrackerFlag;

/* Decision unica que usan el LED y los dos formatos. */
typedef struct {
    TrackerState state;
    uint16_t flags;
} TrackerStatus;

#endif
