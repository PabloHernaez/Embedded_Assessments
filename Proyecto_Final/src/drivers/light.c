#include "light.h"

#include <errno.h>

#include <zephyr/devicetree.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/sys/util.h>

/*
 * Referencias medidas el 02/11/2026: KY-018 a 3,3 V sin pull-down adicional;
 * oscuridad y linterna del movil al maximo a 5 cm. Repetir si cambia el montaje.
 * Registro: tests/board/light/results/Calibracion_LDR_20261102.md.
 * El divisor esta invertido: mas luz da un codigo menor.
 */
#define LIGHT_RAW_DARK   4062
#define LIGHT_RAW_BRIGHT 128

BUILD_ASSERT(LIGHT_RAW_DARK != LIGHT_RAW_BRIGHT, "Referencias LDR iguales");

/* ADC1 canal 5 (PB1), 12 bits y referencia interna: ver el overlay. */
static const struct adc_dt_spec light_adc = ADC_DT_SPEC_GET(DT_PATH(zephyr_user));

int light_init(void)
{
    if (!adc_is_ready_dt(&light_adc)) {
        return -ENODEV;
    }
    return adc_channel_setup_dt(&light_adc);
}

int light_read_raw(int16_t *raw)
{
    int16_t sample;
    struct adc_sequence sequence = {
        .buffer = &sample,
        .buffer_size = sizeof(sample),
    };

    int ret = adc_sequence_init_dt(&light_adc, &sequence);
    if (ret == 0) {
        ret = adc_read_dt(&light_adc, &sequence);
    }
    if (ret == 0) {
        *raw = sample;
    }
    return ret;
}

LightReading light_from_raw(int16_t raw)
{
    const float pct = 100.0f * (float)(raw - LIGHT_RAW_DARK) /
                      (float)(LIGHT_RAW_BRIGHT - LIGHT_RAW_DARK);

    return (LightReading){
        .light_pct = CLAMP(pct, 0.0f, 100.0f),
        .clipped = pct < 0.0f || pct > 100.0f,
    };
}
