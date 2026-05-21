// Wipe Flash Dialog — shared confirmation and erase flow

#include "wipe_flash_dialog.h"
#include "../../core/storage.h"
#include "../../ui/dialog.h"
#include <lvgl.h>
#include <stddef.h>

static void (*wipe_done_cb)(void) = NULL;
static lv_obj_t *wipe_progress = NULL;
static lv_timer_t *wipe_timer = NULL;

static void wipe_complete_cb(void *user_data) {
  (void)user_data;
  if (wipe_done_cb)
    wipe_done_cb();
}

static void deferred_wipe_cb(lv_timer_t *timer) {
  (void)timer;
  wipe_timer = NULL;
  esp_err_t ret = storage_wipe_flash();

  if (wipe_progress) {
    lv_obj_del(wipe_progress);
    wipe_progress = NULL;
  }

  if (ret == ESP_OK) {
    dialog_show_info("已清空", "闪存存储已擦除", wipe_complete_cb, NULL,
                     DIALOG_STYLE_OVERLAY);
  } else {
    dialog_show_error("清空闪存失败", NULL, 0);
  }
}

static void wipe_flash_confirm_cb(bool confirmed, void *user_data) {
  (void)user_data;
  if (!confirmed)
    return;

  wipe_progress = dialog_show_progress("正在清空", "正在擦除闪存存储...",
                                       DIALOG_STYLE_OVERLAY);
  wipe_timer = lv_timer_create(deferred_wipe_cb, 50, NULL);
  lv_timer_set_repeat_count(wipe_timer, 1);
}

void wipe_flash_dialog_start(void (*complete_cb)(void)) {
  wipe_done_cb = complete_cb;
  dialog_show_danger_confirm(
      "闪存里的所有助记词和描述符都会被永久删除。\n是否继续？",
      wipe_flash_confirm_cb, NULL, DIALOG_STYLE_OVERLAY);
}

void wipe_flash_dialog_cleanup(void) {
  if (wipe_timer) {
    lv_timer_del(wipe_timer);
    wipe_timer = NULL;
  }
  if (wipe_progress) {
    lv_obj_del(wipe_progress);
    wipe_progress = NULL;
  }
  wipe_done_cb = NULL;
}
