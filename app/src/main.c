#include "speaker.h"
#include "buttons.h"
#include "charger.h"
#include "display.h"
#include "flash.h"
#include "haptic.h"
#include "light.h"
#include "imu.h"
#include "mag.h"
#include "mic.h"
#include "press.h"

#ifdef CONFIG_BOARD_ASTERIX
#include "lfxo.h"
#endif

#include <stdio.h>

#include <zephyr/shell/shell.h>

#include <app_version.h>

SHELL_SUBCMD_SET_CREATE(hwv_cmds, (hwv));
SHELL_CMD_REGISTER(hwv, &hwv_cmds, "HWV commands", NULL);

int main(void)
{
	int ret;

	printf("HWV v%s\n", APP_VERSION_STRING);

	ret = buttons_init();
	if (ret < 0) {
		printf("Failed to initialize buttons module (%d)\n", ret);
	}

	ret = charger_init();
	if (ret < 0) {
		printf("Failed to initialize charger module (%d)\n", ret);
	}

	ret = display_init();
	if (ret < 0) {
		printf("Failed to initialize display module (%d)\n", ret);
	}

	ret = flash_init();
	if (ret < 0) {
		printf("Failed to initialize flash module (%d)\n", ret);
	}

	ret = haptic_init();
	if (ret < 0) {
		printf("Failed to initialize haptic module (%d)\n", ret);
	}

	ret = light_init();
	if (ret < 0) {
		printf("Failed to initialize light sensor module (%d)\n", ret);
	}

	ret = imu_init();
	if (ret < 0) {
		printf("Failed to initialize IMU module (%d)\n", ret);
	}

	ret = mag_init();
	if (ret < 0) {
		printf("Failed to initialize magnetometer module (%d)\n", ret);
	}

	ret = mic_init();
	if (ret < 0) {
		printf("Failed to initialize microphone module (%d)\n", ret);
	}

	ret = press_init();
	if (ret < 0) {
		printf("Failed to initialize pressure sensor module (%d)\n", ret);
	}

	ret = speaker_init();
	if (ret < 0) {
		printf("Failed to initialize speaker module (%d)\n", ret);
	}

#ifdef CONFIG_BOARD_ASTERIX
	ret = lfxo_init();
	if (ret < 0) {
		printf("Failed to initialize LFXO module (%d)\n", ret);
	}
#endif

	return 0;
}
