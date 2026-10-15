#include "brightness.h"

#include <errno.h>
#include <stdint.h>

#include <zephyr/devicetree.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/sys/util.h>

#define ADC_NODE DT_PATH(zephyr_user)
#define READS_PER_SAMPLE 8U

static const struct adc_dt_spec light_adc = ADC_DT_SPEC_GET(ADC_NODE);

enum brightness_level brightness_classify(uint32_t value, uint32_t full_scale)
{
	if ((full_scale == 0U) ||
	    ((uint64_t)value * 100U < (uint64_t)full_scale * 33U)) {
		return BRIGHTNESS_RED;
	}

	if ((uint64_t)value * 100U > (uint64_t)full_scale * 66U) {
		return BRIGHTNESS_GREEN;
	}

	return BRIGHTNESS_YELLOW;
}

int brightness_init(void)
{
	if (!adc_is_ready_dt(&light_adc)) {
		return -ENODEV;
	}

	return adc_channel_setup_dt(&light_adc);
}

int brightness_read(struct brightness_sample *sample)
{
	struct brightness_sample result;
	int16_t adc_value;
	uint32_t sum = 0U;
	uint32_t full_scale;
	uint32_t brightness_value;
	struct adc_sequence sequence = {
		.buffer = &adc_value,
		.buffer_size = sizeof(adc_value),
	};

	if (sample == NULL) {
		return -EINVAL;
	}

	if ((light_adc.resolution == 0U) || (light_adc.resolution > 16U)) {
		return -EINVAL;
	}

	int err = adc_sequence_init_dt(&light_adc, &sequence);

	if (err != 0) {
		return err;
	}

	for (uint32_t i = 0U; i < READS_PER_SAMPLE; i++) {
		err = adc_read_dt(&light_adc, &sequence);
		if (err != 0) {
			return err;
		}

		if (adc_value < 0) {
			return -ERANGE;
		}

		sum += (uint16_t)adc_value;
	}

	full_scale = BIT(light_adc.resolution) - 1U;
	result.adc_raw = (uint16_t)(sum / READS_PER_SAMPLE);
	if (result.adc_raw > full_scale) {
		return -ERANGE;
	}

	brightness_value = full_scale - result.adc_raw;
	result.percent = (uint8_t)DIV_ROUND_CLOSEST(
		brightness_value * 100U, full_scale);
	result.level = brightness_classify(brightness_value, full_scale);

	*sample = result;
	return 0;
}