# Tweebbo for PlayStation Vita (showcase proof of concept).
# Build with ./build.sh (uses the vitasdk/vitasdk container).
VITASDK ?= /usr/local/vitasdk
export PATH := $(VITASDK)/bin:$(PATH)

CC      := arm-vita-eabi-gcc
TARGET  ?= tweebbo
TITLE_ID ?= TWEE00001
APP_NAME ?= Tweebbo Showcase

APP_VERSION := $(shell cat VERSION)
# The hotel on the local network this proof of concept plays on (set by build.sh).
TWEEBBO_SERVER ?= $(error Set TWEEBBO_SERVER, e.g. ws://192.168.1.20:8096 (use ./build.sh))
CFLAGS  := -std=gnu11 -O2 -Wall -ffunction-sections -fdata-sections \
           -I source -I $(VITASDK)/arm-vita-eabi/include -D_GNU_SOURCE \
           -DSCREEN_W=480 -DSCREEN_H=272 -DTWEEBBO_DEVICE=3 \
           -DTWEEBBO_LAN_ONLY=1 -DDEFAULT_SERVER='"$(TWEEBBO_SERVER)"' \
           -DAPP_VERSION='"$(APP_VERSION)"' $(EXTRA_CFLAGS)
LDFLAGS := -Wl,-q -Wl,--gc-sections

BUILD ?= build

SRC := $(wildcard source/*.c) $(wildcard source/gen/*.c) $(wildcard source/vita/*.c)
OBJ := $(addprefix $(BUILD)/,$(notdir $(SRC:.c=.o)))

STUBS := -lSceDisplay_stub -lSceGxm_stub -lSceCtrl_stub -lSceTouch_stub -lSceNet_stub -lSceNetCtl_stub \
         -lSceSysmodule_stub -lSceIofilemgr_stub -lSceProcessmgr_stub -lSceKernelThreadMgr_stub \
         -lSceCommonDialog_stub -lSceAppMgr_stub -lSceAppUtil_stub -lSceMotion_stub
LIBS  := -Wl,--start-group $(STUBS) -lvita2d -lmbedtls -lmbedx509 -lmbedcrypto -lpng -ljpeg -lz -lpthread -lm -Wl,--end-group

SCE_SYS := sce_sys/icon0.png sce_sys/livearea/contents/bg.png sce_sys/livearea/contents/startup.png \
           sce_sys/livearea/contents/template.xml

all: out/$(TARGET).vpk

$(BUILD):
	@mkdir -p $(BUILD)

# Rebuild everything when the baked hotel address changes.
$(BUILD)/server.stamp: FORCE | $(BUILD)
	@echo '$(TWEEBBO_SERVER)' | cmp -s - $@ || echo '$(TWEEBBO_SERVER)' > $@

$(BUILD)/%.o: source/%.c $(BUILD)/server.stamp | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: source/gen/%.c $(BUILD)/server.stamp | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: source/vita/%.c $(BUILD)/server.stamp | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/$(TARGET).elf: $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $(OBJ) $(LIBS)

# All furniture textures in one file: VitaShell then copies ~35 files instead of ~1.900.
romfs/furni.pak: $(wildcard romfs/furni/*.png)
	python3 tools/pack-furni.py

out/$(TARGET).vpk: $(BUILD)/$(TARGET).elf $(SCE_SYS) romfs/furni.pak
	vita-elf-create $(BUILD)/$(TARGET).elf $(BUILD)/$(TARGET).velf
	vita-make-fself -c $(BUILD)/$(TARGET).velf $(BUILD)/eboot.bin
	vita-mksfoex -s TITLE_ID=$(TITLE_ID) "$(APP_NAME)" $(BUILD)/param.sfo
	@mkdir -p out
	python3 tools/pack-vpk.py $@ $(BUILD)/param.sfo $(BUILD)/eboot.bin
	@ls -la $@

clean:
	rm -rf build out $(TARGET).elf $(TARGET).velf $(TARGET).vpk romfs/furni.pak

.PHONY: all clean FORCE
