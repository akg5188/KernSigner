// Store Mnemonic Page — encrypt and save to flash or SD card

#include "store_mnemonic.h"
#include "../core/key.h"
#include "../core/storage.h"
#include "../qr/encoder.h"
#include "../ui/dialog.h"
#include "../ui/theme.h"
#include "../utils/secure_mem.h"
#include "shared/kef_encrypt_page.h"

#include <lvgl.h>
#include <stdlib.h>
#include <string.h>
#include <wally_core.h>

static lv_obj_t *main_screen = NULL;
static lv_obj_t *progress_dialog = NULL;
static lv_timer_t *save_timer = NULL;
static void (*return_callback)(void) = NULL;
static storage_location_t target_location;

/* Data to encrypt (compact SeedQR binary entropy) */
static unsigned char *compact_seedqr_data = NULL;
static size_t compact_seedqr_len = 0;

/* Pending save (valid between encrypt success and deferred save) */
static const uint8_t *pending_envelope = NULL;
static size_t pending_envelope_len = 0;
static const char *pending_id = NULL;

/* ---------- Navigation ---------- */

static void go_back(void) {
  if (return_callback)
    return_callback();
}

/* ---------- Save result handling ---------- */

static void save_success_dialog_cb(void *user_data) {
  (void)user_data;
  go_back();
}

static void do_save(void) {
  char saved_id[64] = {0};
  if (pending_id)
    snprintf(saved_id, sizeof(saved_id), "%s", pending_id);

  esp_err_t ret = storage_save_mnemonic(target_location, pending_id,
                                        pending_envelope, pending_envelope_len);

  pending_envelope = NULL;
  pending_envelope_len = 0;
  pending_id = NULL;

  if (progress_dialog) {
    lv_obj_del(progress_dialog);
    progress_dialog = NULL;
  }
  kef_encrypt_page_destroy();

  if (ret == ESP_OK) {
    const char *loc_name =
        (target_location == STORAGE_FLASH) ? "闪存" : "存储卡";
    char msg[128];
    snprintf(msg, sizeof(msg), "助记词已保存到%s，名称：%s", loc_name,
             saved_id);
    dialog_show_info("已保存", msg, save_success_dialog_cb, NULL,
                     DIALOG_STYLE_OVERLAY);
  } else {
    dialog_show_error("保存失败", go_back, 0);
  }
}

static void overwrite_confirm_cb(bool confirmed, void *user_data) {
  (void)user_data;
  if (confirmed) {
    do_save();
  } else {
    pending_envelope = NULL;
    pending_envelope_len = 0;
    pending_id = NULL;
    if (progress_dialog) {
      lv_obj_del(progress_dialog);
      progress_dialog = NULL;
    }
    kef_encrypt_page_destroy();
    go_back();
  }
}

static void deferred_save_cb(lv_timer_t *timer) {
  (void)timer;
  save_timer = NULL;

  if (storage_mnemonic_exists(target_location, pending_id)) {
    if (progress_dialog) {
      lv_obj_del(progress_dialog);
      progress_dialog = NULL;
    }
    dialog_show_danger_confirm(
        "同名备份已经存在，是否覆盖？",
        overwrite_confirm_cb, NULL, DIALOG_STYLE_OVERLAY);
    return;
  }

  do_save();
}

/* ---------- Encrypt callbacks ---------- */

static void encrypt_return_cb(void) {
  kef_encrypt_page_destroy();
  go_back();
}

static void encrypt_success_cb(const char *id, const uint8_t *envelope,
                               size_t len) {
  /* Envelope and ID remain valid until kef_encrypt_page_destroy() */
  pending_envelope = envelope;
  pending_envelope_len = len;
  pending_id = id;

  /* Show "Saving..." and defer the actual save so LVGL can render
     before the potentially-blocking storage call */
  progress_dialog =
      dialog_show_progress("加密备份", "正在保存...", DIALOG_STYLE_OVERLAY);
  save_timer = lv_timer_create(deferred_save_cb, 50, NULL);
  lv_timer_set_repeat_count(save_timer, 1);
}

/* ---------- Page lifecycle ---------- */

void store_mnemonic_page_create(lv_obj_t *parent, void (*return_cb)(void),
                                storage_location_t location) {
  if (!parent || !key_is_loaded())
    return;

  if (location == STORAGE_FLASH) {
    dialog_show_error("无状态模式不保存助记词到本机闪存", return_cb, 0);
    return;
  }

  if (!key_mnemonic_is_valid()) {
    dialog_show_error("临时助记词不能加密备份", return_cb, 0);
    return;
  }

  return_callback = return_cb;
  target_location = location;

  /* Get mnemonic and convert to compact SeedQR (binary entropy) */
  char *mnemonic = NULL;
  if (!key_get_mnemonic(&mnemonic) || !mnemonic) {
    dialog_show_error("读取助记词失败", return_cb, 0);
    return;
  }

  compact_seedqr_data =
      mnemonic_to_compact_seedqr(mnemonic, &compact_seedqr_len);

  SECURE_FREE_STRING(mnemonic);

  if (!compact_seedqr_data) {
    dialog_show_error("准备数据失败", return_cb, 0);
    return;
  }

  /* Create background screen */
  const char *title = "保存到存储卡";
  main_screen = theme_create_page_container(parent);
  lv_obj_t *title_label = lv_label_create(main_screen);
  lv_label_set_text(title_label, title);
  lv_obj_set_style_text_font(title_label, theme_font_medium(), 0);
  lv_obj_set_style_text_color(title_label, main_color(), 0);
  lv_obj_align(title_label, LV_ALIGN_CENTER, 0, 0);

  kef_encrypt_page_create(parent, encrypt_return_cb, encrypt_success_cb,
                          compact_seedqr_data, compact_seedqr_len, NULL);
}

void store_mnemonic_page_show(void) {
  if (main_screen)
    lv_obj_clear_flag(main_screen, LV_OBJ_FLAG_HIDDEN);
}

void store_mnemonic_page_hide(void) {
  if (main_screen)
    lv_obj_add_flag(main_screen, LV_OBJ_FLAG_HIDDEN);
}

void store_mnemonic_page_destroy(void) {
  if (save_timer) {
    lv_timer_del(save_timer);
    save_timer = NULL;
  }
  if (progress_dialog) {
    lv_obj_del(progress_dialog);
    progress_dialog = NULL;
  }

  kef_encrypt_page_destroy();

  pending_envelope = NULL;
  pending_envelope_len = 0;
  pending_id = NULL;

  SECURE_FREE_BUFFER(compact_seedqr_data, compact_seedqr_len);
  compact_seedqr_len = 0;

  if (main_screen) {
    lv_obj_del(main_screen);
    main_screen = NULL;
  }

  return_callback = NULL;
}
