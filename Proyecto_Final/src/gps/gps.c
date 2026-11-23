/*
 * Servicio GPS (P1). USART1 a 9600 8N1 (RX en PB7, ver overlay). La ISR junta
 * bytes hasta el LF y entrega cada linea a una cola; un hilo de prioridad 5
 * la pasa por el parser NMEA (nmea.c) y publica un GpsSnapshot.
 */
#include "gps_api.h"
#include "nmea.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/task_wdt/task_wdt.h>

#define GPS_LINE_MAX      128 /* NMEA admite 82 caracteres; el resto se descarta */
#define GPS_QUEUE_LINES   4
#define GPS_STACK_SIZE    1536
#define GPS_PRIORITY      5
#define GPS_WAIT_MS       1000 /* sin NMEA el hilo despierta igual para su watchdog */
#define GPS_WDT_MS        3000

static const struct device *const gps_uart = DEVICE_DT_GET(DT_NODELABEL(usart1));

K_MSGQ_DEFINE(gps_lines, GPS_LINE_MAX, GPS_QUEUE_LINES, 1);
K_MUTEX_DEFINE(gps_lock);

static GpsSnapshot published = {
    .last_nmea_ms = -1,
    .last_fix_ms = -1,
};

void gps_snapshot_get(GpsSnapshot *out)
{
    k_mutex_lock(&gps_lock, K_FOREVER);
    *out = published;
    k_mutex_unlock(&gps_lock);
}

/* ISR de RX: drena la FIFO sin esperar bytes futuros. */
static void gps_isr(const struct device *dev, void *user_data)
{
    static char line[GPS_LINE_MAX];
    static size_t len;
    static bool too_long;
    uint8_t c;

    ARG_UNUSED(user_data);
    (void)uart_irq_update(dev);

    while (uart_irq_rx_ready(dev) && uart_fifo_read(dev, &c, 1) == 1) {
        if (c == '\n') {
            if (!too_long && len > 0) {
                line[len] = '\0';
                (void)k_msgq_put(&gps_lines, line, K_NO_WAIT); /* cola llena: se pierde */
            }
            len = 0;
            too_long = false;
        } else if (c != '\r') {
            if (len < sizeof(line) - 1) {
                line[len++] = (char)c;
            } else {
                too_long = true;
            }
        }
    }
}

/* Solo este hilo escribe published: se parsea sobre una copia y se publica de una vez. */
static void process_line(char *line)
{
    if (!nmea_checksum_ok(line)) {
        return;
    }

    GpsSnapshot next = published;
    next.last_nmea_ms = k_uptime_get(); /* enlace vivo, haya fix o no */
    if (nmea_parse_gga(line, &next) && next.fix_valid) {
        next.last_fix_ms = next.last_nmea_ms;
    }

    k_mutex_lock(&gps_lock, K_FOREVER);
    published = next;
    k_mutex_unlock(&gps_lock);
}

static void gps_entry(void *p1, void *p2, void *p3)
{
    char line[GPS_LINE_MAX];

    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    /* Sin UART el hilo sigue vivo: el enlace se vera caido por last_nmea_ms. */
    if (device_is_ready(gps_uart) &&
        uart_irq_callback_user_data_set(gps_uart, gps_isr, NULL) == 0) {
        uart_irq_rx_enable(gps_uart);
    }

    const int wdt = task_wdt_add(GPS_WDT_MS, NULL, NULL);

    while (true) {
        (void)task_wdt_feed(wdt);
        if (k_msgq_get(&gps_lines, line, K_MSEC(GPS_WAIT_MS)) == 0) {
            process_line(line);
        }
    }
}

K_THREAD_DEFINE(gps_tid, GPS_STACK_SIZE, gps_entry, NULL, NULL, NULL, GPS_PRIORITY, 0, 0);
