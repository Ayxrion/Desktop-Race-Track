#pragma once
// Touch (tap a car to follow it, drag to pan, pinch to zoom), on-screen buttons,
// the settings panel, the leaderboard / timing HUD, start lights, results, the
// event ticker, and the TV camera that runs when nobody is touching the screen.

#include <WiFi.h>
#include "champ.h"
#include "version.h"

#define UI_HIDE_MS        15000UL
#define AUTOCAM_IDLE_MS   30000UL
#define TAP_MAX_MS        450
#define TAP_SLOP          12

static bool     panelOpen = false;
static uint32_t lastTouchMs = 0;
static bool     autoCam = true;
static int      followCar = -1;              // car the camera is locked to (tap / TV)

// ─── Toast (UI messages) ──────────────────────────────────────────────────────
static char     toastMsg[48] = "";
static uint32_t toastUntil = 0;
static void toast(const char* m, uint32_t ms = 1800) {
  strncpy(toastMsg, m, sizeof(toastMsg) - 1); toastMsg[sizeof(toastMsg) - 1] = 0; toastUntil = millis() + ms;
}

// ─── Layout ───────────────────────────────────────────────────────────────────
struct Rect { int16_t x, y, w, h; bool hit(int px, int py) const { return px >= x && px < x + w && py >= y && py < y + h; } };

enum UiId : int8_t {
  UI_NONE = -1, UI_ZOOM_OUT = 0, UI_ZOOM_IN, UI_HUD, UI_SETTINGS, UI_SPECTATE,
  UI_P_SLIDER, UI_P_WX_PREV, UI_P_WX_NEXT, UI_P_TIME_PREV, UI_P_TIME_NEXT, UI_P_CARS_MINUS, UI_P_CARS_PLUS,
  UI_P_TRACK_PREV, UI_P_TRACK_NEXT, UI_P_LAPS_PREV, UI_P_LAPS_NEXT, UI_P_SER_PREV, UI_P_SER_NEXT,
  UI_P_CAM_PREV, UI_P_CAM_NEXT, UI_P_SPEC_PREV, UI_P_SPEC_NEXT,
  UI_P_NEXT_RACE, UI_P_CLOSE, UI_P_DRIVERS, UI_P_RECORDS, UI_COUNT
};

#define NUM_TOP_BTN 5
// zoom out, zoom in, leaderboard, settings, spectator (TV) — the TV button sits leftmost
static const Rect BTN[NUM_TOP_BTN] = { { 572, 8, 50, 46 }, { 630, 8, 50, 46 }, { 688, 8, 50, 46 }, { 746, 8, 46, 46 }, { 514, 8, 50, 46 } };

#define PANEL_X 150
#define PANEL_Y 7
#define PANEL_W 500
#define PANEL_H 466
#define PANEL_ROWS 8                         // arrow rows (row 0 is the brightness slider)
#define ROW_Y(i) (PANEL_Y + 50 + (i) * 39)
#define CTRL_X   (PANEL_X + 190)
#define CTRL_R   (PANEL_X + PANEL_W - 64)
static const Rect P_SLIDER  = { CTRL_X, ROW_Y(0) - 4, 270, 38 };
static const Rect P_ARROW_L(int row) { return { CTRL_X, (int16_t)(ROW_Y(row) - 2), 44, 34 }; }
static const Rect P_ARROW_R(int row) { return { CTRL_R, (int16_t)(ROW_Y(row) - 2), 44, 34 }; }
static const Rect P_NEXT_RACE = { PANEL_X + 24, PANEL_Y + PANEL_H - 52, 150, 40 };
static const Rect P_DRIVERS   = { PANEL_X + 184, PANEL_Y + PANEL_H - 52, 100, 40 };
static const Rect P_RECORDS   = { PANEL_X + 294, PANEL_Y + PANEL_H - 52, 100, 40 };
static const Rect P_CLOSE     = { PANEL_X + PANEL_W - 96, PANEL_Y + PANEL_H - 52, 72, 40 };

// Rows top to bottom: which control pair each row is (prev/next ids are adjacent).
static const int8_t ROW_ID[PANEL_ROWS + 1] = { UI_NONE, UI_P_WX_PREV, UI_P_TIME_PREV, UI_P_CARS_MINUS, UI_P_TRACK_PREV,
                                               UI_P_SER_PREV, UI_P_LAPS_PREV, UI_P_CAM_PREV, UI_P_SPEC_PREV };

// ─── Drivers page and its name keyboard ──────────────────────────────────────
// panelPage 0 = settings, 1 = drivers, 2 = editing a driver's name, 3 = career records.
static int  panelPage = 0;
static int  editSlot = 0;
static char editBuf[4] = "";
enum : int { UI_D_TOGGLE = 100, UI_D_COPY, UI_D_BACK, UI_D_SEASON, UI_D_NAME0 = 110,
             UI_K_KEY0 = 200, UI_K_DEL = 250, UI_K_CANCEL, UI_K_OK };
static const char KEYS[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
#define NUM_KEYS 36
static const Rect D_TOGGLE = { PANEL_X + PANEL_W - 190, PANEL_Y + 10, 166, 34 };
static Rect dSlotName(int k)  { int col = k / 10, row = k % 10; return { (int16_t)(PANEL_X + 16 + col * 240 + 26), (int16_t)(PANEL_Y + 78 + row * 31), 62, 27 }; }
static Rect kKey(int i)       { int col = i % 9, row = i / 9; return { (int16_t)(PANEL_X + 27 + col * 50), (int16_t)(PANEL_Y + 112 + row * 48), 46, 42 }; }
static const Rect K_DEL = { PANEL_X + 24, PANEL_Y + 320, 130, 40 };
static const Rect PANEL       = { PANEL_X, PANEL_Y, PANEL_W, PANEL_H };

static uint32_t nextRaceArmUntil = 0, seasonArmUntil = 0;

static bool uiVisible() { return panelOpen || (millis() - lastTouchMs < UI_HIDE_MS); }

static int hitTopButton(int x, int y) {
  if (!uiVisible()) return UI_NONE;
  for (int i = 0; i < NUM_TOP_BTN; ++i) if (BTN[i].hit(x, y)) return i;
  return UI_NONE;
}

static int hitPanel(int x, int y) {
  if (panelPage == 1) {
    if (D_TOGGLE.hit(x, y)) return UI_D_TOGGLE;
    for (int k = 0; k < MAX_CARS; ++k) {
      if (dSlotName(k).hit(x, y)) return UI_D_NAME0 + k;
    }
    if (P_NEXT_RACE.hit(x, y)) return UI_D_COPY;
    if (P_DRIVERS.hit(x, y)) return UI_D_SEASON;
    if (P_CLOSE.hit(x, y)) return UI_D_BACK;
    return UI_NONE;
  }
  if (panelPage == 3) return P_CLOSE.hit(x, y) ? UI_D_BACK : UI_NONE;
  if (panelPage == 2) {
    for (int i = 0; i < NUM_KEYS; ++i) if (kKey(i).hit(x, y)) return UI_K_KEY0 + i;
    if (K_DEL.hit(x, y)) return UI_K_DEL;
    if (P_NEXT_RACE.hit(x, y)) return UI_K_CANCEL;
    if (P_CLOSE.hit(x, y)) return UI_K_OK;
    return UI_NONE;
  }
  if (P_SLIDER.hit(x, y)) return UI_P_SLIDER;
  for (int row = 1; row <= PANEL_ROWS; ++row) {
    if (P_ARROW_L(row).hit(x, y)) return ROW_ID[row];
    if (P_ARROW_R(row).hit(x, y)) return ROW_ID[row] + 1;
  }
  if (P_NEXT_RACE.hit(x, y)) return UI_P_NEXT_RACE;
  if (P_DRIVERS.hit(x, y)) return UI_P_DRIVERS;
  if (P_RECORDS.hit(x, y)) return UI_P_RECORDS;
  if (P_CLOSE.hit(x, y)) return UI_P_CLOSE;
  return UI_NONE;
}

// ─── Drawing helpers ──────────────────────────────────────────────────────────
static void blendRoundRect(int x, int y, int w, int h, int r, uint32_t col, uint32_t a) {
  if (!stripHit(y, y + h)) return;
  uint16_t c = rgbTo565(col);
  int yy0 = clipY0 - y > 0 ? clipY0 - y : 0, yy1 = clipY1 - y < h ? clipY1 - y : h;
  for (int yy = yy0; yy < yy1; ++yy) {
    int inset = 0;
    int dy = yy < r ? r - yy : (yy >= h - r ? yy - (h - r - 1) : 0);
    if (dy > 0) inset = r - (int)sqrtf((float)(r * r - dy * dy));
    blendSpan(y + yy, x + inset, x + w - 1 - inset, c, a);
  }
}

static void drawButtonBg(const Rect& r, bool active, bool pressed) {
  uint32_t bg = active ? 0xB0302Au : 0x101418u;
  blendRoundRect(r.x, r.y, r.w, r.h, 10, bg, pressed ? 30 : 22);
  canvas.drawRoundRect(r.x, r.y, r.w, r.h, 10, active ? 0xF0A0A0u : 0x7A8088u);
}

static void drawText(const char* s, int x, int y, uint32_t col, const lgfx::IFont* font, textdatum_t datum) {
  if (!stripHit(y - 16, y + 16)) return;
  canvas.setFont(font);
  canvas.setTextDatum(datum);
  canvas.setTextColor(col);
  canvas.drawString(s, x, y);
}

static void iconMinus(int cx, int cy, uint32_t c) { canvas.fillRoundRect(cx - 11, cy - 2, 22, 5, 2, c); }
static void iconPlus(int cx, int cy, uint32_t c)  { iconMinus(cx, cy, c); canvas.fillRoundRect(cx - 2, cy - 11, 5, 22, 2, c); }
static void iconList(int cx, int cy, uint32_t c) {
  for (int k = -1; k <= 1; ++k) { canvas.fillRect(cx - 12, cy + k * 7 - 2, 4, 4, c); canvas.fillRoundRect(cx - 5, cy + k * 7 - 2, 17, 4, 1, c); }
}
static void iconTv(int cx, int cy, uint32_t c, uint32_t hole) {
  wideLine(cx - 7, cy - 14, cx - 1, cy - 7, 2.0f, c);
  wideLine(cx + 7, cy - 14, cx + 1, cy - 7, 2.0f, c);
  canvas.fillRoundRect(cx - 14, cy - 7, 28, 19, 4, c);
  canvas.fillRoundRect(cx - 11, cy - 4, 22, 13, 2, hole);
}
static void iconGear(int cx, int cy, uint32_t c, uint32_t hole) {
  for (int k = 0; k < 8; ++k) {
    float a = k * PI / 4;
    wideLine(cx + (int)(cosf(a) * 8), cy + (int)(sinf(a) * 8), cx + (int)(cosf(a) * 13), cy + (int)(sinf(a) * 13), 2.6f, c);
  }
  fCircle(cx, cy, 10, c);
  fCircle(cx, cy, 4, hole);
}

// ─── Panel actions ────────────────────────────────────────────────────────────
static void applyBrightnessFromX(int x) {
  float t = clampf((x - P_SLIDER.x - 10) / (float)(P_SLIDER.w - 20), 0, 1);
  brightness = (uint8_t)(20 + t * 235);
  display.setBrightness(brightness);
}

static inline int cyc(int v, int n) { return ((v % n) + n) % n; }

static void cutToAutoCam();                    // defined with the TV camera

static void setSpectator(bool on) {
  spectator = on;
  if (on) cutToAutoCam();
  char b[48];
  if (on) snprintf(b, sizeof(b), "Spectator: %s", CAM_MODE_NAMES[camMode]);
  else    snprintf(b, sizeof(b), "Spectator off - touch to take over");
  toast(b, 2200);
}

static void closePanel() { panelOpen = false; panelPage = 0; saveSettings(); }

static void driversAction(int id) {
  if (id == UI_D_TOGGLE) {
    customDrivers = !customDrivers;
    toast(customDrivers ? "Custom drivers: from the next race" : "Standard drivers: from the next race", 2200);
  } else if (id == UI_D_SEASON) {                     // two taps: it wipes the standings
    if (millis() < seasonArmUntil) { champReset(); seasonArmUntil = 0; toast("New season from the next race", 2200); }
    else { seasonArmUntil = millis() + 3000; toast("Tap again to wipe the standings", 2600); }
  } else if (id == UI_D_COPY) {                       // start from the field that's racing now
    for (int i = 0; i < numCars && i < MAX_CARS; ++i) { memcpy(customCode[i], cars[i].code, 4); customCode[i][3] = 0; }
    toast("Copied this race's drivers", 1800);
  } else if (id == UI_D_BACK) {
    panelPage = 0; saveSettings();
  } else if (id >= UI_D_NAME0 && id < UI_D_NAME0 + MAX_CARS) {
    editSlot = id - UI_D_NAME0;
    memcpy(editBuf, customCode[editSlot], 4); editBuf[3] = 0;
    panelPage = 2;
  } else if (id >= UI_K_KEY0 && id < UI_K_KEY0 + NUM_KEYS) {
    size_t n = strlen(editBuf);
    if (n < 3) { editBuf[n] = KEYS[id - UI_K_KEY0]; editBuf[n + 1] = 0; }
  } else if (id == UI_K_DEL) {
    size_t n = strlen(editBuf);
    if (n > 0) editBuf[n - 1] = 0;
  } else if (id == UI_K_CANCEL) {
    panelPage = 1;
  } else if (id == UI_K_OK) {
    if (editBuf[0]) { memcpy(customCode[editSlot], editBuf, 4); customCode[editSlot][3] = 0; }
    panelPage = 1;
  }
}

static void panelAction(int id) {
  if (id >= 100) { driversAction(id); return; }
  switch (id) {
    case UI_P_DRIVERS: panelPage = 1; break;
    case UI_P_RECORDS: panelPage = 3; break;
    case UI_P_WX_PREV: case UI_P_WX_NEXT: {
      int v = weatherOverride + (id == UI_P_WX_NEXT ? 1 : -1);
      if (v < -1) v = WEATHER_COUNT - 1;
      if (v >= WEATHER_COUNT) v = -1;
      weatherOverride = (int8_t)v;
      if (v < 0) forceWeatherRefetch();
      break;
    }
    case UI_P_TIME_PREV: case UI_P_TIME_NEXT:
      currentTimeMode = (TimeMode)cyc((int)currentTimeMode + (id == UI_P_TIME_NEXT ? 1 : -1), TIME_MODE_COUNT);
      break;
    case UI_P_CARS_MINUS: case UI_P_CARS_PLUS:
      carSetting = (uint8_t)clampi(carSetting + (id == UI_P_CARS_PLUS ? 1 : -1), 4, MAX_CARS);
      toast("Cars: from the next race");
      break;
    case UI_P_TRACK_PREV: case UI_P_TRACK_NEXT:
      trackMode = (uint8_t)cyc(trackMode + (id == UI_P_TRACK_NEXT ? 1 : -1), TRACK_MODE_COUNT);
      break;
    case UI_P_LAPS_PREV: case UI_P_LAPS_NEXT:
      lapsIdx = (uint8_t)cyc(lapsIdx + (id == UI_P_LAPS_NEXT ? 1 : -1), LAPS_OPT_COUNT);
      raceLaps = LAPS_OPTS[lapsIdx];          // applies to the current race too
      break;
    case UI_P_SER_PREV: case UI_P_SER_NEXT:
      seriesSel = (uint8_t)cyc(seriesSel + (id == UI_P_SER_NEXT ? 1 : -1), SERIES_SEL_COUNT);
      break;
    case UI_P_CAM_PREV: case UI_P_CAM_NEXT:
      camMode = (uint8_t)cyc(camMode + (id == UI_P_CAM_NEXT ? 1 : -1), CAM_MODE_COUNT);
      cutToAutoCam();                         // show the new camera straight away
      break;
    case UI_P_SPEC_PREV: case UI_P_SPEC_NEXT:
      setSpectator(!spectator);
      break;
    case UI_P_NEXT_RACE:
      if (millis() < nextRaceArmUntil) {
        saveSettings();
        nextRaceRequested = true;
        panelOpen = false;
        nextRaceArmUntil = 0;
      } else {
        nextRaceArmUntil = millis() + 3000;
      }
      break;
    case UI_P_CLOSE: closePanel(); break;
    default: break;
  }
}

static void topButtonAction(int id) {
  switch (id) {
    case UI_ZOOM_OUT: if (!spectator) { autoCam = false; startZoomStep(-1); } break;
    case UI_ZOOM_IN:  if (!spectator) { autoCam = false; startZoomStep(+1); } break;
    case UI_HUD:      hudOn = !hudOn; saveSettings(); break;
    case UI_SETTINGS: if (panelOpen) closePanel(); else { panelOpen = true; panelPage = 0; } break;
    case UI_SPECTATE: setSpectator(!spectator); saveSettings(); break;
  }
}

static void onTap(int x, int y) {
  float wx = s2wx(x), wy = s2wy(y);
  int c = nearestCar(wx, wy, 22.0f / cam.zoom + 6.0f);
  if (c >= 0) {
    followCar = c; autoCam = false;
    if (cam.zoom < 1.2f) { cam.targetZoom = 1.6f; cam.animZoom = true; cam.anchorSX = SCREEN_CX; cam.anchorSY = SCREEN_CY; }
    char b[48]; snprintf(b, sizeof(b), "Following P%d %s  (%s)", cars[c].pos, cars[c].code, CLS[cars[c].cls].name);
    toast(b, 2500);
  } else if (followCar >= 0) {
    followCar = -1; toast("Free camera");
  }
}

// ─── Touch state machine ──────────────────────────────────────────────────────
enum TouchMode : uint8_t { TM_NONE, TM_PENDING, TM_PAN, TM_PINCH, TM_UI, TM_PANEL, TM_SLIDER };

struct TouchState {
  TouchMode mode = TM_NONE;
  int16_t sx = 0, sy = 0, lx = 0, ly = 0;
  uint32_t t0 = 0;
  int uiId = UI_NONE;
  float pinchD0 = 1, pinchZ0 = 1, pinchWX = 0, pinchWY = 0;
  float vx = 0, vy = 0;
};
static TouchState ts;
static int   effN = 0, lowCount = 0;
static int16_t tpx[2] = { 0, 0 }, tpy[2] = { 0, 0 };

static void onDown(int x, int y) {
  ts.sx = ts.lx = x; ts.sy = ts.ly = y; ts.t0 = millis();
  ts.uiId = UI_NONE; ts.vx = ts.vy = 0;
  cam.vx = cam.vy = 0;
  if (panelOpen) {
    int id = hitTopButton(x, y);
    if (id == UI_SETTINGS) { ts.mode = TM_UI; ts.uiId = id; return; }
    if (!PANEL.hit(x, y)) { closePanel(); ts.mode = TM_UI; ts.uiId = UI_NONE; return; }
    id = hitPanel(x, y);
    if (id == UI_P_SLIDER) { ts.mode = TM_SLIDER; applyBrightnessFromX(x); return; }
    ts.mode = TM_PANEL; ts.uiId = id;
    return;
  }
  int id = hitTopButton(x, y);
  if (id != UI_NONE) { ts.mode = TM_UI; ts.uiId = id; return; }
  if (spectator) {                              // camera is locked to the TV feed
    ts.mode = TM_UI; ts.uiId = UI_NONE;
    if (millis() > toastUntil) toast("Spectator mode - tap the TV button to take over", 2200);
    return;
  }
  ts.mode = TM_PENDING;
}

static void onMove(int x, int y) {
  int dx = x - ts.lx, dy = y - ts.ly;
  switch (ts.mode) {
    case TM_PENDING:
      if (abs(x - ts.sx) > TAP_SLOP || abs(y - ts.sy) > TAP_SLOP) { ts.mode = TM_PAN; followCar = -1; }
      break;
    case TM_PAN:
      cam.x -= dx / cam.zoom; cam.y -= dy / cam.zoom; clampCamera();
      ts.vx = ts.vx * 0.5f + (-dx / cam.zoom) * 0.5f; ts.vy = ts.vy * 0.5f + (-dy / cam.zoom) * 0.5f;
      cam.animZoom = false;
      break;
    case TM_SLIDER: applyBrightnessFromX(x); break;
    default: break;
  }
  ts.lx = x; ts.ly = y;
}

static void onUp() {
  switch (ts.mode) {
    case TM_PENDING: if (millis() - ts.t0 < TAP_MAX_MS) onTap(ts.sx, ts.sy); break;
    case TM_PAN: cam.vx = ts.vx; cam.vy = ts.vy; break;
    case TM_UI: if (ts.uiId != UI_NONE && BTN[ts.uiId].hit(ts.lx, ts.ly)) topButtonAction(ts.uiId); break;
    case TM_PANEL: { int id = hitPanel(ts.lx, ts.ly); if (id != UI_NONE && id == ts.uiId) panelAction(id); break; }
    case TM_SLIDER: saveSettings(); break;
    default: break;
  }
  ts.mode = TM_NONE;
}

static void startPinch() {
  float dx = tpx[1] - tpx[0], dy = tpy[1] - tpy[0];
  ts.pinchD0 = fmaxf(20.0f, sqrtf(dx * dx + dy * dy));
  ts.pinchZ0 = cam.zoom;
  float mx = (tpx[0] + tpx[1]) * 0.5f, my = (tpy[0] + tpy[1]) * 0.5f;
  ts.pinchWX = s2wx(mx); ts.pinchWY = s2wy(my);
  ts.mode = TM_PINCH;
  cam.animZoom = false; cam.vx = cam.vy = 0;
  followCar = -1;
}

static void updatePinch() {
  float dx = tpx[1] - tpx[0], dy = tpy[1] - tpy[0];
  float d = fmaxf(20.0f, sqrtf(dx * dx + dy * dy));
  float mx = (tpx[0] + tpx[1]) * 0.5f, my = (tpy[0] + tpy[1]) * 0.5f;
  cam.zoom = clampf(ts.pinchZ0 * d / ts.pinchD0, ZOOM_MIN, ZOOM_MAX);
  cam.x = ts.pinchWX - (mx - SCREEN_CX) / cam.zoom;
  cam.y = ts.pinchWY - (my - SCREEN_CY) / cam.zoom;
  clampCamera();
}

// GT911 polled on its own core-0 task (see the koi pond notes).
static portMUX_TYPE   _touchMux = portMUX_INITIALIZER_UNLOCKED;
static volatile uint8_t _touchN = 0;
static int16_t        _touchX[2], _touchY[2];

static void _touchTask(void*) {
  for (;;) {
    lgfx::touch_point_t tp2[2];
    int n = display.getTouch(tp2, 2);
    portENTER_CRITICAL(&_touchMux);
    _touchN = (uint8_t)n;
    for (int i = 0; i < n && i < 2; ++i) { _touchX[i] = tp2[i].x; _touchY[i] = tp2[i].y; }
    portEXIT_CRITICAL(&_touchMux);
    vTaskDelay(pdMS_TO_TICKS(15));
  }
}
static void startTouchTask() { xTaskCreatePinnedToCore(_touchTask, "touch", 4096, nullptr, 2, nullptr, 0); }

static void pollTouch() {
  int raw;
  portENTER_CRITICAL(&_touchMux);
  raw = _touchN;
  for (int i = 0; i < raw && i < 2; ++i) { tpx[i] = _touchX[i]; tpy[i] = _touchY[i]; }
  portEXIT_CRITICAL(&_touchMux);
  int prevN = effN;
  if (raw >= effN) { effN = raw; lowCount = 0; }
  else if (++lowCount >= 2) { effN = raw; lowCount = 0; }
  if (effN > 2) effN = 2;
  if (effN > 0) {
    lastTouchMs = millis();
    if (!spectator && (ts.mode == TM_PENDING || ts.mode == TM_PAN || ts.mode == TM_PINCH)) autoCam = false;
  }

  if (prevN == 0 && effN >= 1) {
    onDown(tpx[0], tpy[0]);
    if (effN == 2 && (ts.mode == TM_PENDING || ts.mode == TM_PAN)) startPinch();
  } else if (prevN >= 1 && effN == 0) {
    onUp();
  } else if (prevN == 1 && effN == 2) {
    if (ts.mode == TM_PENDING || ts.mode == TM_PAN) startPinch();
  } else if (prevN == 2 && effN == 1) {
    if (ts.mode == TM_PINCH) { ts.mode = TM_PAN; ts.lx = tpx[0]; ts.ly = tpy[0]; ts.vx = ts.vy = 0; }
  } else if (effN == 2 && ts.mode == TM_PINCH) {
    updatePinch();
  } else if (effN >= 1) {
    onMove(tpx[0], tpy[0]);
  }
}

// ─── TV camera ────────────────────────────────────────────────────────────────
// camMode picks which shots it takes: the director mixes them all (and cuts to
// spins), the others stick to one kind of shot.
enum TvMode : uint8_t { TV_OVERVIEW, TV_BATTLE, TV_LEADER, TV_CORNER, TV_EVENT, TV_GRID, TV_FOLLOW };
static struct { TvMode mode; int car; int car2; uint32_t until, since; float tx, ty, tz; int stand; } tv = { TV_OVERVIEW, 0, -1, 0, 0, WORLD_W / 2, WORLD_H / 2, ZOOM_MIN, -1 };

static void cutToAutoCam() {
  autoCam = true; followCar = -1; tv.until = 0;
  cam.animZoom = false; cam.vx = cam.vy = 0;
}

// A random car that's still racing, preferably not the one we're on.
static int pickRandomDriver(int avoid) {
  int pick = -1, seen = 0;
  for (int i = 0; i < numCars; ++i) {
    if (i == avoid || cars[i].finished || cars[i].state != CS_RACE) continue;
    if (irandr(0, ++seen) == 0) pick = i;      // reservoir sample
  }
  return pick >= 0 ? pick : (avoid >= 0 && avoid < numCars ? avoid : order[0]);
}

// The closest fight on track: returns the chasing car, sets `front`.
static int findBattle(int& front) {
  int best = -1; float bestScore = 1e9f; front = -1;
  for (int k = 1; k < numCars; ++k) {
    const Car& b = cars[order[k]]; const Car& a = cars[order[k - 1]];
    if (b.state != CS_RACE || a.state != CS_RACE || b.finished) continue;
    float gap = a.dist - b.dist;
    if (gap <= 0 || gap > 45) continue;
    float score = gap + k * 1.5f;
    if (score < bestScore) { bestScore = score; best = order[k]; front = order[k - 1]; }
  }
  return best;
}

// Ease the camera toward a car, aiming a little ahead of it.
static void camChase(const Car& c, float tz, const Car* other = nullptr) {
  float tx = c.x + (c.x - c.px) * 7.0f, ty = c.y + (c.y - c.py) * 7.0f;
  if (other) { tx = (tx + other->x + (other->x - other->px) * 7.0f) * 0.5f; ty = (ty + other->y + (other->y - other->py) * 7.0f) * 0.5f; }
  cam.zoom = lerpf(cam.zoom, tz, 0.025f);
  cam.x = lerpf(cam.x, tx, 0.14f);
  cam.y = lerpf(cam.y, ty, 0.14f);
  clampCamera();
}

static void pickTvShot(uint32_t now) {
  TvMode prev = tv.mode;
  int prevCar = tv.car;
  switch (camMode) {
    case CAM_LEADER: tv.mode = TV_LEADER; break;
    case CAM_BATTLE: tv.mode = TV_BATTLE; break;
    case CAM_DRIVER: tv.mode = TV_FOLLOW; break;
    case CAM_STANDS: tv.mode = trk.nStands > 0 ? TV_CORNER : TV_OVERVIEW; break;
    case CAM_TRACK:  tv.mode = TV_OVERVIEW; break;
    default:
      if (phase == RP_RESULTS || phase == RP_FINISH) {
        tv.mode = (prev == TV_OVERVIEW || phase == RP_FINISH) ? TV_LEADER : TV_OVERVIEW;
      } else if (prev == TV_GRID) {
        tv.mode = TV_LEADER;
      } else {
        float r = frand();
        tv.mode = r < 0.26f ? TV_OVERVIEW : (r < 0.56f ? TV_BATTLE : (r < 0.70f ? TV_LEADER : (r < 0.82f ? TV_FOLLOW : TV_CORNER)));
        if (tv.mode == prev && prev == TV_OVERVIEW) tv.mode = TV_BATTLE;
        if (tv.mode == TV_CORNER && trk.nStands == 0) tv.mode = TV_BATTLE;
      }
      break;
  }
  tv.since = now;
  switch (tv.mode) {
    case TV_OVERVIEW: tv.tz = ZOOM_MIN; tv.tx = WORLD_W / 2; tv.ty = WORLD_H / 2; tv.until = now + irandr(18000, 30000); break;
    case TV_BATTLE:
      tv.car = findBattle(tv.car2);
      tv.tz = frandr(1.6f, 2.2f); tv.until = now + irandr(14000, 24000);
      if (tv.car < 0) {                        // nobody close: show the leader meanwhile
        tv.mode = TV_LEADER; tv.car = order[0]; tv.car2 = -1;
        if (camMode == CAM_BATTLE) tv.until = now + 6000;   // and look again soon
      }
      break;
    case TV_LEADER: tv.car = order[0]; tv.car2 = -1; tv.tz = frandr(1.4f, 1.9f); tv.until = now + irandr(10000, 18000); break;
    case TV_FOLLOW:
      tv.car = pickRandomDriver(prev == TV_FOLLOW ? prevCar : -1); tv.car2 = -1;
      tv.tz = frandr(1.5f, 2.1f); tv.until = now + irandr(22000, 38000);
      break;
    case TV_CORNER: {
      int k = irandr(0, trk.nStands);
      if (trk.nStands > 1 && k == tv.stand) k = (k + 1) % trk.nStands;
      tv.stand = k;
      const Stand& st = trk.stands[k];
      trackPoint(st.sc, (st.side ? 1.0f : -1.0f) * (st.d0 * 0.5f), tv.tx, tv.ty);
      tv.tz = frandr(1.3f, 1.7f); tv.until = now + irandr(12000, 18000);
      break;
    }
    default: break;
  }
}

static void updateTvCam() {
  uint32_t now = millis();
  if (!autoCam) {
    if (followCar >= 0 && followCar < numCars) { camChase(cars[followCar], cam.animZoom ? cam.targetZoom : cam.zoom); return; }
    if (!panelOpen && ts.mode == TM_NONE && now - lastTouchMs > AUTOCAM_IDLE_MS) { autoCam = true; tv.until = 0; followCar = -1; }
    else return;
  }
  bool gridShot = camMode != CAM_STANDS && camMode != CAM_TRACK;
  if (phase == RP_GRID && gridShot && tv.mode != TV_GRID) { tv.mode = TV_GRID; tv.since = now; tv.until = now + 120000; tv.tz = 1.7f; }
  if (spinCamCar >= 0) {                        // replays of spins / contact: director only
    if (camMode == CAM_DIRECTOR && tv.mode != TV_EVENT && tv.mode != TV_GRID) { tv.mode = TV_EVENT; tv.car = spinCamCar; tv.car2 = -1; tv.tz = 2.0f; tv.since = now; tv.until = now + 9000; }
    spinCamCar = -1;
  }
  if (tv.mode == TV_LEADER) tv.car = order[0];  // stay on whoever leads now
  if (camMode == CAM_BATTLE && tv.mode == TV_BATTLE && now - tv.since > 5000 &&
      (tv.car2 < 0 || fabsf(cars[tv.car2].dist - cars[tv.car].dist) > 80)) tv.until = 0;   // fight's over: find another
  if (tv.mode == TV_FOLLOW && tv.car >= 0 && cars[tv.car].finished && phase == RP_RACE && now - tv.since > 3000) tv.until = 0;
  if (now > tv.until || (tv.mode == TV_GRID && phase != RP_GRID)) pickTvShot(now);

  switch (tv.mode) {
    case TV_GRID: {                                   // pan slowly up the grid toward pole
      float back = gridSlotS(numCars - 1), front = gridSlotS(0) + 20;
      float u = clampf(phaseT / fmx(goAt - 1.0f, 1.0f), 0, 1);
      float x, y; trackPoint(lerpf(back, front, smooth01(u)), 0, x, y);
      cam.animZoom = false;
      cam.zoom = lerpf(cam.zoom, tv.tz, 0.03f);
      cam.x = lerpf(cam.x, x, 0.06f); cam.y = lerpf(cam.y, y, 0.06f);
      clampCamera();
      break;
    }
    case TV_BATTLE:
      if (tv.car >= 0) {
        const Car* other = (tv.car2 >= 0 && fabsf(cars[tv.car2].dist - cars[tv.car].dist) < 60) ? &cars[tv.car2] : nullptr;
        camChase(cars[tv.car], tv.tz, other);
      }
      break;
    case TV_LEADER: case TV_EVENT: case TV_FOLLOW:
      if (tv.car >= 0) camChase(cars[tv.car], tv.tz);
      break;
    default:
      cam.animZoom = false;
      cam.zoom = lerpf(cam.zoom, tv.tz, 0.02f);
      cam.x = lerpf(cam.x, tv.tx, 0.03f);
      cam.y = lerpf(cam.y, tv.ty, 0.03f);
      if (fabsf(cam.zoom - tv.tz) < 0.0008f) cam.zoom = tv.tz;     // settle exactly, so the
      if (fabsf(cam.x - tv.tx) < 0.08f) cam.x = tv.tx;             // renderer can skip static rows
      if (fabsf(cam.y - tv.ty) < 0.08f) cam.y = tv.ty;
      clampCamera();
      break;
  }
}

// The car the camera is on, for the highlight ring and name tag.
static int cameraCar() {
  if (followCar >= 0) return followCar;
  if (autoCam && (tv.mode == TV_BATTLE || tv.mode == TV_LEADER || tv.mode == TV_EVENT || tv.mode == TV_FOLLOW) && cam.zoom > 1.1f) return tv.car;
  return -1;
}

// ─── HUD ──────────────────────────────────────────────────────────────────────
#define LB_X 6
#define LB_Y 6
#define LB_W 124
#define LB_ROW 13
#define LB_HEAD 36

// The leaderboard's text is drawn a few times a second into a cached 4-bit
// palette sprite (~20 KB, in PSRAM), then copied into each strip that
// overlaps it. Car colour swatches are drawn directly, since 20 liveries don't
// fit a 16-colour palette. If the 4-bit sprite can't be made (or its pixel
// layout isn't what we expect), it falls back to a 16-bit sprite in PSRAM.
enum : uint8_t { LBC_KEY = 0, LBC_WHITE, LBC_LIGHT, LBC_DIM, LBC_GAP, LBC_GOLD, LBC_ORANGE, LBC_PROTO, LBC_GT,
                 LBC_FL, LBC_DMG, LBC_OUT, LBC_COUNT };
static const uint32_t LB_PAL[LBC_COUNT] = { 0x000000u, 0xFFFFFFu, 0xC8D0D8u, 0xA8B4BCu, 0xE0E4E8u, 0xF0C040u, 0xFFA040u,
                                            0x60C0FFu, 0xFFB040u, 0xC890FFu, 0xFF5A48u, 0x6A7680u };

static LGFX_Sprite lbSpr(&display);
static bool     lbOk = false, lbTried = false, lb4 = false, lbHiFirst = true;
static int      lbStride = 0;                 // bytes per row (4-bit mode)
static uint16_t lbPal565[16];
static uint16_t lbKey = 0;                    // 16-bit fallback mode
static int      lbH = 0;
static uint32_t lbBuiltMs = 0;
static bool     lbDirty = true;               // changed since last frame (forces those rows to re-render)
static int      lbOrd[MAX_CARS];              // order and swatch colours at the last rebuild
static uint16_t lbSw[MAX_CARS];
static int      lbN = 0, lbCamC = -1;
#define LB_SPR_H (LB_HEAD + MAX_CARS * LB_ROW + 34)

// The sprite's text as runs of same-coloured pixels, rebuilt with the sprite.
// Most of the sprite is transparent, so a frame only writes the text pixels
// instead of unpacking ~20 KB of nibbles from PSRAM.
struct LbRun { uint8_t x, len, c; };
#define LB_MAX_RUNS 6000
static LbRun*    lbRuns = nullptr;            // PSRAM
static uint16_t* lbRowStart = nullptr;        // LB_SPR_H + 1 entries
static bool      lbRunsOk = false;

static void buildLbRuns() {
  lbRunsOk = false;
  if (!lb4) return;
  if (!lbRuns) {
    lbRuns = (LbRun*)heap_caps_malloc(sizeof(LbRun) * LB_MAX_RUNS, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    lbRowStart = (uint16_t*)heap_caps_malloc(sizeof(uint16_t) * (LB_SPR_H + 1), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!lbRuns || !lbRowStart) return;
  }
  const uint8_t* src = (const uint8_t*)lbSpr.getBuffer();
  const int sh = lbHiFirst ? 4 : 0, sl = lbHiFirst ? 0 : 4;
  int n = 0;
  for (int y = 0; y < LB_SPR_H; ++y) {
    lbRowStart[y] = (uint16_t)n;
    const uint8_t* s = src + y * lbStride;
    int x = 0;
    while (x < LB_W) {
      uint8_t v = s[x >> 1], c = (x & 1) ? ((v >> sl) & 15) : ((v >> sh) & 15);
      if (!c) { ++x; continue; }
      int x0 = x;
      while (x < LB_W) {
        uint8_t v2 = s[x >> 1], c2 = (x & 1) ? ((v2 >> sl) & 15) : ((v2 >> sh) & 15);
        if (c2 != c) break;
        ++x;
      }
      if (n >= LB_MAX_RUNS) return;                       // too busy: keep the nibble path
      lbRuns[n++] = { (uint8_t)x0, (uint8_t)(x - x0), c };
    }
  }
  lbRowStart[LB_SPR_H] = (uint16_t)n;
  lbRunsOk = true;
}

static void lbText(const char* s, int x, int y, uint8_t c, const lgfx::IFont* font, textdatum_t datum) {
  lbSpr.setFont(font); lbSpr.setTextDatum(datum);
  if (lb4) lbSpr.setTextColor((uint32_t)c); else lbSpr.setTextColor(LB_PAL[c]);
  lbSpr.drawString(s, x, y);
}

static void initLeaderboardSprite() {
  lbTried = true;
  lbSpr.setColorDepth(4);
  lbSpr.setPsram(true);              // PSRAM: internal RAM is needed for WiFi/TLS
  if (lbSpr.createSprite(LB_W, LB_SPR_H) && lbSpr.createPalette()) {
    for (int i = 0; i < LBC_COUNT; ++i) lbSpr.setPaletteColor(i, LB_PAL[i]);
    // Work out the pixel layout: nibble order and row stride.
    lbSpr.fillScreen((uint32_t)0);
    lbSpr.drawPixel(0, 0, (uint32_t)1); lbSpr.drawPixel(1, 0, (uint32_t)2); lbSpr.drawPixel(0, 1, (uint32_t)3);
    const uint8_t* buf = (const uint8_t*)lbSpr.getBuffer();
    int stride = -1;
    for (int k = 1; k < 200; ++k) if (buf[k] != 0) { stride = k; break; }
    if (buf[0] == 0x12 || buf[0] == 0x21) {
      lbHiFirst = buf[0] == 0x12;
      lbStride = stride;
      lb4 = stride >= LB_W / 2 && (buf[stride] == 0x30 || buf[stride] == 0x03);
    }
    Serial.printf("Leaderboard cache: 4-bit, bytes %02X %02X, stride %d -> %s\n", buf[0], stride > 0 ? buf[stride] : 0, stride, lb4 ? "ok" : "unexpected layout");
    if (!lb4) lbSpr.deleteSprite();
  }
  if (lb4) {
    for (int i = 0; i < 16; ++i) lbPal565[i] = toPx(rgbTo565(i < LBC_COUNT ? LB_PAL[i] : 0));
    lbOk = true;
    return;
  }
  lbSpr.setColorDepth(lgfx::color_depth_t::rgb565_nonswapped);       // fallback: 16-bit in PSRAM
  lbSpr.setPsram(true);
  lbOk = lbSpr.createSprite(LB_W, LB_SPR_H) != nullptr;
  Serial.printf("Leaderboard cache: 16-bit PSRAM fallback %s\n", lbOk ? "ok" : "FAILED");
}

static void rebuildLeaderboard() {
  if (!lbTried) initLeaderboardSprite();
  if (!lbOk) return;
  if (lb4) lbSpr.fillScreen((uint32_t)LBC_KEY);
  else { lbSpr.fillScreen((uint16_t)0x0821u); lbKey = ((const uint16_t*)lbSpr.getBuffer())[0]; }
  char b[32];
  if (phase == RP_GRID)                              snprintf(b, sizeof(b), "GRID");
  else if (phase == RP_FINISH || phase == RP_RESULTS) snprintf(b, sizeof(b), "FINISH");
  else if (raceLaps > 0)                              snprintf(b, sizeof(b), "LAP %d/%d", leaderLapDisplay(), raceLaps);
  else                                                snprintf(b, sizeof(b), "LAP %d", leaderLapDisplay());
  lbText(b, 8, 12, LBC_WHITE, &fonts::DejaVu12, textdatum_t::middle_left);
  lbText(scState != SC_NONE ? "SAFETY CAR" : SERIES_NAMES[raceSeries], LB_W - 7, 12, scState != SC_NONE ? LBC_ORANGE : LBC_GOLD, &fonts::DejaVu9, textdatum_t::middle_right);
  lbText(trk.name, 8, 27, LBC_DIM, &fonts::DejaVu9, textdatum_t::middle_left);
  lbN = numCars;
  for (int k = 0; k < numCars; ++k) {
    int yy = LB_HEAD + k * LB_ROW + 6;
    const Car& c = cars[order[k]];
    lbOrd[k] = order[k];
    lbSw[k] = toPx(rgbTo565(c.c1));
    snprintf(b, sizeof(b), "%d", k + 1);
    lbText(b, 18, yy, LBC_LIGHT, &fonts::DejaVu9, textdatum_t::middle_right);
    lbText(c.code, 29, yy, c.out ? LBC_OUT : (c.state == CS_SPIN || c.state == CS_STOPPED) ? LBC_ORANGE : LBC_WHITE, &fonts::DejaVu9, textdatum_t::middle_left);
    if (!c.out && c.dmg >= DMG_SHOW) lbSpr.fillRect(54, yy - 4, 3, 8, lb4 ? (uint32_t)LBC_DMG : LB_PAL[LBC_DMG]);   // damaged
    if (raceSeries == SER_ENDURANCE) lbText(c.cls == CC_PROTO ? "P" : "GT", 61, yy, c.cls == CC_PROTO ? LBC_PROTO : LBC_GT, &fonts::DejaVu9, textdatum_t::middle_left);
    if (phase == RP_GRID) b[0] = 0;
    else if (c.out) snprintf(b, sizeof(b), "OUT");
    else if (c.pit >= 2 && phase == RP_RACE) snprintf(b, sizeof(b), "PIT");
    else if (c.finished) snprintf(b, sizeof(b), c.finishPos == 1 ? "WIN" : "FIN");
    else if (k == 0) { if (c.lastLap > 0) fmtLapTime(b, sizeof(b), c.lastLap); else b[0] = 0; }
    else {
      float g = gapToLeader(order[k]);
      if (g < 0) snprintf(b, sizeof(b), "+%dL", (int)-g); else snprintf(b, sizeof(b), "+%.1f", g);
    }
    if (b[0]) lbText(b, LB_W - 7, yy, c.out ? LBC_DMG : k == 0 ? LBC_DIM : LBC_GAP, &fonts::DejaVu9, textdatum_t::middle_right);
  }
  if (fastestCar >= 0) {
    int yy = LB_HEAD + numCars * LB_ROW + 8;
    char t[16]; fmtLapTime(t, sizeof(t), fastestLap);
    snprintf(b, sizeof(b), "FL %s %s", cars[fastestCar].code, t);
    lbText(b, 8, yy, LBC_FL, &fonts::DejaVu9, textdatum_t::middle_left);
  }
  lbH = LB_HEAD + numCars * LB_ROW + (fastestCar >= 0 ? 17 : 4);
  lbBuiltMs = millis();
  lbDirty = true;
  buildLbRuns();
}

// ─── Per-frame UI snapshot (taken before the sim runs ahead on core 0) ────────
static char     evShow[56] = "";
static uint32_t evShowUntil = 0;
static int      ordR[MAX_CARS];

static void prepareResults();
static void prepareUI() {
  prepareResults();
  lbDirty = false;
  if (hudOn && (!lbBuiltMs || millis() - lbBuiltMs >= 250)) rebuildLeaderboard();
  int camC = cameraCar();
  if (camC != lbCamC) { lbCamC = camC; lbDirty = true; }
  memcpy(evShow, evMsg, sizeof(evShow)); evShow[sizeof(evShow) - 1] = 0;
  evShowUntil = evUntil;
  memcpy(ordR, order, sizeof(ordR));
}

static void drawLeaderboard() {
  if (!hudOn || !lbOk || lbH == 0) return;
  if (!stripHit(LB_Y, LB_Y + lbH)) return;
  blendRoundRect(LB_X, LB_Y, LB_W, lbH, 8, 0x06090Cu, 22);
  for (int k = 0; k < lbN; ++k) {
    int yy = LB_Y + LB_HEAD + k * LB_ROW + 6;
    if (!stripHit(yy - 7, yy + 7)) continue;
    if (lbOrd[k] == lbCamC) blendRect(LB_X + 3, yy - 6, LB_W - 6, LB_ROW, 0xFFFFFFu, 6);
    for (int r = yy - 4; r < yy + 5; ++r) if (r >= clipY0 && r < clipY1) { uint16_t* p = fb + r * SCREEN_W + LB_X + 22; p[0] = p[1] = p[2] = lbSw[k]; }
  }
  int y0 = clipY0 > LB_Y ? clipY0 : LB_Y, y1 = clipY1 < LB_Y + lbH ? clipY1 : LB_Y + lbH;
  if (lb4 && lbRunsOk) {
    for (int y = y0; y < y1; ++y) {
      uint16_t* d = fb + y * SCREEN_W + LB_X;
      for (int k = lbRowStart[y - LB_Y], e = lbRowStart[y - LB_Y + 1]; k < e; ++k) {
        const LbRun r = lbRuns[k];
        const uint16_t c = lbPal565[r.c];
        for (int i = 0; i < r.len; ++i) d[r.x + i] = c;
      }
    }
  } else if (lb4) {
    const uint8_t* src = (const uint8_t*)lbSpr.getBuffer();
    const int sh = lbHiFirst ? 4 : 0, sl = lbHiFirst ? 0 : 4;
    for (int y = y0; y < y1; ++y) {
      const uint8_t* s = src + (y - LB_Y) * lbStride;
      uint16_t* d = fb + y * SCREEN_W + LB_X;
      for (int x = 0; x < LB_W / 2; ++x) {
        uint8_t v = s[x];
        if (!v) continue;
        uint8_t a = (v >> sh) & 15, c = (v >> sl) & 15;
        if (a) d[2 * x] = lbPal565[a];
        if (c) d[2 * x + 1] = lbPal565[c];
      }
    }
  } else {
    const uint16_t* src = (const uint16_t*)lbSpr.getBuffer();
    for (int y = y0; y < y1; ++y) {
      const uint16_t* s = src + (y - LB_Y) * LB_W;
      uint16_t* d = fb + y * SCREEN_W + LB_X;
      for (int x = 0; x < LB_W; ++x) if (s[x] != lbKey) d[x] = s[x];
    }
  }
}

static void drawStartLights() {
  if (phase != RP_GRID || phaseT < 1.0f) return;
  const int cy = 34, sp = 30, x0 = SCREEN_CX - 2 * sp;
  if (!stripHit(cy - 20, cy + 20)) return;
  blendRoundRect(x0 - 24, cy - 18, 4 * sp + 48, 36, 10, 0x050505u, 28);
  for (int i = 0; i < 5; ++i) {
    bool on = i < lightsOn;
    fCircle(x0 + i * sp, cy, 10, on ? 0xFF2A1Au : 0x3A1010u);
    if (on) fCircle(x0 + i * sp - 3, cy - 3, 3, 0xFFA090u);
  }
}

static void drawCarTag(int ci) {
  if (ci < 0 || ci >= numCars || cam.zoom < 1.1f) return;
  const CarDraw& d = cdraw[ci];
  int x = (int)d.sx, y = (int)(d.sy - d.hl - 16);
  if (!stripHit(y - 9, y + 9) || x < 0 || x >= SCREEN_W) return;
  const Car& c = cars[ci];
  char b[24]; snprintf(b, sizeof(b), "P%d %s", c.pos, c.code);
  canvas.setFont(&fonts::DejaVu9);
  int w = canvas.textWidth(b) + 14;
  blendRoundRect(x - w / 2, y - 8, w, 16, 6, 0x06090Cu, 24);
  canvas.fillRect(x - w / 2 + 3, y - 5, 3, 10, c.c1);
  drawText(b, x + 3, y, 0xFFFFFFu, &fonts::DejaVu9, textdatum_t::middle_center);
}

// ─── Results panel: cached opaque 4-bit sprite (PSRAM) ───────────────────────
// Rebuilt when the results appear and once a second for the countdown, then
// copied into the strips: blending a 330×150 translucent panel and drawing its
// text in every strip cost ~15 ms per frame.
#define RES_W 330
#define RES_H 280
#define RES_X (SCREEN_CX - RES_W / 2 + 60)
#define RES_Y (SCREEN_CY - RES_H / 2)
enum : uint8_t { RSC_KEY = 0, RSC_BG, RSC_WHITE, RSC_LIGHT, RSC_INK, RSC_GOLD, RSC_SILVER, RSC_BRONZE, RSC_DIM, RSC_FAINT, RSC_CAR0,
                 RSC_UP = 13, RSC_DOWN, RSC_NOTE };
static LGFX_Sprite resSpr(&display);
static bool     resOk = false, resTried = false, resHiFirst = true, resDirty = false;
static int      resStride = 0, resSec = -1, resPage = -1;
static bool     resChamp = false;
static uint16_t resPal565[16];

static void resPal(int i, uint32_t rgb) { resSpr.setPaletteColor(i, rgb); resPal565[i] = toPx(rgbTo565(rgb)); }
static void resText(const char* s, int x, int y, uint8_t c, const lgfx::IFont* f, textdatum_t d) {
  resSpr.setFont(f); resSpr.setTextDatum(d); resSpr.setTextColor((uint32_t)c, (uint32_t)RSC_BG); resSpr.drawString(s, x, y);
}

static void initResultsSprite() {
  resTried = true;
  resSpr.setColorDepth(4);
  resSpr.setPsram(true);
  if (!resSpr.createSprite(RES_W, RES_H) || !resSpr.createPalette()) { Serial.println("Results cache: FAILED"); return; }
  resSpr.fillScreen((uint32_t)0);
  resSpr.drawPixel(0, 0, (uint32_t)1); resSpr.drawPixel(1, 0, (uint32_t)2); resSpr.drawPixel(0, 1, (uint32_t)3);
  const uint8_t* buf = (const uint8_t*)resSpr.getBuffer();
  int stride = -1;
  for (int k = 1; k < 400; ++k) if (buf[k] != 0) { stride = k; break; }
  resHiFirst = buf[0] == 0x12;
  resStride = stride;
  resOk = (buf[0] == 0x12 || buf[0] == 0x21) && stride >= RES_W / 2 && (buf[stride] == 0x30 || buf[stride] == 0x03);
  Serial.printf("Results cache: %s (stride %d)\n", resOk ? "ok" : "unexpected layout", stride);
  if (!resOk) { resSpr.deleteSprite(); return; }
  const uint32_t base[RSC_CAR0] = { 0x000000u, 0x0C1014u, 0xFFFFFFu, 0xC8D0D8u, 0x101010u, 0xF0C840u, 0xC8CCD0u, 0xD08A4Au, 0xA8B4BCu, 0x8A98A0u };
  for (int i = 0; i < RSC_CAR0; ++i) resPal(i, base[i]);
  resPal(RSC_UP, 0x5CC870u); resPal(RSC_DOWN, 0xE06050u); resPal(RSC_NOTE, 0xF0D890u);
}

static void resPlace(int x, int y, int k, int r) {       // a place number, in a medal for the top three
  char b[4]; snprintf(b, sizeof(b), "%d", k + 1);
  if (k < 3) {
    resSpr.fillCircle(x, y, r, (uint32_t)(RSC_GOLD + k));
    resSpr.setFont(r >= 9 ? &fonts::DejaVu12 : &fonts::DejaVu9); resSpr.setTextDatum(textdatum_t::middle_center);
    resSpr.setTextColor((uint32_t)RSC_INK); resSpr.drawString(b, x, y);
  } else resText(b, x, y, RSC_LIGHT, &fonts::DejaVu9, textdatum_t::middle_center);
}

// Page 0: the race result (podium, then the rest), with championship points.
static void buildRaceResult(int secLeft) {
  for (int i = 0; i < RES_W - 28; i += 6)                       // chequered strip
    for (int r = 0; r < 2; ++r) if (((i / 6) + r) & 1) resSpr.fillRect(14 + i, 10 + r * 6, 6, 6, (uint32_t)RSC_WHITE);
  char b[56];
  snprintf(b, sizeof(b), "%s  -  %s", trk.name, SERIES_NAMES[raceSeries]);
  resText(b, RES_W / 2, 34, RSC_LIGHT, &fonts::DejaVu9, textdatum_t::middle_center);
  if (champShown) {
    snprintf(b, sizeof(b), "Season %u  -  race %u of %d", champ.season, champ.raceNo, SEASON_RACES);
    resText(b, RES_W / 2, 48, RSC_FAINT, &fonts::DejaVu9, textdatum_t::middle_center);
  }
  for (int k = 0; k < 3 && k < numCars; ++k) {
    const int i = order[k];
    const Car& c = cars[i];
    const int yy = 74 + k * 32;
    resPal(RSC_CAR0 + k, c.c1);
    resPlace(34, yy, k, 9);
    resSpr.fillRect(52, yy - 8, 4, 16, (uint32_t)(RSC_CAR0 + k));
    const bool note = champShown && resNote[k][0];
    resText(c.code, 62, note ? yy - 5 : yy, RSC_WHITE, &fonts::DejaVu18, textdatum_t::middle_left);
    if (note) resText(resNote[k], 62, yy + 10, RSC_NOTE, &fonts::DejaVu9, textdatum_t::middle_left);
    if (champShown) {
      snprintf(b, sizeof(b), "+%u", resPts[i]);
      resText(b, RES_W - 18, yy - 5, RSC_WHITE, &fonts::DejaVu12, textdatum_t::middle_right);
    }
    if (c.bestLap > 0) {
      char t[16]; fmtLapTime(t, sizeof(t), c.bestLap);
      snprintf(b, sizeof(b), "best %s", t);
      resText(b, RES_W - 18, champShown ? yy + 10 : yy, RSC_DIM, &fonts::DejaVu9, textdatum_t::middle_right);
    }
  }
  // The rest of the field, in two columns
  const int rows = 5, y0 = 178, rh = 14;
  for (int k = 3; k < numCars && k < 3 + rows * 2; ++k) {
    const int col = (k - 3) / rows, row = (k - 3) % rows, i = order[k];
    const int x = 24 + col * 156, yy = y0 + row * rh;
    resPlace(x, yy, k, 0);
    resText(cars[i].code, x + 16, yy, RSC_WHITE, &fonts::DejaVu9, textdatum_t::middle_left);
    if (champShown && resPts[i]) { snprintf(b, sizeof(b), "+%u", resPts[i]); resText(b, x + 120, yy, RSC_DIM, &fonts::DejaVu9, textdatum_t::middle_right); }
  }
  if (champShown && resFoot[0]) resText(resFoot, RES_W / 2, RES_H - 34, RSC_DIM, &fonts::DejaVu9, textdatum_t::middle_center);
  if (champShown) snprintf(b, sizeof(b), "Standings in %d", secLeft);
  else            snprintf(b, sizeof(b), "Next race in %d", secLeft);
  resText(b, RES_W / 2, RES_H - 14, RSC_FAINT, &fonts::DejaVu9, textdatum_t::middle_center);
}

// Page 1: the championship standings.
static void buildStandings(int secLeft) {
  const bool champion = champ.raceNo >= SEASON_RACES;
  resSpr.fillRect(14, 10, RES_W - 28, 4, (uint32_t)RSC_GOLD);
  resText(standHead, RES_W / 2, 32, champion ? RSC_GOLD : RSC_WHITE, champion ? &fonts::DejaVu18 : &fonts::DejaVu12, textdatum_t::middle_center);
  if (standSub[0]) resText(standSub, RES_W / 2, 52, RSC_DIM, &fonts::DejaVu9, textdatum_t::middle_center);
  const int top = 72, avail = RES_H - 28 - top;
  const bool two = standN > 12;
  const int perCol = two ? (standN + 1) / 2 : standN;
  const int rh = clampi(avail / (perCol > 0 ? perCol : 1), 12, 17);
  char b[16];
  for (int k = 0; k < standN; ++k) {
    const StandRow& s = stand[k];
    const int col = two ? k / perCol : 0, row = two ? k % perCol : k;
    const int x = two ? 16 + col * 152 : 24, w = two ? 146 : RES_W - 48, yy = top + row * rh + rh / 2;
    resPlace(x + 6, yy, k, rh >= 16 ? 7 : 6);
    resText(s.code, x + 22, yy, RSC_WHITE, two ? &fonts::DejaVu9 : &fonts::DejaVu12, textdatum_t::middle_left);
    if (!two) {
      if (s.wins) { snprintf(b, sizeof(b), "%u win%s", s.wins, s.wins == 1 ? "" : "s"); resText(b, x + 92, yy, RSC_FAINT, &fonts::DejaVu9, textdatum_t::middle_left); }
      if (s.moved) { snprintf(b, sizeof(b), "%+d", s.moved); resText(b, x + 168, yy, s.moved > 0 ? RSC_UP : RSC_DOWN, &fonts::DejaVu9, textdatum_t::middle_center); }
    }
    if (s.gained) { snprintf(b, sizeof(b), "+%u", s.gained); resText(b, x + w - 44, yy, RSC_DIM, &fonts::DejaVu9, textdatum_t::middle_right); }
    snprintf(b, sizeof(b), "%u", s.pts);
    resText(b, x + w, yy, RSC_WHITE, two ? &fonts::DejaVu9 : &fonts::DejaVu12, textdatum_t::middle_right);
  }
  char t[32];
  if (champion) snprintf(t, sizeof(t), "New season in %d", secLeft);
  else          snprintf(t, sizeof(t), "Next race in %d", secLeft);
  resText(t, RES_W / 2, RES_H - 14, RSC_FAINT, &fonts::DejaVu9, textdatum_t::middle_center);
}

static void rebuildResults(int secLeft, int page) {
  if (!resTried) initResultsSprite();
  if (!resOk) return;
  resSpr.fillScreen((uint32_t)RSC_KEY);
  resSpr.fillRoundRect(0, 0, RES_W, RES_H, 14, (uint32_t)RSC_BG);
  if (page == 1) buildStandings(secLeft); else buildRaceResult(secLeft);
  resDirty = true;
}

static void prepareResults() {
  resDirty = false;
  if (phase != RP_RESULTS) { resSec = -1; resPage = -1; return; }
  const float standSecs = champFinal ? 20.0f : 14.0f;              // the standings get the end of the results time
  const float pageAt = resultsSecs - standSecs;
  const int page = champShown && phaseT >= pageAt ? 1 : 0;
  const int sec = (int)fmx(0, (page == 0 && champShown ? pageAt : resultsSecs) - phaseT) + 1;
  if (sec != resSec || page != resPage || champShown != resChamp) { resSec = sec; resPage = page; resChamp = champShown; rebuildResults(sec, page); }
}

static void drawResults() {
  if (phase != RP_RESULTS || !resOk || resSec < 0) return;
  if (!stripHit(RES_Y, RES_Y + RES_H)) return;
  const uint8_t* src = (const uint8_t*)resSpr.getBuffer();
  const int sh = resHiFirst ? 4 : 0, sl = resHiFirst ? 0 : 4;
  int y0 = clipY0 > RES_Y ? clipY0 : RES_Y, y1 = clipY1 < RES_Y + RES_H ? clipY1 : RES_Y + RES_H;
  for (int y = y0; y < y1; ++y) {
    const uint8_t* s = src + (y - RES_Y) * resStride;
    uint16_t* d = fb + y * SCREEN_W + RES_X;
    for (int x = 0; x < RES_W / 2; ++x) {
      uint8_t v = s[x];
      uint8_t a = (v >> sh) & 15, c = (v >> sl) & 15;
      if (a) d[2 * x] = resPal565[a];
      if (c) d[2 * x + 1] = resPal565[c];
    }
  }
}

static void drawEventTicker() {
  if (millis() > evShowUntil || !evShow[0]) return;
  int y = SCREEN_H - 34;
  if (!stripHit(y, y + 26)) return;
  canvas.setFont(&fonts::DejaVu12);
  int w = canvas.textWidth(evShow) + 34;
  int x = SCREEN_CX - w / 2 + (hudOn ? 60 : 0);
  blendRoundRect(x, y, w, 26, 8, 0x06090Cu, 25);
  canvas.fillRect(x + 8, y + 6, 4, 14, 0xE02A20u);
  drawText(evShow, x + w / 2 + 4, y + 13, 0xFFFFFFu, &fonts::DejaVu12, textdatum_t::middle_center);
}

// ─── Buttons & panel ──────────────────────────────────────────────────────────
static void drawTopButtons() {
  if (!uiVisible() || !stripHit(BTN[0].y, BTN[0].y + BTN[0].h)) return;
  bool pressed[NUM_TOP_BTN] = { false };
  if (ts.mode == TM_UI && ts.uiId >= 0 && ts.uiId < NUM_TOP_BTN) pressed[ts.uiId] = true;
  const uint32_t ic = 0xF0F4F4u, dim = 0x6A7A80u;
  drawButtonBg(BTN[0], false, pressed[0]); iconMinus(BTN[0].x + BTN[0].w / 2, BTN[0].y + BTN[0].h / 2, !spectator && cam.zoom > ZOOM_MIN + 0.01f ? ic : dim);
  drawButtonBg(BTN[1], false, pressed[1]); iconPlus(BTN[1].x + BTN[1].w / 2, BTN[1].y + BTN[1].h / 2, !spectator && cam.zoom < ZOOM_MAX - 0.01f ? ic : dim);
  drawButtonBg(BTN[2], hudOn, pressed[2]); iconList(BTN[2].x + BTN[2].w / 2, BTN[2].y + BTN[2].h / 2, ic);
  drawButtonBg(BTN[3], panelOpen, pressed[3]);
  iconGear(BTN[3].x + BTN[3].w / 2, BTN[3].y + BTN[3].h / 2, ic, panelOpen ? 0xB0302Au : 0x1A2026u);
  drawButtonBg(BTN[4], spectator, pressed[4]);
  iconTv(BTN[4].x + BTN[4].w / 2, BTN[4].y + BTN[4].h / 2 + 2, ic, spectator ? 0xB0302Au : 0x1A2026u);
}

static void drawArrowBtn(const Rect& r, bool left) {
  blendRoundRect(r.x, r.y, r.w, r.h, 8, 0x3A4450u, 26);
  int cx = r.x + r.w / 2, cy = r.y + r.h / 2;
  if (left) fTri(cx + 6, cy - 9, cx + 6, cy + 9, cx - 7, cy, 0xF0F4F4u);
  else      fTri(cx - 6, cy - 9, cx - 6, cy + 9, cx + 7, cy, 0xF0F4F4u);
}

static void drawPanelRow(int row, const char* label, const char* value, bool plusMinus = false) {
  const lgfx::IFont* f = &fonts::DejaVu12;
  drawText(label, PANEL_X + 24, ROW_Y(row) + 15, 0xC8D0D8u, f, textdatum_t::middle_left);
  Rect l = P_ARROW_L(row), r = P_ARROW_R(row);
  if (plusMinus) {
    blendRoundRect(l.x, l.y, l.w, l.h, 8, 0x3A4450u, 26); iconMinus(l.x + l.w / 2, l.y + l.h / 2, 0xF0F4F4u);
    blendRoundRect(r.x, r.y, r.w, r.h, 8, 0x3A4450u, 26); iconPlus(r.x + r.w / 2, r.y + r.h / 2, 0xF0F4F4u);
  } else {
    drawArrowBtn(l, true); drawArrowBtn(r, false);
  }
  drawText(value, (l.x + r.x + r.w) / 2, ROW_Y(row) + 15, 0xFFFFFFu, f, textdatum_t::middle_center);
}

static void drawPanelButton(const Rect& r, const char* label, uint32_t bg, uint32_t a = 26) {
  blendRoundRect(r.x, r.y, r.w, r.h, 10, bg, a);
  drawText(label, r.x + r.w / 2, r.y + r.h / 2, 0xFFFFFFu, &fonts::DejaVu12, textdatum_t::middle_center);
}

static void drawDriversPage() {
  drawText("Drivers", PANEL_X + 24, PANEL_Y + 26, 0xF4F8F8u, &fonts::DejaVu18, textdatum_t::middle_left);
  drawPanelButton(D_TOGGLE, customDrivers ? "Custom: On" : "Custom: Off", customDrivers ? 0xB0302Au : 0x3A4450u, 28);
  drawText(customDrivers ? "On: these drivers race (from the next race). Tap to edit."
                         : "Off: the standard field.",
           PANEL_X + 24, PANEL_Y + 60, 0x9AA6B0u, &fonts::DejaVu9, textdatum_t::middle_left);
  char b[8];
  for (int k = 0; k < MAX_CARS; ++k) {
    Rect n = dSlotName(k);
    if (!stripHit(n.y, n.y + n.h)) continue;
    const bool inRace = k < carSetting;                 // slots past the car count sit out
    const uint32_t txt = (customDrivers && inRace) ? 0xFFFFFFu : 0x7A8690u;
    snprintf(b, sizeof(b), "%d", k + 1);
    drawText(b, n.x - 6, n.y + n.h / 2, 0x8A98A0u, &fonts::DejaVu9, textdatum_t::middle_right);
    blendRoundRect(n.x, n.y, n.w, n.h, 7, 0x3A4450u, 24);
    drawText(customCode[k], n.x + n.w / 2, n.y + n.h / 2, txt, &fonts::DejaVu12, textdatum_t::middle_center);
  }
  drawPanelButton(P_NEXT_RACE, "Copy this race", 0x3A4450u);
  drawPanelButton(P_DRIVERS, "New season", 0x3A4450u);
  drawPanelButton(P_CLOSE, "Back", 0xB0302Au, 28);
}

// Career records of this race's field: titles, then wins, then podiums.
static void drawRecordsPage() {
  drawText("Driver records", PANEL_X + 24, PANEL_Y + 26, 0xF4F8F8u, &fonts::DejaVu18, textdatum_t::middle_left);
  char b[40];
  if (champ.lastChamp[0]) snprintf(b, sizeof(b), "Season %u  -  last champion %.3s", champ.season, champ.lastChamp);
  else                    snprintf(b, sizeof(b), "Season %u", champ.season);
  drawText(b, PANEL_X + PANEL_W - 24, PANEL_Y + 26, 0x8A98A0u, &fonts::DejaVu9, textdatum_t::middle_right);
  static const char* const HEAD[] = { "Starts", "Wins", "Podiums", "Poles", "Fast laps", "Titles", "Best run" };
  static const int16_t COLX[] = { 150, 198, 252, 302, 358, 410, 462 };
  const int hy = PANEL_Y + 62;
  for (int c = 0; c < 7; ++c) drawText(HEAD[c], PANEL_X + COLX[c], hy, 0x8A98A0u, &fonts::DejaVu9, textdatum_t::middle_center);
  const RecRow* rows[MAX_CARS]; int n = 0;
  for (int i = 0; i < numCars; ++i) { const RecRow* r = recFind(cars[i].code, false); if (r) rows[n++] = r; }
  std::sort(rows, rows + n, [](const RecRow* a, const RecRow* b) {
    if (a->titles != b->titles) return a->titles > b->titles;
    if (a->wins != b->wins) return a->wins > b->wins;
    return a->pods > b->pods;
  });
  if (!n) drawText("No finished races yet.", PANEL_X + PANEL_W / 2, PANEL_Y + 140, 0x9AA6B0u, &fonts::DejaVu12, textdatum_t::middle_center);
  for (int k = 0; k < n; ++k) {
    const RecRow& r = *rows[k];
    const int yy = PANEL_Y + 84 + k * 16;
    if (!stripHit(yy - 8, yy + 8)) continue;
    if (k & 1) blendRect(PANEL_X + 16, yy - 8, PANEL_W - 32, 16, 0xFFFFFFu, 3);
    snprintf(b, sizeof(b), "%.3s", r.code);
    drawText(b, PANEL_X + 40, yy, 0xFFFFFFu, &fonts::DejaVu12, textdatum_t::middle_left);
    const unsigned v[7] = { r.starts, r.wins, r.pods, r.poles, r.fastLaps, r.titles, r.bestStreak };
    for (int c = 0; c < 7; ++c) {
      snprintf(b, sizeof(b), "%u", v[c]);
      drawText(b, PANEL_X + COLX[c], yy, c == 5 && r.titles ? 0xF0C840u : (v[c] ? 0xE0E6EAu : 0x5A6670u), &fonts::DejaVu9, textdatum_t::middle_center);
    }
  }
  drawPanelButton(P_CLOSE, "Back", 0xB0302Au, 28);
}

static void drawKeyboardPage() {
  char b[32];
  snprintf(b, sizeof(b), "Driver %d name", editSlot + 1);
  drawText(b, PANEL_X + 24, PANEL_Y + 26, 0xF4F8F8u, &fonts::DejaVu18, textdatum_t::middle_left);
  // the name being typed, with a cursor while there's room
  const int bx = PANEL_X + PANEL_W / 2 - 60, by = PANEL_Y + 52;
  blendRoundRect(bx, by, 120, 46, 10, 0x000000u, 26);
  snprintf(b, sizeof(b), "%s%s", editBuf, strlen(editBuf) < 3 ? "_" : "");
  drawText(b, bx + 60, by + 23, 0xFFFFFFu, &fonts::DejaVu24, textdatum_t::middle_center);
  drawText("Up to 3 letters", bx + 132, by + 23, 0x8A98A0u, &fonts::DejaVu9, textdatum_t::middle_left);
  for (int i = 0; i < NUM_KEYS; ++i) {
    Rect r = kKey(i);
    if (!stripHit(r.y, r.y + r.h)) continue;
    blendRoundRect(r.x, r.y, r.w, r.h, 8, 0x3A4450u, 26);
    char k[2] = { KEYS[i], 0 };
    drawText(k, r.x + r.w / 2, r.y + r.h / 2, 0xFFFFFFu, &fonts::DejaVu18, textdatum_t::middle_center);
  }
  drawPanelButton(K_DEL, "Delete", 0x3A4450u);
  drawPanelButton(P_NEXT_RACE, "Cancel", 0x3A4450u);
  drawPanelButton(P_CLOSE, "OK", 0xB0302Au, 28);
}

static void drawPanel() {
  if (!panelOpen || !stripHit(PANEL_Y, PANEL_Y + PANEL_H)) return;
  blendRoundRect(PANEL_X, PANEL_Y, PANEL_W, PANEL_H, 14, 0x0A0E12u, 28);
  canvas.drawRoundRect(PANEL_X, PANEL_Y, PANEL_W, PANEL_H, 14, 0x7A8088u);
  if (panelPage == 1) { drawDriversPage(); return; }
  if (panelPage == 2) { drawKeyboardPage(); return; }
  if (panelPage == 3) { drawRecordsPage(); return; }
  drawText("Race settings", PANEL_X + 24, PANEL_Y + 26, 0xF4F8F8u, &fonts::DejaVu18, textdatum_t::middle_left);
  char buf[48];
  snprintf(buf, sizeof(buf), "v%s  WiFi %s", FIRMWARE_VERSION, WiFi.status() == WL_CONNECTED ? "on" : "off");
  drawText(buf, PANEL_X + PANEL_W - 24, PANEL_Y + 26, 0x8A98A0u, &fonts::DejaVu9, textdatum_t::middle_right);

  drawText("Brightness", PANEL_X + 24, ROW_Y(0) + 15, 0xC8D0D8u, &fonts::DejaVu12, textdatum_t::middle_left);
  int sx0 = P_SLIDER.x + 10, sw = P_SLIDER.w - 20, sy = P_SLIDER.y + P_SLIDER.h / 2;
  canvas.fillRoundRect(sx0, sy - 3, sw, 6, 3, 0x3A4450u);
  int knob = sx0 + (int)((brightness - 20) / 235.0f * sw);
  canvas.fillRoundRect(sx0, sy - 3, knob - sx0, 6, 3, 0xE04A3Au);
  fCircle(knob, sy, 11, 0xF4F8F8u);

  if (weatherOverride < 0) snprintf(buf, sizeof(buf), "Auto: %s", weatherLiveOk ? WEATHER_NAMES[currentWeather] : "--");
  else snprintf(buf, sizeof(buf), "%s", WEATHER_NAMES[weatherOverride]);
  drawPanelRow(1, "Weather", buf);
  if (currentTimeMode == TIME_REAL && !timeSynced()) snprintf(buf, sizeof(buf), "Real (no clock yet)");
  else snprintf(buf, sizeof(buf), "%s", TIME_MODE_NAMES[currentTimeMode]);
  drawPanelRow(2, "Time of day", buf);
  snprintf(buf, sizeof(buf), "%d", carSetting);
  drawPanelRow(3, "Number of cars", buf, true);
  drawPanelRow(4, "Track", trackModeName(trackMode));
  drawPanelRow(5, "Car type", seriesSelName(seriesSel));
  if (LAPS_OPTS[lapsIdx] == 0) snprintf(buf, sizeof(buf), "Endless"); else snprintf(buf, sizeof(buf), "%d laps", LAPS_OPTS[lapsIdx]);
  drawPanelRow(6, "Race length", buf);
  drawPanelRow(7, "Camera", CAM_MODE_NAMES[camMode]);
  drawPanelRow(8, "Spectator", spectator ? "On - camera locked" : "Off - auto after 30 s");

  bool armed = millis() < nextRaceArmUntil;
  blendRoundRect(P_NEXT_RACE.x, P_NEXT_RACE.y, P_NEXT_RACE.w, P_NEXT_RACE.h, 10, armed ? 0xB02A20u : 0x3A4450u, 26);
  drawText(armed ? "Tap again" : "Next race now", P_NEXT_RACE.x + P_NEXT_RACE.w / 2, P_NEXT_RACE.y + P_NEXT_RACE.h / 2, 0xFFFFFFu, &fonts::DejaVu12, textdatum_t::middle_center);
  drawPanelButton(P_DRIVERS, customDrivers ? "Drivers: On" : "Drivers", customDrivers ? 0x7A2A26u : 0x3A4450u);
  drawPanelButton(P_RECORDS, "Records", 0x3A4450u);
  blendRoundRect(P_CLOSE.x, P_CLOSE.y, P_CLOSE.w, P_CLOSE.h, 10, 0xB0302Au, 28);
  drawText("Done", P_CLOSE.x + P_CLOSE.w / 2, P_CLOSE.y + P_CLOSE.h / 2, 0xFFFFFFu, &fonts::DejaVu12, textdatum_t::middle_center);
}

static void drawToast() {
  if (millis() > toastUntil || !toastMsg[0]) return;
  int y = SCREEN_H - 76;
  if (!stripHit(y, y + 34)) return;
  canvas.setFont(&fonts::DejaVu12);
  int w = canvas.textWidth(toastMsg) + 36;
  blendRoundRect(SCREEN_CX - w / 2, y, w, 34, 12, 0x0A0E12u, 26);
  drawText(toastMsg, SCREEN_CX, y + 17, 0xF4F8F8u, &fonts::DejaVu12, textdatum_t::middle_center);
}

// Mark the screen rows the UI changes this frame (see markDynamicRows in the .ino).
static void uiDynamicRows(void (*mark)(int, int)) {
  if (hudOn && lbDirty) mark(LB_Y, LB_Y + LB_SPR_H);
  if (phase == RP_GRID) mark(14, 54);
  if (resDirty) mark(RES_Y, RES_Y + RES_H);
  if (millis() <= evShowUntil + 100) mark(SCREEN_H - 36, SCREEN_H - 6);
  if (millis() <= toastUntil + 100) mark(SCREEN_H - 78, SCREEN_H - 40);
  if (uiVisible() || millis() - lastTouchMs < UI_HIDE_MS + 200) mark(BTN[0].y - 2, BTN[0].y + BTN[0].h + 2);
  if (panelOpen) mark(PANEL_Y, PANEL_Y + PANEL_H);
  int cc = cameraCar();
  if (cc >= 0 && cc < numCars && cam.zoom >= 1.1f) { int ty = (int)(cdraw[cc].sy - cdraw[cc].hl - 16); mark(ty - 10, ty + 10); }
}

static uint32_t uiProf[8] = {0};
#define UIP(i, call) do { uint32_t _t = micros(); call; uiProf[i] += micros() - _t; } while (0)
static void drawUI() {
  UIP(0, drawCarTag(cameraCar()));
  UIP(1, drawLeaderboard());
  UIP(2, drawStartLights());
  UIP(3, drawResults());
  UIP(4, drawEventTicker());
  UIP(5, drawTopButtons());
  UIP(6, drawPanel());
  UIP(7, drawToast());
}
