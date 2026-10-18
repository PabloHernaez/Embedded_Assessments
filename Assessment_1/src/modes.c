
#include <errno.h>
#include <stdbool.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "modes.h"
#include "rgb_led.h"
#include "sensor_service.h"

/* Estado actual del sistema. */
static enum app_mode current_mode = NORMAL;

/* Traduce el nivel de brillo al color correspondiente del RGB. */
static enum rgb_color color_for_level(enum brightness_level level)
{
    switch (level) {
    case BRIGHTNESS_RED:
        return RGB_RED;

    case BRIGHTNESS_YELLOW:
        return RGB_YELLOW;

    case BRIGHTNESS_GREEN:
        return RGB_GREEN;

    default:
        return RGB_OFF;
    }
}

/* Modo NORMAL: activar el sensor. */
static void NORMAL_execution(void)
{
    int err = sensor_start();

    if (err != 0) {
        printk("Error al activar el sensor: %d\n", err);
    } else {
        printk("Modo NORMAL: sensor activo\n");
    }
}

/* Modo BLUE: pausar el sensor y encender el LED azul. */
static void BLUE_execution(void)
{
    int err = sensor_pause(K_MSEC(1000));

    if (err != 0) {
        printk("Error al pausar el sensor: %d\n", err);
    }

    err = rgb_set(RGB_BLUE);

    if (err != 0) {
        printk("Error al configurar el LED azul: %d\n", err);
    }

    printk("Modo BLUE\n");
}

/* Modo OFF: pausar el sensor y apagar el LED. */
static void OFF_execution(void)
{
    int err = sensor_pause(K_MSEC(1000));

    if (err != 0) {
        printk("Error al pausar el sensor: %d\n", err);
    }

    err = rgb_set(RGB_OFF);

    if (err != 0) {
        printk("Error al apagar el LED: %d\n", err);
    }

    printk("Modo OFF\n");
}

/* Aplica las acciones asociadas a cada modo. */
static void apply_mode(enum app_mode mode)
{
    switch (mode) {
    case NORMAL:
        printk("Entering NORMAL state");
        NORMAL_execution();
        break;

    case BLUE:
        printk("Entering BLUE state");
        BLUE_execution();
        break;

    case OFF:
        printk("Entering OFF state");
        OFF_execution();
        break;

    default:
        printk("Error: modo desconocido\n");
        break;
    }
}

/* Inicialización del gestor de modos. */
void modes_init(void)
{
    current_mode = NORMAL;
    apply_mode(current_mode);
}

/* Gestiona las transiciones de la máquina de estados. */
void modes_handle_button_event(const struct button_event *event)
{
    static bool button_down;
    static bool long_press_handled;
    static int64_t press_start;

    if (event == NULL) {
        return;
    }

    switch (event->type) {
    case BUTTON_PRESSED:
        button_down = true;
        long_press_handled = false;
        press_start = event->timestamp;
        break;

    case BUTTON_LONG_TIMEOUT:
        if (button_down && !long_press_handled) {
            long_press_handled = true;

            if (current_mode != OFF) {
                current_mode = OFF;
                apply_mode(current_mode);
            }
        }
        break;

    case BUTTON_RELEASED:
        if (!button_down) {
            break;
        }

        button_down = false;

        if (event->timestamp - press_start >= 1000) {
            if (!long_press_handled && current_mode != OFF) {
                current_mode = OFF;
                apply_mode(current_mode);
            }
        } else if (!long_press_handled) {
            switch (current_mode) {
            case NORMAL:
                current_mode = BLUE;
                break;

            case BLUE:
            case OFF:
                current_mode =
                    (current_mode == BLUE) ? NORMAL : BLUE;
                break;
            }

            apply_mode(current_mode);
        }
        break;
    }
}


void modes_handle_sensor_result(const struct sensor_result *result)
{
    int err;

    if (result == NULL) {
        return;
    }

    /* Las muestras solo controlan el RGB en modo NORMAL. */
    if (current_mode != NORMAL) {
        return;
    }

    if (result->status != 0) {
        printk("Error reading sensor/ADC: %d\n", result->status);
        return;
    }

    err = rgb_set(color_for_level(result->sample.level));

    if (err != 0) {
        printk("Error updating RGB: %d\n", err);
    }

    printk("ADC=%u, brightness=%u %%\n",
           result->sample.adc_raw,
           result->sample.percent);
}

/* Devuelve el modo actual. */
enum app_mode modes_get_current(void)
{
    return current_mode;
}
