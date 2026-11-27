/* PS Vita platform layer: display, input, file IO, app scan, launching. */
#include "platform.h"
#include <psp2/display.h>
#include <psp2/ctrl.h>
#include <psp2/touch.h>
#include <psp2/rtc.h>
#include <psp2/power.h>
#include <psp2/appmgr.h>
#include <psp2/avconfig.h>
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

int plat_bg_paths(const char *id, char out[][128], int max)
{
	static const char *FMT[] = {
		"ux0:data/VitaDeck/covers/%s.png",                   /* user supplied */
		"ux0:data/VitaDeck/covers/%s.jpg",
		"ur0:appmeta/%s/pic0.png",                           /* system copies (decrypted) */
		"ur0:appmeta/%s/livearea/contents/bg0.png",
		"ux0:app/%s/sce_sys/pic0.png",                       /* plain for homebrew */
		"ux0:app/%s/sce_sys/livearea/contents/bg0.png",
		"ux0:app/%s/sce_sys/livearea/contents/bg.png",
	};
	int n = 0;
	for (unsigned i = 0; i < sizeof FMT / sizeof FMT[0] && n < max; i++) {
		char p[128];
		snprintf(p, sizeof p, FMT[i], id);
		if (file_exists(p)) { snprintf(out[n], 128, "%s", p); n++; }
	}
	return n;
}

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

int plat_plugin_uninstall(void)
{
	int r = plugin_write_config(0);
	if (r == 0) sceIoRemove(PLUG_DST);   /* the running copy stays in memory until reboot */
	return r;
}

void plat_plugin_cfg_load(int *autostart, int *ret)
{
	*autostart = 1; *ret = 1;
	unsigned char *d; int len;
	if (plat_read_file(PLUG_CFG, &d, &len) < 0) return;
	for (int i = 0; i < len; ) {
		const char *l = (const char *)d + i;
		int rest = len - i;
		if (rest > 10 && !strncmp(l, "autostart=", 10)) *autostart = l[10] != '0';
		if (rest > 7 && !strncmp(l, "return=", 7)) *ret = l[7] != '0';
		while (i < len && d[i] != '\n') i++;
		i++;
	}
	free(d);
}

int plat_plugin_cfg_save(int autostart, int ret)
{
	char b[48];
	int n = snprintf(b, sizeof b, "autostart=%d\nreturn=%d\n", autostart ? 1 : 0, ret ? 1 : 0);
	return plat_write_file(PLUG_CFG, b, n);
}

/* ---------- network probe (cover download, step 1) ---------- */
#define NET_LOG  "ux0:data/VitaDeck/net.log"
#define NET_TEST_URL "https://raw.githubusercontent.com/andiweli/hexflow-covers/main/Covers/PSVita/PCSB00321.png"

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

/* The Vita's own SSL library (FW 3.65) cannot complete a TLS handshake with
   raw.githubusercontent.com – it fails with and without certificate checks
   (SceHttp 0x80431075 / SceSsl 0x80435061, tested on hardware). So downloads
   use libcurl with mbedTLS and the Mozilla CA bundle shipped in the VPK. */
#define NET_CA_BUNDLE "app0:res/cacert.pem"

typedef struct { int total; unsigned char head[8]; } NetSink;

static size_t net_sink(void *ptr, size_t size, size_t nmemb, void *ud)
{
	NetSink *s = ud;
	size_t n = size * nmemb;
	if (s->total < 8) {
		size_t k = 8 - s->total < n ? 8 - s->total : n;
		memcpy(s->head + s->total, ptr, k);
	}
	s->total += (int)n;
	return s->total > 4 * 1024 * 1024 ? 0 : n;   /* abort anything absurdly large */
}

/* one GET of NET_TEST_URL; returns the CURLcode, fills HTTP status / bytes / PNG check */
static int net_get_once(int verify, long *status, int *bytes, int *is_png)
{
	*status = 0; *bytes = 0; *is_png = 0;
	CURL *c = curl_easy_init();
	if (!c) { net_log("  curl_easy_init failed"); return -1; }
	char err[CURL_ERROR_SIZE] = { 0 };
	NetSink sink = { 0 };
	curl_easy_setopt(c, CURLOPT_URL, NET_TEST_URL);
	curl_easy_setopt(c, CURLOPT_USERAGENT, "VitaDeck/0.3");
	curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 10L);
	curl_easy_setopt(c, CURLOPT_TIMEOUT, 30L);
	curl_easy_setopt(c, CURLOPT_ERRORBUFFER, err);
	curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, net_sink);
	curl_easy_setopt(c, CURLOPT_WRITEDATA, &sink);
	curl_easy_setopt(c, CURLOPT_CAINFO, NET_CA_BUNDLE);
	curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, verify ? 1L : 0L);
	curl_easy_setopt(c, CURLOPT_SSL_VERIFYHOST, verify ? 2L : 0L);
	uint64_t t0 = sceKernelGetProcessTimeWide();
	CURLcode res = curl_easy_perform(c);
	unsigned ms = (unsigned)((sceKernelGetProcessTimeWide() - t0) / 1000);
	curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, status);
	char *ip = NULL;
	curl_easy_getinfo(c, CURLINFO_PRIMARY_IP, &ip);
	net_log("  curl_easy_perform: %d (%s) in %u ms, server %s", (int)res,
	        err[0] ? err : curl_easy_strerror(res), ms, ip && ip[0] ? ip : "-");
	*bytes = sink.total;
	*is_png = sink.total >= 8 && !memcmp(sink.head, "\x89PNG\r\n\x1a\n", 8);
	net_log("  HTTP %ld, received %d bytes, PNG signature: %s", *status, sink.total, *is_png ? "yes" : "no");
	curl_easy_cleanup(c);
	return (int)res;
}

int plat_net_test(char *sum, int n)
{
	sceIoRemove(NET_LOG);
	net_log("VitaDeck network test (libcurl), URL %s", NET_TEST_URL);
	net_log("LoadModule NET: 0x%08X", (unsigned)sceSysmoduleLoadModule(SCE_SYSMODULE_NET));

	static char netmem[1024 * 1024];
	SceNetInitParam p = { netmem, sizeof netmem, 0 };
	int r = sceNetInit(&p);
	net_log("sceNetInit: 0x%08X", (unsigned)r);   /* 0x80410110 = already initialised, fine */
	int ctl = sceNetCtlInit();
	net_log("sceNetCtlInit: 0x%08X", (unsigned)ctl);
	int state = -1;
	r = sceNetCtlInetGetState(&state);
	net_log("InetGetState: r=0x%08X state=%d (3 = connected)", (unsigned)r, state);
	int ok = -1;
	if (state != SCE_NETCTL_STATE_CONNECTED) {
		snprintf(sum, n, "Kein WLAN verbunden (Status %d)", state);
		goto out;
	}
	SceNetCtlInfo info;
	memset(&info, 0, sizeof info);
	if (sceNetCtlInetGetInfo(SCE_NETCTL_INFO_GET_IP_ADDRESS, &info) >= 0) net_log("IP address: %s", info.ip_address);
	net_log("CA bundle %s: %s", NET_CA_BUNDLE, file_exists(NET_CA_BUNDLE) ? "present" : "MISSING");

	CURLcode g = curl_global_init(CURL_GLOBAL_DEFAULT);
	net_log("curl_global_init: %d, %s", (int)g, curl_version());

	long status; int bytes, png;
	net_log("Attempt 1: certificate verification ON (Mozilla CA bundle)");
	int res = net_get_once(1, &status, &bytes, &png);
	if (res == CURLE_OK && status == 200 && png) {
		snprintf(sum, n, "OK mit Zertifikatsprüfung · HTTP 200 · %d KB", (bytes + 512) / 1024);
		ok = 0;
	} else {
		int res1 = res; long st1 = status;
		net_log("Attempt 2: certificate verification OFF (diagnosis only)");
		res = net_get_once(0, &status, &bytes, &png);
		if (res == CURLE_OK && status == 200 && png)
			snprintf(sum, n, "Nur ohne Prüfung OK (mit: curl %d) · %d KB", res1, (bytes + 512) / 1024);
		else
			snprintf(sum, n, "Fehlgeschlagen: curl %d / ohne Prüfung curl %d (HTTP %ld/%ld)", res1, res, st1, status);
	}
	curl_global_cleanup();
out:
	net_log("Result: %s", sum);
	if (ctl >= 0) sceNetCtlTerm();
	sceNetTerm();
	return ok;
}

int plat_set_brightness(int pct)
{
	int max = 0;
	if (sceAVConfigGetDisplayMaxBrightness(&max) < 0 || max <= 0) return -1;
	return sceAVConfigSetDisplayBrightness(max * pct / 100);
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
