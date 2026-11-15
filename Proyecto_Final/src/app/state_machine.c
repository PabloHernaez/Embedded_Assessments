#include "state_machine.h"
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>

static App_State current_state = STATE_TEST;

App_State state_machine_init(void)
{
    current_state = STATE_TEST;

    return current_state;
}

App_State state_machine_on_button(void)
{

    switch (current_state) {

        case STATE_TEST:
            current_state = STATE_NORMAL;
            break;

        case STATE_NORMAL:
            current_state = STATE_SIMU;
            break;

        case STATE_SIMU:
            current_state = STATE_TEST;
            break;

        default:
            current_state = STATE_TEST;
            break;
    }

    return current_state;
}

App_State state_machine_get_state(void)
{
    return current_state;
}