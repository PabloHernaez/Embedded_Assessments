#include "output.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/irq.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>

#include "human_report.h"
#include "simu_frame.h"

#define OUTPUT_BAUD          115200U
#define TX_TIMEOUT_MARGIN_MS 20U

static const struct device *const port = DEVICE_DT_GET(DT_NODELABEL(lpuart1));
static const struct uart_config config = {
    .baudrate = OUTPUT_BAUD,
    .parity = UART_CFG_PARITY_NONE,
    .stop_bits = UART_CFG_STOP_BITS_1,
    .data_bits = UART_CFG_DATA_BITS_8,
    .flow_ctrl = UART_CFG_FLOW_CTRL_NONE,
};

/* Mientras busy vale 1, tx_buf/tx_len/tx_offset son de la ISR. */
static char tx_buf[HUMAN_REPORT_BUFFER_SIZE];
static size_t tx_len;
static size_t tx_offset;
static atomic_t busy;
static int64_t tx_deadline_ms;
static bool ready;
static uint16_t seq; /* SEQ de SIMU: solo avanza si la trama se envia; 65535 -> 0 */
static uint32_t boot_reset_cause;
static int boot_watchdog_status;

static void tx_isr(const struct device *dev, void *user_data)
{
    ARG_UNUSED(user_data);

    if (uart_irq_update(dev) <= 0) {
        return;
    }
    if (!atomic_get(&busy)) {
        uart_irq_tx_disable(dev); /* nada que enviar: no dejar IRQ ociosas */
        return;
    }

    if (tx_offset < tx_len) {
        if (uart_irq_tx_ready(dev) > 0) {
            const int n = uart_fifo_fill(dev, (const uint8_t *)&tx_buf[tx_offset],
                                         (int)(tx_len - tx_offset));
            tx_offset += n > 0 ? (size_t)n : 0U;
        }
        return; /* el ultimo byte se confirma con TC en otra IRQ */
    }

    if (uart_irq_tx_complete(dev) > 0) {
        uart_irq_tx_disable(dev);
        atomic_clear(&busy);
    }
}

void output_init(uint32_t reset_cause, int watchdog_status)
{
    boot_reset_cause = reset_cause;
    boot_watchdog_status = watchdog_status;
    ready = device_is_ready(port) && uart_configure(port, &config) == 0 &&
            uart_irq_callback_user_data_set(port, tx_isr, NULL) == 0;
}

bool output_busy(void)
{
    return atomic_get(&busy) != 0;
}

void output_service(int64_t now_ms)
{
    /* Comprobar y abortar con la IRQ bloqueada: la ISR no puede terminar a la vez. */
    const unsigned int key = irq_lock();
    if (output_busy() && now_ms >= tx_deadline_ms) {
        uart_irq_tx_disable(port);
        atomic_clear(&busy);
    }
    irq_unlock(key);
}

void output_report(AppMode mode, const AcquisitionSnapshot *acq, const GpsSnapshot *gps,
                   const TrackerStatus *status, int64_t now_ms)
{
    int len;

    if (!ready || output_busy()) {
        return;
    }

    if (mode == APP_MODE_SIMU) {
        const SimuFrameInput in = {
            .acquisition = acq, .gps = gps, .status = status,
            .seq = seq, .uptime_ms = (uint64_t)now_ms,
        };
        len = simu_frame_encode(&in, tx_buf) == 0 ? (int)SIMU_FRAME_WIRE_SIZE : -1;
    } else {
        const HumanReportInput in = {
            .acquisition = acq, .gps = gps, .status = status,
            .uptime_ms = (uint64_t)now_ms, .reset_cause = boot_reset_cause,
            .watchdog_error = boot_watchdog_status,
        };
        len = human_report_format(mode, &in, tx_buf, sizeof(tx_buf));
    }
    if (len <= 0) {
        return;
    }
    if (mode == APP_MODE_SIMU) {
        seq++;
    }

    /* Plazo: duracion en el cable (10 bits por byte) mas margen. */
    const unsigned int key = irq_lock();
    tx_len = (size_t)len;
    tx_offset = 0U;
    tx_deadline_ms = now_ms + (int64_t)(len * 10 * 1000 / OUTPUT_BAUD) + 1 + TX_TIMEOUT_MARGIN_MS;
    atomic_set(&busy, 1);
    uart_irq_tx_enable(port);
    irq_unlock(key);
}
