/*
 * VitaDeck boot plugin for Adrenaline (PSP side, kernel VSH plugin).
 *
 * VitaDeck starts a PSP/PS1 title that has no bubble of its own (copied for
 * Adrenaline) by writing ms0:/VitaDeck/boot.txt and starting Adrenaline.
 * Adrenaline loads this plugin whenever its XMB starts. Without a request the
 * plugin returns at once and the XMB runs as usual. With one, it waits until
 * the XMB is up, deletes the request and boots the title the way the XMB does.
 *
 * The approach and the boot parameters follow Pocket Shelf's psp-boot plugin
 * (github.com/fthdgn-gh/pocket-shelf, MIT): boot only after vsh_module and
 * the next module have started (booting earlier hung the PSP side), then
 *   - PSN game (EBOOT.PBP whose DATA.PSAR starts with "NPUMDIMG"):
 *     apitype 0x123 (0x124 with a PBOOT.PBP update), key "umdemu", argument
 *     disc0:/PSP_GAME/SYSDIR/EBOOT.BIN (or the PBOOT.PBP), no UMD file and the
 *     normal boot config, so the firmware's NP9660 reads the image;
 *   - other PSP EBOOT: apitype 0x141, key "game";
 *   - PS1 EBOOT: apitype 0x144, key "pops".
 *
 * Request file, three lines:  vitadeck1 / psp or psx / ms0:/PSP/GAME/<ID>/EBOOT.PBP
 * Every request leaves a line in ms0:/VitaDeck/boot.log.
 */
#include <pspkernel.h>
#include <pspiofilemgr.h>
#include <psploadexec_kernel.h>
#include <pspmodulemgr_kernel.h>
#include <string.h>

PSP_MODULE_INFO("VitaDeckBoot", 0x1000, 1, 0);

/* SystemCtrlForKernel (imports.S, name-hash NIDs exported by Adrenaline) */
typedef int (*StartModuleHandler)(SceModule *mod);
unsigned int sctrlHENFindFunction(const char *module, const char *library, unsigned int nid);
int sctrlKernelLoadExecVSHWithApitype(int apitype, const char *file, struct SceKernelLoadExecVSHParam *param);
StartModuleHandler sctrlHENSetStartModuleHandler(StartModuleHandler handler);

#define REQUEST_PATH "ms0:/VitaDeck/boot.txt"
#define LOG_PATH     "ms0:/VitaDeck/boot.log"
#define GAME_PREFIX  "ms0:/PSP/GAME/"
#define EBOOT_SUFFIX "/EBOOT.PBP"

#define AFTER_XMB_US  (200 * 1000)        /* after the first module that follows vsh_module */
#define XMB_SETTLE_US (2 * 1000 * 1000)   /* when no module follows vsh_module */
#define VSH_WAIT_US   (20 * 1000 * 1000)
#define POLL_US       (50 * 1000)

#define APITYPE_UMDEMU_MS1 0x123
#define APITYPE_UMDEMU_MS2 0x124   /* with a PBOOT.PBP update */
#define APITYPE_MS_GAME    0x141
#define APITYPE_MS_PS1     0x144
#define BOOT_NORMAL        0

/* not name-hash NIDs; looked up so a missing one is a null pointer, not a load failure */
#define NID_SET_UMD_FILE  0xB64186D0
#define NID_SET_BOOT_CONF 0x5CB025F0

static StartModuleHandler previous_handler;
static volatile int vsh_seen, after_vsh_seen;
static char request[512], path[256], arg[256];

static void log_line(const char *a, const char *b)
{
	SceUID fd = sceIoOpen(LOG_PATH, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_APPEND, 0777);
	if (fd < 0) return;
	sceIoWrite(fd, a, strlen(a));
	if (b) { sceIoWrite(fd, " ", 1); sceIoWrite(fd, b, strlen(b)); }
	sceIoWrite(fd, "\n", 1);
	sceIoClose(fd);
}

static void *system_control(unsigned int nid)
{
	return (void *)sctrlHENFindFunction("SystemControl", "SystemCtrlForKernel", nid);
}

static int on_module_start(SceModule *mod)
{
	if (!strcmp(mod->modname, "vsh_module")) vsh_seen = 1;
	else if (vsh_seen) after_vsh_seen = 1;
	return previous_handler ? previous_handler(mod) : 0;
}

static int is_psn_image(void)
{
	SceUID fd = sceIoOpen(path, PSP_O_RDONLY, 0);
	if (fd < 0) return 0;
	unsigned char h[0x28];
	char magic[8];
	int found = 0;
	if (sceIoRead(fd, h, sizeof h) == (int)sizeof h && !memcmp(h, "\0PBP", 4)) {
		unsigned int psar = h[0x24] | h[0x25] << 8 | h[0x26] << 16 | (unsigned int)h[0x27] << 24;
		if (sceIoLseek32(fd, (int)psar, PSP_SEEK_SET) == (int)psar && sceIoRead(fd, magic, 8) == 8)
			found = !memcmp(magic, "NPUMDIMG", 8);
	}
	sceIoClose(fd);
	return found;
}

static int boot(int ps1)
{
	struct SceKernelLoadExecVSHParam param;
	memset(&param, 0, sizeof param);
	param.size = sizeof param;
	if (!ps1 && is_psn_image()) {
		void (*set_umd_file)(const char *) = system_control(NID_SET_UMD_FILE);
		int (*set_boot_conf)(int) = system_control(NID_SET_BOOT_CONF);
		if (set_umd_file) set_umd_file("");
		if (set_boot_conf) set_boot_conf(BOOT_NORMAL);
		int apitype = APITYPE_UMDEMU_MS1;
		strcpy(arg, "disc0:/PSP_GAME/SYSDIR/EBOOT.BIN");
		/* a PBOOT.PBP update next to the EBOOT wins */
		static char pboot[256];
		size_t l = strlen(path), sl = sizeof EBOOT_SUFFIX - 1;
		if (l > sl && l - sl + sizeof "/PBOOT.PBP" < sizeof pboot) {
			memcpy(pboot, path, l - sl);
			strcpy(pboot + l - sl, "/PBOOT.PBP");
			SceIoStat st;
			if (sceIoGetstat(pboot, &st) >= 0) { apitype = APITYPE_UMDEMU_MS2; strcpy(arg, pboot); }
		}
		log_line(apitype == APITYPE_UMDEMU_MS2 ? "psn+pboot" : "psn", path);
		param.args = strlen(arg) + 1;
		param.argp = arg;
		param.key = "umdemu";
		return sctrlKernelLoadExecVSHWithApitype(apitype, path, &param);
	}
	log_line(ps1 ? "ps1" : "eboot", path);
	param.args = strlen(path) + 1;
	param.argp = path;
	param.key = ps1 ? "pops" : "game";
	return sctrlKernelLoadExecVSHWithApitype(ps1 ? APITYPE_MS_PS1 : APITYPE_MS_GAME, path, &param);
}

static void handle_request(void)
{
	SceUID fd = sceIoOpen(REQUEST_PATH, PSP_O_RDONLY, 0);
	if (fd < 0) return;
	int n = sceIoRead(fd, request, sizeof request - 1);
	sceIoClose(fd);
	sceIoRemove(REQUEST_PATH);   /* acted on once at most */
	if (n <= 0) { log_line("empty request", NULL); return; }
	request[n] = 0;
	char *line[3] = { request, NULL, NULL };
	for (int k = 1, i = 0; i < n && k < 3; i++) if (request[i] == '\n') { request[i] = 0; line[k++] = request + i + 1; }
	for (int k = 0; k < 3; k++) if (line[k]) { char *e = strchr(line[k], '\n'); if (e) *e = 0; e = strchr(line[k], '\r'); if (e) *e = 0; }
	if (!line[2] || strcmp(line[0], "vitadeck1")) { log_line("bad request", NULL); return; }
	size_t l = strlen(line[2]), sl = sizeof EBOOT_SUFFIX - 1;
	if (l >= sizeof path || strncmp(line[2], GAME_PREFIX, sizeof GAME_PREFIX - 1) || l <= sl ||
	    strcmp(line[2] + l - sl, EBOOT_SUFFIX) || strstr(line[2], "..")) { log_line("bad path", line[2]); return; }
	strcpy(path, line[2]);
	int r = boot(!strcmp(line[1], "psx"));
	log_line(r < 0 ? "boot failed" : "booted", NULL);
}

static int boot_thread(SceSize args, void *argp)
{
	(void)args; (void)argp;
	for (unsigned int w = 0; !vsh_seen && w < VSH_WAIT_US; w += POLL_US) sceKernelDelayThread(POLL_US);
	if (!vsh_seen) { log_line("vsh_module not seen", NULL); return sceKernelExitDeleteThread(0); }
	for (unsigned int w = 0; !after_vsh_seen && w < XMB_SETTLE_US; w += POLL_US) sceKernelDelayThread(POLL_US);
	if (after_vsh_seen) sceKernelDelayThread(AFTER_XMB_US);
	handle_request();
	return sceKernelExitDeleteThread(0);
}

int module_start(SceSize args, void *argp)
{
	(void)args; (void)argp;
	SceIoStat st;
	if (sceIoGetstat(REQUEST_PATH, &st) < 0) return 0;   /* normal Adrenaline start: do nothing */
	previous_handler = sctrlHENSetStartModuleHandler(on_module_start);
	SceUID th = sceKernelCreateThread("VitaDeckBoot", boot_thread, 0x18, 0x1000, 0, NULL);
	if (th >= 0) sceKernelStartThread(th, 0, NULL);
	return 0;
}

int module_stop(SceSize args, void *argp)
{
	(void)args; (void)argp;
	return 0;
}
