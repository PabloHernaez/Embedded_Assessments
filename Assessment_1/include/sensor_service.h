#ifndef SENSOR_SERVICE_H_
#define SENSOR_SERVICE_H_

#include <zephyr/kernel.h>

#include "brightness.h"

#define SENSOR_SAMPLE_PERIOD_MS 2000

struct sensor_result {
	int status;
	struct brightness_sample sample;
};

int sensor_service_init(void);
int sensor_start(void);
int sensor_pause(k_timeout_t timeout);
int sensor_get_result(struct sensor_result *result, k_timeout_t timeout);

#endif /* SENSOR_SERVICE_H_ */