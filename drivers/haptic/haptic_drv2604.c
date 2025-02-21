#define DT_DRV_COMPAT ti_drv2604

#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <hwv/drivers/haptic.h>

LOG_MODULE_REGISTER(drv2604, CONFIG_HAPTIC_LOG_LEVEL);

#define DRV2604_MODE     0x01
#define DRV2604_RTPI     0x02
#define DRV2604_FEEDBACK 0x1A
#define DRV2604_CONTROL3 0x1D

#define DRV2604_MODE_DEV_RESET BIT(7)
#define DRV2604_MODE_STANDBY   BIT(6)
#define DRV2604_MODE_MODE      GENMASK(2, 0)

#define DRV2604_MODE_MODE_RTP 0x05U

#define DRV2604_RTPI_RTP_INPUT GENMASK(7, 0)

#define DRV2604_RTPI_RTP_INPUT_MAX 0x7FU

#define DRV2604_FEEDBACK_N_ERM_LRA GENMASK(7, 6)
#define DRV2604_FEEDBACK_LRA BIT(7)
struct drv2604_config {
	struct i2c_dt_spec i2c;
	struct gpio_dt_spec en;
};

static int drv2604_configure(const struct device *dev, uint8_t ampl)
{
	const struct drv2604_config *config = dev->config;
	uint8_t val;
	int ret;

	val = FIELD_PREP(DRV2604_MODE_STANDBY, ampl > 0U ? 0U : 1U);
	ret = i2c_reg_update_byte_dt(&config->i2c, DRV2604_MODE, DRV2604_MODE_STANDBY, val);
	if (ret < 0) {
		LOG_ERR("Could not set active mode (%d)", ret);
		return ret;
	}

	val = FIELD_PREP(DRV2604_RTPI_RTP_INPUT, ampl * DRV2604_RTPI_RTP_INPUT_MAX / 100U);
	ret = i2c_reg_write_byte_dt(&config->i2c, DRV2604_RTPI, val);
	if (ret < 0) {
		LOG_ERR("Could not set amplitude (%d)", ret);
		return ret;
	}

	return 0;
}

static const struct haptic_driver_api drv2604_api = {
	.configure = drv2604_configure,
};

static int drv2604_init(const struct device *dev)
{
	const struct drv2604_config *config = dev->config;
	int ret;
	uint8_t val;

	if (!i2c_is_ready_dt(&config->i2c)) {
		LOG_ERR("I2C not ready");
		return -ENODEV;
	}

	if (!gpio_is_ready_dt(&config->en)) {
		LOG_ERR("EN GPIO not ready");
		return -ENODEV;
	}

	ret = gpio_pin_configure_dt(&config->en, GPIO_OUTPUT_ACTIVE);
	if (ret < 0) {
		LOG_ERR("Failed to configure EN GPIO");
		return ret;
	}

	val = FIELD_PREP(DRV2604_MODE_DEV_RESET, 1U);
	ret = i2c_reg_write_byte_dt(&config->i2c, DRV2604_MODE, val);
	if (ret < 0) {
		LOG_ERR("Could not reset (%d)", ret);
		return ret;
	}

	val = FIELD_PREP(DRV2604_MODE_MODE, DRV2604_MODE_MODE_RTP);
	ret = i2c_reg_write_byte_dt(&config->i2c, DRV2604_MODE, val);
	if (ret < 0) {
		LOG_ERR("Could not set mode (%d)", ret);
		return ret;
	}

	val = FIELD_PREP(DRV2604_FEEDBACK_LRA, 1U);
	ret = i2c_reg_update_byte_dt(&config->i2c, DRV2604_FEEDBACK, DRV2604_FEEDBACK_LRA, val);
	if (ret < 0) {
		LOG_ERR("Could not set LRA", ret);
		return ret;
	}

	return 0;
}

#define TI_DRV2604_DEFINE(n)                                                                       \
	static const struct drv2604_config drv2604_config_##n = {                                  \
		.en = GPIO_DT_SPEC_INST_GET(n, en_gpios),                                          \
		.i2c = I2C_DT_SPEC_INST_GET(n),                                                    \
	};                                                                                         \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(n, &drv2604_init, NULL, NULL, &drv2604_config_##n, POST_KERNEL,      \
			      CONFIG_HAPTIC_INIT_PRIORITY, &drv2604_api);

DT_INST_FOREACH_STATUS_OKAY(TI_DRV2604_DEFINE)
