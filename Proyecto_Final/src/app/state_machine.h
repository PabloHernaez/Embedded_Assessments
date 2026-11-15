#ifndef STATE_MACHINE_H
#define STATE_MACHINE_H

typedef enum {
    STATE_TEST,
    STATE_NORMAL,
    STATE_SIMU
} App_State;

App_State state_machine_init(void);

App_State state_machine_on_button(void);

App_State state_machine_get_state(void);

#endif