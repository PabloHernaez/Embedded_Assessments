#include "acquisition_environment.h"

#include <zephyr/kernel.h>

#include "../drivers/environment.h"

#define ENVIRONMENT_PERIOD_MS 2000

/**
 * Prepara el contexto ambiental; la configuración del hardware queda diferida.
 *
 * Entradas: ctx válido y escribible; no necesita contenido previo.
 * Retorno: ninguno (void).
 * Modifica: sobrescribe *ctx con configured=false, datos inválidos, last_good_ms=-1,
 *           primera lectura vencida y marcas de captura/conversión sin referencia.
 *           No accede al bus I2C.
 */
void acquisition_environment_init(AcquisitionEnvironment *ctx)
{
    *ctx = (AcquisitionEnvironment){
        .data = {
            .quality = {
                .valid = false,
                .last_good_ms = -1,
            },
        },
        .ready_ticks = -1,
        .capture_begin_ticks = -1,
        .sample_ticks = -1,
    };
}

/**
 * Configura el Si7021 si hace falta e inicia la siguiente conversión con F5.
 *
 * Entradas: ctx válido, sin conversión pendiente y con su cadencia vencida.
 * Retorno: false si inicia correctamente y queda esperando; true si falla
 *          y hay calidad inválida que publicar.
 * Modifica: configured, capture_begin_ticks, next_read_ticks y ready_ticks de *ctx.
 *           El vencimiento de conversión se cuenta desde el final de F5 con
 *           ceil(40 ms) + 1 tick. En fallo invalida calidad, fecha el intento
 *           en sample_ticks y deja pendiente reconfigurar en el próximo vencimiento.
 *           Conserva el último par y su last_good_ms; usa el driver sin esperar.
 */
static bool environment_begin(AcquisitionEnvironment *ctx)
{
    int ret = 0;

    if (!ctx->configured) {
        ret = environment_init();
        ctx->configured = (ret == 0);
    }

    ctx->capture_begin_ticks = k_uptime_ticks();

    if (ret == 0) {
        ret = environment_start();
    }

    int64_t start_end_ticks = k_uptime_ticks();

    ctx->next_read_ticks =
        start_end_ticks + k_ms_to_ticks_ceil64(ENVIRONMENT_PERIOD_MS);

    if (ret != 0) {
        ctx->bus_interrupted = true;
        ctx->configured = false;
        ctx->ready_ticks = -1;
        ctx->sample_ticks = start_end_ticks;
        ctx->data.quality.valid = false;
        return true;
    }

    ctx->ready_ticks = start_end_ticks +
        k_ms_to_ticks_ceil64(ENVIRONMENT_CONVERSION_WAIT_MS) + 1;

    return false;
}

/**
 * Recoge un par ambiental completo después de vencer la conversión.
 *
 * Entradas: ctx válido, con una conversión iniciada y ready_ticks ya alcanzado.
 * Retorno: ninguno (void).
 * Modifica: termina la espera (ready_ticks=-1) y fecha sample_ticks al acabar
 *           environment_read. En éxito reemplaza data: temperatura en °C,
 *           humedad en % y calidad/last_good_ms. En fallo invalida calidad y
 *           configured, conservando valores y última fecha buena para reintentar
 *           configurar en la siguiente cadencia. Conserva capture_begin_ticks,
 *           tomado antes de F5, para identificar la sesión de la medida completa.
 */
static void environment_collect(AcquisitionEnvironment *ctx)
{
    EnvironmentReading reading;
    int ret = environment_read(&reading);

    ctx->sample_ticks = k_uptime_ticks();
    ctx->ready_ticks = -1;

    if (ret == 0) {
        ctx->data = (EnvironmentData){
            .temperature_c = reading.temperature_c,
            .humidity_pct = reading.humidity_pct,
            .quality = {
                .valid = true,
                .last_good_ms = k_ticks_to_ms_floor64(ctx->sample_ticks),
            },
        };
    } else {
        ctx->bus_interrupted = true;
        ctx->configured = false;
        ctx->data.quality.valid = false;
    }
}

/**
 * Atiende la cadencia ambiental o la recogida de una conversión pendiente.
 *
 * Entradas: ctx válido e inicializado, usado por el hilo de adquisición;
 *           bus_interrupted indica un fallo previo o recuperación física, no NO_NEW/saturación.
 * Retorno: true al completar un intento o invalidar calidad; false si espera
 *          cadencia/conversión, inicia correctamente o se omite I2C sin pendiente.
 * Modifica: tiempos, configuración y datos/calidad de *ctx mediante los pasos
 *           de inicio/recogida. Reinicia su bandera bus_interrupted y la activa
 *           ante error propio o bus no permitido. Un fallo previo omite I2C esta vuelta
 *           y deja configured=false para revalidar antes del siguiente F5.
 *           Si había conversión pendiente, la abandona, invalida calidad y pospone
 *           el inicio 2 s; sin pendiente conserva calidad y vencimiento ambiental.
 *           Conserva el par y su última fecha buena. No publica ni toma mutex.
 *           Las transacciones I2C son síncronas; no espera los 40 ms.
 */
bool acquisition_environment_update(AcquisitionEnvironment *ctx, bool bus_interrupted)
{
    ctx->bus_interrupted = false;
    int64_t now_ticks = k_uptime_ticks();

    if (bus_interrupted) {
        ctx->bus_interrupted = true;
        ctx->configured = false;
        if (ctx->ready_ticks < 0) {
            return false;
        }
        ctx->ready_ticks = -1;
        ctx->next_read_ticks =
            now_ticks + k_ms_to_ticks_ceil64(ENVIRONMENT_PERIOD_MS);
        ctx->data.quality.valid = false;
        return true;
    }

    if (ctx->ready_ticks >= 0) {
        if (now_ticks < ctx->ready_ticks) {
            return false;
        }

        environment_collect(ctx);
        return true;
    }

    if (now_ticks < ctx->next_read_ticks) {
        return false;
    }

    return environment_begin(ctx);
}
