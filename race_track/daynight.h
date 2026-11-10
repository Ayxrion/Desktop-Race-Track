#pragma once
// Day/night cycle: NTP-synced real time, a fast 20-minute cycle, or a fixed
// day / night override. Non-blocking: SNTP syncs in the background whenever
// WiFi comes up (the weather task keeps WiFi connected).
// Requires TIMEZONE_OFFSET_HOURS (integer UTC offset) in wifi_config.h.

#include <time.h>
#include "wifi_config.h"

#ifndef TIMEZONE_OFFSET_HOURS
#define TIMEZONE_OFFSET_HOURS 0
#endif

enum TimeMode : uint8_t { TIME_REAL = 0, TIME_FAST, TIME_DAY, TIME_NIGHT, TIME_MODE_COUNT };
static const char* const TIME_MODE_NAMES[TIME_MODE_COUNT] = { "Real", "Fast", "Day", "Night" };
static TimeMode currentTimeMode = TIME_REAL;

static const uint32_t _FAST_CYCLE_MS = 20UL * 60UL * 1000UL;   // 24 h in 20 min

static void initDayNight() {
  configTime((long)TIMEZONE_OFFSET_HOURS * 3600L, 0, "pool.ntp.org", "time.nist.gov");
}

static bool timeSynced() {
  time_t now = time(nullptr);
  return now > 1700000000;   // any time after late 2023 means SNTP has synced
}

// 0.0–1.0 fraction of the day: 0 = midnight, 0.25 = 6 am, 0.5 = noon, 0.75 = 6 pm
static float getDayProgress() {
  switch (currentTimeMode) {
    case TIME_FAST:  return (float)(millis() % _FAST_CYCLE_MS) / (float)_FAST_CYCLE_MS;
    case TIME_DAY:   return 0.50f;
    case TIME_NIGHT: return 0.02f;
    default: break;
  }
  if (!timeSynced()) return 0.5f;   // noon until NTP has synced
  time_t now = time(nullptr);
  struct tm t;
  localtime_r(&now, &t);
  return (t.tm_hour * 3600.0f + t.tm_min * 60.0f + t.tm_sec) / 86400.0f;
}
