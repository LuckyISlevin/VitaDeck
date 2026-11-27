/* VitaDeck – launcher UI (SteamOS-inspired, no third-party branding) */
#include "platform.h"
#include "gfx.h"
#include "stb_image.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

/* ---------- palette: colour schemes (Settings → "Farbschema") ----------
   All dark, so the darkened cover backgrounds and the white text on box art
   keep working. The accents of Glut/Aurora/Amethyst are the alternative
   accents of the 3E logo draft. */
typedef struct {
	const char *name;
	u32 bg, panel, head, line, line2, txt, txt2, mute, dim, acc, ink, card, btn, track, tile2, edge;
} Theme;
static const Theme THEMES[] = {
	{ "Nacht",    0x0b1016, 0x121b25, 0x0f161e, 0x1c2733, 0x22303e, 0xe6edf3, 0xb6c2cf, 0x8fa3b8, 0x6f8193,
	              0x3d9bff, 0x06121f, 0x18232f, 0x1f2b38, 0x2a3846, 0x3a4a5c, 0x2f3e4e },
	{ "Glut",     0x110d0b, 0x1a1512, 0x15110e, 0x2a221c, 0x332a23, 0xf3ece6, 0xcfc2b6, 0xb8a38f, 0x93806f,
	              0xff8a3d, 0x1f0f05, 0x211a15, 0x2b231d, 0x3d3128, 0x4f4134, 0x4a3c31 },
	{ "Aurora",   0x0a1210, 0x101b18, 0x0d1714, 0x1a2925, 0x20322d, 0xe6f3ee, 0xb6cfc5, 0x8fb8a8, 0x6f9386,
	              0x6ee7a8, 0x04170e, 0x15231f, 0x1c2d28, 0x2a403a, 0x38534b, 0x2f4842 },
	{ "Amethyst", 0x0f0c16, 0x17121f, 0x130f1a, 0x241c30, 0x2b2239, 0xede6f3, 0xc4b6cf, 0xa68fb8, 0x846f93,
	              0xc792ff, 0x14061f, 0x1d1727, 0x271f33, 0x382c48, 0x4a3c5e, 0x3e3150 },
	{ "OLED",     0x000000, 0x0b0b0d, 0x050506, 0x18181c, 0x202025, 0xededf0, 0xbdbdc4, 0x95959f, 0x74747d,
	              0x3d9bff, 0x06121f, 0x111114, 0x1a1a1f, 0x28282e, 0x36363e, 0x2c2c33 },
};
#define NTHEMES ((int)(sizeof THEMES / sizeof THEMES[0]))
static int theme_idx;
#define TH       (THEMES[theme_idx])
#define C_BG     TH.bg
#define C_PANEL  TH.panel
#define C_HEAD   TH.head
#define C_LINE   TH.line
#define C_LINE2  TH.line2
#define C_TXT    TH.txt
#define C_TXT2   TH.txt2
#define C_MUTE   TH.mute
#define C_DIM    TH.dim
#define C_ACC    TH.acc
#define C_INK    TH.ink
#define C_CARD   TH.card
#define C_BTN    TH.btn
#define C_TRACK  TH.track   /* slider/switch tracks, logo tiles */
#define C_TILE2  TH.tile2   /* lighter logo tile */
#define C_EDGE   TH.edge    /* toast border */
#define C_GREEN  0x6ee7a8
#define C_STAR   0xffd166

/* shown in the main menu; keep in sync with APP_VER in build.ps1 / CMakeLists.txt / Makefile */
#define VD_VERSION "v0.11"

/* UI font size: +2 px over the original layout sizes (chip labels <= 10 px stay) */
#define FS(n) ((n) <= 10 ? (n) : (n) + 2)

#define TILE      112   /* tile width = square icon size */
#define TILE_H    143   /* portrait tiles: box art is 250x320 */
#define HOME_PX   128   /* tile pitch on home rows */
#define ROW_H     205
#define LIB_COLS  7
#define LIB_PX    133
#define LIB_PY    190
#define HBOX_W    150   /* box art in the hero */
#define HBOX_H    192
#define MAXA      512

/* ---------- data ---------- */
typedef struct {
	char id[16];
	char title[128];
	char icon_path[128];
	char ver[8];       /* APP_VER of the installed app */
	int cat;
	int fav;
	int hidden;        /* hidden from home and library (except the "Ausgeblendet" tab) */
	int last;          /* minutes since civil epoch, 0 = never */
	unsigned play;     /* playtime in seconds (from the plugin's playtime.txt) */
	unsigned colmask;  /* custom collections this app belongs to (bit i = colls[i]) */
	Img *ic, *ics;     /* hero (128) and tile (112) icon */
	Img *cv, *cvs;     /* cached placeholder cover, 128 and 112 */
	int ic_state;      /* 0 = pending, 1 = ok, 2 = none */
	unsigned char *ica, *icsa;   /* alpha of ic/ics before flattening (NULL = opaque icon), for recolouring */
	Img *bg;           /* hero background (HERO_W x HERO_H, blurred + darkened, opaque) */
	Img *box, *boxh;   /* box art: tile (TILE x TILE_H) and hero (HBOX_W x HBOX_H), opaque */
	Img *bgs;          /* small blurred background (BGS_W x BGS_H), source of bg and of the cache */
	int bg_state;      /* 0 = not tried, 1 = loaded, 2 = none */
	unsigned bg_tick;  /* last use, for evicting old backgrounds */
} App;

static App *apps;

/* custom collections (ROADMAP 9): up to 8 named rows, stored as "#coll i name" in state.txt */
#define MAXCOLL 8
static char colls[MAXCOLL][28];   /* "" = unused slot */
static char search[48];           /* library search ("" = off), UTF-8 */
static int napps;

typedef struct { const char *title; int n; int idx[MAXA]; } Row;
#define MAXROWS (6 + MAXCOLL)   /* recent, favourites, collections, Vita, PSP, homebrew, emulators */
static Row rows[MAXROWS];
static int nrows;
static int lib[MAXA], nlib;
static int lists_dirty = 1;

/* library tabs: logical ids (index into TABS); S.tab indexes the *visible* list,
   in which "PSP" only appears with PSP content and "Ausgeblendet" only with hidden apps */
static const char *TABS[] = { "Alle", "Vita", "Homebrew", "Emulatoren", "Favoriten", "Ausgeblendet", "PSP" };
#define TAB_FAV    4
#define TAB_HIDDEN 5
#define TAB_PSP    6
static int ntabs(void);
static int tab_id(int visible);
static const char *SORTS[] = { "A–Z", "Zuletzt gestartet", "Spielzeit" };
#define NSORTS 3

enum { V_BOOT, V_HOME, V_LIB };
enum { OV_NONE, OV_MENU, OV_QA, OV_CTX, OV_LAUNCH, OV_SET, OV_CONFIRM, OV_KBD, OV_COLL };

static struct {
	int view, ov;
	int boot;
	int hrow, hcol;
	int tab, lidx, sort;
	int mi, qi, ci, ctx_app;
	int launch_app, launch_frames, launch_prev_last;
	int bright, vol, vol_ok, bright_ok;
	int sound;            /* navigation sounds on/off (#sound in state.txt, default on) */
	char toast[128];
	uint64_t toast_until;
	int dirty;
	float sy, sx[MAXROWS], ly;  /* animated scroll */
	int hoff[MAXROWS], loff;    /* scroll targets: first visible column per home row / first library row */
	int last_minute;
	int si, set_as, set_ret, plug, plug_old, confirm;   /* settings overlay */
	int coi, coll_del;    /* collections overlay: selection, "press △ again to delete" */
	char plug_cfg[32];
	unsigned perf_us;     /* smoothed time to render a full screen (no overlay cache) */
	unsigned perf_sec[4]; /* home screen breakdown: header, hero, tiles, footer */
} S;

static void perf_add(int k, uint64_t *t)
{
	uint64_t now = plat_us();
	unsigned dt = (unsigned)(now - *t);
	S.perf_sec[k] = S.perf_sec[k] ? (S.perf_sec[k] * 3 + dt) / 4 : dt;
	*t = now;
}

/* overlay background cache: base view + dimming, rendered once per overlay */
static u32 *under;
static int under_ok;

/* ---------- time helpers ---------- */
static int days_from_civil(int y, int m, int d)
{
	y -= m <= 2;
	int era = (y >= 0 ? y : y - 399) / 400;
	int yoe = y - era * 400;
	int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
	int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	return era * 146097 + doe - 719468;
}

static int now_minutes(void)
{
	Clock c; plat_clock(&c);
	return days_from_civil(c.year, c.month, c.day) * 1440 + c.hour * 60 + c.minute + 1;
}

static void fmt_last(int last, char *out, int n)
{
	if (!last) { snprintf(out, n, "noch nie gestartet"); return; }
	int now = now_minutes();
	int dn = (now - 1) / 1440, dl = (last - 1) / 1440;
	int hm = (last - 1) % 1440;
	int diff = dn - dl;
	if (diff <= 0) snprintf(out, n, "heute, %02d:%02d", hm / 60, hm % 60);
	else if (diff == 1) snprintf(out, n, "gestern, %02d:%02d", hm / 60, hm % 60);
	else snprintf(out, n, "vor %d Tagen", diff);
}

/* "45 Min.", "2,5 Std.", "12 Std." */
static void fmt_play(unsigned sec, char *out, int n)
{
	unsigned min = sec / 60;
	if (min == 0) snprintf(out, n, "unter 1 Min.");
	else if (min < 60) snprintf(out, n, "%u Min.", min);
	else if (min < 600) {
		unsigned t = (min + 3) / 6;   /* tenths of an hour, rounded */
		if (t % 10) snprintf(out, n, "%u,%u Std.", t / 10, t % 10);
		else snprintf(out, n, "%u Std.", t / 10);
	} else snprintf(out, n, "%u Std.", (min + 30) / 60);
}

/* "850 MB", "3,4 GB", "120 GB" */
static void fmt_size(int64_t b, char *out, int n)
{
	if (b < 0) { snprintf(out, n, "?"); return; }
	double mb = b / 1048576.0, gb = mb / 1024.0;
	if (gb >= 100) snprintf(out, n, "%d GB", (int)(gb + 0.5));
	else if (gb >= 1) { int t = (int)(gb * 10 + 0.5); snprintf(out, n, "%d,%d GB", t / 10, t % 10); }
	else snprintf(out, n, "%d MB", (int)(mb + 0.5) < 1 ? 1 : (int)(mb + 0.5));
}

static const char *cat_name(int c)
{
	return c == CAT_VITA ? "Vita" : c == CAT_EMU ? "Emulator" : c == CAT_PSP ? "PSP" : "Homebrew";
}

/* ---------- persistence ---------- */
static App *find_app(const char *id)
{
	for (int i = 0; i < napps; i++) if (!strcmp(apps[i].id, id)) return &apps[i];
	return NULL;
}

static void load_state(void)
{
	unsigned char *d; int len;
	if (plat_read_file(plat_state_path(), &d, &len) < 0) {
		const char *old = plat_legacy_state_path();
		if (!old || plat_read_file(old, &d, &len) < 0) return;
	}
	char *s = malloc(len + 1);
	if (!s) { free(d); return; }
	memcpy(s, d, len); s[len] = 0; free(d);
	char *line = strtok(s, "\n");
	while (line) {
		char id[16], cname[28]; int fav, last, hid = 0; unsigned cm = 0;
		/* "id fav last [hidden [collections]]" – columns 4 and 5 are newer, older files have 3 */
		if (line[0] != '#' && sscanf(line, "%15s %d %d %d %u", id, &fav, &last, &hid, &cm) >= 3) {
			App *a = find_app(id);
			if (a) { a->fav = fav; a->last = last; a->hidden = hid != 0; a->colmask = cm; }
		} else if (sscanf(line, "#coll %d %27[^\r]", &fav, cname) == 2 && fav >= 0 && fav < MAXCOLL) {
			snprintf(colls[fav], sizeof colls[fav], "%s", cname);
		} else if (sscanf(line, "#bright %d", &fav) == 1) {
			S.bright = fav;
		} else if (sscanf(line, "#sound %d", &fav) == 1) {
			S.sound = fav != 0;
		} else if (sscanf(line, "#theme %d", &fav) == 1) {
			theme_idx = fav >= 0 && fav < NTHEMES ? fav : 0;
		}
		line = strtok(NULL, "\n");
	}
	free(s);
}

/* playtime.txt: the plugin appends "TITLEID seconds" (one line per minute played
   and per session end). Sum per app; once the file has grown, rewrite it with
   one line per title – also for titles that are not installed right now. */
#define PLAY_COMPACT_BYTES 16384
static void load_playtime(void)
{
	for (int i = 0; i < napps; i++) apps[i].play = 0;
	unsigned char *d; int len;
	if (plat_read_file(plat_playtime_path(), &d, &len) < 0) return;
	char *s = malloc(len + 1);
	if (!s) { free(d); return; }
	memcpy(s, d, len); s[len] = 0; free(d);
	int cap = 64, nt = 0, lines = 0;
	struct pt { char id[16]; unsigned sec; } *t = malloc(cap * sizeof *t);
	for (char *line = strtok(s, "\n"); line && t; line = strtok(NULL, "\n")) {
		char id[16]; unsigned sec;
		if (sscanf(line, "%15s %u", id, &sec) != 2 || strlen(id) != 9) continue;
		lines++;
		int k = 0;
		while (k < nt && strcmp(t[k].id, id)) k++;
		if (k == nt) {
			if (nt == cap) { void *nt2 = realloc(t, (cap *= 2) * sizeof *t); if (!nt2) break; t = nt2; }
			snprintf(t[nt].id, sizeof t[nt].id, "%s", id); t[nt].sec = 0; nt++;
		}
		t[k].sec += sec;
	}
	free(s);
	if (!t) return;
	for (int k = 0; k < nt; k++) { App *a = find_app(t[k].id); if (a) a->play = t[k].sec; }
	if (len > PLAY_COMPACT_BYTES && lines > nt) {
		char *o = malloc(nt * 24 + 1);
		if (o) {
			int n = 0;
			for (int k = 0; k < nt; k++) n += sprintf(o + n, "%s %u\n", t[k].id, t[k].sec);
			plat_write_file(plat_playtime_path(), o, n);
			free(o);
		}
	}
	free(t);
}

static void save_state(void)
{
	int cap = 64 + MAXCOLL * 40 + napps * 60, n = 0;
	char *s = malloc(cap);
	if (!s) return;
	n += snprintf(s + n, cap - n, "#bright %d\n#theme %d\n#sound %d\n", S.bright, theme_idx, S.sound);
	for (int c = 0; c < MAXCOLL; c++) if (colls[c][0]) n += snprintf(s + n, cap - n, "#coll %d %s\n", c, colls[c]);
	for (int i = 0; i < napps; i++)
		if (apps[i].fav || apps[i].last || apps[i].hidden || apps[i].colmask)
			n += snprintf(s + n, cap - n, "%s %d %d %d %u\n", apps[i].id, apps[i].fav, apps[i].last, apps[i].hidden, apps[i].colmask);
	plat_write_file(plat_state_path(), s, n);
	free(s);
}

/* ---------- lists ---------- */
/* lower-case copy for searching: ASCII plus the Latin-1 capitals in UTF-8 (Ä Ö Ü …: C3 80–9E → +0x20) */
static void fold(char *out, int n, const char *s)
{
	int k = 0;
	for (; *s && k < n - 1; s++) {
		unsigned char c = (unsigned char)*s;
		if (c == 0xC3 && (unsigned char)s[1] >= 0x80 && (unsigned char)s[1] <= 0x9E && (unsigned char)s[1] != 0x97 && k < n - 2) {
			out[k++] = (char)c; out[k++] = (char)((unsigned char)s[1] + 0x20); s++;
		} else out[k++] = (char)tolower(c);
	}
	out[k] = 0;
}

static int ci_cmp(const char *a, const char *b)
{
	for (;; a++, b++) {
		int ca = tolower((unsigned char)*a), cb = tolower((unsigned char)*b);
		if (ca != cb || !ca) return ca - cb;
	}
}

static int cmp_title(const void *x, const void *y)
{
	return ci_cmp(apps[*(const int *)x].title, apps[*(const int *)y].title);
}

static int cmp_last(const void *x, const void *y)
{
	const App *a = &apps[*(const int *)x], *b = &apps[*(const int *)y];
	if (a->last != b->last) return b->last - a->last;
	return ci_cmp(a->title, b->title);
}

static int cmp_play(const void *x, const void *y)
{
	const App *a = &apps[*(const int *)x], *b = &apps[*(const int *)y];
	if (a->play != b->play) return a->play < b->play ? 1 : -1;
	return ci_cmp(a->title, b->title);
}

static int tab_list(int out[])
{
	int psp = 0, hid = 0, n = 0;
	for (int i = 0; i < napps; i++) { if (apps[i].hidden) hid = 1; else if (apps[i].cat == CAT_PSP) psp = 1; }
	out[n++] = 0; out[n++] = 1;
	if (psp) out[n++] = TAB_PSP;
	out[n++] = 2; out[n++] = 3; out[n++] = TAB_FAV;
	if (hid) out[n++] = TAB_HIDDEN;
	return n;
}

static int ntabs(void) { int t[8]; return tab_list(t); }
static int tab_id(int visible) { int t[8], n = tab_list(t); return visible >= 0 && visible < n ? t[visible] : 0; }

static int lib_filter(int tab, int out[])   /* tab = logical id */
{
	int n = 0;
	for (int i = 0; i < napps && n < MAXA; i++) {
		App *a = &apps[i];
		int ok = tab == TAB_HIDDEN ? a->hidden :
		         !a->hidden && (tab == 0 || (tab == 1 && a->cat == CAT_VITA) || (tab == 2 && a->cat == CAT_HOMEBREW) ||
		                        (tab == 3 && a->cat == CAT_EMU) || (tab == TAB_FAV && a->fav) || (tab == TAB_PSP && a->cat == CAT_PSP));
		if (ok) out[n++] = i;
	}
	return n;
}

static void rebuild(void)
{
	int tmp[MAXA], n;
	nrows = 0;
	/* recent */
	n = 0;
	for (int i = 0; i < napps && n < MAXA; i++) if (apps[i].last && !apps[i].hidden) tmp[n++] = i;
	qsort(tmp, n, sizeof(int), cmp_last);
	Row *r = &rows[nrows];
	if (n) {
		r->title = "Zuletzt gestartet";
		r->n = n > 15 ? 15 : n;
		memcpy(r->idx, tmp, r->n * sizeof(int));
		nrows++;
	}
	static const char *ttl[] = { "Favoriten", "Vita-Spiele", "PSP-Spiele", "Homebrew", "Emulatoren" };
	int tabs[] = { TAB_FAV, 1, TAB_PSP, 2, 3 };
	for (int k = 0; k < 5 && nrows < MAXROWS; k++) {
		n = lib_filter(tabs[k], tmp);
		if (n) {
			qsort(tmp, n, sizeof(int), cmp_title);
			r = &rows[nrows++];
			r->title = ttl[k];
			r->n = n;
			memcpy(r->idx, tmp, n * sizeof(int));
		}
		if (k != 0) continue;
		/* custom collections right after the favourites */
		for (int c = 0; c < MAXCOLL && nrows < MAXROWS; c++) {
			if (!colls[c][0]) continue;
			n = 0;
			for (int i = 0; i < napps && n < MAXA; i++) if (!apps[i].hidden && (apps[i].colmask >> c & 1)) tmp[n++] = i;
			if (!n) continue;
			qsort(tmp, n, sizeof(int), cmp_title);
			r = &rows[nrows++];
			r->title = colls[c];
			r->n = n;
			memcpy(r->idx, tmp, n * sizeof(int));
		}
	}
	if (S.tab >= ntabs()) S.tab = 0;   /* "PSP" / "Ausgeblendet" vanish once empty */
	nlib = lib_filter(tab_id(S.tab), lib);
	if (search[0]) {   /* library search: keep titles containing the text (case-insensitive, umlauts too) */
		char q[64], t[160];
		fold(q, sizeof q, search);
		int m = 0;
		for (int i = 0; i < nlib; i++) { fold(t, sizeof t, apps[lib[i]].title); if (strstr(t, q)) lib[m++] = lib[i]; }
		nlib = m;
	}
	qsort(lib, nlib, sizeof(int), S.sort == 2 ? cmp_play : S.sort == 1 ? cmp_last : cmp_title);
	if (S.hrow >= nrows) S.hrow = nrows ? nrows - 1 : 0;
	if (nrows && S.hcol >= rows[S.hrow].n) S.hcol = rows[S.hrow].n - 1;
	if (S.hcol < 0) S.hcol = 0;
	if (S.lidx >= nlib) S.lidx = nlib ? nlib - 1 : 0;
	lists_dirty = 0;
}

static int home_app(void) { return nrows ? rows[S.hrow].idx[S.hcol] : -1; }
static int lib_app(void) { return nlib ? lib[S.lidx] : -1; }

/* ---------- icons ---------- */
static void flatten(Img *im, u32 bg)
{
	if (!im || im->opaque) return;
	gfx_begin_target(im->px, im->w, im->h);
	Img tmp = *im;
	tmp.px = malloc((size_t)im->w * im->h * 4);
	if (tmp.px) {
		memcpy(tmp.px, im->px, (size_t)im->w * im->h * 4);
		tmp.opaque = 0;
		gfx_rect(0, 0, im->w, im->h, bg, 255);
		gfx_img(&tmp, 0, 0, 0, 255);
		free(tmp.px);
		im->opaque = 1;
	}
	gfx_end_target();
}

static unsigned char *alpha_of(const Img *im)
{
	if (!im || im->opaque) return NULL;
	int n = im->w * im->h;
	unsigned char *m = malloc(n);
	if (m) for (int i = 0; i < n; i++) m[i] = im->px[i] >> 24;
	return m;
}

/* flattened pixel = a*icon + (1-a)*old  →  + (1-a)*(new - old) gives the same
   icon on the new card colour, without decoding the PNG again */
static void recolor(Img *im, const unsigned char *alpha, u32 oldc, u32 newc)
{
	if (!im || !alpha) return;
	int d[3];
	for (int k = 0; k < 3; k++) d[k] = (int)((newc >> (8 * k)) & 0xFF) - (int)((oldc >> (8 * k)) & 0xFF);
	int n = im->w * im->h;
	for (int i = 0; i < n; i++) {
		int t = 255 - alpha[i];
		if (!t) continue;
		u32 p = im->px[i], o = p & 0xFF000000u;
		for (int k = 0; k < 3; k++) {
			int c = (int)((p >> (8 * k)) & 0xFF) + (t * d[k] + (d[k] < 0 ? -127 : 127)) / 255;
			o |= (u32)(c < 0 ? 0 : c > 255 ? 255 : c) << (8 * k);
		}
		im->px[i] = o;
	}
}

static void load_icon(App *a)
{
	a->ic_state = 2;
	if (!a->icon_path[0]) return;
	unsigned char *d; int len;
	if (plat_read_file(a->icon_path, &d, &len) < 0) return;
	int w, h, n;
	unsigned char *rgba = stbi_load_from_memory(d, len, &w, &h, &n, 4);
	free(d);
	if (!rgba) return;
	Img *im = img_from_rgba(rgba, w, h);
	stbi_image_free(rgba);
	if (!im) return;
	if (w != h && w > 0 && h > 0) {
		/* non-square icons (PSP: 144x80) are fitted, not stretched: centred on a transparent square */
		int nw = w > h ? 128 : 128 * w / h, nh = w > h ? 128 * h / w : 128;
		Img *s = img_scaled(im, nw, nh);
		img_free(im);
		im = NULL;
		Img *sq = s ? malloc(sizeof(Img)) : NULL;
		if (sq && (sq->px = calloc(128 * 128, 4))) {
			sq->w = sq->h = 128; sq->avg = s->avg; sq->opaque = 0;
			for (int j = 0; j < nh; j++)
				memcpy(sq->px + (size_t)((128 - nh) / 2 + j) * 128 + (128 - nw) / 2, s->px + (size_t)j * nw, (size_t)nw * 4);
			im = sq;
		} else free(sq);
		img_free(s);
	} else if (w != 128) { Img *s = img_scaled(im, 128, 128); img_free(im); im = s; }
	if (!im) return;
	a->ics = img_scaled(im, TILE, TILE);
	/* pre-composite onto the card colour (what used to be drawn underneath
	   every frame), so both images are opaque and blit as plain row copies;
	   keep the alpha so a colour scheme change can swap the card colour */
	a->ica = alpha_of(im);
	a->icsa = alpha_of(a->ics);
	flatten(im, rgb(C_CARD));
	flatten(a->ics, rgb(C_CARD));
	a->ic = im;
	a->ic_state = 1;
}

/* ---------- box art + hero backgrounds ----------
   Box art lives in <plat_cover_dir()>/<TITLEID>.png – downloaded from
   HexFlow-Covers (Einstellungen → Cover laden) or put there by the user.
   The job thread decodes it and builds three opaque images: the portrait
   tile, the hero cover and a blurred, darkened hero background. Apps without
   box art get the background from their icon. The worker only touches
   `bgjob` and must not call gfx_* (global state) – everything here is
   plain pixel code. */
#define HERO_W  912
#define HERO_H  214   /* home rows below show only the next category headline */
#define BG_KEEP 64         /* apps whose images stay in memory (~940 KB each) */

static struct {
	volatile int state;    /* 0 idle, 1 running, 2 done */
	char id[16];
	char icon[128];        /* fallback source for the background */
	Img *bg, *box, *boxh, *bgs;   /* results */
	Img *in_small;               /* input: cached small blur (owned by the job) */
} bgjob;
static int bg_focus = -1, bg_ok, bg_bad, bg_none, bg_cursor, bg_loaded;
static uint64_t bg_since;
static uint64_t last_input;   /* plat_ms() of the last button/touch activity */
static int icon_cursor;       /* icons are loaded on the UI thread, see pump_icons */
static unsigned bg_clock;

/* composite onto a solid colour so the image can be blitted as plain rows */
static void to_opaque(Img *im, u32 bgc)
{
	if (!im) return;
	int br = bgc & 0xFF, bg = (bgc >> 8) & 0xFF, bb = (bgc >> 16) & 0xFF;
	for (int i = 0; i < im->w * im->h; i++) {
		u32 c = im->px[i];
		int a = c >> 24, ia = 255 - a;
		int r = ((c & 0xFF) * a + br * ia) / 255;
		int g = (((c >> 8) & 0xFF) * a + bg * ia) / 255;
		int b = (((c >> 16) & 0xFF) * a + bb * ia) / 255;
		im->px[i] = 0xFF000000u | (u32)b << 16 | (u32)g << 8 | (u32)r;
	}
	im->opaque = 1;
}

/* bilinear scaling (img_scaled only averages down / repeats pixels up) */
static Img *scale_bilinear(const Img *s, int w, int h)
{
	Img *d = malloc(sizeof(Img));
	if (!d) return NULL;
	d->w = w; d->h = h; d->avg = s->avg; d->opaque = s->opaque;
	d->px = malloc((size_t)w * h * 4);
	if (!d->px) { free(d); return NULL; }
	for (int j = 0; j < h; j++) {
		float fy = (j + 0.5f) * s->h / h - 0.5f;
		int y0 = fy < 0 ? 0 : (int)fy, y1 = y0 + 1 < s->h ? y0 + 1 : y0;
		int wy = (int)((fy - y0) * 256); if (wy < 0) wy = 0;
		for (int i = 0; i < w; i++) {
			float fx = (i + 0.5f) * s->w / w - 0.5f;
			int x0 = fx < 0 ? 0 : (int)fx, x1 = x0 + 1 < s->w ? x0 + 1 : x0;
			int wx = (int)((fx - x0) * 256); if (wx < 0) wx = 0;
			u32 a = s->px[y0 * s->w + x0], b = s->px[y0 * s->w + x1];
			u32 c = s->px[y1 * s->w + x0], e = s->px[y1 * s->w + x1];
			u32 o = 0;
			for (int k = 0; k < 32; k += 8) {
				int top = (((a >> k) & 0xFF) * (256 - wx) + ((b >> k) & 0xFF) * wx) >> 8;
				int bot = (((c >> k) & 0xFF) * (256 - wx) + ((e >> k) & 0xFF) * wx) >> 8;
				o |= (u32)((top * (256 - wy) + bot * wy) >> 8) << k;
			}
			d->px[j * w + i] = o;
		}
	}
	return d;
}

/* separable box blur, radius r, in place */
static void box_blur(Img *im, int r)
{
	int w = im->w, h = im->h, n = 2 * r + 1;
	u32 *tmp = malloc((size_t)w * h * 4);
	if (!tmp) return;
	for (int pass = 0; pass < 2; pass++) {
		const u32 *src = pass ? tmp : im->px;
		u32 *dst = pass ? im->px : tmp;
		for (int j = 0; j < h; j++)
			for (int i = 0; i < w; i++) {
				int s[3] = { 0, 0, 0 };
				for (int k = -r; k <= r; k++) {
					int x = pass ? i : i + k, y = pass ? j + k : j;
					x = x < 0 ? 0 : x >= w ? w - 1 : x;
					y = y < 0 ? 0 : y >= h ? h - 1 : y;
					u32 c = src[y * w + x];
					s[0] += c & 0xFF; s[1] += (c >> 8) & 0xFF; s[2] += (c >> 16) & 0xFF;
				}
				dst[j * w + i] = 0xFF000000u | (u32)(s[2] / n) << 16 | (u32)(s[1] / n) << 8 | (u32)(s[0] / n);
			}
	}
	free(tmp);
}

/* darken towards the panel colour: strong on the left behind the text, lighter on the right */
static void darken_hero(Img *im)
{
	u32 pc = rgb(C_PANEL);
	int pr = pc & 0xFF, pg = (pc >> 8) & 0xFF, pb = (pc >> 16) & 0xFF;
	static int ov[HERO_W];
	for (int i = 0; i < HERO_W; i++) {
		float t = (float)i / (HERO_W - 1);
		float s = t < 0.12f ? 0.f : t > 0.8f ? 1.f : (t - 0.12f) / 0.68f;
		s = s * s * (3 - 2 * s);
		ov[i] = (int)(226 - 110 * s);          /* panel overlay strength, 0..256 */
	}
	for (int j = 0; j < im->h; j++) {
		u32 *p = im->px + j * im->w;
		for (int i = 0; i < im->w && i < HERO_W; i++) {
			u32 c = p[i];
			int a = ov[i], ia = 256 - a;
			int r = ((c & 0xFF) * ia + pr * a) >> 8;
			int g = (((c >> 8) & 0xFF) * ia + pg * a) >> 8;
			int b = (((c >> 16) & 0xFF) * ia + pb * a) >> 8;
			p[i] = 0xFF000000u | (u32)b << 16 | (u32)g << 8 | (u32)r;
		}
	}
	im->opaque = 1;
}

/* blurred hero background, step 1: centre strip in hero aspect, shrunk to
   BGS_W x BGS_H and blurred. This small image is what gets cached. */
#define BGS_W (HERO_W / 8)
#define BGS_H (HERO_H / 8)
static Img *make_blur_small(const Img *src)
{
	int w = src->w, h = src->h;
	int cw = w, ch = h, cx = 0, cy = 0;
	if (w * HERO_H > h * HERO_W) { cw = h * HERO_W / HERO_H; cx = (w - cw) / 2; }
	else { ch = w * HERO_H / HERO_W; cy = (h - ch) * 2 / 5; }
	if (cw < 4 || ch < 2) return NULL;
	Img crop = { .w = cw, .h = ch, .px = malloc((size_t)cw * ch * 4) };
	if (!crop.px) return NULL;
	for (int j = 0; j < ch; j++)
		memcpy(crop.px + j * cw, src->px + (size_t)(cy + j) * w + cx, (size_t)cw * 4);
	to_opaque(&crop, rgb(C_PANEL));
	Img *small = img_scaled(&crop, BGS_W, BGS_H);
	free(crop.px);
	if (!small) return NULL;
	box_blur(small, 2);
	box_blur(small, 2);
	small->opaque = 1;
	return small;
}

/* step 2: scale the small blurred image up smoothly and darken it (a few ms) */
static Img *bg_from_small(const Img *small)
{
	Img *big = small ? scale_bilinear(small, HERO_W, HERO_H) : NULL;
	if (big) darken_hero(big);
	return big;
}

/* scale to w x h keeping the aspect: crop the centre (worker-safe, no gfx state) */
static Img *scale_fill(const Img *src, int w, int h)
{
	int sw = src->w, sh = src->h, cw = sw, ch = sh, cx = 0, cy = 0;
	if ((int64_t)sw * h > (int64_t)sh * w) { cw = (int)((int64_t)sh * w / h); cx = (sw - cw) / 2; }
	else { ch = (int)((int64_t)sw * h / w); cy = (sh - ch) / 2; }
	if (cw == sw && ch == sh) return img_scaled(src, w, h);
	Img crop = { .w = cw, .h = ch, .px = malloc((size_t)cw * ch * 4), .opaque = src->opaque };
	if (!crop.px) return img_scaled(src, w, h);
	for (int j = 0; j < ch; j++) memcpy(crop.px + (size_t)j * cw, src->px + (size_t)(cy + j) * sw + cx, (size_t)cw * 4);
	Img *r = img_scaled(&crop, w, h);
	free(crop.px);
	return r;
}

static Img *decode_file(const char *path)
{
	unsigned char *d; int len;
	if (!path[0] || plat_read_file(path, &d, &len) < 0) return NULL;
	int w, h, n;
	unsigned char *rgba = stbi_load_from_memory(d, len, &w, &h, &n, 4);
	free(d);
	if (!rgba) return NULL;
	Img *im = img_from_rgba(rgba, w, h);
	stbi_image_free(rgba);
	return im;
}

static Img *img_new(int w, int h)
{
	Img *im = malloc(sizeof(Img));
	if (!im) return NULL;
	im->px = malloc((size_t)w * h * 4);
	if (!im->px) { free(im); return NULL; }
	im->w = w; im->h = h; im->avg = 0; im->opaque = 1;
	return im;
}

static Img *img_dup(const Img *s)
{
	Img *d = s ? img_new(s->w, s->h) : NULL;
	if (d) { memcpy(d->px, s->px, (size_t)s->w * s->h * 4); d->avg = s->avg; }
	return d;
}

/* ---- art cache: <cover dir>/<ID>.vdc next to the PNG ----
   Holds the finished pixels (tile, hero cover, small blurred background) so
   VitaDeck can show every cover right at startup – also after returning from
   a game – without decoding a single PNG. Rebuilt when the PNG's size changes. */
#define VDC_MAGIC 0x31434456u   /* "VDC1" */
#define VDC_TILE  (TILE * TILE_H)
#define VDC_HERO  (HBOX_W * HBOX_H)
#define VDC_SMALL (BGS_W * BGS_H)
#define VDC_LEN   (8 + 4 * (VDC_TILE + VDC_HERO + VDC_SMALL))

static void vdc_path(char *out, int n, const char *id) { snprintf(out, n, "%s/%s.vdc", plat_cover_dir(), id); }

static int cache_load(const char *id, int png_size, Img **box, Img **boxh, Img **small)
{
	char p[160];
	vdc_path(p, sizeof p, id);
	unsigned char *d; int len;
	if (plat_read_file(p, &d, &len) < 0) return 0;
	uint32_t hdr[2];
	int ok = len == VDC_LEN;
	if (ok) { memcpy(hdr, d, 8); ok = hdr[0] == VDC_MAGIC && (int)hdr[1] == png_size; }
	Img *b = ok ? img_new(TILE, TILE_H) : NULL, *h = ok ? img_new(HBOX_W, HBOX_H) : NULL, *s = ok ? img_new(BGS_W, BGS_H) : NULL;
	if (!b || !h || !s) { img_free(b); img_free(h); img_free(s); free(d); return 0; }
	const unsigned char *q = d + 8;
	memcpy(b->px, q, 4 * VDC_TILE); q += 4 * VDC_TILE;
	memcpy(h->px, q, 4 * VDC_HERO); q += 4 * VDC_HERO;
	memcpy(s->px, q, 4 * VDC_SMALL);
	free(d);
	*box = b; *boxh = h; *small = s;
	return 1;
}

static void cache_save(const char *id, int png_size, const Img *box, const Img *boxh, const Img *small)
{
	if (!box || !boxh || !small || box->w * box->h != VDC_TILE || boxh->w * boxh->h != VDC_HERO ||
	    small->w * small->h != VDC_SMALL) return;
	unsigned char *d = malloc(VDC_LEN);
	if (!d) return;
	uint32_t hdr[2] = { VDC_MAGIC, (uint32_t)png_size };
	memcpy(d, hdr, 8);
	unsigned char *q = d + 8;
	memcpy(q, box->px, 4 * VDC_TILE); q += 4 * VDC_TILE;
	memcpy(q, boxh->px, 4 * VDC_HERO); q += 4 * VDC_HERO;
	memcpy(q, small->px, 4 * VDC_SMALL);
	char p[160];
	vdc_path(p, sizeof p, id);
	plat_write_file(p, d, VDC_LEN);
	free(d);
}

static void bg_work(void *arg)
{
	(void)arg;
	if (bgjob.in_small) {
		/* tile + hero cover are already there (from the cache): only the big background */
		bgjob.bg = bg_from_small(bgjob.in_small);
		img_free(bgjob.in_small);
		bgjob.in_small = NULL;
	} else {
		char path[160];
		snprintf(path, sizeof path, "%s/%s.png", plat_cover_dir(), bgjob.id);
		int size = plat_file_size(path);
		Img *small = NULL;
		if (size > 0 && cache_load(bgjob.id, size, &bgjob.box, &bgjob.boxh, &small)) {
			/* cached */
		} else if (size > 0) {
			Img *cover = decode_file(path);
			if (cover) {
				bgjob.box = scale_fill(cover, TILE, TILE_H);   /* PSP/PS1 covers have other aspects: crop, don't stretch */
				bgjob.boxh = scale_fill(cover, HBOX_W, HBOX_H);
				to_opaque(bgjob.box, rgb(C_CARD));
				to_opaque(bgjob.boxh, rgb(C_CARD));
				small = make_blur_small(cover);
				img_free(cover);
				cache_save(bgjob.id, size, bgjob.box, bgjob.boxh, small);
			}
		}
		if (!small) {
			Img *ic = decode_file(bgjob.icon);   /* no box art: background from the icon */
			if (ic) { small = make_blur_small(ic); img_free(ic); }
		}
		bgjob.bg = bg_from_small(small);
		bgjob.bgs = small;
	}
	__sync_synchronize();
	bgjob.state = 2;
}

static void app_free_art(App *a)
{
	img_free(a->bg); img_free(a->box); img_free(a->boxh); img_free(a->bgs);
	a->bg = a->box = a->boxh = a->bgs = NULL;
}

/* only kicks in with more than BG_KEEP apps: drop the big background of the
   least recently shown app (tile, hero cover and small blur stay – cheap) */
static void bg_trim(const App *keep)
{
	while (bg_loaded > BG_KEEP) {
		App *old = NULL;
		for (int i = 0; i < napps; i++)
			if (apps[i].bg && &apps[i] != keep && (!old || apps[i].bg_tick < old->bg_tick)) old = &apps[i];
		if (!old) break;
		img_free(old->bg); old->bg = NULL; old->bg_state = 0;   /* rebuilt from bgs when needed */
		bg_loaded--;
	}
}

static void bg_start(App *a)
{
	snprintf(bgjob.id, sizeof bgjob.id, "%s", a->id);
	snprintf(bgjob.icon, sizeof bgjob.icon, "%s", a->icon_path);
	bgjob.bg = bgjob.box = bgjob.boxh = bgjob.bgs = NULL;
	/* a private copy: the app's own image may be freed (app list reload) while the job runs */
	bgjob.in_small = a->bgs ? img_dup(a->bgs) : NULL;
	bgjob.state = 1;
	if (plat_job(bg_work, NULL) < 0) { img_free(bgjob.in_small); bgjob.in_small = NULL; bgjob.state = 0; }
}

/* startup (during the boot splash, nothing else touches the card yet): take
   every cached cover straight from its .vdc so the first frame already has them */
static int art_boot_load(void)
{
	int n = 0;
	for (int i = 0; i < napps; i++) {
		App *a = &apps[i];
		if (a->box) continue;
		char path[160];
		snprintf(path, sizeof path, "%s/%s.png", plat_cover_dir(), a->id);
		int size = plat_file_size(path);
		if (size > 0 && cache_load(a->id, size, &a->box, &a->boxh, &a->bgs)) n++;
	}
	return n;
}

static void pump_bg(uint64_t now)
{
	if (bgjob.state == 2) {
		__sync_synchronize();
		App *a = find_app(bgjob.id);
		if (a && a->bg_state != 1 && (bgjob.bg || bgjob.box)) {
			if (bgjob.box) { img_free(a->box); a->box = bgjob.box; bgjob.box = NULL; }
			if (bgjob.boxh) { img_free(a->boxh); a->boxh = bgjob.boxh; bgjob.boxh = NULL; }
			if (bgjob.bgs) { img_free(a->bgs); a->bgs = bgjob.bgs; bgjob.bgs = NULL; }
			img_free(a->bg); a->bg = bgjob.bg; bgjob.bg = NULL;
			a->bg_state = 1; a->bg_tick = ++bg_clock;
			if (a->box) bg_ok++; else bg_none++;
			bg_loaded++;
			bg_trim(a);
		} else if (a && a->bg_state != 1) { a->bg_state = 2; bg_bad++; }
		img_free(bgjob.bg); img_free(bgjob.box); img_free(bgjob.boxh); img_free(bgjob.bgs);
		bgjob.bg = bgjob.box = bgjob.boxh = bgjob.bgs = NULL;
		bgjob.state = 0;
		S.dirty = 1;
	}
	if (bgjob.state != 0 || !napps) return;

	/* 1. the app in the hero, once the focus rests for a moment */
	if (S.view == V_HOME && nrows) {
		int ai = home_app();
		if (ai >= 0 && apps[ai].bg_state == 0) {
			if (ai != bg_focus) { bg_focus = ai; bg_since = now; return; }
			if (now - bg_since < 180) return;   /* keep the worker free for it */
			if (icon_cursor < napps) return;    /* icon reads on the UI thread first */
			bg_start(&apps[ai]);
			return;
		}
	}
	/* 2. otherwise preload the rest one by one, as long as they all fit – but only
	   once the icons are in and the user has not touched anything for a moment,
	   so the reads never compete with what the UI is doing */
	if (bg_loaded >= BG_KEEP) return;
	if (icon_cursor < napps || now - last_input < 600) return;
	for (int n = 0; n < napps; n++) {
		int i = (bg_cursor + n) % napps;
		if (apps[i].bg_state == 0 && !apps[i].hidden) {
			bg_cursor = (i + 1) % napps;
			bg_start(&apps[i]);
			return;
		}
	}
}

static void pump_icons(void)
{
	int budget = 2;
	int f = S.view == V_LIB ? lib_app() : home_app();
	if (f >= 0 && apps[f].ic_state == 0) { load_icon(&apps[f]); budget--; S.dirty = 1; }
	while (budget > 0 && icon_cursor < napps) {
		App *a = &apps[icon_cursor++];
		if (a->ic_state == 0) { load_icon(a); budget--; S.dirty = 1; }
	}
}

/* ---------- toast ---------- */
static void toast(const char *msg)
{
	snprintf(S.toast, sizeof(S.toast), "%s", msg);
	S.toast_until = plat_ms() + 2200;
	S.dirty = 1;
}

/* ---------- hit testing (touch) ---------- */
enum { H_HTILE = 1, H_HERO, H_TAB, H_LTILE, H_MENU, H_QA, H_CTX, H_BACKDROP, H_BTN, H_SET, H_KEY, H_COLL };
typedef struct { int x, y, w, h, act, a, b; } Hit;
static Hit hits[160];
static int nhits;
static void hit(int x, int y, int w, int h, int act, int a, int b)
{
	if (nhits < 160) hits[nhits++] = (Hit){ x, y, w, h, act, a, b };
}

/* ---------- drawing helpers ---------- */
static const u32 PAL[8][3] = {
	{ 0x2a4a6b, 0x3c6894, 0x16283a }, { 0x5a3b2a, 0x8a5a3a, 0x2c1c13 }, { 0x2e4a3a, 0x4f7a5c, 0x16251c },
	{ 0x48325c, 0x6e4f8c, 0x22162e }, { 0x5c2f3a, 0x8f4a5a, 0x2a141a }, { 0x2f4f55, 0x4f8088, 0x132326 },
	{ 0x5a5230, 0x8a7d45, 0x29251a }, { 0x3a3f4a, 0x5b6373, 0x1b1e24 } };

static unsigned hash_s(const char *s) { unsigned h = 7; while (*s) h = h * 31 + (unsigned char)*s++; return h; }

static u32 app_tint(const App *a)
{
	if (a->ic) return a->ic->avg;
	return rgb(PAL[hash_s(a->id) % 8][0]);
}

static void draw_cover(const App *a, int x, int y, int s, int r)
{
	unsigned h = hash_s(a->id);
	const u32 *p = PAL[h % 8];
	gfx_rrect(x, y, s, s, r, rgb(p[0]), 255);
	gfx_push_clip(x, y, s, s);
	gfx_circle(x + s * (0.2f + (h % 60) / 100.f), y + s * (0.25f + ((h >> 6) % 30) / 100.f), s * 0.33f, rgb(p[1]), 255);
	gfx_tri(x - 4, y + s * 0.72f, x + s + 4, y + s * 0.5f, x + s + 4, y + s + 4, rgb(p[2]), 255);
	gfx_tri(x - 4, y + s * 0.72f, x + s + 4, y + s + 4, x - 4, y + s + 4, rgb(p[2]), 255);
	gfx_pop_clip();
	char ini[3] = { 0 };
	int k = 0;
	for (const char *t = a->title; *t && k < 2; t++)
		if (isalnum((unsigned char)*t) && (t == a->title || t[-1] == ' ')) ini[k++] = (char)toupper((unsigned char)*t);
	if (!k) { ini[0] = a->title[0]; }
	int fs = s * 30 / 100;
	int tw = text_w(FONT_BOLD, fs, ini);
	text(FONT_BOLD, fs, x + (s - tw) / 2, y + (s - text_lineh(FONT_BOLD, fs)) / 2, ini, rgb(C_TXT), 230);
}

static void star(float cx, float cy, float r, u32 c)
{
	float px[10], py[10];
	for (int i = 0; i < 10; i++) {
		float ang = -1.5707963f + i * 0.6283185f;
		float rr = (i & 1) ? r * 0.45f : r;
		px[i] = cx + cosf(ang) * rr; py[i] = cy + sinf(ang) * rr;
	}
	for (int i = 0; i < 10; i++) {
		int j = (i + 1) % 10;
		gfx_tri(cx, cy, px[i], py[i], px[j], py[j], c, 255);
	}
}

/* render the placeholder cover once into an image (square; corners are rounded by gfx_img) */
static Img *make_cover(const App *a, int s)
{
	Img *im = malloc(sizeof(Img));
	if (!im) return NULL;
	im->w = im->h = s;
	im->px = malloc((size_t)s * s * 4);
	if (!im->px) { free(im); return NULL; }
	im->avg = rgb(PAL[hash_s(a->id) % 8][0]);
	im->opaque = 1;   /* blend() always writes alpha 255 */
	gfx_begin_target(im->px, s, s);
	draw_cover(a, 0, 0, s, 0);
	gfx_end_target();
	return im;
}

static Img *cover_img(App *a, int s)
{
	Img **slot = s == TILE ? &a->cvs : s == 128 ? &a->cv : NULL;
	if (!slot) return NULL;
	if (!*slot) *slot = make_cover(a, s);
	return *slot;
}

static void draw_icon_or_cover(App *a, int x, int y, int s, int r)
{
	const Img *im = a->ic_state == 1 ? (s == TILE ? a->ics : (s == 128 ? a->ic : NULL)) : NULL;
	if (im) {
		gfx_img(im, x, y, r, 255);   /* already composited onto C_CARD */
	} else if ((im = cover_img(a, s))) {
		gfx_img(im, x, y, r, 255);
	} else draw_cover(a, x, y, s, r);
}

static void draw_tile(int ai, int x, int y, int focused)
{
	App *a = &apps[ai];
	/* portrait tile: box art if there is one, else the square icon centred on a card */
	if (focused) gfx_rrect(x - 4, y - 4, TILE + 8, TILE_H + 8, 14, rgb(C_ACC), 255);
	if (a->box) gfx_img(a->box, x, y, 10, 255);
	else {
		gfx_rrect(x, y, TILE, TILE_H, 10, rgb(C_CARD), 255);
		draw_icon_or_cover(a, x, y + (TILE_H - TILE) / 2, TILE, 10);
	}
	if (a->fav) {
		gfx_circle(x + TILE - 13, y + 13, 10, rgb(C_BG), 200);
		star(x + TILE - 13, y + 13.5f, 7, rgb(C_STAR));
	}
	text_fit(focused ? FONT_BOLD : FONT_REG, FS(12), x, y + TILE_H + 8, TILE, a->title, rgb(focused ? C_TXT : C_TXT2), 255);
}

/* controller glyphs */
enum { G_X, G_O, G_TRI, G_SQ, G_L, G_R, G_START, G_SELECT, G_LR };
static const char *GLBL[] = { "", "", "", "", "L", "R", "START", "SELECT", "←→" };

static int glyph(int x, int y, int g, u32 bg, u32 fg)
{
	if (g <= G_SQ) {
		float cx = x + 10, cy = y + 10;
		gfx_circle(cx, cy, 10, bg, 255);
		if (g == G_X) { gfx_line(cx - 4.5f, cy - 4.5f, cx + 4.5f, cy + 4.5f, 2, fg, 255); gfx_line(cx + 4.5f, cy - 4.5f, cx - 4.5f, cy + 4.5f, 2, fg, 255); }
		else if (g == G_O) gfx_ring(cx, cy, 5, 2, fg, 255);
		else if (g == G_TRI) { gfx_line(cx, cy - 5.5f, cx + 5.5f, cy + 4, 2, fg, 255); gfx_line(cx + 5.5f, cy + 4, cx - 5.5f, cy + 4, 2, fg, 255); gfx_line(cx - 5.5f, cy + 4, cx, cy - 5.5f, 2, fg, 255); }
		else { gfx_line(cx - 5, cy - 5, cx + 5, cy - 5, 2, fg, 255); gfx_line(cx + 5, cy - 5, cx + 5, cy + 5, 2, fg, 255); gfx_line(cx + 5, cy + 5, cx - 5, cy + 5, 2, fg, 255); gfx_line(cx - 5, cy + 5, cx - 5, cy - 5, 2, fg, 255); }
		return 20;
	}
	int tw = text_w(FONT_BOLD, FS(10), GLBL[g]);
	int w = tw + 12 < 22 ? 22 : tw + 12;
	gfx_rrect(x, y, w, 20, 6, bg, 255);
	text(FONT_BOLD, FS(10), x + (w - tw) / 2, y + 4, GLBL[g], fg, 255);
	return w;
}

static int glyph_w(int g) { return g <= G_SQ ? 20 : (text_w(FONT_BOLD, FS(10), GLBL[g]) + 12 < 22 ? 22 : text_w(FONT_BOLD, FS(10), GLBL[g]) + 12); }

static const unsigned GBTN[] = { B_X, B_O, B_TRI, B_SQ, B_L, B_R, B_START, B_SELECT, 0 };

typedef struct { int g; const char *t; } Hint;

static void footer(const char *info, const Hint *h, int n)
{
	gfx_rect(0, SCR_H - 36, SCR_W, 36, rgb(C_HEAD), 255);
	gfx_rect(0, SCR_H - 36, SCR_W, 1, rgb(C_LINE), 255);
	int total = 0;
	for (int i = 0; i < n; i++) total += glyph_w(h[i].g) + 6 + text_w(FONT_REG, FS(12), h[i].t) + (i ? 18 : 0);
	int x = SCR_W - 20 - total, y = SCR_H - 28;
	if (info) text_fit(FONT_REG, FS(12), 20, y + 3, x - 40, info, rgb(C_MUTE), 255);
	for (int i = 0; i < n; i++) {
		if (i) x += 18;
		int x0 = x;
		x += glyph(x, y, h[i].g, rgb(C_TXT), rgb(C_BG)) + 6;
		x += text(FONT_REG, FS(12), x, y + 3, h[i].t, rgb(C_TXT2), 255);
		hit(x0 - 6, SCR_H - 36, x - x0 + 12, 36, H_BTN, GBTN[h[i].g], 0);
	}
}

static void battery_icon(int x, int y, int pct, int chg)
{
	u32 c = rgb(C_TXT2);
	gfx_line(x + 1, y + 1, x + 21, y + 1, 1.5f, c, 255);
	gfx_line(x + 21, y + 1, x + 21, y + 11, 1.5f, c, 255);
	gfx_line(x + 21, y + 11, x + 1, y + 11, 1.5f, c, 255);
	gfx_line(x + 1, y + 11, x + 1, y + 1, 1.5f, c, 255);
	gfx_rect(x + 23, y + 4, 2, 5, c, 255);
	int fw = pct * 16 / 100;
	if (fw < 1) fw = 1;
	gfx_rect(x + 3, y + 3, fw, 7, rgb(pct <= 15 ? 0xff7a6b : C_GREEN), 255);
	if (chg) {
		gfx_tri(x + 12, y - 1, x + 7, y + 7, x + 12, y + 6, rgb(C_TXT), 255);
		gfx_tri(x + 10, y + 13, x + 15, y + 5, x + 10, y + 6, rgb(C_TXT), 255);
	}
}

/* Wi-Fi: polled every 2 s in ui_frame (an IPC call, not every frame) */
static int wifi_level = -1;   /* -1 = not connected, else 1..3 bars */
static int wifi_bars(int pct) { return pct < 0 ? -1 : pct >= 67 ? 3 : pct >= 34 ? 2 : 1; }

static void arc(float cx, float cy, float r, float th, u32 c)
{
	const float a0 = -2.36f, a1 = -0.785f;   /* -135° .. -45°: an upward fan */
	float px = cx + cosf(a0) * r, py = cy + sinf(a0) * r;
	for (int i = 1; i <= 8; i++) {
		float a = a0 + (a1 - a0) * i / 8, x = cx + cosf(a) * r, y = cy + sinf(a) * r;
		gfx_line(px, py, x, y, th, c, 255);
		px = x; py = y;
	}
}

static void wifi_icon(int x, int y, int bars)   /* 20x15 box, x/y = top left */
{
	float cx = x + 10, cy = y + 14;
	u32 on = rgb(C_TXT2), off = rgb(C_TRACK);
	gfx_circle(cx, cy - 1, 1.8f, on, 255);
	arc(cx, cy, 5.5f, 2.0f, bars >= 1 ? on : off);
	arc(cx, cy, 9.5f, 2.0f, bars >= 2 ? on : off);
	arc(cx, cy, 13.5f, 2.0f, bars >= 3 ? on : off);
}

static void header(const char *title)
{
	gfx_rect(0, 0, SCR_W, 40, rgb(C_HEAD), 255);
	gfx_rect(0, 39, SCR_W, 1, rgb(C_LINE), 255);
	text(FONT_BOLD, FS(15), 20, 11, title, rgb(C_TXT), 255);
	Clock c; plat_clock(&c);
	char buf[32];
	snprintf(buf, sizeof buf, "%02d:%02d", c.hour, c.minute);
	int x = SCR_W - 20 - text_w(FONT_BOLD, FS(14), buf);
	text(FONT_BOLD, FS(14), x, 12, buf, rgb(C_TXT), 255);
	int chg = 0, pct = plat_battery(&chg);
	if (pct >= 0) {
		snprintf(buf, sizeof buf, "%d %%", pct);
		x -= 18 + text_w(FONT_REG, FS(13), buf);
		text(FONT_REG, FS(13), x, 13, buf, rgb(C_TXT2), 255);
		battery_icon(x - 32, 14, pct, chg);
		x -= 32;
	}
	if (wifi_level > 0) wifi_icon(x - 34, 11, wifi_level);   /* hidden without a connection, like the system bar */
}

/* simple stroke icons, 18px box */
enum { I_HOME, I_LIB, I_RELOAD, I_MOON, I_POWER, I_EXIT, I_GEAR };
static void icon(int x, int y, int k, u32 c)
{
	float t = 2;
	switch (k) {
	case I_HOME:
		gfx_line(x + 2, y + 8, x + 9, y + 2, t, c, 255); gfx_line(x + 9, y + 2, x + 16, y + 8, t, c, 255);
		gfx_line(x + 4, y + 7, x + 4, y + 16, t, c, 255); gfx_line(x + 14, y + 7, x + 14, y + 16, t, c, 255);
		gfx_line(x + 4, y + 16, x + 14, y + 16, t, c, 255);
		break;
	case I_LIB:
		gfx_line(x + 3, y + 2, x + 3, y + 16, t, c, 255); gfx_line(x + 8, y + 2, x + 8, y + 16, t, c, 255);
		gfx_line(x + 12, y + 3, x + 16, y + 15, t, c, 255); gfx_line(x + 1, y + 16, x + 17, y + 16, t, c, 255);
		break;
	case I_RELOAD:
		gfx_ring(x + 9, y + 9, 6.5f, t, c, 255);
		gfx_rect(x + 9, y + 1, 8, 8, rgb(C_PANEL), 255);
		gfx_tri(x + 9, y, x + 9, y + 7, x + 14, y + 3.5f, c, 255);
		break;
	case I_MOON:
		gfx_circle(x + 9, y + 9, 7.5f, c, 255);
		gfx_circle(x + 13, y + 6, 6.5f, rgb(C_PANEL), 255);
		break;
	case I_POWER:
		gfx_ring(x + 9, y + 10, 6.5f, t, c, 255);
		gfx_rect(x + 6, y + 1, 6, 7, rgb(C_PANEL), 255);
		gfx_line(x + 9, y + 1, x + 9, y + 9, t, c, 255);
		break;
	case I_EXIT:
		gfx_line(x + 8, y + 2, x + 2, y + 2, t, c, 255); gfx_line(x + 2, y + 2, x + 2, y + 16, t, c, 255);
		gfx_line(x + 2, y + 16, x + 8, y + 16, t, c, 255); gfx_line(x + 7, y + 9, x + 17, y + 9, t, c, 255);
		gfx_line(x + 13, y + 5, x + 17, y + 9, t, c, 255); gfx_line(x + 13, y + 13, x + 17, y + 9, t, c, 255);
		break;
	case I_GEAR:   /* ring with 8 teeth */
		for (int k = 0; k < 8; k++) {
			float a = k * 0.7853982f, ca = cosf(a), sa = sinf(a);
			gfx_line(x + 9 + ca * 5.5f, y + 9 + sa * 5.5f, x + 9 + ca * 8.2f, y + 9 + sa * 8.2f, 2.6f, c, 255);
		}
		gfx_ring(x + 9, y + 9, 5, t, c, 255);
		break;
	}
}

/* ---------- logo (concept 3E: focus grid with start tile) ---------- */
#define L(v) ((v) * k)
static void draw_logo(float x, float y, float size, u32 bg)
{
	float k = size / 120.f;
	int r10 = (int)(L(10) + 0.5f);
	gfx_rrect((int)(x + L(8)), (int)(y + L(8)), (int)L(46), (int)L(46), r10, rgb(C_TRACK), 255);
	/* focus ring = 3E's <rect x=59 y=3 w=58 h=58 rx=15 stroke-width=3>: outer accent, inner background */
	gfx_rrect((int)(x + L(57.5f)), (int)(y + L(1.5f)), (int)(L(61) + 0.5f), (int)(L(61) + 0.5f), (int)(L(16.5f) + 0.5f), rgb(C_ACC), 255);
	gfx_rrect((int)(x + L(60.5f)), (int)(y + L(4.5f)), (int)(L(55) + 0.5f), (int)(L(55) + 0.5f), (int)(L(13.5f) + 0.5f), bg, 255);
	gfx_rrect((int)(x + L(65)), (int)(y + L(9)), (int)L(46), (int)L(46), r10, rgb(C_ACC), 255);
	/* start arrow with rounded tips */
	float ax = x + L(83.5f), ay = y + L(24.5f), bx = x + L(83.5f), by = y + L(39.5f), cx = x + L(96.5f), cy = y + L(32);
	u32 ink = rgb(C_INK);
	gfx_tri(ax, ay, bx, by, cx, cy, ink, 255);
	float th = L(5);
	gfx_line(ax, ay, bx, by, th, ink, 255);
	gfx_line(bx, by, cx, cy, th, ink, 255);
	gfx_line(cx, cy, ax, ay, th, ink, 255);
	gfx_rrect((int)(x + L(8)), (int)(y + L(66)), (int)L(46), (int)L(46), r10, rgb(C_TILE2), 255);
	gfx_rrect((int)(x + L(65)), (int)(y + L(66)), (int)L(46), (int)L(46), r10, rgb(C_TRACK), 255);
}
#undef L

static int wordmark_w(int size)
{
	return text_w(FONT_WM, size, "Vita") + text_w(FONT_WM_BOLD, size, "Deck");
}

static int wordmark(int x, int y, int size, u32 c)
{
	int w = text(FONT_WM, size, x, y, "Vita", c, 255);
	return w + text(FONT_WM_BOLD, size, x + w, y, "Deck", c, 255);
}

/* ---------- screens ---------- */
static void draw_boot(void)
{
	gfx_clear(rgb(C_BG));
	int ls = 96, ww = wordmark_w(44);
	int total = ls + 22 + ww;
	int x = (SCR_W - total) / 2, y = SCR_H / 2 - ls / 2 - 24;
	draw_logo(x, y, ls, rgb(C_BG));
	wordmark(x + ls + 22, y + (ls - text_lineh(FONT_WM, 44)) / 2, 44, rgb(C_TXT));
	const char *s = "Apps werden geladen …";
	text(FONT_REG, FS(13), (SCR_W - text_w(FONT_REG, FS(13), s)) / 2, y + ls + 40, s, rgb(C_MUTE), 255);
}

/* home: header (0..40) + hero (52..52+HERO_H) end above the rows at HOME_TOP.
   The hero content (laid out for 156 px) is centred vertically via HERO_DY. */
#define HERO_Y     52
#define HERO_DY    ((HERO_H - 156) / 2)
#define HOME_TOP   (HERO_Y + HERO_H + 8)
#define HERO_TX    (14 + HBOX_W + 20)   /* text column right of the box art */
#define HERO_BTN_X (24 + HERO_TX)
#define HERO_BTN_Y (HERO_Y + HERO_DY + 106)
#define TOP_KEYN   13
static int homebg_gen;   /* home background generation (defined with the home background below) */
static u32 *topbuf;
static int top_ok, top_keyv[TOP_KEYN];

static void top_key(int *k)
{
	int ai = home_app(), chg = 0;
	const App *a = &apps[ai];
	Clock c; plat_clock(&c);
	k[0] = ai; k[1] = a->fav; k[2] = a->last; k[3] = a->ic_state;
	k[4] = S.hrow; k[5] = nrows; k[6] = (int)hash_s(nrows ? rows[S.hrow].title : "");
	k[7] = c.hour * 60 + c.minute;
	k[8] = plat_battery(&chg); k[9] = chg;
	k[10] = (int)hash_s(a->id);
	k[11] = (a->bg != NULL) | (a->boxh != NULL) << 1 | (wifi_level + 1) << 2;
	k[12] = homebg_gen;   /* the full-screen background shows around the hero too */
}

static void draw_hero(int ai)
{
	App *a = &apps[ai];
	int x = 24, y = HERO_Y, w = HERO_W, h = HERO_H;
	u32 tint = app_tint(a);
	u32 dark = rgb(C_PANEL);
	/* mix tint 45% into panel */
	{
		u32 t = tint, p = dark;
		u32 r = ((t & 0xFF) * 45 + (p & 0xFF) * 55) / 100;
		u32 g = (((t >> 8) & 0xFF) * 45 + ((p >> 8) & 0xFF) * 55) / 100;
		u32 b = (((t >> 16) & 0xFF) * 45 + ((p >> 16) & 0xFF) * 55) / 100;
		tint = 0xFF000000u | (b << 16) | (g << 8) | r;
	}
	if (a->bg) {
		a->bg_tick = ++bg_clock;
		gfx_img(a->bg, x, y, 12, 255);   /* already cropped + darkened */
	} else {
		gfx_rrect(x, y, w, h, 12, dark, 255);
		gfx_rrect(x, y, 30, h, 12, tint, 255);
		gfx_hgrad(x + 14, y, 520, h, tint, dark);
	}
	if (a->boxh) gfx_img(a->boxh, x + 14, y + (h - HBOX_H) / 2, 10, 255);
	else draw_icon_or_cover(a, x + 14 + (HBOX_W - 128) / 2, y + (h - 128) / 2, 128, 12);
	y += HERO_DY;   /* text block below is laid out for the original 156 px */
	int tx = x + HERO_TX;
	const char *eb = S.hrow < nrows ? rows[S.hrow].title : "";
	if (S.hrow == 0 && a->last && nrows && rows[0].title[0] == 'Z') eb = "Weiterspielen";
	text(FONT_BOLD, FS(12), tx, y + 18, eb, rgb(C_MUTE), 255);
	text_fit(FONT_BOLD, FS(26), tx, y + 36, w - HERO_TX - 18, a->title, rgb(C_TXT), 255);
	/* "Zuletzt gestartet" only knows launches from VitaDeck; the playtime also
	   covers LiveArea starts, so with playtime "Noch nie gestartet" is left out */
	char when[72], play[48] = "", meta[200];
	if (a->last) {
		char last[48];
		fmt_last(a->last, last, sizeof last);
		snprintf(when, sizeof when, "Zuletzt gestartet: %s", last);
	} else snprintf(when, sizeof when, "%s", a->play ? "" : "Noch nie gestartet");
	if (a->play > 0) {
		char p[24];
		fmt_play(a->play, p, sizeof p);
		snprintf(play, sizeof play, "%sGespielt: %s", when[0] ? "   ·   " : "", p);
	}
	snprintf(meta, sizeof meta, "%s%s   ·   %s   ·   %s", when, play, cat_name(a->cat), a->id);
	text_fit(FONT_REG, FS(13), tx, y + 76, w - HERO_TX - 18, meta, rgb(C_TXT2), 255);
	int by = y + 106;
	gfx_rrect(tx, by, 136, 36, 8, rgb(C_ACC), 255);
	glyph(tx + 12, by + 8, G_X, rgb(C_INK), rgb(C_ACC));
	text(FONT_BOLD, FS(15), tx + 40, by + 9, "Starten", rgb(C_INK), 255);
	hit(HERO_BTN_X, HERO_BTN_Y, 136, 36, H_HERO, 0, 0);
	int hx = tx + 156;
	hx += glyph(hx, by + 8, G_TRI, rgb(C_TXT), rgb(C_BG)) + 6;
	hx += text(FONT_REG, FS(12), hx, by + 11, "Optionen", rgb(C_TXT2), 255) + 18;
	hx += glyph(hx, by + 8, G_SQ, rgb(C_TXT), rgb(C_BG)) + 6;
	text(FONT_REG, FS(12), hx, by + 11, a->fav ? "Favorit entfernen" : "Favorit", rgb(C_TXT2), 255);
}

/* ---------- home background (ROADMAP 8) ----------
   The hero app's small blurred background (bgs), cropped to the screen's aspect,
   scaled to full screen and darkened towards C_BG. Built on the UI thread (a few ms,
   pure pixel work) once the focus has rested for a moment; drawn with row copies. */
static u32 *homebg;
static char homebg_id[16];
static int homebg_gen;          /* bumped on every rebuild – part of the hero cache key */
static uint64_t homebg_since;
static int homebg_want = -1;

static void home_bg_build(const App *a)
{
	const Img *s = a ? a->bgs : NULL;
	if (!s) { if (homebg_id[0]) { homebg_id[0] = 0; homebg_gen++; } return; }
	if (!homebg) homebg = malloc(SCR_W * SCR_H * 4);
	if (!homebg) return;
	/* crop the centre of the wide hero strip to 960:544 */
	float cw = s->h * (float)SCR_W / SCR_H, cx = (s->w - cw) * 0.5f;
	if (cw > s->w) { cw = (float)s->w; cx = 0; }
	u32 bg = rgb(C_BG);
	int br = bg & 0xFF, bgc = (bg >> 8) & 0xFF, bb = (bg >> 16) & 0xFF;
	for (int y = 0; y < SCR_H; y++) {
		float fy = (y + 0.5f) * s->h / SCR_H - 0.5f;
		int y0 = (int)fy; float ty = fy - y0;
		if (y0 < 0) { y0 = 0; ty = 0; }
		int y1 = y0 + 1 < s->h ? y0 + 1 : y0;
		/* fade to the plain background towards the bottom, where the rows are */
		int keep = 70 - 40 * y / SCR_H;   /* % of the picture: 70 at the top, 30 at the bottom */
		for (int x = 0; x < SCR_W; x++) {
			float fx = cx + (x + 0.5f) * cw / SCR_W - 0.5f;
			int x0 = (int)fx; float tx = fx - x0;
			if (x0 < 0) { x0 = 0; tx = 0; }
			int x1 = x0 + 1 < s->w ? x0 + 1 : x0;
			u32 p00 = s->px[y0 * s->w + x0], p01 = s->px[y0 * s->w + x1], p10 = s->px[y1 * s->w + x0], p11 = s->px[y1 * s->w + x1];
			u32 o = 0xFF000000u;
			for (int k = 0; k < 3; k++) {
				int sh = 8 * k;
				float c = ((p00 >> sh) & 0xFF) * (1 - tx) * (1 - ty) + ((p01 >> sh) & 0xFF) * tx * (1 - ty) +
				          ((p10 >> sh) & 0xFF) * (1 - tx) * ty + ((p11 >> sh) & 0xFF) * tx * ty;
				int base = k == 0 ? br : k == 1 ? bgc : bb;
				int v = (int)(c * 0.35f * keep / 100 + base * (100 - keep * 0.35f) / 100);   /* darkened */
				o |= (u32)(v > 255 ? 255 : v) << sh;
			}
			homebg[y * SCR_W + x] = o;
		}
	}
	snprintf(homebg_id, sizeof homebg_id, "%s", a->id);
	homebg_gen++;
}

/* called every frame on the home screen: rebuild once the hero app has been stable for 250 ms */
static void home_bg_pump(uint64_t now)
{
	if (S.view != V_HOME || !nrows) return;
	int ai = home_app();
	if (ai < 0) return;
	if (ai != homebg_want) { homebg_want = ai; homebg_since = now; return; }
	if (now - homebg_since < 250) return;
	const App *a = &apps[ai];
	if (a->bgs && strcmp(homebg_id, a->id)) { home_bg_build(a); S.dirty = 1; }
}

static void home_bg_rows(int y0, int y1)
{
	if (!homebg || !homebg_id[0]) { gfx_rect(0, y0, SCR_W, y1 - y0, rgb(C_BG), 255); return; }
	memcpy(gfx_buffer() + (size_t)y0 * SCR_W, homebg + (size_t)y0 * SCR_W, (size_t)(y1 - y0) * SCR_W * 4);
}

static void draw_home(void)
{
	uint64_t t = plat_us();
	if (nrows) {   /* napps > 0 but nrows == 0 when every app is hidden */
		/* header + hero only change with focus, favourites, clock or battery;
		   while the rows scroll they are restored from a cache */
		int key[TOP_KEYN];
		top_key(key);
		if (top_ok && !memcmp(key, top_keyv, sizeof key)) {
			memcpy(gfx_buffer(), topbuf, SCR_W * HOME_TOP * 4);
			home_bg_rows(HOME_TOP, SCR_H);
			hit(HERO_BTN_X, HERO_BTN_Y, 136, 36, H_HERO, 0, 0);
			perf_add(0, &t);
		} else {
			home_bg_rows(0, SCR_H);
			header("Startseite");
			perf_add(0, &t);
			draw_hero(home_app());
			perf_add(1, &t);
			if (!topbuf) topbuf = malloc(SCR_W * HOME_TOP * 4);
			if (topbuf) {
				memcpy(topbuf, gfx_buffer(), SCR_W * HOME_TOP * 4);
				memcpy(top_keyv, key, sizeof key);
				top_ok = 1;
			}
		}
		t = plat_us();
	} else {
		gfx_clear(rgb(C_BG));
		header("Startseite");
	}
	if (!nrows) {
		const char *m1 = napps ? "Alle Apps sind ausgeblendet" : "Keine Apps gefunden";
		const char *m2 = napps ? "Wieder einblenden: Bibliothek (R) → Tab „Ausgeblendet“ → △"
		                       : "In ux0:app wurden keine installierten Apps erkannt.";
		text(FONT_BOLD, FS(20), (SCR_W - text_w(FONT_BOLD, FS(20), m1)) / 2, 220, m1, rgb(C_TXT), 255);
		text(FONT_REG, FS(14), (SCR_W - text_w(FONT_REG, FS(14), m2)) / 2, 254, m2, rgb(C_MUTE), 255);
		Hint hs[] = { { G_R, "Bibliothek" }, { G_START, "Menü" }, { G_SELECT, "Schnellzugriff" } };
		if (napps) footer(NULL, hs, 3); else footer(NULL, hs + 1, 2);
		return;
	}
	int top = HOME_TOP;
	gfx_push_clip(0, top, SCR_W, SCR_H - 36 - top);
	for (int r = 0; r < nrows; r++) {
		int ry = top + 6 + r * ROW_H - (int)S.sy;
		if (ry > SCR_H || ry + ROW_H < top) continue;
		text(FONT_BOLD, FS(14), 24, ry, rows[r].title, rgb(r == S.hrow ? C_TXT : C_DIM), 255);
		for (int c = 0; c < rows[r].n; c++) {
			int tx = 28 + c * HOME_PX - (int)S.sx[r];
			if (tx > SCR_W || tx + TILE < 0) continue;
			int foc = r == S.hrow && c == S.hcol;
			draw_tile(rows[r].idx[c], tx, ry + 28, foc);
			hit(tx, ry + 28, TILE, TILE_H + 26, H_HTILE, r, c);
		}
	}
	gfx_pop_clip();
	perf_add(2, &t);
	Hint hs[] = { { G_X, "Starten" }, { G_TRI, "Optionen" }, { G_R, "Bibliothek" }, { G_START, "Menü" }, { G_SELECT, "Schnellzugriff" } };
	footer(NULL, hs, 5);
	perf_add(3, &t);
}

static void draw_lib(void)
{
	gfx_clear(rgb(C_BG));
	header("Bibliothek");
	gfx_rect(0, 87, SCR_W, 1, rgb(C_LINE), 255);
	int x = 20, y = 54;
	x += glyph(x, y, G_L, rgb(C_TXT), rgb(C_BG)) + 8;
	int tmp[MAXA];
	for (int t = 0, nt = ntabs(); t < nt; t++) {
		char lbl[48];
		snprintf(lbl, sizeof lbl, "%s · %d", TABS[tab_id(t)], lib_filter(tab_id(t), tmp));
		int on = t == S.tab;
		int tw = text_w(on ? FONT_BOLD : FONT_REG, FS(13), lbl);
		if (on) gfx_rrect(x, 46, tw + 26, 32, 6, rgb(C_ACC), 255);
		text(on ? FONT_BOLD : FONT_REG, FS(13), x + 13, 54, lbl, rgb(on ? C_INK : C_TXT2), 255);
		hit(x, 46, tw + 26, 32, H_TAB, t, 0);
		x += tw + 30;
	}
	glyph(x + 2, y, G_R, rgb(C_TXT), rgb(C_BG));
	char sl[64];
	if (search[0]) {   /* active search replaces the sort label (always shown) */
		snprintf(sl, sizeof sl, "Suche: „%.40s“", search);
		int sw = text_w(FONT_BOLD, FS(12), sl), sx = SCR_W - 20 - sw;
		if (sx < x + 30) sx = x + 30;
		gfx_rrect(sx - 10, 47, SCR_W - 20 - sx + 20, 30, 15, rgb(C_ACC), 255);
		text_fit(FONT_BOLD, FS(12), sx, 54, SCR_W - 20 - sx, sl, rgb(C_INK), 255);
	} else {
		snprintf(sl, sizeof sl, "Sortierung: %s", SORTS[S.sort]);
		int sw = text_w(FONT_REG, FS(12), sl);
		if (x + 30 + sw < SCR_W - 20)   /* with many tabs there is no room – sorting is in △ too */
			text(FONT_REG, FS(12), SCR_W - 20 - sw, 56, sl, rgb(C_MUTE), 255);
	}

	int top = 88;
	if (!nlib) {
		char nm[96];
		snprintf(nm, sizeof nm, "Keine Treffer für „%s“.", search);
		const char *m = search[0] ? nm
		              : tab_id(S.tab) == TAB_FAV ? "Noch keine Favoriten. Drück □ auf einer App, dann taucht sie hier auf."
		              : tab_id(S.tab) == TAB_HIDDEN ? "Keine ausgeblendeten Apps."
		                                    : "In dieser Kategorie ist nichts installiert.";
		text(FONT_REG, FS(15), (SCR_W - text_w(FONT_REG, FS(15), m)) / 2, 260, m, rgb(C_MUTE), 255);
	} else {
		gfx_push_clip(0, top, SCR_W, SCR_H - 36 - top);
		for (int i = 0; i < nlib; i++) {
			int r = i / LIB_COLS, c = i % LIB_COLS;
			int tx = 28 + c * LIB_PX, ty = top + 18 + r * LIB_PY - (int)S.ly;
			if (ty > SCR_H || ty + LIB_PY < top) continue;
			draw_tile(lib[i], tx, ty, i == S.lidx);
			hit(tx, ty, TILE, TILE_H + 26, H_LTILE, i, 0);
		}
		gfx_pop_clip();
	}
	char info[200] = { 0 };
	int ai = lib_app();
	if (ai >= 0) {
		const App *a = &apps[ai];
		char last[48] = "", play[40] = "";
		if (a->last || !a->play) fmt_last(a->last, last, sizeof last);   /* "noch nie gestartet" only without playtime */
		if (a->play) { char p[24]; fmt_play(a->play, p, sizeof p); snprintf(play, sizeof play, "%s%s gespielt", last[0] ? " · " : "", p); }
		snprintf(info, sizeof info, "%s · %s · %s%s%s", a->title, cat_name(a->cat), a->last ? "zuletzt " : "", last, play);
	}
	Hint hs[] = { { G_X, "Starten" }, { G_SQ, "Favorit" }, { G_TRI, "Optionen" }, { G_O, search[0] ? "Suche beenden" : "Zurück" } };
	footer(ai >= 0 ? info : NULL, hs, 4);
}

static const char *MENU[] = { "Startseite", "Bibliothek", "App-Liste neu laden", "Einstellungen", "Standby", "Neustart", "Launcher beenden" };
static const int MENU_IC[] = { I_HOME, I_LIB, I_RELOAD, I_GEAR, I_MOON, I_POWER, I_EXIT };
#define NMENU 7
#define MENU_SEP 4   /* divider before Standby */

/* darkens the base view; the result is cached in `under` so later frames
   of the overlay only need a memcpy instead of redrawing the whole screen */
static void dim(void)
{
	if (!under_ok) {
		gfx_rect(0, 0, SCR_W, SCR_H, rgb(0x000000), 170);
		if (!under) under = malloc(SCR_W * SCR_H * 4);
		if (under) { memcpy(under, gfx_buffer(), SCR_W * SCR_H * 4); under_ok = 1; }
	}
	hit(0, 0, SCR_W, SCR_H, H_BACKDROP, 0, 0);
}

static void draw_menu(void)
{
	dim();
	gfx_rect(0, 0, 280, SCR_H, rgb(C_PANEL), 255);
	gfx_rect(280, 0, 1, SCR_H, rgb(C_LINE2), 255);
	hit(0, 0, 281, SCR_H, 0, 0, 0);
	draw_logo(18, 16, 42, rgb(C_PANEL));
	wordmark(72, 17, 20, rgb(C_TXT));
	text(FONT_REG, FS(12), 72, 42, VD_VERSION, rgb(C_MUTE), 255);
	gfx_rect(20, 72, 240, 1, rgb(C_LINE2), 255);
	int y = 84;
	for (int i = 0; i < NMENU; i++) {
		if (i == MENU_SEP) { y += 10; gfx_rect(20, y - 6, 240, 1, rgb(C_LINE2), 255); }
		int on = i == S.mi;
		if (on) gfx_rrect(12, y, 256, 44, 8, rgb(C_ACC), 255);
		icon(26, y + 13, MENU_IC[i], rgb(on ? C_INK : C_TXT2));
		text(on ? FONT_BOLD : FONT_REG, FS(14), 58, y + 13, MENU[i], rgb(on ? C_INK : C_TXT), 255);
		hit(12, y, 256, 44, H_MENU, i, 0);
		y += 46;
	}
	Hint hs[] = { { G_X, "Auswählen" }, { G_O, "Schließen" } };
	footer(NULL, hs, 2);
}

/* ---------- settings (return/autostart plugin) ---------- */
enum { CF_INSTALL = 1, CF_UNINSTALL };
#define NSET 5
#define SET_W 440

/* ---------- box art download (HexFlow-Covers, CC BY-NC-SA 4.0) ----------
   Runs on the job thread: one HTTPS GET per installed app that has no box
   art yet; 404 = no cover in the database. Covers are only stored on the
   device – never bundled with VitaDeck. */
#define COVER_URL     "https://raw.githubusercontent.com/andiweli/hexflow-covers/main/Covers/PSVita/%s.png"
#define COVER_URL_PSP "https://raw.githubusercontent.com/andiweli/hexflow-covers/main/Covers/PSP/%s.png"

static struct {
	volatile int state;        /* 0 idle, 1 running, 2 finished (pending), 3 shown */
	volatile int done;
	int total, got, have, missing, failed;
	char ids[MAXA][16];
	unsigned char cat[MAXA];   /* app category: PSP covers live in another folder */
	char msg[128];
} cov;
static int cov_have_now;       /* covers on the card when the settings were opened */

/* create every directory along path ("ux0:data/VitaDeck/covers/box") */
static void mkdirs(const char *path)
{
	char p[160];
	snprintf(p, sizeof p, "%s", path);
	for (char *c = strchr(p, '/'); c; c = strchr(c + 1, '/')) { *c = 0; plat_mkdir(p); *c = '/'; }
	plat_mkdir(p);
}

static void cover_job(void *arg)
{
	(void)arg;
	char err[96];
	mkdirs(plat_cover_dir());
	if (plat_net_begin(err, sizeof err) < 0) {
		snprintf(cov.msg, sizeof cov.msg, "Cover laden: %s", err);
	} else {
		for (int i = 0; i < cov.total; i++) {
			char path[160], url[200];
			snprintf(path, sizeof path, "%s/%s.png", plat_cover_dir(), cov.ids[i]);
			if (plat_file_exists(path)) cov.have++;
			else {
				snprintf(url, sizeof url, cov.cat[i] == CAT_PSP ? COVER_URL_PSP : COVER_URL, cov.ids[i]);
				int r = plat_http_download(url, path);
				if (r == 0) cov.got++; else if (r == 1) cov.missing++; else cov.failed++;
			}
			cov.done = i + 1;
			if (cov.failed >= 3 && cov.got == 0) break;   /* GitHub not reachable – don't hammer it */
		}
		plat_net_end();
		if (cov.failed >= 3 && cov.got == 0)
			snprintf(cov.msg, sizeof cov.msg, "Keine Verbindung zum Cover-Server (Details in net.log)");
		else
			snprintf(cov.msg, sizeof cov.msg, "Cover: %d neu, %d schon da, %d nicht verfügbar%s", cov.got, cov.have, cov.missing,
			         cov.failed ? ", Fehler bei einigen" : "");
	}
	__sync_synchronize();
	cov.state = 2;
}

static int count_covers(void)
{
	int n = 0;
	for (int i = 0; i < napps; i++) {
		char path[160];
		snprintf(path, sizeof path, "%s/%s.png", plat_cover_dir(), apps[i].id);
		n += plat_file_exists(path);
	}
	return n;
}

/* new covers on the card: drop all loaded art so it is rebuilt from the files */
static void art_reload_all(void)
{
	for (int i = 0; i < napps; i++) { app_free_art(&apps[i]); apps[i].bg_state = 0; }
	bg_loaded = bg_ok = bg_bad = bg_none = bg_cursor = 0;
	bg_focus = -1;
	top_ok = 0;
	art_boot_load();
}

static void cover_start(void)
{
	if (cov.state == 1) return;
	cov.total = 0;
	for (int i = 0; i < napps && cov.total < MAXA; i++) { cov.cat[cov.total] = (unsigned char)apps[i].cat; snprintf(cov.ids[cov.total++], 16, "%s", apps[i].id); }
	cov.done = cov.got = cov.have = cov.missing = cov.failed = 0;
	cov.msg[0] = 0;
	cov.state = 1;
	if (plat_job(cover_job, NULL) < 0) { cov.state = 0; toast("Lader ist beschäftigt, bitte gleich nochmal"); }
}

static void settings_open(void)
{
	plat_plugin_cfg_load(&S.set_as, &S.set_ret);
	S.plug = plat_plugin_state(S.plug_cfg, sizeof S.plug_cfg);
	S.plug_old = plat_plugin_outdated();
	if (cov.state != 1) cov_have_now = count_covers();
	S.si = 0;
	S.ov = OV_SET;
}

static void toggle_sw(int x, int y, int on)
{
	gfx_rrect(x, y, 44, 24, 12, rgb(on ? C_ACC : C_TRACK), 255);
	gfx_circle(on ? x + 32 : x + 12, y + 12, 9, rgb(on ? 0xffffff : C_MUTE), 255);
}

static void draw_set(void)
{
	dim();
	gfx_rect(0, 0, SET_W, SCR_H, rgb(C_PANEL), 255);
	gfx_rect(SET_W, 0, 1, SCR_H, rgb(C_LINE2), 255);
	hit(0, 0, SET_W + 1, SCR_H, 0, 0, 0);
	icon(22, 22, I_GEAR, rgb(C_TXT));
	text(FONT_BOLD, FS(15), 52, 21, "Einstellungen", rgb(C_TXT), 255);
	gfx_rect(20, 60, SET_W - 40, 1, rgb(C_LINE2), 255);

	static const char *lbl[NSET] = { "Nach Spielende zurück", "Beim Start öffnen", "Rückkehr-Plugin", "Cover laden", "Farbschema" };
	static const char *sub[NSET] = { "Öffnet VitaDeck wieder, wenn das Spiel endet",
	                                 "VitaDeck startet nach dem Einschalten der Vita", NULL, NULL, NULL };
	/* cards are 56 px (+8 gap) so five of them and the notes fit above the footer;
	   the content below is laid out for 64 px cards, hence the -4 shift */
	int y = 70;
	for (int i = 0; i < NSET; i++) {
		int on = i == S.si, h = 56;
		gfx_rrect(16, y, SET_W - 32, h, 10, rgb(C_CARD), 255);
		if (on) { gfx_rrect(14, y - 2, SET_W - 28, h + 4, 12, rgb(C_ACC), 255); gfx_rrect(16, y, SET_W - 32, h, 10, rgb(C_CARD), 255); }
		hit(16, y, SET_W - 32, h, H_SET, i, 0);
		y -= 4;
		text(FONT_BOLD, FS(14), 30, y + 10, lbl[i], rgb(C_TXT), 255);
		if (i == 0) {
			/* three modes instead of a switch: off / after VitaDeck starts / always */
			static const char *val[3] = { "Aus", "VitaDeck-Starts", "Immer" };
			static const char *hint[3] = { "VitaDeck öffnet sich nach Spielende nicht",
			                               "Nach Spielen, die du in VitaDeck startest",
			                               "Nach jedem Spiel, auch aus der LiveArea" };
			int m = S.set_ret < 0 || S.set_ret > 2 ? 1 : S.set_ret;
			text_fit(FONT_REG, FS(11), 30, y + 37, SET_W - 200, hint[m], rgb(C_MUTE), 255);
			int vw = text_w(FONT_BOLD, FS(12), val[m]) + 24;
			gfx_rrect(SET_W - 32 - vw, y + 16, vw, 28, 14, rgb(m ? C_ACC : C_TRACK), 255);
			text(FONT_BOLD, FS(12), SET_W - 32 - vw + 12, y + 21, val[m], rgb(m ? C_INK : C_TXT2), 255);
		} else if (i == 1) {
			text_fit(FONT_REG, FS(11), 30, y + 37, SET_W - 130, sub[i], rgb(C_MUTE), 255);
			toggle_sw(SET_W - 76, y + 20, S.set_as);
		} else if (i == 4) {
			char st[64];
			snprintf(st, sizeof st, "%s · ←/→ wechseln", TH.name);
			text_fit(FONT_REG, FS(11), 30, y + 37, SET_W - 230, st, rgb(C_MUTE), 255);
			/* one swatch per scheme: its accent on its background, the current one ringed */
			for (int k = 0; k < NTHEMES; k++) {
				int cx = SET_W - 52 - (NTHEMES - 1 - k) * 30, cy = y + 32;
				if (k == theme_idx) gfx_circle(cx, cy, 13, rgb(C_TXT), 255);
				gfx_circle(cx, cy, 11, rgb(THEMES[k].bg == 0 ? 0x2c2c33 : THEMES[k].line2), 255);
				gfx_circle(cx, cy, 9, rgb(THEMES[k].bg), 255);
				gfx_circle(cx, cy, 6, rgb(THEMES[k].acc), 255);
			}
		} else if (i == 3) {
			char st[128];
			if (cov.state == 1) snprintf(st, sizeof st, "Lade %d von %d …", cov.done, cov.total);
			else if (cov.state >= 2) snprintf(st, sizeof st, "%s", cov.msg);
			else snprintf(st, sizeof st, "%d von %d vorhanden · aus HexFlow-Covers", cov_have_now, napps);
			text_fit(FONT_REG, FS(11), 30, y + 37, SET_W - 170, st, rgb(C_MUTE), 255);
			if (cov.state == 1) {   /* progress bar instead of the button */
				gfx_rrect(SET_W - 132, y + 28, 100, 8, 4, rgb(C_TRACK), 255);
				int pw = cov.total ? 100 * cov.done / cov.total : 0;
				if (pw > 0) gfx_rrect(SET_W - 132, y + 28, pw < 8 ? 8 : pw, 8, 4, rgb(C_ACC), 255);
			} else {
				const char *act = "Laden";
				int aw = text_w(FONT_BOLD, FS(13), act) + 28;
				gfx_rrect(SET_W - 32 - aw, y + 16, aw, 32, 8, rgb(C_ACC), 255);
				text(FONT_BOLD, FS(13), SET_W - 32 - aw + 14, y + 23, act, rgb(C_INK), 255);
			}
		} else {
			const char *st = S.plug == PLUG_INSTALLED ? (S.plug_old ? "installiert · Neustart der Vita nötig" : "installiert") :
			                 S.plug == PLUG_OTHER_PATH ? "eingetragen (anderer Pfad)" : "nicht installiert";
			char b[96];
			snprintf(b, sizeof b, "Status: %s", st);
			u32 sc = S.plug != PLUG_INSTALLED ? rgb(C_MUTE) : S.plug_old ? rgb(0xffd166) : rgb(C_GREEN);
			text_fit(FONT_REG, FS(11), 30, y + 37, SET_W - 170, b, sc, 255);
			const char *act = S.plug == PLUG_INSTALLED ? "Entfernen" : "Installieren";
			int aw = text_w(FONT_BOLD, FS(13), act) + 28;
			gfx_rrect(SET_W - 32 - aw, y + 16, aw, 32, 8, rgb(S.plug == PLUG_INSTALLED ? C_BTN : C_ACC), 255);
			text(FONT_BOLD, FS(13), SET_W - 32 - aw + 14, y + 23, act, rgb(S.plug == PLUG_INSTALLED ? C_TXT : C_INK), 255);
		}
		y += 4;   /* undo the content shift */
		y += h + 8;
	}
	/* the switches only do something once the plugin is loaded */
	const char *note = S.plug == PLUG_NONE ? "Schalter und Spielzeit-Erfassung wirken erst mit installiertem Plugin."
	                                       : "Plugin-Änderungen wirken nach einem Neustart der Vita.";
	text(FONT_REG, FS(11), 22, y + 4, note, rgb(C_TXT2), 255);
	text(FONT_REG, FS(11), 22, y + 24, "Notausgang beim Einschalten: SELECT = kein Autostart, L = keine Plugins", rgb(C_MUTE), 255);
	char b[64];
	snprintf(b, sizeof b, "Konfiguration: %s", S.plug_cfg);
	text(FONT_REG, FS(11), 22, y + 44, b, rgb(C_DIM), 255);
	/* required attribution for the box art database (CC BY-NC-SA 4.0) */
	text(FONT_REG, FS(11), 22, y + 64, "Cover: HexFlow-Covers von Andiweli, CC BY-NC-SA 4.0", rgb(C_DIM), 255);
	text(FONT_REG, FS(11), 22, y + 82, "Spielgrafiken © der jeweiligen Rechteinhaber", rgb(C_DIM), 255);
	Hint hs[] = { { G_X, S.si >= 2 ? "Ausführen" : "Umschalten" }, { G_O, "Zurück" } };
	footer(NULL, hs, 2);
}

static void draw_confirm(void)
{
	gfx_rect(0, 0, SCR_W, SCR_H, rgb(0x000000), 150);
	hit(0, 0, SCR_W, SCR_H, H_BACKDROP, 0, 0);
	int w = 560, h = 200, x = (SCR_W - w) / 2, y = (SCR_H - h) / 2 - 20;
	gfx_rrect(x, y, w, h, 12, rgb(C_PANEL), 255);
	hit(x, y, w, h, 0, 0, 0);
	int inst = S.confirm == CF_INSTALL;
	text(FONT_BOLD, FS(16), x + 24, y + 20, inst ? "Rückkehr-Plugin installieren?" : "Rückkehr-Plugin entfernen?", rgb(C_TXT), 255);
	char l1[96], l2[96];
	if (inst) snprintf(l1, sizeof l1, "Kopiert vdreturn.suprx nach ur0:tai/ und trägt es unter *main ein.");
	else snprintf(l1, sizeof l1, "Entfernt den Eintrag und die Datei aus ur0:tai/.");
	snprintf(l2, sizeof l2, "Geändert wird %s, vorher wird eine Sicherung", S.plug_cfg);
	text_fit(FONT_REG, FS(12), x + 24, y + 62, w - 48, l1, rgb(C_TXT2), 255);
	text_fit(FONT_REG, FS(12), x + 24, y + 86, w - 48, l2, rgb(C_TXT2), 255);
	text_fit(FONT_REG, FS(12), x + 24, y + 108, w - 48, "angelegt (config.txt.vitadeck.bak). Wirkt nach einem Neustart.", rgb(C_TXT2), 255);
	text_fit(FONT_REG, FS(11), x + 24, y + 146, w - 48, "Falls etwas hängt: beim Einschalten L halten, dann lädt taiHEN keine Plugins.", rgb(C_MUTE), 255);
	Hint hs[] = { { G_X, inst ? "Installieren" : "Entfernen" }, { G_O, "Abbrechen" } };
	footer(NULL, hs, 2);
}

/* switch the colour scheme right away, without touching the memory card:
   icons are pre-composited onto C_CARD → recoloured via their alpha;
   hero backgrounds are darkened towards C_PANEL → rebuilt from the small blur
   (a few ms each, on the job thread, as they are needed); placeholders redrawn */
static Img *ctx_img;          /* defined with the context menu below */
static const Img *ctx_src;
static void theme_step(int d)
{
	u32 oldc = rgb(C_CARD);
	homebg_id[0] = 0;   /* darkened towards the old C_BG: rebuild */
	homebg_gen++;
	theme_idx = (theme_idx + d + NTHEMES) % NTHEMES;
	u32 newc = rgb(C_CARD);
	for (int i = 0; i < napps; i++) {
		App *a = &apps[i];
		if (a->ic_state == 1) { recolor(a->ic, a->ica, oldc, newc); recolor(a->ics, a->icsa, oldc, newc); }
		img_free(a->cv); img_free(a->cvs); a->cv = a->cvs = NULL;
		if (a->bg) { img_free(a->bg); a->bg = NULL; a->bg_state = 0; }
	}
	bg_loaded = 0;
	/* the visible hero right away (no flash of the plain gradient) */
	int ai = nrows ? home_app() : -1;
	if (ai >= 0 && apps[ai].bgs) {
		apps[ai].bg = bg_from_small(apps[ai].bgs);
		if (apps[ai].bg) { apps[ai].bg_state = 1; apps[ai].bg_tick = ++bg_clock; bg_loaded = 1; }
	}
	img_free(ctx_img); ctx_img = NULL; ctx_src = NULL;
	under_ok = 0; top_ok = 0;
	S.dirty = 1;
	save_state();
}

static void settings_act(int i)
{
	S.si = i;
	if (i == 4) { theme_step(1); return; }
	if (i == 0 || i == 1) {
		if (i == 0) S.set_ret = (S.set_ret + 1) % 3; else S.set_as = !S.set_as;
		if (plat_plugin_cfg_save(S.set_as, S.set_ret) < 0) toast("Einstellung konnte nicht gespeichert werden");
	} else if (i == 3) {
		cover_start();
	} else {
		S.confirm = S.plug == PLUG_INSTALLED ? CF_UNINSTALL : CF_INSTALL;
		S.ov = OV_CONFIRM;
	}
	S.dirty = 1;
}

static void confirm_act(void)
{
	int inst = S.confirm == CF_INSTALL;
	int r = inst ? plat_plugin_install() : plat_plugin_uninstall();
	S.plug = plat_plugin_state(S.plug_cfg, sizeof S.plug_cfg);
	S.plug_old = plat_plugin_outdated();
	S.ov = OV_SET;
	char m[96];
	if (r == 0) snprintf(m, sizeof m, inst ? "Plugin installiert · wirkt nach Neustart" : "Plugin entfernt · wirkt nach Neustart");
	else if (r == -1) snprintf(m, sizeof m, "Plugin-Datei fehlt in VitaDeck");
	else if (r == -2) snprintf(m, sizeof m, "%s nicht gefunden, nichts geändert", S.plug_cfg);
	else if (r == -3) snprintf(m, sizeof m, "Sicherung fehlgeschlagen, nichts geändert");
	else if (r == -5) snprintf(m, sizeof m, "Prüfung fehlgeschlagen! Sicherung: config.txt.vitadeck.bak");
	else snprintf(m, sizeof m, "Fehler %d, Sicherung: config.txt.vitadeck.bak", r);
	toast(m);
	S.dirty = 1;
}

static void slider(int x, int y, int w, int pct, int focus)
{
	gfx_rrect(x, y, w, 8, 4, rgb(C_TRACK), 255);
	int fw = w * pct / 100;
	if (fw > 0) gfx_rrect(x, y, fw < 8 ? 8 : fw, 8, 4, rgb(C_ACC), 255);
	gfx_circle(x + fw, y + 4, focus ? 9 : 7, rgb(0xffffff), 255);
}

static void draw_qa(void)
{
	dim();
	int px = SCR_W - 380;
	gfx_rect(px, 0, 380, SCR_H, rgb(C_PANEL), 255);
	gfx_rect(px - 1, 0, 1, SCR_H, rgb(C_LINE2), 255);
	hit(px, 0, 380, SCR_H, 0, 0, 0);
	text(FONT_BOLD, FS(15), px + 20, 20, "Schnellzugriff", rgb(C_TXT), 255);
	int y = 56;
	const char *lbl[] = { "Helligkeit", "Lautstärke" };
	int val[] = { S.bright, S.vol_ok ? S.vol * 100 / 30 : 0 };
	int ok[] = { S.bright_ok, S.vol_ok };
	for (int i = 0; i < 2; i++) {
		int on = i == S.qi;
		gfx_rrect(px + 20, y, 340, 70, 10, rgb(C_CARD), 255);
		if (on) {
			gfx_rrect(px + 18, y - 2, 344, 74, 12, rgb(C_ACC), 255);
			gfx_rrect(px + 20, y, 340, 70, 10, rgb(C_CARD), 255);
		}
		text(FONT_REG, FS(13), px + 34, y + 12, lbl[i], rgb(C_TXT2), 255);
		char v[32];
		if (ok[i]) snprintf(v, sizeof v, "%d %%", val[i]); else snprintf(v, sizeof v, "nicht verfügbar");
		text(FONT_REG, FS(13), px + 346 - text_w(FONT_REG, FS(13), v), y + 12, v, rgb(C_TXT2), 255);
		slider(px + 34, y + 44, 312, ok[i] ? val[i] : 0, on);
		hit(px + 20, y, 340, 70, H_QA, i, 0);
		y += 84;
	}
	/* navigation sounds: a switch instead of a slider */
	{
		int on = S.qi == 2;
		gfx_rrect(px + 20, y, 340, 50, 10, rgb(C_CARD), 255);
		if (on) {
			gfx_rrect(px + 18, y - 2, 344, 54, 12, rgb(C_ACC), 255);
			gfx_rrect(px + 20, y, 340, 50, 10, rgb(C_CARD), 255);
		}
		text(FONT_REG, FS(13), px + 34, y + 15, "Navigationsklänge", rgb(C_TXT2), 255);
		toggle_sw(px + 302, y + 13, S.sound);
		hit(px + 20, y, 340, 50, H_QA, 2, 0);
		y += 64;
	}
	/* info card */
	gfx_rrect(px + 20, y, 340, 134, 10, rgb(C_CARD), 255);
	int chg = 0, pct = plat_battery(&chg);
	char b[128];
	if (pct >= 0) snprintf(b, sizeof b, "Akku %d %%%s", pct, chg ? " · lädt" : "");
	else snprintf(b, sizeof b, "Akku: unbekannt");
	uint64_t fr, tot;
	if (plat_storage(&fr, &tot) == 0) {
		char f1[24], f2[24];
		fmt_size((int64_t)fr, f1, sizeof f1); fmt_size((int64_t)tot, f2, sizeof f2);
		int n = (int)strlen(b);
		snprintf(b + n, sizeof b - n, " · ux0: %s frei von %s", f1, f2);
	}
	text(FONT_BOLD, FS(14), px + 34, y + 14, b, rgb(C_TXT), 255);
	snprintf(b, sizeof b, "%d Apps · Cover: %d, nur Icon: %d, Fehler: %d · Hell.: %d", napps, bg_ok, bg_none, bg_bad, plat_bright_raw);
	text(FONT_REG, FS(13), px + 34, y + 40, b, rgb(C_TXT2), 255);
	text(FONT_REG, FS(12), px + 34, y + 62, plat_job_threaded() ? "Einstellungen gelten systemweit. · Lader: Thread"
	                                                             : "Einstellungen gelten systemweit. · Lader: inline", rgb(C_MUTE), 255);
#define MS(v) (v) / 1000, (v) % 1000 / 100
	snprintf(b, sizeof b, "Bildaufbau: %u,%u ms + Übertragung %u,%u ms", MS(S.perf_us), MS(plat_present_us));
	text(FONT_REG, FS(12), px + 34, y + 84, b, rgb(C_MUTE), 255);
	snprintf(b, sizeof b, "Kopf %u,%u · Hero %u,%u · Kacheln %u,%u · Fuß %u,%u",
	         MS(S.perf_sec[0]), MS(S.perf_sec[1]), MS(S.perf_sec[2]), MS(S.perf_sec[3]));
	text(FONT_REG, FS(12), px + 34, y + 104, b, rgb(C_MUTE), 255);
#undef MS
	Hint hs[] = { { G_LR, "Ändern" }, { G_O, "Schließen" } };
	footer(NULL, hs, 2);
}

static Img *ctx_img;          /* 48 px icon for the context menu */
static const Img *ctx_src;

enum { CA_LAUNCH, CA_FAV, CA_UNRECENT, CA_HIDE, CA_SORT, CA_OC, CA_COLL, CA_SEARCH };
#define CTX_MAX 8

static struct { volatile int state; char id[16]; int64_t app, patch; char pver[16]; int oc; } cinfo;   /* 0 idle, 1 wanted, 2 running, 3 done */
static int ctx_items(const App *a, const char *out[], int act[])
{
	int n = 0;
	out[n] = "Starten"; act[n++] = CA_LAUNCH;
	out[n] = a->fav ? "Aus Favoriten entfernen" : "Zu Favoriten hinzufügen"; act[n++] = CA_FAV;
	if (a->last && !a->hidden) { out[n] = "Aus „Zuletzt gestartet“ entfernen"; act[n++] = CA_UNRECENT; }
	out[n] = a->hidden ? "Einblenden" : "Ausblenden"; act[n++] = CA_HIDE;
	out[n] = "Sammlungen …"; act[n++] = CA_COLL;
	if (S.view == V_LIB) { out[n] = search[0] ? "Suche ändern …" : "Suchen …"; act[n++] = CA_SEARCH; }
	if (S.view == V_LIB) {
		static char sl[48];
		snprintf(sl, sizeof sl, "Sortieren: %s", SORTS[(S.sort + 1) % NSORTS]);
		out[n] = sl; act[n++] = CA_SORT;
	}
	/* per-game clocks (PSVshell profile); the current one comes with the app details job */
	static char oc[48];
	static const char *OCN[] = { "Standard", "Schnell (444 MHz)", "Maximal (500 MHz)", "Eigenes (PSVshell)" };
	if (!plat_oc_available()) snprintf(oc, sizeof oc, "Takt: PSVshell nicht installiert");
	else if (cinfo.state == 3 && !strcmp(cinfo.id, a->id)) snprintf(oc, sizeof oc, "Takt: %s", OCN[cinfo.oc & 3]);
	else snprintf(oc, sizeof oc, "Takt: …");
	out[n] = oc; act[n++] = CA_OC;
	return n;
}

/* app details for the options menu: sizes need a directory walk, so they are
   computed on the job thread (main thread must not do file IO while jobs run) */

static void cinfo_work(void *arg)
{
	(void)arg;
	plat_app_info(cinfo.id, &cinfo.app, &cinfo.patch, cinfo.pver, sizeof cinfo.pver);
	cinfo.oc = plat_oc_available() ? plat_oc_get(cinfo.id) : OC_DEFAULT;
	__sync_synchronize();
	cinfo.state = 3;
}

static void cinfo_request(const App *a)
{
	if (cinfo.state == 2) return;   /* still running: picked up once it is done */
	if (cinfo.state == 3 && !strcmp(cinfo.id, a->id)) return;
	snprintf(cinfo.id, sizeof cinfo.id, "%s", a->id);
	cinfo.state = 1;
}

static void pump_cinfo(void)
{
	if (cinfo.state == 1) {
		cinfo.state = 2;   /* before starting: the job may finish (state 3) before plat_job returns */
		if (plat_job(cinfo_work, NULL) < 0) cinfo.state = 1;   /* worker busy: try again next frame */
	}
	static int seen;
	if (cinfo.state == 3 && !seen && S.ov == OV_CTX) S.dirty = 1;   /* redraw once with the result */
	seen = cinfo.state == 3;
}

static void draw_ctx(void)
{
	dim();
	App *a = &apps[S.ctx_app];
	cinfo_request(a);
	const char *it[CTX_MAX];
	int act[CTX_MAX];
	int n = ctx_items(a, it, act);
	int w = 380, h = 84 + n * 46, x = (SCR_W - w) / 2, y = (SCR_H - h) / 2 - 10;
	gfx_rrect(x, y, w, h, 12, rgb(C_PANEL), 255);
	hit(x, y, w, h, 0, 0, 0);
	const Img *src = a->ic_state == 1 && a->ic ? a->ic : cover_img(a, 128);
	if (!src) draw_cover(a, x + 16, y + 14, 48, 8);
	else {
		if (ctx_src != src) { img_free(ctx_img); ctx_img = img_scaled(src, 48, 48); ctx_src = src; }
		gfx_rrect(x + 16, y + 14, 48, 48, 8, rgb(C_CARD), 255);
		gfx_img(ctx_img, x + 16, y + 14, 8, 255);
	}
	text_fit(FONT_BOLD, FS(16), x + 76, y + 18, w - 92, a->title, rgb(C_TXT), 255);
	char info[128], sz[24];
	int k = snprintf(info, sizeof info, "%s", a->id);
	int done = cinfo.state == 3 && !strcmp(cinfo.id, a->id);
	const char *ver = done && cinfo.pver[0] ? cinfo.pver : a->ver;
	if (ver[0]) k += snprintf(info + k, sizeof info - k, " · Version %s", ver);
	if (done) { fmt_size(cinfo.app + cinfo.patch, sz, sizeof sz); snprintf(info + k, sizeof info - k, " · %s", sz); }
	else snprintf(info + k, sizeof info - k, " · Größe wird ermittelt …");
	text_fit(FONT_REG, FS(12), x + 76, y + 42, w - 92, info, rgb(C_MUTE), 255);
	int iy = y + 76;
	for (int i = 0; i < n; i++) {
		int on = i == S.ci;
		if (on) gfx_rrect(x + 10, iy, w - 20, 42, 8, rgb(C_ACC), 255);
		text(on ? FONT_BOLD : FONT_REG, FS(14), x + 24, iy + 12, it[i], rgb(on ? C_INK : C_TXT), 255);
		hit(x + 10, iy, w - 20, 42, H_CTX, i, 0);
		iy += 46;
	}
	Hint hs[] = { { G_X, "Auswählen" }, { G_O, "Schließen" } };
	footer(NULL, hs, 2);
}

static void draw_launch(void)
{
	App *a = &apps[S.launch_app];
	gfx_rect(0, 0, SCR_W, SCR_H, rgb(0x000000), 220);
	int s = 128, x = (SCR_W - s) / 2, y = SCR_H / 2 - 100;
	draw_icon_or_cover(a, x, y, s, 14);
	char t[160];
	snprintf(t, sizeof t, "Starte %s …", a->title);
	int tw = text_w(FONT_BOLD, FS(18), t);
	if (tw > 860) tw = 860;
	text_fit(FONT_BOLD, FS(18), (SCR_W - tw) / 2, y + s + 22, 860, t, rgb(C_TXT), 255);
}

static void draw_toast(void)
{
	if (!S.toast[0]) return;
	int tw = text_w(FONT_REG, FS(13), S.toast);
	int w = tw + 36, x = (SCR_W - w) / 2, y = SCR_H - 36 - 52;
	gfx_rrect(x, y, w, 38, 8, rgb(C_EDGE), 255);
	gfx_rrect(x + 1, y + 1, w - 2, 36, 7, rgb(C_BTN), 255);
	text(FONT_REG, FS(13), x + 18, y + 11, S.toast, rgb(C_TXT), 255);
}

/* ---------- on-screen keyboard ----------
   Drawn by VitaDeck itself: the system keyboard (sceImeDialog) needs GXM rendering,
   VitaDeck renders in software. German QWERTZ, auto-capitalisation at the start and
   after a space. D-pad moves, X types, □ deletes, △ = space, START = done, O = cancel. */
#define KB_ROWS 5
static const char *KB[KB_ROWS][11] = {
	{ "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", NULL },
	{ "Q", "W", "E", "R", "T", "Z", "U", "I", "O", "P", "Ü" },
	{ "A", "S", "D", "F", "G", "H", "J", "K", "L", "Ö", "Ä" },
	{ "Y", "X", "C", "V", "B", "N", "M", "-", ".", "ß", NULL },
	{ "Leerzeichen", "Löschen", "Fertig", NULL },
};
static struct {
	char title[48], buf[48];
	int r, c, ret_ov;
	void (*change)(const char *s);
	void (*done)(const char *s, int ok);
} kb;

static int kb_len(int r) { int n = 0; while (n < 11 && KB[r][n]) n++; return n; }

static void kbd_open(const char *title, const char *init, void (*change)(const char *), void (*done)(const char *, int), int ret_ov)
{
	snprintf(kb.title, sizeof kb.title, "%s", title);
	snprintf(kb.buf, sizeof kb.buf, "%s", init);
	kb.r = 1; kb.c = 0; kb.change = change; kb.done = done; kb.ret_ov = ret_ov;
	S.ov = OV_KBD;
	S.dirty = 1;
}

static void kbd_changed(void) { if (kb.change) kb.change(kb.buf); S.dirty = 1; }

static void kbd_backspace(void)
{
	int n = (int)strlen(kb.buf);
	while (n > 0 && ((unsigned char)kb.buf[n - 1] & 0xC0) == 0x80) n--;   /* UTF-8 continuation bytes */
	if (n > 0) n--;
	kb.buf[n] = 0;
	kbd_changed();
}

static void kbd_type(const char *k)
{
	int n = (int)strlen(kb.buf);
	int cap = n == 0 || kb.buf[n - 1] == ' ';
	char t[8];
	if (!cap && k[0] >= 'A' && k[0] <= 'Z' && !k[1]) { t[0] = (char)(k[0] + 32); t[1] = 0; }
	else if (!cap && (unsigned char)k[0] == 0xC3 && (unsigned char)k[1] >= 0x80 && (unsigned char)k[1] <= 0x9E) {
		t[0] = k[0]; t[1] = (char)((unsigned char)k[1] + 0x20); t[2] = 0;   /* Ä → ä */
	} else snprintf(t, sizeof t, "%s", k);
	if (n + (int)strlen(t) >= (int)sizeof kb.buf - 1) return;
	strcat(kb.buf, t);
	kbd_changed();
}

static void kbd_finish(int ok)
{
	S.ov = kb.ret_ov;
	S.dirty = 1;
	under_ok = 0;
	if (kb.done) kb.done(kb.buf, ok);
}

static void kbd_key(int r, int c)
{
	kb.r = r; kb.c = c;
	if (r == KB_ROWS - 1) {
		if (c == 0) { if (strlen(kb.buf) + 1 < sizeof kb.buf) { strcat(kb.buf, " "); kbd_changed(); } }
		else if (c == 1) kbd_backspace();
		else kbd_finish(1);
		return;
	}
	kbd_type(KB[r][c]);
}

static void kbd_press(unsigned b)
{
	if (b == B_UP && kb.r > 0) { kb.r--; if (kb.c >= kb_len(kb.r)) kb.c = kb_len(kb.r) - 1; }
	else if (b == B_DOWN && kb.r < KB_ROWS - 1) {
		kb.r++;
		if (kb.r == KB_ROWS - 1) kb.c = kb.c < 4 ? 0 : kb.c < 7 ? 1 : 2;   /* wide keys in the last row */
		else if (kb.c >= kb_len(kb.r)) kb.c = kb_len(kb.r) - 1;
	}
	else if (b == B_LEFT) kb.c = kb.c > 0 ? kb.c - 1 : kb_len(kb.r) - 1;
	else if (b == B_RIGHT) kb.c = kb.c < kb_len(kb.r) - 1 ? kb.c + 1 : 0;
	else if (b == B_X) kbd_key(kb.r, kb.c);
	else if (b == B_SQ) kbd_backspace();
	else if (b == B_TRI) kbd_key(KB_ROWS - 1, 0);
	else if (b == B_START) kbd_finish(1);
	else if (b == B_O) kbd_finish(0);
}

static void draw_kbd(void)
{
	dim();
	int py = 220;
	gfx_rect(0, py, SCR_W, SCR_H - 36 - py, rgb(C_PANEL), 255);
	gfx_rect(0, py, SCR_W, 1, rgb(C_LINE2), 255);
	hit(0, 0, SCR_W, SCR_H, 0, 0, 0);
	text(FONT_BOLD, FS(15), (SCR_W - text_w(FONT_BOLD, FS(15), kb.title)) / 2, 128, kb.title, rgb(C_TXT), 255);
	int fw = 560, fx = (SCR_W - fw) / 2;
	gfx_rrect(fx, 160, fw, 44, 10, rgb(C_CARD), 255);
	gfx_rrect(fx, 202, fw, 2, 1, rgb(C_ACC), 255);
	char shown[64];
	snprintf(shown, sizeof shown, "%s|", kb.buf);
	text_fit(FONT_REG, FS(16), fx + 16, 171, fw - 32, shown, rgb(C_TXT), 255);
	for (int r = 0; r < KB_ROWS; r++) {
		int n = kb_len(r), y = py + 14 + r * 50;
		int widths[11], total = 0;
		for (int c = 0; c < n; c++) { widths[c] = r == KB_ROWS - 1 ? (c == 0 ? 330 : c == 1 ? 150 : 180) : 70; total += widths[c] + 8; }
		int x = (SCR_W - total + 8) / 2;
		for (int c = 0; c < n; c++) {
			int on = r == kb.r && c == kb.c, w = widths[c];
			u32 bgc = on ? rgb(C_ACC) : r == KB_ROWS - 1 && c == 2 ? rgb(C_BTN) : rgb(C_CARD);
			gfx_rrect(x, y, w, 42, 8, bgc, 255);
			const char *l = KB[r][c];
			int f = r == KB_ROWS - 1 ? FONT_BOLD : FONT_REG;
			text(f, FS(14), x + (w - text_w(f, FS(14), l)) / 2, y + 11, l, rgb(on ? C_INK : C_TXT), 255);
			hit(x, y, w, 42, H_KEY, r, c);
			x += w + 8;
		}
	}
	Hint hs[] = { { G_X, "Tippen" }, { G_SQ, "Löschen" }, { G_TRI, "Leerzeichen" }, { G_START, "Fertig" }, { G_O, "Abbrechen" } };
	footer(NULL, hs, 5);
}

/* ---------- library search (ROADMAP 10) ---------- */
static char search_saved[48];

static void search_change(const char *s)
{
	snprintf(search, sizeof search, "%s", s);
	S.lidx = 0; S.loff = 0; S.ly = 0;
	rebuild();
}

static void search_done(const char *s, int ok)
{
	if (!ok) { snprintf(search, sizeof search, "%s", search_saved); S.lidx = 0; rebuild(); return; }
	snprintf(search, sizeof search, "%s", s);
	rebuild();
}

static void search_open(void)
{
	snprintf(search_saved, sizeof search_saved, "%s", search);
	S.view = V_LIB;
	kbd_open("Suche in der Bibliothek", search, search_change, search_done, OV_NONE);
}

/* ---------- custom collections overlay (ROADMAP 9) ---------- */
static int coll_list(int out[])   /* used slots, then -1 for "Neue Sammlung" */
{
	int n = 0;
	for (int c = 0; c < MAXCOLL; c++) if (colls[c][0]) out[n++] = c;
	out[n++] = -1;
	return n;
}

static void coll_new_done(const char *s, int ok)
{
	S.ov = OV_COLL;
	if (!ok || !s[0]) return;
	for (int c = 0; c < MAXCOLL; c++) {
		if (colls[c][0]) continue;
		snprintf(colls[c], sizeof colls[c], "%s", s);
		apps[S.ctx_app].colmask |= 1u << c;
		save_state();
		rebuild();
		char m[64]; snprintf(m, sizeof m, "Sammlung „%s“ angelegt", colls[c]); toast(m);
		return;
	}
}

static void coll_act(int i)
{
	int l[MAXCOLL + 1], n = coll_list(l);
	if (i < 0 || i >= n) return;
	S.coi = i;
	S.coll_del = 0;
	int c = l[i];
	if (c < 0) {
		int used = n - 1;
		if (used >= MAXCOLL) { toast("Mehr als 8 Sammlungen gehen nicht"); return; }
		kbd_open("Neue Sammlung", "", NULL, coll_new_done, OV_COLL);
		return;
	}
	apps[S.ctx_app].colmask ^= 1u << c;
	save_state();
	rebuild();
}

static void coll_delete(void)
{
	int l[MAXCOLL + 1], n = coll_list(l);
	if (S.coi >= n || l[S.coi] < 0) return;
	if (!S.coll_del) { S.coll_del = 1; toast("Nochmal △ drücken, um die Sammlung zu löschen"); return; }
	int c = l[S.coi];
	char m[64]; snprintf(m, sizeof m, "Sammlung „%s“ gelöscht", colls[c]);
	colls[c][0] = 0;
	for (int i = 0; i < napps; i++) apps[i].colmask &= ~(1u << c);
	S.coll_del = 0;
	if (S.coi > 0) S.coi--;
	save_state();
	rebuild();
	toast(m);
}

static void coll_press(unsigned b)
{
	int l[MAXCOLL + 1], n = coll_list(l);
	if (b == B_UP) { S.coi = S.coi > 0 ? S.coi - 1 : 0; S.coll_del = 0; }
	else if (b == B_DOWN) { S.coi = S.coi < n - 1 ? S.coi + 1 : n - 1; S.coll_del = 0; }
	else if (b == B_X) coll_act(S.coi);
	else if (b == B_TRI) coll_delete();
	else if (b == B_O) { S.ov = OV_CTX; S.coll_del = 0; }
}

static void draw_coll(void)
{
	dim();
	const App *a = &apps[S.ctx_app];
	int l[MAXCOLL + 1], n = coll_list(l);
	int w = 420, h = 84 + n * 46, x = (SCR_W - w) / 2, y = (SCR_H - h) / 2 - 10;
	gfx_rrect(x, y, w, h, 12, rgb(C_PANEL), 255);
	hit(x, y, w, h, 0, 0, 0);
	text(FONT_BOLD, FS(16), x + 20, y + 18, "Sammlungen", rgb(C_TXT), 255);
	text_fit(FONT_REG, FS(12), x + 20, y + 44, w - 40, a->title, rgb(C_MUTE), 255);
	int iy = y + 76;
	for (int i = 0; i < n; i++) {
		int on = i == S.coi, c = l[i];
		if (on) gfx_rrect(x + 10, iy, w - 20, 42, 8, rgb(C_ACC), 255);
		u32 fg = rgb(on ? C_INK : C_TXT);
		if (c >= 0) {
			int member = a->colmask >> c & 1;
			gfx_rrect(x + 22, iy + 10, 22, 22, 6, on ? rgb(C_INK) : rgb(C_TRACK), 255);
			if (member) {
				u32 ck = on ? rgb(C_ACC) : rgb(C_ACC);
				gfx_line(x + 27, iy + 21, x + 32, iy + 26, 2.4f, ck, 255);
				gfx_line(x + 32, iy + 26, x + 40, iy + 15, 2.4f, ck, 255);
			}
			text_fit(on ? FONT_BOLD : FONT_REG, FS(14), x + 56, iy + 12, w - 90, colls[c], fg, 255);
		} else text(on ? FONT_BOLD : FONT_REG, FS(14), x + 24, iy + 12, "+ Neue Sammlung …", fg, 255);
		hit(x + 10, iy, w - 20, 42, H_COLL, i, 0);
		iy += 46;
	}
	Hint hs[] = { { G_X, "Umschalten" }, { G_TRI, "Löschen" }, { G_O, "Zurück" } };
	footer(NULL, hs, 3);
}

static void draw(void)
{
	nhits = 0;
	if (S.view == V_BOOT) { draw_boot(); return; }
	int cached = (S.ov >= OV_MENU && S.ov <= OV_CTX) || S.ov == OV_SET || S.ov == OV_CONFIRM || S.ov == OV_COLL;   /* OV_KBD: live search changes the view below */
	if (!cached) under_ok = 0;
	if (cached && under_ok) {
		memcpy(gfx_buffer(), under, SCR_W * SCR_H * 4);   /* base + dim from cache */
	} else {
		uint64_t t0 = plat_us();
		if (S.view == V_LIB) draw_lib(); else draw_home();
		unsigned dt = (unsigned)(plat_us() - t0);
		S.perf_us = S.perf_us ? (S.perf_us * 3 + dt) / 4 : dt;
	}
	if (S.ov == OV_MENU) draw_menu();
	else if (S.ov == OV_QA) draw_qa();
	else if (S.ov == OV_CTX) draw_ctx();
	else if (S.ov == OV_LAUNCH) draw_launch();
	else if (S.ov == OV_SET) draw_set();
	else if (S.ov == OV_CONFIRM) { draw_set(); draw_confirm(); }
	else if (S.ov == OV_KBD) draw_kbd();
	else if (S.ov == OV_COLL) draw_coll();
	draw_toast();
}

/* ---------- actions ---------- */
static void launch(int ai)
{
	if (ai < 0) return;
	S.launch_prev_last = apps[ai].last;
	apps[ai].last = now_minutes();
	save_state();
	lists_dirty = 1;
	S.launch_app = ai;
	S.launch_frames = 0;
	S.ov = OV_LAUNCH;
	S.dirty = 1;
}

static void toggle_fav(int ai)
{
	if (ai < 0) return;
	int keep = S.view == V_LIB ? ai : -1;
	apps[ai].fav = !apps[ai].fav;
	save_state();
	rebuild();
	if (keep >= 0) for (int i = 0; i < nlib; i++) if (lib[i] == keep) S.lidx = i;
	toast(apps[ai].fav ? "Zu Favoriten hinzugefügt" : "Aus Favoriten entfernt");
}

static void free_apps(void)
{
	for (int i = 0; i < napps; i++) {
		img_free(apps[i].ic); img_free(apps[i].ics);
		free(apps[i].ica); free(apps[i].icsa);
		img_free(apps[i].cv); img_free(apps[i].cvs);
		app_free_art(&apps[i]);
	}
	bg_focus = -1;   /* a running background job is matched by id when it finishes */
	bg_ok = bg_bad = bg_none = bg_cursor = bg_loaded = 0;
	img_free(ctx_img); ctx_img = NULL; ctx_src = NULL;
	under_ok = 0;
	top_ok = 0;
	free(apps);
	apps = NULL; napps = 0;
}

void ui_scan(void)
{
	free_apps();
	PlatApp *pa = NULL;
	int n = plat_scan_apps(&pa);
	if (n > MAXA) n = MAXA;
	apps = calloc(n > 0 ? n : 1, sizeof(App));
	for (int i = 0; i < n; i++) {
		snprintf(apps[i].id, sizeof apps[i].id, "%s", pa[i].id);
		snprintf(apps[i].title, sizeof apps[i].title, "%s", pa[i].title);
		snprintf(apps[i].icon_path, sizeof apps[i].icon_path, "%s", pa[i].icon_path);
		snprintf(apps[i].ver, sizeof apps[i].ver, "%s", pa[i].ver);
		apps[i].cat = pa[i].cat;
	}
	napps = n < 0 ? 0 : n;
	free(pa);
	icon_cursor = 0;
	load_state();
	load_playtime();
	/* brightness: the system setting wins (it may have been changed in the PS
	   button menu since); only if it can't be read, apply VitaDeck's saved value */
	int sb = plat_get_brightness();
	if (sb >= 0) { S.bright = sb < 10 ? 10 : sb; S.bright_ok = 1; }
	else S.bright_ok = plat_set_brightness(S.bright) >= 0;
	art_boot_load();   /* cached covers right away – no PNG decoding at startup */
	plat_oc_available();   /* reads the taiHEN config once, before any job runs */
	static int plug_checked;   /* once per start: bring an installed plugin up to date */
	if (!plug_checked) {
		plug_checked = 1;
		if (plat_plugin_update() == 1) toast("Rückkehr-Plugin aktualisiert · wirkt nach Neustart");
		else if (plat_plugin_outdated()) toast("Neues Rückkehr-Plugin läuft erst nach einem Neustart der Vita");
	}
	rebuild();
}

static void menu_act(int i)
{
	S.ov = OV_NONE;
	switch (i) {
	case 0: S.view = V_HOME; break;
	case 1: S.view = V_LIB; break;
	case 2: ui_scan(); { char m[64]; snprintf(m, sizeof m, "%d Apps gefunden", napps); toast(m); } break;
	case 3: settings_open(); break;
	case 4: plat_standby(); break;
	case 5: plat_reboot(); break;
	case 6: plat_exit(); break;
	}
	S.dirty = 1;
}

static void qa_adjust(int d)
{
	if (S.qi == 2) { (void)d; S.sound = !S.sound; save_state(); if (S.sound) plat_sound(SND_OK); S.dirty = 1; return; }
	if (S.qi == 0) {
		int v = S.bright + d * 10;
		S.bright = v < 10 ? 10 : v > 100 ? 100 : v;
		S.bright_ok = plat_set_brightness(S.bright) >= 0;
		save_state();
	} else if (S.vol_ok) {
		int v = S.vol + d;
		S.vol = v < 0 ? 0 : v > 30 ? 30 : v;
		plat_set_volume(S.vol);
	}
	S.dirty = 1;
}

static void lib_toggle_sort(int keep_app)
{
	S.sort = (S.sort + 1) % NSORTS;
	rebuild();
	for (int i = 0; i < nlib; i++) if (lib[i] == keep_app) S.lidx = i;
	char m[64];
	snprintf(m, sizeof m, "Sortierung: %s", SORTS[S.sort]);
	toast(m);
}

static void ctx_act(int i)
{
	App *a = &apps[S.ctx_app];
	const char *it[CTX_MAX];
	int act[CTX_MAX];
	int n = ctx_items(a, it, act);
	S.ov = OV_NONE;
	S.dirty = 1;
	if (i < 0 || i >= n) return;
	switch (act[i]) {
	case CA_LAUNCH: launch(S.ctx_app); break;
	case CA_FAV: toggle_fav(S.ctx_app); break;
	case CA_UNRECENT: a->last = 0; save_state(); rebuild(); toast("Aus „Zuletzt gestartet“ entfernt"); break;
	case CA_HIDE: {
		int keep = S.lidx;
		a->hidden = !a->hidden;
		save_state();
		rebuild();
		if (S.view == V_LIB) { S.lidx = keep < nlib ? keep : (nlib ? nlib - 1 : 0); }
		toast(a->hidden ? "Ausgeblendet · wiederfinden unter Bibliothek → „Ausgeblendet“" : "Wieder eingeblendet");
		break;
	}
	case CA_SORT: lib_toggle_sort(S.ctx_app); break;
	case CA_COLL: S.ov = OV_COLL; S.coi = 0; S.coll_del = 0; break;
	case CA_SEARCH: search_open(); break;
	case CA_OC: {
		S.ov = OV_CTX;   /* stay in the menu: the entry shows the new value */
		if (!plat_oc_available()) { toast("PSVshell (Kernel-Plugin) installieren, z. B. über VitaDB"); break; }
		if (cinfo.state != 3 || strcmp(cinfo.id, a->id)) break;   /* current value not read yet */
		int next = cinfo.oc == OC_FAST ? OC_MAX : cinfo.oc == OC_MAX ? OC_DEFAULT : cinfo.oc == OC_CUSTOM ? OC_DEFAULT : OC_FAST;
		if (plat_oc_set(a->id, next) < 0) { toast("Profil konnte nicht gespeichert werden"); break; }
		cinfo.oc = next;
		toast(next == OC_DEFAULT ? "Takt: Standard · gilt ab dem nächsten Start" :
		      next == OC_FAST ? "Takt: CPU 444 MHz · gilt ab dem nächsten Start" : "Takt: CPU 500 MHz · gilt ab dem nächsten Start");
		break;
	}
	}
}

/* navigation sounds: moves tick, X confirms, O goes back */
static void ui_sound(unsigned b)
{
	if (!S.sound || S.ov == OV_LAUNCH) return;
	if (b == B_X || b == B_START) plat_sound(SND_OK);
	else if (b == B_O) plat_sound(SND_BACK);
	else if (b & (B_UP | B_DOWN | B_LEFT | B_RIGHT | B_L | B_R | B_TRI | B_SQ | B_SELECT)) plat_sound(SND_MOVE);
}

static void press(unsigned b)
{
	S.dirty = 1;
	if (S.ov == OV_LAUNCH) return;
	if (S.ov == OV_KBD) { kbd_press(b); return; }
	if (S.ov == OV_COLL) { coll_press(b); return; }
	if (S.ov == OV_CONFIRM) {
		if (b == B_X) confirm_act();
		else if (b == B_O) S.ov = OV_SET;
		return;
	}
	if (S.ov == OV_SET) {
		if (b == B_UP) S.si = S.si > 0 ? S.si - 1 : 0;
		else if (b == B_DOWN) S.si = S.si < NSET - 1 ? S.si + 1 : NSET - 1;
		else if (b == B_X) settings_act(S.si);
		else if (b == B_LEFT && S.si == 0) { S.set_ret = (S.set_ret + 1) % 3; settings_act(0); }   /* +1 +1 = one step back */
		else if ((b == B_LEFT || b == B_RIGHT) && S.si < 2) settings_act(S.si);
		else if ((b == B_LEFT || b == B_RIGHT) && S.si == 4) theme_step(b == B_LEFT ? -1 : 1);
		else if (b == B_O) { S.ov = OV_MENU; S.mi = 3; }
		else if (b == B_START) S.ov = OV_NONE;
		return;
	}
	if (S.ov == OV_MENU) {
		if (b == B_UP) S.mi = S.mi > 0 ? S.mi - 1 : 0;
		else if (b == B_DOWN) S.mi = S.mi < NMENU - 1 ? S.mi + 1 : NMENU - 1;
		else if (b == B_O || b == B_START) S.ov = OV_NONE;
		else if (b == B_X) menu_act(S.mi);
		return;
	}
	if (S.ov == OV_QA) {
		if (b == B_UP) S.qi = S.qi > 0 ? S.qi - 1 : 0;
		else if (b == B_DOWN) S.qi = S.qi < 2 ? S.qi + 1 : 2;
		else if (b == B_LEFT) qa_adjust(-1);
		else if (b == B_RIGHT || b == B_X) qa_adjust(1);
		else if (b == B_O || b == B_SELECT) S.ov = OV_NONE;
		return;
	}
	if (S.ov == OV_CTX) {
		const char *it[CTX_MAX];
		int act[CTX_MAX];
		int n = ctx_items(&apps[S.ctx_app], it, act);
		if (b == B_UP) S.ci = S.ci > 0 ? S.ci - 1 : 0;
		else if (b == B_DOWN) S.ci = S.ci < n - 1 ? S.ci + 1 : n - 1;
		else if (b == B_O || b == B_TRI) S.ov = OV_NONE;
		else if (b == B_X) ctx_act(S.ci);
		return;
	}
	if (b == B_START) { S.ov = OV_MENU; S.mi = S.view == V_LIB ? 1 : 0; return; }
	if (b == B_SELECT) { S.ov = OV_QA; S.qi = 0; return; }

	if (S.view == V_HOME) {
		if ((b == B_R || b == B_L) && napps) { S.view = V_LIB; return; }
		if (!nrows) return;
		int ai = home_app();
		switch (b) {
		case B_LEFT: if (S.hcol > 0) S.hcol--; break;
		case B_RIGHT: if (S.hcol < rows[S.hrow].n - 1) S.hcol++; break;
		case B_UP: if (S.hrow > 0) { S.hrow--; if (S.hcol >= rows[S.hrow].n) S.hcol = rows[S.hrow].n - 1; } break;
		case B_DOWN: if (S.hrow < nrows - 1) { S.hrow++; if (S.hcol >= rows[S.hrow].n) S.hcol = rows[S.hrow].n - 1; } break;
		case B_X: launch(ai); break;
		case B_SQ: toggle_fav(ai); break;
		case B_TRI: S.ov = OV_CTX; S.ctx_app = ai; S.ci = 0; break;
		case B_R: case B_L: S.view = V_LIB; break;
		}
		return;
	}
	/* library */
	int ai = lib_app();
	switch (b) {
	case B_L: S.tab = (S.tab + ntabs() - 1) % ntabs(); S.lidx = 0; S.ly = 0; S.loff = 0; rebuild(); break;
	case B_R: S.tab = (S.tab + 1) % ntabs(); S.lidx = 0; S.ly = 0; S.loff = 0; rebuild(); break;
	case B_LEFT: if (S.lidx > 0) S.lidx--; break;
	case B_RIGHT: if (S.lidx < nlib - 1) S.lidx++; break;
	case B_UP: if (S.lidx >= LIB_COLS) S.lidx -= LIB_COLS; break;
	case B_DOWN:
		if (nlib && S.lidx / LIB_COLS < (nlib - 1) / LIB_COLS) {
			S.lidx += LIB_COLS;
			if (S.lidx > nlib - 1) S.lidx = nlib - 1;
		}
		break;
	case B_X: launch(ai); break;
	case B_SQ: toggle_fav(ai); break;
	case B_TRI:   /* options, like on the home screen; sorting lives in there */
		if (ai >= 0) { S.ov = OV_CTX; S.ctx_app = ai; S.ci = 0; }
		else lib_toggle_sort(-1);
		break;
	case B_O:   /* an active search is cleared first */
		if (search[0]) { search[0] = 0; S.lidx = 0; rebuild(); toast("Suche beendet"); }
		else S.view = V_HOME;
		break;
	}
}

static void tap(int x, int y)
{
	for (int i = nhits - 1; i >= 0; i--) {
		Hit *h = &hits[i];
		if (x < h->x || y < h->y || x >= h->x + h->w || y >= h->y + h->h) continue;
		S.dirty = 1;
		switch (h->act) {
		case 0: return; /* panel body: swallow */
		case H_BACKDROP:
			if (S.ov == OV_CONFIRM) S.ov = OV_SET;
			else if (S.ov != OV_LAUNCH) S.ov = OV_NONE;
			return;
		case H_SET: settings_act(h->a); return;
		case H_BTN: if (h->a) press(h->a); return;
		case H_HERO: launch(home_app()); return;
		case H_HTILE:
			if (S.hrow == h->a && S.hcol == h->b) launch(home_app());
			else { S.hrow = h->a; S.hcol = h->b; }
			return;
		case H_TAB: S.tab = h->a; S.lidx = 0; S.ly = 0; S.loff = 0; rebuild(); return;
		case H_LTILE:
			if (S.lidx == h->a) launch(lib_app()); else S.lidx = h->a;
			return;
		case H_MENU: menu_act(h->a); return;
		case H_CTX: ctx_act(h->a); return;
		case H_KEY: kbd_key(h->a, h->b); return;
		case H_COLL: coll_act(h->a); return;
		case H_QA: {
			S.qi = h->a;
			if (h->a == 2) { qa_adjust(1); return; }
			int px = SCR_W - 380 + 34;
			int pct = (x - px) * 100 / 312;
			pct = pct < 0 ? 0 : pct > 100 ? 100 : pct;
			if (h->a == 0) { S.bright = pct < 10 ? 10 : (pct + 5) / 10 * 10; S.bright_ok = plat_set_brightness(S.bright) >= 0; save_state(); }
			else if (S.vol_ok) { S.vol = (pct * 30 + 50) / 100; plat_set_volume(S.vol); }
			return;
		}
		}
	}
}

/* ---------- input & frame ---------- */
static unsigned prev_btn;
static uint64_t held_since[12], next_rep[12];
static int touch_was, touch_x0, touch_y0, touch_moved, touch_lx, touch_ly;

static float approach(float v, float t)
{
	float d = t - v;
	if (fabsf(d) < 0.6f) return t;
	return v + d * 0.32f;
}

/* ---------- swipe scrolling ----------
   The axis is locked once the finger has moved past the tap threshold. While
   dragging, the scroll position follows the finger; on release it snaps to whole
   columns / rows, projected a little ahead by the release speed (fling). */
enum { SW_NONE, SW_OFF, SW_HOME_H, SW_HOME_V, SW_LIB_V, SW_LIB_H };
static struct { int axis, row, x0, y0, last; float base, vel; } swipe;

static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

static void swipe_begin(int x, int y)
{
	swipe.axis = SW_NONE; swipe.x0 = x; swipe.y0 = y; swipe.vel = 0; swipe.last = 0;
}

static void swipe_move(int dx, int dy)
{
	if (swipe.axis == SW_NONE) {
		int horiz = abs(dx) > abs(dy), y = swipe.y0;
		swipe.axis = SW_OFF;
		if (S.ov != OV_NONE || y < 44 || y >= SCR_H - 36) return;
		if (S.view == V_HOME && nrows) {
			if (horiz && y >= HOME_TOP && S.hrow < MAXROWS) { swipe.axis = SW_HOME_H; swipe.row = S.hrow; swipe.base = S.sx[S.hrow]; }
			else if (!horiz) { swipe.axis = SW_HOME_V; swipe.base = S.sy; }
		} else if (S.view == V_LIB && y >= 90) {
			if (horiz) swipe.axis = SW_LIB_H;
			else if (nlib) { swipe.axis = SW_LIB_V; swipe.base = S.ly; }
		}
	}
	int d = swipe.axis == SW_HOME_H || swipe.axis == SW_LIB_H ? dx : dy;
	swipe.vel = swipe.vel * 0.5f + (d - swipe.last) * 0.5f;   /* px per frame, smoothed */
	swipe.last = d;
	switch (swipe.axis) {
	case SW_HOME_H: {
		int n = rows[swipe.row].n, mx = n > 6 ? n - 6 : 0;
		S.sx[swipe.row] = clampf(swipe.base - d, -48, mx * (float)HOME_PX + 48);
		S.dirty = 1;
		break;
	}
	case SW_HOME_V:
		S.sy = clampf(swipe.base - d, -48, (nrows - 1) * (float)ROW_H + 48);
		S.dirty = 1;
		break;
	case SW_LIB_V: {
		int lrows = (nlib + LIB_COLS - 1) / LIB_COLS, mx = lrows > 2 ? lrows - 2 : 0;
		S.ly = clampf(swipe.base - d, -48, mx * (float)LIB_PY + 48);
		S.dirty = 1;
		break;
	}
	}
}

static void swipe_end(int dx, int dy)
{
	(void)dy;
	float fling = -swipe.vel * 7;   /* where a flick would carry the content */
	switch (swipe.axis) {
	case SW_HOME_H: {
		int r = swipe.row, n = rows[r].n, mx = n > 6 ? n - 6 : 0;
		int k = (int)floorf((S.sx[r] + fling) / HOME_PX + 0.5f);
		k = k < 0 ? 0 : k > mx ? mx : k;
		S.hoff[r] = k;
		if (r == S.hrow) {   /* pull the focus into the new window */
			if (S.hcol < k) S.hcol = k;
			if (S.hcol > k + 5) S.hcol = k + 5;
			if (S.hcol > n - 1) S.hcol = n - 1;
		}
		break;
	}
	case SW_HOME_V: {
		int k = (int)floorf((S.sy + fling) / ROW_H + 0.5f);
		/* a deliberate swipe always moves at least one row */
		if (k == S.hrow && fabsf(S.sy - S.hrow * (float)ROW_H) > ROW_H / 4) k += S.sy > S.hrow * ROW_H ? 1 : -1;
		k = k < 0 ? 0 : k > nrows - 1 ? nrows - 1 : k;
		S.hrow = k;
		if (S.hcol >= rows[k].n) S.hcol = rows[k].n - 1;
		if (k < MAXROWS) {   /* keep the column if it is visible in that row, else take the nearest visible one */
			if (S.hcol < S.hoff[k]) S.hcol = S.hoff[k];
			if (S.hcol > S.hoff[k] + 5) S.hcol = S.hoff[k] + 5;
			if (S.hcol > rows[k].n - 1) S.hcol = rows[k].n - 1;
		}
		break;
	}
	case SW_LIB_V: {
		int lrows = (nlib + LIB_COLS - 1) / LIB_COLS, mx = lrows > 2 ? lrows - 2 : 0;
		int k = (int)floorf((S.ly + fling) / LIB_PY + 0.5f);
		k = k < 0 ? 0 : k > mx ? mx : k;
		S.loff = k;
		int r = S.lidx / LIB_COLS, c = S.lidx % LIB_COLS;
		if (r < k) r = k;
		if (r > k + 1) r = k + 1;
		S.lidx = r * LIB_COLS + c;
		if (S.lidx > nlib - 1) S.lidx = nlib - 1;
		break;
	}
	case SW_LIB_H:
		if (abs(dx) > 100) {
			int n = ntabs();
			S.tab = (S.tab + (dx < 0 ? 1 : n - 1)) % n;
			S.lidx = 0; S.ly = 0; S.loff = 0;
			rebuild();
		}
		break;
	}
	swipe.axis = SW_NONE;
	S.dirty = 1;
}

void ui_init(void)
{
	memset(&S, 0, sizeof S);
	S.view = V_BOOT;
	S.bright = 70;
	S.sound = 1;
	S.bright_ok = 1;
	int v = plat_get_volume();
	S.vol_ok = v >= 0;
	S.vol = v < 0 ? 0 : v;
	S.dirty = 1;
	S.last_minute = -1;
	load_state();   /* no apps yet: only picks up brightness and the colour scheme for the boot screen */
	unsigned char *d; int len;
	if (plat_read_file(plat_res("Lato-Regular.ttf"), &d, &len) >= 0) font_load(FONT_REG, d);
	if (plat_read_file(plat_res("Lato-Bold.ttf"), &d, &len) >= 0) font_load(FONT_BOLD, d);
	if (plat_read_file(plat_res("Lexend-Regular.ttf"), &d, &len) >= 0) font_load(FONT_WM, d);
	if (plat_read_file(plat_res("Lexend-Bold.ttf"), &d, &len) >= 0) font_load(FONT_WM_BOLD, d);
	/* font data stays allocated for the app's lifetime */
}

int ui_frame(const Input *in, u32 *buf)
{
	gfx_init(buf);
	uint64_t now = plat_ms();

	if (S.view == V_BOOT) {
		static uint64_t boot_t0;
		if (S.boot == 0) { draw_boot(); S.boot = 1; boot_t0 = now; return 1; }
		if (S.boot == 1) { ui_scan(); S.boot = 2; }
		if (now - boot_t0 < 900) return 0;   /* keep the splash up briefly */
		S.view = V_HOME;
		S.dirty = 1;
	}

	/* launch: show the overlay for a couple of frames, then hand over */
	if (S.ov == OV_LAUNCH) {
		int err = 0;
		if (++S.launch_frames == 3) err = plat_launch(apps[S.launch_app].id);
		if (err < 0 || S.launch_frames > 90) {
			if (err < 0) {
				/* rejected start must not count as "recently launched" */
				apps[S.launch_app].last = S.launch_prev_last;
				save_state();
			}
			S.ov = OV_NONE;
			S.dirty = 1;
			rebuild();
			char m[64];
			if (err < 0) snprintf(m, sizeof m, "Start fehlgeschlagen (0x%08X)", (unsigned)err);
			else snprintf(m, sizeof m, "Start fehlgeschlagen");
			toast(m);
		}
	}

	if (lists_dirty) rebuild();

	/* buttons + stick → directional presses with repeat */
	unsigned b = in->buttons;
	if (in->lx < -64) b |= B_LEFT;
	if (in->lx > 64) b |= B_RIGHT;
	if (in->ly < -64) b |= B_UP;
	if (in->ly > 64) b |= B_DOWN;
	for (int i = 0; i < 12; i++) {
		unsigned m = 1u << i;
		if ((b & m) && !(prev_btn & m)) { held_since[i] = now; next_rep[i] = now + 320; ui_sound(m); press(m); }
		else if ((b & m) && i < 4 && now >= next_rep[i]) { next_rep[i] = now + 90; ui_sound(m); press(m); }
	}
	prev_btn = b;

	/* touch: tap = down/up with little movement; a drag scrolls (swipe) */
	if (in->touch) {
		if (!touch_was) { touch_x0 = in->tx; touch_y0 = in->ty; touch_moved = 0; swipe_begin(in->tx, in->ty); }
		if (abs(in->tx - touch_x0) > 24 || abs(in->ty - touch_y0) > 24) touch_moved = 1;
		touch_lx = in->tx; touch_ly = in->ty;
		if (touch_moved) swipe_move(in->tx - touch_x0, in->ty - touch_y0);
	} else if (touch_was) {
		if (!touch_moved) tap(touch_lx, touch_ly);
		else swipe_end(touch_lx - touch_x0, touch_ly - touch_y0);
	}
	touch_was = in->touch;
	if (b || in->touch) last_input = now;

	/* scroll targets: the focus stays inside the visible window (6 columns /
	   2 library rows); a swipe moves the window and pulls the focus along */
	int lrows = (nlib + LIB_COLS - 1) / LIB_COLS, lmax = lrows > 2 ? lrows - 2 : 0;
	if (S.loff > lmax) S.loff = lmax;
	if (nlib) {
		int r = S.lidx / LIB_COLS;
		if (r < S.loff) S.loff = r;
		if (r > S.loff + 1) S.loff = r - 1;
	}
	if (swipe.axis != SW_HOME_V) {
		float nsy = approach(S.sy, S.hrow * (float)ROW_H);
		if (nsy != S.sy) S.dirty = 1;
		S.sy = nsy;
	}
	if (swipe.axis != SW_LIB_V) {
		float nly = approach(S.ly, S.loff * (float)LIB_PY);
		if (nly != S.ly) S.dirty = 1;
		S.ly = nly;
	}
	for (int r = 0; r < nrows && r < MAXROWS; r++) {
		int mx = rows[r].n > 6 ? rows[r].n - 6 : 0;
		if (S.hoff[r] > mx) S.hoff[r] = mx;
		if (r == S.hrow) {
			if (S.hcol < S.hoff[r]) S.hoff[r] = S.hcol;
			if (S.hcol > S.hoff[r] + 5) S.hoff[r] = S.hcol - 5;
		}
		if (swipe.axis == SW_HOME_H && r == swipe.row) continue;
		float n = approach(S.sx[r], S.hoff[r] * (float)HOME_PX);
		if (n != S.sx[r]) S.dirty = 1;
		S.sx[r] = n;
	}

	if (S.toast[0] && now > S.toast_until) { S.toast[0] = 0; S.dirty = 1; }

	Clock c; plat_clock(&c);
	if (c.minute != S.last_minute) { S.last_minute = c.minute; S.dirty = 1; under_ok = 0; }
	static uint64_t wifi_next;
	if (now >= wifi_next) {
		wifi_next = now + 2000;
		int w = wifi_bars(plat_wifi());
		if (w != wifi_level) { wifi_level = w; S.dirty = 1; under_ok = 0; }
	}

	pump_icons();
	pump_cinfo();
	pump_bg(now);
	home_bg_pump(now);
	if (cov.state == 1 && (S.ov == OV_SET || S.ov == OV_CONFIRM)) {   /* live progress */
		static int shown = -1;
		if (shown != cov.done) { shown = cov.done; S.dirty = 1; }
	}
	if (cov.state == 2) {   /* cover download finished on the job thread */
		__sync_synchronize();
		cov.state = 3;
		toast(cov.msg);
		if (cov.got) art_reload_all();   /* rebuild tiles/hero from the new files */
		cov_have_now = cov.have + cov.got;
		S.dirty = 1;
	}

	if (!S.dirty) return 0;
	S.dirty = 0;
	draw();
	return 1;
}
