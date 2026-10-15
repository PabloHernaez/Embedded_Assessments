#ifndef BRIGHTNESS_H_
#define BRIGHTNESS_H_

#include <stdint.h>

enum brightness_level {
	BRIGHTNESS_RED,
	BRIGHTNESS_YELLOW,
	BRIGHTNESS_GREEN,
};

struct brightness_sample {
	uint16_t adc_raw;
	uint8_t percent;
	enum brightness_level level;
};

int brightness_init(void);
int brightness_read(struct brightness_sample *sample);
enum brightness_level brightness_classify(uint32_t value,
					   uint32_t full_scale);

#endif /* BRIGHTNESS_H_ */