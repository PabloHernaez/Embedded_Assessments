#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/task_wdt/task_wdt.h>
#include <zephyr/sys/printk.h>

#include "acquisition_api.h"

/* Identificador creado por K_THREAD_DEFINE, para medir su pila desde el ensayo. */
extern const k_tid_t acquisition_tid;

static const struct device *const console = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));

/* El servicio registra su canal de task_wdt; en produccion lo inicia main.c. */
static int watchdog_init(void)
{
    return task_wdt_init(NULL);
}
SYS_INIT(watchdog_init, APPLICATION, 0);

/* Edad en ms de la ultima medida utilizable, o -1 si nunca la hubo. */
static int64_t sample_age(int64_t now_ms, const SampleQuality *quality)
{
    return quality->last_good_ms < 0 ? -1 : now_ms - quality->last_good_ms;
}

/* Una linea por familia: IMU, navegacion y sensores lentos. */
static void snapshot_print(const AcquisitionSnapshot *s, int64_t now_ms,
                           int64_t report_dt_ms)
{
    printk("D4 t_ms=%lld dt_ms=%lld cal=%d moving=%d "
           "imu_ok=%d imu_age=%lld sat=%d nav_ok=%d\n",
           (long long)now_ms,
           (long long)report_dt_ms, s->imu.calibration_pending, s->moving,
           s->imu.quality.valid, (long long)sample_age(now_ms, &s->imu.quality),
           s->imu.range_limited, s->nav.valid);
    printk("IMU a=%.5f,%.5f,%.5f gyro=%.5f,%.5f,%.5f\n",
           (double)s->imu.accel_mps2.x, (double)s->imu.accel_mps2.y,
           (double)s->imu.accel_mps2.z, (double)s->imu.gyro_dps.x,
           (double)s->imu.gyro_dps.y, (double)s->imu.gyro_dps.z);
    printk("NAV q=%.6f,%.6f,%.6f,%.6f rel=%.6f,%.6f,%.6f,%.6f "
           "v=%.5f,%.5f,%.5f p=%.5f,%.5f,%.5f\n",
           (double)s->nav.q.w, (double)s->nav.q.x,
           (double)s->nav.q.y, (double)s->nav.q.z,
           (double)s->nav.relative_q.w, (double)s->nav.relative_q.x,
           (double)s->nav.relative_q.y, (double)s->nav.relative_q.z,
           (double)s->nav.velocity_mps.x, (double)s->nav.velocity_mps.y,
           (double)s->nav.velocity_mps.z, (double)s->nav.position_m.x,
           (double)s->nav.position_m.y, (double)s->nav.position_m.z);
    printk("SLOW light_ok=%d light_age=%lld light_pct=%.4f clipped=%d "
           "env_ok=%d env_age=%lld temp_c=%.4f rh_pct=%.4f stats_ready=%d\n",
           s->light.quality.valid, (long long)sample_age(now_ms, &s->light.quality),
           (double)s->light.light_pct, s->light.clipped,
           s->environment.quality.valid,
           (long long)sample_age(now_ms, &s->environment.quality),
           (double)s->environment.temperature_c,
           (double)s->environment.humidity_pct, s->stats.ready);
}

/* Imprime una ventana completa de 30 s con las cinco metricas. */
static void statistics_print(const StatsResult *result)
{
    static const char *const names[STATS_METRIC_COUNT] = {
        "light_pct", "temp_c", "rh_pct", "accel_mps2", "gyro_dps",
    };

    printk("WINDOW begin_ms=%lld end_ms=%lld\n",
           (long long)result->begin_ms, (long long)result->end_ms);
    for (int i = 0; i < STATS_METRIC_COUNT; ++i) {
        const MetricStats *m = &result->metric[i];
        if (m->count == 0U) {
            printk("STAT metric=%s n=0 value=N/D\n", names[i]);
        } else {
            printk("STAT metric=%s n=%u mean=%.6f min=%.6f max=%.6f\n",
                   names[i], (unsigned)m->count, (double)m->mean,
                   (double)m->min, (double)m->max);
        }
    }
}

/* Bytes de pila sin usar en adquisicion y main. */
static void stacks_print(void)
{
    size_t acquisition_free = 0;
    size_t main_free = 0;
    int acquisition_ret = k_thread_stack_space_get(acquisition_tid, &acquisition_free);
    int main_ret = k_thread_stack_space_get(k_current_get(), &main_free);

    printk("STACK acquisition_ret=%d acquisition_free=%u main_ret=%d main_free=%u\n",
           acquisition_ret, (unsigned)acquisition_free,
           main_ret, (unsigned)main_free);
}

/* Observador del servicio real: un reporte por segundo; b/e abren y cierran la sesion. */
int main(void)
{
    if (!device_is_ready(console)) {
        return 0;
    }

    printk("D4_START service=real commands=b:begin,e:end report_ms=1000\n");
    acquisition_stats_begin();
    printk("SESSION begin t_ms=%lld\n", (long long)k_uptime_get());

    int64_t next_report_ms = 0;
    int64_t previous_report_ms = 0;
    int64_t last_window_end_ms = -1;

    while (true) {
        unsigned char command;
        if (uart_poll_in(console, &command) == 0) {
            if (command == 'b' || command == 'e') {
                if (command == 'b') {
                    acquisition_stats_begin();
                } else {
                    acquisition_stats_end();
                }
                last_window_end_ms = -1;
                printk("SESSION %s t_ms=%lld\n", command == 'b' ? "begin" : "end",
                       (long long)k_uptime_get());
            }
        }

        AcquisitionSnapshot snapshot;
        acquisition_snapshot_get(&snapshot);
        int64_t now_ms = k_uptime_get();

        if (snapshot.stats.ready && snapshot.stats.end_ms != last_window_end_ms) {
            statistics_print(&snapshot.stats);
            last_window_end_ms = snapshot.stats.end_ms;
        }
        if (now_ms >= next_report_ms) {
            snapshot_print(&snapshot, now_ms, now_ms - previous_report_ms);
            stacks_print();
            previous_report_ms = now_ms;
            next_report_ms = now_ms + 1000;
        }
        k_msleep(100);
    }
}
