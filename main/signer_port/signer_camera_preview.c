#include "signer_camera_preview.h"

#include "core/settings.h"
#include "i18n/i18n.h"
#include "signer_qr_decoder.h"
#include "ui/i18n_text.h"
#include "ui/theme.h"

#include <bsp/esp-bsp.h>
#include <driver/ppa.h>
#include <esp_log.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <lvgl.h>
#include <stdbool.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../components/video/video.h"

static const char *TAG = "SIGNER_CAMERA";

static lv_obj_t *s_screen;
static lv_obj_t *s_camera_img;
static lv_obj_t *s_status_label;
static lv_img_dsc_t s_img_dsc;

static int s_camera_handle = -1;
static ppa_client_handle_t s_ppa_client;
static bool s_video_initialized;
static bool s_streaming;
static volatile bool s_closing;
static volatile int s_active_frame_ops;
static bool s_seen_frame;
static bool s_qr_detected;
static bool s_reveal_qr_payload;
static bool s_ppa_failure_logged;
static uint32_t s_frame_counter;

static uint8_t *s_display_buffer_a;
static uint8_t *s_display_buffer_b;
static uint8_t *s_current_display_buffer;
static uint32_t s_preview_size;
static size_t s_display_buffer_size;

static int min_i(int a, int b) { return a < b ? a : b; }
static int max_i(int a, int b) { return a > b ? a : b; }

typedef enum {
  QR_KIND_TEXT,
  QR_KIND_URL,
  QR_KIND_BITCOIN_URI,
  QR_KIND_UR,
  QR_KIND_BBQR,
  QR_KIND_PMOFN,
  QR_KIND_PSBT,
  QR_KIND_SEED,
  QR_KIND_BINARY,
} qr_payload_kind_t;

typedef struct {
  qr_payload_kind_t kind;
  const char *name;
  const char *action;
  bool sensitive;
} qr_payload_classification_t;

static char ascii_lower(char ch) {
  return (ch >= 'A' && ch <= 'Z') ? (char)(ch - 'A' + 'a') : ch;
}

static bool starts_with_ci(const char *text, const char *prefix) {
  if (!text || !prefix)
    return false;
  while (*prefix) {
    if (ascii_lower(*text++) != ascii_lower(*prefix++))
      return false;
  }
  return true;
}

static bool contains_ci(const char *text, const char *needle) {
  if (!text || !needle || !needle[0])
    return false;

  for (size_t i = 0; text[i]; i++) {
    size_t j = 0;
    while (needle[j] &&
           ascii_lower(text[i + j]) == ascii_lower(needle[j])) {
      j++;
    }
    if (!needle[j])
      return true;
  }
  return false;
}

static bool looks_like_pmofn(const char *text) {
  if (!text || (text[0] != 'p' && text[0] != 'P'))
    return false;

  size_t i = 1;
  if (text[i] < '0' || text[i] > '9')
    return false;
  while (text[i] >= '0' && text[i] <= '9')
    i++;

  if (ascii_lower(text[i]) != 'o' || ascii_lower(text[i + 1]) != 'f')
    return false;
  i += 2;

  if (text[i] < '0' || text[i] > '9')
    return false;
  while (text[i] >= '0' && text[i] <= '9')
    i++;

  return text[i] == ' ';
}

static bool looks_like_numeric_seed(const char *text, size_t len) {
  if (!text || len < 48 || len > 128)
    return false;

  for (size_t i = 0; i < len; i++) {
    if (text[i] < '0' || text[i] > '9')
      return false;
  }
  return true;
}

static qr_payload_classification_t classify_qr_payload(
    const signer_qr_decode_result_t *result) {
  qr_payload_classification_t cls = {
      .kind = QR_KIND_TEXT,
      .name = i18n_tr_or("camera.qr_plain_text", "Plain text QR"),
      .action = i18n_tr_or("camera.qr_plain_text_action",
                           "Text only. Wallet flow will not start."),
      .sensitive = false,
  };

  if (!result || !result->printable) {
    cls.kind = QR_KIND_BINARY;
    cls.name = i18n_tr_or("camera.qr_binary", "Binary QR");
    cls.action = i18n_tr_or("camera.qr_binary_action",
                            "Content hidden. Wallet import will not start.");
    cls.sensitive = true;
    return cls;
  }

  const char *text = result->text;
  if (starts_with_ci(text, "seed") || contains_ci(text, "seedqr") ||
      starts_with_ci(text, "ur:crypto-seed") ||
      looks_like_numeric_seed(text, result->payload_len)) {
    cls.kind = QR_KIND_SEED;
    cls.name = i18n_tr_or("camera.qr_possible_seed", "Possible seed QR");
    cls.action = i18n_tr_or("camera.qr_possible_seed_action",
                            "Use Load Mnemonic to import wallet seed data.");
    cls.sensitive = true;
  } else if (starts_with_ci(text, "ur:")) {
    cls.kind = QR_KIND_UR;
    cls.name = i18n_tr_or("camera.qr_ur", "UR/BC-UR QR");
    cls.action = i18n_tr_or("camera.qr_ur_action",
                            "May be multipart wallet data. Use Import or Scan Sign.");
    cls.sensitive = true;
  } else if (starts_with_ci(text, "B$") || starts_with_ci(text, "bbqr:")) {
    cls.kind = QR_KIND_BBQR;
    cls.name = i18n_tr_or("camera.qr_multipart", "Multipart QR");
    cls.action = i18n_tr_or("camera.qr_multipart_action",
                            "May be multipart wallet data. Use Import or Scan Sign.");
    cls.sensitive = true;
  } else if (looks_like_pmofn(text)) {
    cls.kind = QR_KIND_PMOFN;
    cls.name = i18n_tr_or("camera.qr_multipart", "Multipart QR");
    cls.action = i18n_tr_or("camera.qr_multipart_open_action",
                            "Open the matching wallet flow to decode all parts.");
    cls.sensitive = true;
  } else if (starts_with_ci(text, "psbt") || starts_with_ci(text, "cHNidP") ||
             contains_ci(text, "psbt") || contains_ci(text, "70736274ff")) {
    cls.kind = QR_KIND_PSBT;
    cls.name = i18n_tr_or("camera.qr_transaction_signing",
                          "Transaction signing QR");
    cls.action = i18n_tr_or("camera.qr_transaction_signing_action",
                            "Use Scan Sign to review and sign this request.");
    cls.sensitive = true;
  } else if (starts_with_ci(text, "bitcoin:") ||
             starts_with_ci(text, "lightning:")) {
    cls.kind = QR_KIND_BITCOIN_URI;
    cls.name = i18n_tr_or("camera.qr_payment", "Payment QR");
    cls.action = i18n_tr_or("camera.qr_payment_action",
                            "Showing public text only. Ownership is not checked.");
  } else if (starts_with_ci(text, "http://") ||
             starts_with_ci(text, "https://")) {
    cls.kind = QR_KIND_URL;
    cls.name = i18n_tr_or("camera.qr_url", "URL QR");
    cls.action = i18n_tr_or("camera.qr_url_action",
                            "Showing URL text only. No network request is made.");
  }

  return cls;
}

static void format_qr_status(char *buf, size_t buf_len,
                             const signer_qr_decode_result_t *result) {
  if (!buf || buf_len == 0)
    return;

  qr_payload_classification_t cls = classify_qr_payload(result);
  if (!result) {
    snprintf(buf, buf_len, "%s",
             i18n_tr_or("camera.qr_empty",
                        "QR detected, but the result is empty."));
    return;
  }

  if (!s_reveal_qr_payload || cls.sensitive) {
    snprintf(buf, buf_len,
             i18n_tr_or("camera.qr_detected_length_format",
                        "Detected: %s\nLength: %zu bytes\n%s"),
             cls.name, result->payload_len, cls.action);
    return;
  }

  snprintf(buf, buf_len,
           i18n_tr_or("camera.qr_detected_content_format",
                      "Detected: %s\n%s\nContent: %s"),
           cls.name, cls.action, result->text);
}

static uint8_t *allocate_preview_buffer(size_t size) {
  size_t aligned = (size + CONFIG_CACHE_L2_CACHE_LINE_SIZE - 1) &
                   ~(CONFIG_CACHE_L2_CACHE_LINE_SIZE - 1);
  uint8_t *buf = heap_caps_aligned_calloc(CONFIG_CACHE_L2_CACHE_LINE_SIZE,
                                          aligned, 1, MALLOC_CAP_SPIRAM);
  if (!buf)
    buf = heap_caps_aligned_calloc(CONFIG_CACHE_L2_CACHE_LINE_SIZE, aligned, 1,
                                   MALLOC_CAP_INTERNAL);
  return buf;
}

static void free_preview_buffers(void) {
  s_current_display_buffer = NULL;
  free(s_display_buffer_a);
  free(s_display_buffer_b);
  s_display_buffer_a = NULL;
  s_display_buffer_b = NULL;
  s_display_buffer_size = 0;
}

static uint32_t choose_preview_size(void) {
  int width = theme_get_screen_width();
  int height = theme_get_screen_height();
  int size = min_i(width, height);
  size = min_i(size, 480);
  size = max_i(size, 220);

  // The ESP32-P4 PPA scales in 1/16 steps.  The Waveshare camera normally
  // crops to 960x960, so choosing an exact 1/16 result avoids unwritten edges.
  if (size >= 320) {
    int frag = (size * 16) / 960;
    if (frag > 0)
      size = (960 * frag) / 16;
  }

  if (size & 1)
    size--;
  return (uint32_t)size;
}

static bool allocate_preview_buffers(void) {
  size_t pixel_size = (size_t)s_preview_size * s_preview_size * 2;
  s_display_buffer_size =
      (pixel_size + CONFIG_CACHE_L2_CACHE_LINE_SIZE - 1) &
      ~(CONFIG_CACHE_L2_CACHE_LINE_SIZE - 1);
  s_display_buffer_a = allocate_preview_buffer(s_display_buffer_size);
  s_display_buffer_b = allocate_preview_buffer(s_display_buffer_size);
  if (!s_display_buffer_a || !s_display_buffer_b) {
    free_preview_buffers();
    return false;
  }

  s_current_display_buffer = s_display_buffer_a;
  return true;
}

static bool ppa_crop_scale_rgb565(const uint8_t *src, uint8_t *dst,
                                  uint32_t src_w, uint32_t src_h,
                                  uint32_t dst_size) {
  if (!src || !dst || src_w == 0 || src_h == 0 || dst_size == 0)
    return false;

  uint32_t crop = src_w < src_h ? src_w : src_h;
  uint32_t crop_x = (src_w - crop) / 2;
  uint32_t crop_y = (src_h - crop) / 2;

  ppa_srm_oper_config_t srm = {
      .in.buffer = (void *)src,
      .in.pic_w = src_w,
      .in.pic_h = src_h,
      .in.block_w = crop,
      .in.block_h = crop,
      .in.block_offset_x = crop_x,
      .in.block_offset_y = crop_y,
      .in.srm_cm = PPA_SRM_COLOR_MODE_RGB565,
      .out.buffer = dst,
      .out.buffer_size = s_display_buffer_size,
      .out.pic_w = dst_size,
      .out.pic_h = dst_size,
      .out.block_offset_x = 0,
      .out.block_offset_y = 0,
      .out.srm_cm = PPA_SRM_COLOR_MODE_RGB565,
      .rotation_angle = PPA_SRM_ROTATION_ANGLE_0,
      .scale_x = (float)dst_size / (float)crop,
      .scale_y = (float)dst_size / (float)crop,
      .mode = PPA_TRANS_MODE_BLOCKING,
  };

  return s_ppa_client &&
         ppa_do_scale_rotate_mirror(s_ppa_client, &srm) == ESP_OK;
}

static void camera_frame_cb(uint8_t *camera_buf, uint8_t camera_buf_index,
                            uint32_t camera_buf_hes, uint32_t camera_buf_ves,
                            size_t camera_buf_len) {
  (void)camera_buf_index;
  (void)camera_buf_len;

  __atomic_add_fetch(&s_active_frame_ops, 1, __ATOMIC_SEQ_CST);

  if (s_closing || !s_streaming || !s_camera_img || !s_display_buffer_a ||
      !s_display_buffer_b || !s_current_display_buffer) {
    __atomic_sub_fetch(&s_active_frame_ops, 1, __ATOMIC_SEQ_CST);
    return;
  }

  uint8_t *back_buffer = (s_current_display_buffer == s_display_buffer_a)
                             ? s_display_buffer_b
                             : s_display_buffer_a;
  if (!ppa_crop_scale_rgb565(camera_buf, back_buffer, camera_buf_hes,
                             camera_buf_ves, s_preview_size)) {
    if (!s_ppa_failure_logged) {
      ESP_LOGE(TAG, "PPA crop/scale failed, frame=%" PRIu32 "x%" PRIu32,
               camera_buf_hes, camera_buf_ves);
      s_ppa_failure_logged = true;
    }
    __atomic_sub_fetch(&s_active_frame_ops, 1, __ATOMIC_SEQ_CST);
    return;
  }

  signer_qr_decode_result_t qr_result = {0};
  bool qr_checked = false;
  s_frame_counter++;
  if ((s_frame_counter % 30) == 0 && !s_qr_detected) {
    qr_checked = signer_qr_decode_rgb565(back_buffer, s_preview_size,
                                       s_preview_size, &qr_result);
  }

  if (!s_closing && bsp_display_lock(10)) {
    s_current_display_buffer = back_buffer;
    s_img_dsc.data = s_current_display_buffer;
    lv_img_set_src(s_camera_img, &s_img_dsc);

    if (qr_checked && qr_result.found && s_status_label) {
      char status[260];
      format_qr_status(status, sizeof(status), &qr_result);
      lv_label_set_text(s_status_label, status);
      lv_obj_set_style_text_color(s_status_label, yes_color(), 0);
      s_qr_detected = true;
      ESP_LOGI(TAG, "QR detected, len=%zu printable=%d",
               qr_result.payload_len, qr_result.printable);
    } else if (!s_seen_frame && s_status_label) {
      lv_label_set_text(s_status_label,
                        "Camera frame received. Checking plain QR safely.");
      lv_obj_set_style_text_color(s_status_label, yes_color(), 0);
      s_seen_frame = true;
      ESP_LOGI(TAG, "first camera frame received, source=%" PRIu32 "x%" PRIu32,
               camera_buf_hes, camera_buf_ves);
    }

    bsp_display_unlock();
  }

  __atomic_sub_fetch(&s_active_frame_ops, 1, __ATOMIC_SEQ_CST);
}

static void update_status(const char *text, lv_color_t color) {
  if (!s_status_label)
    return;
  lv_label_set_text(s_status_label, text ? text : "");
  lv_obj_set_style_text_color(s_status_label, color, 0);
}

static bool start_camera(void) {
  i2c_master_bus_handle_t i2c_handle = bsp_i2c_get_handle();
  if (!i2c_handle) {
    update_status("Start failed: I2C bus is not initialized.", error_color());
    return false;
  }

  if (app_video_main(i2c_handle) != ESP_OK) {
    update_status("Start failed: camera video subsystem init failed.",
                  error_color());
    ESP_LOGE(TAG, "video init failed");
    return false;
  }
  s_video_initialized = true;

  s_camera_handle = app_video_open((char *)CAM_DEV_PATH, APP_VIDEO_FMT_RGB565);
  if (s_camera_handle < 0) {
    update_status("Start failed: camera device did not open.", error_color());
    ESP_LOGE(TAG, "open camera device failed");
    return false;
  }

  if (app_video_register_frame_operation_cb(camera_frame_cb) != ESP_OK) {
    update_status("Start failed: camera callback was not registered.",
                  error_color());
    ESP_LOGE(TAG, "camera frame callback registration failed");
    return false;
  }

  if (!allocate_preview_buffers()) {
    update_status("Start failed: not enough memory for preview buffers.",
                  error_color());
    ESP_LOGE(TAG, "preview buffer allocation failed");
    return false;
  }

  s_img_dsc = (lv_img_dsc_t){
      .header = {.cf = LV_COLOR_FORMAT_RGB565,
                 .w = s_preview_size,
                 .h = s_preview_size},
      .data_size = (size_t)s_preview_size * s_preview_size * 2,
      .data = s_current_display_buffer,
  };
  lv_img_set_src(s_camera_img, &s_img_dsc);

  ppa_client_config_t ppa_cfg = {.oper_type = PPA_OPERATION_SRM};
  if (ppa_register_client(&ppa_cfg, &s_ppa_client) != ESP_OK) {
    s_ppa_client = NULL;
    update_status("Start failed: image scaling hardware did not start.",
                  error_color());
    ESP_LOGE(TAG, "PPA client registration failed");
    return false;
  }

  if (app_video_set_bufs(s_camera_handle, CAM_BUF_NUM, NULL) != ESP_OK) {
    update_status("Start failed: camera frame buffer setup failed.",
                  error_color());
    ESP_LOGE(TAG, "camera buffer setup failed");
    return false;
  }

  s_streaming = true;
  if (app_video_stream_task_start(s_camera_handle, 0) != ESP_OK) {
    s_streaming = false;
    update_status("Start failed: camera capture task did not start.",
                  error_color());
    ESP_LOGE(TAG, "camera stream task start failed");
    return false;
  }

  app_video_set_ae_target(s_camera_handle, settings_get_ae_target());
  if (app_video_has_focus_motor(s_camera_handle)) {
    app_video_set_focus(s_camera_handle, settings_get_focus_position());
  }
  ESP_LOGI(TAG, "camera preview started, preview=%" PRIu32 "x%" PRIu32,
           s_preview_size, s_preview_size);
  update_status("Waiting for the first camera frame...", highlight_color());
  return true;
}

static void stop_camera(void) {
  s_streaming = false;

  if (s_camera_handle >= 0) {
    (void)app_video_register_frame_operation_cb(NULL);
    (void)app_video_stream_task_stop(s_camera_handle);
  }

  int wait = 0;
  while (__atomic_load_n(&s_active_frame_ops, __ATOMIC_SEQ_CST) > 0 &&
         wait < 30) {
    vTaskDelay(pdMS_TO_TICKS(10));
    wait++;
  }

  if (s_camera_handle >= 0) {
    esp_err_t close_ret = app_video_close(s_camera_handle);
    if (close_ret == ESP_ERR_TIMEOUT) {
      ESP_LOGE(TAG, "Video close timeout; forced camera preview cleanup");
    }
    s_camera_handle = -1;
  }

  if (s_ppa_client) {
    ppa_unregister_client(s_ppa_client);
    s_ppa_client = NULL;
  }

  free_preview_buffers();

  if (s_video_initialized) {
    app_video_deinit();
    s_video_initialized = false;
  }

  s_seen_frame = false;
  s_active_frame_ops = 0;
  s_frame_counter = 0;
  s_qr_detected = false;
  s_ppa_failure_logged = false;
  signer_qr_decoder_deinit();
}

static lv_obj_t *create_label(lv_obj_t *parent, const char *text, bool medium,
                              lv_color_t color) {
  lv_obj_t *label = lv_label_create(parent);
  lv_label_set_text(label, ui_i18n_text(text));
  lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(label, LV_PCT(100));
  lv_obj_set_style_text_font(label, medium ? theme_font_medium()
                                           : theme_font_small(),
                             0);
  lv_obj_set_style_text_color(label, color, 0);
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
  return label;
}

static void close_event_cb(lv_event_t *event) {
  (void)event;
  signer_camera_preview_close();
}

bool signer_camera_preview_open_ex(const char *title, const char *notice,
                                 bool reveal_qr_payload) {
  if (s_screen) {
    if (s_streaming) {
      lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_HIDDEN);
      return true;
    }
    signer_camera_preview_close();
  }

  s_closing = false;
  s_streaming = false;
  s_seen_frame = false;
  s_qr_detected = false;
  s_reveal_qr_payload = reveal_qr_payload;
  s_ppa_failure_logged = false;
  s_frame_counter = 0;
  s_active_frame_ops = 0;
  s_preview_size = choose_preview_size();

  s_screen = lv_obj_create(lv_screen_active());
  lv_obj_set_size(s_screen, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_color(s_screen, bg_color(), 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(s_screen, 0, 0);
  lv_obj_set_style_radius(s_screen, 0, 0);
  lv_obj_set_style_pad_all(s_screen, theme_get_small_padding(), 0);
  lv_obj_set_style_pad_gap(s_screen, 8, 0);
  lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(s_screen, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(s_screen, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);

  create_label(s_screen,
               title ? title : i18n_tr_or("tools.camera_preview", "Camera Preview"),
               true,
               highlight_color());
  create_label(s_screen,
               notice ? notice
                      : i18n_tr_or("service.camera.summary",
                                   "Checks camera frames only. QR data is not imported."),
               false, secondary_color());

  lv_obj_t *frame = lv_obj_create(s_screen);
  lv_obj_set_size(frame, s_preview_size, s_preview_size);
  lv_obj_set_style_bg_color(frame, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(frame, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(frame, highlight_color(), 0);
  lv_obj_set_style_border_width(frame, 2, 0);
  lv_obj_set_style_radius(frame, 8, 0);
  lv_obj_set_style_pad_all(frame, 0, 0);
  lv_obj_clear_flag(frame, LV_OBJ_FLAG_SCROLLABLE);

  s_camera_img = lv_img_create(frame);
  lv_obj_set_size(s_camera_img, s_preview_size, s_preview_size);
  lv_obj_center(s_camera_img);

  s_status_label =
      create_label(s_screen, i18n_tr_or("tools.camera_running", "Starting camera..."),
                   false, highlight_color());

  lv_obj_t *close_btn =
      theme_create_button(s_screen, i18n_tr_or("common.back", "Back"), false);
  lv_obj_set_width(close_btn, LV_PCT(100));
  lv_obj_add_event_cb(close_btn, close_event_cb, LV_EVENT_CLICKED, NULL);

  bool ok = start_camera();
  if (!ok) {
    stop_camera();
    s_camera_img = NULL;
    s_status_label = NULL;
    if (s_screen) {
      lv_obj_del(s_screen);
      s_screen = NULL;
    }
  }
  return ok;
}

bool signer_camera_preview_open(const char *title, const char *notice) {
  return signer_camera_preview_open_ex(title, notice, false);
}

void signer_camera_preview_close(void) {
  if (!s_screen)
    return;

  s_closing = true;
  stop_camera();

  bool locked = bsp_display_lock(1000);
  s_camera_img = NULL;
  s_status_label = NULL;
  if (s_screen) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }
  if (locked)
    bsp_display_unlock();

  s_closing = false;
}

bool signer_camera_preview_is_open(void) { return s_screen != NULL; }
