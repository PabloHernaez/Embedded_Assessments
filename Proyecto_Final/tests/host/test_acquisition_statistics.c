#include <errno.h>
#include <math.h>
#include <stdio.h>

/* Incluir el coordinador real permite activar sus pasos privados sin hilo RTOS. */
#include "../../src/acquisition/acquisition.c"
#include "imu.h"
#include "environment.h"

#define CHECK(expr) do { ++checks; if (!(expr)) { \
    printf("FAIL line %d: %s\n", __LINE__, #expr); return 1; } } while (0)

static int64_t now_ticks;
static int64_t read_delay_ticks;
static int read_result;
static ImuReading next_reading;
static bool restart_session_in_read;
static bool end_session_in_read;
static bool hardware_under_lock;
static bool mutex_fault;
static int configuration_result;
static bool configuration_matches = true;
static unsigned int configuration_count;
static int environment_read_result;
static unsigned int environment_start_count;
static unsigned int environment_read_count;
static uint32_t timer_expirations = 1;

int k_mutex_lock(struct k_mutex *mutex, int timeout)
{
    (void)timeout;
    ++mutex->depth;
    return 0;
}

int k_mutex_unlock(struct k_mutex *mutex)
{
    if (mutex->depth == 0) {
        mutex_fault = true;
    } else {
        --mutex->depth;
    }
    return 0;
}

uint32_t k_timer_status_sync(struct k_timer *timer)
{
    (void)timer;
    return timer_expirations;
}

void k_timer_start(struct k_timer *timer, int delay, int period)
{
    (void)timer;
    (void)delay;
    (void)period;
}

int32_t k_sleep(int timeout)
{
    (void)timeout;
    return 0;
}

int64_t k_uptime_ticks(void)
{
    return now_ticks;
}

uint32_t k_cycle_get_32(void)
{
    return (uint32_t)(now_ticks * 4800); /* 48 MHz / 10000 ticks/s. */
}

int64_t k_ticks_to_ms_floor64(int64_t ticks)
{
    return ticks / 10;
}

int64_t k_ms_to_ticks_floor64(int64_t ms)
{
    return ms * 10;
}

int64_t k_ms_to_ticks_ceil64(int64_t ms) { return ms * 10; }

int imu_check_configuration(bool *matches)
{
    hardware_under_lock |= acquisition_lock.depth != 0;
    ++configuration_count;
    if (configuration_result == 0) { *matches = configuration_matches; }
    return configuration_result;
}
int imu_bus_is_blocked(bool *blocked) { *blocked = false; return 0; }
int imu_recover_bus(void) { return 0; }

int imu_init(void)
{
    hardware_under_lock |= acquisition_lock.depth != 0;
    return 0;
}

int imu_read(ImuReading *out)
{
    hardware_under_lock |= acquisition_lock.depth != 0;
    now_ticks += read_delay_ticks;

    /* Simular que main cambia la sesion mientras la lectura esta en curso. */
    if (restart_session_in_read) {
        acquisition_stats_end();
        acquisition_stats_begin();
        restart_session_in_read = false;
    } else if (end_session_in_read) {
        acquisition_stats_end();
        end_session_in_read = false;
    }

    if (read_result == IMU_NEW) {
        *out = next_reading;
    }
    return read_result;
}

/* Luz queda fuera del ensayo; ambiente participa en los casos de bus compartido. */
void acquisition_light_init(AcquisitionLight *ctx) { (void)ctx; }
bool acquisition_light_update(AcquisitionLight *ctx) { (void)ctx; return false; }
int environment_init(void) { return 0; }
int environment_start(void) { ++environment_start_count; return 0; }
int environment_read(EnvironmentReading *out)
{
    ++environment_read_count;
    if (environment_read_result == 0) { *out = (EnvironmentReading){25.0f, 50.0f}; }
    return environment_read_result;
}

static void imu_step(int result, int64_t elapsed_ticks)
{
    read_result = result;
    now_ticks += elapsed_ticks;
    acquisition_imu_step();
}

static Vec3f sensor_accel_for(Vec3f corrected)
{
    return (Vec3f){
        corrected.x / 0.9964143f + 0.12409f,
        corrected.y / 0.9966736f - 0.06164f,
        corrected.z / 0.9891509f - 0.05113f,
    };
}

static void set_sample(Vec3f accel, Vec3f corrected_gyro)
{
    next_reading = (ImuReading){
        .accel_mps2 = sensor_accel_for(accel),
        .gyro_dps = {
            corrected_gyro.x + 1.0f,
            corrected_gyro.y + 2.0f,
            corrected_gyro.z + 3.0f,
        },
    };
}

static bool near(float actual, float expected)
{
    return fabsf(actual - expected) < 1e-5f;
}

int main(void)
{
    unsigned int checks = 0;
    AcquisitionSnapshot snapshot;
    CHECK(acquisition_tid != NULL); /* La macro conserva la entrada, sin ejecutarla. */
    acquisition_imu_init(&imu_context);
    acquisition_stats_begin(); /* Una sesion puede comenzar en uptime cero. */
    set_sample((Vec3f){0.0f, 0.0f, TRACKER_GRAVITY_MPS2}, (Vec3f){0});

    for (unsigned int i = 0; i + 1U < TRACKER_GYRO_CALIBRATION_SAMPLES; ++i) {
        imu_step(IMU_NEW, 200);
        CHECK(stats.metric[STATS_ACCEL_NORM].count == 0);
        CHECK(stats.metric[STATS_GYRO_NORM].count == 0);
    }
    imu_step(IMU_NEW, 200);
    CHECK(!imu_context.data.calibration_pending);
    CHECK(stats.metric[STATS_ACCEL_NORM].count == 1);
    CHECK(stats.metric[STATS_GYRO_NORM].count == 1);
    CHECK(near(imu_context.accel_norm_mps2, TRACKER_GRAVITY_MPS2));
    CHECK(near(imu_context.gyro_norm_dps, 0.0f));

    /* NO_NEW y getters repetidos conservan valores sin volver a acumular. */
    imu_step(IMU_NO_NEW, 200);
    for (int i = 0; i < 10; ++i) {
        acquisition_snapshot_get(&snapshot);
        CHECK(snapshot.imu.quality.valid && !snapshot.stats.ready);
    }
    CHECK(stats.metric[STATS_ACCEL_NORM].count == 1);
    CHECK(stats.metric[STATS_GYRO_NORM].count == 1);
    imu_step(-EIO, 200);
    next_reading.range_limited = true;
    imu_step(IMU_NEW, 200);
    CHECK(stats.metric[STATS_ACCEL_NORM].count == 1);
    CHECK(stats.metric[STATS_GYRO_NORM].count == 1);
    next_reading.range_limited = false;
    imu_step(IMU_NEW, 200);
    imu_step(IMU_NEW, 200);
    CHECK(stats.metric[STATS_ACCEL_NORM].count == 3);
    CHECK(stats.metric[STATS_GYRO_NORM].count == 3);

    /* Las dos magnitudes deben ser finitas antes de entrar en los acumuladores. */
    next_reading.gyro_dps.x = NAN;
    imu_step(IMU_NEW, 200);
    CHECK(stats.metric[STATS_ACCEL_NORM].count == 3);
    CHECK(stats.metric[STATS_GYRO_NORM].count == 3);
    set_sample((Vec3f){0.0f, 0.0f, INFINITY}, (Vec3f){0});
    imu_step(IMU_NEW, 200);
    CHECK(stats.metric[STATS_ACCEL_NORM].count == 3);
    CHECK(stats.metric[STATS_GYRO_NORM].count == 3);

    /* Sin sesion se sigue publicando la IMU, sin acumulacion. */
    acquisition_stats_end();
    set_sample((Vec3f){0.0f, 0.0f, TRACKER_GRAVITY_MPS2}, (Vec3f){0});
    imu_step(IMU_NEW, 200);
    acquisition_snapshot_get(&snapshot);
    CHECK(snapshot.imu.quality.valid && !snapshot.stats.ready);
    CHECK(!stats.active && stats.metric[STATS_ACCEL_NORM].count == 0);

    /* Serie conocida: repetidos NEW cuentan por separado; gyro ya sin sesgo. */
    acquisition_stats_begin();
    int64_t session_begin_ticks = now_ticks;
    set_sample((Vec3f){3.0f, 4.0f, 12.0f}, (Vec3f){3.0f, 4.0f, 0.0f});
    imu_step(IMU_NEW, 200);
    CHECK(near(imu_context.accel_norm_mps2, 13.0f));
    CHECK(near(imu_context.gyro_norm_dps, 5.0f));
    imu_step(IMU_NEW, 200);
    set_sample((Vec3f){0.0f, 0.0f, 5.0f}, (Vec3f){0.0f, 0.0f, 12.0f});
    imu_step(IMU_NEW, 200);
    CHECK(!imu_context.moving);
    CHECK(stats.metric[STATS_ACCEL_NORM].count == 3);
    CHECK(stats.metric[STATS_GYRO_NORM].count == 3);

    /* Capturar antes del limite y terminar exactamente en el: va a la siguiente. */
    int64_t window_end_ticks = stats.window_end_ticks;
    now_ticks = window_end_ticks - 10;
    acquisition_statistics_step();
    read_delay_ticks = 10;
    set_sample((Vec3f){0.0f, 0.0f, 10.0f}, (Vec3f){0.0f, 0.0f, 1.0f});
    imu_step(IMU_NEW, 0);
    read_delay_ticks = 0;
    acquisition_snapshot_get(&snapshot);
    CHECK(snapshot.stats.ready);
    CHECK(snapshot.stats.begin_ms == session_begin_ticks / 10);
    CHECK(snapshot.stats.end_ms == window_end_ticks / 10);
    MetricStats accel = snapshot.stats.metric[STATS_ACCEL_NORM];
    MetricStats gyro = snapshot.stats.metric[STATS_GYRO_NORM];
    CHECK(accel.count == 3 && gyro.count == 3);
    CHECK(near(accel.mean, 31.0f / 3.0f));
    CHECK(near(accel.min, 5.0f) && near(accel.max, 13.0f));
    CHECK(near(gyro.mean, 22.0f / 3.0f));
    CHECK(near(gyro.min, 5.0f) && near(gyro.max, 12.0f));
    CHECK(stats.metric[STATS_ACCEL_NORM].count == 1);
    CHECK(stats.metric[STATS_GYRO_NORM].count == 1);
    CHECK(snapshot.stats.metric[STATS_TEMP].count == 0);

    /* El cierre sigue funcionando sin muestras nuevas y puede publicar count=0. */
    now_ticks = stats.window_end_ticks;
    imu_step(IMU_NO_NEW, 0);
    acquisition_statistics_step();
    acquisition_snapshot_get(&snapshot);
    CHECK(snapshot.stats.ready);
    CHECK(snapshot.stats.metric[STATS_ACCEL_NORM].count == 1);
    CHECK(near(snapshot.stats.metric[STATS_ACCEL_NORM].mean, 10.0f));
    CHECK(near(snapshot.stats.metric[STATS_GYRO_NORM].mean, 1.0f));
    now_ticks = stats.window_end_ticks;
    acquisition_statistics_step();
    acquisition_snapshot_get(&snapshot);
    CHECK(snapshot.stats.ready);
    CHECK(snapshot.stats.metric[STATS_ACCEL_NORM].count == 0);
    CHECK(snapshot.stats.metric[STATS_GYRO_NORM].count == 0);
    now_ticks = stats.window_end_ticks + 300000;
    acquisition_statistics_step();
    acquisition_snapshot_get(&snapshot);
    CHECK(!snapshot.stats.ready && stats.window_begin_ticks == now_ticks);

    /* Salir/entrar en NORMAL durante una lectura excluye esa captura antigua. */
    read_delay_ticks = 20;
    restart_session_in_read = true;
    imu_step(IMU_NEW, 200);
    CHECK(imu_context.capture_begin_ticks < stats.session_begin_ticks);
    CHECK(imu_context.sample_ticks == stats.session_begin_ticks);
    CHECK(stats.metric[STATS_ACCEL_NORM].count == 0);
    CHECK(stats.metric[STATS_GYRO_NORM].count == 0);
    read_delay_ticks = 0;
    imu_step(IMU_NEW, 200);
    CHECK(stats.metric[STATS_ACCEL_NORM].count == 1);
    CHECK(stats.metric[STATS_GYRO_NORM].count == 1);

    end_session_in_read = true;
    imu_step(IMU_NEW, 200);
    acquisition_snapshot_get(&snapshot);
    CHECK(!stats.active && !snapshot.stats.ready);
    CHECK(stats.metric[STATS_ACCEL_NORM].count == 0);
    CHECK(acquisition_lock.depth == 0 && !mutex_fault && !hardware_under_lock);

    /* Un fallo tardio de configuracion cancela ENV sin una recogida adicional. */
    acquisition_environment_init(&environment_context);
    acquisition_environment_step();
    CHECK(environment_context.ready_ticks >= 0 && environment_start_count == 1);
    configuration_result = -EIO;
    acquisition_imu_configuration_step();
    CHECK(configuration_count == 1 && imu_context.review_pending);
    CHECK(imu_context.bus_interrupted && environment_context.bus_interrupted);
    CHECK(environment_context.ready_ticks == -1 && environment_read_count == 0);
    acquisition_snapshot_get(&snapshot);
    CHECK(!snapshot.imu.quality.valid);

    configuration_result = 0;
    now_ticks = imu_context.next_review_ticks;
    imu_step(IMU_NEW, 0); /* Revision, sin leer una muestra en esta vuelta. */
    imu_step(IMU_NEW, 200);
    now_ticks = environment_context.next_read_ticks;
    acquisition_environment_step();
    CHECK(environment_context.ready_ticks >= 0 && environment_start_count == 2);
    now_ticks = environment_context.ready_ticks;
    environment_read_result = -EBADMSG;
    acquisition_environment_step();
    CHECK(environment_context.bus_interrupted);
    imu_context.next_configuration_check_ticks = now_ticks;
    unsigned int checks_before_environment_error = configuration_count;
    acquisition_imu_configuration_step();
    CHECK(configuration_count == checks_before_environment_error);

    /* Revision/init son cambios de calidad, no muestras para estadisticas. */
    acquisition_stats_begin();
    for (int i = 0; i < 3; ++i) { imu_step(-EIO, 200); }
    CHECK(imu_context.review_pending);
    imu_step(IMU_NEW, 200); /* Solo revision de registros. */
    CHECK(stats.metric[STATS_ACCEL_NORM].count == 0);
    CHECK(stats.metric[STATS_GYRO_NORM].count == 0);
    imu_step(IMU_NEW, 200);
    CHECK(stats.metric[STATS_ACCEL_NORM].count == 1);
    CHECK(stats.metric[STATS_GYRO_NORM].count == 1);
    CHECK(acquisition_lock.depth == 0 && !mutex_fault && !hardware_under_lock);

    /* Diagnostico temporal: limites estrictos de 10/20 ms y categorias separadas. */
    now_ticks = 1000000;
    uint32_t begin_cycles = k_cycle_get_32();
    now_ticks += 99;
    acquisition_duration_record(begin_cycles, false);
    CHECK(timing.normal_count == 1 && timing.normal_max_cycles == 99U * 4800U);
    CHECK(timing.normal_ge_10_ms == 0 && timing.normal_ge_20_ms == 0);
    begin_cycles = k_cycle_get_32();
    now_ticks += 100;
    acquisition_duration_record(begin_cycles, false);
    CHECK(timing.normal_ge_10_ms == 1 && timing.normal_ge_20_ms == 0);
    begin_cycles = k_cycle_get_32();
    now_ticks += 200;
    acquisition_duration_record(begin_cycles, false);
    CHECK(timing.normal_count == 3 && timing.normal_max_cycles == 200U * 4800U);
    CHECK(timing.normal_ge_10_ms == 2 && timing.normal_ge_20_ms == 1);
    CHECK(timing.normal_max_at_ms == now_ticks / 10);

    begin_cycles = k_cycle_get_32();
    now_ticks += 1100;
    acquisition_duration_record(begin_cycles, true);
    CHECK(timing.maintenance_count == 1 && timing.maintenance_ge_20_ms == 1);
    CHECK(timing.maintenance_max_cycles == 1100U * 4800U);
    CHECK(timing.normal_count == 3 && timing.normal_max_cycles == 200U * 4800U);

    /* Una medida corta que cruza el desborde conserva la diferencia correcta. */
    now_ticks = UINT32_MAX / 4800U;
    begin_cycles = k_cycle_get_32();
    now_ticks += 2;
    acquisition_duration_record(begin_cycles, false);
    CHECK(timing.normal_count == 4 && timing.normal_max_cycles == 200U * 4800U);
    CHECK(timing.normal_ge_10_ms == 2 && timing.normal_ge_20_ms == 1);

    timer_expirations = 4;
    acquisition_wait_for_period();
    CHECK(timing.expired_periods == 4 && timing.missed_periods == 3);
    CHECK(timing.normal_count == 4); /* Esperar no fabrica trabajo de adquisicion. */

    printf("PASS: %u acquisition statistics/bus/timing checks\n", checks);
    return 0;
}
