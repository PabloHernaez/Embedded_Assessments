#include "output.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>

#include "human_report.h"
#include "simu_frame.h"

#define OUTPUT_BAUD          115200U
#define TX_TIMEOUT_MARGIN_US 20000U

static const struct device *const port = DEVICE_DT_GET(DT_NODELABEL(lpuart1));
static const struct uart_config config = {
    .baudrate = OUTPUT_BAUD,
    .parity = UART_CFG_PARITY_NONE,
    .stop_bits = UART_CFG_STOP_BITS_1,
    .data_bits = UART_CFG_DATA_BITS_8,
    .flow_ctrl = UART_CFG_FLOW_CTRL_NONE,
};

/* Mientras busy vale 1, el DMA lee tx_buf: main no lo toca. */
static char tx_buf[HUMAN_REPORT_BUFFER_SIZE];
static atomic_t busy;
static bool ready;
static uint16_t seq; /* SEQ de SIMU: solo avanza si la trama se envia; 65535 -> 0 */
static uint32_t boot_reset_cause;
static int boot_watchdog_status;

/* El driver avisa al salir el ultimo bit (TX_DONE) o al vencer el plazo de uart_tx (TX_ABORTED). */
static void tx_event(const struct device *dev, struct uart_event *evt, void *user_data)
{
    ARG_UNUSED(dev);
    ARG_UNUSED(user_data);

    if (evt->type == UART_TX_DONE || evt->type == UART_TX_ABORTED) {
        atomic_clear(&busy);
    }
}

void output_init(uint32_t reset_cause, int watchdog_status)
{
    boot_reset_cause = reset_cause;
    boot_watchdog_status = watchdog_status;
    ready = device_is_ready(port) && uart_configure(port, &config) == 0 &&
            uart_callback_set(port, tx_event, NULL) == 0;
}

bool output_busy(void)
{
    return atomic_get(&busy) != 0;
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

    /* DMA hacia LPUART1. Plazo: duracion en el cable (10 bits por byte) mas margen;
     * si vence, el driver aborta y la salida no se queda ocupada. */
    const int32_t timeout_us = (int32_t)((uint64_t)len * 10U * 1000000U / OUTPUT_BAUD + TX_TIMEOUT_MARGIN_US);

    atomic_set(&busy, 1);
    if (uart_tx(port, (const uint8_t *)tx_buf, (size_t)len, timeout_us) != 0) {
        atomic_clear(&busy);
        return;
    }
    if (mode == APP_MODE_SIMU) {
        seq++;
    }
}
