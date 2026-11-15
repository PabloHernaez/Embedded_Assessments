
#ifndef BUTTON_H
#define BUTTON_H

#include <zephyr/kernel.h>
#include <stdint.h>

/* Tipos de eventos del boton */
typedef enum {
    BUTTON_PRESSED
} button_event_type_t;

/* Estructura de un evento del boton */
struct button_event {
    button_event_type_t type;
    int64_t timestamp;
};

/* Inicializacion del boton */
int button_init(void);

/* Obtencion de eventos del boton */
int button_get_event(struct button_event *event,
                     k_timeout_t timeout);

#endif
