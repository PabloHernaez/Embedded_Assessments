#include "sensor_service.h"

#include <errno.h>
#include <stdbool.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>

#define SENSOR_THREAD_STACK_SIZE 1024
#define SENSOR_THREAD_PRIORITY 5

enum sensor_command {
	SENSOR_COMMAND_START,
	SENSOR_COMMAND_PAUSE,
};

K_MSGQ_DEFINE(command_queue, sizeof(enum sensor_command), 1, 4);
K_MSGQ_DEFINE(result_queue, sizeof(struct sensor_result), 1, 4);
K_SEM_DEFINE(pause_ack, 0, 1);

static atomic_t service_ready = ATOMIC_INIT(0);
static atomic_t run_requested = ATOMIC_INIT(0);

static void publish_latest(const struct sensor_result *result)
{
	struct sensor_result discarded;

	if (k_msgq_put(&result_queue, result, K_NO_WAIT) == 0) {
		return;
	}

	(void)k_msgq_get(&result_queue, &discarded, K_NO_WAIT);
	(void)k_msgq_put(&result_queue, result, K_NO_WAIT);
}

static void acknowledge_pause(void)
{
	k_msgq_purge(&result_queue);
	k_sem_give(&pause_ack);
}

static void sensor_thread(void *arg1, void *arg2, void *arg3)
{
	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	while (true) {
		enum sensor_command command;

		(void)k_msgq_get(&command_queue, &command, K_FOREVER);
		if (command == SENSOR_COMMAND_PAUSE) {
			atomic_clear(&run_requested);
			acknowledge_pause();
			continue;
		}

		if (!atomic_get(&run_requested)) {
			continue;
		}

		int64_t next_sample = k_uptime_get();

		while (true) {
			struct sensor_result result = { 0 };

			if (!atomic_get(&run_requested)) {
				(void)k_msgq_get(&command_queue, &command, K_NO_WAIT);
				acknowledge_pause();
				break;
			}

			result.status = brightness_read(&result.sample);

			/* Si llega PAUSE durante el promedio, no se publica. */
			if (!atomic_get(&run_requested)) {
				(void)k_msgq_get(&command_queue, &command, K_NO_WAIT);
				acknowledge_pause();
				break;
			}

			publish_latest(&result);
			next_sample += SENSOR_SAMPLE_PERIOD_MS;

			while (atomic_get(&run_requested)) {
				int err = k_msgq_get(&command_queue, &command,
						     K_TIMEOUT_ABS_MS(next_sample));

				if (err != 0) {
					break;
				}

				if (command == SENSOR_COMMAND_PAUSE) {
					atomic_clear(&run_requested);
					acknowledge_pause();
					break;
				}
			}

			if (!atomic_get(&run_requested)) {
				break;
			}
		}
	}
}

K_THREAD_DEFINE(sensor_thread_id, SENSOR_THREAD_STACK_SIZE, sensor_thread,
		NULL, NULL, NULL, SENSOR_THREAD_PRIORITY, 0, 0);

int sensor_service_init(void)
{
	if (atomic_get(&service_ready)) {
		return 0;
	}

	int err = brightness_init();

	if (err == 0) {
		atomic_set(&service_ready, 1);
	}

	return err;
}

int sensor_start(void)
{
	enum sensor_command command = SENSOR_COMMAND_START;

	if (!atomic_get(&service_ready)) {
		return -EACCES;
	}

	if (!atomic_cas(&run_requested, 0, 1)) {
		return 0;
	}

	k_msgq_purge(&result_queue);
	int err = k_msgq_put(&command_queue, &command, K_NO_WAIT);

	if (err != 0) {
		atomic_clear(&run_requested);
	}

	return err;
}

int sensor_pause(k_timeout_t timeout)
{
	enum sensor_command command = SENSOR_COMMAND_PAUSE;

	if (!atomic_get(&service_ready)) {
		return -EACCES;
	}

	if (!atomic_get(&run_requested)) {
		return 0;
	}

	k_sem_reset(&pause_ack);
	int err = k_msgq_put(&command_queue, &command, K_NO_WAIT);

	if (err != 0) {
		return err;
	}

	atomic_clear(&run_requested);
	return k_sem_take(&pause_ack, timeout);
}

int sensor_get_result(struct sensor_result *result, k_timeout_t timeout)
{
	if (result == NULL) {
		return -EINVAL;
	}

	if (!atomic_get(&service_ready)) {
		return -EACCES;
	}

	return k_msgq_get(&result_queue, result, timeout);
}