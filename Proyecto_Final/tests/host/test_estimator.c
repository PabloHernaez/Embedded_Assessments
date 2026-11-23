#include <math.h>
#include <stdio.h>

#include "estimator.h"

#define CHECK(expr) do { ++checks; if (!(expr)) { \
    printf("FAIL line %d: %s\n", __LINE__, #expr); return 1; } } while (0)

static bool near(float a, float b, float tol)
{
    return fabsf(a - b) < tol;
}

static bool near_vec(Vec3f a, Vec3f b, float tol)
{
    return near(a.x, b.x, tol) && near(a.y, b.y, tol) && near(a.z, b.z, tol);
}

static bool near_q(Quaternionf a, Quaternionf b, float tol)
{
    return near(a.w, b.w, tol) && near(a.x, b.x, tol) && near(a.y, b.y, tol) && near(a.z, b.z, tol);
}

static float q_norm(Quaternionf q)
{
    return sqrtf(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z);
}

/* n pasos de dt con la misma muestra. */
static void run(NavigationView *nav, Vec3f g0, Vec3f accel, Vec3f gyro, float dt, int n)
{
    for (int i = 0; i < n; ++i) {
        estimator_update(nav, &g0, &accel, &gyro, dt);
    }
}

int main(void)
{
    unsigned int checks = 0;
    const Vec3f zero = {0};
    const Quaternionf identity = { .w = 1.0f };
    const float s = sqrtf(0.5f);
    NavigationView nav;

    /* Reposo con la placa inclinada: medir g0 y restarlo deja todo a cero. */
    const Vec3f tilted_g0 = {0.62f, 0.04f, 9.88f};
    estimator_init(&nav);
    run(&nav, tilted_g0, tilted_g0, zero, 0.02f, 1000);
    CHECK(near_q(nav.q, identity, 1e-6f));
    CHECK(near_vec(nav.velocity_mps, zero, 1e-6f) && near_vec(nav.position_m, zero, 1e-6f));

    /* dt = 0 (primera muestra o tras un hueco) no cambia nada. */
    run(&nav, tilted_g0, (Vec3f){5.0f, 5.0f, 5.0f}, (Vec3f){90.0f, 0.0f, 0.0f}, 0.0f, 10);
    CHECK(near_q(nav.q, identity, 1e-6f) && near_vec(nav.velocity_mps, zero, 1e-6f));

    /* 90 grados en cada eje (90 grados/s durante 1 s) y norma unitaria. */
    const Quaternionf expected[3] = { {s, s, 0, 0}, {s, 0, s, 0}, {s, 0, 0, s} };
    for (int axis = 0; axis < 3; ++axis) {
        Vec3f gyro = zero;
        (&gyro.x)[axis] = 90.0f;
        estimator_init(&nav);
        run(&nav, zero, zero, gyro, 0.02f, 50);
        CHECK(near_q(nav.q, expected[axis], 2e-3f));
        CHECK(near(q_norm(nav.q), 1.0f, 1e-5f));
        (&gyro.x)[axis] = -90.0f;
        run(&nav, zero, zero, gyro, 0.02f, 50);
        CHECK(near_q(nav.q, identity, 2e-3f)); /* y vuelta */
    }

    /* Giros en ejes del sensor: primero Z y luego X dan q = qz * qx. */
    estimator_init(&nav);
    run(&nav, zero, zero, (Vec3f){0.0f, 0.0f, 90.0f}, 0.02f, 50);
    run(&nav, zero, zero, (Vec3f){90.0f, 0.0f, 0.0f}, 0.02f, 50);
    CHECK(near_q(nav.q, (Quaternionf){0.5f, 0.5f, 0.5f, 0.5f}, 3e-3f));

    /* Girada 90 grados en Z y quieta: el sensor ve g0 en otros ejes y v sigue en 0. */
    const Vec3f level_g0 = {0.0f, 0.0f, 9.81f};
    estimator_init(&nav);
    nav.q = (Quaternionf){s, 0, 0, s};
    run(&nav, level_g0, level_g0, zero, 0.02f, 100); /* Z no cambia al girar en Z */
    CHECK(near_vec(nav.velocity_mps, zero, 1e-5f));
    estimator_init(&nav);
    nav.q = (Quaternionf){s, s, 0, 0};                /* 90 grados en X */
    run(&nav, level_g0, (Vec3f){0.0f, 9.81f, 0.0f}, zero, 0.02f, 100);
    CHECK(near_vec(nav.velocity_mps, zero, 1e-5f));

    /* 1 m/s2 en X durante 1 s a 20 ms: v = 1 y p = 0,49 (Euler explicito, no 0,5). */
    estimator_init(&nav);
    run(&nav, level_g0, (Vec3f){1.0f, 0.0f, 9.81f}, zero, 0.02f, 50);
    CHECK(near_vec(nav.velocity_mps, (Vec3f){1.0f, 0.0f, 0.0f}, 1e-4f));
    CHECK(near_vec(nav.position_m, (Vec3f){0.49f, 0.0f, 0.0f}, 1e-4f));

    /* Subir: aceleracion mayor que g0 hacia arriba da Z positiva. */
    estimator_init(&nav);
    run(&nav, level_g0, (Vec3f){0.0f, 0.0f, 10.81f}, zero, 0.02f, 50);
    CHECK(nav.velocity_mps.z > 0.99f && nav.position_m.z > 0.0f);

    printf("PASS: %u estimator checks\n", checks);
    return 0;
}
