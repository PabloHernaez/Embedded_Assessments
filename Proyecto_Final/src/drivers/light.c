#include "light.h"

#include <errno.h>
#include <stdbool.h>

#include <zephyr/devicetree.h>
#include <zephyr/drivers/adc.h>

/** 
 *   ADC1, canal 5, PB1
 */
static const struct adc_dt_spec light_adc =
    ADC_DT_SPEC_GET(DT_PATH(zephyr_user));

static int16_t raw_sample;

/**
 * sequence es la petición de conversión. Se almacena en .buffer 
 */
static struct adc_sequence sequence = {
    .buffer = &raw_sample,
    .buffer_size = sizeof(raw_sample),
};

static bool configured;

/**
 *  Configuración de canal y preparación de petición con resolución cuadrada en overlay
 *  Básicamente inicialización del ADC
 */
int light_init(void)
{
    int ret;

    configured = false;

    if (!adc_is_ready_dt(&light_adc)) {
        return -ENODEV;
    }

    ret = adc_channel_setup_dt(&light_adc);
    if (ret < 0) {
        return ret;
    }

    ret = adc_sequence_init_dt(&light_adc, &sequence);
    if (ret < 0) {
        return ret;
    }

    configured = true;
    return 0;
}

/**
 *  Realiza una conversión y entrega el resultado solo cuando es correcto.
 *  Se espera un valor entre 0-4095
 */

int light_read_raw(int16_t *out)
{
    int ret;
    // configured está como variable estática privada del driver de luz
    if (!configured) {
        return -EAGAIN;
    }

    ret = adc_read_dt(&light_adc, &sequence);
    if (ret < 0) {
        return ret;
    }

    *out = raw_sample;
    return 0;
}

/**
 *  Operación de conversión del valor crudo de tensión de 0-4095 a un rango de 0-100%
 */
int light_raw_to_percent(int16_t raw, int16_t raw_dark, int16_t raw_bright, LightReading *out)
{
    // Chequeo de que el valor no esté fuera de límites para los tres valores
    if (raw < 0 || raw > 4095 ||
        raw_dark < 0 || raw_dark > 4095 ||
        raw_bright < 0 || raw_bright > 4095 ||
        raw_dark == raw_bright) {
        return -EINVAL;
    }

    //Construcción del objeto que se va a transmitir con los datos
    LightReading result = {
        .light_pct = 100.0f * (float)(raw - raw_dark) / (float)(raw_bright - raw_dark),
        .clipped = false,
    };

    // Corrección de objeto de lectura en caso que la operación resulte fuera de rangos
    if (result.light_pct < 0.0f) {
        result.light_pct = 0.0f;
        result.clipped = true;
    } else if (result.light_pct > 100.0f) {
        result.light_pct = 100.0f;
        result.clipped = true;
    }

    // Manda a esa memoria out el LightReading result
    *out = result;
    return 0;
}