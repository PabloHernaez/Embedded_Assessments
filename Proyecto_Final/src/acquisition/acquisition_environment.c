#include "acquisition_environment.h"

#include <zephyr/kernel.h>

#include "../drivers/environment.h"

#define ENVIRONMENT_PERIOD_MS 2000

void acquisition_environment_init(AcquisitionEnvironment *ctx)
{
    *ctx = (AcquisitionEnvironment){
        .data.quality = { .valid = false, .last_good_ms = -1 },
        .ready_ticks = -1,
        .sample_ticks = -1,
    };
}

/* Fallo: calidad invalida, se conserva el ultimo par y se revalida el sensor. */
static bool environment_failed(AcquisitionEnvironment *ctx)
{
    ctx->configured = false;
    ctx->ready_ticks = -1;
    ctx->sample_ticks = k_uptime_ticks();
    ctx->data.quality.valid = false;
    return true;
}

bool acquisition_environment_update(AcquisitionEnvironment *ctx, bool bus_error)
{
    const int64_t now = k_uptime_ticks();

    if (bus_error) {
        /* Tras un pulso en SDA el Si7021 puede quedar sin responder: revalidarlo. */
        ctx->configured = false;
        return ctx->ready_ticks >= 0 ? environment_failed(ctx) : false;
    }

    if (ctx->ready_ticks >= 0) {
        if (now < ctx->ready_ticks) {
            return false;
        }

        EnvironmentReading reading;
        if (environment_read(&reading) != 0) {
            return environment_failed(ctx);
        }
        ctx->ready_ticks = -1;
        ctx->sample_ticks = k_uptime_ticks();
        ctx->data = (EnvironmentData){
            .temperature_c = reading.temperature_c,
            .humidity_pct = reading.humidity_pct,
            .quality = { .valid = true, .last_good_ms = k_ticks_to_ms_floor64(ctx->sample_ticks) },
        };
        return true;
    }

    if (now < ctx->next_read_ticks) {
        return false;
    }
    ctx->next_read_ticks = now + k_ms_to_ticks_ceil64(ENVIRONMENT_PERIOD_MS);

    int ret = ctx->configured ? 0 : environment_init();
    if (ret == 0) {
        ret = environment_start();
    }
    if (ret != 0) {
        return environment_failed(ctx);
    }
    ctx->configured = true;
    /* Contar los 40 ms desde que termino F5; se recoge en la primera vuelta posterior. */
    ctx->ready_ticks = k_uptime_ticks() + k_ms_to_ticks_ceil64(ENVIRONMENT_CONVERSION_WAIT_MS);
    return false;
}
