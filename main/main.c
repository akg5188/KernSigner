#include "core/key.h"
#include "core/mnemonic_slots.h"
#include "core/pin.h"
#include "core/settings.h"
#include "core/session.h"
#include "core/wallet.h"
#include "i18n/i18n.h"
#include "pages/pin/pin_page.h"
#include "pages/signer_shell/signer_shell.h"
#include "smartcard/smartcard_transport.h"
#include "ui/theme.h"
#include <bsp/display.h>
#include <bsp/esp-bsp.h>
#include <bsp/pmic.h>
#include <bsp/radio.h>
#include <esp_err.h>
#include <esp_log.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <lvgl.h>
#include <nvs_flash.h>

static const char *TAG = "KSIG_MAIN";
static lv_obj_t *s_main_screen;

#if defined(CONFIG_BT_ENABLED) || defined(CONFIG_ESP_WIFI_ENABLED) ||          \
    defined(CONFIG_ESP_HOST_WIFI_ENABLED) || defined(CONFIG_ETH_ENABLED) ||    \
    defined(CONFIG_IEEE802154_ENABLED) || defined(CONFIG_ESP_COEX_ENABLED) ||  \
    defined(CONFIG_ESP_PHY_ENABLED) || defined(CONFIG_LWIP_ENABLE) ||          \
    defined(CONFIG_OPENTHREAD_ENABLED)
#error "KernSigner must be built without wireless or network stacks."
#endif

static void show_pin_gate(void);

#ifndef KSIG_SMARTCARD_BOOT_PROBE
#define KSIG_SMARTCARD_BOOT_PROBE 0
#endif

static void clear_sensitive_session(void) {
  mnemonic_slots_clear_all();
  wallet_unload();
  key_unload();
}

static void start_locked_poweroff_guard(void) {
  session_start_protected(0, PIN_DEFAULT_POWER_OFF_TIMEOUT_SEC);
}

static void restart_after_boot_failure(const char *reason) {
  ESP_LOGE(TAG, "Boot failed: %s", reason);
  vTaskDelay(pdMS_TO_TICKS(1500));
  esp_restart();
}

static void start_signer_shell(void) {
  if (!s_main_screen)
    s_main_screen = lv_screen_active();

  lv_obj_clean(s_main_screen);
  signer_shell_create(s_main_screen);

  uint16_t timeout = pin_get_session_timeout();
  if (timeout > 0)
    session_start_protected(timeout, PIN_DEFAULT_POWER_OFF_TIMEOUT_SEC);
}

static void pin_gate_complete(void) {
  pin_page_destroy();
  start_signer_shell();
}

static void pin_setup_cancelled(void) {
  pin_page_destroy();
  show_pin_gate();
}

static void show_pin_gate(void) {
  if (!s_main_screen)
    s_main_screen = lv_screen_active();

  lv_obj_clean(s_main_screen);
  if (pin_is_configured()) {
    pin_page_create(s_main_screen, PIN_PAGE_UNLOCK, pin_gate_complete, NULL);
  } else {
    pin_page_create(s_main_screen, PIN_PAGE_SETUP, pin_gate_complete,
                    pin_setup_cancelled);
  }
}

static void session_expired_handler(void) {
  clear_sensitive_session();
  show_pin_gate();
}

static void session_poweroff_handler(void) {
  ESP_LOGW(TAG, "Inactivity power-off protection requested");
  clear_sensitive_session();
  (void)bsp_pmic_init();
  if (bsp_pmic_power_off() == ESP_OK)
    return;
  esp_restart();
}

#if KSIG_SMARTCARD_BOOT_PROBE
static void smartcard_boot_probe_task(void *arg) {
  (void)arg;

  vTaskDelay(pdMS_TO_TICKS(4500));
  ESP_LOGI(TAG, "SMARTCARD_BOOT_PROBE: begin");
  esp_err_t ret = smartcard_transport_probe(20000);

  char report[768];
  smartcard_transport_format_report(report, sizeof(report));
  ESP_LOGI(TAG, "SMARTCARD_BOOT_PROBE: result=%s", esp_err_to_name(ret));
  ESP_LOGI(TAG, "SMARTCARD_BOOT_PROBE_REPORT_BEGIN\n%s\nSMARTCARD_BOOT_PROBE_REPORT_END",
           report);

  vTaskDelete(NULL);
}
#endif

void app_main(void) {
  ESP_ERROR_CHECK(bsp_wireless_disable());

  // Initialize NVS for persistent settings
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    nvs_flash_erase();
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);
  ESP_ERROR_CHECK(settings_init());
  i18n_set_language(settings_get_language());
  (void)settings_set_permissive_signing(false);
  ESP_ERROR_CHECK(pin_init());
  session_set_expired_callback(session_expired_handler);
  session_set_poweroff_callback(session_poweroff_handler);

  lv_display_t *disp = bsp_display_start();
  if (!disp) {
    restart_after_boot_failure("display init failed");
  }
  ESP_LOGI(TAG, "Display initialized successfully");

  // Paint screen black early to overwrite stale framebuffer on warm reset.
  if (!bsp_display_lock(0)) {
    restart_after_boot_failure("display lock failed before splash clear");
  }
  lv_obj_t *screen = lv_screen_active();
  s_main_screen = screen;
  lv_obj_set_style_bg_color(screen, bg_color(), 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
  lv_obj_invalidate(screen);
  lv_refr_now(NULL);
  bsp_display_unlock();

  // Initialize PMIC (AXP2101 on wave_35; no-op on wave_4b)
  esp_err_t pmic_ret = bsp_pmic_init();
  if (pmic_ret == ESP_OK) {
    ESP_LOGI(TAG, "PMIC initialized");
  } else if (pmic_ret != ESP_ERR_NOT_SUPPORTED) {
    ESP_LOGW(TAG, "PMIC init failed: %s", esp_err_to_name(pmic_ret));
  }

  theme_init();
  if (!bsp_display_lock(0)) {
    restart_after_boot_failure("display lock failed before UI start");
  }

  // Set up screen theme background
  theme_apply_screen(screen);
  lv_refr_now(NULL);
  bsp_display_unlock();
  vTaskDelay(pdMS_TO_TICKS(50));

  esp_err_t brightness_ret = bsp_display_brightness_set(settings_get_brightness());
  if (brightness_ret != ESP_OK) {
    ESP_LOGW(TAG, "Brightness set failed: %s", esp_err_to_name(brightness_ret));
  }

  // Require PIN at boot. If no PIN exists yet, force PIN setup before the main
  // wallet shell is reachable.
  if (!bsp_display_lock(0)) {
    restart_after_boot_failure("display lock failed before PIN gate");
  }
  show_pin_gate();
  start_locked_poweroff_guard();
  bsp_display_unlock();

#if KSIG_SMARTCARD_BOOT_PROBE
  BaseType_t probe_started =
      xTaskCreatePinnedToCore(smartcard_boot_probe_task,
                              "ksig_card_boot_probe", 6144, NULL, 3, NULL, 0);
  if (probe_started != pdPASS) {
    ESP_LOGW(TAG, "SMARTCARD_BOOT_PROBE: task create failed");
  }
#endif
}
