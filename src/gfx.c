/* Software renderer: anti-aliased primitives, image blits and
   stb_truetype text with a glyph cache. */
#include "gfx.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "stb_truetype.h"

static u32 *fb;
static int fbw = SCR_W, fbh = SCR_H;   /* current target size (= stride) */
static int cx0 = 0, cy0 = 0, cx1 = SCR_W, cy1 = SCR_H;

void gfx_init(u32 *buffer) { fb = buffer; fbw = SCR_W; fbh = SCR_H; }
u32 *gfx_buffer(void) { return fb; }

void gfx_clip(int x, int y, int w, int h)
{
	cx0 = x < 0 ? 0 : x;
	cy0 = y < 0 ? 0 : y;
	cx1 = x + w > fbw ? fbw : x + w;
	cy1 = y + h > fbh ? fbh : y + h;
}

void gfx_noclip(void) { cx0 = 0; cy0 = 0; cx1 = fbw; cy1 = fbh; }

static int cstack[8][4], csn = 0;

/* offscreen rendering (e.g. caching cover art); not nestable */
static struct { u32 *fb; int w, h, c[4]; } saved;
void gfx_begin_target(u32 *buf, int w, int h)
{
	saved.fb = fb; saved.w = fbw; saved.h = fbh;
	saved.c[0] = cx0; saved.c[1] = cy0; saved.c[2] = cx1; saved.c[3] = cy1;
	fb = buf; fbw = w; fbh = h;
	gfx_noclip();
}
void gfx_end_target(void)
{
	fb = saved.fb; fbw = saved.w; fbh = saved.h;
	cx0 = saved.c[0]; cy0 = saved.c[1]; cx1 = saved.c[2]; cy1 = saved.c[3];
}

/* clip an integer bounding box [x0,x1]x[y0,y1] (inclusive) */
static inline int clip_box(int *x0, int *y0, int *x1, int *y1)
{
	if (*x0 < cx0) *x0 = cx0;
	if (*y0 < cy0) *y0 = cy0;
	if (*x1 > cx1 - 1) *x1 = cx1 - 1;
	if (*y1 > cy1 - 1) *y1 = cy1 - 1;
	return *x0 <= *x1 && *y0 <= *y1;
}

void gfx_push_clip(int x, int y, int w, int h)
{
	if (csn < 8) { cstack[csn][0] = cx0; cstack[csn][1] = cy0; cstack[csn][2] = cx1; cstack[csn][3] = cy1; csn++; }
	int nx0 = x > cx0 ? x : cx0, ny0 = y > cy0 ? y : cy0;
	int nx1 = x + w < cx1 ? x + w : cx1, ny1 = y + h < cy1 ? y + h : cy1;
	if (nx1 < nx0) nx1 = nx0;
	if (ny1 < ny0) ny1 = ny0;
	cx0 = nx0; cy0 = ny0; cx1 = nx1; cy1 = ny1;
}

void gfx_pop_clip(void)
{
	if (csn > 0) { csn--; cx0 = cstack[csn][0]; cy0 = cstack[csn][1]; cx1 = cstack[csn][2]; cy1 = cstack[csn][3]; }
}

static inline u32 blend(u32 d, u32 s, int a)
{
	if (a >= 255) return s | 0xFF000000u;
	if (a <= 0) return d;
	u32 rb = (((s & 0x00FF00FFu) * a) + ((d & 0x00FF00FFu) * (255 - a))) >> 8;
	u32 g = (((s & 0x0000FF00u) * a) + ((d & 0x0000FF00u) * (255 - a))) >> 8;
	return 0xFF000000u | (rb & 0x00FF00FFu) | (g & 0x0000FF00u);
}

static inline void plot(int x, int y, u32 c, int a)
{
	if (x < cx0 || y < cy0 || x >= cx1 || y >= cy1 || a <= 0) return;
	u32 *p = fb + y * fbw + x;
	*p = blend(*p, c, a);
}

/* plot without clip test (caller clipped already) */
static inline void plot_nc(int x, int y, u32 c, int a)
{
	if (a <= 0) return;
	u32 *p = fb + y * fbw + x;
	*p = blend(*p, c, a);
}

void gfx_clear(u32 c)
{
	int n = fbw * fbh;
	for (int i = 0; i < n; i++) fb[i] = c;
}

void gfx_rect(int x, int y, int w, int h, u32 c, int a)
{
	int x0 = x < cx0 ? cx0 : x, y0 = y < cy0 ? cy0 : y;
	int x1 = x + w > cx1 ? cx1 : x + w, y1 = y + h > cy1 ? cy1 : y + h;
	if (x0 >= x1 || y0 >= y1 || a <= 0) return;
	for (int j = y0; j < y1; j++) {
		u32 *p = fb + j * fbw + x0;
		if (a >= 255) for (int i = x0; i < x1; i++) *p++ = c;
		else for (int i = x0; i < x1; i++, p++) *p = blend(*p, c, a);
	}
}

static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

/* coverage of a rounded rect at pixel (px,py) for corner rows */
void gfx_rrect(int x, int y, int w, int h, int r, u32 c, int a)
{
	if (r <= 0) { gfx_rect(x, y, w, h, c, a); return; }
	if (r * 2 > w) r = w / 2;
	if (r * 2 > h) r = h / 2;
	gfx_rect(x, y + r, w, h - 2 * r, c, a);
	for (int j = 0; j < r; j++) {
		for (int pass = 0; pass < 2; pass++) {
			int yy = pass ? (y + h - 1 - j) : (y + j);
			if (yy < cy0 || yy >= cy1) continue;
			float fy = (float)(r - j) - 0.5f;
			/* straight middle part */
			gfx_rect(x + r, yy, w - 2 * r, 1, c, a);
			for (int i = 0; i < r; i++) {
				float fx = (float)(r - i) - 0.5f;
				float d = sqrtf(fx * fx + fy * fy);
				float cov = clampf((float)r - d + 0.5f, 0.f, 1.f);
				int aa = (int)(cov * a);
				plot(x + i, yy, c, aa);
				plot(x + w - 1 - i, yy, c, aa);
			}
		}
	}
}

static inline u32 lerpc(u32 c0, u32 c1, int t)
{
	return blend(c0, c1, t);
}

void gfx_hgrad(int x, int y, int w, int h, u32 c0, u32 c1)
{
	if (w <= 0 || h <= 0) return;
	int x0 = x, y0 = y, x1 = x + w - 1, y1 = y + h - 1;
	if (!clip_box(&x0, &y0, &x1, &y1)) return;
	static u32 row[1024];
	int n = x1 - x0 + 1;
	if (n > 1024) n = 1024;
	for (int i = 0; i < n; i++) row[i] = lerpc(c0, c1, (x0 + i - x) * 255 / w);
	/* row-major: one memcpy per line instead of column-wise writes */
	for (int j = y0; j <= y1; j++) memcpy(fb + j * fbw + x0, row, (size_t)n * 4);
}

void gfx_circle(float cx, float cy, float r, u32 c, int a)
{
	int x0 = (int)floorf(cx - r - 1), x1 = (int)ceilf(cx + r + 1);
	int y0 = (int)floorf(cy - r - 1), y1 = (int)ceilf(cy + r + 1);
	if (!clip_box(&x0, &y0, &x1, &y1)) return;
	float ri = r - 0.5f, ro = r + 0.5f;
	float ri2 = ri > 0 ? ri * ri : 0, ro2 = ro * ro;
	for (int j = y0; j <= y1; j++) {
		float dy = j + 0.5f - cy, dy2 = dy * dy;
		for (int i = x0; i <= x1; i++) {
			float dx = i + 0.5f - cx, d2 = dx * dx + dy2;
			if (d2 >= ro2) continue;                       /* outside */
			if (d2 <= ri2) { plot_nc(i, j, c, a); continue; } /* fully inside: no sqrt */
			float cov = clampf(r - sqrtf(d2) + 0.5f, 0.f, 1.f);
			plot_nc(i, j, c, (int)(cov * a));
		}
	}
}

void gfx_ring(float cx, float cy, float r, float th, u32 c, int a)
{
	float ro = r + th * 0.5f;
	int x0 = (int)floorf(cx - ro - 1), x1 = (int)ceilf(cx + ro + 1);
	int y0 = (int)floorf(cy - ro - 1), y1 = (int)ceilf(cy + ro + 1);
	if (!clip_box(&x0, &y0, &x1, &y1)) return;
	float lim = ro + 0.5f, lim2 = lim * lim;
	float inn = r - th * 0.5f - 0.5f, inn2 = inn > 0 ? inn * inn : -1;
	for (int j = y0; j <= y1; j++)
		for (int i = x0; i <= x1; i++) {
			float dx = i + 0.5f - cx, dy = j + 0.5f - cy, d2 = dx * dx + dy * dy;
			if (d2 >= lim2 || d2 <= inn2) continue;
			float d = fabsf(sqrtf(d2) - r);
			float cov = clampf(th * 0.5f - d + 0.5f, 0.f, 1.f);
			if (cov > 0) plot_nc(i, j, c, (int)(cov * a));
		}
}

void gfx_line(float x0, float y0, float x1, float y1, float th, u32 c, int a)
{
	float hw = th * 0.5f;
	int bx0 = (int)floorf(fminf(x0, x1) - hw - 1), bx1 = (int)ceilf(fmaxf(x0, x1) + hw + 1);
	int by0 = (int)floorf(fminf(y0, y1) - hw - 1), by1 = (int)ceilf(fmaxf(y0, y1) + hw + 1);
	if (!clip_box(&bx0, &by0, &bx1, &by1)) return;
	float vx = x1 - x0, vy = y1 - y0, l2 = vx * vx + vy * vy;
	float il2 = l2 > 0 ? 1.f / l2 : 0.f;
	float lim = hw + 0.5f, lim2 = lim * lim;
	for (int j = by0; j <= by1; j++)
		for (int i = bx0; i <= bx1; i++) {
			float px = i + 0.5f - x0, py = j + 0.5f - y0;
			float t = clampf((px * vx + py * vy) * il2, 0.f, 1.f);
			float dx = px - t * vx, dy = py - t * vy, d2 = dx * dx + dy * dy;
			if (d2 >= lim2) continue;
			float cov = clampf(hw - sqrtf(d2) + 0.5f, 0.f, 1.f);
			if (cov > 0) plot_nc(i, j, c, (int)(cov * a));
		}
}

void gfx_tri(float ax, float ay, float bx, float by, float cx, float cy, u32 c, int a)
{
	/* ensure consistent winding */
	float area = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
	if (area > 0) { float tx = bx, ty = by; bx = cx; by = cy; cx = tx; cy = ty; }
	int x0 = (int)floorf(fminf(ax, fminf(bx, cx))) - 1, x1 = (int)ceilf(fmaxf(ax, fmaxf(bx, cx))) + 1;
	int y0 = (int)floorf(fminf(ay, fminf(by, cy))) - 1, y1 = (int)ceilf(fmaxf(ay, fmaxf(by, cy))) + 1;
	if (!clip_box(&x0, &y0, &x1, &y1)) return;
	/* signed edge distance d = px*A + py*B + C, normalised once per edge */
	float ex[3] = { bx - ax, cx - bx, ax - cx }, ey[3] = { by - ay, cy - by, ay - cy };
	float ox[3] = { ax, bx, cx }, oy[3] = { ay, by, cy };
	float A[3], B[3], C[3];
	for (int k = 0; k < 3; k++) {
		float len = sqrtf(ex[k] * ex[k] + ey[k] * ey[k]);
		float il = len > 0 ? 1.f / len : 0.f;
		A[k] = ey[k] * il; B[k] = -ex[k] * il;
		C[k] = -(ox[k] * A[k] + oy[k] * B[k]);
	}
	for (int j = y0; j <= y1; j++) {
		float py = j + 0.5f;
		for (int i = x0; i <= x1; i++) {
			float px = i + 0.5f;
			float d = fminf(px * A[0] + py * B[0] + C[0],
			          fminf(px * A[1] + py * B[1] + C[1], px * A[2] + py * B[2] + C[2]));
			if (d <= -0.5f) continue;
			if (d >= 0.5f) { plot_nc(i, j, c, a); continue; }
			plot_nc(i, j, c, (int)((d + 0.5f) * a));
		}
	}
}

void gfx_img(const Img *im, int x, int y, int r, int a)
{
	if (!im) return;
	if (r * 2 > im->w) r = im->w / 2;
	if (r * 2 > im->h) r = im->h / 2;
	if (r > 64) r = 64;
	if (r < 0) r = 0;
	int i0 = cx0 - x > 0 ? cx0 - x : 0, i1 = cx1 - x < im->w ? cx1 - x : im->w;
	int j0 = cy0 - y > 0 ? cy0 - y : 0, j1 = cy1 - y < im->h ? cy1 - y : im->h;
	if (i0 >= i1 || j0 >= j1) return;
	int cov[64];
	for (int j = j0; j < j1; j++) {
		const u32 *s = im->px + j * im->w;
		u32 *d = fb + (y + j) * fbw + x;
		int corner = r > 0 && (j < r || j >= im->h - r);
		if (!corner && im->opaque && a >= 255) {
			/* opaque row outside the rounded corners: plain copy */
			memcpy(d + i0, s + i0, (size_t)(i1 - i0) * 4);
			continue;
		}
		if (corner) {
			/* coverage of the rounded corner for this row, computed once */
			float fy = j < r ? (float)(r - j) - 0.5f : (float)(j - (im->h - r)) + 0.5f;
			for (int k = 0; k < r; k++) {
				float fx = (float)(r - k) - 0.5f;
				cov[k] = (int)(clampf((float)r - sqrtf(fx * fx + fy * fy) + 0.5f, 0.f, 1.f) * 256.f);
			}
		}
		for (int i = i0; i < i1; i++) {
			u32 p = s[i];
			int pa = (int)(p >> 24);
			if (corner) {
				int k = i < r ? i : (i >= im->w - r ? im->w - 1 - i : -1);
				if (k >= 0) pa = pa * cov[k] >> 8;
			}
			if (a < 255) pa = pa * a / 255;
			if (pa >= 255) d[i] = p | 0xFF000000u;
			else if (pa > 0) d[i] = blend(d[i], p, pa);
		}
	}
}

Img *img_from_rgba(const unsigned char *rgba, int w, int h)
{
	Img *im = malloc(sizeof(Img));
	if (!im) return NULL;
	im->w = w; im->h = h;
	im->px = malloc((size_t)w * h * 4);
	if (!im->px) { free(im); return NULL; }
	memcpy(im->px, rgba, (size_t)w * h * 4);
	im->opaque = 1;
	for (int i = 0; i < w * h; i++) if ((im->px[i] >> 24) != 255) { im->opaque = 0; break; }
	unsigned long sr = 0, sg = 0, sb = 0, n = 0;
	for (int i = 0; i < w * h; i += 7) {
		u32 p = im->px[i];
		if ((p >> 24) < 128) continue;
		sr += p & 0xFF; sg += (p >> 8) & 0xFF; sb += (p >> 16) & 0xFF; n++;
	}
	if (!n) n = 1;
	im->avg = 0xFF000000u | ((sb / n) << 16) | ((sg / n) << 8) | (sr / n);
	return im;
}

/* area-averaging downscale / bilinear upscale */
Img *img_scaled(const Img *src, int w, int h)
{
	if (!src) return NULL;
	Img *im = malloc(sizeof(Img));
	if (!im) return NULL;
	im->w = w; im->h = h; im->avg = src->avg;
	im->px = malloc((size_t)w * h * 4);
	if (!im->px) { free(im); return NULL; }
	for (int j = 0; j < h; j++) {
		float sy0 = (float)j * src->h / h, sy1 = (float)(j + 1) * src->h / h;
		for (int i = 0; i < w; i++) {
			float sx0 = (float)i * src->w / w, sx1 = (float)(i + 1) * src->w / w;
			float acc[4] = {0, 0, 0, 0}, wsum = 0;
			int iy0 = (int)sy0, iy1 = (int)ceilf(sy1), ix0 = (int)sx0, ix1 = (int)ceilf(sx1);
			if (iy1 <= iy0) iy1 = iy0 + 1;
			if (ix1 <= ix0) ix1 = ix0 + 1;
			for (int y = iy0; y < iy1 && y < src->h; y++)
				for (int x = ix0; x < ix1 && x < src->w; x++) {
					u32 p = src->px[y * src->w + x];
					float wa = (float)(p >> 24) / 255.f;
					acc[0] += (p & 0xFF) * wa; acc[1] += ((p >> 8) & 0xFF) * wa;
					acc[2] += ((p >> 16) & 0xFF) * wa; acc[3] += wa; wsum += 1;
				}
			u32 out = 0;
			if (acc[3] > 0) {
				u32 r = (u32)(acc[0] / acc[3]), g = (u32)(acc[1] / acc[3]), b = (u32)(acc[2] / acc[3]);
				u32 al = (u32)(acc[3] / wsum * 255.f);
				out = (al << 24) | (b << 16) | (g << 8) | r;
			}
			im->px[j * w + i] = out;
		}
	}
	im->opaque = 1;
	for (int i = 0; i < w * h; i++) if ((im->px[i] >> 24) != 255) { im->opaque = 0; break; }
	return im;
}

void img_free(Img *im)
{
	if (!im) return;
	free(im->px);
	free(im);
}

/* ---------------- text ---------------- */

typedef struct Glyph {
	int cp;
	int w, h, xoff, yoff, adv, idx;
	unsigned char *bm;
	struct Glyph *next;
} Glyph;

typedef struct {
	int size;
	float scale;
	int ascent, lineh;
	Glyph *lo[256];
	Glyph *hi;
} GlyphSet;

#define MAX_SETS 24
static stbtt_fontinfo fonts[4];
static int font_ok[4];
static GlyphSet sets[4][MAX_SETS];
static int nsets[4];

int font_load(int id, const unsigned char *ttf)
{
	font_ok[id] = stbtt_InitFont(&fonts[id], ttf, stbtt_GetFontOffsetForIndex(ttf, 0));
	return font_ok[id];
}

static GlyphSet *get_set(int f, int size)
{
	for (int i = 0; i < nsets[f]; i++)
		if (sets[f][i].size == size) return &sets[f][i];
	if (nsets[f] >= MAX_SETS) return &sets[f][0];
	GlyphSet *gs = &sets[f][nsets[f]++];
	memset(gs, 0, sizeof(*gs));
	gs->size = size;
	gs->scale = stbtt_ScaleForPixelHeight(&fonts[f], (float)size);
	int asc, desc, gap;
	stbtt_GetFontVMetrics(&fonts[f], &asc, &desc, &gap);
	gs->ascent = (int)roundf(asc * gs->scale);
	gs->lineh = (int)roundf((asc - desc + gap) * gs->scale);
	return gs;
}

static Glyph *get_glyph(int f, GlyphSet *gs, int cp)
{
	Glyph *g = NULL;
	if (cp < 256) g = gs->lo[cp];
	else for (g = gs->hi; g && g->cp != cp; g = g->next) {}
	if (g) return g;
	g = calloc(1, sizeof(Glyph));
	if (!g) return NULL;
	g->cp = cp;
	g->idx = stbtt_FindGlyphIndex(&fonts[f], cp);
	int adv, lsb;
	stbtt_GetGlyphHMetrics(&fonts[f], g->idx, &adv, &lsb);
	g->adv = (int)roundf(adv * gs->scale);
	g->bm = stbtt_GetGlyphBitmap(&fonts[f], gs->scale, gs->scale, g->idx, &g->w, &g->h, &g->xoff, &g->yoff);
	if (cp < 256) gs->lo[cp] = g;
	else { g->next = gs->hi; gs->hi = g; }
	return g;
}

static int utf8_next(const char **ps)
{
	const unsigned char *s = (const unsigned char *)*ps;
	int c = *s;
	if (!c) return 0;
	if (c < 0x80) { *ps += 1; return c; }
	if ((c & 0xE0) == 0xC0 && s[1]) { *ps += 2; return ((c & 0x1F) << 6) | (s[1] & 0x3F); }
	if ((c & 0xF0) == 0xE0 && s[1] && s[2]) { *ps += 3; return ((c & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F); }
	if ((c & 0xF8) == 0xF0 && s[1] && s[2] && s[3]) { *ps += 4; return ((c & 0x07) << 18) | ((s[1] & 0x3F) << 12) | ((s[2] & 0x3F) << 6) | (s[3] & 0x3F); }
	*ps += 1;
	return '?';
}

/* kerning cache: stbtt_GetGlyphKernAdvance walks the GPOS tables on every
   call, which is far too slow to do for every glyph pair in every frame */
#define KC_BITS 11
static struct { unsigned key; int v; } kcache[4][1 << KC_BITS];

static int kern_units(int f, int g1, int g2)
{
	unsigned key = (((unsigned)g1 << 16) | ((unsigned)g2 & 0xFFFF)) + 1u;
	unsigned h = (key * 2654435761u) >> (32 - KC_BITS);
	if (kcache[f][h].key == key) return kcache[f][h].v;
	int v = stbtt_GetGlyphKernAdvance(&fonts[f], g1, g2);
	kcache[f][h].key = key;
	kcache[f][h].v = v;
	return v;
}

int text_lineh(int f, int size)
{
	if (!font_ok[f]) return size;
	return get_set(f, size)->lineh;
}

static int text_run(int f, int size, int x, int y, const char *s, u32 c, int a, int draw, int maxw)
{
	if (!font_ok[f] || !s) return 0;
	GlyphSet *gs = get_set(f, size);
	int pen = 0, prev = -1;
	while (*s) {
		int cp = utf8_next(&s);
		if (cp == '\n') cp = ' ';
		Glyph *g = get_glyph(f, gs, cp);
		if (!g) continue;
		if (prev >= 0)
		{
			int k = kern_units(f, prev, g->idx);
			if (k) pen += (int)roundf(k * gs->scale);
		}
		if (maxw >= 0 && pen + g->adv > maxw) break;
		if (draw && g->bm) {
			int gx = x + pen + g->xoff, gy = y + gs->ascent + g->yoff;
			int i0 = cx0 - gx > 0 ? cx0 - gx : 0, i1 = cx1 - gx < g->w ? cx1 - gx : g->w;
			int j0 = cy0 - gy > 0 ? cy0 - gy : 0, j1 = cy1 - gy < g->h ? cy1 - gy : g->h;
			for (int j = j0; j < j1; j++) {
				const unsigned char *b = g->bm + j * g->w;
				u32 *d = fb + (gy + j) * fbw + gx;
				for (int i = i0; i < i1; i++) {
					int v = b[i];
					if (!v) continue;
					if (a < 255) v = v * a / 255;
					d[i] = blend(d[i], c, v);
				}
			}
		}
		pen += g->adv;
		prev = g->idx;
	}
	return pen;
}

int text_w(int f, int size, const char *s) { return text_run(f, size, 0, 0, s, 0, 0, 0, -1); }

int text(int f, int size, int x, int y, const char *s, u32 c, int a)
{
	return text_run(f, size, x, y, s, c, a, 1, -1);
}

/* draws text, truncated with an ellipsis if wider than maxw */
int text_fit(int f, int size, int x, int y, int maxw, const char *s, u32 c, int a)
{
	int w = text_w(f, size, s);
	if (w <= maxw) return text(f, size, x, y, s, c, a);
	int ew = text_w(f, size, "…");
	int drawn = text_run(f, size, x, y, s, c, a, 1, maxw - ew);
	text(f, size, x + drawn, y, "…", c, a);
	return drawn + ew;
}
