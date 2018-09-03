SDKDIR ?= /opt/esp8266-sdk

AR = xtensa-lx106-elf-ar
CC = xtensa-lx106-elf-gcc
LD = xtensa-lx106-elf-gcc
OBJCOPY = xtensa-lx106-elf-objcopy
OBJDUMP = xtensa-lx106-elf-objdump

LIBS = -lc -lhal -lphy -lpp -lnet80211 -llwip -lwpa -lmain

CFLAGS = -Wall -Os -fno-inline-functions -mlongcalls -DICACHE_FLASH -I. \
	 -I$(SDKDIR)/xtensa-lx106-elf/include
LDFLAGS = -nostdlib -Wl,--no-check-sections -Wl,--gc-sections -Wl,-static \
	  -u call_user_start \
	  -L$(SDKDIR)/xtensa-lx106-elf/lib

APP = clock
OBJS = user_main.o clock.o max7219.o ota.o spi.o

all: rom0.bin rom1.bin

%.bin: %.elf
	PATH=$$PATH:$(SDKDIR)/bin esptool.py elf2image $^ --version 2 -o $@

%.elf: $(APP)_app.a
	$(LD) -T$(basename $@).ld $(LDFLAGS) -Wl,--start-group $(LIBS) $^ -Wl,--end-group -lgcc -o $@
	$(OBJDUMP) -h -j .data -j .rodata -j .bss -j .text -j .irom0.text $@

$(APP)_app.a: project_config.h $(OBJS)
	$(AR) cru $@ $^

flash: rom0.bin rom1.bin
	$(SDKDIR)/bin/esptool.py write_flash 0x2000 rom0.bin 0x42000 rom1.bin

project_config.h:
	echo '#error "Edit this file to match your configuration."' > $@
	echo '#define CFG_WIFI_SSID "My Wifi"' >> $@
	echo '#define CFG_WIFI_PASSWORD "password"' >> $@

clean:
	rm -f $(OBJS) $(APP)_app.a rom0.elf rom1.elf rom0.bin rom1.bin

.PHONY: all clean
