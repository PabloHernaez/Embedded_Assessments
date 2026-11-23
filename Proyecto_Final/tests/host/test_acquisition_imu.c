#include <errno.h>
#include <math.h>
#include <stdio.h>

#include "acquisition_imu.h"
#include "imu.h"
#include <zephyr/kernel.h>

#define CHECK(expr) do { ++checks; if (!(expr)) { \
    printf("FAIL line %d: %s\n", __LINE__, #expr); return 1; } } while (0)

static int init_result;
static unsigned int init_count;
static unsigned int recover_count;
static int read_result;
static unsigned int read_count;
static int64_t now_ticks;
static ImuReading next_reading;

int imu_init(void)
{
    ++init_count;
    return init_result;
}

int imu_recover_bus(void)
{
    ++recover_count;
    return 0;
}

int imu_read(ImuReading *out)
{
    ++read_count;
    if (read_result == IMU_NEW) {
        *out = next_reading;
    }
    return read_result;
}

int64_t k_uptime_ticks(void)
{
    return now_ticks;
}

int64_t k_ticks_to_ms_floor64(int64_t ticks)
{
    /* Igual que el perfil de produccion: 10000 ticks/s. */
    return ticks / 10;
}

int64_t k_ms_to_ticks_floor64(int64_t ms)
{
    return ms * 10;
}

int64_t k_ms_to_ticks_ceil64(int64_t ms) { return ms * 10; }

static bool step_ticks(AcquisitionImu *ctx, int result, int64_t elapsed_ticks)
{
    read_result = result;
    now_ticks += elapsed_ticks;
    return acquisition_imu_update(ctx);
}

static bool step(AcquisitionImu *ctx, int result)
{
    return step_ticks(ctx, result, 200); /* 20 ms. */
}

static bool near_vec(Vec3f a, Vec3f b)
{
    return fabsf(a.x - b.x) < 1e-5f &&
           fabsf(a.y - b.y) < 1e-5f &&
           fabsf(a.z - b.z) < 1e-5f;
}

static bool near_q(Quaternionf a, Quaternionf b)
{
    return fabsf(a.w - b.w) < 1e-5f &&
           fabsf(a.x - b.x) < 1e-5f &&
           fabsf(a.y - b.y) < 1e-5f &&
           fabsf(a.z - b.z) < 1e-5f;
}

static bool near_navigation_values(NavigationView a, NavigationView b)
{
    return near_q(a.q, b.q) &&
           near_vec(a.velocity_mps, b.velocity_mps) &&
           near_vec(a.position_m, b.position_m);
}

static Vec3f sensor_accel_for(Vec3f corrected)
{
    /* Lectura sintetica: invertir la correccion de seis posturas del montaje. */
    return (Vec3f){
        .x = corrected.x / TRACKER_ACCEL_SCALE_X + TRACKER_ACCEL_OFFSET_X,
        .y = corrected.y / TRACKER_ACCEL_SCALE_Y + TRACKER_ACCEL_OFFSET_Y,
        .z = corrected.z / TRACKER_ACCEL_SCALE_Z + TRACKER_ACCEL_OFFSET_Z,
    };
}

static int test_calibration(void)
{
    unsigned int checks = 0;
    AcquisitionImu ctx;
    const Quaternionf identity = {.w = 1.0f};
    const Vec3f zero = {0};

    /* Init fallido: no se lee ni se calibra. */
    init_result = -ENODEV;
    now_ticks = 0;
    acquisition_imu_init(&ctx);
    CHECK(!ctx.configured && !ctx.data.quality.valid);
    CHECK(ctx.data.quality.last_good_ms == -1 && ctx.data.calibration_pending);
    CHECK(!step(&ctx, IMU_NEW) && read_count == 0 && ctx.calibration.count == 0);

    init_result = 0;
    acquisition_imu_init(&ctx);
    CHECK(ctx.configured && near_q(ctx.nav.q, identity) && ctx.sample_ticks == -1);

    /* Una muestra en reposo: cuenta 1, dato valido sin corregir todavia. */
    next_reading = (ImuReading){
        .accel_mps2 = {0.0f, 0.0f, TRACKER_GRAVITY_MPS2},
        .gyro_dps = {1.0f, -2.0f, 0.5f},
    };
    CHECK(step(&ctx, IMU_NEW) && ctx.calibration.count == 1);
    CHECK(ctx.data.quality.valid && ctx.data.calibration_pending);
    CHECK(near_vec(ctx.data.gyro_dps, next_reading.gyro_dps));
    CHECK(ctx.data.quality.last_good_ms == now_ticks / 10);

    /* NO_NEW no cuenta ni cambia el dato. */
    const ImuData retained = ctx.data;
    CHECK(!step(&ctx, IMU_NO_NEW) && ctx.calibration.count == 1);
    CHECK(ctx.data.quality.last_good_ms == retained.quality.last_good_ms);

    /* Error, saturacion o movimiento reinician las dos sumas. */
    CHECK(step(&ctx, -EIO) && ctx.calibration.count == 0);
    CHECK(near_vec(ctx.calibration.accel_sum_mps2, zero) && !ctx.data.quality.valid);
    CHECK(ctx.data.quality.last_good_ms == retained.quality.last_good_ms);
    CHECK(step(&ctx, IMU_NEW) && ctx.calibration.count == 1);
    next_reading.range_limited = true;
    CHECK(step(&ctx, IMU_NEW) && ctx.calibration.count == 0 && ctx.data.range_limited);
    next_reading.range_limited = false;
    CHECK(step(&ctx, IMU_NEW) && ctx.calibration.count == 1);
    next_reading.gyro_dps.x = 6.0f; /* > 5 grados/s */
    CHECK(step(&ctx, IMU_NEW) && ctx.calibration.count == 0);
    next_reading.gyro_dps.x = 1.0f;
    next_reading.accel_mps2.z = 12.0f; /* | |a| - g | > 0,3 */
    CHECK(step(&ctx, IMU_NEW) && ctx.calibration.count == 0);

    /* 100 muestras en reposo, en dos grupos: sesgo y g0 son las medias, no la ultima. */
    Vec3f accel_sum = zero;
    for (unsigned int i = 0; i < TRACKER_CALIBRATION_SAMPLES; ++i) {
        const bool second_half = i >= TRACKER_CALIBRATION_SAMPLES / 2U;
        next_reading.accel_mps2 = second_half ? (Vec3f){2.0f, 0.0f, 9.6f}
                                              : (Vec3f){0.0f, 0.0f, TRACKER_GRAVITY_MPS2};
        next_reading.gyro_dps = second_half ? (Vec3f){3.0f, 0.0f, -0.5f}
                                            : (Vec3f){1.0f, -2.0f, 0.5f};
        CHECK(step(&ctx, IMU_NEW));
        accel_sum.x += ctx.data.accel_mps2.x;
        accel_sum.y += ctx.data.accel_mps2.y;
        accel_sum.z += ctx.data.accel_mps2.z;
        CHECK(ctx.data.calibration_pending == (i + 1U < TRACKER_CALIBRATION_SAMPLES));
    }
    const float n = (float)TRACKER_CALIBRATION_SAMPLES;
    CHECK(ctx.calibration.ready);
    CHECK(near_vec(ctx.calibration.gyro_bias_dps, (Vec3f){2.0f, -1.0f, 0.0f}));
    CHECK(near_vec(ctx.calibration.gravity_mps2,
                   (Vec3f){accel_sum.x / n, accel_sum.y / n, accel_sum.z / n}));
    CHECK(near_vec(ctx.data.gyro_dps, (Vec3f){1.0f, 1.0f, -0.5f})); /* ya sin sesgo */

    /* Orientacion inicial cero (SR4) y la primera muestra no integra (dt = 0). */
    CHECK(near_q(ctx.nav.q, identity));
    CHECK(near_vec(ctx.nav.velocity_mps, zero) && near_vec(ctx.nav.position_m, zero));

    /* Una calibracion terminada no se repite: ni fallos ni muestras la cambian. */
    const Vec3f bias = ctx.calibration.gyro_bias_dps;
    const Vec3f g0 = ctx.calibration.gravity_mps2;
    CHECK(step(&ctx, -EIO));
    next_reading.range_limited = true;
    CHECK(step(&ctx, IMU_NEW));
    next_reading.range_limited = false;
    CHECK(step(&ctx, IMU_NEW) && !ctx.data.calibration_pending);
    CHECK(near_vec(ctx.calibration.gyro_bias_dps, bias) &&
          near_vec(ctx.calibration.gravity_mps2, g0));

    printf("PASS: %u acquisition IMU calibration checks\n", checks);
    return 0;
}

static int test_motion(void)
{
    unsigned int checks = 0;
    AcquisitionImu ctx;
    init_result = 0;
    now_ticks = 0;
    acquisition_imu_init(&ctx);

    CHECK(!ctx.moving && !ctx.motion.moving);
    CHECK(ctx.motion.pending_since_ms == -1);
    CHECK(ctx.previous_imu_ticks == -1);

    /* No evaluar movimiento mientras falta la calibracion. */
    next_reading = (ImuReading){
        .accel_mps2 = {0.0f, 0.0f, TRACKER_GRAVITY_MPS2},
        .gyro_dps = {20.0f, 0.0f, 0.0f},
    };
    for (int i = 0; i < 30; ++i) {
        CHECK(step(&ctx, IMU_NEW));
        CHECK(ctx.data.calibration_pending && !ctx.moving);
        CHECK(ctx.motion.pending_since_ms == -1);
        CHECK(ctx.previous_imu_ticks == -1);
    }

    /* Sesgo deliberado de 4 grados/s: el detector recibe giro corregido. */
    next_reading.gyro_dps.x = 4.0f;
    for (unsigned int i = 0; i < TRACKER_CALIBRATION_SAMPLES; ++i) {
        CHECK(step(&ctx, IMU_NEW));
    }
    CHECK(!ctx.data.calibration_pending);
    CHECK(ctx.previous_imu_ticks == ctx.sample_ticks);
    next_reading.gyro_dps.x = 9.0f; /* Corregido: 5, por debajo de 8. */
    for (int i = 0; i < 30; ++i) {
        CHECK(step(&ctx, IMU_NEW));
        CHECK(!ctx.moving && ctx.motion.pending_since_ms == -1);
    }

    /* Con giro corregido de 9, confirmar a 300 ms, nunca antes. */
    next_reading.gyro_dps.x = 13.0f;
    CHECK(step(&ctx, IMU_NEW));
    int64_t candidate_ms = ctx.data.quality.last_good_ms;
    CHECK(ctx.motion.pending_since_ms == candidate_ms);
    for (int elapsed_ms = 20; elapsed_ms < TRACKER_MOTION_CONFIRM_MS;
         elapsed_ms += 20) {
        CHECK(step(&ctx, IMU_NEW));
        CHECK(!ctx.moving);
    }
    CHECK(step(&ctx, IMU_NEW));
    CHECK(ctx.moving && ctx.motion.moving);
    CHECK(ctx.data.quality.last_good_ms - candidate_ms == 300);

    /* Reposo corregido: una interrupcion cancela la confirmacion, no moving. */
    next_reading.gyro_dps.x = 4.0f;
    CHECK(step(&ctx, IMU_NEW));
    for (int i = 0; i < 90; ++i) {
        CHECK(step(&ctx, IMU_NEW));
        CHECK(ctx.moving);
    }
    CHECK(ctx.motion.pending_since_ms >= 0);
    int64_t last_good_ms = ctx.data.quality.last_good_ms;
    CHECK(step(&ctx, -EIO));
    CHECK(ctx.moving && !ctx.data.quality.valid);
    CHECK(ctx.data.quality.last_good_ms == last_good_ms);
    CHECK(ctx.motion.pending_since_ms == -1);
    CHECK(ctx.previous_imu_ticks == -1);
    CHECK(ctx.calibration.ready && !ctx.data.calibration_pending);

    CHECK(step(&ctx, IMU_NEW));
    candidate_ms = ctx.data.quality.last_good_ms;
    for (int elapsed_ms = 20; elapsed_ms < TRACKER_REST_CONFIRM_MS;
         elapsed_ms += 20) {
        CHECK(step(&ctx, IMU_NEW));
        CHECK(ctx.moving);
    }
    CHECK(step(&ctx, IMU_NEW));
    CHECK(!ctx.moving && !ctx.motion.moving);
    CHECK(ctx.data.quality.last_good_ms - candidate_ms == 2000);

    /* NO_NEW no evalua ni cambia la referencia de la ultima utilizable. */
    next_reading.gyro_dps.x = 13.0f;
    CHECK(step(&ctx, IMU_NEW));
    candidate_ms = ctx.motion.pending_since_ms;
    int64_t previous_ticks = ctx.previous_imu_ticks;
    CHECK(!step(&ctx, IMU_NO_NEW));
    CHECK(ctx.previous_imu_ticks == previous_ticks);
    CHECK(ctx.motion.pending_since_ms == candidate_ms);
    CHECK(step(&ctx, IMU_NEW)); /* 40 ms entre utilizables. */
    CHECK(ctx.motion.pending_since_ms == candidate_ms);

    /* A 50 ms exactos se conserva; un tick adicional rompe continuidad. */
    CHECK(step_ticks(&ctx, IMU_NEW, 500));
    CHECK(ctx.motion.pending_since_ms == candidate_ms);
    CHECK(step_ticks(&ctx, IMU_NEW, 501));
    CHECK(!ctx.moving);
    CHECK(ctx.motion.pending_since_ms == ctx.data.quality.last_good_ms);
    CHECK(ctx.motion.pending_since_ms != candidate_ms);

    /* Saturacion cancela tambien una confirmacion de movimiento pendiente. */
    next_reading.range_limited = true;
    CHECK(step(&ctx, IMU_NEW));
    CHECK(!ctx.moving && !ctx.data.quality.valid);
    CHECK(ctx.motion.pending_since_ms == -1);
    CHECK(ctx.previous_imu_ticks == -1);
    next_reading.range_limited = false;
    CHECK(step(&ctx, IMU_NEW));
    for (int elapsed_ms = 20; elapsed_ms < TRACKER_MOTION_CONFIRM_MS;
         elapsed_ms += 20) {
        CHECK(step(&ctx, IMU_NEW));
        CHECK(!ctx.moving);
    }
    CHECK(step(&ctx, IMU_NEW));
    CHECK(ctx.moving);

    /* Un hueco largo tambien reinicia la confirmacion de reposo. */
    next_reading.gyro_dps.x = 4.0f;
    CHECK(step(&ctx, IMU_NEW));
    candidate_ms = ctx.motion.pending_since_ms;
    CHECK(step_ticks(&ctx, IMU_NEW, 501));
    CHECK(ctx.moving);
    CHECK(ctx.motion.pending_since_ms > candidate_ms);

    printf("PASS: %u acquisition IMU motion checks\n", checks);
    return 0;
}

/* Calibra en reposo con la placa inclinada (g0 fuera del eje Z). */
static int calibrate_tilted(AcquisitionImu *ctx, Vec3f g_sensor)
{
    unsigned int checks = 0;
    init_result = 0;
    now_ticks = 0;
    acquisition_imu_init(ctx);
    next_reading = (ImuReading){ .accel_mps2 = sensor_accel_for(g_sensor) };
    for (unsigned int i = 0; i < TRACKER_CALIBRATION_SAMPLES; ++i) {
        CHECK(step(ctx, IMU_NEW));
    }
    CHECK(ctx->calibration.ready);
    return 0;
}

static int test_navigation(void)
{
    unsigned int checks = 0;
    AcquisitionImu ctx;
    const Vec3f zero = {0};
    const Quaternionf identity = {.w = 1.0f};
    const Vec3f g_tilted = {0.0f, 0.6f * TRACKER_GRAVITY_MPS2, 0.8f * TRACKER_GRAVITY_MPS2};

    /* Arranque inclinado y quieto: orientacion cero y ninguna deriva. */
    CHECK(calibrate_tilted(&ctx, g_tilted) == 0);
    for (int i = 0; i < 100; ++i) {
        CHECK(step(&ctx, IMU_NEW));
    }
    CHECK(near_q(ctx.nav.q, identity));
    CHECK(near_vec(ctx.nav.velocity_mps, zero) && near_vec(ctx.nav.position_m, zero));

    /* 1 m/s2 sobre el eje X inicial: Euler explicito con los dt reales. */
    next_reading.accel_mps2 = sensor_accel_for((Vec3f){1.0f, g_tilted.y, g_tilted.z});
    CHECK(step(&ctx, IMU_NEW)); /* dt = 20 ms */
    CHECK(near_vec(ctx.nav.velocity_mps, (Vec3f){0.02f, 0.0f, 0.0f}));
    CHECK(near_vec(ctx.nav.position_m, zero));
    CHECK(step_ticks(&ctx, IMU_NEW, 400)); /* dt = 40 ms */
    CHECK(near_vec(ctx.nav.velocity_mps, (Vec3f){0.06f, 0.0f, 0.0f}));
    CHECK(near_vec(ctx.nav.position_m, (Vec3f){0.0008f, 0.0f, 0.0f}));
    CHECK(step_ticks(&ctx, IMU_NEW, 500)); /* 50 ms exactos se integran */
    CHECK(near_vec(ctx.nav.velocity_mps, (Vec3f){0.11f, 0.0f, 0.0f}));
    CHECK(near_vec(ctx.nav.position_m, (Vec3f){0.0038f, 0.0f, 0.0f}));

    /* NO_NEW no integra; la siguiente muestra usa los 40 ms reales. */
    NavigationView retained = ctx.nav;
    const int64_t previous = ctx.previous_imu_ticks;
    CHECK(!step(&ctx, IMU_NO_NEW) && near_navigation_values(ctx.nav, retained));
    CHECK(ctx.previous_imu_ticks == previous);
    CHECK(step(&ctx, IMU_NEW));
    CHECK(near_vec(ctx.nav.velocity_mps, (Vec3f){0.15f, 0.0f, 0.0f}));

    /* Error o saturacion: se conservan q/v/p y la siguiente muestra usa dt = 0. */
    retained = ctx.nav;
    CHECK(step(&ctx, -EIO) && ctx.previous_imu_ticks == -1);
    CHECK(near_navigation_values(ctx.nav, retained));
    CHECK(step_ticks(&ctx, IMU_NEW, 2000) && near_navigation_values(ctx.nav, retained));
    next_reading.range_limited = true;
    CHECK(step(&ctx, IMU_NEW) && near_navigation_values(ctx.nav, retained));
    next_reading.range_limited = false;
    CHECK(step(&ctx, IMU_NEW) && near_navigation_values(ctx.nav, retained));

    /* Hueco de 50 ms + 1 tick: tampoco se integra. */
    CHECK(step_ticks(&ctx, IMU_NEW, 501) && near_navigation_values(ctx.nav, retained));
    CHECK(step(&ctx, IMU_NEW));
    CHECK(near_vec(ctx.nav.velocity_mps, (Vec3f){0.17f, 0.0f, 0.0f}));

    /* Giro de 90 grados/s sobre Z del sensor durante 1 s desde una postura inclinada:
     * q es el giro respecto a la postura inicial, no respecto a la vertical. */
    CHECK(calibrate_tilted(&ctx, g_tilted) == 0);
    next_reading.gyro_dps.z = 90.0f;
    for (int i = 0; i < 50; ++i) {
        CHECK(step(&ctx, IMU_NEW));
    }
    const float c = sqrtf(0.5f);
    CHECK(fabsf(ctx.nav.q.w - c) < 1e-3f && fabsf(ctx.nav.q.z - c) < 1e-3f);
    CHECK(fabsf(ctx.nav.q.x) < 1e-4f && fabsf(ctx.nav.q.y) < 1e-4f);

    printf("PASS: %u acquisition IMU navigation checks\n", checks);
    return 0;
}

/* Calibra con muestras en reposo y deja una trayectoria no nula. */
static int prepare_navigation(AcquisitionImu *ctx)
{
    unsigned int checks = 0;
    init_result = 0;
    now_ticks = 0;
    acquisition_imu_init(ctx);
    next_reading = (ImuReading){
        .accel_mps2 = sensor_accel_for((Vec3f){0.0f, 0.0f, TRACKER_GRAVITY_MPS2}),
        .gyro_dps = {0.5f, 0.0f, 0.0f},
    };
    for (unsigned int i = 0; i < TRACKER_CALIBRATION_SAMPLES; ++i) {
        CHECK(step(ctx, IMU_NEW));
    }
    next_reading.accel_mps2 = sensor_accel_for((Vec3f){1.0f, 0.0f, TRACKER_GRAVITY_MPS2});
    for (int i = 0; i < 10; ++i) {
        CHECK(step(ctx, IMU_NEW));
    }
    CHECK(ctx->nav.velocity_mps.x > 0.1f);
    return 0;
}

static int test_recovery(void)
{
    unsigned int checks = 0;
    AcquisitionImu ctx;

    /* Arranque: recupera el bus e inicializa una vez. */
    init_count = 0;
    recover_count = 0;
    CHECK(prepare_navigation(&ctx) == 0);
    CHECK(init_count == 1 && recover_count == 1);
    const Vec3f bias = ctx.calibration.gyro_bias_dps;
    const Vec3f g0 = ctx.calibration.gravity_mps2;

    /* Uno o dos errores: dato invalido, bus marcado, sin reinicio. */
    NavigationView retained = ctx.nav;
    CHECK(step(&ctx, -EIO) && ctx.bus_error && !ctx.data.quality.valid);
    CHECK(step(&ctx, -EIO) && init_count == 1);
    CHECK(step(&ctx, IMU_NEW) && !ctx.bus_error && ctx.data.quality.valid);
    CHECK(near_navigation_values(ctx.nav, retained)); /* dt = 0 */

    /* Tres errores seguidos: la vuelta siguiente reinicia sin leer. */
    CHECK(step(&ctx, -EIO));
    CHECK(step(&ctx, -EIO));
    CHECK(step(&ctx, -EIO));
    retained = ctx.nav;
    unsigned int reads = read_count;
    CHECK(step(&ctx, IMU_NEW));
    CHECK(init_count == 2 && recover_count == 2 && read_count == reads);
    CHECK(ctx.bus_error && ctx.configured);
    /* El reinicio conserva sesgo, g0 y trayectoria: no se recalibra en vuelo. */
    CHECK(near_vec(ctx.calibration.gyro_bias_dps, bias) &&
          near_vec(ctx.calibration.gravity_mps2, g0));
    CHECK(near_navigation_values(ctx.nav, retained));
    CHECK(step(&ctx, IMU_NEW) && !ctx.data.calibration_pending);

    /* Init fallido: se reintenta como mucho una vez por segundo. */
    init_result = -EIO;
    CHECK(step(&ctx, -EIO));
    CHECK(step(&ctx, -EIO));
    CHECK(step(&ctx, -EIO));
    const int64_t previous_init = ctx.last_init_ticks;
    while (!step(&ctx, IMU_NEW)) {
        CHECK(init_count == 2); /* esperando: ni lee ni reinicia */
    }
    CHECK(init_count == 3 && !ctx.configured);
    CHECK(ctx.last_init_ticks - previous_init >= 10000);
    reads = read_count;
    CHECK(!step(&ctx, IMU_NEW) && read_count == reads);
    init_result = 0;
    now_ticks += 10000;
    CHECK(step(&ctx, IMU_NEW) && init_count == 4 && ctx.configured);

    /* MPU mudo (reset silencioso: duerme y no da DATA_RDY): reinicio a los 500 ms. */
    CHECK(step(&ctx, IMU_NEW) && ctx.data.quality.valid);
    const int64_t last_new = ctx.sample_ticks;
    while (!step(&ctx, IMU_NO_NEW)) {
        CHECK(init_count == 4);
    }
    CHECK(init_count == 5 && now_ticks - last_new > 5000);

    printf("PASS: %u acquisition IMU recovery checks\n", checks);
    return 0;
}

/* Impacto: |a| >= 3 g o un eje saturado marcan la muestra; si no, se conserva la marca. */
static int test_impact(void)
{
    unsigned int checks = 0;
    AcquisitionImu ctx;

    init_result = 0;
    read_result = IMU_NEW;
    now_ticks = 0;
    acquisition_imu_init(&ctx);
    CHECK(ctx.data.last_impact_ms == 0);

    next_reading = (ImuReading){ .accel_mps2 = sensor_accel_for((Vec3f){0.0f, 0.0f, 2.9f * TRACKER_GRAVITY_MPS2}) };
    CHECK(step(&ctx, IMU_NEW) && ctx.data.quality.valid && ctx.data.last_impact_ms == 0);

    next_reading.accel_mps2 = sensor_accel_for((Vec3f){2.2f * TRACKER_GRAVITY_MPS2, 0.0f, 2.2f * TRACKER_GRAVITY_MPS2});
    CHECK(step(&ctx, IMU_NEW) && ctx.data.last_impact_ms == now_ticks / 10);
    const int64_t first = ctx.data.last_impact_ms;

    next_reading.accel_mps2 = sensor_accel_for((Vec3f){0.0f, 0.0f, TRACKER_GRAVITY_MPS2});
    CHECK(step(&ctx, IMU_NEW) && ctx.data.last_impact_ms == first);
    CHECK(step(&ctx, -EIO) && ctx.data.last_impact_ms == first);

    next_reading.range_limited = true;
    CHECK(step(&ctx, IMU_NEW) && ctx.data.range_limited && !ctx.data.quality.valid);
    CHECK(ctx.data.last_impact_ms == now_ticks / 10 && ctx.data.last_impact_ms > first);
    next_reading.range_limited = false;

    printf("PASS: %u acquisition IMU impact checks\n", checks);
    return 0;
}

int main(void)
{
    if (test_calibration() != 0 || test_motion() != 0 || test_navigation() != 0 ||
        test_impact() != 0) {
        return 1;
    }
    return test_recovery();
}
