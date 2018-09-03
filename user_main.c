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
#include <ets_sys.h>
#include <osapi.h>
#include <os_type.h>
#include <user_interface.h>

#include "project_config.h"

#include "clock.h"
#include "max7219.h"
#include "spi.h"

struct station_config wificfg;
static os_timer_t update_timer;

static const struct fontchar clocknums[] = {
	{ .width = 5,
	  .bitmap = { 0x0e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e } },
	{ .width = 3,
	  .bitmap = { 0x02, 0x03, 0x02, 0x02, 0x02, 0x02, 0x02, 0x07 } },
	{ .width = 5,
	  .bitmap = { 0x0e, 0x11, 0x10, 0x10, 0x08, 0x04, 0x02, 0x1f } },
	{ .width = 5,
	  .bitmap = { 0x0e, 0x11, 0x10, 0x0c, 0x10, 0x10, 0x11, 0x0e } },
	{ .width = 6,
	  .bitmap = { 0x10, 0x18, 0x14, 0x12, 0x11, 0x3f, 0x10, 0x10 } },
	{ .width = 5,
	  .bitmap = { 0x1f, 0x01, 0x01, 0x0f, 0x10, 0x10, 0x11, 0x0e } },
	{ .width = 5,
	  .bitmap = { 0x0e, 0x11, 0x01, 0x0f, 0x11, 0x11, 0x11, 0x0e } },
	{ .width = 5,
	  .bitmap = { 0x1f, 0x10, 0x10, 0x08, 0x04, 0x02, 0x02, 0x02 } },
	{ .width = 5,
	  .bitmap = { 0x0e, 0x11, 0x11, 0x0e, 0x11, 0x11, 0x11, 0x0e } },
	{ .width = 5,
	  .bitmap = { 0x0e, 0x11, 0x11, 0x11, 0x1e, 0x10, 0x11, 0x0e } }
};

void ICACHE_FLASH_ATTR update_func(void *arg)
{
	static bool ind = false;
	uint8_t hour, mins;
	uint8_t digits[4], position[4];
	struct tm curtime;

	max7219_clear();
	ind = !ind;

	/* Draw the middle : */
	if (ind) {
		max7219_set_pixel(15, 1, true);
		max7219_set_pixel(15, 2, true);
		max7219_set_pixel(15, 5, true);
		max7219_set_pixel(15, 6, true);
		max7219_set_pixel(16, 1, true);
		max7219_set_pixel(16, 2, true);
		max7219_set_pixel(16, 5, true);
		max7219_set_pixel(16, 6, true);
	}

	breakdown_time(get_time(), &curtime);
	mins = curtime.tm_min;
	hour = curtime.tm_hour;

	digits[0] = hour / 10;
	digits[1] = hour % 10;
	digits[2] = mins / 10;
	digits[3] = mins % 10;

	/*
	 * We want our numbers to use as much of the LED matrix as possible,
	 * and the displayed time to be centred on the display, so we do our
	 * own positioning and blitting instead of using max7219_print.
	 */
	position[1] = 14 - clocknums[digits[1]].width;
	position[0] = position[1] - clocknums[digits[0]].width - 1;
	position[2] = 18;
	position[3] = position[2] + clocknums[digits[2]].width + 1;

	max7219_blit(position[0], 0, clocknums[digits[0]].bitmap,
		clocknums[digits[0]].width, 8);
	max7219_blit(position[1], 0, clocknums[digits[1]].bitmap,
		clocknums[digits[1]].width, 8);
	max7219_blit(position[2], 0, clocknums[digits[2]].bitmap,
		clocknums[digits[2]].width, 8);
	max7219_blit(position[3], 0, clocknums[digits[3]].bitmap,
		clocknums[digits[3]].width, 8);

	max7219_show();
}

void ICACHE_FLASH_ATTR wifi_callback(System_Event_t *evt)
{
	switch (evt->event) {
	case EVENT_STAMODE_CONNECTED:
	case EVENT_STAMODE_DISCONNECTED:
		break;
	case EVENT_STAMODE_GOT_IP:
		ntp_get_time();
	default:
		break;
	}
}

void ICACHE_FLASH_ATTR wifi_init(void)
{
	wificfg.bssid_set = 0;
	os_memcpy(&wificfg.ssid, CFG_WIFI_SSID,
		os_strlen(CFG_WIFI_SSID));
	os_memcpy(&wificfg.password, CFG_WIFI_PASSWORD,
		os_strlen(CFG_WIFI_PASSWORD));

	wifi_station_set_hostname("esp8266-clock");
	wifi_set_opmode(STATION_MODE);
	wifi_station_set_config(&wificfg);

	wifi_set_event_handler_cb(wifi_callback);
}

void user_init(void)
{
	/* Fix up UART0 baud rate */
	uart_div_modify(0, UART_CLK_FREQ / 115200);
	os_printf("Starting up.");

	rtc_init();
	gpio_init();

	spi_init();
	max7219_init(BIT12);		/* GPIO12 is CS */
	max7219_print("Booting");
	max7219_show();

	wifi_init();

	os_timer_setfn(&update_timer, update_func, NULL);
	os_timer_arm(&update_timer, 10000 /* 10s */, 1);
}
