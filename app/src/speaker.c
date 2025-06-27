#include "audio_data.h"

#include <zephyr/drivers/i2s.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>

#define TIMEOUT             1000
#define SAMPLE_BIT_WIDTH    16
#define INITIAL_BLOCKS      2
#define BLOCK_COUNT (INITIAL_BLOCKS + 2)

static const struct i2c_dt_spec codec = I2C_DT_SPEC_GET(DT_NODELABEL(da7212));
static const struct device *const i2s = DEVICE_DT_GET(DT_NODELABEL(i2s0));
K_MEM_SLAB_DEFINE_STATIC(mem_slab, BLOCK_SIZE, BLOCK_COUNT, 4);
static bool initialized;

static int codec_setup(void)
{
	int ret;

	static const uint8_t init[][2] = {
		// word freq to 44.1khz
		{0x22, 0x0a},
		// codec in slave mode, 32 BCLK per WCLK
		{0x28, 0x00},
		// enable DAC_L
		{0x69, 0x88},
		// setup LINE_AMP_GAIN to 15db
		{0x4a, 0x3f},
		// enable LINE amplifier
		{0x6d, 0x80},
		// enable DAC_R
		{0x6a, 0x80},
		// setup MIXIN_R_GAIN to 0dB
		{0x35, 0x03},
		// enable MIXIN_R
		{0x66, 0x80},
		// setup DIG_ROUTING_DAI to DAI
		{0x21, 0x32},
		// setup DIG_ROUTING_DAC to mono
		{0x2a, 0xba},
		// setup DAC_L_GAIN to 0dB
		{0x45, 0x6f},
		// setup DAC_R_GAIN to 0dB
		{0x46, 0x6f},
		// enable DAI, 16bit per channel
		{0x29, 0x80},
		// setup SYSTEM_MODES_OUTPUT to use DAC_R,DAC_L and LINE
		{0X51, 0x00},
		// setup Master bias enable
		{0X23, 0x08},
		// Sets the input clock range for the PLL 40-80MHz
		{0X27, 0x00},
		// setup MIXOUT_R_SELECT to DAC_R selected
		{0X4C, 0x08},
		// setup MIXOUT_R_CTRL to MIXOUT_R mixer amp enable and MIXOUT R mixer enable
		{0X6F, 0x98},
	};

	// CIF_CTRL: soft reset
	ret = i2c_reg_write_byte_dt(&codec, 0x1d, 0x80);
	if (ret < 0) {
		return ret;
	}

	k_msleep(10);

	// SYSTEM_ACTIVE: wake-up
	ret = i2c_reg_write_byte_dt(&codec, 0xfd, 0x01);
	if (ret < 0) {
		return ret;
	}

	for (int i = 0; i < ARRAY_SIZE(init); ++i) {
		const uint8_t *entry = init[i];

		ret = i2c_reg_write_byte_dt(&codec, entry[0], entry[1]);
		if (ret < 0) {
			return ret;
		}
	}

	return 0;
}

static int codec_standby(void)
{
	return i2c_reg_write_byte_dt(&codec, 0xfd, 0x00);
}

static int i2s_setup(void)
{
	int ret;
	struct i2s_config config;

	config.word_size = SAMPLE_BIT_WIDTH;
	config.channels = NUMBER_OF_CHANNELS;
	config.format = I2S_FMT_DATA_FORMAT_I2S;
	config.options = I2S_OPT_BIT_CLK_MASTER | I2S_OPT_FRAME_CLK_MASTER;
	config.frame_clk_freq = SAMPLE_FREQUENCY;
	config.mem_slab = &mem_slab;
	config.block_size = BLOCK_SIZE;
	config.timeout = TIMEOUT;

	ret = i2s_configure(i2s, I2S_DIR_TX, &config);
	if (ret < 0) {
		return ret;
	}

	return 0;
}

static int cmd_speaker_play(const struct shell *sh, size_t argc, char **argv)
{
	int ret;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	if (!initialized) {
		shell_error(sh, "Speaker module not initialized");
		return -EPERM;
	}

	ret = codec_setup();
	if (ret < 0) {
		return ret;
	}

	ret = i2s_setup();
	if (ret < 0) {
		return ret;
	}

	for (int i = 0; i < 40; i++) {
		ret = i2s_trigger(i2s, I2S_DIR_TX, I2S_TRIGGER_START);
		if (ret < 0) {
			return ret;
		}

		ret = i2s_write(i2s, audio_data, BLOCK_SIZE);
		if (ret < 0) {
			return ret;
		}

		k_msleep(100);

		ret = i2s_trigger(i2s, I2S_DIR_TX, I2S_TRIGGER_DROP);
		if (ret < 0) {
			return ret;
		}
	}

	ret = codec_standby();
	if (ret < 0) {
		return ret;
	}

	shell_print(sh, "Speaker test done");

	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_speaker_cmds,
			       SHELL_CMD(play, NULL, "Play sound", cmd_speaker_play),
			       SHELL_SUBCMD_SET_END);

SHELL_SUBCMD_ADD((hwv), speaker, &sub_speaker_cmds, "Speaker", NULL, 0, 0);

int speaker_init(void)
{
	if (!i2c_is_ready_dt(&codec)) {
		return -ENODEV;
	}

	if (!device_is_ready(i2s)) {
		return -ENODEV;
	}

	initialized = true;

	return 0;
}
