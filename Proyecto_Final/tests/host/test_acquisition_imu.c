#include <errno.h>
#include <math.h>
#include <stdio.h>

#include "acquisition_imu.h"
#include "estimator.h"
#include "imu.h"
#include <zephyr/kernel.h>

#define CHECK(expr) do { ++checks; if (!(expr)) { \
    printf("FAIL line %d: %s\n", __LINE__, #expr); return 1; } } while (0)

static int init_result;
static int read_result;
static unsigned int read_count;
static int64_t now_ticks;
static ImuReading next_reading;

int imu_init(void)
{
    return init_result;
}

int imu_check_configuration(bool *matches) { *matches = true; return 0; }
int imu_bus_is_blocked(bool *blocked) { *blocked = false; return 0; }
int imu_recover_bus(void) { return 0; }

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
    return near_q(a.q, b.q) && near_q(a.relative_q, b.relative_q) &&
           near_vec(a.velocity_mps, b.velocity_mps) &&
           near_vec(a.position_m, b.position_m);
}

static Vec3f sensor_accel_for(Vec3f corrected)
{
    /* Lectura sintetica: invertir las correcciones existentes del montaje. */
    return (Vec3f){
        .x = corrected.x / 0.9964143f + 0.12409f,
        .y = corrected.y / 0.9966736f - 0.06164f,
        .z = corrected.z / 0.9891509f - 0.05113f,
    };
}

static int test_calibration(void)
{
    unsigned int checks = 0;
    AcquisitionImu ctx;
    const Quaternionf identity = {.w = 1.0f};
    const Vec3f zero = {0};

    /* Fallo de init: no leer ni avanzar la calibracion. */
    init_result = -ENODEV;
    acquisition_imu_init(&ctx);
    CHECK(!ctx.configured);
    CHECK(!ctx.data.quality.valid);
    CHECK(ctx.data.quality.last_good_ms == -1);
    CHECK(ctx.data.calibration_pending);
    CHECK(near_q(ctx.initial_q, identity));
    CHECK(!step(&ctx, IMU_NEW));
    CHECK(read_count == 0);
    CHECK(ctx.calibration.count == 0);

    init_result = 0;
    acquisition_imu_init(&ctx);
    CHECK(ctx.configured);
    CHECK(!ctx.nav.valid);
    CHECK(near_q(ctx.nav.q, identity));
    CHECK(near_q(ctx.nav.relative_q, identity));
    CHECK(ctx.capture_begin_ticks == -1 && ctx.sample_ticks == -1);

    next_reading = (ImuReading){
        .accel_mps2 = {0.0f, 0.0f, TRACKER_GRAVITY_MPS2},
        .gyro_dps = {1.0f, -2.0f, 0.5f},
    };
    CHECK(step(&ctx, IMU_NEW));
    CHECK(ctx.calibration.count == 1);
    CHECK(ctx.data.quality.valid && ctx.data.calibration_pending);
    CHECK(near_vec(ctx.data.gyro_dps, next_reading.gyro_dps));
    CHECK(ctx.capture_begin_ticks == now_ticks);
    CHECK(ctx.sample_ticks == now_ticks);
    CHECK(ctx.data.quality.last_good_ms == now_ticks / 10);

    /* NO_NEW no repite ni interrumpe la muestra anterior. */
    ImuData retained = ctx.data;
    Vec3f retained_sum = ctx.calibration_accel_sum;
    CHECK(!step(&ctx, IMU_NO_NEW));
    CHECK(ctx.calibration.count == 1);
    CHECK(near_vec(ctx.calibration_accel_sum, retained_sum));
    CHECK(ctx.data.quality.valid);
    CHECK(ctx.data.quality.last_good_ms == retained.quality.last_good_ms);
    CHECK(near_vec(ctx.data.gyro_dps, retained.gyro_dps));

    /* Error y saturacion cancelan la secuencia, reteniendo el ultimo dato. */
    CHECK(step(&ctx, -EIO));
    CHECK(ctx.calibration.count == 0 && !ctx.calibration.ready);
    CHECK(near_vec(ctx.calibration_accel_sum, zero));
    CHECK(!ctx.data.quality.valid && ctx.data.calibration_pending);
    CHECK(ctx.data.quality.last_good_ms == retained.quality.last_good_ms);
    CHECK(near_vec(ctx.data.gyro_dps, retained.gyro_dps));

    CHECK(step(&ctx, IMU_NEW));
    retained = ctx.data;
    next_reading.range_limited = true;
    CHECK(step(&ctx, IMU_NEW));
    CHECK(ctx.calibration.count == 0);
    CHECK(near_vec(ctx.calibration_accel_sum, zero));
    CHECK(ctx.data.range_limited && !ctx.data.quality.valid);
    CHECK(ctx.data.quality.last_good_ms == retained.quality.last_good_ms);

    /* Movimiento por gyro o aceleracion tambien reinicia ambos acumuladores. */
    next_reading.range_limited = false;
    CHECK(step(&ctx, IMU_NEW));
    next_reading.gyro_dps.x = 6.0f;
    CHECK(step(&ctx, IMU_NEW));
    CHECK(ctx.calibration.count == 0);
    CHECK(near_vec(ctx.calibration_accel_sum, zero));
    CHECK(ctx.data.quality.valid && ctx.data.calibration_pending);
    next_reading.gyro_dps = (Vec3f){1.0f, -2.0f, 0.5f};
    CHECK(step(&ctx, IMU_NEW));
    next_reading.accel_mps2.z = 12.0f;
    CHECK(step(&ctx, IMU_NEW));
    CHECK(ctx.calibration.count == 0);
    CHECK(near_vec(ctx.calibration_accel_sum, zero));

    /* Dos grupos distintos: inclinacion usa la media, no la ultima muestra. */
    Vec3f accel_sum = {0};
    for (unsigned int i = 0; i < TRACKER_GYRO_CALIBRATION_SAMPLES; ++i) {
        bool second_half = i >= TRACKER_GYRO_CALIBRATION_SAMPLES / 2U;
        next_reading.accel_mps2 = second_half
            ? (Vec3f){2.0f, 0.0f, 9.6f}
            : (Vec3f){0.0f, 0.0f, TRACKER_GRAVITY_MPS2};
        next_reading.gyro_dps = second_half
            ? (Vec3f){3.0f, 0.0f, -0.5f}
            : (Vec3f){1.0f, -2.0f, 0.5f};

        CHECK(step(&ctx, IMU_NEW));
        accel_sum.x += ctx.data.accel_mps2.x;
        accel_sum.y += ctx.data.accel_mps2.y;
        accel_sum.z += ctx.data.accel_mps2.z;
        CHECK(ctx.calibration.count == i + 1U);
        CHECK(ctx.data.calibration_pending ==
              (i + 1U < TRACKER_GYRO_CALIBRATION_SAMPLES));
        CHECK(ctx.nav.valid ==
              (i + 1U == TRACKER_GYRO_CALIBRATION_SAMPLES));
    }

    CHECK(ctx.calibration.ready);
    CHECK(near_vec(ctx.calibration.bias_dps, (Vec3f){2.0f, -1.0f, 0.0f}));
    CHECK(near_vec(ctx.data.gyro_dps, (Vec3f){1.0f, 1.0f, -0.5f}));
    Vec3f accel_mean = {
        accel_sum.x / TRACKER_GYRO_CALIBRATION_SAMPLES,
        accel_sum.y / TRACKER_GYRO_CALIBRATION_SAMPLES,
        accel_sum.z / TRACKER_GYRO_CALIBRATION_SAMPLES,
    };
    NavigationView expected;
    estimator_init(&expected);
    CHECK(estimator_set_initial_tilt(&expected, &accel_mean));
    CHECK(near_q(ctx.nav.q, expected.q));
    CHECK(near_q(ctx.initial_q, expected.q));
    CHECK(!near_q(ctx.initial_q, identity));
    CHECK(near_q(ctx.nav.relative_q, identity));
    CHECK(near_vec(ctx.nav.velocity_mps, zero));
    CHECK(near_vec(ctx.nav.position_m, zero));

    /* Tras calibrar, ni nuevas lecturas ni fallos recapturan q0 o el sesgo. */
    Quaternionf initial_q = ctx.initial_q;
    Vec3f bias = ctx.calibration.bias_dps;
    next_reading.gyro_dps = (Vec3f){4.0f, 3.0f, 2.0f};
    CHECK(step(&ctx, IMU_NEW));
    CHECK(near_vec(ctx.data.gyro_dps, (Vec3f){2.0f, 4.0f, 2.0f}));
    retained = ctx.data;
    NavigationView retained_nav = ctx.nav;
    CHECK(ctx.nav.valid);
    CHECK(!step(&ctx, IMU_NO_NEW));
    CHECK(step(&ctx, -EIO));
    next_reading.range_limited = true;
    CHECK(step(&ctx, IMU_NEW));
    CHECK(ctx.calibration.ready && !ctx.data.calibration_pending);
    CHECK(ctx.calibration.count == TRACKER_GYRO_CALIBRATION_SAMPLES);
    CHECK(near_vec(ctx.calibration.bias_dps, bias));
    CHECK(near_q(ctx.initial_q, initial_q));
    CHECK(near_navigation_values(ctx.nav, retained_nav));
    CHECK(!ctx.nav.valid && !ctx.data.quality.valid);
    CHECK(near_vec(ctx.data.gyro_dps, retained.gyro_dps));
    CHECK(ctx.data.quality.last_good_ms == retained.quality.last_good_ms);
    next_reading.range_limited = false;
    CHECK(step(&ctx, IMU_NEW));
    CHECK(ctx.data.quality.valid && !ctx.data.calibration_pending);
    CHECK(near_vec(ctx.data.gyro_dps, retained.gyro_dps));
    CHECK(near_q(ctx.initial_q, initial_q));
    CHECK(ctx.nav.valid && near_navigation_values(ctx.nav, retained_nav));

    /* Muestras aceptables por separado, pero media incompatible con gravedad. */
    acquisition_imu_init(&ctx);
    next_reading.gyro_dps = (Vec3f){1.0f, -2.0f, 0.5f};
    for (unsigned int i = 0; i < TRACKER_GYRO_CALIBRATION_SAMPLES; ++i) {
        next_reading.accel_mps2 = (Vec3f){
            0.0f, 0.0f, i < TRACKER_GYRO_CALIBRATION_SAMPLES / 2U
                ? TRACKER_GRAVITY_MPS2 : -TRACKER_GRAVITY_MPS2,
        };
        CHECK(step(&ctx, IMU_NEW));
    }
    CHECK(!ctx.calibration.ready && ctx.calibration.count == 0);
    CHECK(ctx.data.calibration_pending);
    CHECK(near_vec(ctx.calibration_accel_sum, zero));
    CHECK(near_q(ctx.initial_q, identity));
    CHECK(near_q(ctx.nav.q, identity));
    CHECK(near_vec(ctx.data.gyro_dps, next_reading.gyro_dps));

    next_reading.accel_mps2.z = TRACKER_GRAVITY_MPS2;
    for (unsigned int i = 0; i < TRACKER_GYRO_CALIBRATION_SAMPLES; ++i) {
        CHECK(step(&ctx, IMU_NEW));
    }
    CHECK(ctx.calibration.ready && !ctx.data.calibration_pending);
    CHECK(near_vec(ctx.data.gyro_dps, zero));

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
    for (unsigned int i = 0; i < TRACKER_GYRO_CALIBRATION_SAMPLES; ++i) {
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

static int test_navigation(void)
{
    unsigned int checks = 0;
    AcquisitionImu ctx;
    const Vec3f zero = {0};
    const Quaternionf identity = {.w = 1.0f};
    init_result = 0;
    now_ticks = 0;
    acquisition_imu_init(&ctx);
    CHECK(ctx.navigation_time_pending && !ctx.nav.valid);

    next_reading = (ImuReading){
        .accel_mps2 = sensor_accel_for((Vec3f){0.0f, 0.0f, TRACKER_GRAVITY_MPS2}),
    };
    for (unsigned int i = 0; i + 1U < TRACKER_GYRO_CALIBRATION_SAMPLES; ++i) {
        CHECK(step(&ctx, IMU_NEW));
        CHECK(!ctx.nav.valid && ctx.navigation_time_pending);
    }

    /* La muestra que termina de calibrar establece el tiempo con dt=0. */
    CHECK(step(&ctx, IMU_NEW));
    CHECK(ctx.nav.valid && !ctx.navigation_time_pending);
    CHECK(near_q(ctx.nav.q, identity));
    CHECK(near_q(ctx.nav.relative_q, identity));
    CHECK(near_vec(ctx.nav.velocity_mps, zero));
    CHECK(near_vec(ctx.nav.position_m, zero));

    /* Aceleracion local X de 1 m/s2: Euler explicito y gravedad compensada. */
    next_reading.accel_mps2 =
        sensor_accel_for((Vec3f){1.0f, 0.0f, TRACKER_GRAVITY_MPS2});
    CHECK(step(&ctx, IMU_NEW)); /* dt=20 ms. */
    CHECK(near_vec(ctx.nav.velocity_mps, (Vec3f){0.02f, 0.0f, 0.0f}));
    CHECK(near_vec(ctx.nav.position_m, zero));
    CHECK(!ctx.moving); /* moving no bloquea ni reinicia la navegacion. */

    CHECK(step_ticks(&ctx, IMU_NEW, 400)); /* dt=40 ms. */
    CHECK(near_vec(ctx.nav.velocity_mps, (Vec3f){0.06f, 0.0f, 0.0f}));
    CHECK(near_vec(ctx.nav.position_m, (Vec3f){0.0008f, 0.0f, 0.0f}));
    CHECK(step_ticks(&ctx, IMU_NEW, 500)); /* 50 ms exactos se integran. */
    CHECK(near_vec(ctx.nav.velocity_mps, (Vec3f){0.11f, 0.0f, 0.0f}));
    CHECK(near_vec(ctx.nav.position_m, (Vec3f){0.0038f, 0.0f, 0.0f}));

    NavigationView retained = ctx.nav;
    int64_t previous_ticks = ctx.previous_imu_ticks;
    CHECK(!step(&ctx, IMU_NO_NEW));
    CHECK(ctx.nav.valid && near_navigation_values(ctx.nav, retained));
    CHECK(ctx.previous_imu_ticks == previous_ticks);
    CHECK(step(&ctx, IMU_NEW)); /* 40 ms desde la ultima utilizable. */
    CHECK(near_vec(ctx.nav.velocity_mps, (Vec3f){0.15f, 0.0f, 0.0f}));
    CHECK(near_vec(ctx.nav.position_m, (Vec3f){0.0082f, 0.0f, 0.0f}));

    /* Error: invalidar conservando todo; al retomar no integrar el hueco. */
    retained = ctx.nav;
    CHECK(step(&ctx, -EIO));
    CHECK(!ctx.nav.valid && ctx.navigation_time_pending);
    CHECK(near_navigation_values(ctx.nav, retained));
    CHECK(ctx.previous_imu_ticks == -1);
    CHECK(step_ticks(&ctx, IMU_NEW, 2000));
    CHECK(ctx.nav.valid && !ctx.navigation_time_pending);
    CHECK(near_navigation_values(ctx.nav, retained));
    CHECK(step(&ctx, IMU_NEW));
    CHECK(near_vec(ctx.nav.velocity_mps, (Vec3f){0.17f, 0.0f, 0.0f}));
    CHECK(near_vec(ctx.nav.position_m, (Vec3f){0.0112f, 0.0f, 0.0f}));

    /* Saturacion: mismo tratamiento temporal sin perder v/p acumuladas. */
    retained = ctx.nav;
    next_reading.range_limited = true;
    CHECK(step(&ctx, IMU_NEW));
    CHECK(!ctx.nav.valid && ctx.navigation_time_pending);
    CHECK(near_navigation_values(ctx.nav, retained));
    next_reading.range_limited = false;
    CHECK(step(&ctx, IMU_NEW));
    CHECK(ctx.nav.valid && near_navigation_values(ctx.nav, retained));

    /* Un tick mas de 50 ms rompe continuidad; la muestra actual usa dt=0. */
    CHECK(step_ticks(&ctx, IMU_NEW, 501));
    CHECK(ctx.nav.valid && !ctx.navigation_time_pending);
    CHECK(near_navigation_values(ctx.nav, retained));
    CHECK(step(&ctx, IMU_NEW));
    CHECK(near_vec(ctx.nav.velocity_mps, (Vec3f){0.19f, 0.0f, 0.0f}));
    CHECK(near_vec(ctx.nav.position_m, (Vec3f){0.0146f, 0.0f, 0.0f}));

    /* Rechazo tardio: ni siquiera la orientacion candidata se confirma. */
    retained = ctx.nav;
    next_reading.accel_mps2.x = NAN;
    next_reading.gyro_dps.z = 90.0f;
    CHECK(step(&ctx, IMU_NEW));
    CHECK(!ctx.nav.valid && ctx.navigation_time_pending);
    CHECK(near_navigation_values(ctx.nav, retained));
    CHECK(ctx.previous_imu_ticks == ctx.sample_ticks);

    next_reading.accel_mps2 =
        sensor_accel_for((Vec3f){1.0f, 0.0f, TRACKER_GRAVITY_MPS2});
    next_reading.gyro_dps.z = 0.0f;
    CHECK(step(&ctx, IMU_NEW));
    CHECK(ctx.nav.valid && !ctx.navigation_time_pending);
    CHECK(near_navigation_values(ctx.nav, retained));
    CHECK(step(&ctx, IMU_NEW));
    CHECK(near_vec(ctx.nav.velocity_mps, (Vec3f){0.21f, 0.0f, 0.0f}));
    CHECK(near_vec(ctx.nav.position_m, (Vec3f){0.0184f, 0.0f, 0.0f}));
    CHECK(near_q(ctx.initial_q, identity));

    printf("PASS: %u acquisition IMU navigation checks\n", checks);
    return 0;
}

static int test_navigation_relative_orientation(void)
{
    unsigned int checks = 0;
    AcquisitionImu ctx;
    const Quaternionf identity = {.w = 1.0f};
    init_result = 0;
    now_ticks = 0;
    acquisition_imu_init(&ctx);

    /* Arranque inclinado: q0 es distinto de identidad y el sesgo gyro es 2. */
    next_reading = (ImuReading){
        .accel_mps2 = sensor_accel_for((Vec3f){
            0.0f, 0.6f * TRACKER_GRAVITY_MPS2, 0.8f * TRACKER_GRAVITY_MPS2,
        }),
        .gyro_dps = {0.0f, 0.0f, 2.0f},
    };
    for (unsigned int i = 0; i < TRACKER_GYRO_CALIBRATION_SAMPLES; ++i) {
        CHECK(step(&ctx, IMU_NEW));
    }
    Quaternionf initial_q = ctx.initial_q;
    CHECK(ctx.nav.valid && !ctx.navigation_time_pending);
    CHECK(!near_q(initial_q, identity));
    CHECK(near_q(ctx.nav.q, initial_q));
    CHECK(near_q(ctx.nav.relative_q, identity));

    /* 92 - sesgo 2 = 90 grados/s sobre Z del sensor durante un segundo. */
    next_reading.gyro_dps.z = 92.0f;
    for (int i = 0; i < 50; ++i) {
        CHECK(step(&ctx, IMU_NEW));
        CHECK(ctx.nav.valid);
    }
    const float expected_component = sqrtf(0.5f);
    CHECK(fabsf(ctx.nav.relative_q.w - expected_component) < 1e-4f);
    CHECK(fabsf(ctx.nav.relative_q.x) < 1e-4f);
    CHECK(fabsf(ctx.nav.relative_q.y) < 1e-4f);
    CHECK(fabsf(ctx.nav.relative_q.z - expected_component) < 1e-4f);
    CHECK(!near_q(ctx.nav.q, ctx.nav.relative_q));
    CHECK(near_q(ctx.initial_q, initial_q));
    Quaternionf q = ctx.nav.q;
    CHECK(fabsf(q.w*q.w + q.x*q.x + q.y*q.y + q.z*q.z - 1.0f) < 1e-5f);

    NavigationView retained = ctx.nav;
    CHECK(!step(&ctx, IMU_NO_NEW));
    CHECK(ctx.nav.valid && near_navigation_values(ctx.nav, retained));
    CHECK(step(&ctx, -EIO));
    CHECK(!ctx.nav.valid && near_navigation_values(ctx.nav, retained));
    CHECK(near_q(ctx.initial_q, initial_q));
    next_reading.gyro_dps.z = 2.0f;
    CHECK(step_ticks(&ctx, IMU_NEW, 1000));
    CHECK(ctx.nav.valid && near_navigation_values(ctx.nav, retained));
    CHECK(near_q(ctx.initial_q, initial_q));
    CHECK(step_ticks(&ctx, IMU_NEW, 501));
    CHECK(ctx.nav.valid && near_navigation_values(ctx.nav, retained));
    CHECK(near_q(ctx.initial_q, initial_q));

    printf("PASS: %u acquisition IMU relative orientation checks\n", checks);
    return 0;
}

int main(void)
{
    if (test_calibration() != 0 || test_motion() != 0 || test_navigation() != 0) {
        return 1;
    }
    return test_navigation_relative_orientation();
}
