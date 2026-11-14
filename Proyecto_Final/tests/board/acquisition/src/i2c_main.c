#include <errno.h>
#include <limits.h>
#include <soc.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/irq.h>
#include <zephyr/kernel.h>

#include "acquisition_api.h"
#include "acquisition_imu.h"
#include "environment.h"
#include "imu.h"
#include "i2c_stm32.h" /* Diagnostico privado STM32, exclusivo de este ensayo. */

#define TRACE_CAPACITY 96U
#define TRACE_DURATION_MS 15000

enum TestCommand { TEST_SLEEP = 1, TEST_RESET, TEST_SDA_BRIEF, TEST_SDA_LONG,
                   TEST_THREE_ERRORS, TEST_ENV_RESET, TEST_BUS_CLEAR, TEST_ENV_PROBE,
                   TEST_ENV_ID };
enum EventKind { EV_INJECT = 1, EV_HOLD_BEGIN, EV_HOLD_END, EV_READ, EV_CHECK,
                 EV_INIT, EV_RECOVER, EV_ENV_START, EV_ENV_READ, EV_STATE, EV_ENV_INIT,
                 EV_TRANSFER, EV_ID, EV_ID_CRC_RAW };
enum TestPhase { TEST_IDLE, TEST_ARMED, TEST_HOLDING, TEST_DONE, TEST_ERROR };

typedef struct {
    uint32_t t_ms;
    uint32_t cycle;
    uint32_t kind;
    int32_t result;
    uint32_t elapsed_cycles;
    uint32_t detail;
} TestEvent;

/* Solo existe en esta imagen. command se escribe deliberadamente por SWD. */
static volatile struct {
    uint32_t command;
    uint32_t phase;
    uint32_t last_command;
    int32_t gpio_init_result;
    uint32_t injected_ms;
    uint32_t released_ms;
    int32_t inject_result;
    uint32_t event_count;
    uint32_t dropped;
    uint32_t new_count;
    uint32_t no_new_count;
    uint32_t error_count;
    uint32_t update_max_cycles;
    uint32_t hold_cycles;
    uint32_t cycle;
    uint32_t last_failed_remaining;
    TestEvent events[TRACE_CAPACITY];
} i2c_test;

static const struct gpio_dt_spec fault_pin =
    GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), fault_gpios);
static const struct device *const bus = DEVICE_DT_GET(DT_NODELABEL(i2c2));
static volatile int64_t trace_until_ms;
static volatile uint32_t pending_command;
static volatile unsigned int errors_remaining;
static volatile int last_read_result = INT_MIN;
static volatile uint32_t last_read_detail = UINT32_MAX;
static volatile uint32_t last_state_flags = UINT32_MAX;

bool __real_acquisition_imu_update(AcquisitionImu *ctx);
int __real_imu_read(ImuReading *out);
int __real_imu_check_configuration(bool *matches);
int __real_imu_init(void);
int __real_imu_recover_bus(void);
int __wrap_imu_recover_bus(void);
int __real_environment_init(void);
int __real_environment_start(void);
int __real_environment_read(EnvironmentReading *out);
int __real_i2c_stm32_transaction(const struct device *dev, struct i2c_msg msg,
                                 uint8_t *next_msg_flags, uint16_t target);

/**
 * Indica si sigue abierta la captura de 15 s de este caso.
 * Entradas: ninguna explicita; consulta el reloj y trace_until_ms.
 * Retorno: true mientras la captura esta activa.
 * Modifica: nada; no accede a sensores.
 */
static bool trace_active(void)
{
    return k_uptime_get() < trace_until_ms;
}

/**
 * Guarda un evento de prueba, sin imprimir ni realizar transferencias.
 * Entradas: tipo, resultado, duracion en ciclos y detalle propio del tipo.
 * Retorno: ninguno (void).
 * Modifica: eventos/contador o dropped durante 15 s. Protege solo la copia
 *           breve de RAM frente a los dos productores (main e hilo D4).
 */
static void trace_add(uint32_t kind, int result, uint32_t elapsed, uint32_t detail)
{
    if (!trace_active()) { return; }
    unsigned int key = irq_lock();
    uint32_t index = i2c_test.event_count;
    if (index < TRACE_CAPACITY) {
        i2c_test.events[index] = (TestEvent){
            .t_ms = (uint32_t)k_uptime_get(), .cycle = i2c_test.cycle,
            .kind = kind, .result = result, .elapsed_cycles = elapsed, .detail = detail,
        };
        i2c_test.event_count = index + 1U;
    } else {
        i2c_test.dropped++;
    }
    irq_unlock(key);
}

/**
 * Lee una vez la segunda mitad del identificador electronico del Si7021.
 * Entradas: ninguna explicita; bus real disponible en el hilo D4 del ensayo.
 * Retorno: resultado I2C; no convierte un ID diferente en error de bus.
 * Modifica: traza con bytes SNB_3/2/1/0 y los CRC brutos, solo si se recibieron.
 *           No autentica el identificador ni altera la validacion CRC de RH.
 *           No configura el sensor ni incorpora esta consulta a produccion.
 */
static int test_environment_identify(void)
{
    const uint8_t command[2] = {0xFC, 0xC9};
    uint8_t serial[6];
    int ret = i2c_write_read(bus, 0x40, command, sizeof(command), serial, sizeof(serial));
    if (ret == 0) {
        uint32_t id = ((uint32_t)serial[0] << 24) | ((uint32_t)serial[1] << 16) |
                      ((uint32_t)serial[3] << 8) | serial[4];
        trace_add(EV_ID, 0, 0, id);
        trace_add(EV_ID_CRC_RAW, 0, 0,
                  ((uint32_t)serial[2] << 8) | serial[5]);
    }
    return ret;
}

/**
 * Observa cada mensaje ambiental y los mensajes fallidos del bus real.
 * Entradas: dev, msg, siguiente flags y direccion tal como los recibe STM32.
 * Retorno: resultado original de la transferencia, sin reintentos adicionales.
 * Modifica: hardware y buffer mediante la funcion real; despues solo la traza.
 *           detail: direccion[31:24], flags[23:16], longitud[15:12],
 *           errores NACK/ARLO/ERR en bits 8/9/10 y primer byte en [7:0].
 *           Un buffer de lectura fallida no se consulta. La cabecera privada
 *           permite distinguir causas que STM32 agrupa bajo -EIO.
 */
int __wrap_i2c_stm32_transaction(const struct device *dev, struct i2c_msg msg,
                                uint8_t *next_msg_flags, uint16_t target)
{
    uint32_t begin = k_cycle_get_32();
    int ret = __real_i2c_stm32_transaction(dev, msg, next_msg_flags, target);
    uint32_t elapsed = k_cycle_get_32() - begin;
    if (target == 0x40U || ret < 0) {
        const struct i2c_stm32_data *data = dev->data;
        uint32_t errors = (data->current.is_nack ? 1U : 0U) |
                          (data->current.is_arlo ? 2U : 0U) |
                          (data->current.is_err ? 4U : 0U);
        uint32_t first = msg.len > 0U &&
                         (ret == 0 || (msg.flags & I2C_MSG_READ) == 0U) ? msg.buf[0] : 0U;
        uint32_t detail = ((uint32_t)target << 24) | ((uint32_t)msg.flags << 16) |
                          ((msg.len & 15U) << 12) | (errors << 8) | first;
        if (target == 0x40U && ret < 0) {
            i2c_test.last_failed_remaining = data->current.len;
        }
        trace_add(EV_TRANSFER, ret, elapsed, detail);
    }
    return ret;
}

/**
 * Abre un caso nuevo y descarta solo la traza anterior de este ensayo.
 * Entradas: command entre TEST_SLEEP y TEST_ENV_ID.
 * Retorno: ninguno (void).
 * Modifica: contadores/ventana de prueba y peticion que consumira el hilo D4;
 *           conserva todos los contextos y diagnosticos de produccion.
 */
static void test_arm(uint32_t command)
{
    unsigned int key = irq_lock();
    i2c_test.phase = TEST_ARMED;
    i2c_test.last_command = command;
    i2c_test.injected_ms = 0;
    i2c_test.released_ms = 0;
    i2c_test.inject_result = 0;
    i2c_test.event_count = 0;
    i2c_test.dropped = 0;
    i2c_test.new_count = 0;
    i2c_test.no_new_count = 0;
    i2c_test.error_count = 0;
    i2c_test.update_max_cycles = 0;
    i2c_test.hold_cycles = 0;
    i2c_test.last_failed_remaining = 0;
    last_read_result = INT_MIN;
    last_read_detail = UINT32_MAX;
    last_state_flags = UINT32_MAX;
    errors_remaining = 0;
    trace_until_ms = k_uptime_get() + TRACE_DURATION_MS;
    pending_command = command != TEST_SDA_BRIEF && command != TEST_SDA_LONG ? command : 0U;
    irq_unlock(key);
}

/**
 * Cuenta vueltas y observa cambios de estado alrededor del modulo IMU real.
 * Entradas: ctx es el contexto privado real recibido desde el coordinador.
 * Retorno: el bool devuelto por la implementacion real.
 * Modifica: ctx mediante el modulo real y solo diagnosticos de ensayo adicionales.
 *           Flags: 1=configurado, 2=IMU valida, 4=nav valida, 8=cal pendiente,
 *           16=revision pendiente, 32=saturacion, 64=movimiento.
 */
bool __wrap_acquisition_imu_update(AcquisitionImu *ctx)
{
    i2c_test.cycle++;
    uint32_t begin = k_cycle_get_32();
    bool ret = __real_acquisition_imu_update(ctx);
    uint32_t elapsed = k_cycle_get_32() - begin;
    if (trace_active()) {
        if (elapsed > i2c_test.update_max_cycles) { i2c_test.update_max_cycles = elapsed; }
        uint32_t flags = (ctx->configured ? 1U : 0U) | (ctx->data.quality.valid ? 2U : 0U) |
            (ctx->nav.valid ? 4U : 0U) | (ctx->data.calibration_pending ? 8U : 0U) |
            (ctx->review_pending ? 16U : 0U) | (ctx->data.range_limited ? 32U : 0U) |
            (ctx->moving ? 64U : 0U);
        if (flags != last_state_flags) {
            trace_add(EV_STATE, (int)flags, elapsed, 0);
            last_state_flags = flags;
        }
    }
    return ret;
}

/**
 * Atiende una inyeccion pendiente desde el mismo hilo que posee I2C y lee IMU.
 * Entradas: out valido como en imu_read; peticion escrita solo por main de ensayo.
 * Retorno: resultado real, NO_NEW tras inyeccion correcta, o tres -EIO simulados.
 * Modifica: MPU real al inyectar PWR, Si7021 con reset diagnostico o el bus
 *           mediante un bus clear o direccionamiento sin datos (solo ensayo);
 *           out solo como lo permita imu_read real.
 *           Cuenta resultados y registra errores/cambios; detalle READ 0=real,
 *           1=invocacion sustituida por diagnostico I2C, 2=error simulado.
 */
int __wrap_imu_read(ImuReading *out)
{
    uint32_t begin = k_cycle_get_32();
    uint32_t detail = 0;
    int ret;
    uint32_t command = pending_command;
    pending_command = 0;
    if (command == TEST_THREE_ERRORS) { errors_remaining = 3; }

    if (command == TEST_SLEEP || command == TEST_RESET || command == TEST_ENV_RESET ||
        command == TEST_BUS_CLEAR || command == TEST_ENV_PROBE || command == TEST_ENV_ID) {
        uint8_t value = command == TEST_SLEEP ? 0x49U :
                        command == TEST_RESET ? 0x80U :
                        command == TEST_ENV_RESET ? 0xFEU : 0xFFU;
        if (command == TEST_ENV_ID) { ret = test_environment_identify(); }
        else if (command == TEST_ENV_PROBE) { ret = i2c_write(bus, NULL, 0, 0x40); }
        else if (command == TEST_BUS_CLEAR) { ret = __wrap_imu_recover_bus(); }
        else if (command == TEST_ENV_RESET) { ret = i2c_write(bus, &value, 1, 0x40); }
        else { ret = i2c_reg_write_byte(bus, 0x68, 0x6B, value); }
        i2c_test.injected_ms = (uint32_t)k_uptime_get();
        i2c_test.inject_result = ret;
        i2c_test.phase = ret == 0 ? TEST_DONE : TEST_ERROR;
        trace_add(EV_INJECT, ret, k_cycle_get_32() - begin, value);
        ret = ret < 0 ? ret : IMU_NO_NEW;
        detail = 1;
    } else if (errors_remaining > 0U) {
        errors_remaining--;
        ret = -EIO;
        detail = 2;
        if (i2c_test.injected_ms == 0U) { i2c_test.injected_ms = (uint32_t)k_uptime_get(); }
        if (errors_remaining == 0U) { i2c_test.phase = TEST_DONE; }
    } else {
        ret = __real_imu_read(out);
    }

    if (trace_active()) {
        if (ret == IMU_NEW) { i2c_test.new_count++; }
        else if (ret == IMU_NO_NEW) { i2c_test.no_new_count++; }
        else { i2c_test.error_count++; }
        if (ret < 0 || ret != last_read_result || detail != last_read_detail) {
            trace_add(EV_READ, ret, k_cycle_get_32() - begin, detail);
        }
        last_read_result = ret;
        last_read_detail = detail;
    }
    return ret;
}

/**
 * Observa la comprobacion real de PWR/escalas sin alterar su resultado.
 * Entradas: matches valido. Retorno: resultado real; matches cambia solo en exito.
 * Modifica: la salida permitida por el driver y traza; detalle 1=coincide, 0=no.
 */
int __wrap_imu_check_configuration(bool *matches)
{
    uint32_t begin = k_cycle_get_32();
    int ret = __real_imu_check_configuration(matches);
    trace_add(EV_CHECK, ret, k_cycle_get_32() - begin, ret == 0 && *matches ? 1U : 0U);
    return ret;
}

/**
 * Observa el init real, incluidas sus esperas. Entradas: ninguna explicita.
 * Retorno: resultado real. Modifica: hardware mediante el driver y traza.
 */
int __wrap_imu_init(void)
{
    uint32_t begin = k_cycle_get_32();
    int ret = __real_imu_init();
    trace_add(EV_INIT, ret, k_cycle_get_32() - begin, 0);
    return ret;
}

/**
 * Observa los pulsos reales de recuperacion. Entradas: ninguna explicita.
 * Retorno: resultado real. Modifica: bus mediante Zephyr y traza de ensayo.
 */
int __wrap_imu_recover_bus(void)
{
    uint32_t begin = k_cycle_get_32();
    int ret = __real_imu_recover_bus();
    trace_add(EV_RECOVER, ret, k_cycle_get_32() - begin, 0);
    return ret;
}

/**
 * Observa la configuracion ambiental real, incluidos los reintentos fallidos.
 * Entradas: ninguna explicita. Retorno: resultado real de environment_init.
 * Modifica: Si7021 mediante el driver y traza; conserva el resultado original.
 */
int __wrap_environment_init(void)
{
    uint32_t begin = k_cycle_get_32();
    int ret = __real_environment_init();
    trace_add(EV_ENV_INIT, ret, k_cycle_get_32() - begin, 0);
    return ret;
}

/**
 * Observa F5 real. Entradas: ninguna explicita. Retorno: resultado del driver.
 * Modifica: sensor ambiental mediante el driver y traza; no espera conversion.
 */
int __wrap_environment_start(void)
{
    uint32_t begin = k_cycle_get_32();
    int ret = __real_environment_start();
    trace_add(EV_ENV_START, ret, k_cycle_get_32() - begin, 0);
    return ret;
}

/**
 * Observa la recogida ambiental real. Entradas: out valido como en el driver.
 * Retorno: resultado real. Modifica: out solo en exito, hardware y traza.
 */
int __wrap_environment_read(EnvironmentReading *out)
{
    uint32_t begin = k_cycle_get_32();
    int ret = __real_environment_read(out);
    trace_add(EV_ENV_READ, ret, k_cycle_get_32() - begin, 0);
    return ret;
}

/**
 * Ejecuta solo el fixture GPIO y arma las peticiones del ensayo por SWD.
 * Entradas: command 1=SLEEP, 2=RESET, 3=SDA 60 ms, 4=SDA 5 s, 5=tres -EIO,
 *           6=reset Si7021, 7=bus clear, 8=sondeo 0x40, 9=identificacion FC/C9.
 * Retorno: 0 si no puede preparar D8; no retorna durante el ensayo normal.
 * Modifica: D8/PC2 en drenador abierto (1=liberado), traza y sesion estadistica.
 *           Main no realiza I2C; todas las inyecciones I2C usan el hilo D4.
 */
int main(void)
{
    int ret = gpio_is_ready_dt(&fault_pin) ?
        gpio_pin_configure_dt(&fault_pin, GPIO_OUTPUT_HIGH) : -ENODEV;
    i2c_test.gpio_init_result = ret;
    if (ret != 0) { i2c_test.phase = TEST_ERROR; return 0; }
    acquisition_stats_begin();

    while (true) {
        uint32_t command = i2c_test.command;
        if (command >= TEST_SLEEP && command <= TEST_ENV_ID) {
            i2c_test.command = 0;
            test_arm(command);
            if (command == TEST_SDA_BRIEF || command == TEST_SDA_LONG) {
                i2c_test.phase = TEST_HOLDING;
                uint32_t begin = k_cycle_get_32();
                ret = gpio_pin_set_dt(&fault_pin, 0);
                i2c_test.injected_ms = (uint32_t)k_uptime_get();
                i2c_test.inject_result = ret;
                trace_add(EV_HOLD_BEGIN, ret, 0, command);
                if (ret == 0) { k_msleep(command == TEST_SDA_BRIEF ? 60 : 5000); }
                int release_ret = gpio_pin_set_dt(&fault_pin, 1);
                i2c_test.hold_cycles = k_cycle_get_32() - begin;
                i2c_test.released_ms = (uint32_t)k_uptime_get();
                i2c_test.phase = ret == 0 && release_ret == 0 ? TEST_DONE : TEST_ERROR;
                trace_add(EV_HOLD_END, release_ret, i2c_test.hold_cycles,
                          (uint32_t)gpio_pin_get_dt(&fault_pin));
            }
        }
        k_msleep(10);
    }
}
