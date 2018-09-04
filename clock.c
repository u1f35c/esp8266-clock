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

#include "clock.h"

#define NTP_SERVER     "uk.pool.ntp.org"
#define NTP_TIMEOUT_MS 5000

static uint32_t sys_last_ticks;
static uint32_t sys_delta;
static os_timer_t ntp_timeout;

static ip_addr_t ntp_server_ip;

/* See RFC5905 7.3 */
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

bool ICACHE_FLASH_ATTR is_leap(uint32_t year)
{
	return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}

bool ICACHE_FLASH_ATTR is_dst(struct tm *time)
{
	int lastsun = time->tm_mday - time->tm_wday;

	if (time->tm_mon < 2 || time->tm_mon > 9)
		return false;
	if (time->tm_mon > 2 && time->tm_mon < 9)
		return true;

	/*
	 * Starts last Sunday in March, ends last Sunday in October, which must
	 * be at least the 25th of the month. So must be past that in March, or
	 * before that in October, to be in DST.
	 */
	if (time->tm_mon == 2)
		return (lastsun >= 25);
	if (time->tm_mon == 9)
		return (lastsun < 25);

	return false;
}

/*
 * Takes time, a Unix time (seconds since 1st Jan 1970) and breaks it down to:
 *
 * Time:
 *   tm_sec	0-59
 *   tm_min	0-59
 *   tm_hour	0-23
 *
 * Date:
 *   tm_year
 *   tm_mon	0-11
 *   tm_mday	1-31
 *
 *   tm_yday
 *   tm_wday	Sunday = 0, Saturday = 6
 *
 */
void ICACHE_FLASH_ATTR breakdown_time(uint32_t time, struct tm *result)
{
	uint32_t era, doe, yoe, mp;

	/* Do the time component */
	result->tm_sec = time % 60;
	time /= 60;
	result->tm_min = time % 60;
	time /= 60;
	result->tm_hour = time % 24;
	time /= 24;

	/* Now time is the number of days since 1970-01-01 (a Thursday) */
	result->tm_wday = (time + 4) % 7;

	/* Below from http://howardhinnant.github.io/date_algorithms.html */
	time += 719468;
	era = time / 146097;
	doe = time - era * 146097;
	yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;

	result->tm_year = yoe + era * 400;
	result->tm_yday = doe - (365 * yoe + yoe / 4 - yoe / 100);
	mp = (5 * result->tm_yday + 2) / 153;
	result->tm_mday = result->tm_yday - (153 * mp + 2) / 5 + 1;
	result->tm_mon = mp + (mp < 10 ? 2 : -10);
	if (result->tm_mon <= 2)
		result->tm_year++;

	/* result->tm_yday is March 1st indexed at this point; fix up */
	result->tm_yday += 28 + 31;
	if (is_leap(result->tm_year))
		result->tm_yday++;

	result->tm_isdst = is_dst(result);
	if (result->tm_isdst)
		result->tm_hour++;
	if (result->tm_hour > 23) {
		/*
		 * We can ignore fixing up the date at the end of month etc.
		 * because all we're actually displaying is the time.
		 */
		result->tm_hour = 0;
		result->tm_wday++;
		result->tm_mday++;
		result->tm_yday++;
	}
}

static void ICACHE_FLASH_ATTR ntp_udp_timeout(void *arg)
{
	struct espconn *pCon = (struct espconn *) arg;

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
	struct espconn *pCon = (struct espconn *) arg;
	uint32_t timestamp;
	ntp_t *ntp;
	struct tm dt;

	os_printf("Got NTP response.\n");

	os_timer_disarm(&ntp_timeout);

	// Extract NTP time
	ntp = (ntp_t *) pdata;
	timestamp = ntp->trans_time[0] << 24 | ntp->trans_time[1] << 16 |
		ntp->trans_time[2] << 8 | ntp->trans_time[3];
	// NTP 0 is 1st Jan 1900; convert to Unix time 0 of 1st Jan 1970
	timestamp -= 2208988800ULL;

	// Store the time
	set_time(timestamp);

	// Print it out
	breakdown_time(timestamp, &dt);
	os_printf("%04d-%02d-%02d %02d:%02d:%02d (%u)\r\n",
		dt.tm_year, dt.tm_mon + 1, dt.tm_mday,
		dt.tm_hour, dt.tm_min, dt.tm_sec, timestamp);

	// clean up connection
	if (pCon) {
		espconn_delete(pCon);
		os_free(pCon->proto.udp);
		os_free(pCon);
		pCon = NULL;
	}
}

void ICACHE_FLASH_ATTR ntp_got_dns(const char *name, ip_addr_t *ip, void *arg)
{
	ntp_t ntp;
	struct espconn *pCon = (struct espconn *) arg;

	if (ip == NULL) {
		os_printf("NTP DNS request failed.\n");
		os_free(pCon);
		return;
	}

	os_printf("Sending NTP request.\n");

	// Set up the UDP "connection"
	pCon->type = ESPCONN_UDP;
	pCon->state = ESPCONN_NONE;
	pCon->proto.udp = (esp_udp *) os_zalloc(sizeof(esp_udp));
	pCon->proto.udp->local_port = espconn_port();
	pCon->proto.udp->remote_port = 123;
	os_memcpy(pCon->proto.udp->remote_ip, &ip->addr, 4);

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

void ICACHE_FLASH_ATTR ntp_get_time(void)
{
	struct espconn *pCon = NULL;

	os_printf("Sending DNS request for NTP server.\n");
	pCon = (struct espconn *) os_zalloc(sizeof(struct espconn));
	espconn_gethostbyname(pCon, NTP_SERVER, &ntp_server_ip, ntp_got_dns);
}

void ICACHE_FLASH_ATTR rtc_init(void)
{
	sys_last_ticks = system_get_time();
}
