#pragma once
// Live weather from OpenWeatherMap, fetched on a background FreeRTOS task so the
// race animation never stalls while WiFi/TLS/HTTP are busy.
//
// Call initWeather() once from setup() (after OTA). currentWeather is updated
// in the background every 5 min (1 min retry after a failure).
// WiFi is left ON permanently — cycling the radio caused silent reconnect
// failures on the ESP32-S3 in the original aquarium.

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

#include "wifi_config.h"

enum WeatherCondition : uint8_t {
  WEATHER_SUNNY = 0,
  WEATHER_PARTLY_CLOUDY,
  WEATHER_CLOUDY,
  WEATHER_RAINY,
  WEATHER_STORMY,
  WEATHER_SNOWY,
  WEATHER_FOGGY,
  WEATHER_COUNT
};

static const char* const WEATHER_NAMES[WEATHER_COUNT] = {
  "Sunny", "Partly cloudy", "Cloudy", "Rain", "Storm", "Snow", "Fog"
};

static volatile WeatherCondition currentWeather = WEATHER_SUNNY;
static volatile bool             weatherLiveOk  = false;   // last fetch succeeded
static volatile bool             _weatherKick   = false;   // request an immediate refetch

static const uint32_t _WEATHER_INTERVAL       = 5UL * 60UL * 1000UL;
static const uint32_t _WEATHER_RETRY_INTERVAL = 60UL * 1000UL;

static bool weatherConfigured() { return sizeof(WEATHER_API_KEY) > 1; }

// Ensures WiFi is connected, reusing an existing connection if still up.
static bool _wifiEnsureConnected() {
  if (WiFi.status() == WL_CONNECTED) return true;
  if (WiFi.getMode() == WIFI_OFF) { WiFi.mode(WIFI_STA); delay(200); }
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - t0 > 15000UL) { Serial.printf("WiFi: connect timeout (status %d)\n", (int)WiFi.status()); return false; }
    delay(200);
  }
  Serial.printf("WiFi: connected, IP %s\n", WiFi.localIP().toString().c_str());
  return true;
}

// Set when the server rejects the API key: retrying can't help, and every TLS
// connection briefly takes most of the free internal heap from the renderer.
static bool _weatherKeyBad = false;

static bool _fetchWeather(WeatherCondition& out) {
  if (!_wifiEnsureConnected()) return false;

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(10);

  char url[256];
  snprintf(url, sizeof(url),
           "https://api.openweathermap.org/data/2.5/weather?lat=%s&lon=%s&appid=%s",
           WEATHER_LAT, WEATHER_LON, WEATHER_API_KEY);

  bool ok = false;
  HTTPClient http;
  if (http.begin(client, url)) {
    http.setTimeout(12000);
    int code = http.GET();
    if (code != HTTP_CODE_OK) Serial.printf("Weather: HTTP %d%s\n", code, code == 401 ? " (API key rejected - not retrying until the next boot)" : "");
    if (code == 401) _weatherKeyBad = true;
    if (code == HTTP_CODE_OK) {
      String body = http.getString();
      DynamicJsonDocument doc(1024);
      if (!deserializeJson(doc, body)) {
        int id = doc["weather"][0]["id"] | -1;
        if (id >= 0) {
          if      (id == 800)              out = WEATHER_SUNNY;
          else if (id == 801)              out = WEATHER_PARTLY_CLOUDY;
          else if (id >= 802 && id <= 804) out = WEATHER_CLOUDY;
          else if (id >= 500 && id <= 531) out = WEATHER_RAINY;
          else if (id >= 300 && id <= 321) out = WEATHER_RAINY;
          else if (id >= 200 && id <= 232) out = WEATHER_STORMY;
          else if (id >= 600 && id <= 622) out = WEATHER_SNOWY;
          else if (id >= 700 && id <= 781) out = WEATHER_FOGGY;
          else                             out = WEATHER_CLOUDY;
          ok = true;
        }
      }
    }
    http.end();
  }
  return ok;
}

static void _weatherTask(void*) {
  uint32_t lastMs = 0;
  bool first = true;
  for (;;) {
    if (!weatherConfigured() || (_weatherKeyBad && !_weatherKick)) {   // no usable API key: just keep WiFi up for NTP time
      _wifiEnsureConnected();
      vTaskDelay(pdMS_TO_TICKS(30000));
      continue;
    }
    uint32_t interval = weatherLiveOk ? _WEATHER_INTERVAL : _WEATHER_RETRY_INTERVAL;
    if (first || _weatherKick || millis() - lastMs >= interval) {
      first = false;
      _weatherKick = false;
      WeatherCondition w = currentWeather;
      bool ok = _fetchWeather(w);
      weatherLiveOk = ok;
      if (ok) currentWeather = w;
      Serial.printf("Weather: %s\n", ok ? WEATHER_NAMES[w] : "fetch failed");
      lastMs = millis();
    }
    vTaskDelay(pdMS_TO_TICKS(500));
  }
}

static void initWeather() {
  // Core 0, low priority: the render loop (Arduino loopTask) runs on core 1.
  xTaskCreatePinnedToCore(_weatherTask, "weather", 12288, nullptr, 1, nullptr, 0);
}

static void forceWeatherRefetch() { _weatherKick = true; }
