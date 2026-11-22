
#include "gps.h"
#include "gps_api.h"


#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* =====================================================
 * CONFIGURACION
 * ===================================================== */

#define UART_NODE DT_NODELABEL(usart1)

#define GPS_LINE_MAX       128
#define GPS_QUEUE_SIZE     4

#define GPS_THREAD_STACK_SIZE 1536
#define GPS_THREAD_PRIORITY   5

static const struct device *uart = DEVICE_DT_GET(UART_NODE);

// Cola de NMEA
K_MSGQ_DEFINE(gps_msgq, GPS_LINE_MAX, GPS_QUEUE_SIZE, 1);

// HIlo para el stack de GPS
K_THREAD_STACK_DEFINE(gps_stack, GPS_THREAD_STACK_SIZE);

static struct k_thread gps_thread_data;

static char rx_line[GPS_LINE_MAX];
static uint16_t rx_index = 0;
static bool rx_overflow = false;

static GPS_Struct gps_data = {0};

static GpsSnapshot gps_snapshot = {
    .last_nmea_ms = -1,
    .last_fix_ms = -1,
};

K_MUTEX_DEFINE(gps_mutex);

static bool gps_initialized = false;

static void gps_uart_isr(const struct device *dev, void *user_data);

static void gps_thread(void *p1, void *p2, void *p3);

static void gps_process_sentence(char *sentence);

static void gps_process_gga(char *sentence);

static bool gps_check_checksum(const char *sentence);

int gps_init(void)
{
    if (gps_initialized) {
        return 0;
        
    }

    if (!device_is_ready(uart)) {
        printk("GPS: UART no disponible\n");
        return -ENODEV;
    }

    // Creamos el hilo
    k_thread_create(&gps_thread_data,
                    gps_stack,
                    GPS_THREAD_STACK_SIZE,
                    gps_thread,
                    NULL, NULL, NULL,
                    GPS_THREAD_PRIORITY,
                    0,
                    K_NO_WAIT);

    // Registrar callback
    int ret = uart_irq_callback_user_data_set(uart, gps_uart_isr, NULL);

    if (ret != 0) {
        printk("GPS: Error configurando UART IRQ\n");
        return ret;
    }

    gps_initialized = true;

    uart_irq_rx_enable(uart);

    printk("GPS: Inicializado correctamente\n");

    return 0;
}

static void gps_uart_isr(const struct device *dev, void *user_data)
{
    ARG_UNUSED(user_data);

    uart_irq_update(dev);

    while (uart_irq_rx_ready(dev)) {

        uint8_t c;

        if (uart_fifo_read(dev, &c, 1) != 1) {
            break;
        }

        if (c == '\n') {

            if (!rx_overflow && rx_index > 0) {

                rx_line[rx_index] = '\0';

                k_msgq_put(&gps_msgq, rx_line, K_NO_WAIT);
            }

            rx_index = 0;
            rx_overflow = false;
        }

        else if (c != '\r' && !rx_overflow) {

            if (rx_index < GPS_LINE_MAX - 1) {

                rx_line[rx_index++] = c;

            } else {

                rx_index = 0;
                rx_overflow = true;
            }
        }
    }
}


static void gps_thread(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    char sentence[GPS_LINE_MAX];

    while (1) {

        if (k_msgq_get(&gps_msgq, sentence, K_FOREVER) == 0) {

            gps_process_sentence(sentence);
        }
    }
}

static bool gps_check_checksum(const char *sentence)
{
    if (sentence == NULL || sentence[0] != '$') {
        return false;
    }

    const char *star = strchr(sentence, '*');

    if (star == NULL ||
        star[1] == '\0' ||
        star[2] == '\0') {
        return false;
    }

    unsigned char checksum = 0;

    for (const char *p = sentence + 1; p < star; p++) {
        checksum ^= (unsigned char)*p;
    }

    char *end;
    unsigned long received = strtoul(star + 1, &end, 16);

    if (end != star + 3) {
        return false;
    }

    return checksum == (unsigned char)received;
}


static bool gps_parse_number(const char *field, double *out)
{
    char *end;
    double value = strtod(field, &end);

    if (end == field || *end != '\0' || !isfinite(value)) {
        return false;
    }

    *out = value;
    return true;
}

static bool gps_convert_coordinate(const char *coord, char hemisphere, double *out)
{
    const bool latitude = hemisphere == 'N' || hemisphere == 'S';
    double value;

    if ((!latitude && hemisphere != 'E' && hemisphere != 'W') ||
        !gps_parse_number(coord, &value) || value < 0.0 || value > 18000.0) {
        return false;
    }

    const int degrees = (int)(value / 100.0);
    const double minutes = value - degrees * 100.0;
    const double result = degrees + minutes / 60.0;

    if (minutes >= 60.0 || result > (latitude ? 90.0 : 180.0)) {
        return false;
    }

    *out = (hemisphere == 'S' || hemisphere == 'W') ? -result : result;
    return true;
}

static int32_t gps_round_i32(double value)
{
    return (int32_t)(value < 0.0 ? value - 0.5 : value + 0.5);
}


// Procesamos la sentencia
static void gps_process_sentence(char *sentence)
{
    if (!gps_check_checksum(sentence)) {
        return;
    }

    k_mutex_lock(&gps_mutex, K_FOREVER);
    gps_snapshot.last_nmea_ms = k_uptime_get();
    k_mutex_unlock(&gps_mutex);

    // Unicamente accedemos a la parte de informacion GPS y satelital
    if (strncmp(sentence + 3, "GGA", 3) == 0) {

        gps_process_gga(sentence);
    }

}

// Procesamos los datos que nos vienen dell sensor
static void gps_process_gga(char *sentence)
{
    char *fields[15] = {0};

    int field_count = 0;

    char *star = strchr(sentence, '*');

    if (star != NULL) {
        *star = '\0';
    }

    fields[field_count++] = sentence;

    for (char *p = sentence;
         *p != '\0' && field_count < 15;
         p++) {

        if (*p == ',') {
            *p = '\0';
            fields[field_count++] = p + 1;
        }
    }

    if (field_count < 10 || fields[6][0] < '0' || fields[6][0] > '9' ||
        fields[6][1] != '\0') {
        return;
    }

    const uint8_t fix = (uint8_t)(fields[6][0] - '0');
    const bool valid = fix == 1U || fix == 2U;

    char *end;
    const long sats = strtol(fields[7], &end, 10);
    const bool sats_ok = end != fields[7] && *end == '\0' && sats >= 0 && sats <= 99;
    const uint8_t satellites = sats_ok ? (uint8_t)sats : 0U;
    double latitude = 0.0;
    double longitude = 0.0;
    double altitude = 0.0;
    double alt_mm = 0.0;

    if (valid) {
        if (field_count < 11 || !sats_ok || strcmp(fields[10], "M") != 0 ||
            (strcmp(fields[3], "N") != 0 && strcmp(fields[3], "S") != 0) ||
            (strcmp(fields[5], "E") != 0 && strcmp(fields[5], "W") != 0) ||
            !gps_convert_coordinate(fields[2], fields[3][0], &latitude) ||
            !gps_convert_coordinate(fields[4], fields[5][0], &longitude) ||
            !gps_parse_number(fields[9], &altitude)) {
            return;
        }

        alt_mm = altitude * 1000.0;
        if (alt_mm < INT32_MIN || alt_mm > INT32_MAX) {
            return;
        }
    }

    k_mutex_lock(&gps_mutex, K_FOREVER);

    gps_data.latitude = latitude;
    gps_data.longitude = longitude;
    gps_data.altitude = altitude;
    gps_data.satellites = satellites;
    gps_data.fix_quality = fix;
    gps_data.valid = valid;
    gps_data.gga_count++;

    gps_snapshot.fix_valid = valid;
    if (valid) {
        gps_snapshot.lat_udeg = gps_round_i32(latitude * 1000000.0);
        gps_snapshot.lon_udeg = gps_round_i32(longitude * 1000000.0);
        gps_snapshot.alt_mm = gps_round_i32(alt_mm);
        gps_snapshot.satellites = satellites;
        gps_snapshot.last_fix_ms = k_uptime_get();
    }

    k_mutex_unlock(&gps_mutex);
}

// Comprobar posicion valida
bool gps_has_fix(void)
{
    if (!gps_initialized) {
        return false;
    }

    bool valid;

    k_mutex_lock(&gps_mutex, K_FOREVER);

    valid = gps_data.valid;

    k_mutex_unlock(&gps_mutex);

    return valid;
}

// Obtener datos GPS
int gps_get_data(GPS_Struct *data)
{
    if (data == NULL) {
        return -EINVAL;
    }

    if (!gps_initialized) {
        return -EACCES;
    }

    k_mutex_lock(&gps_mutex, K_FOREVER);

    *data = gps_data;

    k_mutex_unlock(&gps_mutex);

    return 0;
}

void gps_snapshot_get(GpsSnapshot *out)
{
    k_mutex_lock(&gps_mutex, K_FOREVER);
    *out = gps_snapshot;
    k_mutex_unlock(&gps_mutex);
}
