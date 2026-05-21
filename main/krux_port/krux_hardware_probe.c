#include "krux_hardware_probe.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_system.h"
#include "lvgl.h"
#include "sd_card.h"

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

void krux_hardware_probe_snapshot(krux_hardware_snapshot_t *out) {
  if (!out)
    return;

  memset(out, 0, sizeof(*out));
  out->screen_width = lv_disp_get_hor_res(NULL);
  out->screen_height = lv_disp_get_ver_res(NULL);
  out->free_heap = esp_get_free_heap_size();
  out->min_free_heap = esp_get_minimum_free_heap_size();
  out->free_spiram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  out->min_free_spiram = heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM);
  out->display_status = BSP_CAPS_DISPLAY ? "已支持" : "可检测";
  out->touch_status = BSP_CAPS_TOUCH ? "已支持" : "可检测";
  out->sd_capability = BSP_CAPS_SDCARD ? "已支持" : "可检测";

#if defined(CONFIG_KERN_BOARD_WAVE_43)
  out->camera_status = "请进入相机检查";
  out->usb_status = "维护口，正式使用请断开";
#else
  out->camera_status = "按目标板适配";
  out->usb_status = "按目标板适配";
#endif
}

void krux_hardware_probe_format_snapshot(char *buf, size_t buf_len) {
  if (!buf || buf_len == 0)
    return;

  krux_hardware_snapshot_t snap;
  krux_hardware_probe_snapshot(&snap);

  snprintf(buf, buf_len,
           "屏幕：%dx%d，显示%s，触摸%s\n"
           "内部堆：%lu KB，可用；最低 %lu KB\n"
           "PSRAM：%zu KB，可用；最低 %zu KB\n"
           "存储卡：%s\n"
           "相机：%s\n"
           "USB：%s",
           snap.screen_width, snap.screen_height, snap.display_status,
           snap.touch_status, (unsigned long)(snap.free_heap / 1024),
           (unsigned long)(snap.min_free_heap / 1024), snap.free_spiram / 1024,
           snap.min_free_spiram / 1024, snap.sd_capability,
           snap.camera_status, snap.usb_status);
}

esp_err_t krux_hardware_probe_storage_rw(char *detail, size_t detail_len) {
  if (detail && detail_len > 0)
    detail[0] = '\0';

  esp_err_t ret = sd_card_init();
  if (ret != ESP_OK) {
    if (detail && detail_len > 0) {
      snprintf(detail, detail_len, "挂载失败：%s。请确认卡槽、供电和分区格式。",
               esp_err_to_name(ret));
    }
    return ret;
  }

  static const char payload[] = "kern-storage-check\n";
  const char *path = SD_CARD_MOUNT_POINT "/kern_check.tmp";

  ret = sd_card_write_file(path, (const uint8_t *)payload, strlen(payload));
  if (ret != ESP_OK) {
    if (detail && detail_len > 0)
      snprintf(detail, detail_len, "写入失败：%s", esp_err_to_name(ret));
    return ret;
  }

  bool exists = false;
  ret = sd_card_file_exists(path, &exists);
  if (ret != ESP_OK || !exists) {
    if (detail && detail_len > 0) {
      snprintf(detail, detail_len, "写入后找不到临时文件：%s",
               esp_err_to_name(ret));
    }
    return ret == ESP_OK ? ESP_FAIL : ret;
  }

  uint8_t *readback = NULL;
  size_t readback_len = 0;
  ret = sd_card_read_file(path, &readback, &readback_len);
  if (ret != ESP_OK) {
    if (detail && detail_len > 0)
      snprintf(detail, detail_len, "读取失败：%s", esp_err_to_name(ret));
    (void)sd_card_delete_file(path);
    return ret;
  }

  bool same = readback_len == strlen(payload) &&
              memcmp(readback, payload, strlen(payload)) == 0;
  free(readback);
  (void)sd_card_delete_file(path);

  if (!same) {
    if (detail && detail_len > 0)
      snprintf(detail, detail_len, "读回数据不一致，存储链路不可靠。");
    return ESP_FAIL;
  }

  if (detail && detail_len > 0) {
    snprintf(detail, detail_len,
             "通过：已挂载、写入、读取、校验并删除 %s。", path);
  }
  return ESP_OK;
}
