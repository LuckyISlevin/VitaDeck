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

enum { CAT_VITA = 1, CAT_HOMEBREW = 2, CAT_EMU = 3 };

typedef struct {
	char id[16];
	char title[128];
	char icon_path[128];
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
int plat_read_file(const char *path, unsigned char **data, int *len);
int plat_write_file(const char *path, const void *data, int len);
const char *plat_res(const char *name);
const char *plat_state_path(void);
const char *plat_legacy_state_path(void);  /* may return NULL */
int plat_scan_apps(PlatApp **out);
/* candidate files for an app's background picture, best first; returns count */
int plat_bg_paths(const char *id, char out[][128], int max);
/* run job(arg) on a background worker (one at a time; host: synchronously).
   Returns 0 if started, <0 if the worker is still busy. */
int plat_job(void (*job)(void *), void *arg);
int plat_job_threaded(void);   /* 1 if jobs really run on a separate thread */
int plat_launch(const char *id);

/* return/autostart plugin (plugin/vdreturn.c) */
enum { PLUG_NONE = 0, PLUG_INSTALLED = 1, PLUG_OTHER_PATH = 2 };
int plat_plugin_state(char *cfg_path, int n);   /* which config.txt taiHEN uses + PLUG_* */
int plat_plugin_install(void);                  /* 0 ok, <0 error */
int plat_plugin_uninstall(void);
void plat_plugin_cfg_load(int *autostart, int *ret);
int plat_plugin_cfg_save(int autostart, int ret);   /* <0 = SCE error code; on success the Vita process exits */
int plat_set_brightness(int pct);
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
