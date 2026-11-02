#include "acquisition_imu.h"

#include <math.h>
#include <errno.h>
#include <zephyr/kernel.h>

#include "../drivers/imu.h"
#include "../algorithms/estimator.h"

#define IMU_REVIEW_ERROR_COUNT 3U
#define IMU_RECOVERY_PERIOD_MS 1000
#define IMU_CONFIGURATION_PERIOD_MS 2000

/**
 * Calcula la magnitud euclídea de un vector de tres componentes.
 *
 * Entradas: value, vector recibido por valor; sus tres componentes deben
 *           estar expresadas en la misma unidad.
 * Retorno: sqrt(x² + y² + z²), en la misma unidad que las componentes.
 * Modifica: nada fuera de la función; conserva el argumento del llamador.
 */
static float vector_norm(Vec3f value)
{
    return sqrtf(value.x * value.x +
                 value.y * value.y +
                 value.z * value.z);
}

/**
 * Aplica las correcciones existentes de offset y escala del acelerómetro.
 *
 * Entradas: measured, aceleración medida en ejes del sensor y en m/s²,
 *           incluida gravedad. Se recibe por valor.
 * Retorno: un Vec3f corregido, en los mismos ejes y unidades, aún con gravedad.
 * Modifica: nada fuera de la función; conserva el vector del llamador.
 */
static Vec3f accel_correct(Vec3f measured)
{
    return (Vec3f){
        .x = (measured.x - 0.12409f) * 0.9964143f,
        .y = (measured.y + 0.06164f) * 0.9966736f,
        .z = (measured.z + 0.05113f) * 0.9891509f,
    };
}

/**
 * Prepara una secuencia de calibración desde cero al arrancar o repetirla.
 *
 * Entradas: ctx, puntero válido al contexto privado IMU. El llamador decide
 *           cuándo reiniciar; esta función también borraría un sesgo terminado.
 * Retorno: ninguno (void).
 * Modifica: pone a cero ctx->calibration, incluidos sesgo, cuenta y ready,
 *           y ctx->calibration_accel_sum. Conserva nav, initial_q y data.
 */
static void calibration_reset(AcquisitionImu *ctx)
{
    gyro_calibration_init(&ctx->calibration);
    ctx->calibration_accel_sum = (Vec3f){0};
}

/**
 * Usa la aceleración media de la calibración para preparar la inclinación
 * y fijar la referencia inicial de orientación.
 *
 * Entradas: ctx válido, con calibración ready, count mayor que cero y la suma
 *           de aceleración corregida de esas mismas muestras aceptadas.
 * Retorno: ninguno (void).
 * Modifica: si la media se acepta, actualiza q, relative_q y valid de ctx->nav
 *           y copia q a ctx->initial_q; conserva velocidad y posición.
 *           Si se rechaza, reinicia calibración y suma para repetir la secuencia.
 *           El indicador data.calibration_pending lo actualiza el llamador.
 */
static void calibration_finish(AcquisitionImu *ctx)
{
    float divisor = (float)ctx->calibration.count;

    Vec3f accel_mean = {
        .x = ctx->calibration_accel_sum.x / divisor,
        .y = ctx->calibration_accel_sum.y / divisor,
        .z = ctx->calibration_accel_sum.z / divisor,
    };

    if (!estimator_set_initial_tilt(&ctx->nav, &accel_mean)) {
        calibration_reset(ctx);
        return;
    }

    ctx->initial_q = ctx->nav.q;
}

/**
 * Procesa una muestra utilizable para completar la calibración inicial.
 *
 * Entradas: ctx válido; accel apunta a aceleración corregida con gravedad
 *           en m/s²; gyro apunta a giro SIN descontar sesgo, en grados/s.
 *           Ambos vectores están en ejes del sensor y sus punteros son válidos.
 * Retorno: ninguno (void).
 * Modifica: actualiza ctx->calibration y la suma de aceleración de las muestras
 *           aceptadas. Una muestra fuera de los umbrales reinicia la secuencia.
 *           Al completarla, calibration_finish prepara nav/initial_q o la reinicia.
 *           Si ya estaba ready, conserva todo. No escribe en accel ni gyro.
 */
static void calibration_step(AcquisitionImu *ctx, const Vec3f *accel, const Vec3f *gyro)
{
    if (ctx->calibration.ready) {
        return;
    }

    gyro_calibration_update(&ctx->calibration, gyro, vector_norm(*accel), vector_norm(*gyro));

    /* La media de inclinacion usa las mismas muestras que el sesgo gyro. */
    if (ctx->calibration.count == 0) {
        ctx->calibration_accel_sum = (Vec3f){0};
        return;
    }

    ctx->calibration_accel_sum.x += accel->x;
    ctx->calibration_accel_sum.y += accel->y;
    ctx->calibration_accel_sum.z += accel->z;

    if (ctx->calibration.ready) {
        calibration_finish(ctx);
    }
}

/**
 * Evalúa las magnitudes de la muestra actual para confirmar movimiento o reposo.
 *
 * Entradas: ctx válido y calibrado, con accel_norm_mps2 (con gravedad),
 *           gyro_norm_dps (sin sesgo) y data.quality.last_good_ms de la muestra
 *           nueva utilizable. inertial_step debe haber calculado estas magnitudes
 *           y atendido la continuidad temporal.
 * Retorno: ninguno (void).
 * Modifica: ctx->motion (estado y comienzo de una transición pendiente)
 *           y ctx->moving. Conserva las medidas y la navegación.
 */
static void motion_step(AcquisitionImu *ctx)
{
    ctx->moving = motion_update(&ctx->motion,
                                ctx->accel_norm_mps2,
                                ctx->gyro_norm_dps,
                                ctx->data.quality.last_good_ms);
}

/**
 * Entrega la muestra corregida al estimador y gestiona la retoma temporal.
 *
 * Entradas: ctx válido y calibrado, con aceleración corregida con gravedad
 *           (data.accel_mps2, m/s²), giro sin sesgo (data.gyro_dps, grados/s),
 *           nav preparado e initial_q fijo. dt_s es el intervalo en segundos,
 *           normalmente de 0 a 0,050.
 *           Si navigation_time_pending es true, usa dt=0 en esta llamada.
 * Retorno: ninguno (void).
 * Modifica: si se acepta el candidato, actualiza ctx->nav completo y deja
 *           navigation_time_pending=false. Si se rechaza, conserva q, relative_q,
 *           velocidad y posición, marca nav.valid=false y deja pendiente la retoma.
 *           Conserva initial_q y data; el cambio local de dt_s no afecta al llamador.
 */
static void navigation_step(AcquisitionImu *ctx, float dt_s)
{
    /* Retomar el tiempo sin integrar el tramo desconocido. q0 se conserva. */
    if (ctx->navigation_time_pending) {
        dt_s = 0.0f;
    }

    if (!estimator_update(&ctx->nav, &ctx->initial_q,
                          &ctx->data.accel_mps2,
                          &ctx->data.gyro_dps, dt_s)) {
        ctx->navigation_time_pending = true;
        return;
    }

    ctx->navigation_time_pending = false;
}

/**
 * Coordina movimiento y navegación con un único intervalo entre muestras.
 *
 * Entradas: ctx válido, con una muestra nueva utilizable en data, su sample_ticks
 *           y la referencia previous_imu_ticks. Lee la frecuencia del kernel.
 * Retorno: ninguno (void). Si falta calibración, termina sin procesar la muestra.
 * Modifica: calcula accel_norm_mps2 y gyro_norm_dps una vez para compartirlas
 *           entre movimiento y estadísticas. Actualiza previous_imu_ticks, motion,
 *           moving, nav y navigation_time_pending. Un hueco mayor de 50 ms cancela
 *           la transición de movimiento pendiente y fuerza dt=0 para retomar navegación.
 *           Conserva las medidas, el sesgo y la referencia initial_q.
 */
static void inertial_step(AcquisitionImu *ctx)
{
    if (!ctx->calibration.ready) {
        return;
    }

    float dt_s = 0.0f;

    if (ctx->previous_imu_ticks >= 0) {
        int64_t elapsed_ticks = ctx->sample_ticks - ctx->previous_imu_ticks;

        if (elapsed_ticks >
            (int64_t)k_ms_to_ticks_floor64(TRACKER_IMU_MAX_GAP_MS)) {
            motion_interrupt(&ctx->motion);
            ctx->navigation_time_pending = true;
        } else {
            dt_s = (float)elapsed_ticks / CONFIG_SYS_CLOCK_TICKS_PER_SEC;
        }
    }

    ctx->previous_imu_ticks = ctx->sample_ticks;

    ctx->accel_norm_mps2 = vector_norm(ctx->data.accel_mps2);
    ctx->gyro_norm_dps = vector_norm(ctx->data.gyro_dps);

    motion_step(ctx);
    navigation_step(ctx, dt_s);
}

/**
 * Invalida la muestra e interrumpe continuidad sin borrar la trayectoria.
 * Entradas: ctx válido; puede tener preparación pendiente o ya terminada.
 * Retorno: ninguno (void).
 * Modifica: calidad, nav.valid, tiempos de integración y confirmación pendiente
 *           de movimiento. Reinicia solo una calibración aún pendiente; conserva
 *           bias terminado, initial_q, q/relative_q, v/p y valores/fecha buenos.
 */
static void imu_interrupt(AcquisitionImu *ctx)
{
    if (!ctx->calibration.ready) {
        calibration_reset(ctx);
    }
    motion_interrupt(&ctx->motion);
    ctx->previous_imu_ticks = -1;
    ctx->navigation_time_pending = true;
    ctx->data.quality.valid = false;
    ctx->nav.valid = false;
}

/**
 * Registra el fallo de bus y recupera físicamente solo con líneas retenidas.
 * Entradas: ctx válido; error es el resultado negativo de una operación IMU.
 * Retorno: ninguno (void).
 * Modifica: bus_interrupted y, ante bloqueo/timeout, revisión pendiente a 1 s.
 *           -EIO no identifica la causa en STM32: observa SCL/SDA sin transferir.
 *           Con una línea baja intenta recuperar y programa la siguiente revisión
 *           para dentro de 1 s; esa espera limita también los intentos físicos.
 *           El resultado de recuperar no habilita lectura: la revisión lo valida.
 *           Un fallo de observación GPIO
 *           no se toma como prueba de bloqueo. Conserva las medidas y el estimador.
 */
static void imu_bus_error(AcquisitionImu *ctx, int error)
{
    ctx->bus_interrupted = true;
    bool blocked = false;

    (void)imu_bus_is_blocked(&blocked);
    if (!blocked && error != -ETIMEDOUT) {
        return;
    }

    if (blocked) {
        (void)imu_recover_bus();
    }

    ctx->review_pending = true;
    ctx->next_review_ticks = k_uptime_ticks() +
        k_ms_to_ticks_ceil64(IMU_RECOVERY_PERIOD_MS);
}

/**
 * Intenta configurar el hardware manteniendo el contexto de cálculo existente.
 * Entradas: ctx válido; el llamador respeta 1 s entre comienzos de imu_init.
 * Retorno: ninguno (void); configured informa del resultado del intento.
 * Modifica: invalida calidad/continuidad, fecha last_init_ticks y configured.
 *           En éxito programa comprobación a 2 s; en fallo atiende el bus y
 *           deja la retoma para otro vencimiento. imu_init puede dormir durante
 *           reset/estabilización, fuera del mutex. No borra una calibración terminada.
 */
static void imu_configuration_init(AcquisitionImu *ctx)
{
    imu_interrupt(ctx);
    ctx->last_init_ticks = k_uptime_ticks();
    int ret = imu_init();
    ctx->configured = (ret == 0);
    ctx->review_pending = false;

    if (ret == 0) {
        ctx->next_configuration_check_ticks = k_uptime_ticks() +
            k_ms_to_ticks_ceil64(IMU_CONFIGURATION_PERIOD_MS);
    } else {
        imu_bus_error(ctx, ret);
    }
}

/**
 * Revisa los registros después de errores, sin leer muestras en esta vuelta.
 * Entradas: ctx válido con review_pending y su plazo de revisión alcanzado.
 * Retorno: ninguno (void).
 * Modifica: si coinciden, habilita lectura para la siguiente vuelta sin escribir;
 *           si difieren deja configured=false para un init espaciado. Un fallo
 *           conserva la revisión pendiente y la pospone 1 s, atendiendo el bus.
 *           Mantiene calidad inválida y conserva trayectoria, bias y referencia.
 */
static void imu_configuration_review(AcquisitionImu *ctx)
{
    bool matches = false;
    int ret = imu_check_configuration(&matches);

    if (ret != 0) {
        ctx->next_review_ticks = k_uptime_ticks() +
            k_ms_to_ticks_ceil64(IMU_RECOVERY_PERIOD_MS);
        imu_bus_error(ctx, ret);
        return;
    }

    ctx->review_pending = false;
    ctx->configured = matches;
    if (matches) {
        ctx->next_configuration_check_ticks = k_uptime_ticks() +
            k_ms_to_ticks_ceil64(IMU_CONFIGURATION_PERIOD_MS);
    }
}

/**
 * Inicializa el contexto privado y realiza un intento de configuración de la IMU.
 *
 * Entradas: ctx apunta a memoria válida y escribible; no necesita contenido previo.
 *           Requiere alimentación estabilizada y uso desde el hilo de adquisición.
 * Retorno: ninguno (void); ctx->configured indica si imu_init terminó correctamente.
 * Modifica: sobrescribe todo *ctx con medidas inválidas, calibración pendiente,
 *           navegación inicial, moving=false y tiempos sin referencia.
 *           imu_init configura el hardware y puede bloquear durante su arranque.
 */
void acquisition_imu_init(AcquisitionImu *ctx)
{
    *ctx = (AcquisitionImu){
        .data = {
            .quality = {
                .valid = false,
                .last_good_ms = -1,
            },
            .calibration_pending = true,
        },
        .initial_q = {.w = 1.0f},
        .navigation_time_pending = true,
        .previous_imu_ticks = -1,
        .capture_begin_ticks = -1,
        .sample_ticks = -1,
    };

    calibration_reset(ctx);
    motion_init(&ctx->motion);
    estimator_init(&ctx->nav);

    imu_configuration_init(ctx);
}

/**
 * Intenta una lectura IMU y procesa calibración, correcciones, movimiento y navegación.
 *
 * Entradas: ctx válido e inicializado, utilizado solo por el hilo de adquisición.
 *           Si configured es true, lee el reloj y consulta el driver IMU.
 * Retorno: true si hay datos o calidad que publicar, incluso ante error o saturación;
 *          true también al atender revisión/init; false durante su espera o NO_NEW.
 * Modifica: fecha el intento en capture_begin_ticks y sample_ticks. Una muestra
 *           utilizable actualiza data y los contextos de procesamiento. Error o
 *           saturación invalidan calidad/nav y rompen continuidad, conservando los
 *           últimos valores utilizables y una calibración ya terminada. Si estaba
 *           pendiente, se reinicia. NO_NEW conserva medidas, calidad y procesamiento,
 *           aunque actualiza las marcas del intento. Reinicia bus_interrupted en cada vuelta.
 *           Tras tres errores de lectura revisa configuración en la siguiente
 *           vuelta; espacia revisiones fallidas e init a 1 s. Conserva el cálculo
 *           durante esos pasos y no lee otra muestra en su misma vuelta.
 *           El llamador publica después: esta función no toma el mutex compartido.
 */
bool acquisition_imu_update(AcquisitionImu *ctx)
{
    ctx->bus_interrupted = false;
    int64_t now_ticks = k_uptime_ticks();

    if (ctx->review_pending) {
        if (now_ticks < ctx->next_review_ticks) {
            return false;
        }
        imu_configuration_review(ctx);
        return true;
    }

    if (!ctx->configured) {
        if (now_ticks - ctx->last_init_ticks <
            (int64_t)k_ms_to_ticks_ceil64(IMU_RECOVERY_PERIOD_MS)) {
            return false;
        }
        imu_configuration_init(ctx);
        return true;
    }

    ImuReading reading;

    ctx->capture_begin_ticks = k_uptime_ticks();

    int ret = imu_read(&reading);

    ctx->sample_ticks = k_uptime_ticks();

    if (ret == IMU_NO_NEW) {
        return false;
    }

    if (ret == IMU_NEW && !reading.range_limited) {
        ctx->consecutive_errors = 0;
        Vec3f accel = accel_correct(reading.accel_mps2);

        calibration_step(ctx, &accel, &reading.gyro_dps);

        Vec3f gyro = reading.gyro_dps;

        if (ctx->calibration.ready) {
            gyro.x -= ctx->calibration.bias_dps.x;
            gyro.y -= ctx->calibration.bias_dps.y;
            gyro.z -= ctx->calibration.bias_dps.z;
        }

        ctx->data = (ImuData){
            .accel_mps2 = accel,
            .gyro_dps = gyro,

            .quality = {
                .valid = true,
                .last_good_ms =
                    k_ticks_to_ms_floor64(ctx->sample_ticks),
            },

            .range_limited = false,
            .calibration_pending = !ctx->calibration.ready,
        };

        inertial_step(ctx);
    } else {
        imu_interrupt(ctx);

        if (ret < 0) {
            if (ctx->consecutive_errors < IMU_REVIEW_ERROR_COUNT) {
                ctx->consecutive_errors++;
            }
            if (ctx->consecutive_errors >= IMU_REVIEW_ERROR_COUNT) {
                ctx->review_pending = true;
                ctx->next_review_ticks = ctx->sample_ticks;
            }
            imu_bus_error(ctx, ret);
        }

        if (ret == IMU_NEW) {
            ctx->data.range_limited = reading.range_limited;
        }
    }

    return true;
}

/**
 * Comprueba periódicamente PWR y escalas con la rutina existente del driver.
 * Entradas: ctx inicializado; bus_allowed confirma que no hubo un fallo previo
 *           en la vuelta. Se llama después de ambiente, desde el mismo hilo.
 * Retorno: true si cambia calidad y debe publicarse; false si no toca, está
 *          suspendida la lectura normal o los registros coinciden.
 * Modifica: fecha la próxima comprobación a 2 s. Una discrepancia invalida la
 *           muestra y programa init; un error programa revisión a 1 s y atiende
 *           el bus. Conserva valores/fecha, trayectoria y bias. No añade estadísticas.
 */
bool acquisition_imu_check_configuration(AcquisitionImu *ctx, bool bus_allowed)
{
    int64_t now_ticks = k_uptime_ticks();
    if (!bus_allowed || !ctx->configured || ctx->review_pending ||
        now_ticks < ctx->next_configuration_check_ticks) {
        return false;
    }

    bool matches = false;
    int ret = imu_check_configuration(&matches);
    ctx->next_configuration_check_ticks = k_uptime_ticks() +
        k_ms_to_ticks_ceil64(IMU_CONFIGURATION_PERIOD_MS);
    if (ret == 0 && matches) {
        return false;
    }

    imu_interrupt(ctx);
    if (ret == 0) {
        ctx->configured = false;
    } else {
        ctx->review_pending = true;
        ctx->next_review_ticks = k_uptime_ticks() +
            k_ms_to_ticks_ceil64(IMU_RECOVERY_PERIOD_MS);
        imu_bus_error(ctx, ret);
    }
    return true;
}
