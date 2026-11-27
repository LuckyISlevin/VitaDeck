/* PS Vita platform layer: display, input, file IO, app scan, launching. */
#include "platform.h"
#include "../plugin/version.h"
#include <psp2/display.h>
#include <psp2/ctrl.h>
#include <psp2/touch.h>
#include <psp2/rtc.h>
#include <psp2/power.h>
#include <psp2/appmgr.h>
#include <psp2/avconfig.h>
#include <psp2/registrymgr.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/dirent.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/cpu.h>
#include <psp2/sysmodule.h>
#include <psp2/net/net.h>
#include <psp2/net/netctl.h>
#include <curl/curl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>

#define SELF_ID "VLAUNCH01"

/* newlib heap (VitaSDK build): icons, covers, frame buffers and up to 64 hero
   backgrounds at ~760 KB each. The Makefile build sets its heap in crt.c. */
int _newlib_heap_size_user = 96 * 1024 * 1024;

uint64_t plat_ms(void) { return sceKernelGetProcessTimeWide() / 1000; }
uint64_t plat_us(void) { return sceKernelGetProcessTimeWide(); }
unsigned plat_present_us;

void plat_clock(Clock *c)
{
	SceDateTime t;
	memset(&t, 0, sizeof t);
	sceRtcGetCurrentClockLocalTime(&t);
	c->year = t.year; c->month = t.month; c->day = t.day;
	c->hour = t.hour; c->minute = t.minute;
}

int plat_battery(int *chg)
{
	*chg = scePowerIsBatteryCharging() > 0;
	int p = scePowerGetBatteryLifePercent();
	return p < 0 || p > 100 ? -1 : p;
}

int plat_read_file(const char *path, unsigned char **data, int *len)
{
	SceUID fd = sceIoOpen(path, SCE_O_RDONLY, 0);
	if (fd < 0) return -1;
	SceOff size = sceIoLseek(fd, 0, SCE_SEEK_END);
	sceIoLseek(fd, 0, SCE_SEEK_SET);
	if (size <= 0 || size > 16 * 1024 * 1024) { sceIoClose(fd); return -1; }
	unsigned char *buf = malloc((size_t)size);
	if (!buf) { sceIoClose(fd); return -1; }
	int got = 0;
	while (got < size) {
		int r = sceIoRead(fd, buf + got, (SceSize)(size - got));
		if (r <= 0) break;
		got += r;
	}
	sceIoClose(fd);
	if (got != size) { free(buf); return -1; }
	*data = buf;
	*len = got;
	return 0;
}

/* write to <path>.tmp, then replace <path>: does not rely on SCE_O_TRUNC
   (Vita3K ignores it) and never leaves a half-written state file behind */
int plat_write_file(const char *path, const void *d, int len)
{
	sceIoMkdir("ux0:data/VitaDeck", 0777);
	char tmp[160];
	snprintf(tmp, sizeof tmp, "%s.tmp", path);
	sceIoRemove(tmp);
	SceUID fd = sceIoOpen(tmp, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
	if (fd < 0) return -1;
	int r = sceIoWrite(fd, d, len);
	sceIoClose(fd);
	if (r != len) { sceIoRemove(tmp); return -1; }
	sceIoRemove(path);
	if (sceIoRename(tmp, path) >= 0) return 0;
	/* rename failed after the old file is gone: never leave nothing behind,
	   write the target directly (this matters for ur0:tai/config.txt) */
	fd = sceIoOpen(path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
	if (fd < 0) return -1;
	r = sceIoWrite(fd, d, len);
	sceIoClose(fd);
	if (r == len) sceIoRemove(tmp);
	return r == len ? 0 : -1;
}

static char resbuf[128];
const char *plat_res(const char *name) { snprintf(resbuf, sizeof resbuf, "app0:res/%s", name); return resbuf; }
const char *plat_state_path(void) { return "ux0:data/VitaDeck/state.txt"; }
const char *plat_playtime_path(void) { return "ux0:data/VitaDeck/playtime.txt"; }
const char *plat_legacy_state_path(void) { return "ux0:data/VitaLauncher/state.txt"; }

static int file_exists(const char *p)
{
	SceIoStat st;
	return sceIoGetstat(p, &st) >= 0;
}

/* param.sfo parser: returns value of a string key */
static int sfo_get(const unsigned char *d, int len, const char *key, char *out, int outn)
{
	if (len < 20 || memcmp(d, "\0PSF", 4)) return -1;
	unsigned kt = d[8] | d[9] << 8 | d[10] << 16 | (unsigned)d[11] << 24;
	unsigned dt = d[12] | d[13] << 8 | d[14] << 16 | (unsigned)d[15] << 24;
	unsigned n = d[16] | d[17] << 8 | d[18] << 16 | (unsigned)d[19] << 24;
	for (unsigned i = 0; i < n; i++) {
		const unsigned char *e = d + 20 + i * 16;
		if (e + 16 > d + len) return -1;
		unsigned ko = e[0] | e[1] << 8;
		unsigned dl = e[4] | e[5] << 8 | e[6] << 16 | (unsigned)e[7] << 24;
		unsigned doff = e[12] | e[13] << 8 | e[14] << 16 | (unsigned)e[15] << 24;
		if (kt + ko >= (unsigned)len) continue;
		if (strcmp((const char *)d + kt + ko, key)) continue;
		if (dt + doff + dl > (unsigned)len) return -1;
		int m = (int)dl < outn - 1 ? (int)dl : outn - 1;
		memcpy(out, d + dt + doff, m);
		out[m] = 0;
		return 0;
	}
	return -1;
}

static int is_retail_id(const char *id)
{
	if (strlen(id) != 9 || strncmp(id, "PCS", 3) || !isupper((unsigned char)id[3])) return 0;
	for (int i = 4; i < 9; i++) if (!isdigit((unsigned char)id[i])) return 0;
	return 1;
}

static int contains_ci(const char *h, const char *n)
{
	size_t ln = strlen(n);
	for (; *h; h++) {
		size_t i = 0;
		while (i < ln && h[i] && tolower((unsigned char)h[i]) == tolower((unsigned char)n[i])) i++;
		if (i == ln) return 1;
	}
	return 0;
}

static int classify(const char *id, const char *title)
{
	static const char *EMU[] = { "retroarch", "mgba", "daedalus", "pcsx", "ppsspp", "snes9x", "fceu", "nestopia",
		"genesis plus", "picodrive", "gpsp", "vba", "gambatte", "dosbox", "scummvm", "mame", "fba", "fbneo",
		"melonds", "nooDS", "flycast", "yabause", "mednafen", "emulator", NULL };
	if (is_retail_id(id)) return CAT_VITA;
	for (int i = 0; EMU[i]; i++) if (contains_ci(title, EMU[i])) return CAT_EMU;
	return CAT_HOMEBREW;
}

int plat_scan_apps(PlatApp **out)
{
	int cap = 64, n = 0;
	PlatApp *list = calloc(cap, sizeof(PlatApp));
	*out = list;
	if (!list) return 0;
	SceUID dfd = sceIoDopen("ux0:app");
	if (dfd < 0) return 0;
	SceIoDirent de;
	memset(&de, 0, sizeof de);
	while (sceIoDread(dfd, &de) > 0) {
		if (!SCE_S_ISDIR(de.d_stat.st_mode) || de.d_name[0] == '.') continue;
		if (strlen(de.d_name) > 12 || !strcmp(de.d_name, SELF_ID)) continue;
		if (n == cap) {
			PlatApp *nl = realloc(list, cap * 2 * sizeof(PlatApp));
			if (!nl) break;
			list = nl; cap *= 2;
		}
		PlatApp *a = &list[n];
		memset(a, 0, sizeof *a);
		snprintf(a->id, sizeof a->id, "%s", de.d_name);
		snprintf(a->title, sizeof a->title, "%s", de.d_name);

		char p[128];
		unsigned char *d = NULL; int len = 0;
		snprintf(p, sizeof p, "ur0:appmeta/%s/param.sfo", a->id);
		if (plat_read_file(p, &d, &len) < 0) {
			snprintf(p, sizeof p, "ux0:app/%s/sce_sys/param.sfo", a->id);
			if (plat_read_file(p, &d, &len) < 0) d = NULL;
		}
		if (d) {
			char t[128];
			if (sfo_get(d, len, "STITLE", t, sizeof t) == 0 && t[0]) snprintf(a->title, sizeof a->title, "%s", t);
			else if (sfo_get(d, len, "TITLE", t, sizeof t) == 0 && t[0]) snprintf(a->title, sizeof a->title, "%s", t);
			if (sfo_get(d, len, "APP_VER", t, sizeof t) == 0) snprintf(a->ver, sizeof a->ver, "%.7s", t);
			free(d);
		}
		for (char *c = a->title; *c; c++) if (*c == '\n' || *c == '\r') *c = ' ';

		snprintf(p, sizeof p, "ur0:appmeta/%s/icon0.png", a->id);
		if (!file_exists(p)) snprintf(p, sizeof p, "ux0:app/%s/sce_sys/icon0.png", a->id);
		snprintf(a->icon_path, sizeof a->icon_path, "%s", p);
		a->cat = classify(a->id, a->title);
		n++;
	}
	sceIoDclose(dfd);
	*out = list;
	return n;
}

const char *plat_cover_dir(void) { return "ux0:data/VitaDeck/covers/box"; }

/* ---------- background worker (core 2) ---------- */
static SceUID job_sema = -1;
static void (*volatile job_fn)(void *);
static void *volatile job_arg;
static volatile int job_busy;

static int job_thread(SceSize args, void *argp)
{
	(void)args; (void)argp;
	for (;;) {
		if (sceKernelWaitSema(job_sema, 1, NULL) < 0 || !job_fn) { sceKernelDelayThread(10000); continue; }
		job_fn(job_arg);
		__sync_synchronize();
		job_busy = 0;
	}
	return 0;
}

int plat_job(void (*job)(void *), void *arg)
{
	if (job_sema < 0) { job(arg); return 0; }   /* no worker: run inline */
	if (job_busy) return -1;
	job_busy = 1;
	job_fn = job;
	job_arg = arg;
	__sync_synchronize();
	sceKernelSignalSema(job_sema, 1);
	return 0;
}

int plat_job_threaded(void) { return job_sema >= 0; }

static void job_init(void)
{
	SceUID s = sceKernelCreateSema("vl_job", 0, 0, 1, NULL);
	if (s < 0) return;
	job_sema = s;   /* set before the thread starts waiting on it */
	/* lowest user priority (valid absolute range is 64..191; 0x10000100 is the
	   special "default" value and cannot be offset – 0x10000120 was rejected
	   with SCE_KERNEL_ERROR_ILLEGAL_PRIORITY and jobs silently ran inline) */
	SceUID th = sceKernelCreateThread("vl_job", job_thread, 191, 0x40000, 0,   /* 256 KB: TLS handshakes in mbedTLS need stack */
	                                  SCE_KERNEL_CPU_MASK_USER_2, NULL);
	if (th < 0 || sceKernelStartThread(th, 0, NULL) < 0) job_sema = -1;   /* fall back to inline */
}

/* read by the return plugin (plugin/vdreturn.c): "this game was started from VitaDeck" */
#define RETURN_MARKER "ux0:data/VitaDeck/return.txt"

int plat_launch(const char *id)
{
	char uri[48], mark[24];
	snprintf(uri, sizeof uri, "psgm:play?titleid=%s", id);
	int ml = snprintf(mark, sizeof mark, "%s\n", id);
	plat_write_file(RETURN_MARKER, mark, ml);
	int r = sceAppMgrLaunchAppByUri(0xFFFFF, uri);
	if (r < 0) { sceIoRemove(RETURN_MARKER); return r; }   /* launcher stays open, UI shows the error */
	sceKernelDelayThread(10000);
	sceAppMgrLaunchAppByUri(0xFFFFF, uri);
	sceKernelExitProcess(0);
	return 0;
}

/* ---------- return/autostart plugin ---------- */
#define PLUG_FILE   "vdreturn.suprx"
#define PLUG_SRC    "app0:plugin/" PLUG_FILE
#define PLUG_DST    "ur0:tai/" PLUG_FILE
#define PLUG_CFG    "ux0:data/VitaDeck/plugin.txt"

/* taiHEN reads ux0:tai/config.txt if it exists, otherwise ur0:tai/config.txt */
static const char *tai_config(void)
{
	return file_exists("ux0:tai/config.txt") ? "ux0:tai/config.txt" : "ur0:tai/config.txt";
}

/* a non-comment line that loads our plugin (any path) */
static int is_plugin_line(const char *l, int len)
{
	while (len > 0 && (*l == ' ' || *l == '\t')) { l++; len--; }
	if (len <= 0 || *l == '#') return 0;
	int fl = (int)strlen(PLUG_FILE);
	for (int i = 0; i + fl <= len; i++) if (!memcmp(l + i, PLUG_FILE, fl)) return 1;
	return 0;
}

static int is_main_line(const char *l, int len)
{
	while (len > 0 && (l[len - 1] == ' ' || l[len - 1] == '\t' || l[len - 1] == '\r')) len--;
	while (len > 0 && (*l == ' ' || *l == '\t')) { l++; len--; }
	return len == 5 && !memcmp(l, "*main", 5);
}

/* Rewrites config.txt: drops every line loading vdreturn.suprx and, if
   install, adds "ur0:tai/vdreturn.suprx" right below *main (creating the
   section at the end if needed). Keeps everything else and the line endings. */
static int edit_config(const unsigned char *src, int len, int install, char **out, int *outlen)
{
	int crlf = 0;
	for (int i = 0; i + 1 < len; i++) if (src[i] == '\r' && src[i + 1] == '\n') { crlf = 1; break; }
	const char *eol = crlf ? "\r\n" : "\n";
	const char *line = PLUG_DST;
	char *o = malloc(len + 256);
	if (!o) return -1;
	int n = 0, inserted = 0;
	for (int i = 0; i < len; ) {
		int s = i;
		while (i < len && src[i] != '\n') i++;
		int e = i;                         /* line without '\n' */
		int has_nl = i < len;
		if (has_nl) i++;
		int l = e - s;
		if (is_plugin_line((const char *)src + s, l)) continue;
		memcpy(o + n, src + s, i - s); n += i - s;
		if (!has_nl && install && is_main_line((const char *)src + s, l)) { memcpy(o + n, eol, strlen(eol)); n += strlen(eol); }
		if (install && !inserted && is_main_line((const char *)src + s, l)) {
			n += sprintf(o + n, "%s%s", line, eol);
			inserted = 1;
		}
	}
	if (install && !inserted) {
		if (n > 0 && o[n - 1] != '\n') n += sprintf(o + n, "%s", eol);
		n += sprintf(o + n, "*main%s%s%s", eol, line, eol);
	}
	*out = o; *outlen = n;
	return 0;
}

int plat_plugin_state(char *cfg_path, int nmax)
{
	const char *cfg = tai_config();
	snprintf(cfg_path, nmax, "%s", cfg);
	unsigned char *d; int len;
	if (plat_read_file(cfg, &d, &len) < 0) return PLUG_NONE;
	int st = PLUG_NONE;
	for (int i = 0; i < len; ) {
		int s = i;
		while (i < len && d[i] != '\n') i++;
		if (is_plugin_line((const char *)d + s, i - s)) {
			const char *p = (const char *)d + s;
			while (*p == ' ' || *p == '\t') p++;
			int ours = !strncmp(p, PLUG_DST, strlen(PLUG_DST));
			if (st != PLUG_INSTALLED) st = ours && file_exists(PLUG_DST) ? PLUG_INSTALLED : PLUG_OTHER_PATH;
		}
		i++;
	}
	free(d);
	return st;
}

static int plugin_write_config(int install)
{
	const char *cfg = tai_config();
	unsigned char *d; int len;
	if (plat_read_file(cfg, &d, &len) < 0) return -2;   /* no config.txt: refuse, do not invent one */
	char bak[64];
	snprintf(bak, sizeof bak, "%s.vitadeck.bak", cfg);
	if (plat_write_file(bak, d, len) < 0) { free(d); return -3; }
	char *o; int olen;
	if (edit_config(d, len, install, &o, &olen) < 0) { free(d); return -4; }
	free(d);
	int r = plat_write_file(cfg, o, olen);
	/* verify what is on the card now */
	unsigned char *v; int vlen;
	if (r == 0 && (plat_read_file(cfg, &v, &vlen) < 0 || vlen != olen || memcmp(v, o, olen))) r = -5;
	else if (r == 0) free(v);
	free(o);
	return r;
}

int plat_plugin_install(void)
{
	unsigned char *d; int len;
	if (plat_read_file(PLUG_SRC, &d, &len) < 0) return -1;
	sceIoMkdir("ur0:tai", 0777);
	int r = plat_write_file(PLUG_DST, d, len);
	free(d);
	if (r < 0) return -6;
	return plugin_write_config(1);
}

/* The plugin writes its version when SceShell loads it (plugin/version.h).
   Installed but not running at the bundled version = a reboot is still needed. */
int plat_plugin_outdated(void)
{
	char cfg[64];
	if (plat_plugin_state(cfg, sizeof cfg) != PLUG_INSTALLED) return 0;
	unsigned char *d; int len, v = 0;
	if (plat_read_file(PLUGIN_VER_FILE, &d, &len) >= 0) {
		char b[16] = { 0 };
		memcpy(b, d, len < 15 ? len : 15);
		v = atoi(b);
		free(d);
	}
	return v != PLUGIN_VERSION;
}

/* ---------- per-game clocks via PSVshell ----------
   PSVshell (github.com/Electry/PSVshell, v1.1/v1.2beta) loads ur0:data/PSVshell/profiles/<TITLEID>
   when a game starts: char ver[8] = "PSVS0100", psvs_oc_mode_t mode[4] (0 default, 1 manual)
   and int freq[4] for CPU, GPU (ES4), BUS, XBAR. arm-vita-eabi uses 1-byte enums, so the
   modes are single bytes and the file is 28 bytes (8 + 4 + 16). VitaDeck only writes these
   data files. (VitaDeck builds before this fix wrote a 40-byte variant with int modes, which
   PSVshell misread as CPU 1 MHz – plat_oc_get rewrites those.) */
#define OC_DIR "ur0:data/PSVshell/profiles"
typedef struct { char ver[8]; uint8_t mode[4]; int32_t freq[4]; } OcProfile;
typedef struct { char ver[8]; int32_t mode[4]; int32_t freq[4]; } OcProfileBroken;   /* 40 bytes */
_Static_assert(sizeof(OcProfile) == 28, "PSVshell profile layout");

int plat_oc_available(void)
{
	static int cached = -1;
	if (cached >= 0) return cached;
	cached = 0;
	unsigned char *d; int len;
	if (plat_read_file(tai_config(), &d, &len) < 0) return cached;
	for (int i = 0; i < len; ) {
		int s = i;
		while (i < len && d[i] != '\n') i++;
		const char *l = (const char *)d + s;
		int n = i - s;
		while (n > 0 && (*l == ' ' || *l == '\t')) { l++; n--; }
		if (n > 0 && *l != '#')
			for (int k = 0; k + 14 <= n; k++) if (!strncasecmp(l + k, "psvshell.skprx", 14)) { cached = 1; break; }
		i++;
	}
	free(d);
	/* once per start (before any job runs): repair 40-byte profiles from earlier builds */
	SceUID fd = sceIoDopen(OC_DIR);
	if (fd >= 0) {
		SceIoDirent de;
		memset(&de, 0, sizeof de);
		while (sceIoDread(fd, &de) > 0) {
			if (!SCE_S_ISDIR(de.d_stat.st_mode) && de.d_stat.st_size == sizeof(OcProfileBroken) && strlen(de.d_name) < 16)
				plat_oc_get(de.d_name);
			memset(&de, 0, sizeof de);
		}
		sceIoDclose(fd);
	}
	return cached;
}

int plat_oc_get(const char *id)
{
	char p[96];
	snprintf(p, sizeof p, OC_DIR "/%s", id);
	unsigned char *d; int len;
	if (plat_read_file(p, &d, &len) < 0) return OC_DEFAULT;
	int mode[4], freq[4], ok = 0;
	if (len == (int)sizeof(OcProfile)) {
		OcProfile o; memcpy(&o, d, sizeof o);
		ok = !memcmp(o.ver, "PSVS0100", 8);
		for (int k = 0; k < 4; k++) { mode[k] = o.mode[k]; freq[k] = o.freq[k]; }
	} else if (len == (int)sizeof(OcProfileBroken)) {
		OcProfileBroken o; memcpy(&o, d, sizeof o);
		ok = !memcmp(o.ver, "PSVS0100", 8);
		for (int k = 0; k < 4; k++) { mode[k] = o.mode[k]; freq[k] = o.freq[k]; }
		ok = ok ? 2 : 0;   /* our own broken variant: repair below */
	}
	free(d);
	if (!ok) return OC_CUSTOM;
	int fast = mode[0] == 1 && freq[0] == 444, max = mode[0] == 1 && freq[0] == 500;
	int rest = mode[1] == 1 && freq[1] == 222 && mode[2] == 1 && freq[2] == 222 && mode[3] == 1 && freq[3] == 166;
	int preset = rest && fast ? OC_FAST : rest && max ? OC_MAX : OC_CUSTOM;
	if (ok == 2) {   /* rewrite in the layout PSVshell really reads (or drop it if it was no preset) */
		if (preset == OC_CUSTOM) { sceIoRemove(p); return OC_DEFAULT; }
		plat_oc_set(id, preset);
	}
	return preset;
}

int plat_oc_set(const char *id, int preset)
{
	char p[96];
	snprintf(p, sizeof p, OC_DIR "/%s", id);
	if (preset == OC_DEFAULT) { sceIoRemove(p); return 0; }
	sceIoMkdir("ur0:data/PSVshell", 0777);
	sceIoMkdir(OC_DIR, 0777);
	OcProfile o;
	memset(&o, 0, sizeof o);
	memcpy(o.ver, "PSVS0100", 8);
	for (int k = 0; k < 4; k++) o.mode[k] = 1;
	o.freq[0] = preset == OC_MAX ? 500 : 444;   /* CPU */
	o.freq[1] = 222;                             /* GPU ES4 */
	o.freq[2] = 222;                             /* BUS */
	o.freq[3] = 166;                             /* XBAR */
	return plat_write_file(p, &o, sizeof o);
}

/* An installed plugin from an older VitaDeck is replaced by the bundled one
   (SceShell keeps running the old copy until the next reboot). 1 = updated. */
int plat_plugin_update(void)
{
	char cfg[64];
	if (plat_plugin_state(cfg, sizeof cfg) != PLUG_INSTALLED) return 0;
	unsigned char *src, *dst; int ls, ld;
	if (plat_read_file(PLUG_SRC, &src, &ls) < 0) return 0;
	int differ = 1;
	if (plat_read_file(PLUG_DST, &dst, &ld) >= 0) { differ = ld != ls || memcmp(src, dst, ls); free(dst); }
	int r = differ ? plat_write_file(PLUG_DST, src, ls) : -1;
	free(src);
	return differ && r == 0;
}

int plat_plugin_uninstall(void)
{
	int r = plugin_write_config(0);
	if (r == 0) sceIoRemove(PLUG_DST);   /* the running copy stays in memory until reboot */
	return r;
}

/* plugin.txt: autostart=0|1, return=0|1, always=0|1 (missing: 1, 1, 0).
   *ret is the return mode: 0 = off, 1 = after games VitaDeck started, 2 = always */
void plat_plugin_cfg_load(int *autostart, int *ret)
{
	int r = 1, always = 0;
	*autostart = 1;
	unsigned char *d; int len;
	if (plat_read_file(PLUG_CFG, &d, &len) >= 0) {
		for (int i = 0; i < len; ) {
			const char *l = (const char *)d + i;
			int rest = len - i;
			if (rest > 10 && !strncmp(l, "autostart=", 10)) *autostart = l[10] != '0';
			if (rest > 7 && !strncmp(l, "return=", 7)) r = l[7] != '0';
			if (rest > 7 && !strncmp(l, "always=", 7)) always = l[7] != '0';
			while (i < len && d[i] != '\n') i++;
			i++;
		}
		free(d);
	}
	*ret = !r ? 0 : always ? 2 : 1;
}

int plat_plugin_cfg_save(int autostart, int ret)
{
	char b[64];
	int n = snprintf(b, sizeof b, "autostart=%d\nreturn=%d\nalways=%d\n", autostart ? 1 : 0, ret > 0, ret == 2);
	return plat_write_file(PLUG_CFG, b, n);
}

/* ---------- network: HTTPS downloads (box art) ----------
   The Vita's own SSL library (FW 3.65) cannot complete a TLS handshake with
   raw.githubusercontent.com – it fails with and without certificate checks
   (SceHttp 0x80431075 / SceSsl 0x80435061, tested on hardware). So downloads
   use libcurl with mbedTLS and the Mozilla CA bundle shipped in the VPK,
   certificate verification on. Everything here runs on the job thread. */
#define NET_LOG       "ux0:data/VitaDeck/net.log"
#define NET_CA_BUNDLE "app0:res/cacert.pem"
#define NET_MAX_FILE  (2 * 1024 * 1024)

static void net_log(const char *fmt, ...)
{
	char b[320];
	unsigned ms = (unsigned)(sceKernelGetProcessTimeWide() / 1000);
	int n = snprintf(b, sizeof b, "[%5u.%03u] ", ms / 1000, ms % 1000);
	va_list ap;
	va_start(ap, fmt);
	n += vsnprintf(b + n, sizeof b - n - 1, fmt, ap);
	va_end(ap);
	if (n > (int)sizeof b - 2) n = sizeof b - 2;
	b[n++] = '\n';
	sceIoMkdir("ux0:data/VitaDeck", 0777);
	SceUID fd = sceIoOpen(NET_LOG, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0666);
	if (fd < 0) return;
	sceIoWrite(fd, b, n);
	sceIoClose(fd);
}

static CURL *net_curl;
static int net_ctl = -1;

typedef struct { unsigned char *buf; int len, cap; } NetBuf;

static size_t net_sink(void *ptr, size_t size, size_t nmemb, void *ud)
{
	NetBuf *b = ud;
	size_t n = size * nmemb;
	if (b->len + (int)n > NET_MAX_FILE) return 0;   /* abort anything absurdly large */
	if (b->len + (int)n > b->cap) {
		int cap = b->cap ? b->cap * 2 : 64 * 1024;
		while (cap < b->len + (int)n) cap *= 2;
		unsigned char *nb = realloc(b->buf, cap);
		if (!nb) return 0;
		b->buf = nb; b->cap = cap;
	}
	memcpy(b->buf + b->len, ptr, n);
	b->len += (int)n;
	return n;
}

int plat_file_exists(const char *path) { return file_exists(path); }
int plat_file_size(const char *path) { SceIoStat st; return sceIoGetstat(path, &st) < 0 ? -1 : (int)st.st_size; }

/* SceNet + SceNetCtl are brought up once at startup (main thread) and stay up:
   the header polls the Wi-Fi state, the cover job downloads through them */
static int net_rc_load, net_rc_init;
static void net_up(void)
{
	net_rc_load = sceSysmoduleLoadModule(SCE_SYSMODULE_NET);
	static char netmem[1024 * 1024];
	SceNetInitParam p = { netmem, sizeof netmem, 0 };
	net_rc_init = sceNetInit(&p);   /* 0x80410110 = already initialised */
	net_ctl = sceNetCtlInit();
}

/* -1 = no Wi-Fi (off / not connected / unknown), else signal 0..100 */
int plat_wifi(void)
{
	if (net_ctl < 0) return -1;
	int state = -1;
	if (sceNetCtlInetGetState(&state) < 0 || state != SCE_NETCTL_STATE_CONNECTED) return -1;
	SceNetCtlInfo info;
	memset(&info, 0, sizeof info);
	if (sceNetCtlInetGetInfo(SCE_NETCTL_INFO_GET_RSSI_PERCENTAGE, &info) < 0) return 100;   /* connected, strength unknown */
	return info.rssi_percentage > 100 ? 100 : (int)info.rssi_percentage;
}

int plat_net_begin(char *err, int n)
{
	sceIoRemove(NET_LOG);
	net_log("VitaDeck network session");
	net_log("LoadModule NET: 0x%08X, sceNetInit: 0x%08X (at startup)", (unsigned)net_rc_load, (unsigned)net_rc_init);
	int state = -1;
	sceNetCtlInetGetState(&state);
	net_log("sceNetCtlInit: 0x%08X, state %d (3 = connected)", (unsigned)net_ctl, state);
	if (state != SCE_NETCTL_STATE_CONNECTED) {
		snprintf(err, n, "Kein WLAN verbunden");
		plat_net_end();
		return -1;
	}
	CURLcode g = curl_global_init(CURL_GLOBAL_DEFAULT);
	net_log("curl_global_init: %d, %s", (int)g, curl_version());
	net_curl = curl_easy_init();
	if (!net_curl) { snprintf(err, n, "curl konnte nicht starten"); plat_net_end(); return -1; }
	curl_easy_setopt(net_curl, CURLOPT_USERAGENT, "VitaDeck/0.8 (+https://github.com/LuckyISlevin/VitaDeck)");
	curl_easy_setopt(net_curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(net_curl, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(net_curl, CURLOPT_CONNECTTIMEOUT, 10L);
	curl_easy_setopt(net_curl, CURLOPT_TIMEOUT, 30L);
	curl_easy_setopt(net_curl, CURLOPT_CAINFO, NET_CA_BUNDLE);
	curl_easy_setopt(net_curl, CURLOPT_SSL_VERIFYPEER, 1L);
	curl_easy_setopt(net_curl, CURLOPT_SSL_VERIFYHOST, 2L);
	curl_easy_setopt(net_curl, CURLOPT_WRITEFUNCTION, net_sink);
	return 0;
}

/* GET url and store the body at path (via .tmp + rename). Only stores PNG/JPEG.
   Returns 0 = saved, 1 = not on the server (404), <0 = error. */
int plat_http_download(const char *url, const char *path)
{
	if (!net_curl) return -1;
	NetBuf b = { 0 };
	char err[CURL_ERROR_SIZE] = { 0 };
	curl_easy_setopt(net_curl, CURLOPT_URL, url);
	curl_easy_setopt(net_curl, CURLOPT_WRITEDATA, &b);
	curl_easy_setopt(net_curl, CURLOPT_ERRORBUFFER, err);
	uint64_t t0 = sceKernelGetProcessTimeWide();
	CURLcode res = curl_easy_perform(net_curl);
	long status = 0;
	curl_easy_getinfo(net_curl, CURLINFO_RESPONSE_CODE, &status);
	unsigned ms = (unsigned)((sceKernelGetProcessTimeWide() - t0) / 1000);
	int r;
	int is_img = b.len >= 8 && (!memcmp(b.buf, "\x89PNG\r\n\x1a\n", 8) || (b.buf[0] == 0xFF && b.buf[1] == 0xD8));
	if (res != CURLE_OK) r = -2;
	else if (status == 404) r = 1;
	else if (status != 200 || !is_img) r = -3;
	else r = plat_write_file(path, b.buf, b.len) < 0 ? -4 : 0;
	net_log("GET %s -> curl %d, HTTP %ld, %d bytes, %u ms: %s%s%s", url, (int)res, status, b.len, ms,
	        r == 0 ? "saved" : r == 1 ? "not found" : "FAILED", err[0] ? " – " : "", err);
	free(b.buf);
	return r;
}

void plat_net_end(void)
{
	if (net_curl) { curl_easy_cleanup(net_curl); net_curl = NULL; curl_global_cleanup(); }
	/* SceNet/SceNetCtl stay up (net_up): the header keeps showing the Wi-Fi state */
}

int plat_mkdir(const char *path) { return sceIoMkdir(path, 0777); }

int plat_storage(uint64_t *free_b, uint64_t *total_b)
{
	uint64_t mx = 0, fr = 0;
	int r = sceAppMgrGetDevInfo("ux0:", &mx, &fr);
	if (r < 0 || !mx) return -1;
	*free_b = fr; *total_b = mx;
	return 0;
}

/* recursive folder size; iterative with a small path stack (no deep recursion on the job stack) */
static int64_t dir_size(const char *root)
{
	enum { DEPTH = 24 };
	static char stack[DEPTH][256];
	int sp = 0;
	int64_t sum = 0;
	snprintf(stack[sp++], sizeof stack[0], "%s", root);
	while (sp > 0) {
		char dir[256];
		snprintf(dir, sizeof dir, "%s", stack[--sp]);
		SceUID fd = sceIoDopen(dir);
		if (fd < 0) continue;
		SceIoDirent de;
		memset(&de, 0, sizeof de);
		while (sceIoDread(fd, &de) > 0) {
			if (de.d_name[0] == '.' && (!de.d_name[1] || (de.d_name[1] == '.' && !de.d_name[2]))) continue;
			if (SCE_S_ISDIR(de.d_stat.st_mode)) {
				size_t dl = strlen(dir), nl = strlen(de.d_name);
				if (sp < DEPTH && dl + 1 + nl < sizeof stack[0]) {   /* longer paths are skipped, not truncated */
					memcpy(stack[sp], dir, dl); stack[sp][dl] = '/';
					memcpy(stack[sp] + dl + 1, de.d_name, nl + 1);
					sp++;
				}
			} else sum += de.d_stat.st_size;
			memset(&de, 0, sizeof de);
		}
		sceIoDclose(fd);
	}
	return sum;
}

int plat_app_info(const char *id, int64_t *app_b, int64_t *patch_b, char *patch_ver, int n)
{
	char p[128];
	snprintf(p, sizeof p, "ux0:app/%s", id);
	*app_b = dir_size(p);
	snprintf(p, sizeof p, "ux0:patch/%s", id);
	*patch_b = file_exists(p) ? dir_size(p) : 0;
	patch_ver[0] = 0;
	snprintf(p, sizeof p, "ux0:patch/%s/sce_sys/param.sfo", id);
	unsigned char *d; int len;
	if (*patch_b > 0 && plat_read_file(p, &d, &len) >= 0) {
		char t[16];
		if (sfo_get(d, len, "APP_VER", t, sizeof t) == 0) snprintf(patch_ver, n, "%s", t);
		free(d);
	}
	return 0;
}

/* Brightness: SceAVConfig only drives the panel; the system setting (what the
   PS button quick menu shows and restores) lives in the registry key
   /CONFIG/DISPLAY/brightness, in the same units (21..max). VitaDeck reads it at
   startup and writes it back on every change so both stay in sync. */
#define BRIGHT_MIN 21
int plat_bright_raw = -1;   /* last registry value read (quick access shows it) */

int plat_get_brightness(void)
{
	int max = 0, v = 0;
	if (sceAVConfigGetDisplayMaxBrightness(&max) < 0 || max <= 0) return -1;
	if (sceRegMgrGetKeyInt("/CONFIG/DISPLAY", "brightness", &v) < 0) return -1;
	plat_bright_raw = v;
	if (v < BRIGHT_MIN || v > max) return -1;   /* implausible: let the caller fall back */
	return (v * 100 + max / 2) / max;
}

int plat_set_brightness(int pct)
{
	int max = 0;
	if (sceAVConfigGetDisplayMaxBrightness(&max) < 0 || max <= 0) return -1;
	int v = max * pct / 100;
	if (v < BRIGHT_MIN) v = BRIGHT_MIN;
	int r = sceAVConfigSetDisplayBrightness(v);
	if (r >= 0) sceRegMgrSetKeyInt("/CONFIG/DISPLAY", "brightness", v);
	return r;
}

int plat_get_volume(void)
{
	int v = 0;
	if (sceAVConfigGetSystemVol(&v) < 0) return -1;
	return v;
}

int plat_set_volume(int v) { return sceAVConfigSetSystemVol(v); }
void plat_standby(void) { scePowerRequestSuspend(); }
void plat_reboot(void) { scePowerRequestColdReset(); }
void plat_exit(void) { sceKernelExitProcess(0); }

/* ---------- display: present thread ----------
   The UI renders into one of two back buffers in main RAM. A second thread on
   another CPU core copies the finished frame into the CDRAM framebuffer and
   flips it, so the ~6 ms copy overlaps with rendering the next frame. */
#define FB_SIZE  0x200000
#define FB_BYTES (SCR_W * SCR_H * 4)

static void *fbmem[2];
static u32 *backs[2];
static volatile int present_idx;
static SceUID sem_go, sem_free;

static int present_thread(SceSize args, void *argp)
{
	(void)args; (void)argp;
	int cur = 0;
	for (;;) {
		sceKernelWaitSema(sem_go, 1, NULL);
		uint64_t t0 = plat_us();
		memcpy(fbmem[cur], backs[present_idx], FB_BYTES);
		plat_present_us = (unsigned)(plat_us() - t0);
		sceKernelSignalSema(sem_free, 1);   /* back buffer may be reused */
		SceDisplayFrameBuf fb = { sizeof(fb), fbmem[cur], SCR_W, SCE_DISPLAY_PIXELFORMAT_A8B8G8R8, SCR_W, SCR_H };
		sceDisplaySetFrameBuf(&fb, SCE_DISPLAY_SETBUF_NEXTFRAME);
		cur ^= 1;
		sceDisplayWaitVblankStart();        /* flip done: fbmem[cur] is free again */
	}
	return 0;
}

int main(void)
{
	/* software renderer: run the CPU at full speed (default for apps is 333 MHz).
	   The system restores its own clocks for the next app. */
	scePowerSetArmClockFrequency(444);
	scePowerSetBusClockFrequency(222);

	/* we are running again: a left-over marker from an earlier game is stale */
	sceIoRemove(RETURN_MARKER);
	net_up();

	for (int i = 0; i < 2; i++) {
		SceUID id = sceKernelAllocMemBlock("vl_fb", SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW, FB_SIZE, NULL);
		if (id < 0 || sceKernelGetMemBlockBase(id, &fbmem[i]) < 0) return 1;
		memset(fbmem[i], 0, FB_SIZE);
		backs[i] = malloc(FB_BYTES);
		if (!backs[i]) return 1;
		memset(backs[i], 0, FB_BYTES);
	}

	sem_go = sceKernelCreateSema("vl_present_go", 0, 0, 1, NULL);
	sem_free = sceKernelCreateSema("vl_present_free", 0, 1, 1, NULL);
	SceUID th = -1;
	if (sem_go >= 0 && sem_free >= 0) {
		sceKernelChangeThreadCpuAffinityMask(sceKernelGetThreadId(), SCE_KERNEL_CPU_MASK_USER_0);
		th = sceKernelCreateThread("vl_present", present_thread, 0x10000100, 0x4000, 0,
		                           SCE_KERNEL_CPU_MASK_USER_1, NULL);
		if (th >= 0 && sceKernelStartThread(th, 0, NULL) < 0) th = -1;
	}
	int threaded = th >= 0;
	job_init();

	sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG_WIDE);
	sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);

	ui_init();
	int k = 0, cur = 0;
	for (;;) {
		SceCtrlData pad;
		memset(&pad, 0, sizeof pad);
		sceCtrlPeekBufferPositive(0, &pad, 1);
		Input in;
		memset(&in, 0, sizeof in);
		unsigned b = pad.buttons;
		if (b & SCE_CTRL_UP) in.buttons |= B_UP;
		if (b & SCE_CTRL_DOWN) in.buttons |= B_DOWN;
		if (b & SCE_CTRL_LEFT) in.buttons |= B_LEFT;
		if (b & SCE_CTRL_RIGHT) in.buttons |= B_RIGHT;
		if (b & SCE_CTRL_CROSS) in.buttons |= B_X;
		if (b & SCE_CTRL_CIRCLE) in.buttons |= B_O;
		if (b & SCE_CTRL_TRIANGLE) in.buttons |= B_TRI;
		if (b & SCE_CTRL_SQUARE) in.buttons |= B_SQ;
		if (b & (SCE_CTRL_LTRIGGER | SCE_CTRL_L1)) in.buttons |= B_L;
		if (b & (SCE_CTRL_RTRIGGER | SCE_CTRL_R1)) in.buttons |= B_R;
		if (b & SCE_CTRL_START) in.buttons |= B_START;
		if (b & SCE_CTRL_SELECT) in.buttons |= B_SELECT;
		in.lx = (int)pad.lx - 128;
		in.ly = (int)pad.ly - 128;

		SceTouchData td;
		memset(&td, 0, sizeof td);
		if (sceTouchPeek(SCE_TOUCH_PORT_FRONT, &td, 1) >= 0 && td.reportNum > 0) {
			in.touch = 1;
			in.tx = td.report[0].x / 2;
			in.ty = td.report[0].y / 2;
		}

		if (!ui_frame(&in, backs[k])) {
			sceDisplayWaitVblankStart();      /* nothing changed: just pace input polling */
			continue;
		}
		if (threaded) {
			/* wait until the previous frame was copied, then hand this one over;
			   pacing to the display comes from the present thread's vblank wait */
			sceKernelWaitSema(sem_free, 1, NULL);
			present_idx = k;
			sceKernelSignalSema(sem_go, 1);
			k ^= 1;
		} else {
			uint64_t t0 = plat_us();
			memcpy(fbmem[cur], backs[k], FB_BYTES);
			plat_present_us = (unsigned)(plat_us() - t0);
			SceDisplayFrameBuf fb = { sizeof(fb), fbmem[cur], SCR_W, SCE_DISPLAY_PIXELFORMAT_A8B8G8R8, SCR_W, SCR_H };
			sceDisplaySetFrameBuf(&fb, SCE_DISPLAY_SETBUF_NEXTFRAME);
			cur ^= 1;
			sceDisplayWaitVblankStart();
		}
	}
	return 0;
}
