/* Host (Linux) platform for rendering screenshots of the UI. */
#include "../src/platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

static uint64_t fake_ms;
uint64_t plat_ms(void) { return fake_ms; }
uint64_t plat_us(void) { return fake_ms * 1000; }
unsigned plat_present_us;
void plat_clock(Clock *c) { c->year = 2026; c->month = 11; c->day = 25; c->hour = 14; c->minute = 32; }
int plat_battery(int *chg) { *chg = 0; return 82; }

int plat_read_file(const char *path, unsigned char **data, int *len)
{
	FILE *f = fopen(path, "rb");
	if (!f) return -1;
	fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
	*data = malloc(n); fread(*data, 1, n, f); fclose(f);
	*len = (int)n;
	return 0;
}
int plat_write_file(const char *path, const void *d, int len)
{
	FILE *f = fopen(path, "wb"); if (!f) return -1;
	fwrite(d, 1, len, f); fclose(f); return 0;
}
static char rp[256];
const char *plat_res(const char *n) { snprintf(rp, sizeof rp, "res/%s", n); return rp; }
const char *plat_state_path(void) { return "/tmp/vl_state.txt"; }
const char *plat_legacy_state_path(void) { return NULL; }

static const struct { const char *id, *t; int c; const char *icon; } DEMO[] = {
	{ "PCSB00001", "Neon Drift", CAT_VITA, "/tmp/vlicons/a.png" }, { "PCSB00002", "Kryo Station", CAT_VITA, "" },
	{ "PCSB00003", "Waldläufer", CAT_VITA, "/tmp/vlicons/b.png" }, { "PCSE00004", "Sternenfall", CAT_VITA, "" },
	{ "PCSE00005", "Tiefsee Echo", CAT_VITA, "" }, { "PCSF00006", "Gipfelsturm", CAT_VITA, "" },
	{ "PCSG00007", "Schattenklinge", CAT_VITA, "" }, { "PCSB00008", "Blockwerk", CAT_VITA, "" },
	{ "VITASHELL", "VitaShell", CAT_HOMEBREW, "/tmp/vlicons/c.png" }, { "PSPEMUCFW", "Adrenaline", CAT_HOMEBREW, "" },
	{ "AUTOPLUG2", "Autoplugin 2", CAT_HOMEBREW, "" }, { "VITADBDLD", "VitaDB Downloader", CAT_HOMEBREW, "" },
	{ "MLGL00001", "Moonlight", CAT_HOMEBREW, "" }, { "RETROVITA", "RetroArch", CAT_EMU, "" },
	{ "DEDALOX64", "DaedalusX64", CAT_EMU, "" }, { "MGBA00001", "mGBA", CAT_EMU, "" },
	{ "PCSX00001", "PCSX ReARMed", CAT_EMU, "" },
};

int plat_scan_apps(PlatApp **out)
{
	int n = sizeof DEMO / sizeof DEMO[0];
	*out = calloc(n, sizeof(PlatApp));
	for (int i = 0; i < n; i++) {
		snprintf((*out)[i].id, 16, "%s", DEMO[i].id);
		snprintf((*out)[i].title, 128, "%s", DEMO[i].t);
		snprintf((*out)[i].icon_path, 128, "%s", DEMO[i].icon);
		(*out)[i].cat = DEMO[i].c;
	}
	return n;
}
int plat_bg_paths(const char *id, char out[][128], int max)
{
	if (max < 1) return 0;
	snprintf(out[0], 128, "/tmp/vlbg/%s.png", id);
	FILE *f = fopen(out[0], "rb");
	if (!f) return 0;
	fclose(f);
	return 1;
}
int plat_job(void (*job)(void *), void *arg) { job(arg); return 0; }
int plat_job_threaded(void) { return 0; }
static int host_plug, host_as = 1, host_ret = 1;
int plat_plugin_state(char *p, int n) { snprintf(p, n, "ur0:tai/config.txt"); return host_plug; }
int plat_plugin_install(void) { host_plug = PLUG_INSTALLED; return 0; }
int plat_plugin_uninstall(void) { host_plug = PLUG_NONE; return 0; }
void plat_plugin_cfg_load(int *a, int *r) { *a = host_as; *r = host_ret; }
int plat_plugin_cfg_save(int a, int r) { host_as = a; host_ret = r; return 0; }
int plat_net_test(char *s, int n) { snprintf(s, n, "not available on the host build"); return -1; }
int plat_launch(const char *id) { printf("launch %s\n", id); return 0; }
int plat_set_brightness(int p) { (void)p; return 0; }
int plat_get_volume(void) { return 18; }
int plat_set_volume(int v) { (void)v; return 0; }
void plat_standby(void) {}
void plat_reboot(void) {}
void plat_exit(void) {}

static u32 buf[SCR_W * SCR_H];
static void run(Input *in, int frames) { for (int i = 0; i < frames; i++) { fake_ms += 16; ui_frame(in, buf); } }
static void tapb(unsigned b) { Input in = { 0 }; in.buttons = b; run(&in, 1); in.buttons = 0; run(&in, 30); }
static void shot(const char *name)
{
	char p[128]; snprintf(p, sizeof p, "/tmp/vlshots/%s.png", name);
	stbi_write_png(p, SCR_W, SCR_H, 4, buf, SCR_W * 4);
	printf("wrote %s\n", p);
}

int main(void)
{
	remove("/tmp/vl_state.txt");
	ui_init();
	Input in = { 0 };
	run(&in, 1); shot("00_boot");
	run(&in, 60);
	/* fake some history */
	tapb(B_X); run(&in, 100);           /* launch first */
	tapb(B_RIGHT); tapb(B_RIGHT); tapb(B_SQ); run(&in, 150);
	tapb(B_DOWN); tapb(B_RIGHT); tapb(B_X); run(&in, 100);
	tapb(B_UP); tapb(B_LEFT); run(&in, 160);
	shot("01_home");
	tapb(B_DOWN); run(&in, 30); shot("02_home_row2");
	tapb(B_R); run(&in, 30); shot("03_library");
	tapb(B_R); tapb(B_R); run(&in, 30); shot("04_library_homebrew");
	tapb(B_START); run(&in, 20); shot("05_menu");
	tapb(B_O); tapb(B_SELECT); run(&in, 20); shot("06_quick");
	tapb(B_O); tapb(B_L); tapb(B_L); tapb(B_RIGHT); tapb(B_TRI); run(&in, 20); shot("07_sorted");
	tapb(B_O); run(&in, 10); tapb(B_TRI); run(&in, 20); shot("08_ctx");
	return 0;
}
