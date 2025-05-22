#include <zephyr/drivers/sensor.h>
#include <zephyr/pm/device.h>
#include <zephyr/shell/shell.h>
#include <zephyr/drivers/i2c.h>
#include "mag.h"

static const struct device *const i2c = DEVICE_DT_GET(DT_NODELABEL(i2c1));
static bool initialized;

int get_mmc5603nj_date(double *mag_values)
{
	if (!device_is_ready(i2c)) {
		return -ENODEV;
	}

	// Reset sensor
	uint8_t cmd[2] = {MMC5603NJ_ADDR_INTCTRL1, 0x80};
	int ret = i2c_write(i2c, cmd, sizeof(cmd), MMC5603NJ_I2C_ADDR);
	if (ret < 0) {
		return ret;
	}

	k_msleep(10);

	// set configuration register values
	uint8_t config[] = {MMC5603NJ_ADDR_ODR,      0xFF, MMC5603NJ_ADDR_INTCTRL0, 0xA0,
			    MMC5603NJ_ADDR_INTCTRL1, 0x03, MMC5603NJ_ADDR_INTCTRL2, 0x10};

	for (int i = 0; i < sizeof(config); i += 2) {
		cmd[0] = config[i];
		cmd[1] = config[i + 1];
		ret = i2c_write(i2c, cmd, sizeof(cmd), MMC5603NJ_I2C_ADDR);
		if (ret < 0) {
			return ret;
		}
	}

	// Trigger measurement
	cmd[0] = MMC5603NJ_ADDR_INTCTRL0;
	cmd[1] = 0x21;
	ret = i2c_write(i2c, cmd, sizeof(cmd), MMC5603NJ_I2C_ADDR);
	if (ret < 0) {
		return ret;
	}

	// Wait for measurement completion
	uint8_t status;
	uint8_t status_reg = MMC5603NJ_ADDR_STATUS1;
	uint32_t timeout = 100; // Timeout in milliseconds

	do {
		ret = i2c_write_read(i2c, MMC5603NJ_I2C_ADDR, &status_reg, 1, &status, 1);
		if (ret < 0) {
			return ret;
		}
		k_msleep(1);
		timeout--;
	} while (!(status & 0x01) && timeout > 0);
	if (timeout == 0) {
		return -ETIMEDOUT;
	}

	// Read magnetometer data
	uint8_t mag_data[9];
	uint8_t reg = MMC5603NJ_ADDR_XOUT0;
	ret = i2c_write_read(i2c, MMC5603NJ_I2C_ADDR, &reg, 1, mag_data, 9);
	if (ret < 0) {
		return ret;
	}
	printk("mag_data: %02x %02x %02x %02x %02x %02x %02x %02x %02x\n", mag_data[0], mag_data[1],
	       mag_data[2], mag_data[3], mag_data[4], mag_data[5], mag_data[6], mag_data[7],
	       mag_data[8]);
	// Calculate magnetic field strength (mGauss)
	int32_t mag_x = ((mag_data[0] << 12) | (mag_data[1] << 4) | (mag_data[6])) - 524288;
	int32_t mag_y = ((mag_data[2] << 12) | (mag_data[3] << 4) | (mag_data[7])) - 524288;
	int32_t mag_z = ((mag_data[4] << 12) | (mag_data[5] << 4) | (mag_data[8])) - 524288;

	// Convert to Gauss units and store
	mag_values[0] = (mag_x * 625) / 10000; // X-axis magnetic field strength
	mag_values[1] = (mag_y * 625) / 10000; // Y-axis magnetic field strength
	mag_values[2] = (mag_z * 625) / 10000; // Z-axis magnetic field strength

	return 0;
}

static int cmd_mag_get(const struct shell *sh, size_t argc, char **argv)
{
	int err;
	double mag_data[3];

	err = get_mmc5603nj_date(mag_data);
	if (err < 0) {
		shell_error(sh, "Failed to get sensor data (%d)", err);
		return err;
	}

	shell_print(sh, "Magnetic field (gauss): %f, %f, %f", mag_data[0], mag_data[1],
		    mag_data[2]);

	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_mag_cmds, SHELL_CMD(get, NULL, "Get sensor data", cmd_mag_get),
			       SHELL_SUBCMD_SET_END);

SHELL_SUBCMD_ADD((hwv), mag, &sub_mag_cmds, "Magnetic sensor", NULL, 0, 0);

int mag_init(void)
{
	if (!device_is_ready(i2c)) {
		return -ENODEV;
	}

	initialized = true;
	return 0;
}