#include <stdio.h>
#include "motion.h"
#include "algorithm_config.h"

#define CHECK(expr) do { ++checks; if (!(expr)) { \
    printf("FAIL line %d: %s\n", __LINE__, #expr); return 1; } } while (0)

int main(void)
{
    unsigned int checks = 0;
    MotionDetector ctx;
    motion_init(&ctx);

    /* Ordinary rest preserves the initial idle result. */
    for (int64_t t = 0; t <= 3000; t += 20) {
        CHECK(!motion_update(&ctx, TRACKER_GRAVITY_MPS2, 0.0f, t));
    }

    /* A gyro signal alone confirms at 300 ms, never at 280 ms. */
    motion_init(&ctx);
    for (int64_t t = 0; t < 300; t += 20) {
        CHECK(!motion_update(&ctx, TRACKER_GRAVITY_MPS2, 9.0f, t));
    }
    CHECK(motion_update(&ctx, TRACKER_GRAVITY_MPS2, 9.0f, 300));

    /* Rest requires 2000 ms from its first qualifying sample. */
    for (int64_t t = 320; t < 2320; t += 20) {
        CHECK(motion_update(&ctx, TRACKER_GRAVITY_MPS2, 0.0f, t));
    }
    CHECK(!motion_update(&ctx, TRACKER_GRAVITY_MPS2, 0.0f, 2320));

    /* Acceleration alone is sufficient, including a norm below gravity. */
    motion_init(&ctx);
    for (int64_t t = 0; t < 300; t += 20) {
        CHECK(!motion_update(&ctx, TRACKER_GRAVITY_MPS2 - 1.0f, 0.0f, t));
    }
    CHECK(motion_update(&ctx, TRACKER_GRAVITY_MPS2 - 1.0f, 0.0f, 300));

    /* Either failed rest test preserves moving; this is the hysteresis band. */
    for (int64_t t = 320; t <= 3320; t += 20) {
        CHECK(motion_update(&ctx, TRACKER_GRAVITY_MPS2, 4.0f, t));
    }
    for (int64_t t = 3340; t <= 6340; t += 20) {
        CHECK(motion_update(&ctx, TRACKER_GRAVITY_MPS2 + 0.5f, 0.0f, t));
    }

    /* Exact gyro boundaries are excluded by the strict comparisons. */
    for (int64_t t = 6360; t <= 9360; t += 20) {
        CHECK(motion_update(&ctx, TRACKER_GRAVITY_MPS2, 3.0f, t));
    }
    motion_init(&ctx);
    for (int64_t t = 0; t <= 400; t += 20) {
        CHECK(!motion_update(&ctx, TRACKER_GRAVITY_MPS2, 8.0f, t));
    }

    /* Interrupting the condition restarts the complete confirmation period. */
    motion_init(&ctx);
    for (int64_t t = 0; t < 300; t += 20) {
        CHECK(!motion_update(&ctx, TRACKER_GRAVITY_MPS2, 9.0f, t));
    }
    CHECK(!motion_update(&ctx, TRACKER_GRAVITY_MPS2, 0.0f, 300));
    for (int64_t t = 320; t < 620; t += 20) {
        CHECK(!motion_update(&ctx, TRACKER_GRAVITY_MPS2, 9.0f, t));
    }
    CHECK(motion_update(&ctx, TRACKER_GRAVITY_MPS2, 9.0f, 620));

    /* Explicit interruption cancels timing while preserving confirmed moving. */
    CHECK(motion_update(&ctx, TRACKER_GRAVITY_MPS2, 0.0f, 640));
    motion_interrupt(&ctx);
    CHECK(ctx.moving);
    for (int64_t t = 800; t < 2800; t += 20) {
        CHECK(motion_update(&ctx, TRACKER_GRAVITY_MPS2, 0.0f, t));
    }
    CHECK(!motion_update(&ctx, TRACKER_GRAVITY_MPS2, 0.0f, 2800));

    /* NO_NEW is skipped by the owner; 40 ms between useful samples is allowed. */
    motion_init(&ctx);
    for (int64_t t = 0; t < 300; t += 40) {
        CHECK(!motion_update(&ctx, TRACKER_GRAVITY_MPS2, 9.0f, t));
    }
    CHECK(motion_update(&ctx, TRACKER_GRAVITY_MPS2, 9.0f, 320));

    /* The owner interrupts for errors, saturation or a gap longer than 50 ms. */
    motion_init(&ctx);
    CHECK(!motion_update(&ctx, TRACKER_GRAVITY_MPS2, 9.0f, 0));
    CHECK(!motion_update(&ctx, TRACKER_GRAVITY_MPS2, 9.0f, 20));
    motion_interrupt(&ctx);
    CHECK(!ctx.moving);
    for (int64_t t = 400; t < 700; t += 20) {
        CHECK(!motion_update(&ctx, TRACKER_GRAVITY_MPS2, 9.0f, t));
    }
    CHECK(motion_update(&ctx, TRACKER_GRAVITY_MPS2, 9.0f, 700));

    printf("PASS: %u deterministic motion checks\n", checks);
    return 0;
}
