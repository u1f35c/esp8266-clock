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
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include <ets_sys.h>
#include <osapi.h>
#include <os_type.h>
#include <gpio.h>

#include "max7219.h"
#include "spi.h"

/* This is a small 8x4 font */
#include "font-atari.h"

/* The number of 8x8 displays we have daisy chained together */
#define MAX7219_WIDTH 4

enum max7129_regs {
	NOOP = 0,
	ROW7 = 1,
	ROW6 = 2,
	ROW5 = 3,
	ROW4 = 4,
	ROW3 = 5,
	ROW2 = 6,
	ROW1 = 7,
	ROW0 = 8,
	DECODEMODE = 9,
	INTENSITY = 10,
	SCANLIMIT = 11,
	SHUTDOWN = 12,
	DISPLAYTEST = 15,
};

struct max7219_ctx {
	unsigned int width;
	unsigned int cs;
	uint8_t buf[8 * MAX7219_WIDTH];
};

/* Runtime context */
static struct max7219_ctx ctx;

static void ICACHE_FLASH_ATTR max7219_write_reg(uint8_t reg, uint8_t data)
{
	int block;

	/* Set CS low */
	gpio_output_set(0, ctx.cs, ctx.cs, 0);
	for (block = 0; block < ctx.width; block++) {
		spi_write(1, &reg);
		spi_write(1, &data);
	}
	/* Set CS high to latch data into MAX7219 */
	gpio_output_set(ctx.cs, 0, ctx.cs, 0);
	ets_delay_us(10);
}

void ICACHE_FLASH_ATTR max7219_set_pixel(unsigned int x, unsigned int y,
	bool set)
{
	unsigned int block;

	block = x & 0x38;
	x = x & 7;

	if (set) {
		ctx.buf[y + block] |= 1 << x;
	} else {
		ctx.buf[y + block] &= ~(1 << x);
	}
}

void ICACHE_FLASH_ATTR max7219_blit(unsigned int x, unsigned int y,
	const uint8_t *data, unsigned int width, unsigned int height)
{
	unsigned int block = x & 0x38;
	unsigned int left_shift = x & 7;
	bool twoblocks = ((left_shift + width) > 8) &&
		((x + width) < (ctx.width * 8));
	unsigned int right_shift;
	unsigned int row;

	if (block >= (ctx.width << 3)) {
		/* We're off the right hand side; do nothing */
		return;
	}

	if (twoblocks) {
		if (block > (ctx.width << 3)) {
			/* 2nd block is off the edge; ignore it */
			twoblocks = false;
		} else {
			right_shift = (8 - left_shift);
		}
	}

	for (row = 0; row < height; row++)
	{
		if ((y + row) > 7) {
			break;
		}
		ctx.buf[y + row + block] |= ((data[row] << left_shift) & 0xFF);
		if (twoblocks) {
			ctx.buf[y + row + (block + 8)] |=
				(data[row] >> right_shift);
		}
	}
}

void ICACHE_FLASH_ATTR max7219_clear(void)
{
	int i;

	for (i = 0; i < ctx.width * 8;i++)
		ctx.buf[i] = 0;
}

void ICACHE_FLASH_ATTR max7219_show(void)
{
	int y, block;
	uint8_t data[2];

	for (y = 0; y < 8; y++) {
		/* Set CS low */
		gpio_output_set(0, ctx.cs, ctx.cs, 0);
		for (block = ctx.width - 1; block >= 0; block--) {
			data[0] = 8 - y;			/* Row reg */
			data[1] = ctx.buf[y + (block << 3)];	/* Pixel val */
			spi_write(2, data);
		}
		/* Set CS high to latch data into MAX7219 */
		gpio_output_set(ctx.cs, 0, ctx.cs, 0);
		ets_delay_us(10);
	}
}

void ICACHE_FLASH_ATTR max7219_print(const char *str)
{
	int x = 0;
	unsigned char cur;

	while ((cur = (unsigned char) *str++) && x < (ctx.width * 8)) {
		if ((cur > 31) && (cur < 127)) {
			cur -= 32;
			max7219_blit(x, 0, font[cur].bitmap,
				font[cur].width, 8);
			x += font[cur].width + 1;
		}
	}
}

void ICACHE_FLASH_ATTR max7219_init(unsigned int cs)
{
	ctx.width = MAX7219_WIDTH;
	ctx.cs = cs;

	max7219_write_reg(SHUTDOWN, 0);
	max7219_write_reg(DISPLAYTEST, 0);
	max7219_write_reg(SCANLIMIT, 7);
	max7219_write_reg(DECODEMODE, 0);
	max7219_write_reg(INTENSITY, 0);
	max7219_write_reg(SHUTDOWN, 1);

	max7219_clear();
	max7219_show();
}
