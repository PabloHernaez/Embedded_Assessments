#ifndef MODES_H
#define MODES_H

#include "button.h"
#include "sensor_service.h"

enum app_mode {
    NORMAL,
    BLUE,
    OFF
};

void modes_init(void);
void modes_handle_button_event(const struct button_event *event);
void modes_handle_sensor_result(const struct sensor_result *result);
enum app_mode modes_get_current(void);

#endif