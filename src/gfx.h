#ifndef VL_GFX_H
#define VL_GFX_H
#include <stdint.h>

#define SCR_W 960
#define SCR_H 544

/* Pixel format: 0xAABBGGRR (= RGBA bytes in memory, matches the Vita
   A8B8G8R8 framebuffer format). Colors in code are written as 0xRRGGBB. */
typedef uint32_t u32;

static inline u32 rgb(u32 hex)
{
	return 0xFF000000u | ((hex & 0xFF) << 16) | (hex & 0xFF00) | ((hex >> 16) & 0xFF);
}

typedef struct {
	int w, h;
	u32 *px;
	u32 avg; /* average colour (for tinting) */
	int opaque; /* all pixels alpha 255 -> rows can be copied directly */
} Img;

enum { FONT_REG = 0, FONT_BOLD = 1, FONT_WM = 2, FONT_WM_BOLD = 3 };

void gfx_init(u32 *buffer);
u32 *gfx_buffer(void);
void gfx_begin_target(u32 *buf, int w, int h);   /* render into an offscreen w*h buffer */
void gfx_end_target(void);
void gfx_clip(int x, int y, int w, int h);
void gfx_noclip(void);
void gfx_push_clip(int x, int y, int w, int h);
void gfx_pop_clip(void);
void gfx_clear(u32 c);
void gfx_rect(int x, int y, int w, int h, u32 c, int a);
void gfx_rrect(int x, int y, int w, int h, int r, u32 c, int a);
void gfx_hgrad(int x, int y, int w, int h, u32 c0, u32 c1);
void gfx_circle(float cx, float cy, float r, u32 c, int a);
void gfx_ring(float cx, float cy, float r, float th, u32 c, int a);
void gfx_line(float x0, float y0, float x1, float y1, float th, u32 c, int a);
void gfx_tri(float ax, float ay, float bx, float by, float cx, float cy, u32 c, int a);
void gfx_img(const Img *im, int x, int y, int radius, int a);

Img *img_from_rgba(const unsigned char *rgba, int w, int h);
Img *img_scaled(const Img *src, int w, int h);
void img_free(Img *im);

int font_load(int id, const unsigned char *ttf);
int text_w(int font, int size, const char *s);
int text(int font, int size, int x, int y, const char *s, u32 c, int a);
int text_fit(int font, int size, int x, int y, int maxw, const char *s, u32 c, int a);
int text_lineh(int font, int size);

#endif
