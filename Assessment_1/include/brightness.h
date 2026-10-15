#ifndef BRIGHTNESS_H_
#define BRIGHTNESS_H_

#include <stdint.h>

struct brightness_sample {
	uint16_t adc_raw;
	uint8_t percent;
};

int brightness_init(void);
int brightness_read(struct brightness_sample *sample);

#endif /* BRIGHTNESS_H_ */