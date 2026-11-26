#include <psp2/display.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/ctrl.h>
#include <stdlib.h>
#include <string.h>

int main(void)
{
	SceUID id = sceKernelAllocMemBlock("fb", SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW, 0x200000, NULL);
	void *base; sceKernelGetMemBlockBase(id, &base);
	unsigned int *px = base;
	unsigned int *tmp = malloc(960 * 544 * 4);
	for (int i = 0; i < 960 * 544; i++) tmp[i] = 0xFF16100Bu;
	memcpy(px, tmp, 960 * 544 * 4);
	SceDisplayFrameBuf fb = { sizeof(fb), base, 960, SCE_DISPLAY_PIXELFORMAT_A8B8G8R8, 960, 544 };
	sceDisplaySetFrameBuf(&fb, SCE_DISPLAY_SETBUF_NEXTFRAME);
	SceCtrlData pad;
	for (;;) {
		sceCtrlPeekBufferPositive(0, &pad, 1);
		if (pad.buttons & SCE_CTRL_START) break;
		sceDisplayWaitVblankStart();
	}
	return 0;
}
