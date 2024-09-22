/*
 * Copyright 2022,2024 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT zephyr_sdhc_spi_slot



#include <zephyr/drivers/sdhc.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/sys/crc.h>
#include <zephyr/pm/device_runtime.h>

LOG_MODULE_REGISTER(sdhc_spi, CONFIG_SDHC_LOG_LEVEL);

#define MAX_CMD_READ 21
#define SPI_R1B_TIMEOUT_MS 3000
#define SD_SPI_SKIP_RETRIES 1000000

#define _INST_REQUIRES_EXPLICIT_FF(inst) (SPI_MOSI_OVERRUN_DT(DT_INST_BUS(inst)) != 0xFF) ||

/* The SD protocol requires sending ones while reading but the Zephyr
 * SPI API defers the choice of default values to the drivers.
 *
 * For drivers that we know will send ones we can avoid allocating a
 * 512 byte array of ones and remove the limit on the number of bytes
 * that can be read in a single transaction.
 */
#define ANY_INST_REQUIRES_EXPLICIT_FF DT_INST_FOREACH_STATUS_OKAY(_INST_REQUIRES_EXPLICIT_FF) 0

#if ANY_INST_REQUIRES_EXPLICIT_FF

static const uint8_t sdhc_ones[] = {
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
};

BUILD_ASSERT(sizeof(sdhc_ones) == 512, "0xFF array for SDHC must be 512 bytes");

#endif /* ANY_INST_REQUIRES_EXPLICIT_FF */

struct sdhc_spi_config {
	const struct device *spi_dev;
	const struct gpio_dt_spec pwr_gpio;
	const struct gpio_dt_spec cd_gpio;
	const uint32_t spi_max_freq;
	uint32_t power_delay_ms;
};

struct sdhc_spi_data {
	const struct device* dev;
	enum sdhc_power power_mode;
	struct spi_config *spi_cfg;
	struct spi_config cfg_a;
	struct spi_config cfg_b;
	struct gpio_callback cd_callback;
	sdhc_interrupt_cb_t sdhc_cb;
	void *sdhc_cb_user_data;
	int sdhc_cb_sources;
	uint8_t scratch[MAX_CMD_READ];
};

/* Receives a block of bytes */
static int sdhc_spi_rx(const struct device *spi_dev, struct spi_config *spi_cfg,
	uint8_t *buf, int len)
{
#if ANY_INST_REQUIRES_EXPLICIT_FF
	struct spi_buf tx_bufs[] = {
		{
			.buf = (uint8_t *)sdhc_ones,
			.len = len
		}
	};

	const struct spi_buf_set tx = {
		.buffers = tx_bufs,
		.count = 1,
	};
	const struct spi_buf_set *tx_ptr = &tx;
#else
	const struct spi_buf_set *tx_ptr = NULL;
#endif /* ANY_INST_REQUIRES_EXPLICIT_FF */

	struct spi_buf rx_bufs[] = {
		{
			.buf = buf,
			.len = len
		}
	};

	const struct spi_buf_set rx = {
		.buffers = rx_bufs,
		.count = 1,
	};

	return spi_transceive(spi_dev, spi_cfg, tx_ptr, &rx);
}

static int sdhc_spi_init_card(const struct device *dev)
{
	/* SD spec requires at least 74 clocks be send to SD to start it.
	 * for SPI protocol, this will be performed by sending 10 0xff values
	 * to the card (this should result in 80 SCK cycles)
	 */
	const struct sdhc_spi_config *config = dev->config;
	struct sdhc_spi_data *data = dev->data;
	struct spi_config *spi_cfg = data->spi_cfg;
	int ret, ret2;

	if (spi_cfg->frequency == 0) {
		/* Use default 400KHZ frequency */
		spi_cfg->frequency = SDMMC_CLOCK_400KHZ;
	}

	/* Request SPI bus to be active */
	if (pm_device_runtime_get(config->spi_dev) < 0) {
		return -EIO;
	}

	/* the initial 74 clocks must be sent while CS is high */
	spi_cfg->operation |= SPI_CS_ACTIVE_HIGH;
	ret = sdhc_spi_rx(config->spi_dev, spi_cfg, data->scratch, 10);

	/* Release lock on SPI bus */
	ret2 = spi_release(config->spi_dev, spi_cfg);
	spi_cfg->operation &= ~SPI_CS_ACTIVE_HIGH;

	/* Release request for SPI bus to be active */
	(void)pm_device_runtime_put(config->spi_dev);

	return ret ? ret : ret2;
}

/* Checks if SPI SD card is sending busy signal */
static int sdhc_spi_card_busy(const struct device *dev)
{
	const struct sdhc_spi_config *config = dev->config;
	struct sdhc_spi_data *data = dev->data;
	int ret;
	uint8_t response;


	ret = sdhc_spi_rx(config->spi_dev, data->spi_cfg, &response, 1);
	if (ret) {
		return -EIO;
	}

	if (response == 0xFF) {
		return 0;
	} else {
		return 1;
	}
}

/* Waits for SPI SD card to stop sending busy signal */
static int sdhc_spi_wait_unbusy(const struct device *dev,
	int timeout_ms,
	int interval_ticks)
{
	const struct sdhc_spi_config *config = dev->config;
	struct sdhc_spi_data *data = dev->data;
	int ret;
	uint8_t response;

	while (timeout_ms > 0) {
		ret = sdhc_spi_rx(config->spi_dev, data->spi_cfg, &response, 1);
		if (ret) {
			return ret;
		}
		if (response == 0xFF) {
			return 0;
		}
		k_msleep(k_ticks_to_ms_floor32(interval_ticks));
		timeout_ms -= k_ticks_to_ms_floor32(interval_ticks);
	}
	return -ETIMEDOUT;
}


/* Read SD command from SPI response */
static int sdhc_spi_response_get(const struct device *dev, struct sdhc_command *cmd,
	int rx_len)
{
	const struct sdhc_spi_config *config = dev->config;
	struct sdhc_spi_data *dev_data = dev->data;
	uint8_t *response = dev_data->scratch;
	uint8_t *end = response + rx_len;
	int ret, timeout = cmd->timeout_ms;
	uint8_t value, i;

	/* First step is finding the first valid byte of the response.
	 * All SPI responses start with R1, which will have MSB of zero.
	 * we know we can ignore the first 7 bytes, which hold the command and
	 * initial "card ready" byte.
	 */
	response += 8;
	while (response < end && ((*response & SD_SPI_START) == SD_SPI_START)) {
		response++;
	}
	if (response == end) {
		/* Some cards are slow, and need more time to respond. Continue
		 * with single byte reads until the card responds.
		 */
		response = dev_data->scratch;
		end = response + 1;
		while (timeout > 0) {
			ret = sdhc_spi_rx(config->spi_dev, dev_data->spi_cfg,
				response, 1);
			if (ret < 0) {
				return ret;
			}
			if (*response != 0xff) {
				break;
			}
			/* Delay for a bit, and poll the card again */
			k_msleep(10);
			timeout -= 10;
		}
		if (*response == 0xff) {
			return -ETIMEDOUT;
		}
	}
	/* Record R1 response */
	cmd->response[0] = *response++;
	/* Check response for error */
	if (cmd->response[0] != 0) {
		if (cmd->response[0] &	(SD_SPI_R1PARAMETER_ERR | SD_SPI_R1ADDRESS_ERR)) {
			return -EFAULT; /* Bad address */
		} else if (cmd->response[0] & (SD_SPI_R1ILLEGAL_CMD_ERR)) {
			return -EINVAL; /* Invalid command */
		} else if (cmd->response[0] & (SD_SPI_R1CMD_CRC_ERR)) {
			return -EILSEQ; /* Illegal byte sequence */
		} else if (cmd->response[0] & (SD_SPI_R1ERASE_SEQ_ERR | SD_SPI_R1ERASE_RESET)) {
			return -EIO;
		}
		/* else IDLE_STATE bit is set, which is not an error, card is just resetting */
	}
	switch ((cmd->response_type & SDHC_SPI_RESPONSE_TYPE_MASK)) {
	case SD_SPI_RSP_TYPE_R1:
		/* R1 response - one byte*/
		break;
	case SD_SPI_RSP_TYPE_R1b:
		/* R1b response - one byte plus busy signal */
		/* Read remaining bytes to see if card is still busy.
		 * card will be ready when it stops driving data out
		 * low.
		 */
		while (response < end && (*response == 0x0)) {
			response++;
		}
		if (response == end) {
			value = cmd->timeout_ms;
			response--;
			/* Periodically check busy line */
			ret = sdhc_spi_wait_unbusy(dev,
				SPI_R1B_TIMEOUT_MS, 1000);
		}
		break;
	case SD_SPI_RSP_TYPE_R2:
	case SD_SPI_RSP_TYPE_R5:
		/* R2/R5 response - R1 response + 1 byte*/
		if (response == end) {
			response = dev_data->scratch;
			end = response + 1;
			/* Read the next byte */
			ret = sdhc_spi_rx(config->spi_dev,
				dev_data->spi_cfg,
				response, 1);
			if (ret) {
				return ret;
			}
		}
		cmd->response[0] = (*response) << 8;
		break;
	case SD_SPI_RSP_TYPE_R3:
	case SD_SPI_RSP_TYPE_R4:
	case SD_SPI_RSP_TYPE_R7:
		/* R3/R4/R7 response - R1 response + 4 bytes */
		cmd->response[1] = 0;
		for (i = 0; i < 4; i++) {
			cmd->response[1] <<= 8;
			/* Read bytes of response */
			if (response == end) {
				response = dev_data->scratch;
				end = response + 1;
				/* Read the next byte */
				ret = sdhc_spi_rx(config->spi_dev,
					dev_data->spi_cfg,
					response, 1);
				if (ret) {
					return ret;
				}
			}
			cmd->response[1] |= *response++;
		}
		break;
	default:
		/* Other RSP types not supported */
		return -ENOTSUP;
	}
	return 0;
}

/* Send SD command using SPI */
static int sdhc_spi_send_cmd(const struct device *dev, struct sdhc_command *cmd,
	bool data_present)
{
	const struct sdhc_spi_config *config = dev->config;
	struct sdhc_spi_data *dev_data = dev->data;
	int err;
	uint8_t *cmd_buf;
	/* To reduce overhead, we will send entire command in one SPI
	 * transaction. The packet takes the following format:
	 * - all ones byte to ensure card is ready
	 * - opcode byte (which includes start and transmission bits)
	 * - 4 bytes for argument
	 * - crc7 byte (with end bit)
	 * The SD card can take up to 8 bytes worth of SCLK cycles to respond.
	 * therefore, we provide 8 bytes of all ones, to read data from the card.
	 * the maximum spi response length is 5 bytes, so we provide an
	 * additional 5 bytes of data, leaving us with 13 bytes of 0xff.
	 * Finally, we send a padding byte of all 0xff, to ensure that
	 * the card recives at least one 0xff byte before next command.
	 */

	/* Note: we can discard CMD data as we send it,
	 * so resuse the TX buf as RX
	 */
	struct spi_buf bufs[] = {
		{
			.buf = dev_data->scratch,
			.len = sizeof(dev_data->scratch),
		},
	};

	const struct spi_buf_set buf_set = {
		.buffers = bufs,
		.count = 1,
	};


	if (data_present) {
		/* We cannot send extra SCLK cycles with our command,
		 * since we'll miss the data the card responds with. We
		 * send one 0xff byte, six command bytes, two additional 0xff
		 * bytes, since the min value of NCR (see SD SPI timing
		 * diagrams) is one, and we know there will be an R1 response.
		 */
		bufs[0].len = SD_SPI_CMD_SIZE + 3;
	}
	memset(dev_data->scratch, 0xFF, sizeof(dev_data->scratch));
	cmd_buf = dev_data->scratch + 1;

	/* Command packet holds the following bits:
	 * [47]: start bit, 0b0
	 * [46]: transmission bit, 0b1
	 * [45-40]: command index
	 * [39-8]: argument
	 * [7-1]: CRC
	 * [0]: end bit, 0b1
	 * Note that packets are sent MSB first.
	 */
	/* Add start bit, tx bit, and cmd opcode */
	cmd_buf[0] = (cmd->opcode & SD_SPI_CMD);
	cmd_buf[0] = ((cmd_buf[0] | SD_SPI_TX) & ~SD_SPI_START);
	/* Add argument */
	sys_put_be32(cmd->arg, &cmd_buf[1]);
	/* Add CRC, and set LSB as the end bit */
	cmd_buf[SD_SPI_CMD_BODY_SIZE] = crc7_be(0, cmd_buf, SD_SPI_CMD_BODY_SIZE) | 0x1;
	LOG_DBG("cmd%d arg 0x%x", cmd->opcode, cmd->arg);
	/* Set data, will lock SPI bus */
	err = spi_transceive(config->spi_dev, dev_data->spi_cfg, &buf_set, &buf_set);
	if (err != 0) {
		return err;
	}
	/* Read command response */
	return sdhc_spi_response_get(dev, cmd, bufs[0].len);
}

/* Skips bytes in SDHC data stream. */
static int sdhc_skip(const struct device *dev, uint8_t skip_val)
{
	const struct sdhc_spi_config *config = dev->config;
	struct sdhc_spi_data *data = dev->data;
	uint8_t buf;
	int ret;
	uint32_t retries = SD_SPI_SKIP_RETRIES;

	do {
		ret = sdhc_spi_rx(config->spi_dev, data->spi_cfg,
			&buf, sizeof(buf));
		if (ret) {
			return ret;
		}
	} while (buf == skip_val && retries--);
	if (retries == 0) {
		return -ETIMEDOUT;
	}
	/* Return first non-skipped value */
	return buf;
}

/* Handles reading data from SD SPI device */
static int sdhc_spi_read_data(const struct device *dev, struct sdhc_data *data)
{
	const struct sdhc_spi_config *config = dev->config;
	struct sdhc_spi_data *dev_data = dev->data;
	uint8_t *read_location = data->data;
	uint32_t remaining = data->blocks;
	int ret;
	uint8_t crc[SD_SPI_CRC16_SIZE + 1];

#if ANY_INST_REQUIRES_EXPLICIT_FF
	/* If the driver requires explicit 0xFF bytes on receive, we
	 * are limited to receiving the size of the sdhc_ones buffer
	 */
	if (data->block_size > sizeof(sdhc_ones)) {
		return -ENOTSUP;
	}

	const struct spi_buf tx_bufs[] = {
		{
			.buf = (uint8_t *)sdhc_ones,
			.len = data->block_size,
		},
	};

	const struct spi_buf_set tx = {
		.buffers = tx_bufs,
		.count = 1,
	};
	const struct spi_buf_set *tx_ptr = &tx;
#else
	const struct spi_buf_set *tx_ptr = NULL;
#endif /* ANY_INST_REQUIRES_EXPLICIT_FF */

	struct spi_buf rx_bufs[] = {
		{
			.buf = read_location,
			.len = data->block_size,
		}
	};

	const struct spi_buf_set rx = {
		.buffers = rx_bufs,
		.count = 1,
	};


	/* Read bytes until data stream starts. SD will send 0xff until
	 * data is available
	 */
	ret = sdhc_skip(dev, 0xff);
	if (ret < 0) {
		return ret;
	}
	/* Check token */
	if (ret != SD_SPI_TOKEN_SINGLE)	{
		return -EIO;
	}

	/* Read blocks until we are out of data */
	while (remaining--) {
		ret = spi_transceive(config->spi_dev,
			dev_data->spi_cfg, tx_ptr, &rx);
		if (ret) {
			LOG_ERR("Data write failed");
			return ret;
		}
		/* Read CRC16 plus one end byte */
		ret = sdhc_spi_rx(config->spi_dev, dev_data->spi_cfg,
			crc, sizeof(crc));
		if (crc16_itu_t(0, read_location, data->block_size) !=
			sys_get_be16(crc)) {
			/* Bad CRC */
			LOG_ERR("Bad data CRC");
			return -EILSEQ;
		}
		/* Advance read location */
		read_location += data->block_size;
		rx_bufs[0].buf = read_location;
		if (remaining) {
			/* Check next data token */
			ret = sdhc_skip(dev, 0xff);
			if (ret != SD_SPI_TOKEN_SINGLE) {
				LOG_ERR("Bad token");
				return -EIO;
			}
		}
	}
	return ret;
}

/* Handles writing data to SD SPI device */
static int sdhc_spi_write_data(const struct device *dev, struct sdhc_data *data)
{
	const struct sdhc_spi_config *config = dev->config;
	struct sdhc_spi_data *dev_data = dev->data;
	int ret;
	uint8_t token, resp;
	uint8_t *write_location = data->data, crc[SD_SPI_CRC16_SIZE];
	uint32_t remaining = data->blocks;

	struct spi_buf tx_bufs[] = {
		{
			.buf = &token,
			.len = sizeof(uint8_t),
		},
		{
			.buf = write_location,
			.len = data->block_size,
		},
		{
			.buf = crc,
			.len = sizeof(crc),
		},
	};

	struct spi_buf_set tx = {
		.buffers = tx_bufs,
		.count = 3,
	};

	/* Set the token- single block reads use different token
	 * than multibock
	 */
	if (remaining > 1) {
		token = SD_SPI_TOKEN_MULTI_WRITE;
	} else {
		token = SD_SPI_TOKEN_SINGLE;
	}

	while (remaining--) {
		/* Build the CRC for this data block */
		sys_put_be16(crc16_itu_t(0, write_location, data->block_size),
			crc);
		ret = spi_write(config->spi_dev, dev_data->spi_cfg, &tx);
		if (ret) {
			return ret;
		}
		/* Read back the data response token from the card */
		ret = sdhc_spi_rx(config->spi_dev, dev_data->spi_cfg,
			&resp, sizeof(resp));
		if (ret) {
			return ret;
		}
		/* Check response token */
		if ((resp & 0xF) != SD_SPI_RESPONSE_ACCEPTED) {
			if ((resp & 0xF) == SD_SPI_RESPONSE_CRC_ERR) {
				return -EILSEQ;
			} else if ((resp & 0xF) == SD_SPI_RESPONSE_WRITE_ERR) {
				return -EIO;
			}
			LOG_DBG("Unknown write response token 0x%x", resp);
			return -EIO;
		}
		/* Advance write location */
		write_location += data->block_size;
		tx_bufs[1].buf = write_location;
		/* Wait for card to stop being busy */
		ret = sdhc_spi_wait_unbusy(dev, data->timeout_ms, 0);
		if (ret) {
			return ret;
		}
	}
	if (data->blocks > 1) {
		/* Write stop transfer token to card */
		token = SD_SPI_TOKEN_STOP_TRAN;
		tx.count = 1;
		ret = spi_write(config->spi_dev, dev_data->spi_cfg, &tx);
		if (ret) {
			return ret;
		}
		/* Wait for card to stop being busy */
		ret = sdhc_spi_wait_unbusy(dev, data->timeout_ms, 0);
		if (ret) {
			return ret;
		}
	}
	return 0;
}

static int sdhc_spi_request(const struct device *dev,
	struct sdhc_command *cmd,
	struct sdhc_data *data)
{
	const struct sdhc_spi_config *config = dev->config;
	struct sdhc_spi_data *dev_data = dev->data;
	int ret, ret2, stop_ret, retries = cmd->retries;
	const struct sdhc_command stop_cmd = {
		.opcode = SD_STOP_TRANSMISSION,
		.arg = 0,
		.response_type = SD_SPI_RSP_TYPE_R1b,
		.timeout_ms = 1000,
		.retries = 1,
	};

	/* Request SPI bus to be active */
	if (pm_device_runtime_get(config->spi_dev) < 0) {
		return -EIO;
	}

	if (data == NULL) {
		do {
			ret = sdhc_spi_send_cmd(dev, cmd, false);
		} while ((ret != 0) && (retries-- > 0));
	} else {
		do {
			retries--;
			ret = sdhc_spi_send_cmd(dev, cmd, true);
			if (ret) {
				continue;
			}
			if ((cmd->opcode == SD_WRITE_SINGLE_BLOCK) ||
				(cmd->opcode == SD_WRITE_MULTIPLE_BLOCK)) {
				ret = sdhc_spi_write_data(dev, data);
			} else {
				ret = sdhc_spi_read_data(dev, data);
			}
			if (ret || (cmd->opcode == SD_READ_MULTIPLE_BLOCK)) {
				int stop_retries = cmd->retries;

				/* CMD12 is required after multiple read, or
				 * to retry failed transfer
				 */
				stop_ret = sdhc_spi_send_cmd(dev,
					(struct sdhc_command *)&stop_cmd,
					false);
				while ((stop_ret != 0) && (stop_retries > 0)) {
					/* Retry stop command */
					ret = stop_ret = sdhc_spi_send_cmd(dev,
						(struct sdhc_command *)&stop_cmd,
						false);
					stop_retries--;
				}
			}
		} while ((ret != 0) && (retries > 0));
	}

	/* Release SPI bus */
	ret2 = spi_release(config->spi_dev, dev_data->spi_cfg);

	/* Release request for SPI bus to be active */
	(void)pm_device_runtime_put(config->spi_dev);

	return ret ? ret : ret2;
}

static int sdhc_spi_set_io(const struct device *dev, struct sdhc_io *ios)
{
	const struct sdhc_spi_config *cfg = dev->config;
	struct sdhc_spi_data *data = dev->data;

	if (ios->clock != data->spi_cfg->frequency) {
		if (ios->clock > cfg->spi_max_freq) {
			return -ENOTSUP;
		}
		/* Because pointer comparision is used, we have to
		 * swap to a new configuration structure to reconfigure SPI.
		 */
		if (ios->clock != 0) {
			if (data->spi_cfg == &data->cfg_a) {
				data->cfg_a.frequency = ios->clock;
				memcpy(&data->cfg_b, &data->cfg_a,
					sizeof(struct spi_config));
				data->spi_cfg = &data->cfg_b;
			} else {
				data->cfg_b.frequency = ios->clock;
				memcpy(&data->cfg_a, &data->cfg_b,
					sizeof(struct spi_config));
				data->spi_cfg = &data->cfg_a;
			}
		}
	}
	if (ios->bus_mode != SDHC_BUSMODE_PUSHPULL) {
		/* SPI mode supports push pull */
		return -ENOTSUP;
	}
	if (data->power_mode != ios->power_mode) {
		if (ios->power_mode == SDHC_POWER_ON) {
			/* Send 74 clock cycles to start card */
			if (sdhc_spi_init_card(dev) != 0) {
				LOG_ERR("Card SCLK init sequence failed");
				return -EIO;
			}
		}
		if (cfg->pwr_gpio.port) {
			/* If power control GPIO is defined, toggle SD power */
			if (ios->power_mode == SDHC_POWER_ON) {
				if (gpio_pin_set_dt(&cfg->pwr_gpio, 1)) {
					return -EIO;
				}
				LOG_INF("Powered up");
			} else {
				if (gpio_pin_set_dt(&cfg->pwr_gpio, 0)) {
					return -EIO;
				}
				LOG_INF("Powered down");
			}
		}
		data->power_mode = ios->power_mode;
	}
	if (ios->bus_width != SDHC_BUS_WIDTH1BIT) {
		/* SPI mode supports 1 bit bus */
		return -ENOTSUP;
	}
	if (ios->signal_voltage != SD_VOL_3_3_V) {
		/* SPI mode does not support UHS voltages */
		return -ENOTSUP;
	}
	return 0;
}

static int sdhc_spi_get_card_present(const struct device *dev)
{
	const struct sdhc_spi_config *config = dev->config;

	if (config->cd_gpio.port == NULL) {
		/* No card detect GPIO, assume card is in slot */
		return 1;
	}

	return gpio_pin_get_dt(&config->cd_gpio);
}

static void sdhc_spi_cd_gpio_cb(const struct device *port,
				struct gpio_callback *cb,
				gpio_port_pins_t pins)
{
	struct sdhc_spi_data *data = CONTAINER_OF(cb, struct sdhc_spi_data, cd_callback);
	const struct device *dev = data->dev;
	const struct sdhc_spi_config *cfg = dev->config;

	if (data->sdhc_cb) {
		if (gpio_pin_get_dt(&cfg->cd_gpio)) {
			data->sdhc_cb(dev, SDHC_INT_INSERTED, data->sdhc_cb_user_data);
		} else {
			data->sdhc_cb(dev, SDHC_INT_REMOVED, data->sdhc_cb_user_data);
		}
	}
}

static int sdhc_spi_get_host_props(const struct device *dev,
	struct sdhc_host_props *props)
{
	const struct sdhc_spi_config *cfg = dev->config;

	memset(props, 0, sizeof(struct sdhc_host_props));

	props->f_min = SDMMC_CLOCK_400KHZ;
	props->f_max = cfg->spi_max_freq;
	props->power_delay = cfg->power_delay_ms;
	props->host_caps.vol_330_support = true;
	props->is_spi = true;
	return 0;
}

static int sdhc_spi_reset(const struct device *dev)
{
	struct sdhc_spi_data *data = dev->data;

	/* Reset host I/O */
	data->spi_cfg->frequency = SDMMC_CLOCK_400KHZ;
	return 0;
}

static int sdhc_spi_cd_interrupt_configure(const struct device *dev, int sources) {
	const struct sdhc_spi_config *cfg = dev->config;
	int ret;

	if (cfg->cd_gpio.port) {
		gpio_flags_t flags;
		if ((sources & SDHC_INT_INSERTED) && (sources & SDHC_INT_REMOVED)) {
			flags = GPIO_INT_EDGE_BOTH;
		} else if (sources & SDHC_INT_INSERTED) {
			flags = GPIO_INT_EDGE_TO_ACTIVE;
		} else if (sources & SDHC_INT_REMOVED) {
			flags = GPIO_INT_EDGE_TO_INACTIVE;
		} else {
			flags = GPIO_INT_DISABLE;
		}

		ret = gpio_pin_interrupt_configure_dt(&cfg->cd_gpio, flags);
		if (ret) {
			return ret;
		}
	}
	return 0;
}

static int sdhc_spi_enable_interrupt(const struct device *dev, sdhc_interrupt_cb_t callback,
				     int sources, void *user_data)
{
	const struct sdhc_spi_config *cfg = dev->config;
	struct sdhc_spi_data *data = dev->data;

	/* Only insert/remove interrupts are supported */
	if (sources & ~(SDHC_INT_INSERTED | SDHC_INT_REMOVED)) {
		return -ENOTSUP;
	}

	/* Insert/remove detection requires card detect GPIO */
	if (!cfg->cd_gpio.port) {
		return -ENOTSUP;
	}

	data->sdhc_cb = callback;
	data->sdhc_cb_user_data = user_data;
	data->sdhc_cb_sources |= sources;

	return sdhc_spi_cd_interrupt_configure(dev, data->sdhc_cb_sources);
}

static int sdhc_spi_disable_interrupt(const struct device *dev, int sources)
{
	struct sdhc_spi_data *data = dev->data;

	data->sdhc_cb_sources &= ~sources;

	return sdhc_spi_cd_interrupt_configure(dev, data->sdhc_cb_sources);
}

static int sdhc_spi_init(const struct device *dev)
{
	const struct sdhc_spi_config *cfg = dev->config;
	struct sdhc_spi_data *data = dev->data;
	int ret = 0;

	if (!device_is_ready(cfg->spi_dev)) {
		return -ENODEV;
	}
	data->dev = dev;
	if (cfg->pwr_gpio.port) {
		if (!gpio_is_ready_dt(&cfg->pwr_gpio)) {
			return -ENODEV;
		}
		ret = gpio_pin_configure_dt(&cfg->pwr_gpio, GPIO_OUTPUT_INACTIVE);
		if (ret != 0) {
			LOG_ERR("Could not configure power gpio (%d)", ret);
			return ret;
		}
	}
	if (cfg->cd_gpio.port) {
		if (!gpio_is_ready_dt(&cfg->cd_gpio)) {
			return -ENODEV;
		}
		ret = gpio_pin_configure_dt(&cfg->cd_gpio, GPIO_INPUT);
		if (ret != 0) {
			LOG_ERR("Could not configure CD gpio (%d)", ret);
			return ret;
		}

		gpio_init_callback(&data->cd_callback, sdhc_spi_cd_gpio_cb, BIT(cfg->cd_gpio.pin));
		ret = gpio_add_callback_dt(&cfg->cd_gpio, &data->cd_callback);
		if (ret) {
			return ret;
		}
	}
	data->power_mode = SDHC_POWER_OFF;
	data->spi_cfg = &data->cfg_a;
	data->spi_cfg->frequency = 0;
	return ret;
}

static const struct sdhc_driver_api sdhc_spi_api = {
	.request = sdhc_spi_request,
	.set_io = sdhc_spi_set_io,
	.get_host_props = sdhc_spi_get_host_props,
	.get_card_present = sdhc_spi_get_card_present,
	.reset = sdhc_spi_reset,
	.card_busy = sdhc_spi_card_busy,
	.enable_interrupt = sdhc_spi_enable_interrupt,
	.disable_interrupt = sdhc_spi_disable_interrupt,
};


#define SDHC_SPI_INIT(n)							\
	const struct sdhc_spi_config sdhc_spi_config_##n = {			\
		.spi_dev = DEVICE_DT_GET(DT_INST_PARENT(n)),			\
		.pwr_gpio = GPIO_DT_SPEC_INST_GET_OR(n, pwr_gpios, {0}),	\
		.cd_gpio = GPIO_DT_SPEC_INST_GET_OR(n, cd_gpios, {0}),		\
		.spi_max_freq = DT_INST_PROP(n, spi_max_frequency),		\
		.power_delay_ms = DT_INST_PROP(n, power_delay_ms),		\
	};									\
										\
	struct sdhc_spi_data sdhc_spi_data_##n = {				\
		.cfg_a = SPI_CONFIG_DT_INST(n,					\
				(SPI_LOCK_ON | SPI_HOLD_ON_CS | SPI_WORD_SET(8) \
				 | (DT_INST_PROP(n, spi_clock_mode_cpol) ? SPI_MODE_CPOL : 0) \
				 | (DT_INST_PROP(n, spi_clock_mode_cpha) ? SPI_MODE_CPHA : 0) \
				),\
				0),						\
	};									\
										\
	DEVICE_DT_INST_DEFINE(n,						\
			&sdhc_spi_init,						\
			NULL,							\
			&sdhc_spi_data_##n,					\
			&sdhc_spi_config_##n,					\
			POST_KERNEL,						\
			CONFIG_SDHC_INIT_PRIORITY,				\
			&sdhc_spi_api);

DT_INST_FOREACH_STATUS_OKAY(SDHC_SPI_INIT)
