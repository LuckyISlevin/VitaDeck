/*
 * VitaDeck return plugin – step 2: return to VitaDeck after a game.
 *
 * Loaded into SceShell via taiHEN (*main in ur0:tai/config.txt).
 * Before VitaDeck starts a game it writes the game's title ID to
 * ux0:data/VitaDeck/return.txt. This plugin then
 *   1. waits (max. 20 s) until that game shows up as running,
 *   2. waits until it is gone again,
 *   3. after a short pause, if no other (non-system) app took over,
 *      deletes the marker and starts VitaDeck.
 * Games started from the LiveArea have no marker and are left alone.
 * Everything is logged to ux0:data/VitaDeck/plugin.log.
 *
 * Runs inside the system shell, so: no newlib, no malloc, only Sce* calls,
 * every call checked, one small thread, nothing hooked.
 */
#include <psp2/kernel/modulemgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/clib.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/appmgr.h>
#include <psp2/ctrl.h>
#include <stdarg.h>

#define LOG_PATH  "ux0:data/VitaDeck/plugin.log"
#define LOG_OLD   "ux0:data/VitaDeck/plugin.old.log"
#define LOG_MAX   (64 * 1024)
#define MAX_APPS  16
#define POLL_US   500000
#define MARKER    "ux0:data/VitaDeck/return.txt"
#define SETTINGS  "ux0:data/VitaDeck/plugin.txt"   /* "autostart=0|1" / "return=0|1", default 1 */
#define BOOT_APP  "NPXS10079"                       /* system app seen at every boot (~9-12 s) */
#define AUTOSTART_MAX_MS 15000                      /* latest autostart if BOOT_APP never shows */
#define AUTOSTART_CALM_MS 1000                      /* quiet time after the boot app is gone */
#define SELF_ID   "VLAUNCH01"
#define START_TIMEOUT_MS 20000   /* game must appear within this time */
#define RETURN_DELAY_MS   2500   /* pause after the game is gone (close animation) */

static SceUID g_thread = -1;
static volatile int g_run = 1;
static int g_log_bytes;

static void log_line(const char *fmt, ...)
{
	if (g_log_bytes > LOG_MAX) return;   /* never let the log grow without bound */
	char buf[256];
	unsigned ms = (unsigned)(sceKernelGetProcessTimeWide() / 1000);
	int n = sceClibSnprintf(buf, sizeof buf, "[%7u.%03u] ", ms / 1000, ms % 1000);
	if (n < 0) return;
	va_list ap;
	va_start(ap, fmt);
	int m = sceClibVsnprintf(buf + n, sizeof buf - n - 1, fmt, ap);
	va_end(ap);
	if (m < 0) return;
	n += m;
	if (n > (int)sizeof buf - 2) n = sizeof buf - 2;
	buf[n++] = '\n';
	SceUID fd = sceIoOpen(LOG_PATH, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0666);
	if (fd < 0) return;
	sceIoWrite(fd, buf, n);
	sceIoClose(fd);
	g_log_bytes += n;
}

typedef struct { SceInt32 appid; SceUID pid; char name[16]; } RunApp;

/* current list of running apps as SceShell sees them */
static int snapshot(RunApp *out)
{
	SceInt32 ids[MAX_APPS];
	sceClibMemset(ids, 0, sizeof ids);
	int n = sceAppMgrGetRunningAppIdListForShell(ids, MAX_APPS);
	if (n < 0) return n;
	if (n > MAX_APPS) n = MAX_APPS;
	for (int i = 0; i < n; i++) {
		out[i].appid = ids[i];
		out[i].pid = sceAppMgrGetProcessIdByAppIdForShell(ids[i]);
		sceClibMemset(out[i].name, 0, sizeof out[i].name);
		if (out[i].pid >= 0 && sceAppMgrGetNameById(out[i].pid, out[i].name) < 0)
			sceClibSnprintf(out[i].name, sizeof out[i].name, "?");
		out[i].name[sizeof out[i].name - 1] = 0;
	}
	return n;
}

static int same(const RunApp *a, int na, const RunApp *b, int nb)
{
	if (na != nb) return 0;
	for (int i = 0; i < na; i++)
		if (a[i].appid != b[i].appid || a[i].pid != b[i].pid) return 0;
	return 1;
}

static unsigned now_ms(void) { return (unsigned)(sceKernelGetProcessTimeWide() / 1000); }

/* system apps (NPXS…) come and go on their own and never count as "a game" */
static int is_system(const char *name) { return sceClibStrncmp(name, "NPXS", 4) == 0; }

/* reads the title ID from the marker file; returns 1 if present and plausible */
static int read_marker(char *id, int max)
{
	SceUID fd = sceIoOpen(MARKER, SCE_O_RDONLY, 0);
	if (fd < 0) return 0;
	char buf[32];
	sceClibMemset(buf, 0, sizeof buf);
	int n = sceIoRead(fd, buf, sizeof buf - 1);
	sceIoClose(fd);
	if (n <= 0) return 0;
	int k = 0;
	for (int i = 0; i < n && k < max - 1; i++) {
		char c = buf[i];
		if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) id[k++] = c;
		else if (c == '\n' || c == '\r' || c == ' ') break;
		else return 0;
	}
	id[k] = 0;
	return k == 9;   /* title IDs are 9 characters, e.g. PCSB00321 */
}

/* settings written by VitaDeck (step 4); a missing file or key means "on" */
typedef struct { int autostart, ret; } Settings;

static Settings read_settings(void)
{
	Settings s = { 1, 1 };
	SceUID fd = sceIoOpen(SETTINGS, SCE_O_RDONLY, 0);
	if (fd < 0) return s;
	char buf[128];
	sceClibMemset(buf, 0, sizeof buf);
	int n = sceIoRead(fd, buf, sizeof buf - 1);
	sceIoClose(fd);
	for (int i = 0; i < n; ) {
		const char *l = buf + i;
		if (!sceClibStrncmp(l, "autostart=", 10) && l[10]) s.autostart = l[10] != '0';
		if (!sceClibStrncmp(l, "return=", 7) && l[7]) s.ret = l[7] != '0';
		while (i < n && buf[i] != '\n') i++;
		i++;
	}
	return s;
}

static int select_held(void)
{
	SceCtrlData pad;
	sceClibMemset(&pad, 0, sizeof pad);
	return sceCtrlPeekBufferPositive(0, &pad, 1) > 0 && (pad.buttons & SCE_CTRL_SELECT);
}

static int is_running(const char *id)
{
	SceUID pid = -1;
	return sceAppMgrGetIdByName(&pid, id) >= 0 && pid >= 0;
}

/* another non-system app (e.g. a game picked in the LiveArea) is running */
static int other_app_running(const char *except)
{
	RunApp cur[MAX_APPS];
	int n = snapshot(cur);
	for (int i = 0; i < n; i++)
		if (!is_system(cur[i].name) && sceClibStrncmp(cur[i].name, except, 16) != 0) return 1;
	return 0;
}

/* wait up to ms for VitaDeck to actually run */
static int wait_running(unsigned ms)
{
	for (unsigned t = 0; t < ms && g_run; t += 250) {
		if (is_running(SELF_ID)) return 1;
		sceKernelDelayThread(250000);
	}
	return is_running(SELF_ID);
}

/* Hardware test (step 2): LaunchAppByName2ForShell reported success but only
   brought up the LiveArea page, VitaDeck did not start by itself. So: direct
   URI launch (flag 0x20000 = no LiveArea), verify, retry. */
static void launch_vitadeck(void)
{
	for (int attempt = 1; attempt <= 3 && g_run; attempt++) {
		int r = sceAppMgrLaunchAppByUri(0x20000, "psgm:play?titleid=" SELF_ID);
		log_line("Starting VitaDeck, attempt %d (LaunchAppByUri 0x20000): r=0x%08X", attempt, (unsigned)r);
		if (wait_running(4000)) { log_line("VitaDeck is running"); return; }
	}
	SceUID r = sceAppMgrLaunchAppByName2ForShell(SELF_ID, NULL, NULL);
	log_line("Last attempt (LaunchAppByName2ForShell): r=0x%08X", (unsigned)r);
	log_line(wait_running(4000) ? "VitaDeck is running" : "VitaDeck did not start");
}

enum { ST_IDLE, ST_WAIT_START, ST_RUNNING, ST_ENDED };

static int worker(SceSize args, void *argp)
{
	(void)args; (void)argp;

	/* buttons held right after SceShell came up: SELECT = skip the autostart */
	SceCtrlData pad;
	sceClibMemset(&pad, 0, sizeof pad);
	int r = sceCtrlPeekBufferPositive(0, &pad, 1);
	Settings cfg = read_settings();
	int skip = r > 0 && (pad.buttons & SCE_CTRL_SELECT);
	log_line("Plugin started. autostart=%d return=%d, buttons at start: 0x%08X%s",
	         cfg.autostart, cfg.ret, (unsigned)pad.buttons, skip ? " (SELECT: autostart skipped)" : "");

	RunApp prev[MAX_APPS], cur[MAX_APPS];
	int nprev = -1;
	int st = ST_IDLE;
	char game[16] = { 0 };
	unsigned t_state = 0;

	/* autostart: wait until the boot-time system app has come and gone */
	int as_pending = cfg.autostart && !skip, as_seen_boot = 0, as_calm_set = 0;
	unsigned t_start = now_ms(), t_calm = 0;

	while (g_run) {
		/* diagnostics: log the running apps whenever they change */
		int n = snapshot(cur);
		if (n < 0) {
			if (nprev != -2) log_line("GetRunningAppIdListForShell failed: 0x%08X", (unsigned)n);
			nprev = -2;
		} else if (nprev < 0 || !same(prev, nprev, cur, n)) {
			log_line("Running apps: %d", n);
			for (int i = 0; i < n; i++)
				log_line("  title=%s pid=0x%08X", cur[i].name, (unsigned)cur[i].pid);
			sceClibMemcpy(prev, cur, sizeof(RunApp) * n);
			nprev = n;
		}

		unsigned t = now_ms();

		/* autostart (once per boot) */
		if (as_pending && select_held()) {
			as_pending = 0;
			log_line("SELECT pressed, autostart skipped");
		} else if (as_pending && n >= 0) {
			int sys = 0, other = 0;
			const char *other_name = "";
			for (int i = 0; i < n; i++) {
				if (!sceClibStrncmp(cur[i].name, BOOT_APP, 16)) as_seen_boot = 1;
				if (is_system(cur[i].name)) sys = 1;
				else { other = 1; other_name = cur[i].name; }
			}
			if (!as_calm_set || sys) { t_calm = t; as_calm_set = 1; }
			if (other) {
				as_pending = 0;
				log_line("An app is already running (%s), no autostart", other_name);
			} else if (!sys && t - t_calm >= AUTOSTART_CALM_MS &&
			           (as_seen_boot || t - t_start >= AUTOSTART_MAX_MS)) {
				as_pending = 0;
				log_line("Autostart (%s)", as_seen_boot ? BOOT_APP " has finished" : "time limit");
				launch_vitadeck();
			}
		}

		/* return logic */
		char id[16];
		int have = read_marker(id, sizeof id);
		if (have && !read_settings().ret) {
			sceIoRemove(MARKER);
			log_line("Marker %s ignored: return is switched off", id);
			have = 0;
		}
		if (have && (st == ST_IDLE || sceClibStrncmp(id, game, sizeof game) != 0)) {
			sceClibMemcpy(game, id, sizeof game);
			st = ST_WAIT_START; t_state = t;
			log_line("Marker found: %s, waiting for the game to start", game);
		} else if (!have && st != ST_IDLE) {
			log_line("Marker gone, return cancelled");
			st = ST_IDLE;
		}
		switch (st) {
		case ST_WAIT_START:
			if (is_running(game)) { st = ST_RUNNING; log_line("%s is running", game); }
			else if (t - t_state > START_TIMEOUT_MS) {
				log_line("%s did not start, marker discarded", game);
				sceIoRemove(MARKER);
				st = ST_IDLE;
			}
			break;
		case ST_RUNNING:
			if (!is_running(game)) { st = ST_ENDED; t_state = t; log_line("%s has ended", game); }
			break;
		case ST_ENDED:
			if (is_running(game)) { st = ST_RUNNING; break; }   /* came back (e.g. restarted) */
			if (t - t_state < RETURN_DELAY_MS) break;
			sceIoRemove(MARKER);
			st = ST_IDLE;
			if (other_app_running(game)) log_line("Another app is running, no return");
			else launch_vitadeck();
			break;
		}
		sceKernelDelayThread(POLL_US);
	}
	log_line("Plugin stopped");
	return 0;
}

void _start() __attribute__((weak, alias("module_start")));
int module_start(SceSize args, void *argp)
{
	(void)args; (void)argp;
	/* keep the previous boot's log, start a fresh one */
	sceIoRemove(LOG_OLD);
	sceIoRename(LOG_PATH, LOG_OLD);
	g_log_bytes = 0;

	g_thread = sceKernelCreateThread("vdreturn", worker, 191, 0x4000, 0, 0, NULL);
	if (g_thread >= 0 && sceKernelStartThread(g_thread, 0, NULL) < 0) {
		sceKernelDeleteThread(g_thread);
		g_thread = -1;
	}
	if (g_thread < 0) log_line("Could not start the thread: 0x%08X", (unsigned)g_thread);
	return SCE_KERNEL_START_SUCCESS;
}

int module_stop(SceSize args, void *argp)
{
	(void)args; (void)argp;
	g_run = 0;
	if (g_thread >= 0) {
		SceUInt timeout = 2 * POLL_US;
		sceKernelWaitThreadEnd(g_thread, NULL, &timeout);
		sceKernelDeleteThread(g_thread);
	}
	return SCE_KERNEL_STOP_SUCCESS;
}
