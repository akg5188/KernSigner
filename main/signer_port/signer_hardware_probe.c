#include "signer_hardware_probe.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_system.h"
#include "lvgl.h"
#include "sd_card.h"
#include "i18n/i18n.h"

#ifdef __has_include
#if __has_include("bsp/esp-bsp.h")
#include "bsp/esp-bsp.h"
#endif
#endif

#ifndef BSP_CAPS_DISPLAY
#define BSP_CAPS_DISPLAY 0
#endif

#ifndef BSP_CAPS_TOUCH
#define BSP_CAPS_TOUCH 0
#endif

#ifndef BSP_CAPS_SDCARD
#define BSP_CAPS_SDCARD 0
#endif

void signer_hardware_probe_snapshot(signer_hardware_snapshot_t *out) {
  if (!out)
    return;

  memset(out, 0, sizeof(*out));
  out->screen_width = lv_disp_get_hor_res(NULL);
  out->screen_height = lv_disp_get_ver_res(NULL);
  out->free_heap = esp_get_free_heap_size();
  out->min_free_heap = esp_get_minimum_free_heap_size();
  out->free_spiram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  out->min_free_spiram = heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM);
  out->display_status = BSP_CAPS_DISPLAY
                            ? i18n_tr_or("hardware.supported", "Supported")
                            : i18n_tr_or("hardware.checkable", "Checkable");
  out->touch_status = BSP_CAPS_TOUCH
                          ? i18n_tr_or("hardware.supported", "Supported")
                          : i18n_tr_or("hardware.checkable", "Checkable");
  out->sd_capability = BSP_CAPS_SDCARD
                           ? i18n_tr_or("hardware.supported", "Supported")
                           : i18n_tr_or("hardware.checkable", "Checkable");

#if defined(CONFIG_KSIG_BOARD_WAVE_43)
  out->camera_status =
      i18n_tr_or("hardware.camera_check_page", "Open camera check");
  out->usb_status =
      i18n_tr_or("hardware.usb_maintenance_disconnect",
                 "Maintenance port; disconnect for normal use");
#else
  out->camera_status =
      i18n_tr_or("hardware.board_specific", "Board-specific");
  out->usb_status = i18n_tr_or("hardware.board_specific", "Board-specific");
#endif
}

void signer_hardware_probe_format_snapshot(char *buf, size_t buf_len) {
  if (!buf || buf_len == 0)
    return;

  signer_hardware_snapshot_t snap;
  signer_hardware_probe_snapshot(&snap);

  snprintf(buf, buf_len,
           i18n_tr_or("hardware.snapshot_format",
                      "Screen: %dx%d, display %s, touch %s\n"
                      "Internal heap: %lu KB available; low %lu KB\n"
                      "PSRAM: %zu KB available; low %zu KB\n"
                      "Storage card: %s\n"
                      "Camera: %s\n"
                      "USB: %s"),
           snap.screen_width, snap.screen_height, snap.display_status,
           snap.touch_status, (unsigned long)(snap.free_heap / 1024),
           (unsigned long)(snap.min_free_heap / 1024), snap.free_spiram / 1024,
           snap.min_free_spiram / 1024, snap.sd_capability,
           snap.camera_status, snap.usb_status);
}

esp_err_t signer_hardware_probe_storage_rw(char *detail, size_t detail_len) {
  if (detail && detail_len > 0)
    detail[0] = '\0';

  esp_err_t ret = sd_card_init();
  if (ret != ESP_OK) {
    if (detail && detail_len > 0) {
      snprintf(detail, detail_len,
               i18n_tr_or("hardware.storage_mount_failed_format",
                          "Mount failed: %s. Check the card slot, power, and "
                          "partition format."),
               esp_err_to_name(ret));
    }
    return ret;
  }

  static const char payload[] = "signer-storage-check\n";
  const char *path = SD_CARD_MOUNT_POINT "/signer_check.tmp";

  ret = sd_card_write_file(path, (const uint8_t *)payload, strlen(payload));
  if (ret != ESP_OK) {
    if (detail && detail_len > 0)
      snprintf(detail, detail_len,
               i18n_tr_or("hardware.storage_write_failed_format",
                          "Write failed: %s"),
               esp_err_to_name(ret));
    return ret;
  }

  bool exists = false;
  ret = sd_card_file_exists(path, &exists);
  if (ret != ESP_OK || !exists) {
    if (detail && detail_len > 0) {
      snprintf(detail, detail_len,
               i18n_tr_or("hardware.storage_temp_missing_format",
                          "Temporary file missing after write: %s"),
               esp_err_to_name(ret));
    }
    return ret == ESP_OK ? ESP_FAIL : ret;
  }

  uint8_t *readback = NULL;
  size_t readback_len = 0;
  ret = sd_card_read_file(path, &readback, &readback_len);
  if (ret != ESP_OK) {
    if (detail && detail_len > 0)
      snprintf(detail, detail_len,
               i18n_tr_or("hardware.storage_read_failed_format",
                          "Read failed: %s"),
               esp_err_to_name(ret));
    (void)sd_card_delete_file(path);
    return ret;
  }

  bool same = readback_len == strlen(payload) &&
              memcmp(readback, payload, strlen(payload)) == 0;
  free(readback);
  (void)sd_card_delete_file(path);

  if (!same) {
    if (detail && detail_len > 0)
      snprintf(detail, detail_len,
               "%s",
               i18n_tr_or("hardware.storage_verify_mismatch",
                          "Readback data did not match; storage is not "
                          "reliable."));
    return ESP_FAIL;
  }

  if (detail && detail_len > 0) {
    snprintf(detail, detail_len,
             i18n_tr_or("hardware.storage_rw_passed_format",
                        "Passed: mounted, wrote, read, verified, and deleted "
                        "%s."),
             path);
  }
  return ESP_OK;
}
