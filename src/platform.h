#ifndef VL_PLATFORM_H
#define VL_PLATFORM_H
#include <stdint.h>
#include "gfx.h"

enum {
	B_UP = 1 << 0, B_DOWN = 1 << 1, B_LEFT = 1 << 2, B_RIGHT = 1 << 3,
	B_X = 1 << 4, B_O = 1 << 5, B_TRI = 1 << 6, B_SQ = 1 << 7,
	B_L = 1 << 8, B_R = 1 << 9, B_START = 1 << 10, B_SELECT = 1 << 11
};

typedef struct {
	unsigned buttons;
	int lx, ly;          /* left stick, -128..127 */
	int touch, tx, ty;   /* front touch in screen pixels */
} Input;

enum { CAT_VITA = 1, CAT_HOMEBREW = 2, CAT_EMU = 3, CAT_PSP = 4 };   /* PSP: official PSP games and Adrenaline bubbles */

typedef struct {
	char id[16];
	char title[128];
	char icon_path[128];
	char ver[8];       /* APP_VER from param.sfo, e.g. "01.00" ("" = unknown) */
	int cat;
} PlatApp;

typedef struct {
	int year, month, day, hour, minute;
} Clock;

uint64_t plat_ms(void);
uint64_t plat_us(void);
extern unsigned plat_present_us;   /* last time to hand a frame to the display */
void plat_clock(Clock *c);
int plat_battery(int *charging);
int plat_wifi(void);   /* -1 = no Wi-Fi connection, else signal strength 0..100 */
int plat_read_file(const char *path, unsigned char **data, int *len);
int plat_write_file(const char *path, const void *data, int len);
const char *plat_res(const char *name);
const char *plat_state_path(void);
const char *plat_legacy_state_path(void);  /* may return NULL */
int plat_scan_apps(PlatApp **out);
/* folder with downloaded / own box art: <dir>/<TITLEID>.png */
const char *plat_cover_dir(void);
int plat_file_exists(const char *path);
int plat_file_size(const char *path);   /* bytes, <0 if missing */
int plat_mkdir(const char *path);
int plat_storage(uint64_t *free_bytes, uint64_t *total_bytes);   /* ux0:, <0 = unknown */
/* per-game clocks through PSVshell (kernel plugin, installed by the user): VitaDeck only
   writes/reads its profile files ur0:data/PSVshell/profiles/<TITLEID> */
enum { OC_DEFAULT = 0, OC_FAST = 1, OC_MAX = 2, OC_CUSTOM = 3 };
int plat_oc_available(void);                 /* 1 if PSVshell is in the taiHEN config (cached) */
int plat_oc_get(const char *id);             /* OC_*; file IO – call from a job or a user action */
int plat_oc_set(const char *id, int preset); /* OC_DEFAULT deletes the profile; <0 = error */
/* blocking – call only from a job: bytes of the app, of its update (ux0:patch, 0 = none)
   and the update's APP_VER ("" = no update) */
int plat_app_info(const char *id, int64_t *app_bytes, int64_t *patch_bytes, char *patch_ver, int n);
/* run job(arg) on a background worker (one at a time; host: synchronously).
   Returns 0 if started, <0 if the worker is still busy. */
int plat_job(void (*job)(void *), void *arg);
int plat_job_threaded(void);   /* 1 if jobs really run on a separate thread */
int plat_launch(const char *id);   /* <0 = SCE error code; on success the Vita process exits */

/* return/autostart plugin (plugin/vdreturn.c) */
enum { PLUG_NONE = 0, PLUG_INSTALLED = 1, PLUG_OTHER_PATH = 2 };
int plat_plugin_state(char *cfg_path, int n);   /* which config.txt taiHEN uses + PLUG_* */
int plat_plugin_install(void);                  /* 0 ok, <0 error */
int plat_plugin_uninstall(void);
int plat_plugin_update(void);                   /* replace an outdated installed copy; 1 = updated */
int plat_plugin_outdated(void);                 /* installed, but SceShell still runs an older version (reboot) */
/* "TITLEID seconds" lines appended by the plugin, summed and compacted by ui.c */
const char *plat_playtime_path(void);
/* ret = return mode: 0 off, 1 after games started from VitaDeck, 2 always (also LiveArea starts) */
void plat_plugin_cfg_load(int *autostart, int *ret);
int plat_plugin_cfg_save(int autostart, int ret);

/* HTTPS downloads (libcurl + mbedTLS, certificate checks on). Blocking –
   call only from a job. Every request is logged to ux0:data/VitaDeck/net.log. */
int plat_net_begin(char *err, int n);                    /* 0 ok, <0 (e.g. no Wi-Fi) with message */
int plat_http_download(const char *url, const char *path); /* 0 saved, 1 = 404, <0 error */
void plat_net_end(void);
int plat_get_brightness(void);  /* system setting in %, <0 = unknown */
int plat_set_brightness(int pct); /* also stores it as the system setting */
extern int plat_bright_raw;       /* raw system value last read, <0 = none (diagnostics) */
int plat_get_volume(void);      /* 0..30, <0 = unavailable */
int plat_set_volume(int v);
void plat_standby(void);
void plat_reboot(void);
void plat_exit(void);

/* implemented by ui.c */
void ui_init(void);
void ui_scan(void);
int ui_frame(const Input *in, u32 *buf);   /* returns 1 if the frame changed */

#endif
