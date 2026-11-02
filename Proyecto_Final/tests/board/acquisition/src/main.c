#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "acquisition_api.h"

/* Identificador creado por K_THREAD_DEFINE, para medir su pila desde el ensayo. */
extern const k_tid_t acquisition_tid;

static const struct device *const console = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));

/**
 * Calcula la edad de la ultima medida utilizable de una familia.
 * Entradas: now_ms posterior al snapshot; quality apunta a su calidad valida.
 * Retorno: edad en ms, o -1 si nunca hubo una medida utilizable.
 * Modifica: nada; no cambia calidad ni lee hardware.
 */
static int64_t sample_age(int64_t now_ms, const SampleQuality *quality)
{
    return quality->last_good_ms < 0 ? -1 : now_ms - quality->last_good_ms;
}

/**
 * Muestra las medidas publicadas, sus edades, navegacion y avance del hilo.
 * Entradas: snapshot coherente, ahora en ms y diferencias desde el reporte previo.
 * Retorno: ninguno (void).
 * Modifica: escribe por consola; conserva el snapshot y no accede a sensores.
 */
static void snapshot_print(const AcquisitionSnapshot *s, int64_t now_ms,
                           int64_t report_dt_ms, uint32_t progress_delta)
{
    printk("D4 t_ms=%lld progress=%u dp=%u dt_ms=%lld cal=%d moving=%d "
           "imu_ok=%d imu_age=%lld sat=%d nav_ok=%d\n",
           (long long)now_ms, (unsigned)s->progress, (unsigned)progress_delta,
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

/**
 * Muestra una ventana completa con las cinco metricas originales de D4.
 * Entradas: result apunta a un resultado ready del snapshot.
 * Retorno: ninguno (void).
 * Modifica: escribe limites, conteos y resumen por consola; no recalcula estadisticas.
 */
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

/**
 * Consulta el espacio de pila que permanece sin usar en los dos hilos del ensayo.
 * Entradas: ninguna explicita; utiliza acquisition_tid y el hilo main actual.
 * Retorno: ninguno (void).
 * Modifica: consulta las marcas de pila de Zephyr y escribe bytes libres o error.
 *           No cambia prioridades ni la pila solicitada por adquisicion (2048 B).
 */
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

/**
 * Observa el servicio real y permite abrir/cerrar sesiones estadisticas por UART.
 * Entradas: ninguna explicita; recibe 'b' (begin) o 'e' (end) por LPUART1.
 * Retorno: 0 si la consola no esta disponible; no retorna durante el ensayo.
 * Modifica: abre una sesion inicial, consulta snapshots y emite reportes de 1 s;
 *           b/e llaman a las APIs publicas y reinician la deteccion de ventanas.
 *           No lee sensores directamente ni procesa navegacion o movimiento.
 */
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
    uint32_t previous_progress = 0;

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
            snapshot_print(&snapshot, now_ms, now_ms - previous_report_ms,
                           snapshot.progress - previous_progress);
            stacks_print();
            previous_progress = snapshot.progress;
            previous_report_ms = now_ms;
            next_report_ms = now_ms + 1000;
        }
        k_msleep(100);
    }
}
