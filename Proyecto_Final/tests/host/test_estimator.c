#include <math.h>
#include <float.h>
#include <stdio.h>
#include "estimator.h"

#define CHECK(expr) do { ++checks; if (!(expr)) { \
    printf("FAIL line %d: %s\n", __LINE__, #expr); return 1; } } while (0)

static float norm_squared(Quaternionf q)
{
    return q.w*q.w + q.x*q.x + q.y*q.y + q.z*q.z;
}

static int same_q(Quaternionf a, Quaternionf b)
{
    return a.w == b.w && a.x == b.x && a.y == b.y && a.z == b.z;
}

int main(void)
{
    unsigned int checks = 0;
    NavigationView nav;
    estimator_init(&nav);
    Quaternionf q = nav.q;
    Vec3f gyro = {0.0f, 0.0f, 0.0f};

    for (int i = 0; i < 1000; ++i) {
        CHECK(estimator_orientation_update(&q, &gyro, 0.02f));
    }
    CHECK(same_q(q, nav.q));

    const float expected = sqrtf(0.5f);
    float max_component_error = 0.0f;

    /* Six independent known rotations: +/- 90 degrees around each axis. */
    for (int axis = 0; axis < 3; ++axis) {
        for (int sign = -1; sign <= 1; sign += 2) {
            q = nav.q;
            gyro = (Vec3f){0.0f, 0.0f, 0.0f};
            if (axis == 0) gyro.x = (float)sign * 90.0f;
            if (axis == 1) gyro.y = (float)sign * 90.0f;
            if (axis == 2) gyro.z = (float)sign * 90.0f;

            for (int i = 0; i < 50; ++i) {
                CHECK(estimator_orientation_update(&q, &gyro, 0.02f));
                CHECK(fabsf(norm_squared(q) - 1.0f) < 1e-6f);
            }

            const float components[3] = {q.x, q.y, q.z};
            CHECK(fabsf(q.w - expected) < 1e-4f);
            for (int j = 0; j < 3; ++j) {
                const float reference = j == axis ? (float)sign * expected : 0.0f;
                const float error = fabsf(components[j] - reference);
                if (error > max_component_error) max_component_error = error;
                CHECK(error < 1e-4f);
            }
        }
    }

    /* The same one-second turn using valid 40 ms and 50 ms intervals. */
    const float intervals[2] = {0.04f, 0.05f};
    const int counts[2] = {25, 20};
    gyro = (Vec3f){0.0f, 0.0f, 90.0f};
    for (int j = 0; j < 2; ++j) {
        q = nav.q;
        for (int i = 0; i < counts[j]; ++i) {
            CHECK(estimator_orientation_update(&q, &gyro, intervals[j]));
        }
        CHECK(fabsf(q.w - expected) < 5e-4f);
        CHECK(fabsf(q.z - expected) < 5e-4f);
        CHECK(fabsf(norm_squared(q) - 1.0f) < 1e-6f);
    }

    /*
     * Start at +90 degrees around local Z, then turn about SENSOR X.
     * Hamilton qZ*qX yields [0.5, 0.5, 0.5, 0.5], including positive Y.
     */
    q = (Quaternionf){expected, 0.0f, 0.0f, expected};
    gyro = (Vec3f){90.0f, 0.0f, 0.0f};
    for (int i = 0; i < 50; ++i) {
        CHECK(estimator_orientation_update(&q, &gyro, 0.02f));
    }
    CHECK(fabsf(q.w - 0.5f) < 1e-4f);
    CHECK(fabsf(q.x - 0.5f) < 1e-4f);
    CHECK(fabsf(q.y - 0.5f) < 1e-4f);
    CHECK(fabsf(q.z - 0.5f) < 1e-4f);

    /* dt=0 creates no rotation. */
    q = nav.q;
    CHECK(estimator_orientation_update(&q, &gyro, 0.0f));
    CHECK(same_q(q, nav.q));

    /* Rejected intervals retain the previous orientation exactly. */
    q = (Quaternionf){expected, 0.0f, 0.0f, expected};
    const Quaternionf before = q;
    const float invalid_dt[] = {-0.01f, 0.06f, NAN, INFINITY};
    for (int i = 0; i < 4; ++i) {
        CHECK(!estimator_orientation_update(&q, &gyro, invalid_dt[i]));
        CHECK(same_q(q, before));
    }

    /* Invalid inputs and overflowing candidates must not replace q. */
    gyro = (Vec3f){NAN, 0.0f, 0.0f};
    CHECK(!estimator_orientation_update(&q, &gyro, 0.02f));
    CHECK(same_q(q, before));
    gyro = (Vec3f){0.0f, INFINITY, 0.0f};
    CHECK(!estimator_orientation_update(&q, &gyro, 0.02f));
    CHECK(same_q(q, before));

    q = nav.q;
    gyro = (Vec3f){FLT_MAX, FLT_MAX, FLT_MAX};
    CHECK(!estimator_orientation_update(&q, &gyro, 0.02f));
    CHECK(same_q(q, nav.q));

    q = (Quaternionf){0.0f, 0.0f, 0.0f, 0.0f};
    gyro = (Vec3f){0.0f, 0.0f, 0.0f};
    CHECK(!estimator_orientation_update(&q, &gyro, 0.02f));
    CHECK(norm_squared(q) == 0.0f);

    printf("PASS: %u deterministic orientation checks; max axis component error %.8f\n",
           checks, (double)max_component_error);
    return 0;
}
