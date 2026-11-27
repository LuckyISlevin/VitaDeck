# VitaDeck boot plugin for Adrenaline (PSP side, see main.c). Needs the pspdev
# toolchain (Linux/macOS; on Windows the GitHub Actions workflow
# .github/workflows/psp-plugin.yml builds it). `make` gives vdboot.prx, which is
# committed as plugin/vdboot.prx and packed into the VPK by build.ps1.
TARGET = vdboot
OBJS = main.o imports.o

BUILD_PRX = 1
PRX_EXPORTS = exports.exp
USE_KERNEL_LIBC = 1
USE_KERNEL_LIBS = 1

CFLAGS = -Os -G0 -Wall -Wextra -fno-pic -fno-builtin-printf
ASFLAGS = $(CFLAGS)
LDFLAGS = -nostartfiles
LIBS = -lgcc

PSPSDK = $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak
