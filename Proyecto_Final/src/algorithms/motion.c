#include "motion.h"

#include <math.h>

#include "algorithm_config.h"

void motion_init(MotionDetector *ctx)
{
    *ctx = (MotionDetector){
        .moving = false,
        .pending_since_ms = -1,
    };
}

/* Función para interrumpir la cuenta cuando falla una lectura */
void motion_interrupt(MotionDetector *ctx)
{
    ctx->pending_since_ms = -1;
}

bool motion_update(MotionDetector *ctx, float accel_norm, float gyro_norm, int64_t now_ms)
{
    float accel_delta = fabsf(accel_norm - TRACKER_GRAVITY_MPS2);
    bool condition;
    int64_t confirmation_ms;

    if (ctx->moving) {
        /* Para confirmar reposo deben cumplirse ambas condiciones. */
        condition =
            accel_delta < TRACKER_REST_ACCEL_DELTA_MPS2 &&
            gyro_norm < TRACKER_REST_GYRO_MAX_DPS;

        confirmation_ms = TRACKER_REST_CONFIRM_MS;
    } else {
        /* Para confirmar movimiento basta cualquiera de las dos. */
        condition =
            accel_delta > TRACKER_MOTION_ACCEL_DELTA_MPS2 ||
            gyro_norm > TRACKER_MOTION_GYRO_MIN_DPS;

        confirmation_ms = TRACKER_MOTION_CONFIRM_MS;
    }

    if (!condition) {
        motion_interrupt(ctx);
        return ctx->moving;
    }

    if (ctx->pending_since_ms < 0) {
        /* Primera muestra de una posible transicion. */
        ctx->pending_since_ms = now_ms;
    /* Tiempo que lleva manteniéndose la condición */
    } else if (now_ms - ctx->pending_since_ms >= confirmation_ms) {
        ctx->moving = !ctx->moving;
        motion_interrupt(ctx);
    }

    return ctx->moving;
}