# VitaDeck – build without the VitaSDK installer
# Needs: arm-none-eabi-gcc (+newlib), vita-toolchain tools, stub libs from vita-headers.
TITLE      := VitaDeck
TITLE_ID   := VLAUNCH01
VERSION    := 02.00

VT       ?= /opt/vt/bin
HEADERS  ?= /home/claude/vsrc/vita-headers/include
STUBS    ?= /home/claude/vsrc/stubs
CC       := arm-none-eabi-gcc
ARCH     := -march=armv7-a+simd -mtune=cortex-a9 -mthumb -mfloat-abi=hard
CFLAGS   := $(ARCH) -O2 -ffunction-sections -fdata-sections -Wall -Wno-unused-function -I$(HEADERS) -Isrc
LDFLAGS  := $(ARCH) -nostartfiles -Wl,-q -Wl,--gc-sections -T vita.ld -L$(STUBS)
# curl/mbedTLS/zlib/zstd/pthread: static libs from the VitaSDK packages (must be in $(STUBS) or on -L)
LIBS     := -lcurl -lmbedtls -lmbedx509 -lmbedcrypto -leverest -lp256m -lz -lzstd -lz -lpthread \
            -lc -lm -lgcc \
            -lSceDisplay_stub -lSceSysmem_stub -lSceCtrl_stub -lSceTouch_stub -lSceRtc_stub \
            -lScePower_stub -lSceAppMgr_stub -lSceAVConfig_stub -lSceIofilemgr_stub \
            -lSceLibKernel_stub -lSceKernelThreadMgr_stub -lSceKernelModulemgr_stub -lSceProcessmgr_stub \
            -lSceSysmodule_stub -lSceNet_stub -lSceNetCtl_stub -lSceRegistryMgr_stub -lSceAudio_stub

OBJS := build/crt.o build/gfx.o build/img.o build/ui.o build/platform_vita.o

all: build/VitaDeck.vpk

build/%.o: src/%.c src/gfx.h src/platform.h
	@mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

build/launcher.elf: $(OBJS) vita.ld
	$(CC) $(LDFLAGS) $(OBJS) $(LIBS) -o $@

build/launcher.velf: build/launcher.elf
	$(VT)/vita-elf-create $< $@

build/eboot.bin: build/launcher.velf
	$(VT)/vita-make-fself -c $< $@

build/param.sfo: Makefile
	$(VT)/vita-mksfoex -s TITLE_ID=$(TITLE_ID) -s APP_VER=$(VERSION) "$(TITLE)" $@

build/VitaDeck.vpk: build/eboot.bin build/param.sfo sce_sys/icon0.png
	$(VT)/vita-pack-vpk -s build/param.sfo -b build/eboot.bin \
	  -a sce_sys/icon0.png=sce_sys/icon0.png \
	  -a sce_sys/livearea/contents/bg.png=sce_sys/livearea/contents/bg.png \
	  -a sce_sys/livearea/contents/startup.png=sce_sys/livearea/contents/startup.png \
	  -a sce_sys/livearea/contents/template.xml=sce_sys/livearea/contents/template.xml \
	  -a res/Lato-Regular.ttf=res/Lato-Regular.ttf \
	  -a res/Lato-Bold.ttf=res/Lato-Bold.ttf \
	  -a res/Lexend-Regular.ttf=res/Lexend-Regular.ttf \
	  -a res/Lexend-Bold.ttf=res/Lexend-Bold.ttf \
	  -a res/OFL-Lato.txt=res/OFL-Lato.txt \
	  -a res/OFL-Lexend.txt=res/OFL-Lexend.txt \
	  -a res/cacert.pem=res/cacert.pem $@

host:
	gcc -O2 -Wall -Wno-unused-function -Isrc -Ihost src/ui.c src/gfx.c src/img.c host/platform_host.c -lm -o build/host

clean:
	rm -rf build/*.o build/*.elf build/*.velf build/eboot.bin build/param.sfo build/*.vpk

.PHONY: all host clean
