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
 *
 * NTP code based on https://github.com/raburton/esp8266/tree/master/ntp
 * Those portions MIT licensed:
 *
 * Copyright (c) 2015 Richard A Burton (richardaburton@gmail.com)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include <stdint.h>

#include <user_interface.h>
#include <espconn.h>
#include <mem.h>
#include <osapi.h>

#include "espmissingincludes.h"

#include "clock.h"

#define NTP_TIMEOUT_MS 5000

static uint32_t sys_last_ticks;
static uint32_t sys_delta;
static os_timer_t ntp_timeout;
static struct espconn *pCon = NULL;

uint8 ntp_server[] = {87, 124, 126, 49};

typedef struct {
	uint8 options;
	uint8 stratum;
	uint8 poll;
	uint8 precision;
	uint32 root_delay;
	uint32 root_disp;
	uint32 ref_id;
	uint8 ref_time[8];
	uint8 orig_time[8];
	uint8 recv_time[8];
	uint8 trans_time[8];
} ntp_t;

void ICACHE_FLASH_ATTR set_time(uint32_t now)
{
	sys_last_ticks = system_get_time();
	sys_delta = now - (sys_last_ticks / 1000000);
}

uint32_t ICACHE_FLASH_ATTR get_time(void)
{
	uint32_t sys_ticks;

	sys_ticks = system_get_time();
	if (sys_ticks < sys_last_ticks) {
		sys_delta += (1ULL << 32ULL) / 1000000;
	}
	sys_last_ticks = sys_ticks;

	return sys_ticks / 1000000 + sys_delta;
}

void ICACHE_FLASH_ATTR breakdown_time(uint32_t time, struct tm *result)
{
	result->tm_sec = time % 60;
	time /= 60;
	result->tm_min = time % 60;
	time /= 60;
	result->tm_hour = time % 24;
	time /= 24;

	result->tm_year = time / (365 * 4 + 1) * 4 + 70;
	time %= 365 * 4 + 1;
}

static void ICACHE_FLASH_ATTR ntp_udp_timeout(void *arg) {
	os_timer_disarm(&ntp_timeout);
	os_printf("NTP timeout.\n");

	// clean up connection
	if (pCon) {
		espconn_delete(pCon);
		os_free(pCon->proto.udp);
		os_free(pCon);
		pCon = 0;
	}
}

static void ICACHE_FLASH_ATTR ntp_udp_recv(void *arg, char *pdata,
	unsigned short len)
{
	uint32_t timestamp;
	ntp_t *ntp;
	struct tm dt;

	os_printf("Got NTP response.\n");

	os_timer_disarm(&ntp_timeout);

	// Extract NTP time
	ntp = (ntp_t *) pdata;
	timestamp = ntp->trans_time[0] << 24 | ntp->trans_time[1] << 16 |
		ntp->trans_time[2] << 8 | ntp->trans_time[3];
	// Convert to Unix time ms
	timestamp -= 2208988800ULL;

	// Store the time
	set_time(timestamp);

	// Print it out
	breakdown_time(timestamp, &dt);
	os_printf("%04d %02d:%02d:%02d (%u)\r\n", dt.tm_year, dt.tm_hour,
		dt.tm_min, dt.tm_sec, timestamp);

	// clean up connection
	if (pCon) {
		espconn_delete(pCon);
		os_free(pCon->proto.udp);
		os_free(pCon);
		pCon = NULL;
	}
}

void ICACHE_FLASH_ATTR ntp_get_time(void)
{
	ntp_t ntp;

	os_printf("Sending NTP request.\n");

	// Set up the UDP "connection"
	pCon = (struct espconn *) os_zalloc(sizeof(struct espconn));
	pCon->type = ESPCONN_UDP;
	pCon->state = ESPCONN_NONE;
	pCon->proto.udp = (esp_udp *) os_zalloc(sizeof(esp_udp));
	pCon->proto.udp->local_port = espconn_port();
	pCon->proto.udp->remote_port = 123;
	os_memcpy(pCon->proto.udp->remote_ip, ntp_server, 4);

	// Create a really simple NTP request packet
	os_memset(&ntp, 0, sizeof(ntp_t));
	ntp.options = 0b00100011; // leap = 0, version = 4, mode = 3 (client)

	// Set timeout timer
	os_timer_disarm(&ntp_timeout);
	os_timer_setfn(&ntp_timeout, (os_timer_func_t*) ntp_udp_timeout, pCon);
	os_timer_arm(&ntp_timeout, NTP_TIMEOUT_MS, 0);

	// Send the NTP request
	espconn_create(pCon);
	espconn_regist_recvcb(pCon, ntp_udp_recv);
	espconn_sent(pCon, (uint8_t *) &ntp, sizeof(ntp_t));
}

void ICACHE_FLASH_ATTR rtc_init(void)
{
	sys_last_ticks = system_get_time();
}