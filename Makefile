# Tweebbo for PlayStation Vita (development port).
# Build with ./build.sh (uses the vitasdk/vitasdk container).
VITASDK ?= /usr/local/vitasdk
export PATH := $(VITASDK)/bin:$(PATH)

CC      := arm-vita-eabi-gcc
TARGET  ?= tweebbo
TITLE_ID ?= TWEE00001
APP_NAME ?= Tweebbo

APP_VERSION := $(shell cat VERSION)
# The hotel the client plays on (set by build.sh: tweebbo.lol, or a LAN hotel for showcases).
TWEEBBO_SERVER ?= $(error Set TWEEBBO_SERVER, e.g. ws://192.168.1.20:8096 (use ./build.sh))
CFLAGS  := -std=gnu11 -O2 -Wall -ffunction-sections -fdata-sections -fwrapv -fno-strict-aliasing -I vendor/quickjs -DCONFIG_VERSION='"2026-06-04"' \
           -I source -I build/jansson/jansson/include -I $(VITASDK)/arm-vita-eabi/include -D_GNU_SOURCE \
           -DSCREEN_W=480 -DSCREEN_H=272 -DTWEEBBO_DEVICE=3 \
           -DDEFAULT_SERVER='"$(TWEEBBO_SERVER)"' \
           -DAPP_VERSION='"$(APP_VERSION)"' $(EXTRA_CFLAGS)
ifeq ($(TWEEBBO_LAN_ONLY),1)
CFLAGS  += -DTWEEBBO_LAN_ONLY=1
endif
LDFLAGS := -Wl,-q -Wl,--gc-sections
LDFLAGS += -Wl,--wrap=sceGxmShaderPatcherCreateFragmentProgram -Wl,--wrap=sceGxmSetFragmentProgram

BUILD ?= build

SRC := $(wildcard source/*.c) $(wildcard source/gen/*.c) $(wildcard source/vita/*.c) $(wildcard vendor/quickjs/*.c) $(wildcard vendor/qrcodegen/*.c)
OBJ := $(addprefix $(BUILD)/,$(notdir $(SRC:.c=.o)))

STUBS := -lSceAudio_stub -lSceDisplay_stub -lSceGxm_stub -lSceCtrl_stub -lSceTouch_stub -lSceNet_stub -lSceNetCtl_stub \
         -lSceSysmodule_stub -lSceIofilemgr_stub -lSceProcessmgr_stub -lSceKernelThreadMgr_stub \
         -lSceCommonDialog_stub -lSceAppMgr_stub -lSceAppUtil_stub -lSceMotion_stub
LIBS  := -Wl,--start-group $(STUBS) -lvita2d -lmbedtls -lmbedx509 -lmbedcrypto build/jansson/jansson/lib/libjansson.a -lpng -ljpeg -lz -lpthread -lm -Wl,--end-group

SCE_SYS := sce_sys/icon0.png sce_sys/livearea/contents/bg.png sce_sys/livearea/contents/startup.png \
           sce_sys/livearea/contents/template.xml

all: out/$(TARGET).vpk

$(BUILD):
	@mkdir -p $(BUILD)

# The SDK's PIC archive has ARM GOT relocations unsupported by vita-elf-create.
build/jansson/jansson/lib/libjansson.a: vendor/jansson-2.14.tar.gz tools/jansson/CMakeLists.txt
	@mkdir -p build/jansson-src
	tar -xf $< --strip-components=1 -C build/jansson-src
	cmake -S tools/jansson -B build/jansson -DCMAKE_TOOLCHAIN_FILE=$(VITASDK)/share/vita.toolchain.cmake
	cmake --build build/jansson -j4

$(OBJ): build/jansson/jansson/lib/libjansson.a VERSION $(wildcard source/*.h source/vita/*.h)
$(BUILD)/main.o: tools/emu/games-exercise.h

# Test instrumentation must not survive a normal build.
$(BUILD)/server.stamp: FORCE | $(BUILD)
	@echo '$(TWEEBBO_SERVER) $(TWEEBBO_LAN_ONLY) $(EXTRA_CFLAGS)' | cmp -s - $@ || echo '$(TWEEBBO_SERVER) $(TWEEBBO_LAN_ONLY) $(EXTRA_CFLAGS)' > $@

$(BUILD)/%.o: source/%.c $(BUILD)/server.stamp | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: source/gen/%.c $(BUILD)/server.stamp | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: vendor/quickjs/%.c $(BUILD)/server.stamp | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: vendor/qrcodegen/%.c $(BUILD)/server.stamp | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: source/vita/%.c $(BUILD)/server.stamp | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/$(TARGET).elf: $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $(OBJ) $(LIBS)

# All furniture textures in one file: VitaShell then copies ~35 files instead of ~1.900.
romfs/furni.pak: $(wildcard romfs/furni/*.png)
	python3 tools/pack-furni.py

out/$(TARGET).vpk: $(BUILD)/$(TARGET).elf $(SCE_SYS) romfs/furni.pak $(wildcard romfs/gfx/*.png) $(wildcard romfs/spaces/*) $(wildcard romfs/snow/*) $(wildcard romfs/fastfood/*) $(wildcard romfs/battle/*) $(wildcard romfs/lang/*) tools/pack-vpk.py
	vita-elf-create $(BUILD)/$(TARGET).elf $(BUILD)/$(TARGET).velf
	vita-make-fself -c $(BUILD)/$(TARGET).velf $(BUILD)/eboot.bin
	vita-mksfoex -s TITLE_ID=$(TITLE_ID) "$(APP_NAME)" $(BUILD)/param.sfo
	@mkdir -p out
	python3 tools/pack-vpk.py $@ $(BUILD)/param.sfo $(BUILD)/eboot.bin
	@ls -la $@

clean:
	rm -rf build out $(TARGET).elf $(TARGET).velf $(TARGET).vpk romfs/furni.pak

.PHONY: all clean FORCE
