#pragma once
// Championship: points for every finished race over a 10-race season, the standings, and each driver's
// career records (wins, podiums, poles, fastest laps, titles, win streaks). Saved in NVS, so a season
// carries on after a reboot. Drivers are known by their name: rename one and they start from scratch.
//
// Points go by place in a car's own class (25-18-15-12-10-8-6-4-2-1), so in Endurance the first GT scores
// as much as the first prototype, and the prototypes go to the drivers who've had the fewest so far.

#include "settings.h"

#define SEASON_RACES  10
#define CH_MAX        24        // drivers in a season's table
#define REC_MAX       32        // drivers with career records (the longest unseen is dropped when full)
#define CHAMP_VER     1
static const uint8_t CH_PTS[10] = { 25, 18, 15, 12, 10, 8, 6, 4, 2, 1 };

struct ChampRow { char code[4]; uint16_t pts; uint8_t wins, pods, starts, protos, best, pad; };   // this season
struct ChampBlob { uint16_t ver, season; uint8_t raceNo, n, pad[2]; uint32_t seed; char lastChamp[4]; ChampRow row[CH_MAX]; };
struct RecRow { char code[4]; uint16_t starts, wins, pods, poles, fastLaps, titles; uint8_t streak, bestStreak, pad[2]; uint32_t last; };
struct RecBlob { uint16_t ver, n; uint32_t raceCount; RecRow row[REC_MAX]; };
static ChampBlob champ;
static RecBlob   recs;

// This race
static bool champLive = false;          // it counts: a set number of laps
static bool champRecorded = false;
static bool champFinal = false;         // the last race of the season

// What the results and standings screens show (filled in when a race is scored)
struct StandRow { char code[4]; uint16_t pts; uint8_t wins; int8_t moved; uint8_t gained; };
static StandRow stand[CH_MAX];
static int      standN = 0;
static uint8_t  resPts[MAX_CARS];       // points each car scored
static char     resNote[3][32];         // under each podium place: "First win!", "3 wins in a row", ...
static char     resFoot[56];            // pole and fastest lap, or the GT class winner
static char     standHead[56];          // "Championship after race 4 of 10", "BRD is the champion!"
static char     standSub[56];
static bool     champShown = false;     // there's a standings page for this race

static bool sameCode(const char* a, const char* b) { return a[0] == b[0] && a[1] == b[1] && a[2] == b[2]; }

static ChampRow* champFind(const char* code, bool add) {
  for (int k = 0; k < champ.n; ++k) if (sameCode(champ.row[k].code, code)) return &champ.row[k];
  if (!add || champ.n >= CH_MAX) return nullptr;
  ChampRow* r = &champ.row[champ.n++];
  memset(r, 0, sizeof(*r)); memcpy(r->code, code, 3);
  return r;
}

static RecRow* recFind(const char* code, bool add) {
  for (int k = 0; k < recs.n; ++k) if (sameCode(recs.row[k].code, code)) return &recs.row[k];
  if (!add) return nullptr;
  int k = recs.n;
  if (k >= REC_MAX) { k = 0; for (int j = 1; j < REC_MAX; ++j) if (recs.row[j].last < recs.row[k].last) k = j; }
  else ++recs.n;
  RecRow* r = &recs.row[k];
  memset(r, 0, sizeof(*r)); memcpy(r->code, code, 3);
  return r;
}

static void champSave() {
  prefs.begin(SETTINGS_NS, false);
  prefs.putBytes("champ", &champ, sizeof(champ));
  prefs.putBytes("recs", &recs, sizeof(recs));
  prefs.end();
}

static void champNewSeason() {
  const uint16_t season = champ.season + 1;
  char last[4]; memcpy(last, champ.lastChamp, 4);
  memset(&champ, 0, sizeof(champ));
  champ.ver = CHAMP_VER; champ.season = season;
  champ.seed = esp_random() | 1u;                    // new pace for everyone, new colours
  memcpy(champ.lastChamp, last, 4);
  Serial.printf("Championship: season %u starts\n", season);
}

static void champLoad() {
  prefs.begin(SETTINGS_NS, true);
  const bool okC = prefs.getBytesLength("champ") == sizeof(champ) && prefs.getBytes("champ", &champ, sizeof(champ)) == sizeof(champ) && champ.ver == CHAMP_VER;
  const bool okR = prefs.getBytesLength("recs") == sizeof(recs) && prefs.getBytes("recs", &recs, sizeof(recs)) == sizeof(recs) && recs.ver == CHAMP_VER;
  prefs.end();
  if (!okC) memset(&champ, 0, sizeof(champ));
  if (!okR) { memset(&recs, 0, sizeof(recs)); recs.ver = CHAMP_VER; }
  if (champ.n > CH_MAX) champ.n = 0;
  if (recs.n > REC_MAX) recs.n = 0;
  Serial.printf("Championship: season %u, %u of %d races run, %u drivers with records\n", champ.season, champ.raceNo, SEASON_RACES, recs.n);
}

// Wipe the season (Settings → Drivers → New season); the career records stay.
static void champReset() {
  champ.raceNo = SEASON_RACES;                          // the next race starts a fresh season
  champ.n = 0;
  champSave();
}

// Standings order: points, then wins, then podiums, then best finish.
static bool champBetter(const ChampRow& a, const ChampRow& b) {
  if (a.pts != b.pts) return a.pts > b.pts;
  if (a.wins != b.wins) return a.wins > b.wins;
  if (a.pods != b.pods) return a.pods > b.pods;
  return a.best && (!b.best || a.best < b.best);
}
static int champRanked(int* idx) {
  int n = 0;
  for (int k = 0; k < champ.n; ++k) if (champ.row[k].starts) idx[n++] = k;
  std::sort(idx, idx + n, [](int a, int b) { return champBetter(champ.row[a], champ.row[b]); });
  return n;
}

static const char* ordSuffix(int n) {
  if (n % 100 >= 11 && n % 100 <= 13) return "th";
  switch (n % 10) { case 1: return "st"; case 2: return "nd"; case 3: return "rd"; default: return "th"; }
}

// Before a race is set up: start a new season if the last one is over, fix the field, pick who gets the
// prototypes in Endurance, and write the grid ticker.
static void champPrepareRace(int nCars, uint8_t series, int laps) {
  if (champ.season == 0 || champ.raceNo >= SEASON_RACES) champNewSeason();
  const int n = clampi(nCars, 2, MAX_CARS);
  champLive = laps > 0;
  champRecorded = false; champShown = false;
  champFinal = champ.raceNo + 1 >= SEASON_RACES;
  rosterOn = true; rosterSeed = champ.seed;
  rosterProtoMask = 0;
  if (series == SER_ENDURANCE) {
    // The drivers who've had the fewest prototypes this season get them (ties broken at random, per race).
    int idx[MAX_CARS]; float key[MAX_CARS];
    for (int i = 0; i < n; ++i) {
      const ChampRow* r = champFind(rosterCode(i), false);
      idx[i] = i; key[i] = (r ? r->protos : 0) + (hash32(champ.seed + champ.raceNo * 977u + i * 131u) & 1023) / 2048.0f;
    }
    std::sort(idx, idx + n, [&](int a, int b) { return key[a] < key[b]; });
    for (int k = 0; k < (n + 2) / 3; ++k) rosterProtoMask |= 1u << idx[k];
  }
  resultsSecs = champLive ? (champFinal ? 34.0f : 26.0f) : RESULTS_SECONDS;
}

// After the grid is set (startRace): what the ticker says before the lights.
static void champGridTicker() {
  gridMsg[0][0] = gridMsg[1][0] = 0;
  if (poleCar >= 0) {
    const RecRow* rc = recFind(cars[poleCar].code, false);
    const int poles = (rc ? rc->poles : 0) + 1;
    if (poles == 1) snprintf(gridMsg[0], sizeof(gridMsg[0]), "%s takes a first pole position!", cars[poleCar].code);
    else            snprintf(gridMsg[0], sizeof(gridMsg[0]), "Pole position: %s (%d%s pole)", cars[poleCar].code, poles, ordSuffix(poles));
  }
  if (!champLive) return;
  int idx[CH_MAX]; const int n = champRanked(idx);
  if (n >= 2 && champ.raceNo > 0) {
    const ChampRow &a = champ.row[idx[0]], &b = champ.row[idx[1]];
    if (a.pts == b.pts) snprintf(gridMsg[1], sizeof(gridMsg[1]), "Race %d of %d: %s and %s level on points", champ.raceNo + 1, SEASON_RACES, a.code, b.code);
    else snprintf(gridMsg[1], sizeof(gridMsg[1]), "%s %d of %d: %s leads by %d", champFinal ? "Final race," : "Race", champ.raceNo + 1, SEASON_RACES, a.code, a.pts - b.pts);
  } else snprintf(gridMsg[1], sizeof(gridMsg[1]), "Season %u, race 1 of %d", champ.season, SEASON_RACES);
}

// The race is over (results phase, sim idle): score it, update the records and save.
static void champRecordRace() {
  champRecorded = true;
  if (!champLive) return;
  ++recs.raceCount;
  int before[CH_MAX]; const int nb = champRanked(before);
  int prevRank[CH_MAX]; for (int k = 0; k < CH_MAX; ++k) prevRank[k] = 99;
  for (int k = 0; k < nb; ++k) prevRank[before[k]] = k;
  const int remainingBefore = SEASON_RACES - champ.raceNo;
  const bool clinchedBefore = nb >= 2 && champ.row[before[0]].pts > champ.row[before[1]].pts + 25 * remainingBefore;

  int clsN[CC_COUNT] = { 0 };
  resFoot[0] = 0;
  for (int k = 0; k < numCars; ++k) {
    const int i = order[k];
    const Car& c = cars[i];
    const int p = ++clsN[c.cls];                         // place in class
    const uint8_t pts = !c.out && p <= 10 ? CH_PTS[p - 1] : 0;   // a retirement scores nothing
    resPts[i] = pts;
    ChampRow* r = champFind(c.code, true);
    RecRow* rc = recFind(c.code, true);
    if (r) {
      r->pts += pts; ++r->starts;
      if (p == 1) ++r->wins;
      if (p <= 3) ++r->pods;
      if (!r->best || p < r->best) r->best = p;
      if (raceSeries == SER_ENDURANCE && c.cls == CC_PROTO) ++r->protos;
    }
    rc->last = recs.raceCount; ++rc->starts;
    if (p == 1) { ++rc->wins; ++rc->streak; if (rc->streak > rc->bestStreak) rc->bestStreak = rc->streak; }
    else rc->streak = 0;
    if (p <= 3) ++rc->pods;
    if (i == poleCar) ++rc->poles;
    if (i == fastestCar) ++rc->fastLaps;

    const int grid = gridSlot[i];
    if (k < 3) {
      char* nt = resNote[k]; nt[0] = 0;
      if (p == 1) {
        if (rc->wins == 1)                 snprintf(nt, 32, "First win!");
        else if (rc->streak >= 2)          snprintf(nt, 32, "%d wins in a row", rc->streak);
        else if (grid >= 6)                snprintf(nt, 32, "Won from P%d", grid);
        else                               snprintf(nt, 32, "%d%s win", rc->wins, ordSuffix(rc->wins));
      } else if (p <= 3) {
        if (rc->pods == 1)                 snprintf(nt, 32, "First podium!");
        else if (grid - (k + 1) >= 4)      snprintf(nt, 32, "Up %d places", grid - (k + 1));
      }
    } else if (p == 1 && !resFoot[0]) {
      snprintf(resFoot, sizeof(resFoot), "%s class win: %s%s", CLS[c.cls].name, c.code, rc->wins == 1 ? " (a first win!)" : "");
    }
  }
  if (!resFoot[0]) {
    char t[16] = ""; if (fastestCar >= 0) fmtLapTime(t, sizeof(t), fastestLap);
    if (poleCar >= 0 && fastestCar >= 0) snprintf(resFoot, sizeof(resFoot), "Pole %s    Fastest lap %s %s", cars[poleCar].code, cars[fastestCar].code, t);
  }

  ++champ.raceNo;
  int idx[CH_MAX]; standN = champRanked(idx);
  const int remaining = SEASON_RACES - champ.raceNo;
  for (int k = 0; k < standN; ++k) {
    const ChampRow& r = champ.row[idx[k]];
    StandRow& s = stand[k];
    memcpy(s.code, r.code, 4); s.pts = r.pts; s.wins = r.wins;
    s.moved = prevRank[idx[k]] == 99 ? 0 : (int8_t)clampi(prevRank[idx[k]] - k, -99, 99);
    s.gained = 0;
    for (int i = 0; i < numCars; ++i) if (sameCode(cars[i].code, r.code)) s.gained = resPts[i];
  }
  standSub[0] = 0;
  if (remaining <= 0 && standN > 0) {
    RecRow* rc = recFind(stand[0].code, true);
    ++rc->titles;
    memcpy(champ.lastChamp, stand[0].code, 4);
    snprintf(standHead, sizeof(standHead), "%s is the champion!", stand[0].code);
    if (rc->titles > 1) snprintf(standSub, sizeof(standSub), "Season %u  -  %d%s title", champ.season, rc->titles, ordSuffix(rc->titles));
    else                snprintf(standSub, sizeof(standSub), "Season %u  -  a first title", champ.season);
    char b[48]; snprintf(b, sizeof(b), "%s WINS THE CHAMPIONSHIP!", stand[0].code);
    raceEvent(b, 0, 6000);
  } else {
    snprintf(standHead, sizeof(standHead), "Championship  -  after race %d of %d", champ.raceNo, SEASON_RACES);
    const bool clinched = standN >= 2 && stand[0].pts > stand[1].pts + 25 * remaining;
    if (clinched && !clinchedBefore) {
      snprintf(standSub, sizeof(standSub), "%s has clinched the title!", stand[0].code);
      char b[48]; snprintf(b, sizeof(b), "%s CLINCHES THE TITLE!", stand[0].code);
      raceEvent(b, 0, 6000);
    } else if (standN >= 2) {
      snprintf(standSub, sizeof(standSub), "%d race%s to go  -  %d points left", remaining, remaining == 1 ? "" : "s", 25 * remaining);
    }
  }
  champShown = standN > 0;
  champSave();
  Serial.printf("Championship: race %u of %d scored, leader %s %u pts\n", champ.raceNo, SEASON_RACES, standN ? stand[0].code : "-", standN ? stand[0].pts : 0);
}

static void champTick() {
  if (phase == RP_RESULTS && !champRecorded) champRecordRace();
}
