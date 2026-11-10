#pragma once
// Persistent settings in NVS (Preferences), and choosing the next race.

#include <Preferences.h>
#include "race.h"

#define SETTINGS_NS "racetrk"

static Preferences prefs;
static uint8_t  brightness = 200;
static uint8_t  carSetting = 14;          // cars in the next race
static uint8_t  trackMode  = 0;           // 0 rotate, 1 random, 2+ = preset (trackMode − 2)
static uint8_t  lapsIdx    = 2;
static uint8_t  seriesSel  = 0;           // 0 auto, else series + 1
static bool     hudOn      = true;
static uint32_t rotIdx     = 0;           // rotation counter (survives reboots)

#define TRACK_MODE_COUNT  (2 + NUM_PRESETS)
#define SERIES_SEL_COUNT  (SER_COUNT + 1)
static const int LAPS_OPTS[] = { 0, 5, 10, 15, 20, 30 };
#define LAPS_OPT_COUNT ((int)(sizeof(LAPS_OPTS) / sizeof(LAPS_OPTS[0])))

static const char* trackModeName(int m) {
  if (m == 0) return "Rotate";
  if (m == 1) return "Random";
  return PRESETS[clampi(m - 2, 0, NUM_PRESETS - 1)].name;
}
static const char* seriesSelName(int s) { return s == 0 ? "Auto" : SERIES_NAMES[clampi(s - 1, 0, SER_COUNT - 1)]; }

static void loadSettings() {
  prefs.begin(SETTINGS_NS, true);
  brightness      = prefs.getUChar("bright", 200);
  weatherOverride = prefs.getChar("wx", -1);
  currentTimeMode = (TimeMode)clampi(prefs.getUChar("tmode", TIME_REAL), 0, TIME_MODE_COUNT - 1);
  carSetting      = clampi(prefs.getUChar("cars", 14), 4, MAX_CARS);
  trackMode       = prefs.getUChar("track", 0);
  lapsIdx         = prefs.getUChar("laps", 2);
  seriesSel       = prefs.getUChar("series", 0);
  hudOn           = prefs.getBool("hud", true);
  rotIdx          = prefs.getULong("rot", 0);
  prefs.end();
  if (weatherOverride >= WEATHER_COUNT) weatherOverride = -1;
  if (trackMode >= TRACK_MODE_COUNT) trackMode = 0;
  if (lapsIdx >= LAPS_OPT_COUNT) lapsIdx = 2;
  if (seriesSel >= SERIES_SEL_COUNT) seriesSel = 0;
}

static void saveSettings() {
  prefs.begin(SETTINGS_NS, false);
  prefs.putUChar("bright", brightness);
  prefs.putChar("wx", weatherOverride);
  prefs.putUChar("tmode", (uint8_t)currentTimeMode);
  prefs.putUChar("cars", carSetting);
  prefs.putUChar("track", trackMode);
  prefs.putUChar("laps", lapsIdx);
  prefs.putUChar("series", seriesSel);
  prefs.putBool("hud", hudOn);
  prefs.end();
}

// Decide the next circuit and series. sel −1 = generated from `seed`.
static void pickNextRace(int& sel, uint32_t& seed, uint8_t& series) {
  ++rotIdx;
  prefs.begin(SETTINGS_NS, false);
  prefs.putULong("rot", rotIdx);
  prefs.end();
  seed = esp_random() | 1u;
  if (trackMode == 0)      sel = (rotIdx & 1) ? -1 : (int)((rotIdx / 2) % NUM_PRESETS);
  else if (trackMode == 1) sel = -1;
  else                     sel = trackMode - 2;
  if (seriesSel == 0) {
    // Formula a little more often than the others
    static const uint8_t AUTO_SERIES[] = { SER_FORMULA, SER_GT, SER_ENDURANCE, SER_FORMULA, SER_TOURING, SER_CLASSIC, SER_GT, SER_FORMULA };
    series = AUTO_SERIES[hash32(rotIdx * 31 + 7) % sizeof(AUTO_SERIES)];
  } else {
    series = seriesSel - 1;
  }
}
