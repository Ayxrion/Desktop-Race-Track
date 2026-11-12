#pragma once
// Circuit geometry: centreline, racing line, speed profile, corners, kerbs,
// run-off, barriers, pit lane and grandstands. Built either from a real-circuit
// preset (track_presets.h) or procedurally from a seed.
//
// Conventions (world pixels, y down):
//   s  = distance along the centreline, 0 at the start/finish line, [0, L)
//   d  = lateral offset from the centreline, + = to the driver's right
//   normal n = (-ty, tx) points to the driver's right
//   k  = signed curvature, + = right-hand bend (so the inside of a bend is sign(k))

#include "common.h"
#include "track_presets.h"
#include <algorithm>

#define MAX_SAMPLES   2048
#define MAX_CORNERS   40
#define MAX_STANDS    10
#define MAX_LIGHTS    40
#define KERB_W        3.0f
#define TRACK_W_DEF   28.0f

// Speed model, in world px and seconds. Laps take roughly 25–45 s.
#define V_TOP   175.0f
#define A_LAT   70.0f      // cornering grip: v = sqrt(A_LAT / |k|)
#define A_BRK   115.0f
#define A_ACC   55.0f
#ifndef RT_ROUND_M
#define RT_ROUND_M 2.0f          // preset corners are rounded to at least half the road width plus this (px radius)
#endif
#ifndef RL_EDGE
#define RL_EDGE 4.0f             // px from the edge the racing line keeps
#endif
#ifndef RL_CURV_ITERS
#define RL_CURV_ITERS 25         // minimum-curvature passes per scale when building the racing line
#endif

// Fit box for the circuit. The strip on the left stays free so the leaderboard
// doesn't cover the track at the whole-track zoom.
#define FIT_X0  262.0f
#define FIT_X1  1548.0f
#define FIT_Y0  58.0f
#define FIT_Y1  902.0f

struct TrackPt {
  float x, y;        // centreline
  float tx, ty;      // unit tangent
  float k;           // signed curvature (+ = right-hand bend)
  float rl;          // racing-line offset (+ = right)
  float v;           // base speed profile on the racing line, with braking zones (px/s)
  float vc;          // cornering speed limit only (no braking zones), for per-driver braking
  float rk;          // signed curvature of the racing line
  float clr[2];      // distance to the nearest *other* part of the track: [0] left, [1] right
  float bar[2];      // barrier offset |d| per side (0 = none)
  uint8_t kerb[2];   // kerb on this side
  uint8_t run[2];    // run-off surface: 0 grass, 1 gravel, 2 tarmac
};

// A corner: where it bends (i0..i1, apex at the tightest point) and, for the
// drivers' own lines, where they turn in (sIn), apex (sAp) and track out (sOut).
// sIn/sOut may lie outside [0, L): compare them with sDelta().
struct Corner   { int i0, i1, apex; int8_t dir; float vMin; float sIn, sAp, sOut, q; };
struct Stand    { float sc, half, d0, d1; int8_t side; uint8_t roofHue; };   // centred at sc, on side 0/1
struct LightPos { int16_t x, y; };

struct Track {
  int   n = 0;
  float L = 1, ds = 1, W = TRACK_W_DEF, hw = TRACK_W_DEF * 0.5f;
  char  name[40] = "";
  char  place[24] = "";
  int   preset = -1;                 // -1 = generated
  uint32_t seed = 0;
  bool  hasPits = false; int8_t pitSide = 1; float pitS0 = 0, pitS1 = 0;   // pit range in signed s around 0
  int   nCorners = 0; Corner   corners[MAX_CORNERS];
  int   nStands  = 0; Stand    stands[MAX_STANDS];
  int   nLights  = 0; LightPos lights[MAX_LIGHTS];
};

static Track    trk;
static TrackPt* tp = nullptr;            // trk.n samples, PSRAM
static float*   _tmpA = nullptr;         // scratch buffers for generation (PSRAM)
static float*   _tmpB = nullptr;
static float*   _tmpC = nullptr;         // 3 × MAX_SAMPLES, for rounding off preset corners (PSRAM)
#define TMP_N 4096

static bool allocTrack() {
  tp    = (TrackPt*)heap_caps_malloc(sizeof(TrackPt) * MAX_SAMPLES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  _tmpA = (float*)heap_caps_malloc(sizeof(float) * TMP_N, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  _tmpB = (float*)heap_caps_malloc(sizeof(float) * TMP_N, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  _tmpC = (float*)heap_caps_malloc(sizeof(float) * 3 * MAX_SAMPLES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return tp && _tmpA && _tmpB && _tmpC;
}

// ─── Lookups ──────────────────────────────────────────────────────────────────
static inline float wrapS(float s) {
  if (s >= trk.L) s -= trk.L * (int)(s / trk.L);
  if (s < 0) s += trk.L * (1 + (int)(-s / trk.L));
  if (s >= trk.L) s -= trk.L;
  return s;
}
// b − a, wrapped into [−L/2, L/2)
static inline float sDelta(float a, float b) {
  float d = b - a;
  float h = trk.L * 0.5f;
  while (d >= h) d -= trk.L;
  while (d < -h) d += trk.L;
  return d;
}
static inline int wrapI(int i) { while (i >= trk.n) i -= trk.n; while (i < 0) i += trk.n; return i; }
static inline int idxAt(float s) { return wrapI((int)(wrapS(s) / trk.ds)); }
// Signed s around the start line: (−L/2, L/2]
static inline float sSigned(float s) { s = wrapS(s); return s > trk.L * 0.5f ? s - trk.L : s; }

struct TrackFrame { float x, y, tx, ty, k, rl, v, rk; };

static void IRAM_ATTR trackAt(float s, TrackFrame& f) {
  float fi = wrapS(s) / trk.ds;
  int i = (int)fi; float t = fi - i;
  if (i >= trk.n) { i = trk.n - 1; t = 1; }
  int j = i + 1 >= trk.n ? 0 : i + 1;
  const TrackPt& a = tp[i]; const TrackPt& b = tp[j];
  f.x = a.x + (b.x - a.x) * t;   f.y = a.y + (b.y - a.y) * t;
  f.tx = a.tx + (b.tx - a.tx) * t; f.ty = a.ty + (b.ty - a.ty) * t;
  float l = sqrtf(f.tx * f.tx + f.ty * f.ty); if (l > 1e-6f) { f.tx /= l; f.ty /= l; }
  f.k = a.k + (b.k - a.k) * t;
  f.rl = a.rl + (b.rl - a.rl) * t;
  f.v = a.v + (b.v - a.v) * t;
  f.rk = a.rk + (b.rk - a.rk) * t;
}

static void trackPoint(float s, float d, float& x, float& y) {
  TrackFrame f; trackAt(s, f);
  x = f.x - f.ty * d; y = f.y + f.tx * d;
}

// ─── Centreline construction ──────────────────────────────────────────────────
// Closed centripetal Catmull-Rom through m control points, then resampled at an
// even spacing of about 2.5 px.
static void buildCenterline(const float* px, const float* py, int m) {
  float* dx = _tmpA; float* dy = _tmpB;
  int sub = clampi((TMP_N - 8) / m, 4, 64);
  int nd = 0;
  for (int i = 0; i < m; ++i) {
    int i0 = (i - 1 + m) % m, i1 = i, i2 = (i + 1) % m, i3 = (i + 2) % m;
    float P0x = px[i0], P0y = py[i0], P1x = px[i1], P1y = py[i1];
    float P2x = px[i2], P2y = py[i2], P3x = px[i3], P3y = py[i3];
    float t0 = 0;
    float t1 = t0 + sqrtf(sqrtf((P1x - P0x) * (P1x - P0x) + (P1y - P0y) * (P1y - P0y))) + 1e-3f;
    float t2 = t1 + sqrtf(sqrtf((P2x - P1x) * (P2x - P1x) + (P2y - P1y) * (P2y - P1y))) + 1e-3f;
    float t3 = t2 + sqrtf(sqrtf((P3x - P2x) * (P3x - P2x) + (P3y - P2y) * (P3y - P2y))) + 1e-3f;
    for (int k = 0; k < sub; ++k) {
      float t = t1 + (t2 - t1) * k / sub;
      float A1x = (t1 - t) / (t1 - t0) * P0x + (t - t0) / (t1 - t0) * P1x, A1y = (t1 - t) / (t1 - t0) * P0y + (t - t0) / (t1 - t0) * P1y;
      float A2x = (t2 - t) / (t2 - t1) * P1x + (t - t1) / (t2 - t1) * P2x, A2y = (t2 - t) / (t2 - t1) * P1y + (t - t1) / (t2 - t1) * P2y;
      float A3x = (t3 - t) / (t3 - t2) * P2x + (t - t2) / (t3 - t2) * P3x, A3y = (t3 - t) / (t3 - t2) * P2y + (t - t2) / (t3 - t2) * P3y;
      float B1x = (t2 - t) / (t2 - t0) * A1x + (t - t0) / (t2 - t0) * A2x, B1y = (t2 - t) / (t2 - t0) * A1y + (t - t0) / (t2 - t0) * A2y;
      float B2x = (t3 - t) / (t3 - t1) * A2x + (t - t1) / (t3 - t1) * A3x, B2y = (t3 - t) / (t3 - t1) * A2y + (t - t1) / (t3 - t1) * A3y;
      dx[nd] = (t2 - t) / (t2 - t1) * B1x + (t - t1) / (t2 - t1) * B2x;
      dy[nd] = (t2 - t) / (t2 - t1) * B1y + (t - t1) / (t2 - t1) * B2y;
      ++nd;
    }
  }
  // Total length
  float L = 0;
  for (int i = 0; i < nd; ++i) { int j = (i + 1) % nd; L += sqrtf((dx[j] - dx[i]) * (dx[j] - dx[i]) + (dy[j] - dy[i]) * (dy[j] - dy[i])); }
  int n = clampi((int)(L / 2.5f), 400, MAX_SAMPLES);
  trk.n = n; trk.L = L; trk.ds = L / n;
  // Even resample
  int j = 0; float acc = 0;
  float segL = sqrtf((dx[1] - dx[0]) * (dx[1] - dx[0]) + (dy[1] - dy[0]) * (dy[1] - dy[0]));
  for (int i = 0; i < n; ++i) {
    float s = i * trk.ds;
    while (acc + segL < s && j < nd - 1) {
      acc += segL; ++j;
      int jn = (j + 1) % nd;
      segL = sqrtf((dx[jn] - dx[j]) * (dx[jn] - dx[j]) + (dy[jn] - dy[j]) * (dy[jn] - dy[j]));
    }
    int jn = (j + 1) % nd;
    float t = segL > 1e-6f ? clampf((s - acc) / segL, 0, 1) : 0;
    tp[i].x = dx[j] + (dx[jn] - dx[j]) * t;
    tp[i].y = dy[j] + (dy[jn] - dy[j]) * t;
  }
}

// Tangents and smoothed curvature.
static void computeFrames() {
  const int n = trk.n;
  float* head = _tmpA;
  for (int i = 0; i < n; ++i) {
    const TrackPt& a = tp[wrapI(i - 2)]; const TrackPt& b = tp[wrapI(i + 2)];
    float tx = b.x - a.x, ty = b.y - a.y, l = sqrtf(tx * tx + ty * ty);
    if (l < 1e-6f) { tx = 1; ty = 0; l = 1; }
    tp[i].tx = tx / l; tp[i].ty = ty / l;
    head[i] = atan2f(tp[i].ty, tp[i].tx);
  }
  for (int i = 0; i < n; ++i)
    tp[i].k = wrapAngle(head[wrapI(i + 2)] - head[wrapI(i - 2)]) / (4 * trk.ds);
  float* k2 = _tmpB;
  for (int pass = 0; pass < 3; ++pass) {
    for (int i = 0; i < n; ++i) k2[i] = (tp[wrapI(i - 1)].k + 2 * tp[i].k + tp[wrapI(i + 1)].k) * 0.25f;
    for (int i = 0; i < n; ++i) tp[i].k = k2[i];
  }
}

// Distance from each sample to the nearest part of the track that isn't the
// same stretch of road, per side.
static float computeClearance() {
  const int n = trk.n;
  int sep = (int)(110.0f / trk.ds);
  float minAll = 1e9f;
  // Compact copy of the points in internal RAM: this loop is O(n²) and the
  // TrackPt array lives in PSRAM.
  float* pxy = (float*)heap_caps_malloc(sizeof(float) * 2 * n, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  float* pxyFree = pxy;
  if (!pxy) pxy = _tmpA;                      // fall back to PSRAM scratch (n ≤ 2048 fits)
  for (int i = 0; i < n; ++i) { pxy[2 * i] = tp[i].x; pxy[2 * i + 1] = tp[i].y; }
  for (int i = 0; i < n; ++i) {
    float b0 = 1e9f, b1 = 1e9f;
    float xi = tp[i].x, yi = tp[i].y, nx = -tp[i].ty, ny = tp[i].tx;
    for (int j = 0; j < n; j += 2) {
      int dij = abs(j - i); if (dij > n / 2) dij = n - dij;
      if (dij < sep) continue;
      float dx = pxy[2 * j] - xi, dy = pxy[2 * j + 1] - yi, d2 = dx * dx + dy * dy;
      bool right = dx * nx + dy * ny > 0;
      if (d2 >= (right ? b1 : b0)) continue;
      // Same stretch of road (a straight, or a long sweeper) if the distance along
      // the track is barely more than the distance across.
      if (dij * trk.ds < 2.0f * sqrtf(d2) + 30.0f) continue;
      if (right) b1 = d2; else b0 = d2;
    }
    tp[i].clr[0] = b0 < 1e8f ? sqrtf(b0) : 999.0f;
    tp[i].clr[1] = b1 < 1e8f ? sqrtf(b1) : 999.0f;
    minAll = fmn(minAll, fmn(tp[i].clr[0], tp[i].clr[1]));
  }
  if (pxyFree) free(pxyFree);
  return minAll;
}

// Minimum-curvature racing line by elastic relaxation within the track edges.
static void computeRacingLine() {
  const int n = trk.n;
  float lim = trk.hw - 4.0f;
  float* o = _tmpA;
  for (int i = 0; i < n; ++i) o[i] = 0;
  const int steps[] = { 24, 12, 6, 3, 1 };
  for (int si = 0; si < 5; ++si) {
    int st = steps[si];
    for (int it = 0; it < 40; ++it) {
      for (int i = 0; i < n; ++i) {
        int a = wrapI(i - st), b = wrapI(i + st);
        float pax = tp[a].x - tp[a].ty * o[a], pay = tp[a].y + tp[a].tx * o[a];
        float pbx = tp[b].x - tp[b].ty * o[b], pby = tp[b].y + tp[b].tx * o[b];
        float px = tp[i].x - tp[i].ty * o[i], py = tp[i].y + tp[i].tx * o[i];
        float mx = (pax + pbx) * 0.5f - px, my = (pay + pby) * 0.5f - py;
        o[i] = clampf(o[i] + (mx * -tp[i].ty + my * tp[i].tx) * 0.6f, -lim, lim);
      }
    }
  }
  // That alone is a taut string: the shortest way round, which wraps the inside
  // kerb of a hairpin far too tightly (half the centreline's radius). Now relax
  // each point's bend toward its neighbours' (a biharmonic step), which spreads
  // the turn over the whole width: wide in, late apex, wide out.
  auto lx = [&](int j) { return tp[j].x - tp[j].ty * o[j]; };
  auto ly = [&](int j) { return tp[j].y + tp[j].tx * o[j]; };
  const float lim2 = trk.hw - RL_EDGE;            // a little more margin: this line uses the edges all through a corner
  for (int i = 0; i < n; ++i) o[i] = clampf(o[i], -lim2, lim2);
  const int steps2[] = { 16, 8, 4, 2, 1 };
  for (int si = 0; si < 5; ++si) {
    const int st = steps2[si];
    for (int it = 0; it < RL_CURV_ITERS; ++it) {
      for (int i = 0; i < n; ++i) {
        const int j0 = wrapI(i - 2 * st), j1 = wrapI(i - st), j3 = wrapI(i + st), j4 = wrapI(i + 2 * st);
        float mx = (-lx(j0) + 4 * lx(j1) + 4 * lx(j3) - lx(j4)) * (1.0f / 6.0f) - lx(i);
        float my = (-ly(j0) + 4 * ly(j1) + 4 * ly(j3) - ly(j4)) * (1.0f / 6.0f) - ly(i);
        o[i] = clampf(o[i] + (mx * -tp[i].ty + my * tp[i].tx) * 0.6f, -lim2, lim2);
      }
    }
  }
  for (int i = 0; i < n; ++i) tp[i].rl = o[i];
}

// Speed profile on the racing line: cornering limit, then braking zones.
static void computeSpeedProfile() {
  const int n = trk.n;
  float* rk = _tmpB;
  float* hx = _tmpA;   // racing-line heading
  for (int i = 0; i < n; ++i) {
    int a = wrapI(i - 3), b = wrapI(i + 3);
    float ax = tp[a].x - tp[a].ty * tp[a].rl, ay = tp[a].y + tp[a].tx * tp[a].rl;
    float bx = tp[b].x - tp[b].ty * tp[b].rl, by = tp[b].y + tp[b].tx * tp[b].rl;
    hx[i] = atan2f(by - ay, bx - ax);
  }
  // Curvature per unit length of the racing line itself. On the inside of a bend
  // its arc is much shorter than the centreline's, so its true curvature is higher.
  for (int i = 0; i < n; ++i) {
    float arc = 0;
    for (int k = -3; k < 3; ++k) {
      const TrackPt& a = tp[wrapI(i + k)]; const TrackPt& b = tp[wrapI(i + k + 1)];
      float ax = a.x - a.ty * a.rl, ay = a.y + a.tx * a.rl, bx = b.x - b.ty * b.rl, by = b.y + b.tx * b.rl;
      arc += sqrtf((bx - ax) * (bx - ax) + (by - ay) * (by - ay));
    }
    rk[i] = wrapAngle(hx[wrapI(i + 3)] - hx[wrapI(i - 3)]) / fmx(arc, 0.5f * trk.ds);
  }
  for (int i = 0; i < n; ++i) tp[i].rk = (rk[wrapI(i - 2)] + 2 * rk[wrapI(i - 1)] + 3 * rk[i] + 2 * rk[wrapI(i + 1)] + rk[wrapI(i + 2)]) / 9.0f;
  for (int i = 0; i < n; ++i) {
    // plan for the tightest curvature nearby, so short sharp bits aren't underestimated
    float ak = 1e-5f;
    for (int k = -4; k <= 4; ++k) ak = fmx(ak, fabsf(rk[wrapI(i + k)]));
    tp[i].v = tp[i].vc = fmn(V_TOP * 1.1f, sqrtf(A_LAT / ak));
  }
  for (int pass = 0; pass < 2; ++pass)
    for (int i = n - 1; i >= 0; --i) {
      int j = i + 1 >= n ? 0 : i + 1;
      tp[i].v = fmn(tp[i].v, sqrtf(tp[j].v * tp[j].v + 2 * A_BRK * trk.ds));
    }
}

// Rotate the sample ring so index `start` becomes the start/finish line.
static void rotateStart(int start) {
  if (start <= 0 || start >= trk.n) return;
  std::rotate(tp, tp + start, tp + trk.n);
}

// Longest low-curvature stretch; the start line goes 55% of the way along it.
static int findStartIndex() {
  const int n = trk.n;
  int bestLen = 0, bestEnd = 0, run = 0;
  for (int k = 0; k < 2 * n; ++k) {
    int i = k % n;
    if (fabsf(tp[i].k) < 1.0f / 600.0f) { ++run; if (run > bestLen && run <= n) { bestLen = run; bestEnd = k; } }
    else run = 0;
  }
  return wrapI(bestEnd - bestLen + (int)(bestLen * 0.55f));
}

// ─── Corners, kerbs, run-off and barriers ─────────────────────────────────────
static void computeCorners() {
  const int n = trk.n;
  const float th = 1.0f / 240.0f;
  trk.nCorners = 0;
  int start = 0;
  while (start < n && fabsf(tp[start].k) > th) ++start;          // begin on a straight bit
  int i = 0;
  while (i < n && trk.nCorners < MAX_CORNERS) {
    int ii = wrapI(start + i);
    if (fabsf(tp[ii].k) <= th) { ++i; continue; }
    int dir = tp[ii].k > 0 ? 1 : -1;
    int j = i;
    float turn = 0, mk = 0; int apex = ii; float vMin = 1e9f;
    while (j < n) {
      int jj = wrapI(start + j);
      if (fabsf(tp[jj].k) <= th || (tp[jj].k > 0 ? 1 : -1) != dir) break;
      turn += tp[jj].k * trk.ds;
      if (fabsf(tp[jj].k) > mk) { mk = fabsf(tp[jj].k); apex = jj; }
      vMin = fmn(vMin, tp[jj].v);
      ++j;
    }
    if (fabsf(turn) > 0.35f) {
      Corner& c = trk.corners[trk.nCorners++];
      c.i0 = ii; c.i1 = wrapI(start + j - 1); c.apex = apex; c.dir = (int8_t)dir; c.vMin = vMin;
    }
    i = j > i ? j : i + 1;
  }
  // Shapes for the drivers' lines: turn-in a little before the bend starts,
  // track-out a little after it ends, and an apex window either side of the apex.
  for (int k = 0; k < trk.nCorners; ++k) {
    Corner& c = trk.corners[k];
    float s0 = c.i0 * trk.ds, s1 = s0 + wrapS(c.i1 * trk.ds - s0), len = fmx(s1 - s0, trk.ds);
    float sa = s0 + wrapS(c.apex * trk.ds - s0);
    c.sIn = s0 - clampf(0.4f * len, 15.0f, 60.0f);
    c.sOut = s1 + clampf(0.5f * len, 20.0f, 80.0f);
    c.sAp = sa;
    c.q = clampf(0.3f * len, 8.0f, 50.0f);
    if (c.sAp - c.q < c.sIn + 6.0f) c.q = fmx(4.0f, c.sAp - c.sIn - 6.0f);
    if (c.sAp + c.q > c.sOut - 6.0f) c.q = fmn(c.q, fmx(4.0f, c.sOut - c.sAp - 6.0f));
  }
}

static int cornerLen(const Corner& c) { int l = c.i1 - c.i0; return l < 0 ? l + trk.n : l; }

static void computeRoadside() {
  const int n = trk.n;
  const float hw = trk.hw;
  float* runW[2] = { _tmpA, _tmpB };
  for (int i = 0; i < n; ++i) {
    runW[0][i] = runW[1][i] = 0;
    tp[i].kerb[0] = tp[i].kerb[1] = 0;
    tp[i].run[0] = tp[i].run[1] = 0;
  }
  for (int c = 0; c < trk.nCorners; ++c) {
    const Corner& C = trk.corners[c];
    int inside = C.dir > 0 ? 1 : 0, outside = 1 - inside;
    int len = cornerLen(C);
    int apexOff = C.apex - C.i0; if (apexOff < 0) apexOff += n;
    // kerbs: inside through the bend, outside from the apex to the exit
    for (int k = 0; k <= len; ++k) tp[wrapI(C.i0 + k)].kerb[inside] = 1;
    int exitExtra = (int)(10.0f / trk.ds);
    for (int k = apexOff; k <= len + exitExtra; ++k) tp[wrapI(C.i0 + k)].kerb[outside] = 1;
    // run-off on the outside, sized by how much speed is lost in the corner
    float w = clampf(10.0f + (V_TOP - C.vMin) * 0.24f, 12.0f, 46.0f);
    uint8_t type = (C.vMin < 100.0f || (hash32(trk.seed + c * 977) & 3) == 0) ? 1 : 2;
    int pre = (int)(26.0f / trk.ds), post = (int)(12.0f / trk.ds);
    int total = pre + len + post;
    for (int k = -pre; k <= len + post; ++k) {
      float u = (float)(k + pre) / total;
      float taper = smooth01(u * 4.0f) * smooth01((1.0f - u) * 3.0f);
      int ii = wrapI(C.i0 + k);
      float ww = w * taper;
      if (ww > runW[outside][ii]) { runW[outside][ii] = ww; tp[ii].run[outside] = ww > 3 ? type : tp[ii].run[outside]; }
    }
    // a little grass run-off on the inside of slow corners
    if (C.vMin < 90.0f)
      for (int k = 0; k <= len; ++k) { int ii = wrapI(C.i0 + k); runW[inside][ii] = fmx(runW[inside][ii], 6.0f); }
  }
  // Barrier offsets: behind the run-off (or a grass verge), smoothed, and kept
  // short of the halfway point to any neighbouring part of the circuit.
  for (int side = 0; side < 2; ++side) {
    float* b = runW[side];
    for (int i = 0; i < n; ++i) tp[i].bar[side] = hw + KERB_W + fmx(10.0f, b[i] + 3.0f);
  }
  // Smooth (moving average over ±7 samples). The run-off buffers are free now.
  for (int side = 0; side < 2; ++side) {
    float* sm = _tmpA;
    const int R = 7;
    for (int i = 0; i < n; ++i) {
      float acc = 0;
      for (int k = -R; k <= R; ++k) acc += tp[wrapI(i + k)].bar[side];
      sm[i] = acc / (2 * R + 1);
    }
    for (int i = 0; i < n; ++i) {
      float b = fmx(sm[i], tp[i].bar[side] - 4.0f);
      float lim = tp[i].clr[side] * 0.5f - 2.0f;
      if (b > lim) b = lim;
      if (b < hw + KERB_W + 3.0f) b = 0;          // no room for a barrier here
      tp[i].bar[side] = b;
      if (b == 0) tp[i].run[side] = 0;
    }
  }
}

// ─── Pits, grandstands, lights ────────────────────────────────────────────────
static float maxBarrier(float s0, float s1, int side) {
  float m = 0;
  for (float s = s0; s <= s1; s += trk.ds) m = fmx(m, tp[idxAt(s)].bar[side]);
  return m > 0 ? m : trk.hw + KERB_W + 8.0f;
}
static float minClear(float s0, float s1, int side) {
  float m = 1e9f;
  for (float s = s0; s <= s1; s += trk.ds) m = fmn(m, tp[idxAt(s)].clr[side]);
  return m;
}

static void addLight(float s, float d) {
  if (trk.nLights >= MAX_LIGHTS) return;
  float x, y; trackPoint(s, d, x, y);
  trk.lights[trk.nLights++] = { (int16_t)x, (int16_t)y };
}

static bool standOverlaps(float sc, float half, int side) {
  for (int i = 0; i < trk.nStands; ++i) {
    const Stand& st = trk.stands[i];
    if (st.side == side && fabsf(sDelta(sc, st.sc)) < half + st.half + 20) return true;
  }
  return false;
}

static void computePitsAndStands() {
  const float hw = trk.hw;
  trk.nStands = 0; trk.nLights = 0; trk.hasPits = false;
  // Pit side: whichever side of the start straight has more room.
  float c0 = 0, c1 = 0;
  for (float s = -40; s <= 40; s += trk.ds) { c0 += fmn(tp[idxAt(s)].clr[0], 300); c1 += fmn(tp[idxAt(s)].clr[1], 300); }
  int sd = c1 >= c0 ? 1 : 0;
  trk.pitSide = sd ? 1 : -1;
  const float need = hw + 66.0f;
  auto straightAt = [&](float s) {
    float acc = 0; int cnt = 0;
    for (float u = -20; u <= 20; u += trk.ds) { acc += tp[idxAt(s + u)].k; ++cnt; }
    return fabsf(acc / cnt) < 1.0f / 500.0f && tp[idxAt(s)].clr[sd] > need;
  };
  float a = 0, b = 0;
  while (a > -180 && straightAt(a - trk.ds)) a -= trk.ds;
  while (b < 180 && straightAt(b + trk.ds)) b += trk.ds;
  if (b - a >= 150) {
    trk.hasPits = true; trk.pitS0 = a; trk.pitS1 = b;
    for (float s = a; s <= b; s += trk.ds) { TrackPt& p = tp[idxAt(s)]; p.bar[sd] = 0; p.kerb[sd] = 0; p.run[sd] = 0; }
    for (float s = a + 30; s <= b - 30; s += 60) addLight(s, trk.pitSide * (hw + 27));
    // Main grandstand across from the pits
    int os = 1 - sd;
    float sc = (a + b) * 0.5f, half = (b - a) * 0.5f - 22;
    float d0 = maxBarrier(sc - half, sc + half, os) + 4, d1 = d0 + 24;
    if (half > 40 && minClear(sc - half, sc + half, os) > d1 + hw + 10) {
      trk.stands[trk.nStands++] = { wrapS(sc), half, d0, d1, (int8_t)os, 1 };
    }
  }
  // Grandstands on the outside of the slowest, most technical corners.
  int order[MAX_CORNERS];
  for (int i = 0; i < trk.nCorners; ++i) order[i] = i;
  std::sort(order, order + trk.nCorners, [](int x, int y) { return trk.corners[x].vMin < trk.corners[y].vMin; });
  for (int oi = 0; oi < trk.nCorners && trk.nStands < MAX_STANDS - 1 && trk.nStands < 7; ++oi) {
    const Corner& C = trk.corners[order[oi]];
    if (C.vMin > 120.0f) break;
    int outside = C.dir > 0 ? 0 : 1;
    float sc = C.apex * trk.ds, half = 48;
    if (standOverlaps(sc, half, outside)) continue;
    if (trk.hasPits && fabsf(sSigned(sc)) < (trk.pitS1 - trk.pitS0) * 0.5f + 80) continue;
    float d0 = maxBarrier(sc - half, sc + half, outside) + 4, d1 = d0 + 20;
    if (minClear(sc - half, sc + half, outside) < d1 + hw + 12) {
      half = 30;                                              // try a smaller one
      d0 = maxBarrier(sc - half, sc + half, outside) + 4; d1 = d0 + 16;
      if (minClear(sc - half, sc + half, outside) < d1 + hw + 12) continue;
    }
    const uint8_t hues[] = { 0, 1, 3, 4, 5, 7 };
    trk.stands[trk.nStands++] = { wrapS(sc), half, d0, d1, (int8_t)outside, hues[trk.nStands % 6] };
  }
  for (int i = 0; i < trk.nStands; ++i) {
    const Stand& st = trk.stands[i];
    float sg = st.side ? 1.0f : -1.0f;
    addLight(st.sc - st.half, sg * (st.d1 + 2));
    addLight(st.sc + st.half, sg * (st.d1 + 2));
  }
}

// ─── Fitting control points into the world ───────────────────────────────────
static void fitControl(float* px, float* py, int m, bool searchRotation) {
  float bestScale = 0, bestA = 0;
  float boxW = FIT_X1 - FIT_X0, boxH = FIT_Y1 - FIT_Y0;
  for (int k = 0; k < (searchRotation ? 72 : 1); ++k) {
    float a = k * PI / 72, ca = cosf(a), sa = sinf(a);
    float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f;
    for (int i = 0; i < m; ++i) {
      float x = px[i] * ca - py[i] * sa, y = px[i] * sa + py[i] * ca;
      x0 = fmn(x0, x); x1 = fmx(x1, x); y0 = fmn(y0, y); y1 = fmx(y1, y);
    }
    float sc = fmn(boxW / (x1 - x0), boxH / (y1 - y0));
    if (sc > bestScale) { bestScale = sc; bestA = a; }
  }
  float ca = cosf(bestA), sa = sinf(bestA);
  float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f;
  for (int i = 0; i < m; ++i) {
    float x = px[i] * ca - py[i] * sa, y = px[i] * sa + py[i] * ca;
    px[i] = x; py[i] = y;
    x0 = fmn(x0, x); x1 = fmx(x1, x); y0 = fmn(y0, y); y1 = fmx(y1, y);
  }
  float ox = (FIT_X0 + FIT_X1) * 0.5f - (x0 + x1) * 0.5f * bestScale;
  float oy = (FIT_Y0 + FIT_Y1) * 0.5f - (y0 + y1) * 0.5f * bestScale;
  for (int i = 0; i < m; ++i) { px[i] = px[i] * bestScale + ox; py[i] = py[i] * bestScale + oy; }
}

// ─── Procedural circuits ──────────────────────────────────────────────────────
static const char* const RAND_NAME_A[] = { "Valle", "Monte", "Lago", "Pinewood", "Redstone", "Bellmont", "Kestrel", "Ashford",
                                           "Coldwater", "Sierra", "Northfield", "Eagle", "Granite", "Maple", "Silver", "Oakridge" };
static const char* const RAND_NAME_B[] = { "Park", "Ring", "Raceway", "Circuit", "Motorpark", "Speedway" };

static void genRandomControl(uint32_t seed, float* px, float* py, int& m) {
  uint32_t save = _rng;
  _rng = seed * 2654435761u + 0x9E37u; if (!_rng) _rng = 1;
  m = irandr(9, 17);
  float off = frandr(0, PI2);
  bool lastIndent = false;
  for (int i = 0; i < m; ++i) {
    float a = off + PI2 * i / m + frandr(-0.3f, 0.3f) * PI2 / m;
    float r = frandr(0.55f, 1.0f);
    if (!lastIndent && frand() < 0.3f) { r = frandr(0.2f, 0.42f); lastIndent = true; } else lastIndent = false;
    px[i] = cosf(a) * r; py[i] = sinf(a) * r * 0.75f;
  }
  if (frand() < 0.5f) for (int i = 0; i < m / 2; ++i) { std::swap(px[i], px[m - 1 - i]); std::swap(py[i], py[m - 1 - i]); }
  _rng = save;
}

static void smoothControl(float* px, float* py, int m, float a) {
  float tx[32], ty[32];
  for (int i = 0; i < m; ++i) {
    int p = (i - 1 + m) % m, q = (i + 1) % m;
    tx[i] = px[i] + ((px[p] + px[q]) * 0.5f - px[i]) * a;
    ty[i] = py[i] + ((py[p] + py[q]) * 0.5f - py[i]) * a;
  }
  for (int i = 0; i < m; ++i) { px[i] = tx[i]; py[i] = ty[i]; }
}

// Cheap version of computeClearance() for validating candidate layouts.
static float quickMinClear() {
  const int n = trk.n;
  int sep = (int)(110.0f / trk.ds);
  float m = 1e18f;
  for (int i = 0; i < n; i += 4)
    for (int j = i + sep; j < n; j += 3) {
      if (n - (j - i) < sep) break;
      float dx = tp[j].x - tp[i].x, dy = tp[j].y - tp[i].y, d2 = dx * dx + dy * dy;
      if (d2 >= m) continue;
      int dij = j - i; if (dij > n / 2) dij = n - dij;
      if (dij * trk.ds < 2.0f * sqrtf(d2) + 30.0f) continue;
      m = d2;
    }
  return sqrtf(m);
}

static float maxAbsK() { float m = 0; for (int i = 0; i < trk.n; ++i) m = fmx(m, fabsf(tp[i].k)); return m; }

// ─── Public: build a track ────────────────────────────────────────────────────
static void finishTrack() {
  trk.hw = trk.W * 0.5f;
  computeRacingLine();
  computeSpeedProfile();
  computeCorners();
  computeRoadside();
  computePitsAndStands();
  Serial.printf("Track '%s': L %.0f px, %d samples, W %.1f, %d corners, %d stands, pits %d\n",
                trk.name, trk.L, trk.n, trk.W, trk.nCorners, trk.nStands, (int)trk.hasPits);
}

// A few preset corners are tighter than the road is wide, so the inner edge folds
// over itself and no line through them is smooth (cars all but stopped there).
// Round them off, a little at a time, until the road fits.
static void resampleEven() {           // even spacing again (everything else assumes trk.ds between samples)
  const int n = trk.n;
  float* bx = _tmpC; float* by = _tmpC + MAX_SAMPLES; float* acc = _tmpC + 2 * MAX_SAMPLES;
  for (int i = 0; i < n; ++i) { bx[i] = tp[i].x; by[i] = tp[i].y; }
  float L = 0;
  for (int i = 0; i < n; ++i) { int j = wrapI(i + 1); acc[i] = L; L += sqrtf((bx[j] - bx[i]) * (bx[j] - bx[i]) + (by[j] - by[i]) * (by[j] - by[i])); }
  trk.L = L; trk.ds = L / n;
  int j = 0;
  for (int i = 0; i < n; ++i) {
    const float s = i * trk.ds;
    while (j < n - 1 && acc[j + 1] < s) ++j;
    const int jn = wrapI(j + 1);
    const float segL = (j + 1 < n ? acc[j + 1] : L) - acc[j];
    const float t = segL > 1e-6f ? clampf((s - acc[j]) / segL, 0, 1) : 0;
    tp[i].x = bx[j] + (bx[jn] - bx[j]) * t;
    tp[i].y = by[j] + (by[jn] - by[j]) * t;
  }
}
static void roundTightCorners(float kMax) {
  const int n = trk.n, st = 3;
  float* bx = _tmpC; float* by = _tmpC + MAX_SAMPLES; float* kr = _tmpC + 2 * MAX_SAMPLES;
  for (int pass = 0; pass < 120; ++pass) {
    // raw bend over ~10 px of road: the smoothed curvature hides how sharp a kink is
    float worst = 0;
    for (int i = 0; i < n; ++i) {
      const TrackPt& a = tp[wrapI(i - 2)]; const TrackPt& m = tp[i]; const TrackPt& b = tp[wrapI(i + 2)];
      float h0 = atan2f(m.y - a.y, m.x - a.x), h1 = atan2f(b.y - m.y, b.x - m.x);
      kr[i] = wrapAngle(h1 - h0) / (2.0f * trk.ds);
      worst = fmx(worst, fabsf(kr[i]));
    }
    if (worst <= kMax) break;
    for (int i = 0; i < n; ++i) { bx[i] = tp[i].x; by[i] = tp[i].y; }
    for (int i = 0; i < n; ++i) {
      bool hot = false;
      for (int k = -3 * st; k <= 3 * st && !hot; ++k) hot = fabsf(kr[wrapI(i + k)]) > kMax;
      if (!hot) continue;
      const int a = wrapI(i - st), b = wrapI(i + st);
      tp[i].x = bx[i] * 0.5f + (bx[a] + bx[b]) * 0.25f;
      tp[i].y = by[i] * 0.5f + (by[a] + by[b]) * 0.25f;
    }
    resampleEven();
  }
  computeFrames();
}

static void buildPresetTrack(int p) {
  float px[PRESET_PTS], py[PRESET_PTS];
  for (int i = 0; i < PRESET_PTS; ++i) { px[i] = PRESETS[p].pts[i * 2]; py[i] = PRESETS[p].pts[i * 2 + 1]; }
  fitControl(px, py, PRESET_PTS, true);
  buildCenterline(px, py, PRESET_PTS);
  computeFrames();
  roundTightCorners(1.0f / (TRACK_W_DEF * 0.5f + RT_ROUND_M));
  float minClr = computeClearance();
  trk.W = clampf(minClr * 0.55f, 15.0f, TRACK_W_DEF);
  trk.preset = p; trk.seed = 0x5EED0000u + p;
  strncpy(trk.name, PRESETS[p].name, sizeof(trk.name) - 1);
  strncpy(trk.place, PRESETS[p].place, sizeof(trk.place) - 1);
  finishTrack();
}

static void buildRandomTrack(uint32_t seed) {
  float px[32], py[32]; int m = 0;
  uint32_t sd = seed;
  for (int attempt = 0; attempt < 40; ++attempt, sd = hash32(sd + 0x51u) | 1u) {
    genRandomControl(sd, px, py, m);
    for (int pass = 0; pass < 6; ++pass) {
      float cx[32], cy[32];
      for (int i = 0; i < m; ++i) { cx[i] = px[i]; cy[i] = py[i]; }
      fitControl(cx, cy, m, false);
      buildCenterline(cx, cy, m);
      computeFrames();
      if (maxAbsK() < 1.0f / 15.0f && quickMinClear() > TRACK_W_DEF * 1.9f + 4) {
        computeClearance();
        rotateStart(findStartIndex());           // (rotation keeps every per-sample value)
        trk.W = TRACK_W_DEF;
        trk.preset = -1; trk.seed = sd;
        snprintf(trk.name, sizeof(trk.name), "%s %s", RAND_NAME_A[sd % 16], RAND_NAME_B[(sd >> 8) % 6]);
        strncpy(trk.place, "Generated", sizeof(trk.place) - 1);
        finishTrack();
        return;
      }
      smoothControl(px, py, m, 0.3f);
    }
  }
  buildPresetTrack(seed % NUM_PRESETS);   // couldn't make a valid one (very unlikely)
}
