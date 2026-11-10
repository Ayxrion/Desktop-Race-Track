/*
 * Race Track — a top-down desktop race track with AI cars
 * Hardware : Elecrow CrowPanel 7.0" HMI (ESP32-S3-WROOM-1-N4R8, 800×480 RGB, GT911 touch)
 * Library  : LovyanGFX 1.2.21 — the FIXED copy (patched esp32s3 Bus_RGB/Panel_RGB)
 *
 * Arduino IDE board settings (Tools menu):
 *   Board            : ESP32S3 Dev Module
 *   PSRAM            : OPI PSRAM
 *   Flash Size       : 4MB (32Mb)
 *   Partition Scheme : Minimal SPIFFS (1.9MB APP with OTA/190KB SPIFFS)
 *
 * Touch: tap a car to follow it · drag to pan · pinch or +/− to zoom
 *        list button = leaderboard on/off · gear = settings
 * Left alone for 30 s, the TV camera takes over.
 */

#include "common.h"
#include "version.h"
#include "wifi_config.h"
#include "ota_update.h"
#include "ui.h"

#include <esp_async_memcpy.h>

// ─── Strip presenter: GDMA copy SRAM strip → PSRAM framebuffer ────────────────
static async_memcpy_handle_t dmaH = nullptr;
static SemaphoreHandle_t     stripFree[2] = { nullptr, nullptr };
static uint8_t*              fbBase = nullptr;
static bool                  useDma = false;

static bool IRAM_ATTR onStripCopied(async_memcpy_handle_t, async_memcpy_event_t*, void* arg) {
  BaseType_t woken = pdFALSE;
  xSemaphoreGiveFromISR((SemaphoreHandle_t)arg, &woken);
  return woken == pdTRUE;
}

static void initPresenter() {
  for (int i = 0; i < 2; ++i) { stripFree[i] = xSemaphoreCreateBinary(); xSemaphoreGive(stripFree[i]); }
  fbBase = display.frameLine(0);
  bool contiguous = fbBase && display.frameLine(1) == fbBase + SCREEN_W * 2 &&
                    display.frameLine(SCREEN_H - 1) == fbBase + (SCREEN_H - 1) * SCREEN_W * 2;
  bool aligned = ((uintptr_t)fbBase & 31) == 0;
  async_memcpy_config_t cfg = ASYNC_MEMCPY_DEFAULT_CONFIG();
  cfg.backlog = 4;
  esp_err_t e = (contiguous && aligned) ? esp_async_memcpy_install(&cfg, &dmaH) : ESP_ERR_INVALID_STATE;
  useDma = (e == ESP_OK);
  Serial.printf("Presenter: fb %p contiguous %d aligned %d -> %s (%s)\n", fbBase, contiguous, aligned,
                useDma ? "GDMA" : "CPU copy", esp_err_to_name(e));
}

// ─── Dirty-row tracking ───────────────────────────────────────────────────────
static uint32_t rowHash[SCREEN_H];
static bool     rowHashValid = false;
static uint32_t rowsCopied = 0;

static inline uint32_t hashRow(const uint32_t* p) {
  uint32_t h = 0x9E3779B9u;
  for (int i = 0; i < SCREEN_W / 2; i += 4) {
    h = ((h << 5) | (h >> 27)) ^ p[i];
    h = ((h << 5) | (h >> 27)) ^ p[i + 1];
    h = ((h << 5) | (h >> 27)) ^ p[i + 2];
    h = ((h << 5) | (h >> 27)) ^ p[i + 3];
  }
  return h;
}

static void presentStrip(int b, int y, int hh) {
  int first = -1, last = -1;
  int h = (y + hh < SCREEN_H ? y + hh : SCREEN_H) - y;
  for (int i = 0; i < h; ++i) {
    uint32_t hh = hashRow((const uint32_t*)(stripBufs[b] + i * SCREEN_W));
    if (!rowHashValid || hh != rowHash[y + i]) { if (first < 0) first = i; last = i; rowHash[y + i] = hh; }
  }
  if (first < 0) return;
  int n = last - first + 1;
  rowsCopied += n;
  if (useDma) {
    xSemaphoreTake(stripFree[b], 0);
    esp_err_t e = esp_async_memcpy(dmaH, fbBase + (size_t)(y + first) * SCREEN_W * 2, stripBufs[b] + first * SCREEN_W,
                                   (size_t)n * SCREEN_W * 2, onStripCopied, stripFree[b]);
    if (e == ESP_OK) return;
    Serial.printf("GDMA copy failed (%s) - falling back to CPU copy\n", esp_err_to_name(e));
    useDma = false;
    xSemaphoreGive(stripFree[b]);
  }
  display.pushImage(0, y + first, SCREEN_W, n, (const lgfx::rgb565_t*)(stripBufs[b] + first * SCREEN_W));
}

static void waitStripFree(int b) {
  if (!useDma) return;
  xSemaphoreTake(stripFree[b], portMAX_DELAY);
  xSemaphoreGive(stripFree[b]);
}

static uint32_t lastFrameMs = 0;
static uint32_t fpsFrames = 0, fpsT0 = 0;
static uint32_t profAcc[9] = {0}, profT = 0;
static const char* const PROF_NAMES[9] = { "touch", "prep", "bg", "cars", "fx", "-", "glow", "ui", "push" };
#define PROF(i) do { uint32_t _t = micros(); profAcc[i] += _t - profT; profT = _t; } while (0)

// ─── Splash screens (drawn straight to the panel) ─────────────────────────────
static void splash(const char* msg, const char* sub = nullptr) {
  display.fillScreen(0x101418u);
  // chequered band
  for (int x = 0; x < SCREEN_W; x += 16)
    for (int r = 0; r < 2; ++r) display.fillRect(x, 96 + r * 16, 16, 16, ((x / 16 + r) & 1) ? 0xF0F0F0u : 0x101418u);
  display.setTextDatum(textdatum_t::middle_center);
  display.setFont(&fonts::DejaVu40);
  display.setTextColor(0xF04A3Au);
  display.drawString("Race Track", SCREEN_CX, 180);
  display.setFont(&fonts::DejaVu18);
  display.setTextColor(0xD8DEE4u);
  display.drawString(msg, SCREEN_CX, 236);
  if (sub) { display.setFont(&fonts::DejaVu12); display.setTextColor(0x9AA6B0u); display.drawString(sub, SCREEN_CX, 266); }
  display.setFont(&fonts::DejaVu12);
  display.setTextColor(0x6A7680u);
  display.drawString("v" FIRMWARE_VERSION, SCREEN_CX, 440);
}

static void splashProgress(float p) {
  const int x = 220, y = 300, w = 360, h = 10;
  display.drawRoundRect(x, y, w, h, 5, 0x4A545Eu);
  display.fillRoundRect(x + 2, y + 2, (int)((w - 4) * clampf(p, 0, 1)), h - 4, 3, 0xF04A3Au);
}

static void fatal(const char* msg) {
  Serial.println(msg);
  splash(msg);
  for (;;) delay(1000);
}

// Build the next circuit and line the cars up on the grid.
static void beginNextRace() {
  int sel; uint32_t seed; uint8_t series;
  pickNextRace(sel, seed, series);
  waitStripFree(0); waitStripFree(1);          // last frame's strips must land before we draw
  splash(sel >= 0 ? PRESETS[sel].name : "Building a new circuit...", sel >= 0 ? PRESETS[sel].place : nullptr);
  uint32_t t0 = millis();
  generateWorld(sel, seed, splashProgress);
  Serial.printf("World generated in %lu ms\n", (unsigned long)(millis() - t0));
  raceLaps = LAPS_OPTS[lapsIdx];
#ifdef RT_QUICK_RACE
  raceLaps = 1;
#endif
  startRace(carSetting, series, seed);
  char sub[64];
  if (raceLaps > 0) snprintf(sub, sizeof(sub), "%s  -  %d cars  -  %d laps", SERIES_NAMES[series], numCars, raceLaps);
  else              snprintf(sub, sizeof(sub), "%s  -  %d cars", SERIES_NAMES[series], numCars);
  splash(trk.name, sub);
  delay(1200);
  cam.zoom = ZOOM_MIN; cam.x = WORLD_W * 0.5f; cam.y = WORLD_H * 0.5f; cam.vx = cam.vy = 0; cam.animZoom = false;
  followCar = -1; tv.until = 0;
  if (millis() - lastTouchMs > AUTOCAM_IDLE_MS) autoCam = true;
  updateLighting(true);
  rowHashValid = false;
}

void setup() {
  Serial.begin(115200);
  _rng = esp_random() | 1u;
  Serial.printf("Race Track v%s\n", FIRMWARE_VERSION);
  Serial.printf("PSRAM total %u, free %u\n", ESP.getPsramSize(), ESP.getFreePsram());

  // Keep display.init() on core 1 (see the koi pond notes: RGB ISR vs WiFi).
  display.init();
  display.setRotation(0);
  display.setBrightness(200);
  loadSettings();
  display.setBrightness(brightness);

  splash("Checking for updates...");
  bool found = false;
  {
    WiFi.mode(WIFI_STA);
    int n = WiFi.scanNetworks();
    for (int i = 0; i < n; ++i) if (WiFi.SSID(i) == WIFI_SSID) {
      found = true;
      Serial.printf("WiFi scan: %d networks; ours on ch %d, RSSI %d\n", n, WiFi.channel(i), WiFi.RSSI(i));
    }
    if (!found) Serial.printf("WiFi scan: %d networks; ours NOT visible\n", n);
    WiFi.scanDelete();
  }
  otaInit(&display);
  if (found) checkForOTAUpdate();
  else { _otaStatus("WiFi network not found - skipping update check"); delay(1200); }

  for (int i = 0; i < 2; ++i) {
    stripBufs[i] = (uint16_t*)heap_caps_aligned_alloc(64, SCREEN_W * STRIP_H * 2,
                                                      MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    if (!stripBufs[i]) fatal("Strip buffer allocation failed (SRAM)");
  }
  canvas.setColorDepth(lgfx::color_depth_t::rgb565_nonswapped);
  beginStrip(0);
  initPresenter();
  if (!allocWorld()) fatal("World allocation failed (PSRAM?)");
  initBgPrefetch();

  initWeather();
  initDayNight();
  lastTouchMs = millis() - AUTOCAM_IDLE_MS - 1;      // start with the TV camera
  beginNextRace();

  startSimTask();
  startTouchTask();
  Serial.printf("Free PSRAM after setup: %u, free heap %u\n", ESP.getFreePsram(), ESP.getFreeHeap());
  lastFrameMs = fpsT0 = millis();
}

// ─── Simulation on core 0 ─────────────────────────────────────────────────────
// The race simulation runs on core 0 (otherwise only WiFi and touch live there)
// while core 1 renders the previous state from a snapshot: car poses (cdraw),
// particles (partsR), the leaderboard sprite and the event text are all copied
// before the sim is kicked. Everything that touches the camera or UI stays on
// core 1 and only runs while the sim is idle.
static SemaphoreHandle_t simKick = nullptr, simDone = nullptr;
static volatile int      simSteps = 0;
static bool              simBusy = false;
static uint32_t          simUs = 0;            // last sim batch duration (stats)
static TaskHandle_t      simHandle = nullptr;

static void simTask(void*) {
  for (;;) {
    xSemaphoreTake(simKick, portMAX_DELAY);
    uint32_t t0 = micros();
    for (int k = 0; k < simSteps; ++k) {
      simTime += FRAME_MS / 1000.0f;
      updateRace(FRAME_MS / 1000.0f);
      updateParticles();
      updateStandFlashes();
    }
    simUs = micros() - t0;
    xSemaphoreGive(simDone);
  }
}

static void startSimTask() {
  simKick = xSemaphoreCreateBinary();
  simDone = xSemaphoreCreateBinary();
  xTaskCreatePinnedToCore(simTask, "sim", 4096, nullptr, 2, &simHandle, 0);   // small stack: TLS needs the heap
}
static void simJoin()          { if (simBusy) { xSemaphoreTake(simDone, portMAX_DELAY); simBusy = false; } }
static void simStart(int steps) { if (steps <= 0) return; simSteps = steps; simBusy = true; xSemaphoreGive(simKick); }

// ─── Which rows need rendering ────────────────────────────────────────────────
// When the camera and palette haven't changed, only rows with something moving
// on them (this frame or last frame, to erase it) are re-rendered; the panel
// keeps the rest from before.
static uint8_t dynNow[SCREEN_H], dynPrev[SCREEN_H];
static void markRows(int y0, int y1) {
  y0 = clampi(y0, 0, SCREEN_H - 1); y1 = clampi(y1, 0, SCREEN_H - 1);
  if (y1 >= y0) memset(dynNow + y0, 1, y1 - y0 + 1);
}
static void markDynamicRows() {
  memset(dynNow, 0, sizeof(dynNow));
  for (int i = 0; i < numCars; ++i) if (cdraw[i].vis) markRows(cdraw[i].y0, cdraw[i].y1);
  const float z = cam.zoom;
  for (int i = 0; i < MAX_PARTS; ++i) {
    const Particle& p = partsR[i];
    if (!p.active) continue;
    float sy = w2sy(p.y), r = p.r * z + 3.0f * (z > 1.2f ? z : 1.0f) + 2.0f;
    markRows((int)(sy - r), (int)(sy + r));
  }
  uiDynamicRows(markRows);
}

struct Chunk { int16_t y, h; };
static Chunk chunks[SCREEN_H / 4];
static uint32_t rowsRendered = 0;

void loop() {
  simJoin();                                   // the sim batch kicked last frame has finished
  if (nextRaceRequested) { beginNextRace(); lastFrameMs = millis(); }

  uint32_t now = millis();
  if (now - lastFrameMs < FRAME_MS) { delay(1); return; }
  lastFrameMs = now;
  ++frameCount;

  profT = micros();
  pollTouch();
  PROF(0);

  // ── Camera and screen-space effects: fixed 30 Hz steps, here on core 1 ──
  static uint32_t simMs = now;
  int steps = 0;
  while ((int32_t)(now - simMs) >= FRAME_MS && steps < 4) {
    simMs += FRAME_MS; ++steps;
    updateTvCam();
    updateCamera();
    updateWeatherFx();
  }
  if ((int32_t)(now - simMs) >= FRAME_MS) simMs = now;
  updateLighting();

  // ── Snapshot what the renderer needs, then let the sim run ahead on core 0 ──
  int camCar = cameraCar();
  prepareBackground();
  prepareCarsFrame();
  prepareUI();
  snapshotParticles();
  markDynamicRows();
  simStart(steps);
  PROF(1);

  // ── Decide which rows to render ──
  static float lastCamX = -1, lastCamY = -1, lastCamZ = -1;
  static bool  lastHud = true;
  bool full = !rowHashValid || palChanged || cam.x != lastCamX || cam.y != lastCamY || cam.zoom != lastCamZ ||
              hudOn != lastHud || numDrops > 0 || numFlakes > 0 || fogMix > 0.08f;
  lastCamX = cam.x; lastCamY = cam.y; lastCamZ = cam.zoom; lastHud = hudOn;
  palChanged = false;
  int nChunks = 0;
  for (int y = 0; y < SCREEN_H && nChunks < (int)(sizeof(chunks) / sizeof(chunks[0])); ) {
    if (!full && !dynNow[y] && !dynPrev[y]) { ++y; continue; }
    int y0 = y;
    while (y < SCREEN_H && y - y0 < stripH) {
      if (full || dynNow[y] || dynPrev[y]) { ++y; continue; }
      int z = y;                                  // bridge small gaps: fewer, larger strips
      while (z < SCREEN_H && z - y < 6 && !dynNow[z] && !dynPrev[z]) ++z;
      if (z < SCREEN_H && z - y < 6 && z - y0 < stripH) y = z; else break;
    }
    chunks[nChunks++] = { (int16_t)y0, (int16_t)(y - y0) };
  }
  memcpy(dynPrev, dynNow, sizeof(dynNow));

  // ── Render, one SRAM strip at a time ──
  if (nChunks > 0) prefetchStrip(chunks[0].y, chunks[0].h);
  if (!useDma) display.startWrite();
  for (int k = 0, b = 0; k < nChunks; ++k, b ^= 1) {
    const int y = chunks[k].y, h = chunks[k].h;
    waitStripFree(b);
    PROF(8);
    beginStrip(y, b, h);
    renderBackground();
    if (k + 1 < nChunks) prefetchStrip(chunks[k + 1].y, chunks[k + 1].h);
    PROF(2);
    drawCarShadows();
    drawHeadlights();
    drawCars(camCar);
    PROF(3);
    drawParticles();
    PROF(4);
    drawLightGlows();
    drawWeatherFx();
    PROF(6);
    drawUI();
    PROF(7);
    presentStrip(b, y, h);
    rowsRendered += h;
    PROF(8);
  }
  if (!useDma) display.endWrite();
  rowHashValid = true;
  PROF(8);

  ++fpsFrames;
  if (now - fpsT0 >= 5000) {
    Serial.printf("fps %.1f  zoom %.2f  cars %d  lap %d  heap %u  psram %u  wifi %d  sim %.1f ms (core 0)\n",
                  fpsFrames * 1000.0f / (now - fpsT0), cam.zoom, numCars, leaderLapDisplay(),
                  ESP.getFreeHeap(), ESP.getFreePsram(), (int)(WiFi.status() == WL_CONNECTED), simUs / 1000.0f);
    Serial.print("  ms/frame:");
    for (int i = 0; i < 9; ++i) { Serial.printf(" %s %.1f", PROF_NAMES[i], profAcc[i] / 1000.0f / fpsFrames); profAcc[i] = 0; }
    Serial.println();
    Serial.printf("  largest free block %u, sim stack spare %u\n", (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                  simHandle ? (unsigned)uxTaskGetStackHighWaterMark(simHandle) : 0u);
    Serial.print("  ui:");
    for (int i = 0; i < 8; ++i) { Serial.printf(" %.1f", uiProf[i] / 1000.0f / fpsFrames); uiProf[i] = 0; }
    Serial.println();
    Serial.printf("  rows rendered/frame: %u, copied: %u of %d\n", (unsigned)(rowsRendered / fpsFrames),
                  (unsigned)(rowsCopied / fpsFrames), SCREEN_H);
    rowsCopied = rowsRendered = 0;
    fpsFrames = 0; fpsT0 = now;
  }
}
