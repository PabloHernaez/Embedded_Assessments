#include <errno.h>
#include <math.h>
#include <stdio.h>

#include "acquisition_imu.h"
#include "imu.h"
#include <zephyr/kernel.h>

#define CHECK(expr) do { ++checks; if (!(expr)) { \
    printf("FAIL line %d: %s\n", __LINE__, #expr); return 1; } } while (0)

static int64_t now_ticks;
static int init_result, read_result, check_result, probe_result, recover_result;
static bool configuration_matches, lines_blocked;
static unsigned int init_count, read_count, check_count, recover_count;
static int64_t previous_recovery_ticks, recovery_interval_ticks;
static ImuReading reading;

int64_t k_uptime_ticks(void) { return now_ticks; }
int64_t k_ticks_to_ms_floor64(int64_t ticks) { return ticks / 10; }
int64_t k_ms_to_ticks_floor64(int64_t ms) { return ms * 10; }
int64_t k_ms_to_ticks_ceil64(int64_t ms) { return ms * 10; }

int imu_init(void) { ++init_count; return init_result; }
int imu_read(ImuReading *out)
{
    ++read_count;
    if (read_result == IMU_NEW) { *out = reading; }
    return read_result;
}
int imu_check_configuration(bool *matches)
{
    ++check_count;
    if (check_result == 0) { *matches = configuration_matches; }
    return check_result;
}
int imu_bus_is_blocked(bool *blocked)
{
    if (probe_result == 0) { *blocked = lines_blocked; }
    return probe_result;
}
int imu_recover_bus(void)
{
    if (recover_count > 0) { recovery_interval_ticks = now_ticks - previous_recovery_ticks; }
    previous_recovery_ticks = now_ticks;
    ++recover_count;
    return recover_result;
}

static bool step(AcquisitionImu *ctx, int result, int64_t ticks)
{
    now_ticks += ticks;
    read_result = result;
    return acquisition_imu_update(ctx);
}

static bool same_vec(Vec3f a, Vec3f b)
{
    return fabsf(a.x-b.x) < 1e-5f && fabsf(a.y-b.y) < 1e-5f && fabsf(a.z-b.z) < 1e-5f;
}
static bool same_q(Quaternionf a, Quaternionf b)
{
    return fabsf(a.w-b.w) < 1e-5f && fabsf(a.x-b.x) < 1e-5f &&
           fabsf(a.y-b.y) < 1e-5f && fabsf(a.z-b.z) < 1e-5f;
}
static bool calculation_preserved(const AcquisitionImu *a, const AcquisitionImu *b)
{
    return a->calibration.ready && same_vec(a->calibration.bias_dps, b->calibration.bias_dps) &&
           same_q(a->initial_q, b->initial_q) && same_q(a->nav.q, b->nav.q) &&
           same_q(a->nav.relative_q, b->nav.relative_q) &&
           same_vec(a->nav.velocity_mps, b->nav.velocity_mps) &&
           same_vec(a->nav.position_m, b->nav.position_m);
}

/* Preparar datos reales del algoritmo y una trayectoria previa no nula. */
static bool prepare(AcquisitionImu *ctx)
{
    now_ticks = 0;
    init_result = read_result = check_result = probe_result = recover_result = 0;
    init_count = read_count = check_count = recover_count = 0;
    configuration_matches = true;
    lines_blocked = false;
    acquisition_imu_init(ctx);
    reading = (ImuReading){
        .accel_mps2 = {
            0.12409f,
            0.6f * TRACKER_GRAVITY_MPS2 / 0.9966736f - 0.06164f,
            0.8f * TRACKER_GRAVITY_MPS2 / 0.9891509f - 0.05113f,
        },
        .gyro_dps = {1.0f, 2.0f, 3.0f},
    };
    for (unsigned int i = 0; i < TRACKER_GYRO_CALIBRATION_SAMPLES; ++i) {
        if (!step(ctx, IMU_NEW, 200)) { return false; }
    }
    reading.gyro_dps.z = 33.0f;
    if (!step(ctx, IMU_NEW, 200)) { return false; }
    ctx->nav.velocity_mps = (Vec3f){1.0f, 2.0f, 3.0f};
    ctx->nav.position_m = (Vec3f){4.0f, 5.0f, 6.0f};
    return ctx->calibration.ready && ctx->nav.valid;
}

static int test_transient(void)
{
    unsigned int checks = 0;
    AcquisitionImu ctx;
    CHECK(prepare(&ctx));
    AcquisitionImu before = ctx;
    int64_t begin = now_ticks;
    unsigned int reads_before = read_count;

    CHECK(step(&ctx, -EIO, 200)); /* 20 ms. */
    CHECK(step(&ctx, -EIO, 200)); /* 40 ms. */
    CHECK(step(&ctx, -EIO, 200)); /* 60 ms. */
    CHECK(ctx.consecutive_errors == 3 && ctx.review_pending);
    CHECK(check_count == 0 && recover_count == 0 && init_count == 1);
    CHECK(!ctx.data.quality.valid && ctx.data.quality.last_good_ms == before.data.quality.last_good_ms);
    CHECK(calculation_preserved(&ctx, &before));
    CHECK(step(&ctx, IMU_NEW, 200)); /* 80 ms: solo revision. */
    CHECK(check_count == 1 && read_count == reads_before + 3);
    CHECK(!ctx.review_pending && ctx.configured && !ctx.data.quality.valid);
    CHECK(step(&ctx, IMU_NEW, 200)); /* 100 ms: primera muestra, dt=0. */
    CHECK(now_ticks - begin == 1000);
    CHECK(ctx.nav.valid && ctx.data.quality.valid && ctx.consecutive_errors == 0);
    CHECK(calculation_preserved(&ctx, &before));
    CHECK(init_count == 1);

    CHECK(step(&ctx, -EIO, 200));
    CHECK(!step(&ctx, IMU_NO_NEW, 200));
    reading.range_limited = true;
    CHECK(step(&ctx, IMU_NEW, 200));
    CHECK(ctx.consecutive_errors == 1 && !ctx.review_pending);
    reading.range_limited = false;
    CHECK(step(&ctx, IMU_NEW, 200));
    CHECK(ctx.consecutive_errors == 0);
    printf("PASS: %u transient recovery checks\n", checks);
    return 0;
}

static int test_review_and_init_spacing(void)
{
    unsigned int checks = 0;
    AcquisitionImu ctx;
    CHECK(prepare(&ctx));
    AcquisitionImu before = ctx;
    for (int i = 0; i < 3; ++i) { CHECK(step(&ctx, -EIO, 200)); }
    check_result = -EIO;
    CHECK(step(&ctx, IMU_NEW, 200));
    CHECK(check_count == 1 && ctx.next_review_ticks == now_ticks + 10000);
    CHECK(!step(&ctx, IMU_NEW, 9999));
    CHECK(check_count == 1);
    check_result = 0;
    configuration_matches = false;
    CHECK(step(&ctx, IMU_NEW, 1));
    CHECK(!ctx.configured && !ctx.review_pending);
    CHECK(step(&ctx, IMU_NEW, 200)); /* Init, sin muestra ni contexto puesto a cero. */
    CHECK(init_count == 2 && ctx.configured && !ctx.data.quality.valid);
    CHECK(calculation_preserved(&ctx, &before));
    CHECK(step(&ctx, IMU_NEW, 200));
    CHECK(ctx.nav.valid && calculation_preserved(&ctx, &before));

    /* Otra perdida de configuracion cerca del init anterior respeta su segundo. */
    before = ctx;
    for (int i = 0; i < 3; ++i) { CHECK(step(&ctx, -EIO, 200)); }
    CHECK(step(&ctx, IMU_NEW, 200));
    CHECK(!ctx.configured);
    now_ticks = ctx.last_init_ticks + 9999;
    CHECK(!step(&ctx, IMU_NEW, 0));
    CHECK(init_count == 2);
    init_result = -ENODEV;
    CHECK(step(&ctx, IMU_NEW, 1));
    CHECK(init_count == 3 && !ctx.configured);
    CHECK(!step(&ctx, IMU_NEW, 9999));
    init_result = 0;
    CHECK(step(&ctx, IMU_NEW, 1));
    CHECK(init_count == 4 && ctx.configured);
    CHECK(step(&ctx, IMU_NEW, 200));
    CHECK(ctx.nav.valid && calculation_preserved(&ctx, &before));
    printf("PASS: %u review/init spacing checks\n", checks);
    return 0;
}

static int test_blocked_bus(void)
{
    unsigned int checks = 0;
    AcquisitionImu ctx;
    CHECK(prepare(&ctx));
    AcquisitionImu before = ctx;
    lines_blocked = true;
    CHECK(step(&ctx, -EIO, 200));
    CHECK(recover_count == 1 && ctx.review_pending && ctx.bus_interrupted);
    CHECK(ctx.next_review_ticks == now_ticks + 10000);
    CHECK(!step(&ctx, IMU_NEW, 9999));
    CHECK(recover_count == 1 && !ctx.bus_interrupted);
    check_result = -EIO;
    recover_result = -EBUSY;
    CHECK(step(&ctx, IMU_NEW, 1));
    CHECK(recover_count == 2 && recovery_interval_ticks >= 10000);
    CHECK(ctx.review_pending && !ctx.data.quality.valid);
    CHECK(calculation_preserved(&ctx, &before));
    CHECK(!step(&ctx, IMU_NEW, 9999));
    check_result = 0;
    configuration_matches = true;
    lines_blocked = false;
    CHECK(step(&ctx, IMU_NEW, 1));
    CHECK(recover_count == 2 && !ctx.review_pending);
    CHECK(step(&ctx, IMU_NEW, 200));
    CHECK(ctx.nav.valid && calculation_preserved(&ctx, &before));

    /* Timeout explicito con lineas libres: espaciar revision, sin pulsos GPIO. */
    CHECK(step(&ctx, -ETIMEDOUT, 200));
    CHECK(ctx.review_pending && ctx.next_review_ticks == now_ticks + 10000);
    CHECK(recover_count == 2);
    CHECK(prepare(&ctx));
    lines_blocked = true;
    probe_result = -EIO; /* No hay evidencia de nivel si no se pudo observar. */
    CHECK(step(&ctx, -EIO, 200));
    CHECK(recover_count == 0 && !ctx.review_pending);
    printf("PASS: %u blocked bus checks\n", checks);
    return 0;
}

static int test_periodic_configuration(void)
{
    unsigned int checks = 0;
    AcquisitionImu ctx;
    CHECK(prepare(&ctx));
    AcquisitionImu before = ctx;
    CHECK(!acquisition_imu_check_configuration(&ctx, true));
    CHECK(check_count == 1);
    now_ticks = ctx.next_configuration_check_ticks - 1;
    CHECK(!acquisition_imu_check_configuration(&ctx, true));
    CHECK(check_count == 1);
    ++now_ticks;
    CHECK(!acquisition_imu_check_configuration(&ctx, false));
    CHECK(check_count == 1);
    configuration_matches = false; /* Reset silencioso sin necesidad de un NEW. */
    CHECK(!step(&ctx, IMU_NO_NEW, 0));
    CHECK(acquisition_imu_check_configuration(&ctx, true));
    CHECK(!ctx.configured && !ctx.nav.valid && !ctx.data.quality.valid);
    CHECK(calculation_preserved(&ctx, &before));
    CHECK(step(&ctx, IMU_NEW, 200));
    CHECK(init_count == 2 && ctx.configured);
    CHECK(step(&ctx, IMU_NEW, 200));
    CHECK(calculation_preserved(&ctx, &before));

    now_ticks = ctx.next_configuration_check_ticks;
    check_result = -EIO;
    CHECK(acquisition_imu_check_configuration(&ctx, true));
    CHECK(ctx.bus_interrupted && ctx.review_pending);
    unsigned int checks_before = check_count;
    CHECK(!acquisition_imu_check_configuration(&ctx, true));
    CHECK(check_count == checks_before);
    now_ticks = ctx.next_review_ticks;
    check_result = 0;
    configuration_matches = true;
    CHECK(step(&ctx, IMU_NEW, 0));
    CHECK(step(&ctx, IMU_NEW, 200));
    CHECK(ctx.nav.valid && calculation_preserved(&ctx, &before));
    printf("PASS: %u periodic configuration checks\n", checks);
    return 0;
}

int main(void)
{
    if (test_transient() || test_review_and_init_spacing() || test_blocked_bus()) { return 1; }
    return test_periodic_configuration();
}
