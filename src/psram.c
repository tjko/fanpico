/* psram.c
   Copyright (C) 2025 Timo Kokkonen <tjko@iki.fi>

   SPDX-License-Identifier: GPL-3.0-or-later

   This file is part of FanPico.

   FanPico is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   FanPico is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with FanPico. If not, see <https://www.gnu.org/licenses/>.
*/

#include <stdio.h>
#include <string.h>
#if !PICO_RP2040
#include "hardware/structs/qmi.h"
#endif
#include "hardware/structs/ioqspi.h"
#include "hardware/structs/xip_ctrl.h"
#include "hardware/gpio.h"
#include "hardware/clocks.h"
#include "hardware/sync.h"

#include "psram.h"


/* PSRAM Manufacturer IDs */
#define MFID_APMEMORY         0x0d
#define MFID_ISSI             0x9d

/* PSRAM Command Codes */
#define CMD_READ              0x03
#define CMD_FAST_READ         0x0b
#define CMD_QUAD_READ         0xeb
#define CMD_WRITE             0x02
#define CMD_QUAD_WRITE        0x38
#define CMD_ENTER_QPI_MODE    0x35
#define CMD_EXIT_QPI_MODE     0xf5
#define CMD_RESET_ENABLE      0x66
#define CMD_RESET             0x99
#define CMD_SET_BURST_LEN     0xc0
#define CMD_READ_ID           0x9f
#define CMD_DPD_MODE_ENTRY    0xb9

/* Offsets to Read ID command response (8 bytes) */
#define MFID_OFFSET           0
#define KGD_OFFSET            1
#define EID_OFFSET            2  /* 6 bytes */

/* KGD (Known Good Die) Test */
#define KGD_FAIL              0x55
#define KGD_PASS              0x5d

#define PSRAM_MAX_CSR_CLK                  5000000 /* 5MHz max clock for "direct" mode */

/* PSRAM Default Timings */
#define PSRAM_DEFAULT_MAX_CLK            109000000 /* 109MHz max clock rate */
#define PSRAM_DEFAULT_MAX_SELECT_FS     8000000000 /* 8us = 8e9 fs (tCEM) */
#define PSRAM_DEFAULT_MIN_DESELECT_FS     50000000 /* 50ns = 50e6 fs  (tCPH) */

/* Vedor Specific Timings */

/* AP Memory */
#define PSRAM_APMEMORY_MIN_DESELECT_FS    18000000 /* 18ns = 18e6 fs (tCPH) */

/* ISSI */
#define PSRAM_ISSI_MAX_CLK               104000000 /* 104MHz max clock rate */
#define PSRAM_ISSI_MAX_SELECT_FS        4000000000 /* 4us = 4e9 fs (tCEM) */
#define PSRAM_ISSI_MIN_DESELECT_CS               1 /* 1 clock cycle */


#ifdef PSRAM_CS_PIN

static size_t psram_sz = 0;
static char *psram_mf = NULL;
static char psram_id_str_buf[16 + 1] = { 0 };



static inline void csr_busy_wait()
{
	/* Wait for BUSY flag to clear */
	while((qmi_hw->direct_csr & QMI_DIRECT_CSR_BUSY_BITS)) {
	};
}

static inline void csr_txempty_wait()
{
	/* Wait for TXEMPTY flag to get set */
	while((qmi_hw->direct_csr & QMI_DIRECT_CSR_TXEMPTY_BITS) == 0) {
	};
}

static inline void csr_enable_direct_mode(uint8_t csr_clkdiv)
{
	/* Enable CRS mode and set clock divider */
	qmi_hw->direct_csr = csr_clkdiv << QMI_DIRECT_CSR_CLKDIV_LSB | QMI_DIRECT_CSR_EN_BITS;
	csr_busy_wait();
}

static inline void csr_disable_direct_mode()
{
	/* Disable direct mode */
	hw_clear_bits(&qmi_hw->direct_csr, QMI_DIRECT_CSR_EN_BITS | QMI_DIRECT_CSR_ASSERT_CS1N_BITS);
}


static void __no_inline_not_in_flash_func(csr_send_command)(uint32_t cmd, uint16_t *rx)
{
	uint16_t res;

	hw_set_bits(&qmi_hw->direct_csr, QMI_DIRECT_CSR_ASSERT_CS1N_BITS);
	qmi_hw->direct_tx = cmd;
	csr_txempty_wait();
	csr_busy_wait();
	res = qmi_hw->direct_rx;
	hw_clear_bits(&qmi_hw->direct_csr, QMI_DIRECT_CSR_ASSERT_CS1N_BITS);
	if (rx)
		*rx = res;
}


static void __no_inline_not_in_flash_func(psram_read_id)(uint8_t csr_clkdiv, uint8_t *psram_id)
{
	uint32_t saved_ints = save_and_disable_interrupts();
	uint8_t rx;

	csr_enable_direct_mode(csr_clkdiv);

	/* Send 'Exit QPI mode' command */
	csr_send_command(
		QMI_DIRECT_TX_OE_BITS |
		QMI_DIRECT_TX_IWIDTH_VALUE_Q << QMI_DIRECT_TX_IWIDTH_LSB |
		CMD_EXIT_QPI_MODE, NULL);

	/* Send 'Read ID' command */
	hw_set_bits(&qmi_hw->direct_csr, QMI_DIRECT_CSR_ASSERT_CS1N_BITS);
	for (int i = 0; i < (4 + 8); i++) {
		qmi_hw->direct_tx = (i == 0 ? CMD_READ_ID : 0x00);
		csr_txempty_wait();
		csr_busy_wait();
		rx = qmi_hw->direct_rx;
		if (i >= 4)
			psram_id[i - 4] = rx;
	}

	csr_disable_direct_mode();

	restore_interrupts(saved_ints);
}


static void __no_inline_not_in_flash_func(psram_qmi_setup)(uint8_t clkdiv, uint8_t csr_clkdiv,
							uint8_t max_select, uint8_t min_deselect,
							uint8_t rxdelay)
{
	uint32_t saved_ints = save_and_disable_interrupts();


	/* Reset PSRAM chip and set it in QPI Mode */

	csr_enable_direct_mode(csr_clkdiv);
	csr_send_command(CMD_RESET_ENABLE, NULL);
	csr_send_command(CMD_RESET, NULL);
	csr_send_command(CMD_ENTER_QPI_MODE, NULL);
	csr_disable_direct_mode();


	/* Configure QSPI Memory Interface */

	/* Set M1_TIMING register */
	qmi_hw->m[1].timing = (
		1                                  << QMI_M1_TIMING_COOLDOWN_LSB |
		QMI_M1_TIMING_PAGEBREAK_VALUE_1024 << QMI_M1_TIMING_PAGEBREAK_LSB |
		0                                  << QMI_M1_TIMING_SELECT_SETUP_LSB |
		3                                  << QMI_M1_TIMING_SELECT_HOLD_LSB |
		max_select                         << QMI_M1_TIMING_MAX_SELECT_LSB |
		min_deselect                       << QMI_M1_TIMING_MIN_DESELECT_LSB |
		rxdelay                            << QMI_M1_TIMING_RXDELAY_LSB |
		clkdiv                             << QMI_M1_TIMING_CLKDIV_LSB
		);

	/* Set M1_RFMT / M1_RCMD registers */
	qmi_hw->m[1].rfmt = (
		0                                  << QMI_M1_RFMT_DTR_LSB |
		QMI_M1_RFMT_DUMMY_LEN_VALUE_24     << QMI_M1_RFMT_DUMMY_LEN_LSB |
		QMI_M1_RFMT_SUFFIX_LEN_VALUE_NONE  << QMI_M1_RFMT_SUFFIX_LEN_LSB |
		QMI_M1_RFMT_PREFIX_LEN_VALUE_8     << QMI_M1_RFMT_PREFIX_LEN_LSB |
		QMI_M1_RFMT_DATA_WIDTH_VALUE_Q     << QMI_M1_RFMT_DATA_WIDTH_LSB |
		QMI_M1_RFMT_DUMMY_WIDTH_VALUE_Q    << QMI_M1_RFMT_DUMMY_WIDTH_LSB |
		QMI_M1_RFMT_SUFFIX_WIDTH_VALUE_Q   << QMI_M1_RFMT_SUFFIX_WIDTH_LSB |
		QMI_M1_RFMT_ADDR_WIDTH_VALUE_Q     << QMI_M1_RFMT_ADDR_WIDTH_LSB |
		QMI_M1_RFMT_PREFIX_WIDTH_VALUE_Q   << QMI_M1_RFMT_PREFIX_WIDTH_LSB
		);
	qmi_hw->m[1].rcmd = (
		0                                  << QMI_M1_RCMD_SUFFIX_LSB |
		CMD_QUAD_READ                      << QMI_M1_RCMD_PREFIX_LSB
		);

	/* Set M1_WFMT / M1_WCMD registers */
	qmi_hw->m[1].wfmt = (
		0                                  << QMI_M1_WFMT_DTR_LSB |
		QMI_M1_WFMT_DUMMY_LEN_VALUE_NONE   << QMI_M1_WFMT_DUMMY_LEN_LSB |
		QMI_M1_WFMT_SUFFIX_LEN_VALUE_NONE  << QMI_M1_WFMT_SUFFIX_LEN_LSB |
		QMI_M1_WFMT_PREFIX_LEN_VALUE_8     << QMI_M1_WFMT_PREFIX_LEN_LSB |
		QMI_M1_WFMT_DATA_WIDTH_VALUE_Q     << QMI_M1_WFMT_DATA_WIDTH_LSB |
		QMI_M1_WFMT_DUMMY_WIDTH_VALUE_Q    << QMI_M1_WFMT_DUMMY_WIDTH_LSB |
		QMI_M1_WFMT_SUFFIX_WIDTH_VALUE_Q   << QMI_M1_WFMT_SUFFIX_WIDTH_LSB |
		QMI_M1_WFMT_ADDR_WIDTH_VALUE_Q     << QMI_M1_WFMT_ADDR_WIDTH_LSB |
		QMI_M1_WFMT_PREFIX_WIDTH_VALUE_Q   << QMI_M1_WFMT_PREFIX_WIDTH_LSB
		);
	qmi_hw->m[1].wcmd = (
		0                                  << QMI_M1_WCMD_SUFFIX_LSB |
		CMD_QUAD_WRITE                     << QMI_M1_WCMD_PREFIX_LSB
		);


	restore_interrupts(saved_ints);

	/* Enable writes to PSRAM (XIP) memory region */
	hw_set_bits(&xip_ctrl_hw->ctrl, XIP_CTRL_WRITABLE_M1_BITS);
}


static bool psram_check_size(void *base_addr, uint32_t size)
{
	volatile uint32_t *psram = (volatile uint32_t*)base_addr;
	uint32_t psram_len = size / sizeof(uint32_t);


	if (size > PSRAM_WINDOW_SIZE)
		return false;
	if (size == PSRAM_WINDOW_SIZE)
		return true;

	/* Clear out beginning and end of the PSRAM */
	psram[0] = 0;
	psram[psram_len - 1] = 0;

	/* Write 4 bytes so that 2 bytes goes past the 'end' of PSRAM */
	memcpy(base_addr + size - 2, "ABCD", 4);

	/* Check if we see the 2 bytes written past end of the PSRAM at the beginning */
	if (psram[0] != 0x00004443 || psram[psram_len -1] != 0x42410000)
		return false;

	return true;
}


static int psram_init(int pin, bool clear_memory)
{
	uint32_t sys_clk = clock_get_hz(clk_sys);
	uint32_t clock_period_fs = 1000000000000000ll / sys_clk;
	uint8_t psram_id[8] = { 0 };
	int ret = 0;

	/* Default PSRAM Timings */
	uint32_t psram_max_clk = PSRAM_DEFAULT_MAX_CLK;
	uint64_t psram_max_select_fs = PSRAM_DEFAULT_MAX_SELECT_FS;
	uint32_t psram_min_deselect_fs = PSRAM_DEFAULT_MIN_DESELECT_FS;
	int psram_min_deselect_cs = -1;
	uint8_t csr_clkdiv = (sys_clk + PSRAM_MAX_CSR_CLK - 1) / PSRAM_MAX_CSR_CLK;


	psram_sz = 0;
	if (pin < 0)
		return -1;

	/* Configure GPIO pin */
	gpio_set_function(pin, GPIO_FUNC_XIP_CS1);

	/* Check if PSRAM chip is present */
	psram_read_id(csr_clkdiv, psram_id);
	if (psram_id[KGD_OFFSET] != KGD_PASS) {
		/* No PSRAM chip found */
		return -2;
	}
	snprintf(psram_id_str_buf, sizeof(psram_id_str_buf),
		"%02x%02x%02x%02x%02x%02x%02x%02x",
		psram_id[0], psram_id[1], psram_id[2], psram_id[3],
		psram_id[4], psram_id[5], psram_id[6], psram_id[7]);

	/* Density EID[47:45] (encoding of this is manufacturer specific)  */
	uint8_t density = (psram_id[EID_OFFSET] >> 5);

	/* Try to determine PSRAM size and characteristics */
	switch (psram_id[MFID_OFFSET]) {
	case MFID_APMEMORY:
		psram_mf = "AP Memory";
		psram_sz = 2;
		psram_min_deselect_fs = PSRAM_APMEMORY_MIN_DESELECT_FS;
		if (density == 1)
			psram_sz = 4;
		else if (density == 2)
			psram_sz = 8;
		break;
	case MFID_ISSI:
		psram_mf = "ISSI";
		psram_max_clk = PSRAM_ISSI_MAX_CLK;
		psram_max_select_fs = PSRAM_ISSI_MAX_SELECT_FS;
		psram_min_deselect_cs = PSRAM_ISSI_MIN_DESELECT_CS;
	psram_sz = 1;
		if (density == 1)
			psram_sz = 2;
		else if (density == 2)
			psram_sz = 4;
		break;
	default:
		psram_mf = "Unknown";
		psram_sz = 1;
	}

	/* Convert size from megabytes to bytes */
	psram_sz <<= 20;


	/* Calculate clock divider to get as close as possible to the max supported clock speed */
	uint8_t clkdiv = (sys_clk + psram_max_clk - 1) / psram_max_clk;

	/* Calculate PSRAM timings */
	uint8_t max_select = (psram_max_select_fs >> 6) / clock_period_fs;
	uint8_t min_deselect = (psram_min_deselect_fs + clock_period_fs - 1) / clock_period_fs;
	if (psram_min_deselect_cs >= 0)
		min_deselect = psram_min_deselect_cs;
	uint8_t rxdelay = 1 + (sys_clk > 150000000 ? clkdiv : 1);


	/* Enable PSRAM */
	psram_qmi_setup(clkdiv , csr_clkdiv, max_select, min_deselect, rxdelay);


	/* Test that we can write to PSRAM */
	volatile uint32_t *psram = (volatile uint32_t*)PSRAM_NOCACHE_BASE;
	psram[0] = 0xdeadc0de;
	volatile uint32_t readback_1 = psram[0];
	psram[0] = 0;
	volatile uint32_t readback_2 = psram[0];
	if (readback_1 != 0xdeadc0de || readback_2 != 0) {
		/* Cannot write to PSRAM! */
		psram_sz = 0;
		return -3;
	}

	/* Validate PSRAM size determined from the Read ID command */
	if (!psram_check_size((void*)PSRAM_NOCACHE_BASE, psram_sz))  {
		/* Size mismatch, try to determine actual size... */
		uint32_t actual_size = PSRAM_WINDOW_SIZE;
		for (int i = 1; i < 16;  i <<= 1) {
			if (psram_check_size((void*)PSRAM_NOCACHE_BASE, i << 20)) {
				actual_size = i << 20;
				break;
			}
		}
		psram_sz = actual_size;
		ret = -4;
	}

	/* Clear PSRAM */
	if (clear_memory)
		memset((void*)PSRAM_NOCACHE_BASE, 0, psram_sz);

	return ret;
}
#endif


void psram_setup()
{
#ifdef PSRAM_CS_PIN
	int res = psram_init(PSRAM_CS_PIN, true);
	if (res == -3) {
		printf("PSRAM: Cannot write to PSRAM!\n");
	}
	else if (res == -4) {
		printf("PSRAM: Memory size mismatch!\n");
	}
#endif
}


size_t psram_size()
{
#ifdef PSRAM_CS_PIN
	return psram_sz;
#else
	return 0;
#endif
}


const char* psram_manufacturer()
{
#ifdef PSRAM_CS_PIN
	return psram_mf;
#else
	return NULL;
#endif
}


const char* psram_id_str()
{
#ifdef PSRAM_CS_PIN
	return psram_id_str_buf;
#else
	return NULL;
#endif
}

