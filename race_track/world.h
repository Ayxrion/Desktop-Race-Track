#pragma once
// Track world: scenery bake, palette lighting and the scaled background render.
//
// Like the koi pond, everything static (asphalt, kerbs, run-off, barriers, pit
// buildings, grandstands, trees) is baked once per circuit into an 8-bit
// palette-indexed map in PSRAM. Day/night, floodlights, wet track, snow cover
// and lightning only recompute the 256-entry palette.

#include "track.h"
#include "camera.h"
#include "weather.h"
#include "daynight.h"
#include <esp_async_memcpy.h>
#include <esp_cache.h>

// ─── Palette: 16 material groups × 16 shades ─────────────────────────────────
enum : uint8_t {
  G_GRASS = 0,  // outfield
  G_VERGE,      // mown trackside grass
  G_ASPH,       // racing surface
  G_RUNOFF,     // tarmac run-off / paddock
  G_GRAVEL,
  G_RED,        // kerbs, tyre-wall belts
  G_WHITE,      // paint, kerbs
  G_TREE_A,     // broadleaf
  G_TREE_B,     // conifer
  G_TREE_C,     // light green
  G_CONC,       // concrete, steel barriers, roofs
  G_DARK,       // tyres, chequers, deep shadow
  G_CROWD,      // 16 different shirt colours (not shades)
  G_SEAT,       // grandstand seating
  G_ROOF,       // 8 coloured roofs × (dark, light)
  G_DIRT,
  G_COUNT
};
static inline uint8_t PI_(int g, int s) { return (uint8_t)((g << 4) | clampi(s, 0, 15)); }

static uint8_t*  worldIdx = nullptr;   // WORLD_W × WORLD_H palette indices (PSRAM)
static uint8_t*  halfIdx  = nullptr;   // HALF_W  × HALF_H  (for zoomed-out views)
static uint16_t  pal565[256];
static uint32_t  basePal[256];

static const uint32_t ROOF_HUES[8] = { 0xC03A30u, 0x2E5EA8u, 0xE8E8E4u, 0x3A8A4Au, 0xE0B830u, 0xE07A2Au, 0x9AA2AAu, 0x2A9A9Au };
static const uint32_t CROWD_COLS[16] = { 0xE84A3Au, 0xF0F0F0u, 0x3A6AD8u, 0xF0C83Au, 0x2A2A2Au, 0x4AB85Au, 0xE88AB0u, 0xF08A2Au,
                                         0x8A5ACAu, 0x6ACAE8u, 0xB83A3Au, 0xD8D0B0u, 0x3A3A6Au, 0xC8E84Au, 0x8A6A4Au, 0xFFFFFFu };

static void buildBasePalette() {
  const uint32_t base[G_COUNT] = {
    0x4E7A36u, 0x5E9040u, 0x56585Cu, 0x6C7276u, 0xC8B48Cu, 0xC8302Au, 0xE8E8E2u, 0x3E6A2Eu,
    0x2A5034u, 0x6A8E36u, 0xA8A8A2u, 0x2A2A2Eu, 0, 0x3060A8u, 0, 0x8A7050u };
  for (int g = 0; g < G_COUNT; ++g)
    for (int s = 0; s < 16; ++s) {
      uint32_t c;
      if (g == G_CROWD)     c = CROWD_COLS[s];
      else if (g == G_ROOF) c = scaleRgb(ROOF_HUES[s >> 1], (s & 1) ? 1.0f : 0.72f);
      else                  c = scaleRgb(base[g], 0.40f + s * 0.05f);
      basePal[g * 16 + s] = c;
    }
}

static inline uint8_t darkenIdx(uint8_t idx, int amt) {
  int g = idx >> 4, s = idx & 15;
  if (g == G_CROWD || g == G_ROOF) return PI_(G_DARK, 4);
  return PI_(g, s - amt);
}

// ─── Nearest-sample grid (4 px cells) ─────────────────────────────────────────
#define G_CELL 4
#define G_W    (WORLD_W / G_CELL)
#define G_H    (WORLD_H / G_CELL)
#define STAMP_R 112
static uint16_t* gIdx = nullptr;     // nearest centreline sample, 0xFFFF = far from the track
static uint16_t* gD2  = nullptr;     // its squared distance

static bool allocWorld() {
  worldIdx = (uint8_t*)heap_caps_aligned_alloc(64, WORLD_W * WORLD_H, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  halfIdx  = (uint8_t*)heap_caps_aligned_alloc(64, HALF_W * HALF_H,   MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  gIdx     = (uint16_t*)heap_caps_malloc(G_W * G_H * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  gD2      = (uint16_t*)heap_caps_malloc(G_W * G_H * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return worldIdx && halfIdx && gIdx && gD2 && allocTrack();
}

typedef void (*ProgressFn)(float);
typedef void (*RowFn)(int start, int step, ProgressFn progress);

struct _GenJob { RowFn fn; SemaphoreHandle_t done; };
static void _genWorker(void* p) {
  _GenJob* j = (_GenJob*)p;
  j->fn(1, 2, nullptr);
  xSemaphoreGive(j->done);
  vTaskDelete(nullptr);
}
// Rows are interleaved between this task (core 1, reports progress) and a helper on core 0.
static void runOnBothCores(RowFn fn, ProgressFn progress) {
  _GenJob job = { fn, xSemaphoreCreateBinary() };
  if (job.done && xTaskCreatePinnedToCore(_genWorker, "gen", 8192, &job, 1, nullptr, 0) == pdPASS) {
    fn(0, 2, progress);
    xSemaphoreTake(job.done, portMAX_DELAY);
  } else {
    fn(0, 1, progress);
  }
  if (job.done) vSemaphoreDelete(job.done);
}

// Each core stamps every sample but only writes its own grid rows.
static void stampRows(int start, int step, ProgressFn progress) {
  for (int i = 0; i < G_W * G_H; ++i) if ((i / G_W) % step == start) { gIdx[i] = 0xFFFF; gD2[i] = 0xFFFF; }
  const int R = STAMP_R, RC = R / G_CELL + 1;
  for (int i = 0; i < trk.n; ++i) {
    float sx = tp[i].x, sy = tp[i].y;
    int cx = (int)(sx / G_CELL), cy = (int)(sy / G_CELL);
    int gy0 = clampi(cy - RC, 0, G_H - 1), gy1 = clampi(cy + RC, 0, G_H - 1);
    int gx0 = clampi(cx - RC, 0, G_W - 1), gx1 = clampi(cx + RC, 0, G_W - 1);
    for (int gy = gy0; gy <= gy1; ++gy) {
      if (gy % step != start) continue;
      float dy = gy * G_CELL + 2 - sy;
      for (int gx = gx0; gx <= gx1; ++gx) {
        float dx = gx * G_CELL + 2 - sx;
        float d2 = dx * dx + dy * dy;
        if (d2 >= R * R) continue;
        int k = gy * G_W + gx;
        uint16_t q = (uint16_t)d2;
        if (q < gD2[k]) { gD2[k] = q; gIdx[k] = (uint16_t)i; }
      }
    }
    if ((i & 63) == 0) { if (progress) progress(0.05f + 0.15f * i / trk.n); vTaskDelay(1); }   // yield on both cores (IDLE0 watchdog)
  }
}

// ─── Noise ────────────────────────────────────────────────────────────────────
static float vnoise(float x, float y, uint32_t seed) {
  int ix = (int)floorf(x), iy = (int)floorf(y);
  float fx = x - ix, fy = y - iy;
  fx = fx * fx * (3 - 2 * fx); fy = fy * fy * (3 - 2 * fy);
  float a = hash2f(ix, iy, seed),     b = hash2f(ix + 1, iy, seed);
  float c = hash2f(ix, iy + 1, seed), d = hash2f(ix + 1, iy + 1, seed);
  return lerpf(lerpf(a, b, fx), lerpf(c, d, fx), fy);
}
static const uint8_t BAYER4[16] = { 0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5 };
static inline float bayer(int x, int y) { return (BAYER4[(y & 3) * 4 + (x & 3)] + 0.5f) / 16.0f; }

// Grid slots, shared with the race start (race.h).
static inline float gridSlotS(int k) { return -10.0f - k * 13.0f; }
static inline float gridSlotD(int k) { return ((k & 1) ? 1.0f : -1.0f) * trk.W * 0.24f; }
#define CAR_MARK_LEN 14.0f

// ─── Per-pixel scenery ────────────────────────────────────────────────────────
static uint8_t outfieldPixel(int x, int y, float dith) {
  uint32_t sd = trk.seed;
  float forest = vnoise(x / 170.0f, y / 170.0f, sd ^ 0x7711u) + (x < 250 ? 0.35f : 0.0f);
  float s = 7.0f + (vnoise(x / 40.0f, y / 40.0f, sd ^ 0x1234u) - 0.5f) * 4.0f + (hash2f(x, y, sd) - 0.5f) * 2.0f;
  if (forest > 0.58f) s -= 2.5f;                           // shady forest floor
  float field = vnoise(x / 260.0f, y / 260.0f, sd ^ 0x4321u);
  if (field > 0.74f && forest < 0.5f) {                     // ploughed field
    float row = fmodf((x * 0.6f + y * 0.8f) / 5.0f, 1.0f);
    return PI_(G_DIRT, (int)(7 + (row < 0.4f ? -2 : 1) + (hash2f(x, y, sd ^ 9) - 0.5f) * 2 + dith));
  }
  return PI_(G_GRASS, (int)(s + dith));
}

static uint8_t vergePixel(int x, int y, float s, float dith) {
  float stripe = ((int)floorf(s / 16.0f) & 1) ? 1.2f : -1.2f;
  return PI_(G_VERGE, (int)(8.5f + stripe + (hash2f(x, y, trk.seed ^ 0x55u) - 0.5f) * 1.6f + dith));
}

static uint8_t asphaltPixel(int x, int y, float d, float rl, float dith) {
  float sh = 8.0f + (vnoise(x / 7.0f, y / 7.0f, trk.seed ^ 0xA5u) - 0.5f) * 2.4f + (hash2f(x, y, trk.seed ^ 0xA6u) - 0.5f) * 1.4f;
  float r = fabsf(d - rl);
  if (r < 4.5f) sh -= 1.8f * (1.0f - r / 4.5f);            // rubbered-in racing line
  if (fabsf(d) > trk.hw - 3.0f) sh += 0.8f;                 // dusty edges
  return PI_(G_ASPH, (int)(sh + dith));
}

static int pitPixel(int x, int y, float sS, float ad, float dith) {
  const float hw = trk.hw;
  const float a = trk.pitS0, b = trk.pitS1, TAP = 28.0f;
  if (sS < a || sS > b) return -1;
  float t = sS < a + TAP ? (sS - a) / TAP : (sS > b - TAP ? (b - sS) / TAP : 1.0f);
  t = smooth01(t);
  float laneIn = hw + lerpf(0.0f, 8.0f, t), laneOut = laneIn + 10.5f;
  if (ad < laneIn) {
    if (t > 0.99f && ad > hw + 3.0f) return PI_(G_CONC, (ad > laneIn - 1.2f) ? 9 : 13);   // pit wall
    return t > 0.99f ? (int)vergePixel(x, y, sS, dith) : -1;
  }
  if (ad < laneOut) {
    if (ad < laneIn + 0.9f || ad > laneOut - 0.9f) return PI_(G_WHITE, 12);
    return PI_(G_ASPH, (int)(8.5f + (hash2f(x, y, 77) - 0.5f) * 1.5f + dith));
  }
  if (t < 0.99f) return -1;
  float m0 = a + TAP + 6, m1 = b - TAP - 6;
  if (sS < m0 || sS > m1) return ad < laneOut + 44 ? (int)vergePixel(x, y, sS, dith) : -1;
  float u = ad - laneOut;
  float cell = (sS - m0) / 12.0f;
  int ci = (int)floorf(cell); float cf = cell - ci;
  if (u < 15.0f) {                                          // garages
    if (u < 1.8f) return PI_(G_ROOF, ((ci % 8) * 2) + 1);    // team-coloured doors
    if (cf < 0.08f) return PI_(G_CONC, 8);
    return PI_(G_CONC, u > 13.8f ? 9 : 13 - (int)(u / 6));
  }
  if (u < 42.0f) {                                          // paddock with trucks
    float pc = (sS - m0) / 10.0f; int pi = (int)floorf(pc); float pf = pc - pi;
    uint32_t h = hash32(pi * 131 + trk.seed);
    if ((h & 3) != 0 && pf > 0.15f && pf < 0.85f && u > 19 && u < 36) {
      if (u > 32.5f) return PI_(G_ROOF, ((h >> 4) & 7) * 2);
      return PI_(G_ROOF, ((h >> 4) & 7) * 2 + 1);
    }
    if (u > 36 && u < 38.5f && pf > 0.15f && pf < 0.85f && (h & 3)) return PI_(G_DARK, 6);   // truck shadow
    return PI_(G_RUNOFF, (int)(10 + (hash2f(x, y, 91) - 0.5f) * 1.5f + dith));
  }
  if (u < 46.0f) return vergePixel(x, y, sS, dith);
  return -1;
}

static int standPixel(int x, int y, float s, float ad, int side, float dith, bool& shadow) {
  shadow = false;
  for (int i = 0; i < trk.nStands; ++i) {
    const Stand& st = trk.stands[i];
    if (st.side != side) continue;
    float u = sDelta(st.sc, s);
    if (fabsf(u) > st.half || ad < st.d0 - 1.0f || ad > st.d1 + 4.0f) continue;
    if (ad >= st.d1) { shadow = true; return -1; }          // the stand's shadow on the grass
    if (ad < st.d0) return PI_(G_CONC, 9);                  // front wall
    if (fabsf(u) > st.half - 1.5f) return PI_(G_CONC, 11);  // end walls
    float depth = st.d1 - st.d0;
    float v = (ad - st.d0) / depth;
    if (v > 0.74f) {                                        // roof
      if (v > 0.97f) return PI_(G_DARK, 5);
      float rib = fmodf(fabsf(u) / 10.0f, 1.0f);
      return PI_(G_ROOF, st.roofHue * 2 + (rib < 0.12f ? 0 : 1));
    }
    float rowF = (ad - st.d0) / 2.0f;
    int row = (int)rowF; float rf = rowF - row;
    if (rf < 0.32f) return PI_(G_CONC, 9 + (row & 1));       // steps
    uint32_t h = hash2i(x, y, trk.seed ^ (i * 7919));
    if ((h & 255) < 170) return PI_(G_CROWD, (h >> 8) & 15);
    return PI_(G_SEAT, 6 + (row & 1) * 2);
  }
  return -1;
}

static uint8_t bakePixel(int x, int y) {
  const float px = x + 0.5f, py = y + 0.5f;
  const float dith = bayer(x, y);
  int gi = gIdx[(y >> 2) * G_W + (x >> 2)];
  if (gi == 0xFFFF) return outfieldPixel(x, y, dith);

  // Refine to the nearest centreline segment
  float best = 1e18f, bt = 0, bd = 0; int bi = gi;
  for (int k = -4; k <= 3; ++k) {
    int i = wrapI(gi + k), j = wrapI(i + 1);
    float ax = tp[i].x, ay = tp[i].y, ex = tp[j].x - ax, ey = tp[j].y - ay;
    float l2 = ex * ex + ey * ey; if (l2 < 1e-6f) continue;
    float t = clampf(((px - ax) * ex + (py - ay) * ey) / l2, 0, 1);
    float qx = ax + ex * t - px, qy = ay + ey * t - py, d2 = qx * qx + qy * qy;
    if (d2 < best) { best = d2; bi = i; bt = t; bd = ((px - ax) * -ey + (py - ay) * ex) / sqrtf(l2); }
  }
  const float hw = trk.hw;
  const float d = bd, ad = fabsf(d), s = (bi + bt) * trk.ds;
  if (ad > STAMP_R - 2) return outfieldPixel(x, y, dith);
  const int side = d > 0 ? 1 : 0;
  const TrackPt& T = bt < 0.5f ? tp[bi] : tp[wrapI(bi + 1)];
  const float sS = sSigned(s);

  if (trk.hasPits && side == (trk.pitSide > 0 ? 1 : 0) && ad >= hw) {
    int p = pitPixel(x, y, sS, ad, dith);
    if (p >= 0) return (uint8_t)p;
  }
  bool shadow = false;
  if (ad >= hw + KERB_W) {
    int sp = standPixel(x, y, s, ad, side, dith, shadow);
    if (sp >= 0) return (uint8_t)sp;
  }

  uint8_t out;
  const float bar = T.bar[side];
  if (ad < hw) {
    // Racing surface: start line, grid boxes, edge lines
    if (fabsf(sS - 1.5f) < 1.5f) {
      int cx = (int)floorf(sS / 1.5f + 8), cy = (int)floorf((d + hw) / 1.5f);
      return ((cx + cy) & 1) ? PI_(G_WHITE, 14) : PI_(G_DARK, 2);
    }
    if (ad > hw - 1.3f) return PI_(G_WHITE, 12);
    if (sS < 0 && sS > -300) {
      for (int k = 0; k < 20; ++k) {
        float ls = gridSlotS(k) + CAR_MARK_LEN * 0.5f + 2.5f, ld = gridSlotD(k);
        if (fabsf(sS - ls) < 0.6f && fabsf(d - ld) < 4.5f) return PI_(G_WHITE, 12);
        if (fabsf(d - ld) > 3.6f && fabsf(d - ld) < 4.5f && sS < ls && sS > ls - 4.0f) return PI_(G_WHITE, 11);
      }
    }
    return asphaltPixel(x, y, d, T.rl, dith);
  }
  if (T.kerb[side] && ad < hw + KERB_W) {
    bool red = ((int)floorf(s / 4.5f)) & 1;
    int sh = ad > hw + KERB_W - 0.8f ? 8 : 12;
    return red ? PI_(G_RED, sh) : PI_(G_WHITE, sh + 1);
  }
  const bool tyres = T.run[side] == 1;
  const float thick = tyres ? 3.2f : 2.2f;
  if (bar > 0 && ad >= bar && ad < bar + thick) {
    if (tyres) {                                            // stacked tyre wall with coloured belts
      int cell = (int)floorf(s / 2.6f);
      float f = fmodf(s / 2.6f, 1.0f), g = (ad - bar) / thick;
      float r2 = (f - 0.5f) * (f - 0.5f) + (g - 0.5f) * (g - 0.5f);
      if ((cell % 6) < 2 && r2 > 0.09f) return (cell & 1) ? PI_(G_RED, 11) : PI_(G_WHITE, 12);
      return PI_(G_DARK, r2 < 0.05f ? 1 : 4);
    }
    float post = fmodf(s / 6.0f, 1.0f);                     // steel armco with posts
    if (post < 0.12f) return PI_(G_CONC, 6);
    return PI_(G_CONC, (ad - bar) < 1.1f ? 14 : 11);
  }
  if (bar > 0 ? ad < bar : ad < hw + 14.0f) {
    uint8_t type = T.run[side];
    float start = hw + (T.kerb[side] ? KERB_W : 0.0f);
    if (type == 1 && ad >= start + 2.5f) {
      float sp = hash2f(x, y, trk.seed ^ 0x6Au);
      out = PI_(G_GRAVEL, (int)(7 + (sp - 0.5f) * 5.0f + dith - (ad < start + 4.0f ? 2 : 0)));
    } else if (type == 2) {
      out = PI_(G_RUNOFF, (int)(9 + (hash2f(x, y, trk.seed ^ 0x6Bu) - 0.5f) * 1.5f + dith));
    } else {
      out = vergePixel(x, y, s, dith);
    }
  } else if (bar > 0 && ad < bar + thick + 1.6f) {
    out = darkenIdx(vergePixel(x, y, s, dith), 4);          // barrier shadow
  } else if (ad < (bar > 0 ? bar + 8.0f : hw + 20.0f)) {
    out = vergePixel(x, y, s, dith);
  } else {
    out = outfieldPixel(x, y, dith);
  }
  return shadow ? darkenIdx(out, 5) : out;
}

static void bakeRows(int start, int step, ProgressFn progress) {
  for (int y = start; y < WORLD_H; y += step) {
    uint8_t* row = worldIdx + y * WORLD_W;
    for (int x = 0; x < WORLD_W; ++x) row[x] = bakePixel(x, y);
    if ((y & 15) == start) { if (progress) progress(0.2f + 0.7f * y / WORLD_H); vTaskDelay(1); }
  }
}

// ─── Trees ────────────────────────────────────────────────────────────────────
static inline bool grassAt(int x, int y) {
  if ((unsigned)x >= WORLD_W || (unsigned)y >= WORLD_H) return false;
  return (worldIdx[y * WORLD_W + x] >> 4) == G_GRASS;
}

static void stampTree(float cx, float cy, float r, int group, uint32_t h) {
  int R = (int)(r * 1.6f) + 2;
  int x0 = clampi((int)cx - R, 0, WORLD_W - 1), x1 = clampi((int)cx + R, 0, WORLD_W - 1);
  int y0 = clampi((int)cy - R, 0, WORLD_H - 1), y1 = clampi((int)cy + R, 0, WORLD_H - 1);
  // Shadow toward the lower right
  float sx = cx + r * 0.5f, sy = cy + r * 0.6f, sr2 = r * r * 0.9f;
  for (int y = y0; y <= y1; ++y)
    for (int x = x0; x <= x1; ++x) {
      float dx = x - sx, dy = y - sy;
      if (dx * dx + dy * dy > sr2) continue;
      uint8_t& p = worldIdx[y * WORLD_W + x];
      int g = p >> 4;
      if (g == G_GRASS || g == G_VERGE || g == G_DIRT) p = darkenIdx(p, 4);
    }
  // Lumpy canopy, dome-lit from the upper left
  float lx[5], ly[5], lr2[5];
  lx[0] = cx; ly[0] = cy; lr2[0] = (r * 0.72f) * (r * 0.72f);
  float a0 = (h & 255) / 255.0f * PI2;
  for (int k = 1; k < 5; ++k) {
    float a = a0 + k * PI2 / 4 + ((h >> (k * 4)) & 15) / 15.0f * 0.8f;
    lx[k] = cx + cosf(a) * r * 0.38f; ly[k] = cy + sinf(a) * r * 0.38f;
    float rr = r * (0.48f + ((h >> (k * 3 + 8)) & 7) / 7.0f * 0.14f);
    lr2[k] = rr * rr;
  }
  for (int y = y0; y <= y1; ++y)
    for (int x = x0; x <= x1; ++x) {
      bool in = false;
      for (int k = 0; k < 5 && !in; ++k) { float dx = x - lx[k], dy = y - ly[k]; in = dx * dx + dy * dy <= lr2[k]; }
      if (!in) continue;
      float vx = (x - cx) / r, vy = (y - cy) / r;
      float vz = sqrtf(fmaxf(0.0f, 1.0f - vx * vx - vy * vy));
      float diff = fmaxf(0.0f, -vx * 0.5f - vy * 0.6f + vz * 0.62f);
      float n = hash2f(x, y, h);
      worldIdx[y * WORLD_W + x] = PI_(group, (int)(1.0f + diff * 13.0f + (n - 0.5f) * 3.0f + bayer(x, y)));
    }
}

static void placeTrees(ProgressFn progress) {
  uint32_t save = _rng;
  _rng = trk.seed * 747796405u + 0x7733u; if (!_rng) _rng = 1;
  int placed = 0;
  for (int tries = 0; tries < 12000 && placed < 1400; ++tries) {
    float x = frandr(4, WORLD_W - 4), y = frandr(4, WORLD_H - 4);
    float dens = vnoise(x / 170.0f, y / 170.0f, trk.seed ^ 0x7711u) + (x < 250 ? 0.35f : 0.0f);
    float prob = dens > 0.58f ? 0.95f : 0.05f;
    if (frand() > prob) continue;
    float r = frandr(4.5f, 10.0f);
    int ix = (int)x, iy = (int)y, ir = (int)r;
    if (!grassAt(ix, iy) || !grassAt(ix + ir, iy) || !grassAt(ix - ir, iy) || !grassAt(ix, iy + ir) || !grassAt(ix, iy - ir)) continue;
    uint32_t h = rnd32();
    float tv = vnoise(x / 300.0f, y / 300.0f, trk.seed ^ 0x3131u);
    int grp = tv > 0.6f ? G_TREE_B : ((h & 7) == 0 ? G_TREE_C : G_TREE_A);
    stampTree(x, y, r, grp, h);
    ++placed;
    if (progress && (placed & 63) == 0) progress(0.9f + 0.08f * placed / 1400.0f);
  }
  _rng = save;
  Serial.printf("gen: %d trees\n", placed);
}

// ─── Background prefetch (GDMA, PSRAM → SRAM) — as in the koi pond ────────────
#define SRC_ROWS   (STRIP_H + 2)
#define SRC_STRIDE 864
#define SRC_BYTES  (SRC_ROWS * SRC_STRIDE)
static uint8_t*              srcBuf = nullptr;
static async_memcpy_handle_t srcDma = nullptr;
static SemaphoreHandle_t     srcReady = nullptr;
static bool                  srcDmaOk = false, srcPending = false;
static int                   srcRy0 = 0, srcX0 = 0, srcStride = 0;

static bool IRAM_ATTR _srcCopied(async_memcpy_handle_t, async_memcpy_event_t*, void*) {
  BaseType_t woken = pdFALSE;
  xSemaphoreGiveFromISR(srcReady, &woken);
  return woken == pdTRUE;
}

// The DMA bypasses the CPU cache, so freshly baked maps must be written back.
static bool flushMaps() {
  const int fl = ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED;
  esp_err_t e1 = esp_cache_msync(worldIdx, WORLD_W * WORLD_H, fl);
  esp_err_t e2 = esp_cache_msync(halfIdx, HALF_W * HALF_H, fl);
  return e1 == ESP_OK && e2 == ESP_OK;
}

static void initBgPrefetch() {
  srcBuf = (uint8_t*)heap_caps_aligned_alloc(64, SRC_BYTES, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
  srcReady = xSemaphoreCreateBinary();
  async_memcpy_config_t cfg = ASYNC_MEMCPY_DEFAULT_CONFIG();
  cfg.backlog = SRC_ROWS + 2;
  esp_err_t e3 = (srcBuf && srcReady) ? esp_async_memcpy_install(&cfg, &srcDma) : ESP_ERR_NO_MEM;
  srcDmaOk = (e3 == ESP_OK);
  Serial.printf("BG prefetch: %s (install %s)\n", srcDmaOk ? "GDMA" : "direct PSRAM reads", esp_err_to_name(e3));
}

// ─── Generation entry point ───────────────────────────────────────────────────
// sel: 0..NUM_PRESETS-1 = preset, -1 = random from `seed`.
static void generateWorld(int sel, uint32_t seed, ProgressFn progress) {
  uint32_t t0 = millis();
  buildBasePalette();
  if (sel >= 0) buildPresetTrack(sel); else buildRandomTrack(seed);
  if (progress) progress(0.05f);
  Serial.printf("gen: track %lu ms\n", (unsigned long)(millis() - t0)); t0 = millis();
  runOnBothCores(stampRows, progress);
  Serial.printf("gen: grid %lu ms\n", (unsigned long)(millis() - t0)); t0 = millis();
  runOnBothCores(bakeRows, progress);
  Serial.printf("gen: bake %lu ms\n", (unsigned long)(millis() - t0)); t0 = millis();
  placeTrees(progress);
  for (int y = 0; y < HALF_H; ++y) {
    const uint8_t* s0 = worldIdx + (y * 2) * WORLD_W;
    uint8_t* d = halfIdx + y * HALF_W;
    for (int x = 0; x < HALF_W; ++x) d[x] = s0[x * 2 + (y & 1)];
  }
  bool ok = flushMaps();
  if (!ok && srcDmaOk) { Serial.println("gen: cache sync failed - using direct PSRAM reads"); srcDmaOk = false; }
  Serial.printf("gen: trees+half %lu ms\n", (unsigned long)(millis() - t0));
  if (progress) progress(1.0f);
}

// ─── Lighting & palette ───────────────────────────────────────────────────────
static int8_t   weatherOverride = -1;       // -1 = live weather, else WeatherCondition
static float    flashAmt = 0;
static float    snowCover = 0;
static float    wetAmt = 0;                 // 0 dry .. 1 soaked
static uint32_t skyCol = 0x7896B4u;
static bool     palChanged = true;         // set when pal565 actually changes (forces a full redraw)

static WeatherCondition effectiveWeather() {
  return weatherOverride >= 0 ? (WeatherCondition)weatherOverride : (WeatherCondition)currentWeather;
}

static void updateLighting(bool snap = false) {
  float p = getDayProgress();
  float sunElev = sinf((p - 0.25f) * PI2);
  float day = smooth01((sunElev + 0.10f) / 0.38f);
  float gold = (sunElev > -0.12f && sunElev < 0.35f) ? expf(-powf((sunElev - 0.06f) / 0.13f, 2)) : 0;

  float tr = lerpf(0.20f, 1.0f, day), tg = lerpf(0.25f, 1.0f, day), tb = lerpf(0.42f, 1.0f, day);
  tr *= 1 + 0.12f * gold; tg *= 1 - 0.06f * gold; tb *= 1 - 0.28f * gold;

  WeatherCondition wx = effectiveWeather();
  float dim = 1, sun = 1, fog = 0, wetT = 0;
  switch (wx) {
    case WEATHER_PARTLY_CLOUDY: dim = 0.94f; sun = 0.75f; break;
    case WEATHER_CLOUDY:        dim = 0.80f; sun = 0.30f; break;
    case WEATHER_RAINY:         dim = 0.70f; sun = 0.15f; wetT = 1.0f; break;
    case WEATHER_STORMY:        dim = 0.56f; sun = 0.05f; wetT = 1.0f; break;
    case WEATHER_SNOWY:         dim = 0.90f; sun = 0.30f; wetT = 0.5f; break;
    case WEATHER_FOGGY:         dim = 0.86f; sun = 0.12f; fog = 0.34f; wetT = 0.2f; break;
    default: break;
  }
  tr *= dim; tg *= dim; tb *= dim;
  if (wx == WEATHER_SNOWY) tb *= 1.05f;

  const float k = snap ? 1.0f : 0.03f;
  lightR = lerpf(lightR, tr, k); lightG = lerpf(lightG, tg, k); lightB = lerpf(lightB, tb, k);
  // Floodlights: at night the circuit stays bright while the fields go dark.
  float flood = (1.0f - day) * 0.9f;
  float fl0 = 0.80f * fmx(dim, 0.7f);
  trkR = fmx(lightR, lerpf(lightR, fl0 * 0.98f, flood));
  trkG = fmx(lightG, lerpf(lightG, fl0 * 0.97f, flood));
  trkB = fmx(lightB, lerpf(lightB, fl0 * 0.92f, flood));
  fogMix = lerpf(fogMix, fog * (0.4f + 0.6f * day), k);
  fogCol = mixRgb(0x283038u, 0xB8C0C4u, day);
  sunAmt = lerpf(sunAmt, sun * day, k);
  nightAmt = 1.0f - day;
  snowCover = lerpf(snowCover, wx == WEATHER_SNOWY ? 1.0f : 0.0f, snap ? 1.0f : 0.004f);
  if (snap) wetAmt = wetT;
  else if (wetAmt < wetT) wetAmt = fmn(wetT, wetAmt + 0.0025f);     // soaks in over ~15 s
  else wetAmt = fmx(wetT, wetAmt - 0.0006f);                        // dries over ~1 min

  uint32_t daySky = mixRgb(0x9AB4C8u, 0x7894B0u, sun);
  skyCol = mixRgb(mixRgb(0x101828u, daySky, day), 0xE09060u, gold * 0.7f);

  float fl = flashAmt;
  static uint32_t lastPalMs = 0;
  static bool     flashing = false;
  bool flashNow = fl > 0.01f;
  if (!snap && !flashNow && !flashing && millis() - lastPalMs < 500) { flashAmt *= 0.78f; return; }
  flashing = flashNow;
  lastPalMs = millis();
  const float fR = 1 + fl * 1.3f, fG = 1 + fl * 1.35f, fB = 1 + fl * 1.5f;
  uint16_t newPal[256];

  for (int i = 0; i < 256; ++i) {
    int g = i >> 4;
    uint32_t c = basePal[i];
    if (g == G_ASPH || g == G_RUNOFF) c = mixRgb(scaleRgb(c, 1.0f - 0.32f * wetAmt), skyCol, 0.12f * wetAmt);
    else if (g == G_WHITE || g == G_RED || g == G_GRAVEL) c = scaleRgb(c, 1.0f - 0.15f * wetAmt);
    if (snowCover > 0.01f) {
      float amt = 0;
      switch (g) {
        case G_GRASS: case G_VERGE: amt = 0.72f; break;
        case G_TREE_A: case G_TREE_B: case G_TREE_C: amt = 0.45f; break;
        case G_GRAVEL: amt = 0.6f; break;
        case G_DIRT: amt = 0.8f; break;
        case G_ROOF: case G_CONC: amt = 0.55f; break;
        case G_ASPH: case G_RUNOFF: amt = 0.04f; break;
        default: break;
      }
      c = mixRgb(c, 0xE6ECF2u, snowCover * amt);
    }
    bool manMade = !(g == G_GRASS || g == G_TREE_A || g == G_TREE_B || g == G_TREE_C || g == G_DIRT || g == G_VERGE);
    float lr = manMade ? trkR : (g == G_VERGE ? (lightR + trkR) * 0.5f : lightR);
    float lg = manMade ? trkG : (g == G_VERGE ? (lightG + trkG) * 0.5f : lightG);
    float lb = manMade ? trkB : (g == G_VERGE ? (lightB + trkB) * 0.5f : lightB);
    uint32_t o = rgb((uint8_t)clampf(cR(c) * lr * fR, 0, 255), (uint8_t)clampf(cG(c) * lg * fG, 0, 255),
                     (uint8_t)clampf(cB(c) * lb * fB, 0, 255));
    if (fogMix > 0.01f) o = mixRgb(o, fogCol, fogMix);
    newPal[i] = toPx(rgbTo565(o));
  }
  if (memcmp(newPal, pal565, sizeof(newPal)) != 0) { memcpy(pal565, newPal, sizeof(newPal)); palChanged = true; }
  flashAmt *= 0.78f;
}

// Lit colour of the scenery at a world point.
static uint32_t IRAM_ATTR sceneryRgb(float wx, float wy) {
  int x = clampi((int)wx, 0, WORLD_W - 1), y = clampi((int)wy, 0, WORLD_H - 1);
  return c565ToRgb(fromPx(pal565[worldIdx[y * WORLD_W + x]]));
}

// ─── Background render (scaled, palette-mapped) — as in the koi pond ─────────
static int16_t _xmap[SCREEN_W];
static struct { const uint8_t* src; int sw, sh; float inv, y0; bool identity; int xoff; } _bg;

static inline int bgRowFor(int sy) { return clampi((int)(_bg.y0 + (sy + 0.5f) * _bg.inv), 0, _bg.sh - 1); }

static void prefetchStrip(int y, int h = -1) {
  if (h < 0) h = stripH;
  srcPending = false;
  if (!srcDmaOk || y >= SCREEN_H) return;
  int yEnd = (y + h < SCREEN_H ? y + h : SCREEN_H) - 1;
  int ry0 = bgRowFor(y), ry1 = bgRowFor(yEnd);
  int n = ry1 - ry0 + 1;
  if ((size_t)n * _bg.sw > SRC_BYTES) return;
  xSemaphoreTake(srcReady, 0);
  srcStride = _bg.sw; srcX0 = 0;
  esp_err_t e = esp_async_memcpy(srcDma, srcBuf, (void*)(_bg.src + (size_t)ry0 * _bg.sw), (size_t)n * _bg.sw,
                                 _srcCopied, nullptr);
  if (e != ESP_OK) {
    Serial.printf("BG prefetch failed (%s) - using direct PSRAM reads\n", esp_err_to_name(e));
    srcDmaOk = false;
    return;
  }
  srcRy0 = ry0;
  srcPending = true;
}

static void prepareBackground() {
  const bool useHalf = cam.zoom < 0.999f;
  _bg.src = useHalf ? halfIdx : worldIdx;
  _bg.sw = useHalf ? HALF_W : WORLD_W; _bg.sh = useHalf ? HALF_H : WORLD_H;
  const float s = useHalf ? 0.5f : 1.0f;
  _bg.inv = s / cam.zoom;
  const float x0 = (cam.x - SCREEN_CX / cam.zoom) * s;
  _bg.y0 = (cam.y - SCREEN_CY / cam.zoom) * s;
  for (int sx = 0; sx < SCREEN_W; ++sx) _xmap[sx] = (int16_t)clampi((int)(x0 + (sx + 0.5f) * _bg.inv), 0, _bg.sw - 1);
  _bg.identity = fabsf(_bg.inv - 1.0f) < 1e-4f;
  _bg.xoff = _xmap[0];
  int maxRows = (int)(SRC_BYTES / _bg.sw);
  int h = (int)((maxRows - 2) / _bg.inv) + 1;
  stripH = clampi(h, 8, STRIP_H);
}

static void IRAM_ATTR renderBackground() {
  const uint8_t* src = _bg.src;
  const int sw = _bg.sw, sh = _bg.sh, xoff = _bg.xoff;
  const float inv = _bg.inv, y0 = _bg.y0;
  const bool identity = _bg.identity;
  bool fromSram = srcPending && xSemaphoreTake(srcReady, pdMS_TO_TICKS(30)) == pdTRUE;
  srcPending = false;
  int prevRy = -1;
  for (int sy = clipY0; sy < clipY1; ++sy) {
    int ry = clampi((int)(y0 + (sy + 0.5f) * inv), 0, sh - 1);
    uint16_t* d = fb + sy * SCREEN_W;
    if (ry == prevRy) { memcpy(d, d - SCREEN_W, SCREEN_W * 2); continue; }
    prevRy = ry;
    const uint8_t* row = fromSram ? srcBuf + (ry - srcRy0) * srcStride - srcX0 : src + ry * sw;
    uint32_t* d32 = (uint32_t*)d;
    if (identity) {
      const uint8_t* r = row + xoff;
      if (((uintptr_t)r & 3) == 0) {
        const uint32_t* r32 = (const uint32_t*)r;
        for (int i = 0; i < SCREEN_W / 4; ++i) {
          uint32_t q = r32[i];
          d32[2 * i]     = pal565[q & 0xFF]         | ((uint32_t)pal565[(q >> 8) & 0xFF] << 16);
          d32[2 * i + 1] = pal565[(q >> 16) & 0xFF] | ((uint32_t)pal565[q >> 24] << 16);
        }
      } else {
        for (int i = 0; i < SCREEN_W / 2; ++i)
          d32[i] = pal565[r[2 * i]] | ((uint32_t)pal565[r[2 * i + 1]] << 16);
      }
    } else {
      for (int i = 0; i < SCREEN_W / 2; ++i)
        d32[i] = pal565[row[_xmap[2 * i]]] | ((uint32_t)pal565[row[_xmap[2 * i + 1]]] << 16);
    }
  }
}
