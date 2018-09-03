/*
 * Copyright 2018 Jonathan McDowell <noodles@earth.li>
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
#include <user_interface.h>
#include <osapi.h>
#include <espconn.h>
#include <mem.h>
#include <stdlib.h>
#include <upgrade.h>

#include "ota.h"
#include "project_config.h"

struct ota_status {
	struct espconn conn;
	bool do_update;
	uint8_t slot;
	uint32_t rcvd_len;
	uint32_t content_len;
	ip_addr_t server_ip;
};

static void ICACHE_FLASH_ATTR ota_finish(struct ota_status *upgrade)
{
	espconn_disconnect(&upgrade->conn);

	if (system_upgrade_flag_check() == UPGRADE_FLAG_FINISH) {
		os_printf("Rebooting into new ROM.\n");
		system_upgrade_reboot();
	} else {
		system_upgrade_flag_set(UPGRADE_FLAG_IDLE);
	}

	return;
}

static void ICACHE_FLASH_ATTR ota_receive(void *arg, char *buf,
		unsigned short len)
{
	uint8_t maj, min;
	char *verstr;
	char *lenhdr, *data, *ptr;
	struct ota_status *upgrade = arg;
	int sectors;

	if (!upgrade->do_update) {
		if ((os_strncmp(buf + 9, "200", 3) != 0)) {
			os_printf("Failed to fetch version info: %.3s\n",
					buf + 9);
			return;
		}
		/* We're checking if we need an update; look for the version */
		verstr = os_strstr(buf, "ESP8266-Upgrade-Version: ");
		if (!verstr) {
			os_printf("Couldn't find version. Got data: %s\n",
					buf);
			return;
		}
		verstr += strlen("ESP8266-Upgrade-Version: ");

		maj = strtol(verstr, &verstr, 10);
		if (*verstr != '.') {
			os_printf("Parsed major version %d, "
					"but unexpected %c\n",
					maj, *verstr);
		}
		verstr++;
		min = strtol(verstr, NULL, 10);

		os_printf("Got version %d.%d; I have version %d.%d\n",
				maj, min, VER_MAJ, VER_MIN);
		if (maj > VER_MAJ || (maj == VER_MAJ && min > VER_MIN)) {
			os_printf("Need upgrade.\n");
			upgrade->do_update = true;
		}
	} else {
		/* Trying to read the rom image */

		/* First reply? */
		if (upgrade->content_len == 0) {
			if ((lenhdr = os_strstr(buf, "Content-Length: ")) &&
				(data = os_strstr(lenhdr, "\r\n\r\n")) &&
				(os_strncmp(buf + 9, "200", 3) == 0)) {

				data += 4;
				len -= (data - buf);

				lenhdr += 16; /* Content-Length:<sp> */
				ptr = os_strstr(lenhdr, "\r\n");
				*ptr = '\0';
				upgrade->content_len = atoi(lenhdr);
				os_printf("Reading %d bytes of image.\n,",
						upgrade->content_len);

				if (upgrade->content_len > 0x6B000) {
					os_printf("Image too large.\n");
					upgrade->do_update = false;
					ota_finish(upgrade);
					return;
				}

				sectors = (upgrade->content_len +
						SPI_FLASH_SEC_SIZE - 1) >> 12;
				while (sectors--) {
					spi_flash_erase_sector(sectors +
						(upgrade->slot ? 129 : 1));
				}

				spi_flash_write(
					(upgrade->slot ? 0x81000 : 0x1000),
					(void *) data, len);
				upgrade->rcvd_len = len;

			} else {
				ptr = os_strstr(buf, "\r\n");
				*ptr = '\0';
				os_printf("Error getting ROM data: %s\n", buf);
				upgrade->do_update = false;
				ota_finish(upgrade);
				return;
			}
		} else {
			spi_flash_write((upgrade->slot ? 0x81000 : 0x1000) +
				upgrade->rcvd_len, (void *) buf, len);
			upgrade->rcvd_len += len;
		}

		if (upgrade->rcvd_len == upgrade->content_len) {
			upgrade->do_update = false;
			system_upgrade_flag_set(UPGRADE_FLAG_FINISH);
			ota_finish(upgrade);
		}
	}
}

static void ICACHE_FLASH_ATTR ota_sent(void *arg)
{
	/* Callback when all data sent by us down TCP connection */
}

static void ICACHE_FLASH_ATTR ota_connect(void *arg)
{
	struct ota_status *upgrade = arg;
	int len;
	char buf[256];

	espconn_regist_recvcb(&upgrade->conn, ota_receive);
	espconn_regist_sentcb(&upgrade->conn, ota_sent);

	if (upgrade->do_update) {
		os_printf("Sending rom image request header.\n");
		len = os_sprintf(buf, "GET %srom%d.bin HTTP/1.0\r\n"
			"Host: %s:%d\r\n"
			"Connection: close\r\n"
			"User-Agent: ESP8266 " PROJECT "\r\n"
			"\r\n",
			UPGRADE_PATH,
			upgrade->slot,
			UPGRADE_HOST,
			80);
	} else {
		os_printf("Sending version check request header.\n");
		len = os_sprintf(buf, "GET %s%s HTTP/1.1\r\n"
			"Host: %s:%d\r\n"
			"Connection: close\r\n"
			"User-Agent: ESP8266 " PROJECT "\r\n"
			"\r\n",
			UPGRADE_PATH,
			"version.txt",
			UPGRADE_HOST,
			80);
	}

	espconn_send(&upgrade->conn, (uint8_t *) buf, len);
}

static void ICACHE_FLASH_ATTR ota_disconnect(void *arg)
{
	struct ota_status *upgrade = arg;

	if (upgrade == NULL) {
		return;
	}

	espconn_delete(&upgrade->conn);

	if (!upgrade->do_update) {
		if (upgrade->conn.proto.tcp != NULL) {
			os_free(upgrade->conn.proto.tcp);
		}
		os_free(upgrade);
		system_upgrade_flag_set(UPGRADE_FLAG_IDLE);
		return;
	}

	/* Reuse conn for the rom download */
	upgrade->conn.state = ESPCONN_NONE;

	espconn_connect(&upgrade->conn);
}

static void ICACHE_FLASH_ATTR ota_error(void *arg, sint8 err)
{
	os_printf("Upgrade disconnected with error: %d.\n", err);
	ota_disconnect(arg);
}

static void ICACHE_FLASH_ATTR ota_got_dns(const char *name, ip_addr_t *ip,
		void *arg)
{
	struct ota_status *upgrade = arg;

	if (ip == NULL) {
		os_printf("Upgrade DNS request failed.\n");
		os_free(upgrade);
		system_upgrade_flag_set(UPGRADE_FLAG_IDLE);
		return;
	}

	upgrade->conn.type = ESPCONN_TCP;
	upgrade->conn.state = ESPCONN_NONE;
	upgrade->conn.proto.tcp = (esp_tcp *)os_malloc(sizeof(esp_tcp));
	upgrade->conn.proto.tcp->local_port = espconn_port();
	upgrade->conn.proto.tcp->remote_port = 80; /* FIXME; HTTPS */

	os_memcpy(upgrade->conn.proto.tcp->remote_ip, &ip->addr, 4);

	espconn_regist_connectcb(&upgrade->conn, ota_connect);
	espconn_regist_disconcb(&upgrade->conn, ota_disconnect);
	espconn_regist_reconcb(&upgrade->conn, ota_error);

	espconn_connect(&upgrade->conn);
}

bool ICACHE_FLASH_ATTR ota_check()
{
	struct ota_status *upgrade = NULL;

	/* Don't start an upgrade if one is in progress */
	if (system_upgrade_flag_check() == UPGRADE_FLAG_START) {
		return false;
	}

	upgrade = (struct ota_status *) os_zalloc(sizeof(struct ota_status));
	if (!upgrade) {
		os_printf("Couldn't allocate memory for upgrade structure.\n");
		return false;
	}

	system_upgrade_flag_set(UPGRADE_FLAG_START);

	upgrade->slot = system_upgrade_userbin_check() ? 0 : 1;

	/* Kick off the DNS lookup to start */
	espconn_gethostbyname(&upgrade->conn, UPGRADE_HOST,
		&upgrade->server_ip,
		ota_got_dns);

	return true;
}
