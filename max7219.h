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
#ifndef _MAX7219_H_
#define _MAX7219_H_

struct fontchar {
	uint8_t width;
	uint8_t bitmap[8];
};

void ICACHE_FLASH_ATTR max7219_set_pixel(unsigned int x, unsigned int y,
	bool set);
void ICACHE_FLASH_ATTR max7219_blit(unsigned int x, unsigned int y,
	const uint8_t *data, unsigned int width, unsigned int height);
void ICACHE_FLASH_ATTR max7219_clear(void);
void ICACHE_FLASH_ATTR max7219_print(const char *str);
void ICACHE_FLASH_ATTR max7219_show(void);
void ICACHE_FLASH_ATTR max7219_init(unsigned int cs);

#endif /* _MAX7219_H_ */
