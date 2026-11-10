/*----------------------------------------------------------------------------/
  Lovyan GFX - Graphics library for embedded devices.

Original Source:
 https://github.com/lovyan03/LovyanGFX/

Licence:
 [FreeBSD](https://github.com/lovyan03/LovyanGFX/blob/master/license.txt)

Author:
 [lovyan03](https://twitter.com/lovyan03)

Contributors:
 [ciniml](https://github.com/ciniml)
 [mongonta0716](https://github.com/mongonta0716)
 [tobozo](https://github.com/tobozo)
/----------------------------------------------------------------------------*/
#if defined (ESP_PLATFORM)
#include <sdkconfig.h>
#if defined (CONFIG_IDF_TARGET_ESP32S3)
#if __has_include (<esp_lcd_panel_rgb.h>)
#include "Bus_RGB.hpp"

#include <esp_lcd_panel_rgb.h>
#include <esp_pm.h>
#include <esp_log.h>
#include <esp_rom_gpio.h>
#include <rom/gpio.h>
#include <hal/gdma_ll.h>
#include <hal/gpio_ll.h>
#include <hal/gpio_hal.h>
#include <hal/lcd_ll.h>
#include <hal/lcd_hal.h>
#if __has_include(<soc/lcd_periph.h>)
 #include <soc/lcd_periph.h>
 #define LGFX_LCD_RGB_SIG(idx) (&lcd_periph_rgb_signals.panels[(idx)])
 #define LGFX_LCD_I80_SIG(idx) (&lcd_periph_signals.panels[(idx)])
#elif __has_include(<hal/lcd_periph.h>)
 #include <hal/lcd_periph.h>
 #define LGFX_LCD_RGB_SIG(idx) (&soc_lcd_rgb_signals[(idx)])
 #define LGFX_LCD_I80_SIG(idx) (&soc_lcd_i80_signals[(idx)])
#endif
#include <soc/lcd_cam_reg.h>
#include <soc/lcd_cam_struct.h>
#if __has_include(<soc/gdma_channel.h>)
 #include <soc/gdma_channel.h>
#elif __has_include(<hal/gdma_channel.h>)
 #include <hal/gdma_channel.h>
#endif
#include <soc/gdma_reg.h>
#include <soc/gdma_struct.h>

#if __has_include (<esp_private/periph_ctrl.h>)
 #include <esp_private/periph_ctrl.h>
#else
 #include <driver/periph_ctrl.h>
#endif

#if defined ( ESP_IDF_VERSION_VAL )
 #if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 5, 0)
  #define LGFX_HAL_FUNC_SEL
 #endif

 #if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 4, 4)
  // SPI no longer accepts ESP_INTR_FLAG_SHARED in 5.4.4+, but LCD_CAM still
  // requires SHARED when the dummy i80 bus already holds the interrupt slot.
  #define LGFX_INTR_FLAGS ESP_INTR_FLAG_INTRDISABLED | ESP_INTR_FLAG_SHARED
 #else
  #define LGFX_INTR_FLAGS ESP_INTR_FLAG_INTRDISABLED | ESP_INTR_FLAG_SHARED
 #endif

#endif

namespace lgfx
{
 inline namespace v1
 {
//----------------------------------------------------------------------------

  static __attribute__ ((always_inline)) inline volatile uint32_t* reg(uint32_t addr) { return (volatile uint32_t *)ETS_UNCACHED_ADDR(addr); }

  static lcd_cam_dev_t* getDev(int port)
  {
    return &LCD_CAM;
  }

  void Bus_RGB::config(const config_t& cfg)
  {
    _cfg = cfg;
  }


  IRAM_ATTR void Bus_RGB::lcd_default_isr_handler(void *args)
  {
    Bus_RGB *me = (Bus_RGB*)args;
    auto dev = getDev(me->config().port);

    uint32_t intr_status = dev->lc_dma_int_st.val & 0x03;
    dev->lc_dma_int_clr.val = intr_status;
    if (intr_status & LCD_LL_EVENT_VSYNC_END) {
      GDMA.channel[me->_dma_ch].out.conf0.out_rst = 1;
      GDMA.channel[me->_dma_ch].out.conf0.out_rst = 0;
      GDMA.channel[me->_dma_ch].out.link.addr = (uintptr_t)&(me->_dmadesc_restart);
      GDMA.channel[me->_dma_ch].out.link.start = 1;

    // bool need_yield = false;
        // call user registered callback
        // if (rgb_panel->on_vsync) {
        //     if (rgb_panel->on_vsync(&rgb_panel->base, NULL, rgb_panel->user_ctx)) {
        //         need_yield = true;
        //     }
        // }

        // check whether to update the PCLK frequency, it should be safe to update the PCLK frequency in the VSYNC interrupt
        // lcd_rgb_panel_try_update_pclk(rgb_panel);

        // if (need_yield) {
        //     portYIELD_FROM_ISR();
        // }
    }
  }

  static void _gpio_pin_sig(uint32_t pin, uint32_t sig)
  {
    #if defined LGFX_HAL_FUNC_SEL
      gpio_hal_context_t gpio_hal = {
          .dev = GPIO_HAL_GET_HW(GPIO_PORT_0)
      };
      gpio_hal_func_sel(&gpio_hal, pin, PIN_FUNC_GPIO);
    #else
      gpio_hal_iomux_func_sel(GPIO_PIN_MUX_REG[pin], PIN_FUNC_GPIO);
    #endif
    gpio_set_direction((gpio_num_t)pin, GPIO_MODE_OUTPUT);
    esp_rom_gpio_connect_out_signal(pin, sig, false, false);
  }

  bool Bus_RGB::init(void)
  {
    // Use esp_lcd_new_rgb_panel (IDF native RGB driver) so that GDMA, clock
    // setup, and the VSYNC ISR are all handled by the IDF.  The original code
    // attempted to use a dummy i80 bus to bootstrap the peripheral and then
    // configured GDMA / LCD_CAM registers by hand, but that approach breaks
    // under IDF 5.5 due to API and interrupt-allocation changes.
    esp_lcd_rgb_panel_config_t rgb_cfg;
    memset(&rgb_cfg, 0, sizeof(rgb_cfg));

    rgb_cfg.clk_src                        = LCD_CLK_SRC_DEFAULT;
    rgb_cfg.timings.pclk_hz                = _cfg.freq_write;
    rgb_cfg.timings.h_res                  = (uint32_t)_cfg.panel->width();
    rgb_cfg.timings.v_res                  = (uint32_t)_cfg.panel->height();
    rgb_cfg.timings.hsync_pulse_width      = _cfg.hsync_pulse_width;
    rgb_cfg.timings.hsync_back_porch       = _cfg.hsync_back_porch;
    rgb_cfg.timings.hsync_front_porch      = _cfg.hsync_front_porch;
    rgb_cfg.timings.vsync_pulse_width      = _cfg.vsync_pulse_width;
    rgb_cfg.timings.vsync_back_porch       = _cfg.vsync_back_porch;
    rgb_cfg.timings.vsync_front_porch      = _cfg.vsync_front_porch;
    rgb_cfg.timings.flags.pclk_active_neg  = _cfg.pclk_active_neg;
    rgb_cfg.timings.flags.de_idle_high     = _cfg.de_idle_high;
    rgb_cfg.timings.flags.pclk_idle_high   = _cfg.pclk_idle_high;
    rgb_cfg.timings.flags.hsync_idle_low   = _cfg.hsync_polarity ? 0 : 1;
    rgb_cfg.timings.flags.vsync_idle_low   = _cfg.vsync_polarity ? 0 : 1;

    rgb_cfg.data_width           = 16;   // RGB565 parallel bus
    rgb_cfg.num_fbs              = 1;
    rgb_cfg.dma_burst_size       = 64;
    // Bounce buffers: IDF copies PSRAM→SRAM before DMA, ensuring D-cache
    // coherency.  Without this, CPU writes to the PSRAM frame buffer stay in
    // cache and DMA sends stale zeros to the panel.
    rgb_cfg.bounce_buffer_size_px = 10 * (uint32_t)_cfg.panel->width();

    rgb_cfg.hsync_gpio_num = (gpio_num_t)_cfg.pin_hsync;
    rgb_cfg.vsync_gpio_num = (gpio_num_t)_cfg.pin_vsync;
    rgb_cfg.de_gpio_num    = (gpio_num_t)_cfg.pin_henable;
    rgb_cfg.pclk_gpio_num  = (gpio_num_t)_cfg.pin_pclk;
    rgb_cfg.disp_gpio_num  = GPIO_NUM_NC;

    for (int i = 0; i < 16; ++i) {
      rgb_cfg.data_gpio_nums[i] = (gpio_num_t)_cfg.pin_data[i];
    }

    rgb_cfg.flags.fb_in_psram = 1;

    if (ESP_OK != esp_lcd_new_rgb_panel(&rgb_cfg, &_panel_handle)) {
      ESP_LOGE("Bus_RGB", "esp_lcd_new_rgb_panel failed");
      return false;
    }
    if (ESP_OK != esp_lcd_panel_reset(_panel_handle))  { return false; }
    if (ESP_OK != esp_lcd_panel_init(_panel_handle))   { return false; }


    // Obtain the PSRAM frame buffer that IDF allocated.
    void* fb = nullptr;
    esp_lcd_rgb_panel_get_frame_buffer(_panel_handle, 1, &fb);
    _frame_buffer = (uint8_t*)fb;

    if (_frame_buffer == nullptr) {
      ESP_LOGE("Bus_RGB", "Failed to get frame buffer");
      return false;
    }

    return true;
  }

  uint8_t* Bus_RGB::getDMABuffer(uint32_t length)
  {
    return _frame_buffer;
    // return _rgb_panel->fb;
  }

  void Bus_RGB::release(void)
  {
    if (_panel_handle) {
      esp_lcd_panel_del(_panel_handle);
      _panel_handle = nullptr;
    }
  }

//----------------------------------------------------------------------------
 }
}

#endif
#endif
#endif
