#pragma once
// Race cars: classes, liveries, driving physics/AI and drawing.
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
// Every driver races the same way; they differ in skill (mistakes, how
// consistently they commit to corners), a little raw pace, their form on the
// day and their own lines (apex timing, kerb use, how wide they turn in).

#include "effects.h"

// The new AI: every driver plans their own line through each corner (see
// "Drivers' own lines" below), with tyre limits, dirty air, covering and moves
// round the outside. Off = the classic AI, everyone on the one racing line.
#define AI_LINES 1
static bool aiLines = true;

enum CarClass : uint8_t { CC_FORMULA, CC_PROTO, CC_GT, CC_TOURING, CC_CLASSIC, CC_VAN, CC_COUNT };
struct ClassSpec { const char* name; float len, wid, top, grip, acc; };
static const ClassSpec CLS[CC_COUNT] = {
  { "Formula",   14.0f, 5.8f, 1.00f, 1.00f, 1.00f },
  { "Prototype", 14.5f, 6.4f, 0.97f, 0.93f, 0.92f },
  { "GT",        13.5f, 6.2f, 0.88f, 0.82f, 0.80f },
  { "Touring",   12.5f, 5.8f, 0.82f, 0.78f, 0.76f },
  { "Classic",   12.5f, 4.8f, 0.80f, 0.70f, 0.74f },
  { "Van",       14.5f, 6.8f, 0.74f, 0.68f, 0.62f },   // tall and heavy: slow out of corners, leans on the tyres
};

enum Series : uint8_t { SER_FORMULA, SER_GT, SER_TOURING, SER_ENDURANCE, SER_CLASSIC, SER_VANS, SER_COUNT };
static const char* const SERIES_NAMES[SER_COUNT] = { "Formula", "GT", "Touring", "Endurance", "Classic", "Vans" };

// ─── Racecraft ────────────────────────────────────────────────────────────────
// The same for every driver. (There used to be driver traits; they decided too
// many races, so every driver now races as the old "Balanced" one did.)
struct DriverSpec {
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
};
//                         corner straight brake wet   sigma  aware margin gap  room  follow mist spin
static const DriverSpec DRV = { 1.000f, 1.000f, 1.00f, 0.92f, 0.025f, 0.75f,  3.0f, 3.0f,  0.0f, 0.45f, 1.0f, 1.0f };

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
  float form;               // slowly drifting pace offset (×), so gaps open and close
  float pressT;             // > 0 while someone is close behind (more mistakes)
  float tow;                // 0..1: how much tow this car is getting
  float concedeT;           // > 0: giving the corner to a car alongside on the inside
  float react;              // start reaction delay (s)
  float passT; int8_t passSide; int8_t passCar; int8_t outMove;   // outMove: a deliberate move round the outside of a corner
  float passCd;             // > 0: just backed out of a move; wait before trying again
  float passAge;            // how long the current move has been going
  float vTop;               // its straight-line top speed (form, class), without any tow
  // Driving style for their own lines (each about -1..1): apex timing (+ = late),
  // kerb use at the apex, how wide they turn in and track out, and lap-to-lap variation.
  float stApex, stKerb, stIn, stOut, stVar;
  float decelAvg;           // braking actually being done, averaged over a few frames (px/s²)
  float coverT;             // > 0: covering the inside into the next corner
  int8_t coverSide;
  int8_t cutback;           // corner to take with a late apex to cut back after being passed (-1 none)
  float overT;              // > 0: just overdid it (understeer or oversteer), for the viewer/stats
  float exitPush;           // how hard they'll lean on the throttle out of the coming corner (0 = within traction)
  float blockCd, yieldT; int8_t yieldSide;
  float lockT, contactCd, recoverT;
  float stopT;
  float lapStart, lastLap, bestLap;
  int   lap;                // completed laps (−1 before first crossing)
  int   lapsAtFlag;
  bool  finished; int finishPos, finishLaps;   // finishPos: order of crossing the line after the flag
  bool  braking;
  CarState state;
  uint8_t cls;
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
  float planS, planU, planV2;     // last full corner plan: where it was made, and the corner point that set the speed (u ahead, its speed²)
  uint8_t planMode;               // what kind of plan that was (own line, beside another car, ...)
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

// Custom drivers (Settings → Drivers). When on, car i gets slot i's name
// instead of a random one. Saved in NVS by settings.h.
static bool    customDrivers = false;
static char    customCode[MAX_CARS][4];

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

// ─── Drivers' own lines ───────────────────────────────────────────────────────
// Each driver's line is the racing line plus their own offset through every
// corner: five control points (turn-in, before the apex, apex, after the apex,
// track-out) with smooth cosine blends between them and back to the racing line
// on either side. Re-planned every lap, so lines vary corner to corner. The
// line's curvature sets that driver's own corner speed.
#define LINE_RAMP 80.0f                          // px over which an offset blends in/out
#define LINE_TIGHT_K 0.02f                      // corners tighter than this curvature get smaller personal offsets
#define LINE_KMAX 0.003f                         // most extra curvature a line change may add (radius ~330 px)
static int8_t* lineDelta = nullptr;              // [MAX_CARS][MAX_CORNERS][5], 0.1 px units, PSRAM

static inline int8_t* lineDeltaOf(int car, int corner) { return lineDelta + (car * MAX_CORNERS + corner) * 5; }

// Plan one corner for one driver.
static void planCornerLine(const Car& c, int car, int k) {
  if (!lineDelta) return;
  const Corner& C = trk.corners[k];
  const float dir = (float)C.dir;                // + = right-hand bend; inside is +dir
  const float sv = 0.35f + 0.65f * c.stVar;      // how much this lap differs from their usual
  auto n = [&]() { return (frand() + frand() + frand() - 1.5f) * 2.0f * sv; };
  float lam = c.stApex + 0.25f * n();
  bool cut = c.cutback == k;
  if (cut) lam = 1.2f;                           // cutting back: wide in, late apex, square exit
  float raw[5] = {
    -dir * ((cut ? 1.2f : c.stIn) * 1.8f + 0.8f * n()),        // turn-in: + = wider
    -dir * (lam * 2.2f),                         // before the apex: late apex stays outside longer
     dir * (c.stKerb * 1.8f + 0.6f * n()),       // apex: + = deeper onto the kerb
     dir * (lam * 2.2f),                         // after the apex: late apex is still tight here
    -dir * (c.stOut * 1.8f + 0.8f * n()),        // track-out
  };
  const float sAt[5] = { C.sIn, C.sAp - C.q, C.sAp, C.sAp + C.q, C.sOut };
  // Hairpins and chicanes leave little room for a personal line.
  const float tight = clampf(LINE_TIGHT_K / fmx(fabsf(tp[C.apex].rk), 1e-4f), 0.25f, 1.0f);
  for (float& r : raw) r *= tight;
  // apex and track-out may put two wheels on the kerb; turn-in and the bits between stay on the road
  const float dMax[5] = { trk.hw - 3.5f, trk.hw - 3.5f, trk.hw - 2.0f, trk.hw - 3.5f, trk.hw - 1.6f };
  float d[5];
  for (int j = 0; j < 5; ++j) { float rl = tp[idxAt(sAt[j])].rl; d[j] = clampf(rl + raw[j], -dMax[j], dMax[j]) - rl; }
  // A car can only change its line so fast at speed: limit every step of the
  // plan (a cosine blend over length L peaks at a curvature of Δ·π²/(2L²)).
  const float xs[7] = { 0, LINE_RAMP, sAt[1] - C.sIn + LINE_RAMP, sAt[2] - C.sIn + LINE_RAMP, sAt[3] - C.sIn + LINE_RAMP, sAt[4] - C.sIn + LINE_RAMP, sAt[4] - C.sIn + 2 * LINE_RAMP };
  float y[7] = { 0, d[0], d[1], d[2], d[3], d[4], 0 };
  for (int pass = 0; pass < 3; ++pass)
    for (int j = 0; j < 6; ++j) {
      const float L = fmx(xs[j + 1] - xs[j], 1.0f), lim = LINE_KMAX * 2.0f * L * L / (PI * PI);
      const float dy = y[j + 1] - y[j];
      if (fabsf(dy) > lim) { if (j + 1 < 6) y[j + 1] = y[j] + (dy > 0 ? lim : -lim); else y[j] = y[j + 1] - (dy > 0 ? lim : -lim); }
    }
  for (int pass = 0; pass < 3; ++pass)                 // and coming back to the racing line
    for (int j = 5; j >= 1; --j) {
      const float L = fmx(xs[j + 1] - xs[j], 1.0f), lim = LINE_KMAX * 2.0f * L * L / (PI * PI);
      const float dy = y[j] - y[j + 1];
      if (fabsf(dy) > lim) y[j] = y[j + 1] + (dy > 0 ? lim : -lim);
    }
  int8_t* o = lineDeltaOf(car, k);
  for (int j = 0; j < 5; ++j) o[j] = (int8_t)clampi((int)lroundf(y[j + 1] * 10.0f), -127, 127);
}

// This driver's offset from the racing line at s, and its second derivative.
static void IRAM_ATTR lineDelta2(int car, int nextC, float s, float& delta, float& d2) {
  delta = 0; d2 = 0;
  if (!lineDelta || trk.nCorners == 0) return;
  for (int m = -2; m <= 2; ++m) {
    int k = nextC + m; while (k < 0) k += trk.nCorners; while (k >= trk.nCorners) k -= trk.nCorners;
    const Corner& C = trk.corners[k];
    const float x = sDelta(C.sIn - LINE_RAMP, s);            // position within this corner's window
    const float xs[7] = { 0, LINE_RAMP, C.sAp - C.q - C.sIn + LINE_RAMP, C.sAp - C.sIn + LINE_RAMP,
                          C.sAp + C.q - C.sIn + LINE_RAMP, C.sOut - C.sIn + LINE_RAMP, C.sOut - C.sIn + 2 * LINE_RAMP };
    if (x <= 0 || x >= xs[6]) continue;
    const int8_t* o = lineDeltaOf(car, k);
    const float ys[7] = { 0, o[0] * 0.1f, o[1] * 0.1f, o[2] * 0.1f, o[3] * 0.1f, o[4] * 0.1f, 0 };
    int j = 0; while (j < 5 && x > xs[j + 1]) ++j;
    const float L = fmx(xs[j + 1] - xs[j], 1.0f), u = (x - xs[j]) / L, a = ys[j], b = ys[j + 1];
    const float cu = cosf(PI * u);
    delta += a + (b - a) * (1.0f - cu) * 0.5f;
    d2 += (b - a) * (PI * PI) / (2.0f * L * L) * cu;
  }
}

// Where this driver means to be across the track at s, and the curvature of that line.
static inline void lineEval(const Car& c, int car, float s, float& d, float& kPath) {
  float dl, d2; lineDelta2(car, c.nextCorner, s, dl, d2);
  const TrackPt& P = tp[idxAt(s)];
  d = P.rl + dl;
  kPath = P.rk / clampf(1.0f - P.rk * dl, 0.3f, 2.0f) + d2;
}
static inline float lineD(const Car& c, int car, float s) { float d, k; lineEval(c, car, s, d, k); return d; }

// The same line, for many points close together ahead of (or just behind) a
// car: the corner windows that reach that stretch are gathered once, so each
// point only blends those. Points are given as u, the distance from the s the
// cursor was built at.
// Each of a window's six pieces is a cosine blend from a to a + ba, worked out
// ahead (1/length, and the second-derivative factor) so a point needs no divide.
struct LineWin { float x0; float xs[7]; float a[6], ba[6], invL[6], c2[6]; };   // x0: the build s, from the window start
// Each corner's window shape, the same for every driver (worked out once per race)
struct CornerGeo { float xs[7], invL[6], k2[6]; };
static CornerGeo* cornerGeo = nullptr;           // [MAX_CORNERS], PSRAM

static void buildCornerGeo() {
  if (!cornerGeo) cornerGeo = (CornerGeo*)heap_caps_malloc(sizeof(CornerGeo) * MAX_CORNERS, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!cornerGeo) return;
  for (int k = 0; k < trk.nCorners; ++k) {
    const Corner& C = trk.corners[k];
    CornerGeo& g = cornerGeo[k];
    g.xs[0] = 0; g.xs[1] = LINE_RAMP; g.xs[2] = C.sAp - C.q - C.sIn + LINE_RAMP; g.xs[3] = C.sAp - C.sIn + LINE_RAMP;
    g.xs[4] = C.sAp + C.q - C.sIn + LINE_RAMP; g.xs[5] = C.sOut - C.sIn + LINE_RAMP; g.xs[6] = C.sOut - C.sIn + 2 * LINE_RAMP;
    for (int j = 0; j < 6; ++j) {
      const float L = fmx(g.xs[j + 1] - g.xs[j], 1.0f);
      g.invL[j] = 1.0f / L; g.k2[j] = (PI * PI) / (2.0f * L * L);
    }
  }
}
struct LineCur { int n; LineWin w[5]; };
static LineCur lcOwn, lcPass;                    // the sim is one task: no need for stack space

// cos(π·u) for u in [0, 1]: −sin(π(u − ½)), a short series (good to about 4e-6)
static inline float cosPiU(float u) {
  const float x = (u - 0.5f) * PI, x2 = x * x;
  return -x * (1.0f + x2 * (-1.0f / 6 + x2 * (1.0f / 120 + x2 * (-1.0f / 5040 + x2 * (1.0f / 362880)))));
}

// Track samples near a car, without a divide or a wrap loop each: fi0 is the
// car's fractional sample index (wrapS(s) / ds), simInvDs = 1 / ds.
static float simInvDs = 1.0f;
#ifndef PLAN_ALT
#define PLAN_ALT 1                                // full corner plans on alternate steps
#endif
static uint32_t simTick = 0;                     // sim steps, for spreading work over alternate steps
static bool towBoxed[MAX_CARS];                  // boxed in behind a car: the tow can't be used to accelerate
static inline const TrackPt& tpU(float fi0, float u) {
  float fi = fi0 + u * simInvDs;
  if (fi < 0) fi += trk.n; else if (fi >= trk.n) fi -= trk.n;
  int k = (int)fi; if (k >= trk.n) k -= trk.n; else if (k < 0) k = 0;
  return tp[k];
}

static void IRAM_ATTR lineCurBuild(int car, int nextC, float sRef, float ua, float ub, LineCur& lc) {
  lc.n = 0;
  if (!lineDelta || !cornerGeo || trk.nCorners == 0) return;
  for (int m = -2; m <= 2; ++m) {
    int k = nextC + m; while (k < 0) k += trk.nCorners; while (k >= trk.nCorners) k -= trk.nCorners;
    const Corner& C = trk.corners[k];
    const CornerGeo& g = cornerGeo[k];
    const float x0 = sDelta(C.sIn - LINE_RAMP, sRef);
    if (x0 + ua >= g.xs[6] || x0 + ub <= 0) continue;   // this corner's window misses the stretch
    LineWin& w = lc.w[lc.n++];
    w.x0 = x0;
    for (int j = 0; j < 7; ++j) w.xs[j] = g.xs[j];
    const int8_t* o = lineDeltaOf(car, k);
    float ys[7]; ys[0] = 0; for (int j = 0; j < 5; ++j) ys[j + 1] = o[j] * 0.1f; ys[6] = 0;
    for (int j = 0; j < 6; ++j) {
      w.a[j] = ys[j]; w.ba[j] = ys[j + 1] - ys[j]; w.invL[j] = g.invL[j]; w.c2[j] = w.ba[j] * g.k2[j];
    }
  }
}
// The line u ahead of the build point, given the track sample there.
static inline void IRAM_ATTR lineCurEval(const LineCur& lc, float u, const TrackPt& P, float& d, float& kPath) {
  float dl = 0, d2 = 0;
  for (int n = 0; n < lc.n; ++n) {
    const LineWin& w = lc.w[n];
    const float x = w.x0 + u;
    if (x <= 0 || x >= w.xs[6]) continue;
    int j = 0; while (j < 5 && x > w.xs[j + 1]) ++j;
    const float cu = cosPiU((x - w.xs[j]) * w.invL[j]);
    dl += w.a[j] + w.ba[j] * (1.0f - cu) * 0.5f;
    d2 += w.c2[j] * cu;
  }
  d = P.rl + dl;
  kPath = dl == 0.0f ? P.rk + d2 : P.rk / clampf(1.0f - P.rk * dl, 0.3f, 2.0f) + d2;   // (on the racing line: no divide)
}
static inline float lineCurD(const LineCur& lc, float u, const TrackPt& P) { float d, k; lineCurEval(lc, u, P, d, k); return d; }

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
      case SER_VANS:      c.cls = CC_VAN; break;
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
    if (customDrivers && customCode[i][0]) { memcpy(c.code, customCode[i], 4); c.code[3] = 0; }   // the user's own field
    // Driving style: their own quirks.
    c.stApex = clampf(frandr(-0.4f, 0.4f), -1, 1);
    c.stKerb = clampf(frandr(-0.7f, 0.4f), -1, 1);
    c.stIn   = clampf(frandr(-0.4f, 0.4f), -1, 1);
    c.stOut  = clampf(frandr(-0.4f, 0.4f), -1, 1);
    c.stVar  = clampf(0.6f + frandr(-0.2f, 0.2f), 0.2f, 1.2f);
    c.cutback = -1;
    c.perf = frandr(0.988f, 1.0f);
    c.skill = frandr(0.86f, 0.99f);
    c.react = frandr(0.05f, 0.45f);
    c.commit = 1.0f;
    quali[i] = frandr(-0.015f, 0.015f);   // no qualifying: a random grid, so pole is no head start for the quickest
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
  if (aiLines) {
    if (!lineDelta) lineDelta = (int8_t*)heap_caps_malloc(MAX_CARS * MAX_CORNERS * 5, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    buildCornerGeo();
    for (int i = 0; i < numCars; ++i) for (int k = 0; k < trk.nCorners; ++k) planCornerLine(cars[i], i, k);
  }
  _rng = save;
}

// ─── Racing: pace variation, tow, passing ─────────────────────────────────────
// What makes the racing close is below. Tuned with the PC harness over 180 races (16 cars, 10 laps): about 36
// clean on-track overtakes a race, ~13 of them for the lead, ~3.6 incidents.
#define FORM_TAU       25.0f    // s: how quickly a driver's form drifts
#define FORM_SD        0.010f   // typical form offset (×): gaps open and close
#define LEAD_EASE_T0   0.3f     // s: a leader further ahead than this...
#define LEAD_EASE      0.015f   // ...backs off by up to this much (×), managing the gap
// Pack racing (new AI): anyone can win from anywhere on the grid. Further back means more
// speed down the straights (corners stay within grip), and a move down a straight gets a push.
// Both are measured within a car's own class, so GTs in an Endurance race chase the leading GT.
#define PACK_K         0.045f   // top speed (×) for a car PACK_T1 s or more behind its class leader:
#define PACK_T0        2.0f     // s: none within this of them, building smoothly to full at PACK_T1
#define PACK_T1        8.0f
#define POS_K          0.10f    // ...plus up to this much (×) by place in the class, the most for last
#define PRESS_T        0.8f     // s: a car this close behind puts you under pressure...
#define PRESSURE_MIST  2.0f     // ...and doubles your chance of a mistake
#define COVER_T        0.35f    // s: only a car this close behind, and at least as quick, makes a driver cover the inside
#define COVER_CD       10.0f    // s: and not again for this long
#define TOW_T0         0.5f     // s behind: full tow inside this
#define TOW_T1         1.2f     // s behind: no tow beyond this
#define TOW_GAIN       0.15f    // top-speed gain at full tow (×)
#define TOW_ACC        1.2f     // extra acceleration at full tow and top speed (×)
#define PASS_PUSH      0.07f    // push to pass: top speed (×) while making a move down a straight...
#define PASS_PUSH_ACC  1.0f     // ...and extra acceleration (×)
#define TOW_HOLD       2.5f     // s: pulling out of the tow to pass, the speed it gave fades over this long
#define BOXED_LEN      2.0f     // car lengths: closer than this and not passing, the tow can't be used
#define PASS_T         0.35f    // s: a balanced driver goes for it from this far back
#define ATTACK_COMMIT  1.015f   // how much later anyone brakes into a corner when going for a pass
#define DIVE_BRAKE     1.15f    // braking a driver dares while committed to a pass (×, within the car's limit)
#define PASS_EDGE      0.0f     // px past the clean-track limit a passing lane may use
#define LANE_EASE      3.0f     // 1/s: lane changes slow down as they reach the new line
#define PASS_RETRY     1.0f     // s to wait after backing out of a move
#define REPASS_CD      4.0f     // s: just passed, a driver has to get back in the tow before attacking again
#define OUTSIDE_GAP    0.6f     // car lengths behind: close enough to try round the outside in a corner
#define OUTSIDE_RATE_K 1.0f     // scales every driver's appetite for the outside
#define OUTSIDE_GAIN   1.06f    // a committed attacker on a wider arc carries up to this much more speed
#define OUTSIDE_GAIN_M 1.10f    // ...and this much on a deliberate move round the outside (the wider arc's momentum)
#define OUTSIDE_DROP   2.6f     // car lengths behind: a move round the outside is over
#define PASS_REF_ALL   0        // 1: every move plans on the other car's line, not just ones round the outside
#define OUT_DEF_SLOW   0.96f   // the inside car's pace with someone alongside round the outside
#define OUT_DEF_OVL    1.4f     // ...counted from this much overlap (x half the two lengths, behind)
#ifndef PATH_GREED
#define PATH_GREED     1.06f    // an attacker carries this much over its path's comfortable corner speed
#endif
#define SETUP_T        0.8f    // s behind someone at the corner decision: close enough for a setup line
#define DIRTY_T        0.5f     // s behind another car in a corner where dirty air starts to bite
#define DIRTY_GRIP     0.08f    // grip lost right on their tail in a corner
#define GREED          0.08f    // how far past the tyre's limit a driver pushes the pedals
#define OUTSIDE_RATE   0.55f    // per second, right behind someone in a corner: how readily a driver tries round the outside
#define SETUP_RATE     0.35f    // per corner, following closely: how often a driver takes a wide entry / late apex to set up the exit
#define COVER_RATE     0.45f    // per second, with someone on their tail: how readily a driver covers the inside
#define CUTBACK_P      0.6f     // chance a driver who gives the corner tries a switchback
#define CORNER_SLOW    0.65f    // a corner (for racecraft) is where the car must slow below this share of top speed
#define FLAT_OUT       0.97f    // ...and flat out (for the tow) is anywhere it can carry this share
#define FOLLOW_BRAKE   1.0f     // share of their braking a following driver plans to use for a slower car
#define OBSTACLE_V     20.0f    // px/s: a car slower than this is an obstacle, not someone to race
#define PASS_INSIDE_U  150.0f   // px: a corner closer than this means a move must go up its inside
#define COAST_DECEL    20.0f    // px/s²: slowing on drag alone once out of the tow
#define PASS_LANE      1.8f     // px of daylight the attacker aims to leave beside the other car
#define PASS_OVERLAP   0.9f     // × car width: closer than this sideways, an attacker would hit them
#define PASS_PULL      4.0f     // px/s: minimum rate of pulling out to pass (lane changes ease in otherwise)
#define PASS_MAX_T     6.0f     // s: longest a move is kept going while still gaining
#define RACE_ROOM      1.6f     // px of gap left to a car alongside...
#define OVERLAP_NEED   0.5f     // car lengths of overlap needed on the inside before the other car gives way
#define CONCEDE        0.96f    // a car giving the corner lifts this much (×)

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
  const DriverSpec& T = DRV;
  float g = (frand() + frand() + frand() - 1.5f) * 2.0f;            // ~N(0,1)
  c.commit = 1.0f + g * T.sigma * (1.6f - c.skill);
  c.exitPush = GREED * frand() * 2.0f * (1.4f - c.skill);
  float p = (1.0f - c.skill) * 0.09f * T.mistake * (1.0f + 2.5f * wetAmt + 3.0f * snowCover) * (c.pressT > 0 ? PRESSURE_MIST : 1.0f);
  if (frand() < p) { c.commit += frandr(0.08f, 0.24f); c.lockT = 0.35f; }  // a lapse: brakes far too late
}

static inline bool lapping0(const Car& c, const Car& a) { return c.dist - a.dist > trk.L * 0.5f; }

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
  simInvDs = 1.0f / trk.ds;
  ++simTick;
  const float formK = FORM_SD * sqrtf(2.0f * dt / FORM_TAU);   // the same for every car
  evN = 0;
  // For the catch-up: each class's leader, and each car's place within its class
  int clsLead[CC_COUNT], clsN[CC_COUNT], clsPos[MAX_CARS];
  for (int k = 0; k < CC_COUNT; ++k) { clsLead[k] = -1; clsN[k] = 0; }
  for (int k = 0; k < numCars; ++k) { const int j = order[k], cl = cars[j].cls; if (clsLead[cl] < 0) clsLead[cl] = j; clsPos[j] = ++clsN[cl]; }
  for (int i = 0; i < numCars; ++i) {
    Car& c = cars[i];
    const ClassSpec& cs = CLS[c.cls];
    const DriverSpec& T = DRV;
    TrackFrame f; trackAt(c.s, f);
    const float fi0 = wrapS(c.s) * simInvDs;      // this car's place in the track samples
    const float trackAng = atan2f(f.ty, f.tx);
    const float hwC = hw - cs.wid * 0.5f - 0.8f;
    const float gCar = cs.grip * c.perf * c.perf;
    const float surfG = c.surface == 0 ? 1.0f : (c.surface == 1 ? 0.45f : 0.55f);
    float aLatMax = A_LAT * gCar * wx * 1.12f * surfG;                 // what the tyres can really do
    if (aiLines && c.state == CS_RACE && fabsf(tpU(fi0, 0.0f).rk) > 1.0f / 300.0f) {
      // Dirty air: right behind another car in a corner, the front tyres lose some bite.
      float closest = 1e9f;
      for (int j = 0; j < numCars; ++j) {
        if (j == i) continue;
        const Car& o = cars[j];
        float g = sDelta(c.s, o.s);
        if (g > 4.0f && g < closest && fabsf(o.d - c.d) < cs.wid * 1.2f && o.state == CS_RACE) closest = g;
      }
      const float tGap = closest / fmx(c.v, 30.0f);
      if (tGap < DIRTY_T) aLatMax *= 1.0f - DIRTY_GRIP * (1.0f - tGap / DIRTY_T);
    }
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
      const float wxBelief = wx == 1.0f ? 1.0f : powf(wx, T.wetAdapt);   // (dry: skip the slow powf)
      const float gT = gCar * wxBelief * T.corner * c.commit;
      const float abPlain = A_BRK * T.brake * sqrtf(gCar) * wxBelief;                          // comfortable braking
      const float ab = abPlain * (c.passT > 0 ? DIVE_BRAKE : 1.0f) * (aiLines ? 1.0f + 0.3f * GREED : 1.0f);
      if (racing) {                                // drift the driver's form
        float g = (frand() + frand() + frand() - 1.5f) * 2.0f;
        c.form += -c.form * (dt / FORM_TAU) + formK * g;
        c.form = clampf(c.form, -2.5f * FORM_SD, 2.5f * FORM_SD);
      }
      float ease = 0;                              // a leader with a cushion manages the gap
      if (c.pos == 1 && numCars > 1 && racing && !c.finished) {
        const Car& b = cars[order[1]];
        float gapT = (c.dist - b.dist) / fmx(c.v, 60.0f);
        ease = LEAD_EASE * clampf((gapT - LEAD_EASE_T0) / 1.0f, 0, 1);
      }
      float pack = 0;
      if (aiLines && racing && !c.finished && raceTime > 5.0f && clsLead[c.cls] >= 0 && clsLead[c.cls] != i) {
        const Car& L = cars[clsLead[c.cls]];
        const float avgV = fmx(L.dist / raceTime, 50.0f);           // the class leader's average speed so far
        const float x = clampf(((L.dist - c.dist) / avgV - PACK_T0) / fmx(PACK_T1 - PACK_T0, 0.1f), 0, 1);
        const float sm = x * x * (3.0f - 2.0f * x);                  // eases in and out: no step to surge across
        pack = PACK_K * sm + POS_K * (clsPos[i] - 1) / fmx(clsN[c.cls] - 1, 1);   // the further back, the more they find
      }
      const float fm = 1.0f + c.form - ease;
      float vT = V_TOP * cs.top * c.perf * T.straight * fm * (1.0f + pack);   // catching up: more speed down the straights
      const float vTopCap = vT;                    // straight-line target, before corners, tow and traffic
      c.vTop = vTopCap;
      float limMin = 1e18f;                        // (speed²) the corners ahead allow from here
      const float sg = sqrtf(gT);
      // Off the racing line (passing, defending, side by side) the car is on a
      // parallel line, tighter on the inside of a corner, so it can carry less
      // speed there: curvature of a line offset by o from one of curvature k is
      // k / (1 - k·o). Drivers take that into account (aggressive ones only partly).
      float lineHere = f.rl, kLineHere = f.rk;
      const bool usePass = aiLines && c.passT > 0 && c.passCar >= 0;
      if (aiLines) {
        lineCurBuild(i, c.nextCorner, c.s, -8.0f, 180.0f, lcOwn);          // everything below uses s - 8 ... s + 180
        if (usePass) lineCurBuild(c.passCar, cars[c.passCar].nextCorner, c.s, -20.0f, 200.0f, lcPass);
        lineCurEval(lcOwn, 0.0f, tpU(fi0, 0.0f), lineHere, kLineHere);
        float d1, k1, d2_, k2; lineCurEval(lcOwn, -8.0f, tpU(fi0, -8.0f), d1, k1); lineCurEval(lcOwn, 8.0f, tpU(fi0, 8.0f), d2_, k2);
        kLineHere = (k1 + 2.0f * kLineHere + k2) * 0.25f;     // smooth out spikes before steering on it
      }
      const float off0 = c.dPath - lineHere;
      const bool  offLine = fabsf(off0) > 1.5f;
      const float aware = fmn(1.0f, T.lineAware + 0.3f);
      const int outRef = (usePass && (c.outMove || PASS_REF_ALL)) ? c.passCar : -1;
      const float offU = outRef >= 0 ? c.dPath - lineCurD(lcPass, 0.0f, tpU(fi0, 0.0f)) : off0;
      // Passing or covering, the car holds a spot beside the other car (or by the
      // kerb) instead of following its own line, and on a line that swings across
      // the road that is a different shape altogether: plan on that exact path.
      const bool pathPlan = aiLines && ((c.passT > 0 && c.passCar >= 0 && cars[c.passCar].state == CS_RACE) || c.coverT > 0);
      // The full corner plan (every point to 180 px ahead) is made every other
      // step, alternating between cars. In between, only the point under the car
      // is looked at again, along with the one that limited the speed last time
      // (it's just a little closer now).
      const float planDs = sDelta(c.planS, c.s);
      const uint8_t planMode = (uint8_t)((pathPlan ? 1 : 0) | (outRef >= 0 ? 2 : 0) | (c.passT > 0 ? 4 : 0) | (c.coverT > 0 ? 8 : 0));
      const bool fullPlan = !aiLines || !PLAN_ALT || ((simTick + i) & 1) == 0 || c.planU <= 0.0f || planDs < -1.0f || planDs > 15.0f || planMode != c.planMode;
      const int nq = fullPlan ? 23 : 7;                // (7 is all the u = 0 point needs)
      float kPl[21];
      if (pathPlan) {
        float dPl[23];                                 // lateral position at u = -20, -10, ..., 200
        for (int q = 0; q < nq; ++q) {
          const float uq = (q - 2) * 10.0f;
          if (c.coverT > 0) dPl[q] = c.coverSide * (hwC - 1.5f);
          else {
            const Car& a = cars[c.passCar];
            const float lane = (cs.wid + CLS[a.cls].wid) * 0.5f + PASS_LANE, edge = hwC + PASS_EDGE;
            dPl[q] = clampf(lineCurD(lcPass, uq, tpU(fi0, uq)) + c.passSide * lane, -edge, edge);
          }
        }
        for (int pass = 0; pass < 2; ++pass) {         // the lane eases, so smooth the corners of the clamp
          float t[23]; for (int q = 0; q < nq; ++q) t[q] = dPl[q];
          for (int q = 1; q < nq - 1; ++q) dPl[q] = (t[q - 1] + 2.0f * t[q] + t[q + 1]) * 0.25f;
        }
        for (int q = 0; q < nq - 2; ++q) {                 // curvature of that path at u = (q - 1)·10
          const float kc = tpU(fi0, (q - 1) * 10.0f).k, d = dPl[q + 1];
          kPl[q] = kc / fmx(0.3f, 1.0f - kc * d) + (dPl[q] - 2.0f * d + dPl[q + 2]) * 0.01f;
        }
      }
      const bool offLineU = !pathPlan && (outRef >= 0 ? fabsf(offU) > 1.5f : offLine);
      float bestU = 0, bestLim = 1e18f;
      for (float u = 0; u <= (fullPlan ? 180.0f : 0.0f); u += 10.0f) {
        const TrackPt& P = tpU(fi0, u);
        float vc = P.vc * sg * fm;
        float kRef = P.rk;
        if (aiLines && pathPlan) {                   // (and offLineU is false: kRef isn't needed)
          const int q = (int)(u * 0.1f) + 1;
          const float kq = fmx(fabsf(kPl[q - 1]), fmx(fabsf(kPl[q]), fabsf(kPl[q + 1])));
          const float g = c.passT > 0 ? PATH_GREED : 1.0f, vHi = P.vc * OUTSIDE_GAIN_M, akq = fmx(kq, 1e-6f);   // attacking: a bit past the limit
          vc = (A_LAT * g * g < akq * vHi * vHi ? sqrtf(A_LAT / akq) * g : vHi) * sg * fm;
        } else if (aiLines) {
          // this driver's own line: tighter than the racing line is slower, wider can be a bit quicker
          float dl, kl; lineCurEval(outRef >= 0 ? lcPass : lcOwn, u, P, dl, kl);
          // the corner speed of their own line (at most a little above the racing line's,
          // which already plans for the tightest bit nearby). Only worked out when it
          // could be the lower one (a square root is slow on the ESP32).
          const float akl = fmx(fabsf(kl), 1e-6f), vHi = fmn(V_TOP * 1.1f, P.vc * 1.04f);
          const float vMine = A_LAT < akl * vHi * vHi ? sqrtf(A_LAT / akl) : vHi;
          // in hairpins and chicanes a personal line's curvature is too spiky to plan
          // braking on, so the plan leans on the racing line's corner speed there
          const float ark = fabsf(P.rk);
          const float wOwn = ark <= LINE_TIGHT_K ? 1.0f : LINE_TIGHT_K / ark;
          vc = lerpf(P.vc, vMine, wOwn) * sg * fm;
          kRef = kl;
        }
        if (offLineU && fabsf(kRef) > 1e-5f) {
          float kp = kRef / fmx(0.3f, 1.0f - kRef * offU);
          float ratio = fmn(c.passT > 0 && aiLines ? (c.outMove ? OUTSIDE_GAIN_M : OUTSIDE_GAIN) : 1.0f, sqrtf(fabsf(kRef) / fmx(fabsf(kp), 1e-6f)));
          vc *= 1.0f - aware * (1.0f - ratio);
        }
        float lim = vc * vc + 2.0f * ab * u;
        if (lim < vT * vT) vT = sqrtf(lim);
        if (lim < limMin) limMin = lim;
        if (u > 0.0f && lim < bestLim) { bestLim = lim; bestU = u; }
      }
      if (aiLines) {
        if (fullPlan) { c.planS = c.s; c.planU = bestU; c.planV2 = bestLim - 2.0f * ab * bestU; c.planMode = planMode; }
        else {
          const float uB = c.planU - planDs;
          if (uB > 0.0f) {
            const float lim = c.planV2 + 2.0f * ab * uB;
            if (lim < vT * vT) vT = sqrtf(lim);
            if (lim < limMin) limMin = lim;
          }
        }
      }
      if (c.concedeT > 0) { c.concedeT -= dt; vT *= CONCEDE; }

      // Where the track really slows this car, from the speed profile rather
      // than the raw centreline curvature (noisy on the real circuits): a kink
      // taken flat out is still part of the straight.
      auto slowAt = [&](float sq) { return tp[idxAt(sq)].vc * sg < vTopCap * CORNER_SLOW; };
      auto slowU  = [&](float u) { return tpU(fi0, u).vc * sg < vTopCap * CORNER_SLOW; };   // slowAt(c.s + u)
      // Distance to the next corner within [u0, u1], and which way it turns
      // (racing-line curvature at its slowest point). 1e9 if there isn't one.
      auto nextCorner = [&](float u0, float u1, float& kOut) -> float {
        for (float u = u0; u <= u1; u += 10.0f) {
          if (!slowU(u)) continue;
          float best = 1e9f; kOut = 0;
          for (float w = u; w <= u + 100.0f; w += 10.0f) {
            const TrackPt& P = tpU(fi0, w);
            if (P.vc < best) { best = P.vc; kOut = P.rk; }
          }
          return u;
        }
        kOut = 0; return 1e9f;
      };
      if (coolDown || c.finished) vT *= 0.6f;
      if (c.recoverT > 0) { c.recoverT -= dt; vT = fmn(vT, 45.0f); }
      // Tow: on a straight, the air behind another car is thinner. It helps from
      // about a second back and most when right under the rear wing, but only in
      // line behind them: a car alongside gets none (or two cars side by side would
      // leapfrog each other all the way down a straight).
      const float towPrev = c.tow;
      towBoxed[i] = false;
      c.tow = 0;
      if (racing && tpU(fi0, 0.0f).vc * sg >= vTopCap * FLAT_OUT) {
        const float towRange = fmx(c.v, 60.0f) * TOW_T1;
        float towG = towRange;
        for (int j = 0; j < numCars; ++j) {
          if (j == i) continue;
          const Car& o = cars[j];
          float g = sDelta(c.s, o.s);
          if (g > cs.len && g < towG && fabsf(o.d - c.d) < (cs.wid + CLS[o.cls].wid) * 0.5f + 2.0f && o.state == CS_RACE) towG = g;
        }
        if (towG < towRange) {
          float tg = towG / fmx(c.v, 60.0f);          // time gap, s
          c.tow = clampf((TOW_T1 - tg) / (TOW_T1 - TOW_T0), 0, 1);
        }
        if (c.passT > 0) c.tow = fmx(c.tow, towPrev - dt / TOW_HOLD);   // pulling out to pass carries the speed for a while
        if (c.tow > 0) vT = fmn(vT * (1.0f + TOW_GAIN * c.tow), sqrtf(limMin));   // never past what the corners ahead allow
        if (c.passT > 0 && c.passCar >= 0) vT = fmn(vT * (1.0f + PASS_PUSH), sqrtf(limMin));   // push to pass
      }
      if (c.pressT > 0) c.pressT -= dt;
      const float vFree = vT;                      // before traffic

      // ── Traffic ──
      int ahead = -1; float aheadGap = 1e9f;
      int attacker = -1; float attackGap = 1e9f;
      int behind = -1; float behindGap = 1e9f;
      for (int j = 0; j < numCars; ++j) {
        if (j == i) continue;
        const Car& o = cars[j];
        float g = sDelta(c.s, o.s);
        float widAvg = (cs.wid + CLS[o.cls].wid) * 0.5f;
        // Committed to a move and pulling out: only the car it would actually hit
        // counts as ahead, so it isn't held to their speed while drawing alongside.
        float latBand = (c.passT > 0 && o.state == CS_RACE) ? widAvg * PASS_OVERLAP : widAvg + 1.5f;
        if (g > 0 && g < 70 && fabsf(o.d - c.d) < latBand && g < aheadGap) { ahead = j; aheadGap = g; }
        if (g < 0 && -g < 3.0f * cs.len && o.state == CS_RACE && o.passT > 0 && o.passCar == i && -g < attackGap) { attacker = j; attackGap = -g; }
        if (g < 0 && -g < fmx(2.5f * cs.len, o.v * PRESS_T) && o.state == CS_RACE && racing) c.pressT = 1.0f;
        if (g < 0 && -g < behindGap && o.state == CS_RACE && fabsf(o.d - c.d) < widAvg + 6.0f) { behind = j; behindGap = -g; }
      }
      float dT = aiLines ? lineCurD(lcOwn, 14.0f, tpU(fi0, 14.0f)) :f.rl + (tp[idxAt(c.s + 14.0f)].rl - f.rl);
      const bool cornerNear = slowU(0.0f) || slowU(45.0f);

      if (ahead >= 0) {
        const Car& a = cars[ahead];
        float lenAvg = (cs.len + CLS[a.cls].len) * 0.5f, widAvg = (cs.wid + CLS[a.cls].wid) * 0.5f;
        float gap = aheadGap - lenAvg;
        bool lapping = c.dist - a.dist > trk.L * 0.5f;
        // only a real speed edge counts: not just that they're already braking for a corner
        const bool aAtSpeed = !a.braking && !slowAt(a.s);
        // A real speed edge: closing on them now, or able to go faster than them
        // down this straight with the tow. (Not just that they're still
        // accelerating out of the corner while our target is already higher.)
        const float edge = fmx(c.v - a.v, vTopCap * (1.0f + TOW_GAIN * c.tow) - a.vTop);
        bool faster = (edge > T.passMargin && aAtSpeed) || a.state != CS_RACE || (lapping && vFree > a.v);
        bool allowed = !cornerNear || a.state != CS_RACE || lapping;
        const float passWin = fmx(T.passGap * lenAvg, c.v * PASS_T * (T.passGap / 3.0f));
        const bool obstacle = a.state != CS_RACE || a.v < OBSTACLE_V;   // crawling or stricken: just go round
        // Round the outside: right behind them in a corner, carrying as much speed,
        // a driver may swing wide and try to go round (more so if they cover the inside).
        const bool cornerMove = aiLines && cornerNear && !lapping && !obstacle && gap < lenAvg * OUTSIDE_GAP && c.v > a.v - 3.0f &&
                                frand() < OUTSIDE_RATE * (a.coverT > 0 ? 2.0f : 1.0f) * OUTSIDE_RATE_K * dt;
        if (c.passT <= 0 && c.passCd <= 0 && ((faster && allowed) || cornerMove) && gap < passWin && racing && !coolDown) {
          const float hwP = hwC + PASS_EDGE;       // stays on the track proper
          // room either side of them, now and where their line takes them next
          float roomR = 1e9f, roomL = 1e9f;
          for (float u = 0; u <= 75.0f; u += 25.0f) {
            float ad = u == 0 ? a.d : (aiLines ? lineD(a, ahead, a.s + u) : tp[idxAt(a.s + u)].rl);
            roomR = fmn(roomR, hwP - (ad + widAvg + PASS_LANE) - T.passRoom);
            roomL = fmn(roomL, (ad - widAvg - PASS_LANE) + hwP - T.passRoom);
          }
          if (roomR > 0 || roomL > 0) {
            // Which way does the next real corner go, and how far is it?
            float kN = 0;
            const float uN = nextCorner(30.0f, 300.0f, kN);
            if (kN == 0) kN = tpU(fi0, 50.0f).rk;
            int inside = kN > 0 ? 1 : -1;
            float roomIn = inside > 0 ? roomR : roomL, roomOut = inside > 0 ? roomL : roomR;
            c.passSide = (roomIn > 0 && (roomIn >= roomOut - 2.0f || uN < PASS_INSIDE_U)) ? inside : -inside;
            const bool insideShut = roomIn <= 0 || a.coverT > 0;
            c.outMove = 0;
            if (cornerMove && !(faster && allowed)) { c.passSide = roomOut > 0 ? -inside : 0; c.outMove = 1; }   // chose the outside
            else if (aiLines && insideShut && roomOut > 0 && uN < PASS_INSIDE_U && gap < lenAvg * OUTSIDE_GAP * 1.5f) { c.passSide = -inside; c.outMove = 1; }
            // round the outside into a corner that's close, without meaning it: not a real move, so wait
            else if (c.passSide != inside && uN < PASS_INSIDE_U && !lapping && !obstacle) c.passSide = 0;
          }
          if (c.passSide != 0 && (roomR > 0 || roomL > 0)) {
            c.passT = 2.5f; c.passAge = 0; c.passCar = (int8_t)ahead;
            if (cornerNear) c.commit = fmx(c.commit, ATTACK_COMMIT);   // late on the brakes
          }
        }
        float fg = T.followGap * lenAvg;
        if (gap < lenAvg * BOXED_LEN && c.passT <= 0) towBoxed[i] = true;    // boxed in: can't use the tow to accelerate
        // close behind: ease back toward the following gap, but don't stand on the brakes for it
        if (gap < lenAvg * 1.5f) vT = fmn(vT, fmx(gap > 0.0f ? a.v * 0.9f : 0.0f, a.v + (gap - fg) * 2.2f));
        // and further back, brake in time for a much slower car (stopping distance)
        else vT = fmn(vT, sqrtf(a.v * a.v + 2.0f * ab * FOLLOW_BRAKE * (gap - fg)));
      }
      if (c.passT > 0 && c.passCar >= 0) {
        const Car& a = cars[c.passCar];
        float widAvg = (cs.wid + CLS[a.cls].wid) * 0.5f;
        const float lane = widAvg + PASS_LANE, edge = hwC + PASS_EDGE;
        float want = a.dPath + c.passSide * lane;
        if (fabsf(want) > edge) {                  // they've closed that side
          float other = a.dPath - c.passSide * lane;
          bool alongside = sDelta(c.s, a.s) < (cs.len + CLS[a.cls].len) * 0.5f;
          if (fabsf(other) <= edge && !alongside) { c.passSide = (int8_t)-c.passSide; want = other; }   // switch
          else if (!alongside) { c.passT = 0; c.passCd = PASS_RETRY; }   // tuck back in behind
          else if (c.outMove && sDelta(a.s, c.s) > -(cs.len + CLS[a.cls].len) * 0.6f) { }   // alongside round the outside: hold the edge, they must leave room
          else if (sDelta(a.s, c.s) < 0) { c.passT = 0; c.passCd = PASS_RETRY; vT = fmn(vT, fmx(a.v - 3.0f, 10.0f)); }   // squeezed: lift and drop in behind
        }
        dT = clampf(want, -edge, edge);
        {
          float kC = 0, uC = nextCorner(0.0f, 60.0f, kC);
          if (uC > 60.0f) uC = 60.0f;
          const bool outside = kC != 0 && ((kC > 0) != (c.passSide > 0));
          const float lenAvgP = (cs.len + CLS[a.cls].len) * 0.5f;
          // ...unless we are carrying enough extra speed to be ahead before turning in
          const float aheadAtTurnIn = sDelta(a.s, c.s) + (c.v - a.v) * (uC / fmx(c.v, 20.0f));
          const bool holdOutside = aiLines &&
                                   (sDelta(a.s, c.s) > -lenAvgP * 0.6f || c.v > a.v + 4.0f);   // alongside or quicker: keep going round the outside
          if (c.outMove && (sDelta(a.s, c.s) < -lenAvgP * OUTSIDE_DROP || (c.passAge > 0.8f && sDelta(a.s, c.s) < -lenAvgP * 1.4f && c.v < a.v - 1.0f))) {
            c.passT = 0; c.passCd = PASS_RETRY; c.outMove = 0;           // dropped back: tuck in behind again
            vT = fmn(vT, fmx(a.v - 2.0f, 10.0f));
          }
          else if (outside && !holdOutside && !c.outMove && aheadAtTurnIn < lenAvgP * 0.3f && !lapping0(c, a) && a.state == CS_RACE && a.v >= OBSTACLE_V) {
            // hold this lane and ease off until we are behind them; the normal
            // rules then bring us back onto the racing line
            c.passT = 0; c.passCd = PASS_RETRY;
            vT = fmn(vT, fmx(a.v - 4.0f, 10.0f));
          }
        }
        // alongside and on the inside of the next corner: the other car gives way
        {
          float kN = 0;
          nextCorner(0.0f, 60.0f, kN);
          bool inside = (kN > 0 && c.passSide > 0) || (kN < 0 && c.passSide < 0);
          float overlap = sDelta(a.s, c.s);           // > 0: we're ahead of them
          if (inside && overlap > -(cs.len * OVERLAP_NEED))
          {
            Car& dfd = cars[c.passCar];
            dfd.concedeT = 0.5f;
            if (aiLines && dfd.cutback < 0 && frand() < CUTBACK_P) {        // swing wide, cut back underneath on the exit
              const int kc2 = (dfd.nextCorner - 1 + trk.nCorners) % trk.nCorners;
              dfd.cutback = (int8_t)kc2; planCornerLine(dfd, c.passCar, kc2);
            }
          }
        }
        // clear of them now: they can't come straight back at us on the same straight
        if (sDelta(cars[c.passCar].s, c.s) > cs.len) cars[c.passCar].passCd = fmx(cars[c.passCar].passCd, REPASS_CD);
        c.passT -= dt;
        c.passAge += dt;
        // still gaining on them and not done yet: keep going
        if (c.passT > 0 && c.passT < 0.6f && c.passAge < PASS_MAX_T && c.v > a.v + 1.0f && sDelta(a.s, c.s) < 0) c.passT = 0.6f;
        // round the outside: keep going through the corner and out of it while still on terms
        if (c.outMove && c.passT > 0 && c.passT < 0.6f && c.passAge < PASS_MAX_T && cornerNear && sDelta(a.s, c.s) > -(cs.len + CLS[a.cls].len) * 0.6f) c.passT = 0.6f;
        if (c.passT <= 0) c.outMove = 0;
        if (sDelta(a.s, c.s) > (cs.len + CLS[a.cls].len) * 0.5f + 3.0f) c.passT = fmn(c.passT, 0.5f);   // done
        if (fabsf(sDelta(a.s, c.s)) > 90.0f) c.passT = 0;
      }
      // Being attacked: defend, or let them by
      if (attacker >= 0 && racing && !coolDown) {
        const Car& o = cars[attacker];
        if (aiLines && o.outMove && cornerNear && attackGap < (cs.len + CLS[o.cls].len) * 0.5f * OUT_DEF_OVL) {
          vT *= OUT_DEF_SLOW;
          const float keep = (cs.wid + CLS[o.cls].wid) * 0.5f + RACE_ROOM;   // a car's width and a bit
          if (o.d < c.d) dT = fmx(dT, fmn(o.d + keep, hwC)); else dT = fmn(dT, fmx(o.d - keep, -hwC));
        }
        bool lapped = o.dist - c.dist > trk.L * 0.5f;
        if (lapped && c.yieldT <= 0) { c.yieldT = 2.0f; c.yieldSide = (int8_t)(o.passSide > 0 ? -1 : 1); }
      }
      const bool threat = behind >= 0 && behindGap - (cs.len + CLS[cars[behind].cls].len) * 0.5f < cars[behind].v * COVER_T && cars[behind].v > c.v - 3.0f;
      if (aiLines && racing && !coolDown && c.state == CS_RACE && threat && c.coverT <= 0 && c.blockCd <= 0) {
        // A car right on our tail, and at least as quick, with a corner coming: maybe cover the
        // inside up to the turn-in. (Covering for anyone close, and holding it through the exit,
        // left a whole queue on slow inside lines while the leaders got away in long races.)
        float kq = 0; const float uq = nextCorner(40.0f, 160.0f, kq);
        if (uq < 1e8f && kq != 0 && frand() < COVER_RATE * dt) { c.coverT = fmn(1.6f, uq / fmx(c.v, 30.0f) + 0.15f); c.blockCd = COVER_CD; c.coverSide = kq > 0 ? 1 : -1; }
      }
      if (c.coverT > 0) { c.coverT -= dt; dT = c.coverSide * (hwC - 1.5f); }
      if (c.yieldT > 0) { c.yieldT -= dt; dT = c.yieldSide * hwC * 0.75f; vT = fmn(vT, vFree * 0.97f); }
      if (c.blockCd > 0) c.blockCd -= dt;
      if (c.passCd > 0) c.passCd -= dt;

      // Racing room: with a car alongside, don't take the line through it. If
      // that leaves no road and we're the one behind, lift and tuck in.
      if (racing && c.state == CS_RACE) {
        const float edgeR = hwC + PASS_EDGE;
        for (int j = 0; j < numCars; ++j) {
          if (j == i) continue;
          const Car& o = cars[j];
          if (o.state != CS_RACE || o.v < 15.0f || c.v < 15.0f) continue;   // a slow scramble: the normal traffic rules handle it
          float lenAvg = (cs.len + CLS[o.cls].len) * 0.5f, widAvg = (cs.wid + CLS[o.cls].wid) * 0.5f;
          float g = sDelta(c.s, o.s);
          if (fabsf(g) > lenAvg * 1.1f || fabsf(o.d - c.d) > widAvg + 4.0f) continue;
          const float room = widAvg + RACE_ROOM;
          if (o.d >= c.d) { dT = fmn(dT, o.d - room); if (dT < -edgeR && g > 0) { if (c.passT > 0) c.passCd = PASS_RETRY; c.passT = 0; vT = fmn(vT, fmx(o.v - 3.0f, 10.0f)); } }
          else            { dT = fmx(dT, o.d + room); if (dT >  edgeR && g > 0) { if (c.passT > 0) c.passCd = PASS_RETRY; c.passT = 0; vT = fmn(vT, fmx(o.v - 3.0f, 10.0f)); } }
          dT = clampf(dT, -edgeR, edgeR);
        }
      }

      if (!racing || raceTime < c.react) { vT = 0; dT = c.d; }
      c.dbgVT = vT; c.dbgAhead = (int8_t)ahead;

      // ── Lane: the offset of the line being followed eases toward the target ──
      float laneRate = 0.24f * c.v + (c.v > 4.0f ? 5.0f : 0.0f);
      float oldPath = c.dPath;
      // ease into the new line: full rate when it's far, slowing as it arrives,
      // so the car settles onto it instead of overshooting at speed
      const float laneStep = fmn(laneRate, LANE_EASE * fabsf(dT - c.dPath) + (c.passT > 0 ? PASS_PULL : 2.0f)) * dt;
      c.dPath += clampf(dT - c.dPath, -laneStep, laneStep);
      c.dPath = clampf(c.dPath, -hw - 6.0f, hw + 6.0f);

      // ── Longitudinal ──
      float accel = A_ACC * cs.acc * c.perf * (1.15f - 0.5f * c.v / (V_TOP * cs.top)) * wx * (c.surface == 1 ? 0.4f : (c.surface ? 0.6f : 1.0f));
      accel *= (1.0f + TOW_ACC * (towBoxed[i] ? 0.0f : c.tow) * clampf(c.v / (V_TOP * cs.top), 0, 1)) * (c.passT > 0 && c.passCar >= 0 ? 1.0f + PASS_PUSH_ACC : 1.0f);   // drag matters more the faster you go
      // Faster than the straight-line target only because the tow has gone
      // (pulling out, or drawing alongside): lift and let drag slow the car,
      // keeping the momentum, instead of braking. Corners and traffic still brake.
      const bool coast = c.v > vT && vT >= vTopCap * 0.995f && c.v * c.v < limMin && c.state == CS_RACE;
      float latShare = 1.0f;                       // share of the tyre left for cornering after the pedals
      float overThrottle = 0;
      if (aiLines) {
        // What the line asks of the tyre sideways right now.
        const float latUse = fmn(1.0f, c.v * c.v * fabsf(kLineHere) / fmx(aLatMax, 1.0f));
        float acc = accel;
        if (c.v < vT) {
          // traction: what the tyre can take while still cornering; pushing an exit asks for more
          const float tracAvail = accel * sqrtf(fmx(0.05f, 1.0f - latUse * latUse * 0.85f));
          acc = fmn(accel, tracAvail * (1.0f + c.exitPush));
          if (acc > tracAvail && latUse > 0.3f) overThrottle = (acc - tracAvail) / fmx(accel, 1.0f);
        }
        const float v0 = c.v;
        c.braking = vT < c.v - 2.0f && !coast;
        if (c.v < vT) c.v = fmn(vT, c.v + acc * dt);
        else if (coast) c.v = fmx(vT, c.v - COAST_DECEL * dt);
        else {
          float need = (c.v - vT) / dt;
          c.v -= fmn(need, aBrkMax) * dt;
          if (need > aBrkMax * 1.05f && c.v > 50.0f) c.lockT = fmx(c.lockT, 0.1f);   // over the limit: locking up
        }
        c.decelAvg = lerpf(c.decelAvg, (v0 - c.v) / dt, 0.25f);
        // The friction ellipse, for braking past the comfortable plan while turning
        // (a dive, braking late, a lock-up): less grip left to turn, so it runs wide.
        float lg = 0;
        if (latUse > 0.15f) lg = fmx(0.0f, c.decelAvg - abPlain * 0.95f) / fmx(aBrkMax - abPlain * 0.95f, 1.0f) + (c.lockT > 0 ? 0.3f : 0.0f);
        latShare = sqrtf(fmx(0.2f, 1.0f - fmn(1.0f, lg) * fmn(1.0f, lg)));
      } else {
        c.braking = vT < c.v - 2.0f && !coast;
        if (c.v < vT) c.v = fmn(vT, c.v + accel * dt);
        else if (coast) c.v = fmx(vT, c.v - COAST_DECEL * dt);
        else {
          float need = (c.v - vT) / dt;
          c.v -= fmn(need, aBrkMax) * dt;
          if (need > aBrkMax * 1.05f && c.v > 50.0f) c.lockT = fmx(c.lockT, 0.1f);   // over the limit: locking up
        }
      }

      // ── Steering: follow the chosen line ──
      const float La = 10.0f + 0.22f * c.v;
      const float slope = clampf((c.dPath - oldPath) / fmx(c.v * dt, 0.5f), -0.6f, 0.6f);
      const float psiP = trackAng + slope;
      // the driven line runs parallel to the racing line (offset dPath - rl)
      const float kff = kLineHere / clampf(1.0f - kLineHere * (c.dPath - lineHere), 0.5f, 1.5f);
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
      const float kMax = aLatMax * latShare / fmx(c.v * c.v, 1.0f);
      const float cap = kMax * 0.97f;
      kc = clampf(kc, -cap, cap);
      const float demand = fabsf(kff) / kMax;
      // for spins: what the corner itself asks of the tyre (a front slide from braking runs wide, it doesn't spin)
      const float demandSpin = aiLines ? fabsf(kff) * c.v * c.v / fmx(aLatMax, 1.0f) : demand;
      float slipT = 0;
      if (demand > 1.0f) {
        float sgn = kff > 0 ? 1.0f : -1.0f;
        kc = sgn * fmn(fabsf(kc), kMax);
        float over = fmn(demand - 1.0f, 1.0f);
        c.v = fmx(0.0f, c.v - over * 55.0f * dt);                       // scrubbing speed
        slipT = sgn * fmn(0.45f, over * 0.8f);
        if (over > 0.15f && frand() < 0.5f) spawnParticle(PT_SMOKE, c.x, c.y, frandr(-0.2f, 0.2f), frandr(-0.2f, 0.2f), 1.8f, 0.09f, 18);
        if (demandSpin > 1.1f && c.v > 40.0f &&
            frand() < 0.01f * (demandSpin - 1.1f) / 0.1f * T.spin * (1.0f + 1.5f * wetAmt) * (c.surface ? 2.0f : 1.0f))
        {
#ifdef RT_DEBUG_SPIN
          printf("  spin-slide %s s=%.0f d=%.1f v=%.0f demand=%.2f surf=%d k=%.4f rk=%.4f\n", c.code, c.s, c.d, c.v, demand, c.surface, f.k, f.rk);
#endif
          startSpin(c, i, frand() < 0.7f ? sgn : -sgn);
        }
      }
      if (aiLines) {
        if (demand > 1.15f) c.overT = 0.6f;                              // running wide
        // Too much throttle while still turning: the rear steps out. The car
        // rotates (drawn sideways), loses drive, and a big moment can spin it.
        if (overThrottle > 0.03f && fabsf(kff) * c.v > 0.01f && c.v > 30.0f) {
          const float sgn = kff > 0 ? 1.0f : -1.0f;
          slipT += sgn * fmn(0.5f, overThrottle * 1.6f);
          c.v = fmx(0.0f, c.v - overThrottle * 40.0f * dt);
          kc += sgn * fmn(kMax * 0.15f, overThrottle * kMax * 0.3f);       // tightens the line (oversteer)
          if (overThrottle > 0.08f) c.overT = 0.6f;          // a proper slide (small ones just rotate the car a little)
          if (overThrottle > 0.15f && frand() < 0.002f * (overThrottle - 0.15f) / 0.05f * T.spin * (1.0f + wetAmt * 2.0f))
            startSpin(c, i, sgn);
          if (frand() < 0.3f) spawnParticle(PT_SMOKE, c.x, c.y, frandr(-0.2f, 0.2f), frandr(-0.2f, 0.2f), 1.6f, 0.08f, 16);
        }
        if (c.overT > 0) c.overT -= dt;
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
        if (frand() < 0.08f) {
#ifdef RT_DEBUG_SPIN
          printf("  spin-contact %s by %s s=%.0f\n", front.code, rear.code, front.s);
#endif
          startSpin(front, fi, dd > 0 ? 1.0f : -1.0f);
        }
        for (int k = 0; k < 2; ++k) spawnParticle(PT_SPARK, (rear.x + front.x) * 0.5f, (rear.y + front.y) * 0.5f, frandr(-1, 1), frandr(-1, 1), 0.8f, 0, 6);
        pushEv(EV_CONTACT, ri, fi);
      }
      if (fabsf(dd) < need * 0.45f) { rear.v = fmn(rear.v, front.v * 0.97f); continue; }   // nose to tail
      // Push them apart, but whoever is already at the edge of the road gives
      // less and the other car takes the rest.
      const float sgn = dd >= 0 ? 1.0f : -1.0f;      // b is on this side of a
      float pushA = (need - fabsf(dd)) * 0.5f, pushB = pushA;
      const float limA = trk.hw + KERB_W * 0.5f - CLS[a.cls].wid * 0.5f, limB = trk.hw + KERB_W * 0.5f - CLS[b.cls].wid * 0.5f;
      float roomA = limA + sgn * a.d, roomB = limB - sgn * b.d;   // how far each can move outward
      if (pushB > roomB) { float x = pushB - fmx(roomB, 0.0f); pushB -= x; pushA += x; }
      if (pushA > roomA) { float x = pushA - fmx(roomA, 0.0f); pushA -= x; pushB += x; }
      // Nudge sideways only. Re-placing the cars from (s, d) would throw away
      // their forward motion wherever s barely changes (the inside of a tight
      // hairpin), pinning a car next to its neighbour for good.
      TrackFrame fa, fb; trackAt(a.s, fa); trackAt(b.s, fb);
      a.d -= sgn * pushA; a.x += fa.ty * sgn * pushA; a.y -= fa.tx * sgn * pushA;
      b.d += sgn * pushB; b.x -= fb.ty * sgn * pushB; b.y += fb.tx * sgn * pushB;
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
        if (aiLines && racing && c.state == CS_RACE && c.passT <= 0 && c.cutback < 0 && frand() < SETUP_RATE) {
          // right behind someone into this corner: wide entry, late apex, for a better exit and the tow
          for (int j = 0; j < numCars; ++j) {
            if (j == i) continue;
            float g = sDelta(c.s, cars[j].s);
            if (g > 0 && g < fmx(c.v, 40.0f) * SETUP_T && cars[j].state == CS_RACE) {
              c.cutback = (int8_t)c.nextCorner; planCornerLine(c, i, c.nextCorner); break;
            }
          }
        }
        c.nextCorner = (c.nextCorner + 1) % trk.nCorners;
        if (aiLines && trk.nCorners > 4) {
          const int back = (c.nextCorner - 3 + trk.nCorners) % trk.nCorners;
          if (c.cutback == back) c.cutback = -1;
          planCornerLine(c, i, back);                    // next lap through it
        }
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

// Sort by position: by distance covered while racing. Finishers rank by laps
// completed, then by when they crossed the line, so a car a lap down that takes
// the flag just behind the winner still ranks behind every car on the lead lap
// (a car still running will finish with lapsAtFlag + 1 laps).
static void updateOrder() {
  for (int i = 0; i < numCars; ++i) order[i] = i;
  std::sort(order, order + numCars, [](int a, int b) {
    const Car& A = cars[a]; const Car& B = cars[b];
    if (A.finished && B.finished) return A.finishLaps != B.finishLaps ? A.finishLaps > B.finishLaps : A.finishPos < B.finishPos;
    if (A.finished) return A.finishLaps >= B.lapsAtFlag + 1;
    if (B.finished) return B.finishLaps < A.lapsAtFlag + 1;
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
struct CarDraw { float sx, sy, ca, sa, hl, hwd; int y0, y1; bool vis; uint32_t l1, l2, l3; };   // l1..l3: livery, lit
static CarDraw cdraw[MAX_CARS];
// Shared car colours, lit for this frame (worked out once, not in every strip)
static uint32_t litTyre, litGlass, litDark, litLamp, litWhite, litRack;

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
    if (d.vis) { d.l1 = litT(c.c1); d.l2 = litT(c.c2); d.l3 = litT(c.c3); }
  }
  litTyre = litT(0x1A1A1Cu); litGlass = litT(0x1C2A38u); litDark = litT(0x101012u);
  litLamp = litT(0xF0F0D8u); litWhite = litT(0xF4F4F4u); litRack = litT(0x2A2E34u);
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
  const uint32_t c1 = d.l1, c2 = d.l2, c3 = d.l3;
  const uint32_t tyre = litTyre, glass = litGlass, dark = litDark;
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
      cQuad(d, 0.86f, 0.98f, 0.45f, 0.78f, litLamp); cQuad(d, 0.86f, 0.98f, -0.78f, -0.45f, litLamp);
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
      cQuad(d, 0.92f, 1.0f, 0.5f, 0.8f, litLamp); cQuad(d, 0.92f, 1.0f, -0.8f, -0.5f, litLamp);
      break;
    case CC_TOURING:
      cQuad(d, -1.0f, 1.0f, -1.0f, 1.0f, c1);
      cQuad(d, 0.18f, 0.38f, -0.80f, 0.80f, glass);
      cQuad(d, -0.38f, 0.18f, -0.82f, 0.82f, c2);               // two-tone roof
      cQuad(d, -0.58f, -0.38f, -0.78f, 0.78f, glass);
      { int rx, ry; carPt(d, -0.1f, 0, rx, ry); fCircle(rx, ry, (int)fmx(1, d.hwd * 0.35f), litWhite); }
      cQuad(d, 0.6f, 0.98f, 0.85f, 1.0f, c3); cQuad(d, 0.6f, 0.98f, -1.0f, -0.85f, c3);
      cQuad(d, 0.94f, 1.0f, 0.5f, 0.85f, litLamp); cQuad(d, 0.94f, 1.0f, -0.85f, -0.5f, litLamp);
      break;
    case CC_VAN:   // panel van: short sloping bonnet, long flat roof
      cQuad(d, -1.0f, 0.62f, -1.0f, 1.0f, c1);
      cTri(d, 0.62f, -1.0f, 1.0f, -0.82f, 1.0f, 0.82f, c1);
      cTri(d, 0.62f, -1.0f, 1.0f, 0.82f, 0.62f, 1.0f, c1);
      cQuad(d, 0.46f, 0.64f, -0.86f, 0.86f, glass);             // windscreen
      cQuad(d, -0.94f, 0.42f, -0.30f, 0.30f, c2);               // roof stripe
      cQuad(d, -0.72f, -0.62f, -0.88f, 0.88f, litRack); // roof-rack bars
      cQuad(d, -0.30f, -0.20f, -0.88f, 0.88f, litRack);
      cQuad(d, -1.0f, -0.96f, -0.03f, 0.03f, dark);             // split rear doors
      { int rx, ry; carPt(d, 0.12f, 0, rx, ry); fCircle(rx, ry, (int)fmx(1, d.hwd * 0.34f), litWhite); }
      cQuad(d, 0.94f, 1.0f, 0.55f, 0.86f, litLamp); cQuad(d, 0.94f, 1.0f, -0.86f, -0.55f, litLamp);
      break;
    default:   // CC_CLASSIC — cigar-shaped single-seater
      cWheels(d, 0.35f, 0.65f, -0.68f, -0.32f, 0.62f, tyre);
      cQuad(d, -0.7f, 0.4f, -0.46f, 0.46f, c1);
      cTri(d, 0.4f, -0.46f, 1.0f, 0.0f, 0.4f, 0.46f, c1);
      cTri(d, -0.7f, -0.46f, -1.0f, -0.2f, -1.0f, 0.2f, c1);
      cTri(d, -0.7f, -0.46f, -1.0f, 0.2f, -0.7f, 0.46f, c1);
      cQuad(d, -0.35f, -0.05f, -0.26f, 0.26f, dark);
      { int hx, hy; carPt(d, -0.22f, 0, hx, hy); fCircle(hx, hy, (int)fmx(1, d.hwd * 0.24f), c3); }
      { int rx, ry; carPt(d, 0.52f, 0, rx, ry); fCircle(rx, ry, (int)fmx(1, d.hwd * 0.22f), litWhite); }
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
