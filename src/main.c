/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2s.h>
#include <zephyr/sys/printk.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define I2S_DEV_NODE DT_NODELABEL(i2s20)

/* Audio configuration - matches Zephyr i2s test conventions */
#define SAMPLE_FREQUENCY    8000
#define SAMPLE_NO           32
#define BLOCK_SIZE          (2 * SAMPLE_NO * sizeof(int16_t))  /* stereo, 16-bit */
#define NUM_BLOCKS          4
#define TIMEOUT_MS          2000

/*
 * nRF54L15 I2S requires both RX and TX to be active simultaneously.
 * RX blocks are received and immediately discarded.
 */
K_MEM_SLAB_DEFINE(tx_mem_slab, BLOCK_SIZE, NUM_BLOCKS, 32);
K_MEM_SLAB_DEFINE(rx_mem_slab, BLOCK_SIZE, NUM_BLOCKS, 32);

static int16_t data_l[SAMPLE_NO];
static int16_t data_r[SAMPLE_NO];

static void generate_sine_waves(void)
{
	const double amplitude = 16000.0;

	for (int i = 0; i < SAMPLE_NO; i++) {
		/* Left channel: 440 Hz */
		data_l[i] = (int16_t)(amplitude *
			sin(2.0 * M_PI * 440.0 * i / SAMPLE_FREQUENCY));
		/* Right channel: 880 Hz */
		data_r[i] = (int16_t)(amplitude *
			sin(2.0 * M_PI * 880.0 * i / SAMPLE_FREQUENCY));
	}
}

static int tx_block_write(const struct device *dev)
{
	int16_t tx_block[SAMPLE_NO * 2];
	int ret;

	for (int i = 0; i < SAMPLE_NO; i++) {
		tx_block[2 * i]     = data_l[i];
		tx_block[2 * i + 1] = data_r[i];
	}

	ret = i2s_buf_write(dev, tx_block, BLOCK_SIZE);
	if (ret < 0) {
		printk("i2s_buf_write failed: %d\n", ret);
	}
	return ret;
}

static uint32_t rx_errors;
static int rx_sample_offset; /* pipeline delay found empirically: 30 samples */

/*
 * Build a flat interleaved TX reference spanning 2x SAMPLE_NO so we can
 * search for any wrap-around offset without going out of bounds.
 */
static void build_tx_ref(int16_t *ref, int len)
{
	for (int i = 0; i < len; i++) {
		int j = i % SAMPLE_NO;
		ref[2 * i]     = data_l[j];
		ref[2 * i + 1] = data_r[j];
	}
}

static int rx_block_verify(const struct device *dev)
{
	static bool first_block = true;
	int16_t rx_block[SAMPLE_NO * 2];
	int16_t ref[SAMPLE_NO * 2 * 2];
	size_t rx_size;
	int ret;

	ret = i2s_buf_read(dev, rx_block, &rx_size);
	if (ret < 0) {
		printk("i2s_buf_read failed: %d\n", ret);
		return ret;
	}

	/* Skip the first block: its leading frames contain zeros from the
	 * DMA pipeline before TX started, not valid loopback data.
	 */
	if (first_block) {
		first_block = false;
		return 0;
	}

	build_tx_ref(ref, SAMPLE_NO * 2);

	for (int i = 0; i < SAMPLE_NO; i++) {
		int j = rx_sample_offset + i;

		if (rx_block[2 * i]     != ref[2 * j] ||
		    rx_block[2 * i + 1] != ref[2 * j + 1]) {
			rx_errors++;
		}
	}
	return 0;
}

int main(void)
{
	const struct device *dev;
	struct i2s_config cfg;
	int ret;
	uint32_t count = 0;

	printk("\n=== I2S Audio Example (nRF54L15) ===\n");

	dev = DEVICE_DT_GET(I2S_DEV_NODE);
	if (!device_is_ready(dev)) {
		printk("ERROR: I2S device not ready\n");
		return -ENODEV;
	}
	printk("I2S device ready\n");

	generate_sine_waves();

	/* Configure TX and RX with identical settings.
	 * nRF54L15 I2S requires DIR_BOTH - it cannot run TX-only.
	 */
	cfg.word_size      = 16;
	cfg.channels       = 2;
	cfg.format         = I2S_FMT_DATA_FORMAT_I2S;
	cfg.frame_clk_freq = SAMPLE_FREQUENCY;
	cfg.block_size     = BLOCK_SIZE;
	cfg.timeout        = TIMEOUT_MS;
	cfg.options        = I2S_OPT_FRAME_CLK_MASTER | I2S_OPT_BIT_CLK_MASTER;

	cfg.mem_slab = &tx_mem_slab;
	ret = i2s_configure(dev, I2S_DIR_TX, &cfg);
	if (ret < 0) {
		printk("ERROR: i2s_configure TX failed: %d\n", ret);
		return ret;
	}

	cfg.mem_slab = &rx_mem_slab;
	ret = i2s_configure(dev, I2S_DIR_RX, &cfg);
	if (ret < 0) {
		printk("ERROR: i2s_configure RX failed: %d\n", ret);
		return ret;
	}

	printk("I2S configured: %d Hz, 16-bit stereo\n", SAMPLE_FREQUENCY);

	/* Pre-fill TX queue with 2 blocks before starting */
	ret = tx_block_write(dev);
	if (ret < 0) {
		return ret;
	}
	ret = tx_block_write(dev);
	if (ret < 0) {
		return ret;
	}

	/* Start both TX and RX simultaneously */
	ret = i2s_trigger(dev, I2S_DIR_BOTH, I2S_TRIGGER_START);
	if (ret < 0) {
		printk("ERROR: I2S START failed: %d\n", ret);
		return ret;
	}
	printk("I2S streaming started\n");

	/* Offset between TX and RX in the loopback (hardware pipeline delay).
	 * Determined empirically: RX lags TX by 2 stereo samples (offset 30 = 32-2).
	 */
	rx_sample_offset = 30;

	/* Main loop: keep TX fed, verify RX (loopback: wire SDOUT->SDIN) */
	while (true) {
		ret = tx_block_write(dev);
		if (ret < 0) {
			break;
		}

		ret = rx_block_verify(dev);
		if (ret < 0) {
			break;
		}

		count++;
		if (count % 100 == 0) {
			printk("Blocks: %u  RX errors: %u\n", count, rx_errors);
		}
	}

	i2s_trigger(dev, I2S_DIR_BOTH, I2S_TRIGGER_DROP);
	printk("I2S stopped\n");
	return 0;
}
