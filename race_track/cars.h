#pragma once
// Race cars: classes, driver traits, liveries, driving physics/AI and drawing.
//
// Each car has a real position, heading and speed, and steers like a car: a
// path-tracking controller follows the line the driver wants (the racing line,
// or an offset line when passing or defending), feeding forward that line's
// curvature and correcting lateral and heading error, with a limited steering
// rate. Cornering is capped by tyre grip, so a car that arrives too fast can't
// turn tightly enough: it slides wide, scrubs speed, and may end up on the
// grass, in the gravel, in the wall, or spinning. Mistakes come out of that
// physics rather than being scripted.
//
// Driver traits change how hard drivers push (how much grip they *think* they
// have, how late they brake, how they adapt to the wet) and their racecraft
// (when they attack, how much room they need, how close they follow, whether
// they defend or wave faster cars through).

#include "effects.h"

enum CarClass : uint8_t { CC_FORMULA, CC_PROTO, CC_GT, CC_TOURING, CC_CLASSIC, CC_COUNT };
struct ClassSpec { const char* name; float len, wid, top, grip, acc; };
static const ClassSpec CLS[CC_COUNT] = {
  { "Formula",   14.0f, 5.8f, 1.00f, 1.00f, 1.00f },
  { "Prototype", 14.5f, 6.4f, 0.97f, 0.93f, 0.92f },
  { "GT",        13.5f, 6.2f, 0.88f, 0.82f, 0.80f },
  { "Touring",   12.5f, 5.8f, 0.82f, 0.78f, 0.76f },
  { "Classic",   12.5f, 4.8f, 0.80f, 0.70f, 0.74f },
};

enum Series : uint8_t { SER_FORMULA, SER_GT, SER_TOURING, SER_ENDURANCE, SER_CLASSIC, SER_COUNT };
static const char* const SERIES_NAMES[SER_COUNT] = { "Formula", "GT", "Touring", "Endurance", "Classic" };

// ─── Driver traits ────────────────────────────────────────────────────────────
enum Trait : uint8_t { TR_AGGRESSIVE, TR_BALANCED, TR_CAUTIOUS, TR_PATIENT, TR_DEFENSIVE, TR_COUNT };
struct TraitSpec {
  const char* name; uint32_t col;
  float corner;       // cornering speed vs the grip the car really has (sqrt-scaled; >1.1 slides)
  float straight;     // top-speed commitment
  float brake;        // braking decel they plan for (higher = brakes later)
  float wetAdapt;     // 1 = fully respects lost grip in the wet; <1 = overestimates it
  float sigma;        // corner-to-corner variation in commitment
  float lineAware;    // how much they slow when forced off the racing line
  float passMargin;   // speed advantage (px/s) needed before attacking
  float passGap;      // starts an attack within this many car lengths
  float passRoom;     // extra room (px) wanted alongside before committing
  float followGap;    // following distance, in car lengths
  float mistake;      // lapse multiplier
  float spin;         // spin-when-sliding multiplier
  bool  cornerAttack; // will attack on corner entry, not only on straights
  bool  yields;       // moves aside for a faster car behind
  bool  blocks;       // covers the inside when attacked
};
static const TraitSpec TRAITS[TR_COUNT] = {
  //  name          col        corner straight brake wet   sigma  aware margin gap  room  follow mist spin  attack yield block
  { "Aggressive", 0xFF5040u, 1.100f, 1.015f, 1.14f, 0.70f, 0.050f, 0.35f,  1.0f, 4.0f, -0.8f, 0.22f, 2.2f, 2.2f, true,  false, false },
  { "Balanced",   0xE8E8E8u, 1.000f, 1.000f, 1.00f, 0.92f, 0.025f, 0.75f,  3.0f, 3.0f,  0.0f, 0.45f, 1.0f, 1.0f, false, false, false },
  { "Cautious",   0x60A8FFu, 0.960f, 0.995f, 0.92f, 1.00f, 0.012f, 1.00f,  6.0f, 2.6f,  0.8f, 0.70f, 0.5f, 0.5f, false, false, false },
  { "Patient",    0x60D080u, 0.940f, 0.990f, 0.88f, 1.05f, 0.008f, 1.00f, 10.0f, 2.2f,  1.2f, 0.95f, 0.3f, 0.3f, false, true,  false },
  { "Defensive",  0xFFB040u, 0.990f, 1.000f, 1.02f, 0.92f, 0.022f, 0.75f,  3.0f, 3.0f,  0.0f, 0.40f, 0.9f, 1.0f, false, false, true  },
};
static const uint8_t TRAIT_WEIGHTS[TR_COUNT] = { 25, 30, 20, 10, 15 };

enum CarState : uint8_t { CS_RACE, CS_SPIN, CS_STOPPED, CS_RECOVER };

#define MAX_CARS 20
#define NCP      32         // timing checkpoints per lap (for gaps)

struct Car {
  float dist;               // centreline distance since the start (negative on the grid)
  float s, d, v;            // projected track position, lateral offset, speed (px/s)
  float x, y, px, py;       // world position, previous position
  float heading;            // direction of travel (rad)
  float kappa;              // current steering curvature (1/px, + = right)
  float slip;               // visual yaw from sliding
  float dPath;              // lateral offset of the line the driver is following
  float commit;             // commitment for the coming corner (×1)
  float spinV, travel;      // spinning: yaw rate, direction of the slide
  float perf, skill;
  float react;              // start reaction delay (s)
  float passT; int8_t passSide; int8_t passCar;
  float blockT, blockCd, yieldT; int8_t yieldSide;
  float lockT, contactCd, recoverT;
  float stopT;
  float lapStart, lastLap, bestLap;
  int   lap;                // completed laps (−1 before first crossing)
  int   lapsAtFlag;
  bool  finished; int finishPos;
  bool  braking;
  CarState state;
  uint8_t cls, trait;
  uint8_t num;
  uint32_t c1, c2, c3;      // livery
  char  code[4];
  int   lastCp; float cpT[NCP]; int16_t cpLap[NCP];
  int   pos;
  int   nextCorner;
  int8_t surface;           // 0 track, 1 gravel, 2 grass
  uint16_t incidents[4];    // per EvType, this race
  float dbgVT; int8_t dbgAhead;   // last target speed / car followed (debugging)
  float progT, progDist;          // progress watchdog
};
static Car cars[MAX_CARS];
static int numCars = 14;
static int order[MAX_CARS];
static uint8_t raceSeries = SER_FORMULA;
static float raceTime = 0;

// ─── Simulation events for the race director (race.h) ─────────────────────────
enum EvType : uint8_t { EV_SPIN, EV_WALL, EV_CONTACT, EV_GRAVEL };
struct SimEvent { uint8_t type; int8_t a, b; };
static SimEvent evQ[8];
static int evN = 0;
static void pushEv(uint8_t t, int a, int b = -1);

static const char* const DRIVER_CODES[] = {
  "ARD", "BEL", "CRN", "DAV", "EKS", "FON", "GUL", "HAR", "IST", "JOV", "KAR", "LIN", "MOT", "NYG", "ODE",
  "PAL", "QAS", "REN", "SKO", "TAV", "ULM", "VDB", "WEX", "YAM", "ZEL", "BRO", "CAL", "DUN", "FIN", "KEL",
  "MIR", "NOV", "OST", "RAD", "SOL", "TOR", "VAN", "WIL", "ACH", "BAK" };
#define NUM_CODES ((int)(sizeof(DRIVER_CODES) / sizeof(DRIVER_CODES[0])))

static const uint32_t TEAM_COLS[][3] = {
  { 0xD8202Au, 0xF0F0F0u, 0xF0D020u }, { 0x1E3A8Au, 0xF0C020u, 0xE02020u }, { 0x0A7A5Au, 0xE8E8E8u, 0x40E0B0u },
  { 0xF08020u, 0x202020u, 0x40A0F0u }, { 0x141414u, 0xD8B040u, 0xF0F0F0u }, { 0xF0F0F0u, 0x2050C0u, 0xE02030u },
  { 0x7A1A8Au, 0x30C0F0u, 0xF0F0F0u }, { 0x40C0E0u, 0xF060A0u, 0xF0F0F0u }, { 0xF0D020u, 0x1A1A1Au, 0x2050C0u },
  { 0x2A2A2Au, 0xE02020u, 0xF0F0F0u }, { 0xA0A8B0u, 0x10B0A0u, 0x101010u }, { 0x0A2A5Au, 0xE04030u, 0xF0F0F0u },
  { 0x5ACA3Au, 0x103010u, 0xF0F0F0u }, { 0xE85A9Au, 0xF0F0F0u, 0x1A1A1Au } };
#define NUM_TEAMS 14
static const uint32_t CLASSIC_COLS[][3] = {
  { 0x1E4A2Eu, 0xE8E0C8u, 0xD8C040u }, { 0xC0201Au, 0xE8E0C8u, 0xF0F0F0u }, { 0x2A50B0u, 0xF0F0F0u, 0xE02020u },
  { 0xC8CCD0u, 0x202020u, 0xF0F0F0u }, { 0xF0F0E8u, 0x2A50B0u, 0x2A50B0u }, { 0xE8C830u, 0x202020u, 0xE02020u } };

static void pushEv(uint8_t t, int a, int b) {
  if (a >= 0 && cars[a].incidents[t] < 65535) ++cars[a].incidents[t];
  if (evN < 8) evQ[evN++] = { t, (int8_t)a, (int8_t)b };
}

// ─── Setup ────────────────────────────────────────────────────────────────────
static void sortCornersByS() {
  std::sort(trk.corners, trk.corners + trk.nCorners, [](const Corner& a, const Corner& b) { return a.i0 < b.i0; });
}

static int firstCornerAfter(float s) {
  for (int i = 0; i < trk.nCorners; ++i) if (trk.corners[i].i0 * trk.ds > s) return i;
  return 0;
}

static void placeOnTrack(Car& c) {
  TrackFrame f; trackAt(c.s, f);
  c.x = f.x - f.ty * c.d; c.y = f.y + f.tx * c.d;
}

static uint8_t pickTrait() {
  int total = 0; for (int t = 0; t < TR_COUNT; ++t) total += TRAIT_WEIGHTS[t];
  int r = irandr(0, total);
  for (int t = 0; t < TR_COUNT; ++t) { if (r < TRAIT_WEIGHTS[t]) return (uint8_t)t; r -= TRAIT_WEIGHTS[t]; }
  return TR_BALANCED;
}

static void setupCars(int n, uint8_t series, uint32_t seed) {
  uint32_t save = _rng;
  _rng = seed * 2246822519u + 77u; if (!_rng) _rng = 1;
  numCars = clampi(n, 2, MAX_CARS);
  raceSeries = series;
  sortCornersByS();
  int codes[NUM_CODES];
  for (int i = 0; i < NUM_CODES; ++i) codes[i] = i;
  for (int i = NUM_CODES - 1; i > 0; --i) { int j = irandr(0, i + 1); int t = codes[i]; codes[i] = codes[j]; codes[j] = t; }
  int teamOff = irandr(0, NUM_TEAMS);
  float quali[MAX_CARS];
  for (int i = 0; i < numCars; ++i) {
    Car& c = cars[i];
    memset(&c, 0, sizeof(Car));
    switch (series) {
      case SER_GT:        c.cls = CC_GT; break;
      case SER_TOURING:   c.cls = CC_TOURING; break;
      case SER_CLASSIC:   c.cls = CC_CLASSIC; break;
      case SER_ENDURANCE: c.cls = (i < (numCars + 2) / 3) ? CC_PROTO : CC_GT; break;
      default:            c.cls = CC_FORMULA; break;
    }
    if (series == SER_FORMULA) {
      const uint32_t* t = TEAM_COLS[(teamOff + i / 2) % NUM_TEAMS];
      c.c1 = t[0]; c.c2 = t[1]; c.c3 = (i & 1) ? t[2] : t[1];
    } else if (series == SER_CLASSIC) {
      const uint32_t* t = CLASSIC_COLS[(teamOff + i) % 6];
      c.c1 = t[0]; c.c2 = t[1]; c.c3 = t[2];
    } else {
      const uint32_t* t = TEAM_COLS[(teamOff + i) % NUM_TEAMS];
      bool swap = (i / NUM_TEAMS) & 1;
      c.c1 = swap ? t[1] : t[0]; c.c2 = swap ? t[0] : t[1]; c.c3 = t[2];
    }
    memcpy(c.code, DRIVER_CODES[codes[i % NUM_CODES]], 3); c.code[3] = 0;
    c.num = (uint8_t)irandr(1, 99);
    c.trait = pickTrait();
    c.perf = frandr(0.962f, 1.0f);
    c.skill = frandr(0.86f, 0.99f);
    c.react = frandr(0.05f, 0.45f) * (c.trait == TR_AGGRESSIVE ? 0.7f : (c.trait == TR_PATIENT ? 1.3f : 1.0f));
    c.commit = 1.0f;
    const TraitSpec& T = TRAITS[c.trait];
    quali[i] = c.perf * CLS[c.cls].top * (1.0f + 0.3f * (sqrtf(T.corner) - 1.0f)) + frandr(-0.015f, 0.015f);   // one hot lap
  }
  int gridOrd[MAX_CARS];
  for (int i = 0; i < numCars; ++i) gridOrd[i] = i;
  std::sort(gridOrd, gridOrd + numCars, [&](int a, int b) { return quali[a] > quali[b]; });
  for (int k = 0; k < numCars; ++k) {
    Car& c = cars[gridOrd[k]];
    c.dist = gridSlotS(k);
    c.s = wrapS(c.dist);
    c.d = c.dPath = gridSlotD(k);
    c.v = 0; c.state = CS_RACE;
    c.lap = -1; c.lapsAtFlag = -1; c.lastCp = -1;
    c.passCar = -1;
    c.nextCorner = firstCornerAfter(c.s > trk.L * 0.5f ? 0 : c.s);
    placeOnTrack(c);
    TrackFrame f; trackAt(c.s, f);
    c.heading = atan2f(f.ty, f.tx);
    c.px = c.x; c.py = c.y;
    order[k] = gridOrd[k];
    c.pos = k + 1;
  }
  evN = 0;
  _rng = save;
}

// ─── Physics helpers ──────────────────────────────────────────────────────────
static inline float gripFactor() { return 1.0f - 0.14f * wetAmt - 0.14f * snowCover; }

// Surface under a car: 0 track/kerb/tarmac run-off, 1 gravel, 2 grass.
static int8_t surfaceAt(float s, float d) {
  float ad = fabsf(d);
  if (ad < trk.hw + KERB_W * 0.8f) return 0;
  const TrackPt& p = tp[idxAt(s)];
  int side = d > 0 ? 1 : 0;
  if (p.run[side] == 2) return 0;
  if (p.run[side] == 1 && ad > trk.hw + KERB_W + 2.5f) return 1;
  return 2;
}

// Furthest the car's centre can be from the centreline on a side; `wall` says
// whether that limit is a barrier (true) or just the edge of the verge.
static float sideLimit(float s, int side, float halfW, bool& wall) {
  const TrackPt& p = tp[idxAt(s)];
  wall = p.bar[side] > 0;
  return (wall ? p.bar[side] : trk.hw + 18.0f) - halfW - 0.5f;
}

// Update s, d (and dist) from the world position: nearest centreline segment
// near the previous s.
static void projectCar(Car& c) {
  int i0 = idxAt(c.s);
  float best = 1e18f, bt = 0, bd = 0; int bi = i0;
  for (int k = -8; k <= 8; ++k) {
    int i = wrapI(i0 + k), j = wrapI(i + 1);
    float ax = tp[i].x, ay = tp[i].y, ex = tp[j].x - ax, ey = tp[j].y - ay;
    float l2 = ex * ex + ey * ey; if (l2 < 1e-6f) continue;
    float t = clampf(((c.x - ax) * ex + (c.y - ay) * ey) / l2, 0, 1);
    float qx = ax + ex * t - c.x, qy = ay + ey * t - c.y, d2 = qx * qx + qy * qy;
    if (d2 < best) { best = d2; bi = i; bt = t; bd = ((c.x - ax) * -ey + (c.y - ay) * ex) / sqrtf(l2); }
  }
  float ns = (bi + bt) * trk.ds;
  c.dist += sDelta(c.s, ns);
  c.s = wrapS(ns);
  c.d = bd;
}

// Approaching a corner: decide how hard to commit, and maybe make a mistake.
static void cornerEntry(Car& c) {
  const TraitSpec& T = TRAITS[c.trait];
  float g = (frand() + frand() + frand() - 1.5f) * 2.0f;            // ~N(0,1)
  c.commit = 1.0f + g * T.sigma * (1.6f - c.skill);
  float p = (1.0f - c.skill) * 0.09f * T.mistake * (1.0f + 2.5f * wetAmt + 3.0f * snowCover);
  if (frand() < p) { c.commit += frandr(0.08f, 0.24f); c.lockT = 0.35f; }  // a lapse: brakes far too late
}

static void startSpin(Car& c, int ci, float dir) {
  c.state = CS_SPIN;
  c.spinV = dir * frandr(6.0f, 10.0f);
  c.travel = c.heading;
  c.slip = 0;
  pushEv(EV_SPIN, ci);
}

// ─── Simulation step ──────────────────────────────────────────────────────────
static void updateCars(float dt, bool racing, bool coolDown) {
  const float hw = trk.hw;
  const float wx = gripFactor();
  evN = 0;
  for (int i = 0; i < numCars; ++i) {
    Car& c = cars[i];
    const ClassSpec& cs = CLS[c.cls];
    const TraitSpec& T = TRAITS[c.trait];
    TrackFrame f; trackAt(c.s, f);
    const float trackAng = atan2f(f.ty, f.tx);
    const float hwC = hw - cs.wid * 0.5f - 0.8f;
    const float gCar = cs.grip * c.perf * c.perf;
    const float surfG = c.surface == 0 ? 1.0f : (c.surface == 1 ? 0.45f : 0.55f);
    const float aLatMax = A_LAT * gCar * wx * 1.12f * surfG;           // what the tyres can really do
    const float aBrkMax = A_BRK * 1.3f * sqrtf(gCar) * wx * (c.surface ? 0.6f : 1.0f);
    c.px = c.x; c.py = c.y;

    if (c.state == CS_SPIN) {
      c.heading += c.spinV * dt; c.spinV *= 0.965f;
      c.v = fmx(0, c.v - 140.0f * dt);
      c.x += cosf(c.travel) * c.v * dt; c.y += sinf(c.travel) * c.v * dt;
      if (frand() < 0.6f) spawnParticle(PT_SMOKE, c.x, c.y, frandr(-0.3f, 0.3f), frandr(-0.3f, 0.3f), 2.5f, 0.12f, 26);
      if (c.v < 2 && fabsf(c.spinV) < 1.6f) { c.state = CS_STOPPED; c.stopT = frandr(0.8f, 2.0f); c.v = 0; }
    } else if (c.state == CS_STOPPED) {
      c.v = 0;
      c.stopT -= dt;
      if (c.stopT <= 0) c.state = CS_RECOVER;
    } else if (c.state == CS_RECOVER && fabsf(wrapAngle(trackAng - c.heading)) > 0.3f) {
      // turn round to face the right way before driving off
      float e = wrapAngle(trackAng - c.heading);
      c.heading += (e > 0 ? 1.0f : -1.0f) * fmn(fabsf(e), 2.6f * dt);
      c.v = 0;
    } else {
      if (c.state == CS_RECOVER) { c.state = CS_RACE; c.recoverT = 2.5f; c.dPath = c.d; c.kappa = 0; }

      // ── Target speed: the driver's own braking for what they think the grip is ──
      const float wxBelief = powf(wx, T.wetAdapt);
      const float gT = gCar * wxBelief * T.corner * c.commit;
      const float ab = A_BRK * T.brake * sqrtf(gCar) * wxBelief;
      float vT = V_TOP * cs.top * c.perf * T.straight;
      const float sg = sqrtf(gT);
      for (float u = 0; u <= 180.0f; u += 10.0f) {
        float vc = tp[idxAt(c.s + u)].vc * sg;
        float lim = vc * vc + 2.0f * ab * u;
        if (lim < vT * vT) vT = sqrtf(lim);
      }
      float off = clampf(fabsf(c.dPath - f.rl) / hw, 0, 1);
      vT *= 1.0f - 0.12f * T.lineAware * off * clampf(fabsf(f.k) * 80.0f, 0, 1);
      if (coolDown || c.finished) vT *= 0.6f;
      if (c.recoverT > 0) { c.recoverT -= dt; vT = fmn(vT, 45.0f); }
      const float vFree = vT;                      // before traffic

      // ── Traffic ──
      int ahead = -1; float aheadGap = 1e9f;
      int attacker = -1; float attackGap = 1e9f;
      for (int j = 0; j < numCars; ++j) {
        if (j == i) continue;
        const Car& o = cars[j];
        float g = sDelta(c.s, o.s);
        float widAvg = (cs.wid + CLS[o.cls].wid) * 0.5f;
        if (g > 0 && g < 70 && fabsf(o.d - c.d) < widAvg + 1.5f && g < aheadGap) { ahead = j; aheadGap = g; }
        if (g < 0 && -g < 3.0f * cs.len && o.state == CS_RACE && o.passT > 0 && o.passCar == i && -g < attackGap) { attacker = j; attackGap = -g; }
      }
      float dT = f.rl + (tp[idxAt(c.s + 14.0f)].rl - f.rl);
      const bool cornerNear = fabsf(f.k) > 1.0f / 150.0f || fabsf(tp[idxAt(c.s + 45.0f)].k) > 1.0f / 150.0f;

      if (ahead >= 0) {
        const Car& a = cars[ahead];
        float lenAvg = (cs.len + CLS[a.cls].len) * 0.5f, widAvg = (cs.wid + CLS[a.cls].wid) * 0.5f;
        float gap = aheadGap - lenAvg;
        bool lapping = c.dist - a.dist > trk.L * 0.5f;
        bool faster = vFree > a.v + T.passMargin || a.state != CS_RACE || (lapping && vFree > a.v);
        bool allowed = T.cornerAttack || !cornerNear || a.state != CS_RACE || lapping;
        if (c.passT <= 0 && faster && allowed && gap < T.passGap * lenAvg && racing && !coolDown) {
          const float hwP = hwC + 1.8f;            // may use the kerbs
          float roomR = hwP - (a.d + widAvg + 1.2f) - T.passRoom;
          float roomL = (a.d - widAvg - 1.2f) + hwP - T.passRoom;
          if (roomR > 0 || roomL > 0) {
            // prefer the inside of the next corner if there's room there
            float kN = tp[idxAt(c.s + 50.0f)].k;
            int inside = kN > 0 ? 1 : -1;
            float roomIn = inside > 0 ? roomR : roomL, roomOut = inside > 0 ? roomL : roomR;
            c.passSide = (roomIn > 0 && (roomIn >= roomOut - 2.0f || T.cornerAttack)) ? inside : -inside;
            c.passT = 2.5f; c.passCar = (int8_t)ahead;
            if (T.cornerAttack && cornerNear) c.commit = fmx(c.commit, 1.03f);   // late on the brakes
          }
        }
        float fg = T.followGap * lenAvg;
        if (gap < lenAvg * 1.5f) vT = fmn(vT, fmx(0.0f, a.v + (gap - fg) * 2.2f));
        if (gap < lenAvg * 3.0f && fabsf(f.k) < 1.0f / 400.0f) vT *= 1.0f + 0.04f * T.straight;   // slipstream
      }
      if (c.passT > 0 && c.passCar >= 0) {
        const Car& a = cars[c.passCar];
        float widAvg = (cs.wid + CLS[a.cls].wid) * 0.5f;
        dT = clampf(a.dPath + c.passSide * (widAvg + 1.5f), -hwC - 1.8f, hwC + 1.8f);
        c.passT -= dt;
        if (sDelta(a.s, c.s) > (cs.len + CLS[a.cls].len) * 0.5f + 3.0f) c.passT = fmn(c.passT, 0.5f);   // done
        if (fabsf(sDelta(a.s, c.s)) > 90.0f) c.passT = 0;
      }
      // Being attacked: defend, or let them by
      if (attacker >= 0 && racing && !coolDown) {
        const Car& o = cars[attacker];
        bool lapped = o.dist - c.dist > trk.L * 0.5f;
        if (T.yields || lapped) {
          if (c.yieldT <= 0) { c.yieldT = 2.0f; c.yieldSide = (int8_t)(o.passSide > 0 ? -1 : 1); }
        } else if (T.blocks && c.blockCd <= 0 && !cornerNear && attackGap < 2.0f * cs.len) {
          c.blockT = 1.0f; c.blockCd = 5.0f;
        }
      }
      if (c.yieldT > 0) { c.yieldT -= dt; dT = c.yieldSide * hwC * 0.75f; vT = fmn(vT, vFree * 0.97f); }
      if (c.blockT > 0 && attacker >= 0) { c.blockT -= dt; dT = clampf(cars[attacker].dPath, -hwC, hwC); }
      else if (c.blockT > 0) c.blockT -= dt;
      if (c.blockCd > 0) c.blockCd -= dt;

      if (!racing || raceTime < c.react) { vT = 0; dT = c.d; }
      c.dbgVT = vT; c.dbgAhead = (int8_t)ahead;

      // ── Lane: the offset of the line being followed eases toward the target ──
      float laneRate = 0.24f * c.v + (c.v > 4.0f ? 5.0f : 0.0f);
      float oldPath = c.dPath;
      c.dPath += clampf(dT - c.dPath, -laneRate * dt, laneRate * dt);
      c.dPath = clampf(c.dPath, -hw - 6.0f, hw + 6.0f);

      // ── Longitudinal ──
      float accel = A_ACC * cs.acc * c.perf * (1.15f - 0.5f * c.v / (V_TOP * cs.top)) * wx * (c.surface == 1 ? 0.4f : (c.surface ? 0.6f : 1.0f));
      c.braking = vT < c.v - 2.0f;
      if (c.v < vT) c.v = fmn(vT, c.v + accel * dt);
      else {
        float need = (c.v - vT) / dt;
        c.v -= fmn(need, aBrkMax) * dt;
        if (need > aBrkMax * 1.05f && c.v > 50.0f) c.lockT = fmx(c.lockT, 0.1f);   // over the limit: locking up
      }

      // ── Steering: follow the chosen line ──
      const float La = 10.0f + 0.22f * c.v;
      const float slope = clampf((c.dPath - oldPath) / fmx(c.v * dt, 0.5f), -0.6f, 0.6f);
      const float psiP = trackAng + slope;
      // the driven line runs parallel to the racing line (offset dPath - rl)
      const float kff = f.rk / clampf(1.0f - f.rk * (c.dPath - f.rl), 0.5f, 1.5f);
      const float eD = c.dPath - c.d, ePsi = wrapAngle(psiP - c.heading);
      float kc;
      if (fabsf(ePsi) > 0.6f) kc = (ePsi > 0 ? 1.0f : -1.0f) * 0.13f;          // way off line: full lock back toward it
      else kc = kff + 2.0f * (clampf(eD, -0.6f * La, 0.6f * La) + ePsi * La) / (La * La);
      const float KR = 0.36f * dt;                  // steering rate limit (curvature per step)
      kc = clampf(kc, c.kappa - KR, c.kappa + KR);
      kc = clampf(kc, -0.13f, 0.13f);
      // Grip. Drivers keep their own corrections inside the grip they know they
      // have; only arriving too fast for the corner itself (the line's own
      // curvature, kff) overwhelms the tyres, and then the car slides wide.
      const float kMax = aLatMax / fmx(c.v * c.v, 1.0f);
      const float cap = kMax * (T.cornerAttack ? 1.04f : 0.97f);
      kc = clampf(kc, -cap, cap);
      const float demand = fabsf(kff) / kMax;
      float slipT = 0;
      if (demand > 1.0f) {
        float sgn = kff > 0 ? 1.0f : -1.0f;
        kc = sgn * fmn(fabsf(kc), kMax);
        float over = fmn(demand - 1.0f, 1.0f);
        c.v = fmx(0.0f, c.v - over * 55.0f * dt);                       // scrubbing speed
        slipT = sgn * fmn(0.45f, over * 0.8f);
        if (over > 0.15f && frand() < 0.5f) spawnParticle(PT_SMOKE, c.x, c.y, frandr(-0.2f, 0.2f), frandr(-0.2f, 0.2f), 1.8f, 0.09f, 18);
        if (demand > 1.1f && c.v > 40.0f &&
            frand() < 0.01f * (demand - 1.1f) / 0.1f * T.spin * (1.0f + 1.5f * wetAmt) * (c.surface ? 2.0f : 1.0f))
        {
#ifdef RT_DEBUG_SPIN
          printf("  spin-slide %s s=%.0f d=%.1f v=%.0f demand=%.2f surf=%d k=%.4f rk=%.4f\n", c.code, c.s, c.d, c.v, demand, c.surface, f.k, f.rk);
#endif
          startSpin(c, i, frand() < 0.7f ? sgn : -sgn);
        }
      }
      c.kappa = kc;
      c.slip = lerpf(c.slip, slipT, 0.15f);
      if (c.state == CS_RACE) {
        c.heading += c.v * c.kappa * dt;
        c.x += cosf(c.heading) * c.v * dt; c.y += sinf(c.heading) * c.v * dt;
      }
      if (c.lockT > 0) {
        c.lockT -= dt;
        if (frand() < 0.5f) spawnParticle(PT_SMOKE, c.x, c.y, frandr(-0.2f, 0.2f), frandr(-0.2f, 0.2f), 2.0f, 0.10f, 22);
      }
    }


    // ── Where are we on the track now? ──
    projectCar(c);

    int8_t prevSurf = c.surface;
    c.surface = surfaceAt(c.s, c.d);
    if (c.surface == 1) {
      c.v -= c.v * 2.6f * dt;                                   // gravel: heavy drag, but you can crawl out
      if (c.v > 5 && frand() < 0.7f) spawnParticle(PT_DUST, c.x, c.y, frandr(-0.4f, 0.4f), frandr(-0.4f, 0.4f), 2.0f, 0.18f, 30);
      if (prevSurf != 1 && c.state == CS_RACE) pushEv(EV_GRAVEL, i);
    } else if (c.surface == 2) {
      c.v -= c.v * 0.9f * dt;                                   // grass
      if (c.v > 10 && frand() < 0.35f) spawnParticle(PT_GRASS, c.x, c.y, frandr(-0.3f, 0.3f), frandr(-0.3f, 0.3f), 1.2f, 0.05f, 18);
    }
    // ── Barriers ──
    int side = c.d > 0 ? 1 : 0;
    bool wall;
    float lim = sideLimit(c.s, side, cs.wid * 0.5f, wall);
    if (fabsf(c.d) > lim) {
      TrackFrame g; trackAt(c.s, g);
      float ang = atan2f(g.ty, g.tx);
      float rel = wrapAngle(c.heading - ang);
      float vPerp = c.v * fabsf(sinf(rel));
      c.d = side ? lim : -lim;
      c.x = g.x - g.ty * c.d; c.y = g.y + g.tx * c.d;
      c.dPath = c.d;
      if (c.state == CS_SPIN || c.state == CS_RACE || c.state == CS_RECOVER) {
        c.heading = ang + (side ? -0.12f : 0.12f);                   // glance off, nose back toward the track
        c.kappa = 0;
      }
      if (wall) {
        c.v *= 0.80f - 0.35f * clampf(vPerp / 60.0f, 0, 1);
        for (int k = 0; k < 3; ++k) spawnParticle(PT_SPARK, c.x, c.y, frandr(-1.2f, 1.2f), frandr(-1.2f, 1.2f), 0.8f, 0, irandr(4, 9));
        if (vPerp > 28.0f && c.state != CS_STOPPED) {
          c.state = CS_STOPPED; c.stopT = frandr(1.5f, 3.0f); c.v = 0; c.spinV = 0;
          for (int k = 0; k < 5; ++k) spawnParticle(PT_SMOKE, c.x, c.y, frandr(-0.5f, 0.5f), frandr(-0.5f, 0.5f), 2.0f, 0.15f, 30);
          pushEv(EV_WALL, i);
        }
      } else {
        c.v *= 0.97f;
      }
    }


    // ── Spray in the wet ──
    if (wetAmt > 0.3f && c.v > 60.0f && ((frameCount + i) & 1)) {
      float h = c.heading, back = -cs.len * 0.55f;
      spawnParticle(PT_SPRAY, c.x + fastCos(h) * back, c.y + fastSin(h) * back,
                    -fastCos(h) * c.v * 0.004f + frandr(-0.2f, 0.2f), -fastSin(h) * c.v * 0.004f + frandr(-0.2f, 0.2f),
                    2.0f, 0.22f * wetAmt, 18);
    }
    if (c.contactCd > 0) c.contactCd -= dt;
  }

  // ── Cars alongside: keep them apart; hard contact if someone forces it ──
  for (int i = 0; i < numCars; ++i)
    for (int j = i + 1; j < numCars; ++j) {
      Car& a = cars[i]; Car& b = cars[j];
      float lenAvg = (CLS[a.cls].len + CLS[b.cls].len) * 0.5f, widAvg = (CLS[a.cls].wid + CLS[b.cls].wid) * 0.5f;
      float g = sDelta(a.s, b.s);
      if (fabsf(g) > lenAvg) continue;
      float dd = b.d - a.d, need = widAvg + 0.6f;
      if (fabsf(dd) >= need) continue;
      Car& rear = g > 0 ? a : b; Car& front = g > 0 ? b : a;
      int ri = g > 0 ? i : j, fi = g > 0 ? j : i;
      float closing = rear.v - front.v;
      bool hard = fabsf(dd) < need * 0.75f && fabsf(g) < lenAvg * 0.9f && closing > 6.0f &&
                  rear.state == CS_RACE && front.state == CS_RACE && rear.contactCd <= 0 && front.contactCd <= 0;
      if (hard) {
        rear.contactCd = front.contactCd = 4.0f;
        rear.v *= 0.85f; front.v *= 0.95f;
        bool rough = rear.trait == TR_AGGRESSIVE || front.trait == TR_DEFENSIVE;
        if (frand() < (rough ? 0.30f : 0.12f)) {
#ifdef RT_DEBUG_SPIN
          printf("  spin-contact %s by %s s=%.0f\n", front.code, rear.code, front.s);
#endif
          startSpin(front, fi, dd > 0 ? 1.0f : -1.0f);
        }
        for (int k = 0; k < 2; ++k) spawnParticle(PT_SPARK, (rear.x + front.x) * 0.5f, (rear.y + front.y) * 0.5f, frandr(-1, 1), frandr(-1, 1), 0.8f, 0, 6);
        pushEv(EV_CONTACT, ri, fi);
      }
      if (fabsf(dd) < need * 0.45f) { rear.v = fmn(rear.v, front.v * 0.97f); continue; }   // nose to tail
      float push = (need - fabsf(dd)) * 0.5f * (dd >= 0 ? 1.0f : -1.0f);
      a.d -= push; b.d += push;
      placeOnTrack(a); placeOnTrack(b);
    }

  // ── Corners, laps, timing ──
  for (int i = 0; i < numCars; ++i) {
    Car& c = cars[i];
    // Progress watchdog: a car that's tangled up somewhere (pinned against
    // another car, or facing the wrong way) re-orients and pulls away.
    if (racing && raceTime > c.react + 3.0f && !c.finished) {
      c.progT += dt;
      if (c.progT >= 3.0f) {
        if (c.dist - c.progDist < 3.0f && c.state == CS_RACE) { c.state = CS_RECOVER; c.v = 0; c.kappa = 0; }
        c.progT = 0; c.progDist = c.dist;
      }
    } else { c.progT = 0; c.progDist = c.dist; }
    if (trk.nCorners > 0) {
      const Corner& C = trk.corners[c.nextCorner];
      float toC = sDelta(c.s, C.i0 * trk.ds);
      if (toC < 170.0f) {                                  // decide before the braking zone starts
        if (toC > -50.0f && racing && c.state == CS_RACE) cornerEntry(c);
        c.nextCorner = (c.nextCorner + 1) % trk.nCorners;
      }
    }
    int lapNow = (int)floorf(c.dist / trk.L);
    if (lapNow > c.lap) {
      if (lapNow >= 1 && c.lapStart > 0) {
        c.lastLap = raceTime - c.lapStart;
        if (c.bestLap <= 0 || c.lastLap < c.bestLap) c.bestLap = c.lastLap;
      }
      c.lap = lapNow;
      c.lapStart = raceTime > 0 ? raceTime : 0.001f;
    }
    if (c.dist > 0) {
      int cp = clampi((int)(c.s / (trk.L / NCP)), 0, NCP - 1);
      if (cp != c.lastCp) { c.lastCp = cp; c.cpT[cp] = raceTime; c.cpLap[cp] = (int16_t)lapNow; }
    }
  }
}

// Sort by position: finishers in finishing order, then by distance covered.
static void updateOrder() {
  for (int i = 0; i < numCars; ++i) order[i] = i;
  std::sort(order, order + numCars, [](int a, int b) {
    const Car& A = cars[a]; const Car& B = cars[b];
    if (A.finished != B.finished) return A.finished;
    if (A.finished) return A.finishPos < B.finishPos;
    return A.dist > B.dist;
  });
  for (int k = 0; k < numCars; ++k) cars[order[k]].pos = k + 1;
}

// Gap to the leader: seconds (>= 0), or −laps when a lap or more down.
static float gapToLeader(int ci) {
  const Car& c = cars[ci]; const Car& L = cars[order[0]];
  if (&c == &L || c.lastCp < 0) return 0;
  int cp = c.lastCp;
  int lapDiff = L.cpLap[cp] - c.cpLap[cp];
  if (L.dist - c.dist >= trk.L) return -(float)(int)((L.dist - c.dist) / trk.L);
  if (lapDiff > 0) {                     // nearly a lap down: estimate from the leader's average speed
    float avg = raceTime > 1 ? L.dist / raceTime : 100.0f;
    return (L.dist - c.dist) / fmx(avg, 10.0f);
  }
  return fmx(0.0f, c.cpT[cp] - L.cpT[cp]);
}

static int nearestCar(float wx, float wy, float maxD) {
  int best = -1; float bd = maxD * maxD;
  for (int i = 0; i < numCars; ++i) {
    float dx = cars[i].x - wx, dy = cars[i].y - wy, d = dx * dx + dy * dy;
    if (d < bd) { bd = d; best = i; }
  }
  return best;
}

// ─── Drawing ──────────────────────────────────────────────────────────────────
struct CarDraw { float sx, sy, ca, sa, hl, hwd; int y0, y1; bool vis; };
static CarDraw cdraw[MAX_CARS];

static void prepareCarsFrame() {
  const float z = cam.zoom;
  for (int i = 0; i < numCars; ++i) {
    const Car& c = cars[i];
    CarDraw& d = cdraw[i];
    const ClassSpec& cs = CLS[c.cls];
    float h = c.heading + c.slip;
    d.sx = w2sx(c.x); d.sy = w2sy(c.y);
    d.ca = fastCos(h); d.sa = fastSin(h);
    d.hl = cs.len * 0.5f * z; d.hwd = cs.wid * 0.5f * z;
    float R = d.hl + 3;
    float beam = nightAmt > 0.3f ? cs.len * 2.6f * z : 0;
    d.y0 = (int)(d.sy - R - beam) - 2; d.y1 = (int)(d.sy + R + beam + 3 * z) + 2;
    d.vis = d.sx + R + beam >= 0 && d.sx - R - beam < SCREEN_W && d.y1 >= 0 && d.y0 < SCREEN_H;
  }
}

// Local car coordinates: x forward, y to the right, both normalised to ±1.
static inline void carPt(const CarDraw& d, float lx, float ly, int& X, int& Y) {
  float x = lx * d.hl, y = ly * d.hwd;
  X = iround(d.sx + x * d.ca - y * d.sa);
  Y = iround(d.sy + x * d.sa + y * d.ca);
}
static void IRAM_ATTR cQuad(const CarDraw& d, float x0, float x1, float y0, float y1, uint32_t col) {
  int ax, ay, bx, by, cx, cy, ex, ey;
  carPt(d, x0, y0, ax, ay); carPt(d, x1, y0, bx, by); carPt(d, x1, y1, cx, cy); carPt(d, x0, y1, ex, ey);
  fTri(ax, ay, bx, by, cx, cy, col);
  fTri(ax, ay, cx, cy, ex, ey, col);
}
static void IRAM_ATTR cTri(const CarDraw& d, float x0, float y0, float x1, float y1, float x2, float y2, uint32_t col) {
  int ax, ay, bx, by, cx, cy;
  carPt(d, x0, y0, ax, ay); carPt(d, x1, y1, bx, by); carPt(d, x2, y2, cx, cy);
  fTri(ax, ay, bx, by, cx, cy, col);
}
static void IRAM_ATTR cWheels(const CarDraw& d, float fx0, float fx1, float rx0, float rx1, float yi, uint32_t col) {
  cQuad(d, fx0, fx1, yi, 1.0f, col);  cQuad(d, fx0, fx1, -1.0f, -yi, col);
  cQuad(d, rx0, rx1, yi, 1.0f, col);  cQuad(d, rx0, rx1, -1.0f, -yi, col);
}

static void IRAM_ATTR drawCarShadows() {
  uint32_t a = (uint32_t)(4 + 8 * sunAmt);
  float off = 1.5f * cam.zoom;
  for (int i = 0; i < numCars; ++i) {
    CarDraw d = cdraw[i];
    if (!d.vis || !stripHit(d.y0, d.y1)) continue;
    d.sx += off; d.sy += off * 1.3f;
    int ax, ay, bx, by, cx, cy, ex, ey;
    carPt(d, -1, -0.95f, ax, ay); carPt(d, 1, -0.95f, bx, by); carPt(d, 1, 0.95f, cx, cy); carPt(d, -1, 0.95f, ex, ey);
    blendTri(ax, ay, bx, by, cx, cy, 0x000000u, a);
    blendTri(ax, ay, cx, cy, ex, ey, 0x000000u, a);
  }
}

static void IRAM_ATTR drawHeadlights() {
  if (nightAmt < 0.3f) return;
  uint32_t a = (uint32_t)(2 + 6 * (nightAmt - 0.3f) / 0.7f);
  for (int i = 0; i < numCars; ++i) {
    const CarDraw& d = cdraw[i];
    if (!d.vis || !stripHit(d.y0, d.y1) || cars[i].state == CS_SPIN) continue;
    int ax, ay, bx, by, cx, cy;
    carPt(d, 0.95f, 0, ax, ay); carPt(d, 5.2f, -2.4f, bx, by); carPt(d, 5.2f, 2.4f, cx, cy);
    blendTri(ax, ay, bx, by, cx, cy, 0xFFF0C8u, a);
    carPt(d, 0.95f, 0, ax, ay); carPt(d, 3.2f, -1.2f, bx, by); carPt(d, 3.2f, 1.2f, cx, cy);
    blendTri(ax, ay, bx, by, cx, cy, 0xFFF0C8u, a);
  }
}

static void IRAM_ATTR drawCarBody(int i) {
  const Car& c = cars[i];
  const CarDraw& d = cdraw[i];
  const uint32_t c1 = litT(c.c1), c2 = litT(c.c2), c3 = litT(c.c3);
  const uint32_t tyre = litT(0x1A1A1Cu), glass = litT(0x1C2A38u), dark = litT(0x101012u);
  const float lenPx = d.hl * 2;

  if (lenPx < 10.0f) {                     // far away: a coloured dash with an accent
    cQuad(d, -1, 1, -0.85f, 0.85f, c1);
    if (lenPx >= 6.0f) cQuad(d, -0.15f, 0.25f, -0.4f, 0.4f, c.cls == CC_FORMULA || c.cls == CC_CLASSIC ? dark : c2);
    return;
  }
  switch (c.cls) {
    case CC_FORMULA:
      cWheels(d, 0.48f, 0.76f, -0.86f, -0.52f, 0.70f, tyre);
      cQuad(d, -1.0f, -0.84f, -0.9f, 0.9f, c2);                 // rear wing
      cQuad(d, -0.45f, 0.22f, -0.68f, 0.68f, c1);               // sidepods
      cQuad(d, -0.80f, 0.30f, -0.42f, 0.42f, c1);               // engine cover / tub
      cQuad(d, 0.30f, 0.88f, -0.17f, 0.17f, c1);                // nose
      cQuad(d, 0.86f, 1.0f, -0.95f, 0.95f, c2);                 // front wing
      if (lenPx > 22) cQuad(d, -0.8f, 0.86f, -0.07f, 0.07f, c2); // stripe
      cQuad(d, 0.0f, 0.28f, -0.25f, 0.25f, dark);               // cockpit
      { int hx, hy; carPt(d, 0.10f, 0, hx, hy); fCircle(hx, hy, (int)fmx(1, d.hwd * 0.22f), c3); }
      break;
    case CC_PROTO:
      cQuad(d, -1.0f, -0.2f, -1.0f, 1.0f, c1);
      cQuad(d, -0.2f, 0.55f, -0.92f, 0.92f, c1);
      cTri(d, 0.55f, -0.92f, 1.0f, -0.55f, 1.0f, 0.55f, c1);
      cTri(d, 0.55f, -0.92f, 1.0f, 0.55f, 0.55f, 0.92f, c1);
      cQuad(d, -0.9f, 0.85f, 0.55f, 0.72f, c2); cQuad(d, -0.9f, 0.85f, -0.72f, -0.55f, c2);
      cQuad(d, -0.15f, 0.45f, -0.42f, 0.42f, glass);            // canopy
      cQuad(d, -0.95f, -0.15f, -0.06f, 0.06f, c2);              // fin
      cQuad(d, -1.0f, -0.86f, -1.0f, 1.0f, c2);                 // rear wing
      cQuad(d, 0.86f, 0.98f, 0.45f, 0.78f, litT(0xF0F0D8u)); cQuad(d, 0.86f, 0.98f, -0.78f, -0.45f, litT(0xF0F0D8u));
      break;
    case CC_GT:
      cQuad(d, -1.0f, 0.78f, -1.0f, 1.0f, c1);
      cTri(d, 0.78f, -1.0f, 1.0f, -0.7f, 1.0f, 0.7f, c1);
      cTri(d, 0.78f, -1.0f, 1.0f, 0.7f, 0.78f, 1.0f, c1);
      cQuad(d, -1.0f, 0.98f, -0.22f, 0.22f, c2);                // racing stripes
      cQuad(d, 0.08f, 0.32f, -0.78f, 0.78f, glass);             // windscreen
      cQuad(d, -0.42f, 0.08f, -0.74f, 0.74f, c1);               // roof
      cQuad(d, -0.42f, 0.08f, -0.18f, 0.18f, c2);
      cQuad(d, -0.62f, -0.42f, -0.7f, 0.7f, glass);             // rear window
      cQuad(d, -0.99f, -0.86f, -0.95f, 0.95f, dark);            // rear wing
      cQuad(d, 0.92f, 1.0f, 0.5f, 0.8f, litT(0xF0F0D8u)); cQuad(d, 0.92f, 1.0f, -0.8f, -0.5f, litT(0xF0F0D8u));
      break;
    case CC_TOURING:
      cQuad(d, -1.0f, 1.0f, -1.0f, 1.0f, c1);
      cQuad(d, 0.18f, 0.38f, -0.80f, 0.80f, glass);
      cQuad(d, -0.38f, 0.18f, -0.82f, 0.82f, c2);               // two-tone roof
      cQuad(d, -0.58f, -0.38f, -0.78f, 0.78f, glass);
      { int rx, ry; carPt(d, -0.1f, 0, rx, ry); fCircle(rx, ry, (int)fmx(1, d.hwd * 0.35f), litT(0xF4F4F4u)); }
      cQuad(d, 0.6f, 0.98f, 0.85f, 1.0f, c3); cQuad(d, 0.6f, 0.98f, -1.0f, -0.85f, c3);
      cQuad(d, 0.94f, 1.0f, 0.5f, 0.85f, litT(0xF0F0D8u)); cQuad(d, 0.94f, 1.0f, -0.85f, -0.5f, litT(0xF0F0D8u));
      break;
    default:   // CC_CLASSIC — cigar-shaped single-seater
      cWheels(d, 0.35f, 0.65f, -0.68f, -0.32f, 0.62f, tyre);
      cQuad(d, -0.7f, 0.4f, -0.46f, 0.46f, c1);
      cTri(d, 0.4f, -0.46f, 1.0f, 0.0f, 0.4f, 0.46f, c1);
      cTri(d, -0.7f, -0.46f, -1.0f, -0.2f, -1.0f, 0.2f, c1);
      cTri(d, -0.7f, -0.46f, -1.0f, 0.2f, -0.7f, 0.46f, c1);
      cQuad(d, -0.35f, -0.05f, -0.26f, 0.26f, dark);
      { int hx, hy; carPt(d, -0.22f, 0, hx, hy); fCircle(hx, hy, (int)fmx(1, d.hwd * 0.24f), c3); }
      { int rx, ry; carPt(d, 0.52f, 0, rx, ry); fCircle(rx, ry, (int)fmx(1, d.hwd * 0.22f), litT(0xF4F4F4u)); }
      if (lenPx > 22) cQuad(d, 0.0f, 0.95f, -0.08f, 0.08f, c2);
      break;
  }
  // Tail lights: brighter when braking, always on at night
  if (lenPx >= 16.0f && (c.braking || nightAmt > 0.3f)) {
    uint32_t tl = c.braking ? 0xFF3A20u : 0xA01810u;
    int ax, ay, bx, by;
    carPt(d, -0.97f, -0.55f, ax, ay); carPt(d, -0.97f, 0.55f, bx, by);
    fCircle(ax, ay, (int)fmx(0, cam.zoom * 0.5f), tl); fCircle(bx, by, (int)fmx(0, cam.zoom * 0.5f), tl);
  }
}

static void IRAM_ATTR drawCars(int highlight) {
  for (int i = 0; i < numCars; ++i) {
    const CarDraw& d = cdraw[i];
    if (!d.vis || !stripHit(d.y0, d.y1)) continue;
    drawCarBody(i);
    if (i == highlight) blendRing((int)d.sx, (int)d.sy, d.hl + 5, 2, 0xFFFFFFu, 18);
  }
}
