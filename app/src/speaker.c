#include "audio_data.h"

#include <zephyr/sys/__assert.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/irq.h>


#include "hal/nrf_clock.h"
#include "hal/nrf_gpio.h"
#include "nrfx_i2s.h"

#define DA7212_PLL_STATUS            0x03
#define DA7212_CIF_CTRL              0x1D
#define DA7212_DIG_ROUTING_DAI       0x21
#define DA7212_SR                    0x22
#define DA7212_REFERENCES            0x23
#define DA7212_PLL_FRAC_TOP          0x24
#define DA7212_PLL_FRAC_BOT          0x25
#define DA7212_PLL_INTEGER           0x26
#define DA7212_PLL_CTRL              0x27
#define DA7212_DAI_CLK_MODE          0x28
#define DA7212_DAI_CTRL              0x29
#define DA7212_DIG_ROUTING_DAC       0x2A
#define DA7212_DAC_FILTERS5          0x40
#define DA7212_DAC_R_GAIN            0x46
#define DA7212_LINE_GAIN             0x4A
#define DA7212_MIXOUT_R_SELECT       0x4C
#define DA7212_SYSTEM_MODES_OUTPUT   0x51
#define DA7212_DAC_R_CTRL            0x6A
#define DA7212_LINE_CTRL             0x6D
#define DA7212_MIXOUT_R_CTRL         0x6F
#define DA7212_LDO_CTRL              0x90
#define DA7212_GAIN_RAMP_CTRL        0x92
#define DA7212_TONE_GEN_CFG1         0xB4
#define DA7212_TONE_GEN_CYCLES       0xB6
#define DA7212_TONE_GEN_ON_PER       0xBB
#define DA7212_SYSTEM_ACTIVE         0xFD

static bool initialized;

static nrfx_i2s_buffers_t s_i2s_bufs;
static const nrfx_i2s_t s_i2s = NRFX_I2S_INSTANCE(0);
static nrfx_i2s_config_t s_i2s_cfg = NRFX_I2S_DEFAULT_CONFIG(
	 NRF_GPIO_PIN_MAP(0, 12), NRF_GPIO_PIN_MAP(0, 7), NRF_GPIO_PIN_MAP(1, 9),
	NRF_GPIO_PIN_MAP(0, 13), NRF_I2S_PIN_NOT_CONNECTED);

static const struct i2c_dt_spec codec = I2C_DT_SPEC_GET(DT_NODELABEL(da7212));

#define SINE_WAVE_TOTAL_SAMPLES 32

/* Stereo sine wave data (L, R, L, R, ...) */
int16_t sine_wave[SINE_WAVE_TOTAL_SAMPLES] = {
	0,     0,     3134,  3134,  5791,  5791,  7567,  7567,  8191,  8191,  7567,
	7567,  5791,  5791,  3134,  3134,  0,     0,     -3134, -3134, -5791, -5791,
	-7567, -7567, -8191, -8191, -7567, -7567, -5791, -5791, -3134, -3134};

static void da7212_register_write(uint8_t reg, uint8_t value) {
  int ret;

  ret = i2c_reg_write_byte_dt(&codec, reg, value);
  __ASSERT(ret == 0, "Failed to write to DA7212 register 0x%02x", reg);
}

static uint8_t da7212_register_read(uint8_t reg) {
  uint8_t data;
  int ret;

  ret = i2c_reg_read_byte_dt(&codec, reg, &data);
  __ASSERT(ret == 0, "Failed to read from DA7212 register 0x%02x", reg);

  return data;
}

static void prv_codec_setup(void) {
  // CIF_CTRL: soft reset
  da7212_register_write(DA7212_CIF_CTRL, 0x80);

  k_msleep(10);

  // SYSTEM_ACTIVE: wake-up
  da7212_register_write(DA7212_SYSTEM_ACTIVE, 0x01);

  // REFERENCES: enable master bias
  da7212_register_write(DA7212_REFERENCES, 0x08);

  //k_msleep(30);

  // LDO_CTRL: enable LDO, 1.05V
  da7212_register_write(DA7212_LDO_CTRL, 0x80);

  // PLL: MCLK=4MHz (so input divider=2), we need 12.288MHz System Clock for SR=16KHz (see table 34)
  // VCO = System Clock * 8 = 98.304MHz
  // Feedback divider = VCO * Input Divider / MCLK
  //                  = 98.304MHz * 2 / 4MHz = 49.152
  // PLL_INTEGER = 49 (0x31)
  // PLL_FRAC = 0.152 * 2^13 = 1245 (0x4dd)
  // PLL_FRAC_TOP = 0x04
  // PLL_FRAC_BOT = 0xdd
  da7212_register_write(DA7212_PLL_FRAC_TOP, 0x04);
  da7212_register_write(DA7212_PLL_FRAC_BOT, 0xdd);
  da7212_register_write(DA7212_PLL_INTEGER, 0x31);

  // PLL_CTRL: enable + SRM, input clock range 2-10MHz
  da7212_register_write(DA7212_PLL_CTRL, 0xC0);

  // PLL: operate with a 2-5MHz MCLK (ref. DA7212 rev 3.6, 13.29)
  da7212_register_write(0xF0, 0x8B);
  da7212_register_write(0xF2, 0x03);
  da7212_register_write(0xF0, 0x00);

  k_msleep(40);

  __ASSERT(da7212_register_read(DA7212_PLL_STATUS) == 0x07, "DA7212 PLL not locked");

  // GAIN_RAMP_CTRL: 1s
  da7212_register_write(DA7212_GAIN_RAMP_CTRL, 0x02);
  // SR: 16KHz
  da7212_register_write(DA7212_SR, 0x05);
  // DAI_CLK_MODE: slave
  da7212_register_write(DA7212_DAI_CLK_MODE, 0x00);
  // DAI_CTRL: enable, 16-bit
  da7212_register_write(DA7212_DAI_CTRL, 0x80);
  // DIG_ROUTING_DAI: DAI_R/L_SRC to DAI_R/L
  da7212_register_write(DA7212_DIG_ROUTING_DAI, 0x32);
  // DIG_ROUTING_DAC: DAC_R/L mono mix of R/L
  da7212_register_write(DA7212_DIG_ROUTING_DAC, 0xba);
  // DAC_R_GAIN: 3dB
  da7212_register_write(DA7212_DAC_R_GAIN, 0x73);
  // DAC_R_CTRL: enable
  da7212_register_write(DA7212_DAC_R_CTRL, 0x80);
  // MIXOUT_R_SELECT: DAC_R
  da7212_register_write(DA7212_MIXOUT_R_SELECT, 0x08);
  // MIXOUT_R_CTRL: enable, softmix enable, amp enable
  da7212_register_write(DA7212_MIXOUT_R_CTRL, 0x98);
  // LINE_GAIN: 15dB
  da7212_register_write(DA7212_LINE_GAIN, 0x3f);
  // LINE_CTRL: enable
  da7212_register_write(DA7212_LINE_CTRL, 0x80);
}

static void prv_codec_standby(void) {
  da7212_register_write(DA7212_SYSTEM_ACTIVE, 0x00);
}

static void prv_data_handler(nrfx_i2s_buffers_t const *p_released, uint32_t status)
{
}

static void prv_playback(void)
{
	nrfx_err_t err;

	// MCLK: 4MHz, sample rate: ~16KHz (4Mhz / 256 = 15625Hz)
	s_i2s_cfg.mck_setup = NRF_I2S_MCK_32MDIV8;
	s_i2s_cfg.ratio = NRF_I2S_RATIO_256X;
	s_i2s_cfg.channels = NRF_I2S_CHANNELS_STEREO;

	err = nrfx_i2s_init(&s_i2s, &s_i2s_cfg, prv_data_handler);
	__ASSERT_NO_MSG(err == NRFX_SUCCESS);

	s_i2s_bufs.p_tx_buffer = (uint32_t *)sine_wave;
	s_i2s_bufs.buffer_size = SINE_WAVE_TOTAL_SAMPLES / 2;
	err = nrfx_i2s_start(&s_i2s, &s_i2s_bufs, 0);
	__ASSERT_NO_MSG(err == NRFX_SUCCESS);

	prv_codec_setup();

	k_msleep(3000);

	prv_codec_standby();

	nrfx_i2s_stop(&s_i2s);
	nrfx_i2s_uninit(&s_i2s);
}

static void i2s_isr_shim(const void *ctx)
{
	nrfx_i2s_0_irq_handler();
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

	nrf_clock_event_clear(NRF_CLOCK, NRF_CLOCK_EVENT_HFCLKSTARTED);
	nrf_clock_task_trigger(NRF_CLOCK, NRF_CLOCK_TASK_HFCLKSTART);
	while (!nrf_clock_event_check(NRF_CLOCK, NRF_CLOCK_EVENT_HFCLKSTARTED)) {
	}
	nrf_clock_event_clear(NRF_CLOCK, NRF_CLOCK_EVENT_HFCLKSTARTED);

	prv_playback();

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

	IRQ_CONNECT(I2S_IRQn, 0, i2s_isr_shim, NULL, 0);
	irq_enable(I2S_IRQn);

	initialized = true;

	return 0;
}
