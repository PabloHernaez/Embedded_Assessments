#ifndef LED_H
#define LED_H

typedef enum {
    LED_RED,
    LED_BLUE,
    LED_GREEN
} led_color_t;

int led_init(void);
int led_set_color(led_color_t color);
int led_off(void);

#endif