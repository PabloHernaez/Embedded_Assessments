#include "modes.h"
#include <zephyr/sys/printk.h>

static enum app_mode current_mode = NORMAL;

static void apply_mode(enum app_mode mode)
{
    switch (mode) {
    case NORMAL:
        printk("Mode: NORMAL\n receiving measures from the sensor");
        break;

    case BLUE:
        printk("Mode: BLUE\n stopping measurements from the sensor");
        break;
    
    case OFF:
        printk("Mode: OFF\n stopping measurements from the sensor");
        
    }
}

void modes_init(void)
{
    current_mode = NORMAL;
    apply_mode(current_mode);
}

void modes_handle_event(enum app_event event)
{
    if (event != EVENT_B2_SHORT_PRESS) {
        return;
    }

    switch (current_mode) {
    case NORMAL:
        current_mode = BLUE;
        break;

    case BLUE:
        current_mode = NORMAL;
        break;
    
    case OFF:
        current_mode = OFF;
        break;
    
    }

    apply_mode(current_mode);
}

enum app_mode modes_get_current(void)
{
    return current_mode;
}