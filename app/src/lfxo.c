#include <stdlib.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/clock_control/nrf_clock_control.h>
#include <zephyr/drivers/counter.h>
#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/timeutil.h>

#define MEASURE_TIME_S 10

static const struct device *const sys_clock = DEVICE_DT_GET_ONE(nordic_nrf_clock);
static const struct device *const timer = DEVICE_DT_GET(DT_NODELABEL(timer1));
static bool initialized;

static int cmd_lfxo_test(const struct shell *sh, size_t argc, char **argv)
{
	int ret;
	uint32_t top;
	bool measured = false;
	struct timeutil_sync_config sync_config = { 0};
	struct timeutil_sync_state sync_state = { 0 };
	uint64_t counter_ref = 0U;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	if (!initialized) {
		shell_error(sh, "LFXO module not initialized");
		return -EPERM;
	}

	ret = clock_control_on(sys_clock, CLOCK_CONTROL_NRF_SUBSYS_HF);
	if (ret < 0) {
		shell_error(sh, "Failed to start HF clock: %d\n", ret);
		return ret;
	}

	sync_config.ref_Hz = counter_get_frequency(timer);
	if (sync_config.ref_Hz == 0) {
		shell_error(sh, "Timer has no fixed frequency");
		return -ENOTSUP;
	}

	top = counter_get_top_value(timer);
	if (top != UINT32_MAX) {
		shell_error(sh, "Timer wraps at %u not at 32 bits", top);
		return -ENOTSUP;
	}

	ret = counter_start(timer);
	if (ret < 0) {
		shell_error(sh, "Failed to start timer: %d\n", ret);
		return ret;
	}

	sync_config.local_Hz = CONFIG_SYS_CLOCK_TICKS_PER_SEC;
	sync_state.cfg = &sync_config;

	shell_print(sh, "Checking timer at %u Hz against ticks at %u Hz\n", sync_config.ref_Hz,
		    sync_config.local_Hz);

	while (!measured) {
		uint32_t ctr;
		float skew;
		struct timeutil_sync_instant inst;

		ret = counter_get_value(timer, &ctr);
		if (ret < 0) {
			shell_error(sh, "Failed to get timer value: %d\n", ret);
			return ret;
		}

		counter_ref += ctr - (uint32_t)counter_ref;
		inst.ref = counter_ref;
		inst.local = k_uptime_ticks();

		ret = timeutil_sync_state_update(&sync_state, &inst);
		if (ret < 0) {
			shell_error(sh, "Sync update error: %d\n", ret);
			return ret;
		}

		if (ret == 0) {
			k_sleep(K_SECONDS(MEASURE_TIME_S));
			continue;
		}

		skew = timeutil_sync_estimate_skew(&sync_state);
		shell_print(sh, "Skew %f ; err %d ppb", (double)skew,
			    timeutil_sync_skew_to_ppb(skew));
		measured = true;
	}

	ret = counter_stop(timer);
	if (ret < 0) {
		shell_error(sh, "Failed to stop timer: %d\n", ret);
		return ret;
	}

	ret = clock_control_off(sys_clock, CLOCK_CONTROL_NRF_SUBSYS_HF);
	if (ret < 0) {
		shell_error(sh, "Failed to stop HF clock: %d\n", ret);
		return ret;
	}

	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_lfxo_cmds,
			       SHELL_CMD(test, NULL, "Test LFXO accuracy", cmd_lfxo_test),
			       SHELL_SUBCMD_SET_END);

SHELL_SUBCMD_ADD((hwv), lfxo, &sub_lfxo_cmds, "LFXO", NULL, 0, 0);

int lfxo_init(void)
{
	if (!device_is_ready(sys_clock) || !device_is_ready(timer)) {
		return -ENODEV;
	}

	initialized = true;

	return 0;
}
