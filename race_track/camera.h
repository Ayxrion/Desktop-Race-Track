#pragma once
// World ↔ screen camera with zoom, pan inertia and animated zoom steps.

#include "common.h"

struct Camera {
  float x = WORLD_W * 0.5f, y = WORLD_H * 0.5f;   // world point at screen centre
  float zoom = ZOOM_MIN;
  float vx = 0, vy = 0;                           // pan inertia (world px / frame)
  bool  animZoom = false;                         // animating toward targetZoom
  float targetZoom = ZOOM_MIN;
  float anchorSX = SCREEN_CX, anchorSY = SCREEN_CY;
};
static Camera cam;

static inline float w2sx(float wx) { return (wx - cam.x) * cam.zoom + SCREEN_CX; }
static inline float w2sy(float wy) { return (wy - cam.y) * cam.zoom + SCREEN_CY; }
static inline float s2wx(float sx) { return (sx - SCREEN_CX) / cam.zoom + cam.x; }
static inline float s2wy(float sy) { return (sy - SCREEN_CY) / cam.zoom + cam.y; }

// True if a world-space circle touches the strip being drawn.
static inline bool worldVisible(float wx, float wy, float wr) {
  float sx = w2sx(wx), sy = w2sy(wy), r = wr * cam.zoom + 2;
  return sx + r >= 0 && sx - r < SCREEN_W && sy + r >= clipY0 && sy - r < clipY1;   // current strip
}

static void clampCamera() {
  cam.zoom = clampf(cam.zoom, ZOOM_MIN, ZOOM_MAX);
  float hw = SCREEN_CX / cam.zoom, hh = SCREEN_CY / cam.zoom;
  cam.x = (hw * 2 >= WORLD_W) ? WORLD_W * 0.5f : clampf(cam.x, hw, WORLD_W - hw);
  cam.y = (hh * 2 >= WORLD_H) ? WORLD_H * 0.5f : clampf(cam.y, hh, WORLD_H - hh);
}

// Change zoom keeping the world point under screen (sx, sy) fixed.
static void zoomAround(float newZoom, float sx, float sy) {
  float wx = s2wx(sx), wy = s2wy(sy);
  cam.zoom = clampf(newZoom, ZOOM_MIN, ZOOM_MAX);
  cam.x = wx - (sx - SCREEN_CX) / cam.zoom;
  cam.y = wy - (sy - SCREEN_CY) / cam.zoom;
  clampCamera();
}

// Zoom steps used by the +/− buttons.
static const float ZOOM_STEPS[] = { 0.5f, 0.75f, 1.0f, 1.5f, 2.0f, 2.5f };
static const int   NUM_ZOOM_STEPS = sizeof(ZOOM_STEPS) / sizeof(ZOOM_STEPS[0]);

static void startZoomStep(int dir) {
  float z = cam.animZoom ? cam.targetZoom : cam.zoom;
  float t = z;
  if (dir > 0) { for (int i = 0; i < NUM_ZOOM_STEPS; ++i) if (ZOOM_STEPS[i] > z + 0.01f) { t = ZOOM_STEPS[i]; break; } }
  else         { for (int i = NUM_ZOOM_STEPS - 1; i >= 0; --i) if (ZOOM_STEPS[i] < z - 0.01f) { t = ZOOM_STEPS[i]; break; } }
  cam.targetZoom = t;
  cam.anchorSX = SCREEN_CX; cam.anchorSY = SCREEN_CY;
  cam.animZoom = true;
}

// Per-frame: inertia + zoom animation.
static void updateCamera() {
  if (cam.animZoom) {
    float nz = lerpf(cam.zoom, cam.targetZoom, 0.2f);
    if (fabsf(nz - cam.targetZoom) < 0.004f) { nz = cam.targetZoom; cam.animZoom = false; }
    zoomAround(nz, cam.anchorSX, cam.anchorSY);
  }
  if (fabsf(cam.vx) > 0.02f || fabsf(cam.vy) > 0.02f) {
    cam.x += cam.vx; cam.y += cam.vy;
    cam.vx *= 0.90f; cam.vy *= 0.90f;
    clampCamera();
  } else {
    cam.vx = cam.vy = 0;
  }
}
