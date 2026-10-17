#ifndef MODES_H
#define MODES_H

#include "modes.h"

enum app_mode {
    NORMAL,
    BLUE,
    OFF
};

enum app_event {
    EVENT_B2_SHORT_PRESS
};

void modes_init(void);
void modes_handle_event(enum app_event event);
enum app_mode modes_get_current(void);


#endif