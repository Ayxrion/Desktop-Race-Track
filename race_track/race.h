#pragma once
// Race director: grid and start lights, lap counting, the chequered flag,
// results, and the event ticker (lead changes, fastest laps, spins, passes).

#include "cars.h"

enum RacePhase : uint8_t { RP_GRID, RP_RACE, RP_FINISH, RP_RESULTS };
static RacePhase phase = RP_GRID;
static float phaseT = 0;
static int   raceLaps = 10;              // 0 = endless
static int   lightsOn = 0;
static float goAt = 7.0f;
static float fastestLap = 0;
static int   fastestCar = -1;
static int   finishCount = 0;
static bool  nextRaceRequested = false;
static int   spinCamCar = -1;            // for the TV camera
static float seenLap[MAX_CARS];
static int   prevTop[3] = { -1, -1, -1 };

#define RESULTS_SECONDS 16.0f

// ─── Event ticker ─────────────────────────────────────────────────────────────
static char     evMsg[56] = "";
static uint32_t evUntil = 0;
static float    lastEventT = -100;

static void raceEvent(const char* m, float minGap = 3.0f, uint32_t ms = 3200) {
  if (simTime - lastEventT < minGap) return;
  lastEventT = simTime;
  strncpy(evMsg, m, sizeof(evMsg) - 1); evMsg[sizeof(evMsg) - 1] = 0;
  evUntil = millis() + ms;
  Serial.printf("race: %s\n", m);
}

static void fmtLapTime(char* out, size_t n, float t) {
  int m = (int)(t / 60.0f);
  float s = t - m * 60.0f;
  if (m > 0) snprintf(out, n, "%d:%05.2f", m, s); else snprintf(out, n, "%.2f", s);
}

// ─── Race control ─────────────────────────────────────────────────────────────
static void startRace(int nCars, uint8_t series, uint32_t seed) {
  setupCars(nCars, series, seed);
  initEffects();
  phase = RP_GRID; phaseT = 0; raceTime = 0; lightsOn = 0;
  goAt = 6.2f + frandr(0.3f, 1.8f);
  fastestLap = 0; fastestCar = -1; finishCount = 0;
  nextRaceRequested = false; spinCamCar = -1; evN = 0;
  for (int i = 0; i < MAX_CARS; ++i) seenLap[i] = 0;
  prevTop[0] = prevTop[1] = prevTop[2] = -1;
  evMsg[0] = 0;
}

static void updateRace(float dt) {
  phaseT += dt;
  if (phase == RP_GRID) {
    lightsOn = phaseT < 1.5f ? 0 : clampi((int)((phaseT - 1.5f) / 0.9f) + 1, 0, 5);
    updateCars(dt, false, false);
    if (phaseT >= goAt) { phase = RP_RACE; phaseT = 0; lightsOn = 0; raceEvent("LIGHTS OUT!", 0, 2200); }
    updateOrder();
    return;
  }
  raceTime += dt;
  updateCars(dt, true, phase == RP_RESULTS);

  // Chequered flag when the leader completes the last lap
  if (phase == RP_RACE && raceLaps > 0 && cars[order[0]].lap >= raceLaps) {
    phase = RP_FINISH; phaseT = 0;
    for (int i = 0; i < numCars; ++i) cars[i].lapsAtFlag = cars[i].lap;
  }
  if (phase == RP_FINISH || phase == RP_RESULTS) {
    for (int k = 0; k < numCars; ++k) {
      Car& c = cars[order[k]];
      if (c.finished) continue;
      if (c.lap >= raceLaps || c.lap > c.lapsAtFlag) {
        c.finished = true; c.finishPos = ++finishCount;
        if (c.finishPos == 1) { char b[48]; snprintf(b, sizeof(b), "CHEQUERED FLAG - %s WINS!", c.code); raceEvent(b, 0, 5000); }
      }
    }
    if (phase == RP_FINISH && (finishCount >= numCars || phaseT > 50)) { phase = RP_RESULTS; phaseT = 0; }
    if (phase == RP_RESULTS && phaseT > RESULTS_SECONDS) nextRaceRequested = true;
  }
  updateOrder();

  char b[56];
  // Incidents (the camera goes to the worst one)
  for (int e = 0; e < evN; ++e) {
    const SimEvent& ev = evQ[e];
    const Car& A = cars[ev.a];
    switch (ev.type) {
      case EV_SPIN:    snprintf(b, sizeof(b), "%s spins!", A.code); raceEvent(b, 1.0f); spinCamCar = ev.a; break;
      case EV_WALL:    snprintf(b, sizeof(b), "%s hits the wall!", A.code); raceEvent(b, 0.5f); spinCamCar = ev.a; break;
      case EV_CONTACT: snprintf(b, sizeof(b), "Contact! %s and %s", A.code, cars[ev.b].code); raceEvent(b, 1.0f); spinCamCar = ev.b; break;
      case EV_GRAVEL:  if (A.pos <= 6) { snprintf(b, sizeof(b), "%s runs into the gravel", A.code); raceEvent(b, 6.0f); } break;
    }
  }
  evN = 0;
  if (phase != RP_RACE) return;
  // Fastest laps
  for (int i = 0; i < numCars; ++i) {
    Car& c = cars[i];
    if (c.lastLap == seenLap[i]) continue;
    seenLap[i] = c.lastLap;
    if (c.lastLap > 0 && (fastestLap <= 0 || c.lastLap < fastestLap)) {
      fastestLap = c.lastLap; fastestCar = i;
      if (c.lap >= 2) { char t[16]; fmtLapTime(t, sizeof(t), c.lastLap); snprintf(b, sizeof(b), "Fastest lap: %s %s", c.code, t); raceEvent(b); }
    }
  }
  // Lead changes and passes for the podium places
  bool early = raceTime < 8.0f;          // the opening scramble
  for (int k = 0; k < 3 && k < numCars; ++k) {
    int now = order[k];
    if (prevTop[k] >= 0 && now != prevTop[k] && !early && cars[prevTop[k]].pos > k + 1) {
      if (k == 0) snprintf(b, sizeof(b), "%s takes the lead!", cars[now].code);
      else        snprintf(b, sizeof(b), "%s passes %s for P%d", cars[now].code, cars[prevTop[k]].code, k + 1);
      raceEvent(b, 4.0f);
    }
    prevTop[k] = now;
  }
}

static int leaderLapDisplay() {
  int l = cars[order[0]].lap + 1;
  if (l < 1) l = 1;
  if (raceLaps > 0 && l > raceLaps) l = raceLaps;
  return l;
}
