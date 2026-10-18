#ifndef BUTTON_H
#define BUTTON_H

#include <zephyr/kernel.h>

enum button_event_type {
    BUTTON_PRESSED,
    BUTTON_RELEASED,
    BUTTON_LONG_TIMEOUT
};

struct button_event {
    enum button_event_type type;
    int64_t timestamp;
};

int button_init(void);

int button_get_event(struct button_event *event,
                     k_timeout_t timeout);

#endif