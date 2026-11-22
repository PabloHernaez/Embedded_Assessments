#include "acquisition_light.h"

#include <zephyr/kernel.h>

#include "../drivers/light.h"

#define LIGHT_PERIOD_MS 2000

/**
 * Prepara el contexto de luz para que la primera actualización intente leer el ADC.
 *
 * Entradas: ctx apunta a memoria válida y escribible; no necesita contenido previo.
 * Retorno: ninguno (void).
 * Modifica: sobrescribe *ctx con configured=false, datos inválidos, last_good_ms=-1,
 *           siguiente lectura vencida y marcas de captura sin referencia (-1).
 *           La configuración del ADC se realiza después, en acquisition_light_update.
 */
void acquisition_light_init(AcquisitionLight *ctx)
{
    *ctx = (AcquisitionLight){
        .data = {
            .quality = {
                .valid = false,
                .last_good_ms = -1,
            },
        },
        .capture_begin_ticks = -1,
        .sample_ticks = -1,
    };
}

/**
 * Atiende la cadencia de luz, configura el ADC si hace falta e intenta obtener
 * un porcentaje mediante las referencias de calibración del montaje.
 *
 * Entradas: ctx válido e inicializado, usado solo por el hilo de adquisición.
 *           Lee el reloj; la conversion a porcentaje la hace el driver.
 * Retorno: false si aún no vence la lectura, sin modificar ctx; true si vence y
 *          se atiende un intento, aunque falle la configuración, lectura o conversión.
 * Modifica: programa next_read_ticks a 2 s del instante actual, actualiza configured
 *           según el ADC y fecha el intento. En éxito reemplaza data y su calidad;
 *           en fallo, dentro de data solo invalida la calidad, reteniendo
 *           porcentaje, clipped y last_good_ms. Puede configurar y leer hardware;
 *           no publica ni toma mutex.
 */
bool acquisition_light_update(AcquisitionLight *ctx)
{
    int64_t now_ticks = k_uptime_ticks();

    if (now_ticks < ctx->next_read_ticks) {
        return false;
    }

    ctx->next_read_ticks =
        now_ticks + k_ms_to_ticks_ceil64(LIGHT_PERIOD_MS);

    int ret = 0;

    if (!ctx->configured) {
        ret = light_init();
        ctx->configured = (ret == 0);
    }

    int16_t raw = 0;

    ctx->capture_begin_ticks = k_uptime_ticks();

    if (ret == 0) {
        ret = light_read_raw(&raw);

        if (ret < 0) {
            ctx->configured = false;
        }
    }

    ctx->sample_ticks = k_uptime_ticks();

    if (ret == 0) {
        const LightReading reading = light_from_raw(raw);

        ctx->data = (LightData){
            .light_pct = reading.light_pct,
            .quality = {
                .valid = true,
                .last_good_ms =
                    k_ticks_to_ms_floor64(ctx->sample_ticks),
            },
            .clipped = reading.clipped,
        };
    } else {
        ctx->data.quality.valid = false;
    }

    return true;
}
