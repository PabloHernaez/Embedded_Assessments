#ifndef RGB_LED_H_
#define RGB_LED_H_

enum rgb_color {
	RGB_OFF,
	RGB_RED,
	RGB_YELLOW,
	RGB_GREEN,
	RGB_BLUE,
};

int rgb_init(void);
int rgb_set(enum rgb_color color);

#endif /* RGB_LED_H_ */