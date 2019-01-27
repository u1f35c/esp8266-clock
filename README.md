esp8266-clock
=============

This is a simple C / NON-OS SDK based firmware for the
[ESP8266](https://espressif.com/en/products/hardware/esp8266ex/overview)
devices to drive a
[MAX7219](https://www.maximintegrated.com/en/products/power/display-power-control/MAX7219.html)
based display of 4 8x8 LED matrix modules to display a 24 hour clock, synced
via NTP.

It uses the ESP8266 SPI interface to talk to the MAX7219, though repurposes
MISO to be CS as the MAX7219 isn't strictly an SPI device:

```
ESP8266        MAX7219
GPIO13 (MOSI) -> DIN
GPIO14 (CLK)  -> CLK
GPIO12 (CS)   -> LOAD / nCS
```

Building
--------

You will need an xtensa-lx106 toolchain and the Espressif [NONOS
SDK](https://github.com/espressif/ESP8266_NONOS_SDK). If the toolchain is in
the system path and the SDK resides in `/opt/esp8266-sdk` then a simple `make`
should output 2 ROM images (one for each flash slot).

If this is the first time you've built the project you'll need to modify
`project_config.h` to match your settings - in particular wifi details.

You can then flash to your device as follows (these addresses are for a 2MB
flash part, change the last 2 addresses to 0xFC000 & 0xFE000 for a smaller 1MB
part):

```
esptool --port /dev/ttyUSB0 --baud 921600 write_flash \
	0x0 boot_v1.7.bin \
	0x1000 rom0.bin \
	0x1FC000 esp_init_data_default_v08.bin \
	0x1FE000 blank.bin
```

(You might need a `--flash_size` and/or `--flash_mode` parameter to keep your
device happy - I found getting this wrong led to a failure to boot correctly.)

Upgrades
--------

Basic Over-the-Air upgrade functionality is included; `UPGRADE_HOST` and
`UPGRADE_PATH` must be set appropriately to form the base of the URL to check.
`version.txt` is looked for at the URL, and a `ESP8266-Upgrade-Version:` header
parsed to determine the version available for download. If this is later than
the running version then, depending on which flash slot is currently in use,
`rom0.bin` or `rom1.bin` will be downloaded into the non-running slot. If this
is successful the device will boot into the new image. A check for updated
firmware is made every time the wifi is reconnected to.

License
-------

All my code in this project is released under GPLv3 or later.
