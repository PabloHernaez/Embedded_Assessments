#ifndef APP_TASKS_H
#define APP_TASKS_H

#include <stdbool.h>
#include "state_machine.h"

void app_tasks_run(App_State state, bool entering);

int64_t app_tasks_get_interval(App_State state);

#endif