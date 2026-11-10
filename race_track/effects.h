#pragma once
// Particles (dust, tyre smoke, rain spray, grass), camera flashes in the
// grandstands, floodlight glows, and screen-space rain / snow / fog / lightning.

#include "world.h"

// ─── World-space particles ────────────────────────────────────────────────────
enum PType : uint8_t { PT_DUST, PT_SMOKE, PT_SPRAY, PT_GRASS, PT_FLASH, PT_SPARK };
struct Particle { float x, y, vx, vy, r, grow; uint8_t type, life, maxLife; bool active; };
#define MAX_PARTS 180
// Both arrays live in PSRAM (~5 KB each, so they stay in cache): internal RAM
// is tight, and the TLS library (weather) allocates its buffers there.
static Particle* parts  = nullptr;
static Particle* partsR = nullptr;        // snapshot drawn by core 1 while the sim updates parts on core 0
static void snapshotParticles() { memcpy(partsR, parts, sizeof(Particle) * MAX_PARTS); }
static int partCursor = 0;

static void spawnParticle(uint8_t type, float x, float y, float vx, float vy, float r, float grow, int life) {
  for (int k = 0; k < MAX_PARTS; ++k) {
    Particle& p = parts[partCursor];
    partCursor = (partCursor + 1) % MAX_PARTS;
    if (!p.active || k == MAX_PARTS - 1) {
      p = { x, y, vx, vy, r, grow, type, (uint8_t)life, (uint8_t)life, true };
      return;
    }
  }
}

static void updateParticles() {
  for (int i = 0; i < MAX_PARTS; ++i) {
    Particle& p = parts[i];
    if (!p.active) continue;
    p.x += p.vx; p.y += p.vy;
    p.vx *= 0.90f; p.vy *= 0.90f;
    p.r += p.grow;
    if (p.life-- <= 1) p.active = false;
  }
}

static void IRAM_ATTR drawParticles() {
  const float z = cam.zoom;
  for (int i = 0; i < MAX_PARTS; ++i) {
    const Particle& p = partsR[i];
    if (!p.active) continue;
    float R = p.r * z;
    if (!worldVisible(p.x, p.y, p.r + 1)) continue;
    int sx = (int)w2sx(p.x), sy = (int)w2sy(p.y);
    float f = p.life / (float)p.maxLife;
    switch (p.type) {
      case PT_DUST:  blendCircle(sx, sy, (int)fmx(1, R), litT(0xC8B48Cu), (uint32_t)(16 * f) + 1); break;
      case PT_SMOKE: blendCircle(sx, sy, (int)fmx(1, R), litT(0xD8D8D8u), (uint32_t)(15 * f) + 1); break;
      case PT_SPRAY: blendCircle(sx, sy, (int)fmx(1, R), litT(0xD8E0E8u), (uint32_t)(9 * f) + 1); break;
      case PT_GRASS: blendCircle(sx, sy, (int)fmx(1, R), litT(0x6A9A3Au), (uint32_t)(22 * f) + 1); break;
      case PT_SPARK:
        fCircle(sx, sy, (int)fmx(0, 0.6f * z), f > 0.5f ? 0xFFF0A0u : 0xFF9030u);
        if (z > 1.2f) blendCircle(sx, sy, (int)(2 * z), 0xFFB040u, 10);
        break;
      case PT_FLASH:
        blendCircle(sx, sy, (int)fmx(1, 1.4f * z), 0xFFFFFFu, 28);
        if (z > 1.2f) blendCircle(sx, sy, (int)(3 * z), 0xFFFFFFu, 8);
        break;
    }
  }
}

// ─── Grandstands: camera flashes ──────────────────────────────────────────────
static void updateStandFlashes() {
  float rate = 0.012f + 0.2f * nightAmt;
  for (int i = 0; i < trk.nStands; ++i) {
    if (frand() > rate) continue;
    const Stand& st = trk.stands[i];
    float s = st.sc + frandr(-st.half + 2, st.half - 2);
    float d = frandr(st.d0 + 1, st.d0 + (st.d1 - st.d0) * 0.7f) * (st.side ? 1.0f : -1.0f);
    float x, y; trackPoint(s, d, x, y);
    spawnParticle(PT_FLASH, x, y, 0, 0, 1, 0, 2);
  }
}

// ─── Floodlight glows (night) ─────────────────────────────────────────────────
static void IRAM_ATTR drawLightGlows() {
  if (nightAmt < 0.25f) return;
  uint32_t a = (uint32_t)(12 * (nightAmt - 0.25f) / 0.75f) + 1;
  int r = (int)(16 * cam.zoom);
  for (int i = 0; i < trk.nLights; ++i) {
    const LightPos& L = trk.lights[i];
    if (!worldVisible(L.x, L.y, 18)) continue;
    int sx = (int)w2sx(L.x), sy = (int)w2sy(L.y);
    blendGlow(sx, sy, r, 0xFFE8C0u, a);
    fCircle(sx, sy, (int)fmx(1, 1.3f * cam.zoom), 0xFFF4E0u);
  }
}

// ─── Rain / snow / fog / lightning (screen space) ─────────────────────────────
struct Drop { float x, y, vx, vy; uint8_t life; };
#define MAX_DROPS 130
static Drop drops[MAX_DROPS];
static int  numDrops = 0;

struct Flake { float x, y, sway, spd; uint8_t size, life; };
#define MAX_FLAKES 80
static Flake flakes[MAX_FLAKES];
static int  numFlakes = 0;

static float fogPhase = 0, windStrength = 0.3f;

static void resetDrop(Drop& d) {
  d.x = frandr(-40, SCREEN_W); d.y = frandr(-40, SCREEN_H);
  d.vx = 3.2f + windStrength * 2; d.vy = 7.5f; d.life = (uint8_t)irandr(3, 9);
}
static void resetFlake(Flake& f) {
  f.x = frandr(0, SCREEN_W); f.y = frandr(-20, SCREEN_H);
  f.sway = frandr(0, PI2); f.spd = frandr(0.4f, 1.1f); f.size = (uint8_t)irandr(1, 4);
  f.life = (uint8_t)irandr(60, 200);
}

static void updateWeatherFx() {
  WeatherCondition wx = effectiveWeather();
  int wantDrops = wx == WEATHER_RAINY ? 80 : (wx == WEATHER_STORMY ? MAX_DROPS : 0);
  int wantFlakes = wx == WEATHER_SNOWY ? MAX_FLAKES : 0;
  float wantWind = wx == WEATHER_STORMY ? 1.0f : (wx == WEATHER_RAINY ? 0.6f : 0.25f);
  windStrength = lerpf(windStrength, wantWind, 0.002f);
  while (numDrops < wantDrops) resetDrop(drops[numDrops++]);
  if (numDrops > wantDrops) numDrops = wantDrops;
  while (numFlakes < wantFlakes) resetFlake(flakes[numFlakes++]);
  if (numFlakes > wantFlakes) numFlakes = wantFlakes;
  for (int i = 0; i < numDrops; ++i) {
    Drop& d = drops[i];
    d.x += d.vx; d.y += d.vy;
    if (d.life-- == 0) resetDrop(d);
  }
  for (int i = 0; i < numFlakes; ++i) {
    Flake& f = flakes[i];
    f.sway += 0.05f; f.x += fastSin(f.sway) * 0.6f + windStrength * 0.4f; f.y += f.spd;
    if (f.life-- == 0 || f.y > SCREEN_H) { resetFlake(f); f.y = frandr(-10, SCREEN_H * 0.6f); }
  }
  if (wx == WEATHER_STORMY && frand() < 1.0f / 320.0f) flashAmt = 1.0f;
  if (wx == WEATHER_STORMY && flashAmt > 0.2f && flashAmt < 0.3f && frand() < 0.3f) flashAmt = 0.8f;
  fogPhase += 0.0025f;
}

static void drawWeatherFx() {
  uint32_t rc = lit(0xC8D6E0u);
  for (int i = 0; i < numDrops; ++i) {
    const Drop& d = drops[i];
    float ty = d.y - d.vy * 1.4f;
    if (!stripHit((int)ty, (int)d.y)) continue;
    uint32_t bg = sceneryRgb(s2wx(d.x), s2wy(d.y));
    fLine((int)d.x, (int)d.y, (int)(d.x - d.vx * 1.4f), (int)ty, mixRgb(bg, rc, 0.55f));
  }
  uint32_t sc = lit(0xF4F8FFu);
  for (int i = 0; i < numFlakes; ++i) {
    const Flake& f = flakes[i];
    if (f.size <= 1) blendPixel((int)f.x, (int)f.y, sc, 26);
    else blendCircle((int)f.x, (int)f.y, f.size - 1, sc, 24);
  }
  if (fogMix > 0.08f) {
    uint32_t fc = mixRgb(fogCol, 0xFFFFFFu, 0.2f);
    uint32_t a = (uint32_t)(fogMix * 9);
    for (int i = 0; i < 2; ++i) {
      float px = SCREEN_W * (0.5f + 0.55f * fastSin(fogPhase * (1.0f + i * 0.6f) + i * 2.1f));
      float py = SCREEN_H * (0.5f + 0.45f * fastCos(fogPhase * (0.8f + i * 0.5f) + i * 1.3f));
      blendCircle((int)px, (int)py, 140 - i * 30, fc, a);
      blendCircle((int)px, (int)py, 80 - i * 20, fc, a);
    }
  }
}

static void initEffects() {
  if (!parts) {
    parts  = (Particle*)heap_caps_malloc(sizeof(Particle) * MAX_PARTS, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    partsR = (Particle*)heap_caps_malloc(sizeof(Particle) * MAX_PARTS, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  }
  for (int i = 0; i < MAX_PARTS; ++i) parts[i].active = partsR[i].active = false;
  numDrops = numFlakes = 0;
}
