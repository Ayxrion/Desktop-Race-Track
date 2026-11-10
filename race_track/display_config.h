#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
//  LGFX — Elecrow CrowPanel 7.0" (16-bit parallel RGB, GT911 touch)
//  Same pin map/timings as the original Desktop Aquarium.
//  Requires the FIXED LovyanGFX 1.2.21 (patched esp32s3/Bus_RGB.cpp and
//  Panel_RGB.cpp — copies live in ../lovyangfx-fix/).
// ═══════════════════════════════════════════════════════════════════════════════

#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <lgfx/v1/platforms/esp32s3/Panel_RGB.hpp>
#include <lgfx/v1/platforms/esp32s3/Bus_RGB.hpp>

// Panel_RGB with read access to its framebuffer rows, so finished strips can be
// DMA-copied straight into the framebuffer (see presentStrip()).
class Panel_RGB_FB : public lgfx::Panel_RGB {
public:
  uint8_t* frameLine(int y) const { return _lines_buffer ? _lines_buffer[y] : nullptr; }
};

class LGFX : public lgfx::LGFX_Device {
  Panel_RGB_FB       _rgb_panel;
  lgfx::Bus_RGB      _rgb_bus;
  lgfx::Light_PWM    _rgb_light;
  lgfx::Touch_GT911  _rgb_touch;

public:
  uint8_t* frameLine(int y) const { return _rgb_panel.frameLine(y); }

  LGFX() {
    { // ── RGB parallel bus ─────────────────────────────────────────────────
      auto cfg = _rgb_bus.config();
      cfg.panel = &_rgb_panel;

      cfg.pin_d0  = GPIO_NUM_15; // B0
      cfg.pin_d1  = GPIO_NUM_7;  // B1
      cfg.pin_d2  = GPIO_NUM_6;  // B2
      cfg.pin_d3  = GPIO_NUM_5;  // B3
      cfg.pin_d4  = GPIO_NUM_4;  // B4
      cfg.pin_d5  = GPIO_NUM_9;  // G0
      cfg.pin_d6  = GPIO_NUM_46; // G1
      cfg.pin_d7  = GPIO_NUM_3;  // G2
      cfg.pin_d8  = GPIO_NUM_8;  // G3
      cfg.pin_d9  = GPIO_NUM_16; // G4
      cfg.pin_d10 = GPIO_NUM_1;  // G5
      cfg.pin_d11 = GPIO_NUM_14; // R0
      cfg.pin_d12 = GPIO_NUM_21; // R1
      cfg.pin_d13 = GPIO_NUM_47; // R2
      cfg.pin_d14 = GPIO_NUM_48; // R3
      cfg.pin_d15 = GPIO_NUM_45; // R4

      cfg.pin_henable = GPIO_NUM_41;
      cfg.pin_vsync   = GPIO_NUM_40;
      cfg.pin_hsync   = GPIO_NUM_39;
      cfg.pin_pclk    = GPIO_NUM_0;
      cfg.freq_write  = 12000000;

      cfg.hsync_polarity    = 0;
      cfg.hsync_front_porch = 40;
      cfg.hsync_pulse_width = 48;
      cfg.hsync_back_porch  = 40;
      cfg.vsync_polarity    = 0;
      cfg.vsync_front_porch = 1;
      cfg.vsync_pulse_width = 31;
      cfg.vsync_back_porch  = 13;
      cfg.pclk_active_neg   = 1;
      cfg.de_idle_high      = 0;
      cfg.pclk_idle_high    = 0;

      _rgb_bus.config(cfg);
    }
    { // ── Panel ─────────────────────────────────────────────────────────────
      auto cfg          = _rgb_panel.config();
      cfg.memory_width  = 800;
      cfg.memory_height = 480;
      cfg.panel_width   = 800;
      cfg.panel_height  = 480;
      cfg.offset_x      = 0;
      cfg.offset_y      = 0;
      _rgb_panel.config(cfg);

      auto cfg_detail        = _rgb_panel.config_detail();
      cfg_detail.use_psram   = 2;
      _rgb_panel.config_detail(cfg_detail);
    }
    { // ── Backlight (GPIO2) ─────────────────────────────────────────────────
      auto cfg        = _rgb_light.config();
      cfg.pin_bl      = GPIO_NUM_2;
      cfg.invert      = false;
      cfg.freq        = 44100;
      cfg.pwm_channel = 7;
      _rgb_light.config(cfg);
      _rgb_panel.setLight(&_rgb_light);
    }
    { // ── Touch — GT911 via I²C (up to 5 points; we use 2 for pinch) ────────
      auto cfg             = _rgb_touch.config();
      cfg.x_min            = 0;
      cfg.x_max            = 799;
      cfg.y_min            = 0;
      cfg.y_max            = 479;
      cfg.pin_int          = -1;
      cfg.pin_rst          = -1;
      cfg.bus_shared       = false;
      cfg.offset_rotation  = 0;
      cfg.i2c_port         = 1;
      cfg.i2c_addr         = 0x14;
      cfg.pin_sda          = GPIO_NUM_19;
      cfg.pin_scl          = GPIO_NUM_20;
      cfg.freq             = 400000;
      _rgb_touch.config(cfg);
      _rgb_panel.setTouch(&_rgb_touch);
    }
    _rgb_panel.setBus(&_rgb_bus);
    setPanel(&_rgb_panel);
  }
};
