#include "acquisition_light.h"

#include <zephyr/kernel.h>

#include "../drivers/light.h"

#define LIGHT_PERIOD_MS 2000

void acquisition_light_init(AcquisitionLight *ctx)
{
    *ctx = (AcquisitionLight){
        .data.quality = { .valid = false, .last_good_ms = -1 },
    };
}

bool acquisition_light_update(AcquisitionLight *ctx)
{
    const int64_t now = k_uptime_ticks();
    int16_t raw;

    if (now < ctx->next_read_ticks) {
        return false;
    }
    ctx->next_read_ticks = now + k_ms_to_ticks_ceil64(LIGHT_PERIOD_MS);

    int ret = ctx->configured ? 0 : light_init();
    if (ret == 0) {
        ret = light_read_raw(&raw);
    }
    ctx->configured = ret == 0;

    if (ret != 0) {
        ctx->data.quality.valid = false; /* se conserva el ultimo valor y su fecha */
        return true;
    }

    const LightReading reading = light_from_raw(raw);
    ctx->data = (LightData){
        .light_pct = reading.light_pct,
        .clipped = reading.clipped,
        .quality = { .valid = true, .last_good_ms = k_uptime_get() },
    };
    return true;
}
