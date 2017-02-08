/*
 * Copyright 2017 Jonathan McDowell <noodles@earth.li>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <stdint.h>

#include <os_type.h>
#include <gpio.h>

#include "spi_register.h"

#define spi_busy(spi_no) (READ_PERI_REG(SPI_CMD(spi_no)) & SPI_USR)

#define SPI 0
#define HSPI 1

void ICACHE_FLASH_ATTR spi_init(void)
{
	/* SPI clock = CPU clock/160 [(79 + 1) * 2]= 500KHz */
	WRITE_PERI_REG(SPI_CLOCK(HSPI),
		((79 & SPI_CLKDIV_PRE) << SPI_CLKDIV_PRE_S) |
		((1 & SPI_CLKCNT_N) << SPI_CLKCNT_N_S) |
		((1 & SPI_CLKCNT_H) << SPI_CLKCNT_H_S) |
		((0 & SPI_CLKCNT_L) << SPI_CLKCNT_L_S));

	/*
	 * We use hardware SPI, but repurpose MISO for CS, as we don't need it.
	 */
	WRITE_PERI_REG(PERIPHS_IO_MUX, 0x105);
	PIN_FUNC_SELECT(PERIPHS_IO_MUX_MTCK_U, 2);	/* GPIO13: MOSI */
	PIN_FUNC_SELECT(PERIPHS_IO_MUX_MTMS_U, 2);	/* GPIO14: CLK  */
	/* FIXME: CS/SS is not this pin */
	PIN_FUNC_SELECT(PERIPHS_IO_MUX_MTDO_U, 2);	/* GPIO15 */
	/* We don't use MISO, repurpose this pin for GPIO12 (used as CS) */
	PIN_FUNC_SELECT(PERIPHS_IO_MUX_MTDI_U, FUNC_GPIO12);
	PIN_PULLUP_DIS(PERIPHS_IO_MUX_MTDI_U);
	gpio_output_set(0, 0, BIT12, 0);

	/* Configure up our SPI options */
	SET_PERI_REG_MASK(SPI_USER(HSPI),
		SPI_WR_BYTE_ORDER |	/* MSB output first */
		SPI_USR_MOSI		/* Enable data (MOSI) output */
	);
	CLEAR_PERI_REG_MASK(SPI_USER(HSPI),
		SPI_FLASH_MODE |		/* Disable flash mode */
		SPI_USR_MISO | SPI_USR_COMMAND |
		SPI_USR_ADDR | SPI_USR_DUMMY |	/* Disable non-data output */
		SPI_CS_SETUP | SPI_CS_HOLD |	/* We drive our own CS */
		SPI_CK_OUT_EDGE			/* Data valid on CLK leading */
	);
	/* Clock low when inactive */
	CLEAR_PERI_REG_MASK(SPI_PIN(HSPI), SPI_IDLE_EDGE);

	/* We're writing 8 bits at a time */
	/* TODO: We could write up to 32 bits at once potentially. */
	WRITE_PERI_REG(SPI_USER1(HSPI),
		(7 & SPI_USR_MOSI_BITLEN) << SPI_USR_MOSI_BITLEN_S);
}

void ICACHE_FLASH_ATTR spi_write(size_t len, uint8_t *data)
{
	int i;

	for (i = 0; i < len; i++) {
		/* Wait for SPI to be ready */
		while (spi_busy(HSPI));

		/* We just write the top 8 bits of the 32 bit value */
		WRITE_PERI_REG(SPI_W0(HSPI), data[i] << 24);

		/* Begin the SPI transaction */
		SET_PERI_REG_MASK(SPI_CMD(HSPI), SPI_USR);
	}

	/* Don't return until it's done */
	while (spi_busy(HSPI));
}
