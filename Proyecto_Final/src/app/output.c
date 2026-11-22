#include "output.h"

#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/irq.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>

#include "human_report.h"
#include "simu_frame.h"

#define OUTPUT_BAUD 115200U
#define TX_BITS_PER_BYTE 10U
#define TX_TIMEOUT_MARGIN_MS 20U

static const struct device *const port = DEVICE_DT_GET(DT_NODELABEL(lpuart1));
static const struct uart_config config = {
    .baudrate = OUTPUT_BAUD,
    .parity = UART_CFG_PARITY_NONE,
    .stop_bits = UART_CFG_STOP_BITS_1,
    .data_bits = UART_CFG_DATA_BITS_8,
    .flow_ctrl = UART_CFG_FLOW_CTRL_NONE,
};

static char tx_buf[HUMAN_REPORT_BUFFER_SIZE];
static size_t tx_len;
static size_t tx_offset;
static int64_t tx_deadline_ms;
static atomic_t busy;
static atomic_t link_failed;
static atomic_t completed;
static uint32_t aborted;
static bool enabled;

static uint16_t seq;
static uint32_t simu_replaced_fields;

static void tx_irq(const struct device *dev, void *user_data)
{
    ARG_UNUSED(user_data);

    if (uart_irq_update(dev) <= 0) {
        return;
    }
    if (!atomic_get(&busy)) {
        uart_irq_tx_disable(dev);
        return;
    }

    if (tx_offset < tx_len) {
        if (uart_irq_tx_ready(dev) > 0) {
            const int n = uart_fifo_fill(dev, (const uint8_t *)tx_buf + tx_offset,
                                         (int)(tx_len - tx_offset));
            if (n < 0) {
                uart_irq_tx_disable(dev);
                atomic_set(&link_failed, 1);
            } else {
                tx_offset += (size_t)n;
            }
        }

        /* Esperar otra IRQ para confirmar el TC del ultimo byte. */
        return;
    }

    if (uart_irq_tx_complete(dev) > 0) {
        uart_irq_tx_disable(dev);
        atomic_inc(&completed);
        atomic_clear(&link_failed);
        atomic_clear(&busy);
    }
}

void output_init(void)
{
    if (!device_is_ready(port)) {
        atomic_set(&link_failed, 1);
        return;
    }

    uart_irq_tx_disable(port);
    enabled = uart_configure(port, &config) == 0 &&
        uart_irq_callback_user_data_set(port, tx_irq, NULL) == 0;
    if (!enabled) {
        atomic_set(&link_failed, 1);
    }
}

bool output_busy(void)
{
    return atomic_get(&busy) != 0;
}

bool output_service(int64_t now_ms)
{
    const unsigned int key = irq_lock();
    const bool expired = output_busy() && now_ms >= tx_deadline_ms;
    if (expired) {
        uart_irq_tx_disable(port);
        tx_len = 0U;
        tx_offset = 0U;
        ++aborted;
        atomic_set(&link_failed, 1);
        atomic_clear(&busy);
    }
    irq_unlock(key);

    if (expired) {
        enabled = uart_configure(port, &config) == 0;
    }
    return atomic_get(&link_failed) != 0;
}

static void start_tx(size_t len)
{
    const uint32_t bits_ms = (uint32_t)len * TX_BITS_PER_BYTE * 1000U;
    const uint32_t timeout_ms =
        (bits_ms + OUTPUT_BAUD - 1U) / OUTPUT_BAUD + TX_TIMEOUT_MARGIN_MS;
    const int64_t start_ms = k_uptime_get();

    const unsigned int key = irq_lock();
    tx_len = len;
    tx_offset = 0U;
    tx_deadline_ms = start_ms + timeout_ms;
    atomic_set(&busy, 1);
    uart_irq_tx_enable(port);
    irq_unlock(key);
}

bool output_report(AppMode mode, const AcquisitionSnapshot *acq,
                   const GpsSnapshot *gps, const TrackerStatus *status,
                   int64_t now_ms, uint32_t reset_cause, uint32_t omitted_reports)
{
    if (!enabled || output_busy()) {
        return false;
    }

    size_t len;
    uint16_t replacements = 0U;
    if (mode == APP_MODE_SIMU) {
        const SimuFrameInput input = {
            .acquisition = acq,
            .gps = gps,
            .status = status,
            .seq = seq,
            .uptime_ms = (uint64_t)now_ms,
        };
        if (simu_frame_encode(&input, tx_buf, sizeof(tx_buf), &len, &replacements) != 0 ||
            len != SIMU_FRAME_WIRE_SIZE || memcmp(tx_buf, "$SIMU1", 6U) != 0 ||
            tx_buf[len - 2U] != '\r' || tx_buf[len - 1U] != '\n') {
            return false;
        }
    } else {
        const HumanReportInput input = {
            .acquisition = acq,
            .gps = gps,
            .status = status,
            .uptime_ms = (uint64_t)now_ms,
            .reset_cause = reset_cause,
            .simu_replaced_fields = simu_replaced_fields,
            .tx_completed = (uint32_t)atomic_get(&completed),
            .tx_aborted = aborted,
            .tx_omitted = omitted_reports,
        };
        if (human_report_format(mode, &input, tx_buf, sizeof(tx_buf), &len) != 0) {
            return false;
        }
    }

    start_tx(len);
    if (mode == APP_MODE_SIMU) {
        ++seq;
        simu_replaced_fields += replacements;
    }
    return true;
}
