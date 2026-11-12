#pragma once
// Shared constants, globals and low-level helpers (same framework as the koi pond).

#include <Arduino.h>
#include <math.h>
#include <esp_heap_caps.h>
#include "display_config.h"

// ─── Screen / world ───────────────────────────────────────────────────────────
#define SCREEN_W   800
#define SCREEN_H   480
#define SCREEN_CX  400
#define SCREEN_CY  240

// The track "world" is twice the screen in each direction. At zoom 0.5 the whole
// world fits the screen; at zoom 1.0 one world pixel = one screen pixel.
#define WORLD_W    1600
#define WORLD_H    960
#define HALF_W     (WORLD_W / 2)
#define HALF_H     (WORLD_H / 2)

#define ZOOM_MIN   0.5f
#define ZOOM_MAX   2.5f

#define FRAME_MS   33          // ~30 fps target

static LGFX         display;

// ─── Strip rendering ──────────────────────────────────────────────────────────
// PSRAM bandwidth is the bottleneck on this board (the RGB panel scans its PSRAM
// framebuffer continuously). So each frame is composed in horizontal strips in
// fast internal SRAM and every finished strip is written once to the panel.
//
// `canvas` is a full-screen-sized LGFX_Sprite whose buffer pointer is offset so
// that screen row y lands in the strip buffer; its clip rect is limited to the
// current strip. All drawing code therefore uses plain screen coordinates.
// Two strip buffers: the CPU draws into one while GDMA copies the other out.
#define STRIP_H    24                     // 480 / 24 = 20 strips; 38400 B each
static LGFX_Sprite  canvas(&display);
static uint16_t*    stripBufs[2] = { nullptr, nullptr };   // internal SRAM, DMA-capable
static uint16_t*    stripBuf = nullptr;   // the one being drawn
static uint16_t*    fb = nullptr;         // stripBuf offset so fb[y*W+x] is screen (x,y)
static int          clipY0 = 0, clipY1 = SCREEN_H;   // current strip rows [clipY0, clipY1)
static int          stripH = STRIP_H;   // rows per strip this frame (≤ STRIP_H; smaller when zoomed in)

static inline bool stripHit(int y0, int y1) { return y1 >= clipY0 && y0 < clipY1; }

static void beginStrip(int y, int buf = 0, int h = -1) {
  if (h < 0) h = stripH;
  stripBuf = stripBufs[buf];
  clipY0 = y; clipY1 = (y + h < SCREEN_H) ? y + h : SCREEN_H;
  fb = stripBuf - (intptr_t)y * SCREEN_W;
  canvas.setBuffer(fb, SCREEN_W, SCREEN_H);   // keeps the rgb565_nonswapped depth set at boot
  canvas.setClipRect(0, y, SCREEN_W, clipY1 - clipY0);
}

static uint32_t frameCount = 0;
static float    simTime    = 0.0f;     // seconds of simulated time

#ifndef PI2
#define PI2 6.28318530718f
#endif

// ─── Math ─────────────────────────────────────────────────────────────────────
static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
static inline int   clampi(int v, int lo, int hi)       { return v < lo ? lo : (v > hi ? hi : v); }
static inline float lerpf(float a, float b, float t)    { return a + (b - a) * t; }
static inline float smooth01(float t) { t = clampf(t, 0, 1); return t * t * (3 - 2 * t); }
static inline float wrapAngle(float a) {
  while (a >  PI) a -= PI2;
  while (a < -PI) a += PI2;
  return a;
}

// Fast inline sin/cos (max error ≈ 0.001) for drawing and simulation. libm's
// sinf/cosf live in flash, and instruction fetches from flash are slow while
// PSRAM is busy. (World generation keeps the exact libm versions.)
static inline float fastSin(float x) {
  float k = x * (1.0f / PI2);
  int n = (int)(k + (k >= 0 ? 0.5f : -0.5f));
  x -= n * PI2;                                       // now in [-π, π]
  float y = 1.27323954f * x - 0.405284735f * x * fabsf(x);
  return 0.225f * (y * fabsf(y) - y) + y;
}
static inline float fastCos(float x) { return fastSin(x + 1.57079633f); }
static inline float fmx(float a, float b) { return a > b ? a : b; }
static inline float fmn(float a, float b) { return a < b ? a : b; }
static inline int   iround(float x) { return (int)(x + (x >= 0 ? 0.5f : -0.5f)); }

// ─── RNG (xorshift32) ─────────────────────────────────────────────────────────
static uint32_t _rng = 0x1234567u;
static inline uint32_t rnd32() { _rng ^= _rng << 13; _rng ^= _rng >> 17; _rng ^= _rng << 5; return _rng; }
static inline float frand()                   { return (rnd32() >> 8) * (1.0f / 16777216.0f); }
static inline float frandr(float a, float b)  { return a + (b - a) * frand(); }
static inline int   irandr(int a, int b)      { return a + (int)(rnd32() % (uint32_t)(b - a)); }  // [a,b)

static inline uint32_t hash32(uint32_t x) {
  x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16; return x;
}
static inline uint32_t hash2i(int x, int y, uint32_t seed) {
  return hash32((uint32_t)x * 0x8da6b343U ^ (uint32_t)y * 0xd8163841U ^ seed * 0xcb1ab31fU);
}
static inline float hash2f(int x, int y, uint32_t seed) { return (hash2i(x, y, seed) >> 8) * (1.0f / 16777216.0f); }

// ─── Colours ──────────────────────────────────────────────────────────────────
// All "rgb" colours are 0xRRGGBB (uint32_t). The strip canvas stores NATIVE
// (non-swapped) RGB565 to match the panel framebuffer, which GDMA copies raw.
static inline uint32_t rgb(uint8_t r, uint8_t g, uint8_t b) { return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b; }
static inline uint8_t  cR(uint32_t c) { return (c >> 16) & 0xFF; }
static inline uint8_t  cG(uint32_t c) { return (c >> 8) & 0xFF; }
static inline uint8_t  cB(uint32_t c) { return c & 0xFF; }

static inline uint32_t mixRgb(uint32_t a, uint32_t b, float t) {
  t = clampf(t, 0, 1);
  return rgb((uint8_t)(cR(a) + (cR(b) - cR(a)) * t),
             (uint8_t)(cG(a) + (cG(b) - cG(a)) * t),
             (uint8_t)(cB(a) + (cB(b) - cB(a)) * t));
}
static inline uint32_t scaleRgb(uint32_t c, float k) {
  return rgb((uint8_t)clampf(cR(c) * k, 0, 255), (uint8_t)clampf(cG(c) * k, 0, 255), (uint8_t)clampf(cB(c) * k, 0, 255));
}
static inline uint16_t rgbTo565(uint32_t c) {
  return (uint16_t)(((c >> 8) & 0xF800) | ((c >> 5) & 0x07E0) | ((c >> 3) & 0x001F));
}
static inline uint32_t c565ToRgb(uint16_t c) {
  uint8_t r = (c >> 11) & 0x1F, g = (c >> 5) & 0x3F, b = c & 0x1F;
  return rgb((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2));
}
static inline uint16_t toPx(uint16_t c565)   { return c565; }   // native RGB565, no swap
static inline uint16_t fromPx(uint16_t px)   { return px; }

// Blend two RGB565 values; alpha 0..32 (32 = all fg).
static inline uint16_t blend565(uint16_t fg, uint16_t bg, uint32_t alpha) {
  uint32_t f = (fg | ((uint32_t)fg << 16)) & 0x07E0F81FU;
  uint32_t b = (bg | ((uint32_t)bg << 16)) & 0x07E0F81FU;
  uint32_t r = ((((f - b) * alpha) >> 5) + b) & 0x07E0F81FU;
  return (uint16_t)(r | (r >> 16));
}

// ─── Lighting (set each frame by the world module) ───────────────────────────
// Multiplier applied to every scene colour: day/night tint, weather dimming,
// fog wash and lightning flashes. Use lit() for any colour drawn on the canvas.
static float    lightR = 1, lightG = 1, lightB = 1;
static float    fogMix = 0;                 // 0..1 wash toward fogCol
static uint32_t fogCol = 0xB8C0C4u;
static float    nightAmt = 0;               // 0 day .. 1 full night
static float    sunAmt   = 1;               // direct sunlight strength (shadows, glints)

static inline uint32_t lit(uint32_t c) {
  uint32_t o = rgb((uint8_t)clampf(cR(c) * lightR, 0, 255),
                   (uint8_t)clampf(cG(c) * lightG, 0, 255),
                   (uint8_t)clampf(cB(c) * lightB, 0, 255));
  return fogMix > 0.01f ? mixRgb(o, fogCol, fogMix) : o;
}

// ─── Direct canvas pixel ops (alpha blending, which LovyanGFX lacks) ─────────
// Fast path for wide spans: when (32 - alpha) is a sum of at most two powers of
// two (alpha 8, 12, 14..16, 20, 22..24, 26..31: the UI panels and most effects),
// background × (32 - alpha) / 32 is one or two masked shifts, done two pixels
// per 32-bit word; the colour's share is a constant per span. Channel sums
// can't carry into each other (bg share + colour share ≤ full scale).
static const uint32_t SHIFT_MASK2[6] = { 0, 0x7BEF7BEFu, 0x39E739E7u, 0x18E318E3u, 0x08610861u, 0x00200020u };
static void IRAM_ATTR blendSpanShift(uint16_t* p, int n, uint16_t c, uint32_t a, uint32_t inv) {
  int sA = 0, sB = 0;                                  // shifts for the (up to) two set bits of inv
  for (int k = 4; k >= 0; --k) if (inv & (1u << k)) { if (!sA) sA = 5 - k; else sB = 5 - k; }
  const uint32_t mA = SHIFT_MASK2[sA], mB = sB ? SHIFT_MASK2[sB] : 0;
  uint32_t ct = ((((c >> 11) & 31) * a >> 5) << 11) | ((((c >> 5) & 63) * a >> 5) << 5) | ((c & 31) * a >> 5);
  const uint32_t ct2 = ct | (ct << 16);
  if ((uintptr_t)p & 2) { uint32_t w = *p; *p++ = (uint16_t)(((w >> sA) & mA) + ((w >> sB) & mB) + ct); --n; }
  uint32_t* q = (uint32_t*)p;
  for (int i = n >> 1; i > 0; --i, ++q) { uint32_t w = *q; *q = ((w >> sA) & mA) + ((w >> sB) & mB) + ct2; }
  if (n & 1) { p = (uint16_t*)q; uint32_t w = *p; *p = (uint16_t)(((w >> sA) & mA) + ((w >> sB) & mB) + ct); }
}

static inline void blendSpan(int y, int x0, int x1, uint16_t c565, uint32_t a) {
  if (y < clipY0 || y >= clipY1) return;
  if (x0 < 0) x0 = 0;
  if (x1 > SCREEN_W - 1) x1 = SCREEN_W - 1;
  if (x1 < x0) return;
  uint16_t* p = fb + y * SCREEN_W + x0;
  const uint32_t inv = 32 - a;
  if (x1 - x0 >= 7 && a < 32 && __builtin_popcount(inv) <= 2) { blendSpanShift(p, x1 - x0 + 1, c565, a, inv); return; }
  for (int x = x0; x <= x1; ++x, ++p) *p = toPx(blend565(c565, fromPx(*p), a));
}

// Filled circle blended over the canvas.
static void IRAM_ATTR blendCircle(int cx, int cy, int r, uint32_t col, uint32_t alpha) {
  if (r <= 0 || alpha == 0) return;
  if (cx + r < 0 || cx - r >= SCREEN_W || cy + r < clipY0 || cy - r >= clipY1) return;
  uint16_t c = rgbTo565(col);
  int r2 = r * r;
  int dy0 = clipY0 - cy > -r ? clipY0 - cy : -r;
  int dy1 = clipY1 - 1 - cy < r ? clipY1 - 1 - cy : r;
  for (int dy = dy0; dy <= dy1; ++dy) {
    int hw = (int)sqrtf((float)(r2 - dy * dy));
    blendSpan(cy + dy, cx - hw, cx + hw, c, alpha);
  }
}

// Soft glow: concentric blended discs (bright core, faint rim).
static void IRAM_ATTR blendGlow(int cx, int cy, int r, uint32_t col, uint32_t alpha) {
  blendCircle(cx, cy, r, col, alpha / 3 + 1);
  blendCircle(cx, cy, r * 2 / 3, col, alpha / 3 + 1);
  blendCircle(cx, cy, r / 3, col, alpha / 3 + 1);
}

// Ring (annulus) blended over the canvas.
static void IRAM_ATTR blendRing(int cx, int cy, float r, float th, uint32_t col, uint32_t alpha) {
  if (alpha == 0 || r < 1) return;
  int ro = (int)(r + th * 0.5f + 0.5f);
  if (cx + ro < 0 || cx - ro >= SCREEN_W || cy + ro < clipY0 || cy - ro >= clipY1) return;
  float ri = r - th * 0.5f; if (ri < 0) ri = 0;
  float ro2 = (r + th * 0.5f) * (r + th * 0.5f), ri2 = ri * ri;
  uint16_t c = rgbTo565(col);
  int dy0 = clipY0 - cy > -ro ? clipY0 - cy : -ro;
  int dy1 = clipY1 - 1 - cy < ro ? clipY1 - 1 - cy : ro;
  for (int dy = dy0; dy <= dy1; ++dy) {
    float d2 = (float)(dy * dy);
    if (d2 > ro2) continue;
    int wo = (int)sqrtf(ro2 - d2);
    if (d2 >= ri2) { blendSpan(cy + dy, cx - wo, cx + wo, c, alpha); continue; }
    int wi = (int)sqrtf(ri2 - d2);
    blendSpan(cy + dy, cx - wo, cx - wi, c, alpha);
    blendSpan(cy + dy, cx + wi, cx + wo, c, alpha);
  }
}

static void IRAM_ATTR blendRect(int x, int y, int w, int h, uint32_t col, uint32_t alpha) {
  uint16_t c = rgbTo565(col);
  int y0 = y > clipY0 ? y : clipY0, y1 = (y + h) < clipY1 ? (y + h) : clipY1;
  for (int yy = y0; yy < y1; ++yy) blendSpan(yy, x, x + w - 1, c, alpha);
}

static inline void blendPixel(int x, int y, uint32_t col, uint32_t alpha) {
  if ((unsigned)x >= SCREEN_W || y < clipY0 || y >= clipY1) return;
  uint16_t* p = fb + y * SCREEN_W + x;
  *p = toPx(blend565(rgbTo565(col), fromPx(*p), alpha));
}

// ─── Fast solid primitives (direct to the SRAM strip) ─────────────────────────
// Used instead of LovyanGFX's fillCircle/fillTriangle/drawLine for scene drawing:
// those go through virtual per-span calls, costing several µs each, and the
// scene issues ~1,500 of them per frame. These only touch rows in the current
// strip and return immediately otherwise. Colours are 0xRRGGBB.

static inline void fSpan(int y, int x0, int x1, uint16_t c) {
  if (y < clipY0 || y >= clipY1) return;
  if (x0 < 0) x0 = 0;
  if (x1 > SCREEN_W - 1) x1 = SCREEN_W - 1;
  if (x1 < x0) return;
  uint16_t* p = fb + y * SCREEN_W + x0;
  int n = x1 - x0 + 1;
  if (n >= 4) {
    if ((uintptr_t)p & 2) { *p++ = c; --n; }        // align to 32 bits
    uint32_t c2 = c | ((uint32_t)c << 16);
    uint32_t* q = (uint32_t*)p;
    for (int i = n >> 1; i > 0; --i) *q++ = c2;
    p = (uint16_t*)q;
    if (n & 1) *p = c;
  } else {
    while (n--) *p++ = c;
  }
}

// Solid rectangle (rows outside the strip are skipped for free).
static void fRect(int x, int y, int w, int h, uint32_t col) {
  int y0 = y > clipY0 ? y : clipY0, y1 = (y + h) < clipY1 ? (y + h) : clipY1;
  if (y1 <= y0 || w <= 0) return;
  uint16_t c = toPx(rgbTo565(col));
  for (int yy = y0; yy < y1; ++yy) fSpan(yy, x, x + w - 1, c);
}

static inline void fPixel(int x, int y, uint32_t col) {
  if ((unsigned)x < SCREEN_W && y >= clipY0 && y < clipY1) fb[y * SCREEN_W + x] = toPx(rgbTo565(col));
}

static void IRAM_ATTR fCircle(int cx, int cy, int r, uint32_t col) {
  if (r < 0) return;
  if (cy + r < clipY0 || cy - r >= clipY1 || cx + r < 0 || cx - r >= SCREEN_W) return;
  uint16_t c = toPx(rgbTo565(col));
  int dy0 = clipY0 - cy > -r ? clipY0 - cy : -r;
  int dy1 = clipY1 - 1 - cy < r ? clipY1 - 1 - cy : r;
  float rr = r * r + r * 0.8f;                       // slightly "rounder" like LovyanGFX
  for (int dy = dy0; dy <= dy1; ++dy) {
    int hw = (int)sqrtf(fmx(0.0f, rr - dy * dy));
    fSpan(cy + dy, cx - hw, cx + hw, c);
  }
}

static void IRAM_ATTR fEllipse(int cx, int cy, int rx, int ry, uint32_t col) {
  if (rx < 1 || ry < 1) { fCircle(cx, cy, rx > ry ? rx : ry, col); return; }
  if (cy + ry < clipY0 || cy - ry >= clipY1 || cx + rx < 0 || cx - rx >= SCREEN_W) return;
  uint16_t c = toPx(rgbTo565(col));
  int dy0 = clipY0 - cy > -ry ? clipY0 - cy : -ry;
  int dy1 = clipY1 - 1 - cy < ry ? clipY1 - 1 - cy : ry;
  float iry = 1.0f / (ry + 0.4f);
  for (int dy = dy0; dy <= dy1; ++dy) {
    float t = dy * iry;
    int hw = (int)((rx + 0.4f) * sqrtf(fmx(0.0f, 1.0f - t * t)));
    fSpan(cy + dy, cx - hw, cx + hw, c);
  }
}

static void IRAM_ATTR fTri(int x0, int y0, int x1, int y1, int x2, int y2, uint32_t col) {
  // sort by y
  if (y0 > y1) { int t = y0; y0 = y1; y1 = t; t = x0; x0 = x1; x1 = t; }
  if (y1 > y2) { int t = y1; y1 = y2; y2 = t; t = x1; x1 = x2; x2 = t; }
  if (y0 > y1) { int t = y0; y0 = y1; y1 = t; t = x0; x0 = x1; x1 = t; }
  if (y2 < clipY0 || y0 >= clipY1) return;
  int mnx = x0 < x1 ? (x0 < x2 ? x0 : x2) : (x1 < x2 ? x1 : x2);
  int mxx = x0 > x1 ? (x0 > x2 ? x0 : x2) : (x1 > x2 ? x1 : x2);
  if (mxx < 0 || mnx >= SCREEN_W) return;
  uint16_t c = toPx(rgbTo565(col));
  if (y0 == y2) { fSpan(y0, mnx, mxx, c); return; }
  int ys = y0 > clipY0 ? y0 : clipY0, ye = y2 < clipY1 - 1 ? y2 : clipY1 - 1;
  // edge slopes, once per triangle (a divide per row is slow on the ESP32)
  const float inv02 = (float)(x2 - x0) / (y2 - y0);
  const float inv01 = y1 != y0 ? (float)(x1 - x0) / (y1 - y0) : 0.0f;
  const float inv12 = y2 != y1 ? (float)(x2 - x1) / (y2 - y1) : 0.0f;
  for (int y = ys; y <= ye; ++y) {
    float xa = x0 + (y - y0) * inv02, xb;
    if (y < y1 || y1 == y2) xb = (y1 == y0) ? (float)x1 : x0 + (y - y0) * inv01;
    else                    xb = x1 + (y - y1) * inv12;
    int a = (int)iround(xa), b = (int)iround(xb);
    if (a > b) { int t = a; a = b; b = t; }
    fSpan(y, a, b, c);
  }
}

static void IRAM_ATTR fLine(int x0, int y0, int x1, int y1, uint32_t col) {
  if ((y0 < clipY0 && y1 < clipY0) || (y0 >= clipY1 && y1 >= clipY1)) return;
  if ((x0 < 0 && x1 < 0) || (x0 >= SCREEN_W && x1 >= SCREEN_W)) return;
  uint16_t c = toPx(rgbTo565(col));
  int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
  int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;
  for (;;) {
    if ((unsigned)x0 < SCREEN_W && y0 >= clipY0 && y0 < clipY1) fb[y0 * SCREEN_W + x0] = c;
    if (x0 == x1 && y0 == y1) break;
    int e2 = 2 * err;
    if (e2 >= dy) { err += dy; x0 += sx; }
    if (e2 <= dx) { err += dx; y0 += sy; }
  }
}

static void IRAM_ATTR fCircleOutline(int cx, int cy, int r, uint32_t col) {
  if (r <= 0) { fPixel(cx, cy, col); return; }
  if (cy + r < clipY0 || cy - r >= clipY1) return;
  uint16_t c = toPx(rgbTo565(col));
  auto px = [&](int x, int y) { if ((unsigned)x < SCREEN_W && y >= clipY0 && y < clipY1) fb[y * SCREEN_W + x] = c; };
  int x = r, y = 0, err = 1 - r;
  while (x >= y) {
    px(cx + x, cy + y); px(cx - x, cy + y); px(cx + x, cy - y); px(cx - x, cy - y);
    px(cx + y, cy + x); px(cx - y, cy + x); px(cx + y, cy - x); px(cx - y, cy - x);
    ++y;
    if (err < 0) err += 2 * y + 1; else { --x; err += 2 * (y - x) + 1; }
  }
}

// Thick line with round caps. Do NOT use LovyanGFX drawWideLine/drawWedgeLine on
// the strip canvas: it calls setClipRect(x0, y0, x1, y1) with corner coordinates
// where width/height are expected (widening the clip past the strip, so it writes
// outside the strip buffer), and then clearClipRect() at the end.
static void wideLine(int x0, int y0, int x1, int y1, float r, uint32_t col) {
  int top = (y0 < y1 ? y0 : y1) - (int)r - 1, bot = (y0 > y1 ? y0 : y1) + (int)r + 1;
  if (!stripHit(top, bot)) return;
  float dx = (float)(x1 - x0), dy = (float)(y1 - y0), l = sqrtf(dx * dx + dy * dy);
  if (l > 0.01f) {
    float px = -dy / l * r, py = dx / l * r;
    int ax = (int)iround(x0 + px), ay = (int)iround(y0 + py), bx = (int)iround(x0 - px), by = (int)iround(y0 - py);
    int cx = (int)iround(x1 + px), cy = (int)iround(y1 + py), ex = (int)iround(x1 - px), ey = (int)iround(y1 - py);
    fTri(ax, ay, bx, by, cx, cy, col);
    fTri(bx, by, cx, cy, ex, ey, col);
  }
  int rr = (int)iround(r);
  fCircle(x0, y0, rr, col);
  fCircle(x1, y1, rr, col);
}

// Blended (semi-transparent) triangle — shadows, headlight beams.
static void IRAM_ATTR blendTri(int x0, int y0, int x1, int y1, int x2, int y2, uint32_t col, uint32_t alpha) {
  if (alpha == 0) return;
  if (y0 > y1) { int t = y0; y0 = y1; y1 = t; t = x0; x0 = x1; x1 = t; }
  if (y1 > y2) { int t = y1; y1 = y2; y2 = t; t = x1; x1 = x2; x2 = t; }
  if (y0 > y1) { int t = y0; y0 = y1; y1 = t; t = x0; x0 = x1; x1 = t; }
  if (y2 < clipY0 || y0 >= clipY1) return;
  int mnx = x0 < x1 ? (x0 < x2 ? x0 : x2) : (x1 < x2 ? x1 : x2);
  int mxx = x0 > x1 ? (x0 > x2 ? x0 : x2) : (x1 > x2 ? x1 : x2);
  if (mxx < 0 || mnx >= SCREEN_W) return;
  uint16_t c = rgbTo565(col);
  if (y0 == y2) { blendSpan(y0, mnx, mxx, c, alpha); return; }
  int ys = y0 > clipY0 ? y0 : clipY0, ye = y2 < clipY1 - 1 ? y2 : clipY1 - 1;
  // edge slopes, once per triangle (a divide per row is slow on the ESP32)
  const float inv02 = (float)(x2 - x0) / (y2 - y0);
  const float inv01 = y1 != y0 ? (float)(x1 - x0) / (y1 - y0) : 0.0f;
  const float inv12 = y2 != y1 ? (float)(x2 - x1) / (y2 - y1) : 0.0f;
  for (int y = ys; y <= ye; ++y) {
    float xa = x0 + (y - y0) * inv02, xb;
    if (y < y1 || y1 == y2) xb = (y1 == y0) ? (float)x1 : x0 + (y - y0) * inv01;
    else                    xb = x1 + (y - y1) * inv12;
    int a = iround(xa), b = iround(xb);
    if (a > b) { int t = a; a = b; b = t; }
    blendSpan(y, a, b, c, alpha);
  }
}

// ─── Track-side lighting ──────────────────────────────────────────────────────
// The circuit is floodlit at night, so the track, cars and buildings use a
// brighter light level than the surrounding fields and trees.
static float trkR = 1, trkG = 1, trkB = 1;

static inline uint32_t litT(uint32_t c) {
  uint32_t o = rgb((uint8_t)clampf(cR(c) * trkR, 0, 255),
                   (uint8_t)clampf(cG(c) * trkG, 0, 255),
                   (uint8_t)clampf(cB(c) * trkB, 0, 255));
  return fogMix > 0.01f ? mixRgb(o, fogCol, fogMix) : o;
}
