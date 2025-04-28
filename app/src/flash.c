#include <stdlib.h>

#include <zephyr/drivers/flash.h>
#include <zephyr/drivers/regulator.h>
#include <zephyr/kernel.h>
#include <zephyr/pm/device.h>
#include <zephyr/random/random.h>
#include <zephyr/shell/shell.h>

#define FLASH_STRESS_TEST_SIZE 1024

static const struct device *const flash = DEVICE_DT_GET(DT_ALIAS(flash0));
static bool initialized;

static uint32_t xorshift32(uint32_t seed)
{
	seed ^= seed << 13;
	seed ^= seed >> 17;
	seed ^= seed < 5;
	return seed;
}

static int cmd_flash_id(const struct shell *sh, size_t argc, char **argv)
{
	int ret;
	uint8_t id[3];

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	if (!initialized) {
		shell_error(sh, "Flash module not initialized");
		return -EPERM;
	}

	ret = pm_device_action_run(flash, PM_DEVICE_ACTION_RESUME);
	if (ret < 0) {
		shell_error(sh, "Failed to resume flash (%d)", ret);
		return ret;
	}

	ret = flash_read_jedec_id(flash, id);
	if (ret < 0) {
		shell_error(sh, "Failed to read flash ID (%d)", ret);
		goto end;
	}

	shell_print(sh, "Flash ID: %02x %02x %02x", id[0], id[1], id[2]);

end:
	(void)pm_device_action_run(flash, PM_DEVICE_ACTION_SUSPEND);

	return ret;
}

static int cmd_flash_erase(const struct shell *sh, size_t argc, char **argv)
{
	int ret;
	uint32_t addr;
	struct flash_pages_info info;

	if (!initialized) {
		shell_error(sh, "Flash module not initialized");
		return -EPERM;
	}

	if (argc < 2) {
		shell_error(sh, "Missing address or size");
		return -EINVAL;
	}

	addr = strtoul(argv[1], NULL, 0);

	ret = pm_device_action_run(flash, PM_DEVICE_ACTION_RESUME);
	if (ret < 0) {
		shell_error(sh, "Failed to resume flash (%d)", ret);
		return ret;
	}

	ret = flash_get_page_info_by_offs(flash, addr, &info);
	if (ret < 0) {
		shell_error(sh, "Could not determine page size (%d)", ret);
		goto end;
	}

	ret = flash_erase(flash, addr, info.size);
	if (ret < 0) {
		shell_error(sh, "Failed to erase flash (%d)", ret);
		goto end;
	}

	shell_print(sh, "Erased %d bytes at 0x%08x", info.size, addr);

end:
	(void)pm_device_action_run(flash, PM_DEVICE_ACTION_SUSPEND);

	return ret;
}

static int cmd_flash_read(const struct shell *sh, size_t argc, char **argv)
{
	int ret;
	uint32_t addr;
	uint8_t buf[SHELL_HEXDUMP_BYTES_IN_LINE];
	size_t len;

	if (!initialized) {
		shell_error(sh, "Flash module not initialized");
		return -EPERM;
	}

	if (argc < 3) {
		shell_error(sh, "Missing address or length");
		return -EINVAL;
	}

	addr = strtoul(argv[1], NULL, 0);
	len = strtoul(argv[2], NULL, 0);

	ret = pm_device_action_run(flash, PM_DEVICE_ACTION_RESUME);
	if (ret < 0) {
		shell_error(sh, "Failed to resume flash (%d)", ret);
		return ret;
	}

	while (len > 0U) {
		size_t rd = MIN(len, sizeof(buf));

		ret = flash_read(flash, addr, buf, rd);
		if (ret < 0) {
			shell_error(sh, "Failed to read from flash (%d)", ret);
			goto end;
		}

		shell_hexdump_line(sh, addr, buf, rd);

		addr += rd;
		len -= rd;
	}

end:
	(void)pm_device_action_run(flash, PM_DEVICE_ACTION_SUSPEND);

	return ret;
}

static int cmd_flash_write(const struct shell *sh, size_t argc, char **argv)
{
	int ret;
	uint32_t addr;
	uint8_t *buf;
	size_t data_len;

	if (!initialized) {
		shell_error(sh, "Flash module not initialized");
		return -EPERM;
	}

	if (argc < 3) {
		shell_error(sh, "Missing address or data");
		return -EINVAL;
	}

	addr = strtoul(argv[1], NULL, 0);
	data_len = strlen(argv[2]) / 2U;

	buf = k_malloc(data_len);
	if (buf == NULL) {
		shell_error(sh, "Failed to allocate buffer");
		return -ENOMEM;
	}

	for (size_t i = 0U; i < data_len; i++) {
		buf[i] = strtoul(&argv[2][i * 2], NULL, 16);
	}

	ret = pm_device_action_run(flash, PM_DEVICE_ACTION_RESUME);
	if (ret < 0) {
		shell_error(sh, "Failed to resume flash (%d)", ret);
		k_free(buf);
		return ret;
	}

	ret = flash_write(flash, addr, buf, data_len);
	if (ret < 0) {
		shell_error(sh, "Failed to write to flash (%d)", ret);
	} else {
		shell_print(sh, "Wrote %d bytes to 0x%08x", data_len, addr);
	}

	k_free(buf);

	(void)pm_device_action_run(flash, PM_DEVICE_ACTION_SUSPEND);

	return ret;
}

static int cmd_flash_stress(const struct shell *sh, size_t argc, char **argv)
{
	int ret;
	unsigned int iters;
	struct flash_pages_info info;
	uint8_t *buf;
	uint64_t flash_size;

	if (!initialized) {
		shell_error(sh, "Flash module not initialized");
		return -EPERM;
	}

	if (argc < 2) {
		iters = 1;
	} else {
		iters = (unsigned int)strtoul(argv[1], NULL, 0);
	}

	ret = flash_get_size(flash, &flash_size);
	if (ret < 0) {
		shell_error(sh, "Failed to get flash size (%d)", ret);
		return ret;
	}

	buf = k_malloc(FLASH_STRESS_TEST_SIZE);
	if (buf == NULL) {
		shell_error(sh, "Buffer allocation failed");
		return -ENOMEM;
	}

	ret = pm_device_action_run(flash, PM_DEVICE_ACTION_RESUME);
	if (ret < 0) {
		shell_error(sh, "Failed to resume flash (%d)", ret);
		goto err_free;
	}

	for (unsigned int i = 0; i < iters; i++) {
		uint32_t addr, lfsr_seed, lfsr_cur;

		addr = sys_rand32_get() % flash_size;

		ret = flash_get_page_info_by_offs(flash, addr, &info);
		if (ret < 0) {
			shell_error(sh, "Failed to get page info (%d)", ret);
			goto err_suspend;
		}

		shell_print(sh, "Stress test iteration %u: addr 0x%08x", i, (uint32_t)info.start_offset);

		ret = flash_erase(flash, info.start_offset, info.size);
		if (ret < 0) {
			shell_error(sh, "Failed to erase flash (%d)", ret);
			goto err_suspend;
		}

		lfsr_seed = sys_rand32_get();
		if (lfsr_seed == 0U) {
			lfsr_seed = 1U;
		}

		lfsr_cur = lfsr_seed;
		for (unsigned int i = 0U; i < FLASH_STRESS_TEST_SIZE; i++) {
			buf[i] = (uint8_t)lfsr_cur;
			lfsr_cur = xorshift32(lfsr_cur);
		}

		ret = flash_write(flash, info.start_offset, buf, FLASH_STRESS_TEST_SIZE);
		if (ret < 0) {
			shell_error(sh, "Failed to write to flash (%d)", ret);
			goto err_suspend;
		}

		memset(buf, 0, FLASH_STRESS_TEST_SIZE);
		ret = flash_read(flash, info.start_offset, buf, FLASH_STRESS_TEST_SIZE);
		if (ret < 0) {
			shell_error(sh, "Failed to read from flash (%d)", ret);
			goto err_suspend;
		}

		lfsr_cur = lfsr_seed;
		for (unsigned int i = 0U; i < FLASH_STRESS_TEST_SIZE; i++) {
			if (buf[i] != (uint8_t)lfsr_cur) {
				shell_error(
					sh,
					"Miscompare at offset %u: expected 0x%02x, found 0x%02x", 0,
					(uint8_t)lfsr_cur, buf[i]);
				ret = -EINVAL;
				goto err_suspend;
			}
			lfsr_cur = xorshift32(lfsr_cur);
		}
	}

	shell_print(sh, "Flash stress test done! All %u iterations passed", iters);

err_suspend:
	(void)pm_device_action_run(flash, PM_DEVICE_ACTION_SUSPEND);

err_free:
	k_free(buf);

	return ret;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
	sub_flash_cmds, SHELL_CMD(id, NULL, "Read flash ID", cmd_flash_id),
	SHELL_CMD_ARG(erase, NULL, "Erase page: erase PAGE_ADDR", cmd_flash_erase, 2, 0),
	SHELL_CMD_ARG(read, NULL, "Read: read ADDR NUM_BYTES", cmd_flash_read, 3, 0),
	SHELL_CMD_ARG(write, NULL, "Write: write ADDR DATA", cmd_flash_write, 3, 0),
	SHELL_CMD_ARG(stress, NULL, "Stress: stress [ITERS]", cmd_flash_stress, 1, 1),
	SHELL_SUBCMD_SET_END);

SHELL_SUBCMD_ADD((hwv), flash, &sub_flash_cmds, "Flash", NULL, 0, 0);

int flash_init(void)
{
	int ret;

	if (!device_is_ready(flash)) {
		return -ENODEV;
	}

	ret = pm_device_action_run(flash, PM_DEVICE_ACTION_SUSPEND);
	if (ret < 0) {
		return ret;
	}

	initialized = true;

	return 0;
}
