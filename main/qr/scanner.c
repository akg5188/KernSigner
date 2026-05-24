// QR Scanner

#include "scanner.h"
#include "../components/cUR/src/ur_decoder.h"
#include "../core/settings.h"
#include "../i18n/i18n.h"
#include "../ui/input_helpers.h"
#include "../ui/theme.h"
#include "../utils/memory_utils.h"
#include "parser.h"
#include <bsp/esp-bsp.h>
#include <driver/ppa.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/idf_additions.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <k_quirc.h>
#include <lvgl.h>
#include <zbar_qr.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef QR_PERF_DEBUG
#include <esp_timer.h>
#endif

// Camera preview is a square sized to the smaller display dimension, capped at
// 640px.  Sensor outputs 1280x960 (binning mode); we take a centered square
// crop and downscale with the PPA in a single pass.
//
// The ESP32-P4 PPA uses Q4.4 fixed-point scaling (fractional scale quantized
// to 1/16), so an arbitrary scale like 2/3 truncates to 10/16 = 0.625 and
// leaves the last rows/cols unwritten. We therefore quantize the scale down
// to the nearest 1/16 and derive the actual preview size from it, so the
// PPA output exactly fills the widget — no black edges.
//   wave_4b: crop 960, scale 10/16 -> 600x600 preview
//   wave_35: crop 640, scale  8/16 -> 320x320 preview
#define CAMERA_SCREEN_DIM_MIN                                                  \
  ((BSP_LCD_H_RES) < (BSP_LCD_V_RES) ? (BSP_LCD_H_RES) : (BSP_LCD_V_RES))
#define CAMERA_TARGET                                                          \
  ((CAMERA_SCREEN_DIM_MIN) < 640 ? (CAMERA_SCREEN_DIM_MIN) : 640)
#define CAMERA_INPUT_WIDTH 1280
#define CAMERA_INPUT_HEIGHT 960
#define CAMERA_INPUT_CROP                                                      \
  ((CAMERA_TARGET * 2 <= 960) ? (CAMERA_TARGET * 2) : 960)
// Largest Q4.4 scale <= target/crop, and the exact preview size it yields.
#define CAMERA_PPA_FRAG ((CAMERA_TARGET * 16) / CAMERA_INPUT_CROP)
#define CAMERA_SCREEN_SIZE ((CAMERA_INPUT_CROP * CAMERA_PPA_FRAG) / 16)
#define CAMERA_SCREEN_WIDTH CAMERA_SCREEN_SIZE
#define CAMERA_SCREEN_HEIGHT CAMERA_SCREEN_SIZE
// Decode from a larger square than the on-screen preview, but keep the common
// path light enough for animated QR sampling. 480px remains the speed-first
// profile; occasional rescue frames use tighter crops and/or 640px when no
// payload has been seen yet.
#define CAMERA_DECODE_SIZE 480
#define CAMERA_DECODE_WIDTH CAMERA_DECODE_SIZE
#define CAMERA_DECODE_HEIGHT CAMERA_DECODE_SIZE
#define CAMERA_DECODE_RESCUE_SIZE 640
#define CAMERA_DECODE_MAX_SIZE CAMERA_DECODE_RESCUE_SIZE
#define CAMERA_PREVIEW_FRAME_PERIOD 30
#define CAMERA_PREVIEW_AFTER_PAYLOAD_FRAME_PERIOD 60
#define QR_FRAME_QUEUE_SIZE 1
#define QR_DECODE_TASK_STACK_SIZE 12288
#define QR_DECODE_TASK_PRIORITY 5
#define QR_DOT_RETRY_DILATE_RADIUS_MAX 3
#define QR_SLOW_FALLBACK_PERIOD 12
#define QR_UR_SLOW_FALLBACK_PERIOD 5
#define QR_RESCUE_PROFILE_PERIOD 24
#define QR_INVERTED_FALLBACK_PERIOD 24
#define PROGRESS_BAR_HEIGHT 20
#define PROGRESS_FRAME_PADD 2
#define PROGRESS_BLOC_PAD 1
#define MAX_QR_PARTS 100
#define DISPLAY_LOCK_TIMEOUT_MS 100
#ifdef QR_PERF_DEBUG
#define FPS_LOG_INTERVAL_MS 2000
#endif
#define RGB565_RED_BITS 5
#define RGB565_GREEN_BITS 6
#define RGB565_BLUE_BITS 5
#define RGB565_RED_LEVELS (1 << RGB565_RED_BITS)
#define RGB565_GREEN_LEVELS (1 << RGB565_GREEN_BITS)
#define RGB565_BLUE_LEVELS (1 << RGB565_BLUE_BITS)

typedef enum {
  CAMERA_EVENT_TASK_RUN = BIT(0),
  CAMERA_EVENT_DELETE = BIT(1),
} camera_event_id_t;

typedef struct {
  uint8_t *frame_data;
  uint32_t width;
  uint32_t height;
  bool uses_decode_slot;
  bool rescue_profile;
} qr_frame_data_t;

typedef struct {
  uint32_t output_size;
  uint8_t crop_percent;
  bool smooth_downscale;
  bool rescue_profile;
} qr_decode_profile_t;

static const char *TAG = "QR_SCANNER";

static lv_obj_t *qr_scanner_screen = NULL;
static lv_obj_t *camera_img = NULL;
static lv_obj_t *progress_frame = NULL;
static lv_obj_t **progress_rectangles = NULL;
static int progress_rectangles_count = 0;
static lv_obj_t *ur_progress_bar = NULL;
static lv_obj_t *ur_progress_indicator = NULL;
static int ur_progress_bar_inner_width = 0;
static void (*return_callback)(void) = NULL;

static int camera_ctlr_handle = -1;
static lv_img_dsc_t img_refresh_dsc;
static bool video_system_initialized = false;
static EventGroupHandle_t camera_event_group = NULL;

static uint8_t *display_buffer_a = NULL;
static uint8_t *display_buffer_b = NULL;
static uint8_t *current_display_buffer = NULL;
static size_t display_buffer_size = 0;
static volatile bool buffer_swap_needed = false;
static uint8_t *decode_buffer = NULL;
static size_t decode_buffer_size = 0;
static volatile bool decode_frame_in_flight = false;
static uint8_t *qr_preprocess_buffer = NULL;
static size_t qr_preprocess_buffer_size = 0;
static uint32_t qr_decoder_width = 0;
static uint32_t qr_decoder_height = 0;
static uint32_t decode_profile_seq = 0;
static volatile bool qr_payload_seen = false;
static volatile uint32_t qr_decode_miss_streak = 0;

static k_quirc_t *qr_decoder = NULL;
static zbar_qr_decoder_t *qr_zbar_decoder = NULL;
static k_quirc_result_t *qr_decode_result = NULL;
static TaskHandle_t qr_decode_task_handle = NULL;
static bool qr_decode_task_with_caps = false;
static QueueHandle_t qr_frame_queue = NULL;
static SemaphoreHandle_t qr_task_done_sem = NULL;
static QRPartParser *qr_parser = NULL;
static int previously_parsed = -1;

// Direct RGB565-to-grayscale lookup table (64KB, initialized once)
static uint8_t *rgb565_gray_lut = NULL;

static volatile bool closing = false;
static volatile bool scan_completed = false;
static volatile bool is_fully_initialized = false;
static volatile bool destruction_in_progress = false;
static volatile bool preview_paused_for_payload = false;

// Camera settings overlay
static lv_obj_t *settings_overlay = NULL;
static lv_obj_t *ae_slider = NULL;
static lv_obj_t *focus_slider = NULL;
static lv_obj_t *scan_status_label = NULL;
static bool has_focus_motor = false;
static volatile bool settings_active = false;

// PPA does centered crop + downscale (1280x960 -> 640x640) in a single pass.
static ppa_client_handle_t cam_ppa_client = NULL;

static volatile int active_frame_operations = 0;
static lv_timer_t *completion_timer = NULL;
static bool video_stream_stopped_for_destroy = false;

#ifdef QR_PERF_DEBUG
typedef struct {
  volatile uint32_t camera_frames;
  volatile uint32_t decode_frames;
  volatile uint32_t qr_detections;
  volatile uint64_t total_decode_time_us;
  volatile uint64_t total_grayscale_time_us;
  volatile uint64_t total_quirc_time_us;
  int64_t last_log_time;
} qr_perf_metrics_t;

static qr_perf_metrics_t perf_metrics = {0};
static lv_obj_t *fps_label = NULL;
#endif

static void touch_event_cb(lv_event_t *e);
static void camera_video_frame_operation(uint8_t *camera_buf,
                                         uint8_t camera_buf_index,
                                         uint32_t camera_buf_hes,
                                         uint32_t camera_buf_ves,
                                         size_t camera_buf_len);
static bool allocate_display_buffers(uint32_t width, uint32_t height);
static bool allocate_decode_buffer(void);
static bool allocate_qr_preprocess_buffer(uint32_t width, uint32_t height);
static bool ensure_qr_preprocess_buffer(uint32_t width, uint32_t height);
static void free_display_buffers(void);
static void free_decode_buffer(void);
static void free_qr_preprocess_buffer(void);
static qr_decode_profile_t select_decode_profile(uint32_t frame_seq,
                                                 bool payload_seen,
                                                 uint32_t miss_streak);
static void rgb565_crop_to_grayscale(const uint8_t *rgb565_data,
                                     uint8_t *gray_data, uint32_t src_w,
                                     uint32_t src_h, uint32_t crop,
                                     uint32_t crop_ox, uint32_t crop_oy,
                                     uint32_t dst_size,
                                     bool smooth_downscale);
static void dilate_dark_grayscale(uint8_t *gray_data, uint8_t *scratch,
                                  uint32_t width, uint32_t height,
                                  uint32_t radius);
static bool normalize_grayscale_contrast(uint8_t *gray_data, uint32_t width,
                                         uint32_t height);

typedef struct {
  int capstones;
  int grids;
  uint32_t radius;
  int grid_size;
  int timing_bias;
  k_quirc_error_t last_error;
  uint32_t last_error_radius;
  int last_error_grid_size;
  int last_error_timing_bias;
  int decode_attempts;
} qr_scan_diag_t;

static bool process_quirc_results(qr_scan_diag_t *diag, uint32_t radius,
                                  const k_quirc_debug_info_t *debug);
static bool process_zbar_result(const uint8_t *gray_data, uint32_t width,
                                uint32_t height);
static bool should_run_slow_fallback(uint32_t frame_seq);
static bool qr_decoder_ensure_size(uint32_t width, uint32_t height);
static bool handle_decoded_qr_payload(const uint8_t *payload,
                                      int payload_len);
static void qr_decode_task(void *pvParameters);
static bool qr_decoder_init(uint32_t width, uint32_t height);
static void qr_decoder_cleanup(void);
static bool camera_run(void);
static void camera_init(void);
static void create_progress_indicators(int total_parts);
static void update_progress_indicator(int part_index);
static void cleanup_progress_indicators(void);
static void create_ur_progress_bar(void);
static void update_ur_progress_bar(double percent_complete);
static void cleanup_ur_progress_bar(void);
static void update_scan_status(const char *text);

#ifdef QR_PERF_DEBUG
static void log_perf_metrics(void);
static void reset_perf_metrics(void);

static void reset_perf_metrics(void) {
  memset((void *)&perf_metrics, 0, sizeof(perf_metrics));
  perf_metrics.last_log_time = esp_timer_get_time();
}

static void log_perf_metrics(void) {
  int64_t now = esp_timer_get_time();
  int64_t elapsed_us = now - perf_metrics.last_log_time;

  if (elapsed_us < (FPS_LOG_INTERVAL_MS * 1000)) {
    return;
  }

  float elapsed_sec = elapsed_us / 1000000.0f;
  float camera_fps = perf_metrics.camera_frames / elapsed_sec;
  float decode_fps = perf_metrics.decode_frames / elapsed_sec;
  float successes_per_sec = perf_metrics.qr_detections / elapsed_sec;

  float avg_decode_ms = 0;
  float avg_grayscale_ms = 0;
  float avg_quirc_ms = 0;

  if (perf_metrics.decode_frames > 0) {
    avg_decode_ms =
        (perf_metrics.total_decode_time_us / perf_metrics.decode_frames) /
        1000.0f;
    avg_grayscale_ms =
        (perf_metrics.total_grayscale_time_us / perf_metrics.decode_frames) /
        1000.0f;
    avg_quirc_ms =
        (perf_metrics.total_quirc_time_us / perf_metrics.decode_frames) /
        1000.0f;
  }

  ESP_LOGI(TAG,
           "PERF: cam=%.1f fps, decode/s=%.1f , successes/s=%.1f | "
           "avg: total=%.1fms (gray=%.1fms, quirc=%.1fms)",
           camera_fps, decode_fps, successes_per_sec, avg_decode_ms,
           avg_grayscale_ms, avg_quirc_ms);

  if (fps_label && bsp_display_lock(20)) {
    lv_label_set_text_fmt(fps_label, "Camera:%.0f Decode:%.0f", camera_fps,
                          decode_fps);
    bsp_display_unlock();
  }

  perf_metrics.camera_frames = 0;
  perf_metrics.decode_frames = 0;
  perf_metrics.qr_detections = 0;
  perf_metrics.total_decode_time_us = 0;
  perf_metrics.total_grayscale_time_us = 0;
  perf_metrics.total_quirc_time_us = 0;
  perf_metrics.last_log_time = now;
}
#endif

static void create_progress_indicators(int total_parts) {
  if (total_parts <= 1 || total_parts > MAX_QR_PARTS || !qr_scanner_screen) {
    return;
  }

  if (!bsp_display_lock(DISPLAY_LOCK_TIMEOUT_MS)) {
    return;
  }

  int progress_frame_width = lv_obj_get_width(qr_scanner_screen) * 80 / 100;
  int rect_width = progress_frame_width / total_parts;
  rect_width -= PROGRESS_BLOC_PAD;
  progress_frame_width = total_parts * rect_width + 1;
  progress_frame_width += 2 * PROGRESS_FRAME_PADD + 2;

  progress_frame = lv_obj_create(qr_scanner_screen);
  lv_obj_set_size(progress_frame, progress_frame_width, PROGRESS_BAR_HEIGHT);
  lv_obj_align(progress_frame, LV_ALIGN_BOTTOM_MID, 0, -10);
  theme_apply_frame(progress_frame);
  lv_obj_set_style_pad_all(progress_frame, 2, 0);

  progress_rectangles = malloc(total_parts * sizeof(lv_obj_t *));
  if (!progress_rectangles) {
    ESP_LOGE(TAG, "Failed to allocate progress rectangles array");
    lv_obj_del(progress_frame);
    progress_frame = NULL;
    bsp_display_unlock();
    return;
  }
  progress_rectangles_count = total_parts;

  lv_obj_update_layout(progress_frame);

  for (int i = 0; i < total_parts; i++) {
    progress_rectangles[i] = lv_obj_create(progress_frame);
    lv_obj_set_size(progress_rectangles[i], rect_width - PROGRESS_BLOC_PAD, 12);
    lv_obj_set_pos(progress_rectangles[i], i * rect_width, 0);
    theme_apply_solid_rectangle(progress_rectangles[i]);
  }

  bsp_display_unlock();
}

static void update_progress_indicator(int part_index) {
  if (!progress_rectangles || part_index < 0 ||
      part_index >= progress_rectangles_count) {
    return;
  }

  if (previously_parsed != part_index &&
      bsp_display_lock(DISPLAY_LOCK_TIMEOUT_MS)) {
    lv_obj_set_style_bg_color(progress_rectangles[part_index],
                              highlight_color(), 0);
    if (previously_parsed >= 0) {
      lv_obj_set_style_bg_color(progress_rectangles[previously_parsed],
                                main_color(), 0);
    }
    if (scan_status_label) {
      char text[64];
      snprintf(text, sizeof(text), "%d/%d", part_index + 1,
               progress_rectangles_count);
      lv_label_set_text(scan_status_label, text);
    }
    previously_parsed = part_index;
    bsp_display_unlock();
  }
}

static void cleanup_progress_indicators(void) {
  SAFE_FREE_STATIC(progress_rectangles);
  progress_rectangles_count = 0;
  progress_frame = NULL;
  previously_parsed = -1;
}

static void create_ur_progress_bar(void) {
  if (!qr_scanner_screen || ur_progress_bar)
    return;
  if (!bsp_display_lock(DISPLAY_LOCK_TIMEOUT_MS))
    return;

  int bar_width = lv_obj_get_width(qr_scanner_screen) * 80 / 100;
  int bar_height = PROGRESS_BAR_HEIGHT;
  ur_progress_bar_inner_width = bar_width - 4;

  ur_progress_bar = lv_obj_create(qr_scanner_screen);
  lv_obj_set_size(ur_progress_bar, bar_width, bar_height);
  lv_obj_align(ur_progress_bar, LV_ALIGN_BOTTOM_MID, 0, -10);
  theme_apply_frame(ur_progress_bar);
  lv_obj_set_style_pad_all(ur_progress_bar, 2, 0);

  ur_progress_indicator = lv_obj_create(ur_progress_bar);
  lv_obj_set_size(ur_progress_indicator, 0, 12);
  lv_obj_set_pos(ur_progress_indicator, 0, 0);
  theme_apply_solid_rectangle(ur_progress_indicator);
  lv_obj_set_style_bg_color(ur_progress_indicator, highlight_color(), 0);

  bsp_display_unlock();
}

static void update_ur_progress_bar(double percent_complete) {
  if (!ur_progress_bar || !ur_progress_indicator ||
      ur_progress_bar_inner_width <= 0)
    return;
  if (!bsp_display_lock(DISPLAY_LOCK_TIMEOUT_MS))
    return;

  int indicator_width = (int)(ur_progress_bar_inner_width * percent_complete);
  if (indicator_width < 0)
    indicator_width = 0;
  if (indicator_width > ur_progress_bar_inner_width)
    indicator_width = ur_progress_bar_inner_width;

  lv_obj_set_width(ur_progress_indicator, indicator_width);
  if (scan_status_label) {
    char text[64];
    snprintf(text, sizeof(text), "UR %.0f%%", percent_complete * 100.0);
    lv_label_set_text(scan_status_label, text);
  }
  bsp_display_unlock();
}

static void cleanup_ur_progress_bar(void) {
  ur_progress_bar = NULL;
  ur_progress_indicator = NULL;
  ur_progress_bar_inner_width = 0;
}

static void update_scan_status(const char *text) {
  if (!scan_status_label || !text)
    return;
  if (!bsp_display_lock(DISPLAY_LOCK_TIMEOUT_MS))
    return;
  lv_label_set_text(scan_status_label, text);
  bsp_display_unlock();
}

static void completion_timer_cb(lv_timer_t *timer) {
  if (scan_completed && return_callback && !closing &&
      !destruction_in_progress) {
    closing = true;
    lv_timer_del(completion_timer);
    completion_timer = NULL;

    if (camera_event_group)
      xEventGroupClearBits(camera_event_group, CAMERA_EVENT_TASK_RUN);

    vTaskDelay(pdMS_TO_TICKS(50));
    return_callback();
  }
}

static void touch_event_cb(lv_event_t *e) {
  if (closing || settings_overlay)
    return;
  closing = true;
  if (return_callback)
    return_callback();
}

// --- Camera settings overlay ---

static void destroy_settings_overlay(void) {
  if (!settings_overlay)
    return;

  // Save current values to NVS (invert focus slider back to hardware range)
  if (ae_slider)
    settings_set_ae_target((uint8_t)lv_slider_get_value(ae_slider));
  if (focus_slider)
    settings_set_focus_position(
        (uint16_t)(FOCUS_POSITION_MAX - lv_slider_get_value(focus_slider)));

  lv_obj_del(settings_overlay);
  settings_overlay = NULL;
  ae_slider = NULL;
  focus_slider = NULL;
  settings_active = false;
}

static void ae_slider_cb(lv_event_t *e) {
  int32_t val = lv_slider_get_value(lv_event_get_target(e));
  app_video_set_ae_target(camera_ctlr_handle, (uint32_t)val);
}

static void focus_slider_cb(lv_event_t *e) {
  int32_t val = lv_slider_get_value(lv_event_get_target(e));
  app_video_set_focus(camera_ctlr_handle, (uint32_t)(FOCUS_POSITION_MAX - val));
}

static void settings_close_cb(lv_event_t *e) { destroy_settings_overlay(); }

static void style_settings_slider(lv_obj_t *slider) {
  lv_obj_set_width(slider, LV_PCT(90));
  lv_obj_set_height(slider, 20);
  lv_obj_set_style_bg_color(slider, highlight_color(), LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(slider, highlight_color(), LV_PART_KNOB);
  lv_obj_set_style_bg_color(slider, panel_color(), LV_PART_MAIN);
  lv_obj_set_style_pad_all(slider, 8, LV_PART_KNOB);
  lv_obj_set_style_margin_bottom(slider, 20, 0);
}

static void create_settings_overlay(void) {
  if (settings_overlay)
    return;

  settings_active = true;

  // Full-screen blocker
  settings_overlay = lv_obj_create(qr_scanner_screen);
  lv_obj_remove_style_all(settings_overlay);
  lv_obj_set_size(settings_overlay, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_opa(settings_overlay, LV_OPA_TRANSP, 0);
  lv_obj_add_flag(settings_overlay, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(settings_overlay, LV_OBJ_FLAG_SCROLLABLE);

  // Bottom-aligned panel
  lv_obj_t *panel = lv_obj_create(settings_overlay);
  lv_obj_set_size(panel, LV_PCT(85), LV_SIZE_CONTENT);
  lv_obj_align(panel, LV_ALIGN_BOTTOM_MID, 0, -12);
  theme_apply_frame(panel);
  lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
  lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_ver(panel, 12, 0);
  lv_obj_set_style_pad_hor(panel, 12, 0);
  lv_obj_set_style_pad_row(panel, 10, 0);
  lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(panel, LV_OBJ_FLAG_OVERFLOW_VISIBLE);

  // Exposure label + slider
  lv_obj_t *ae_title = lv_label_create(panel);
  lv_label_set_text(ae_title, i18n_tr_or("camera.exposure", "Exposure"));
  lv_obj_set_style_text_font(ae_title, theme_font_small(), 0);
  lv_obj_set_style_text_color(ae_title, main_color(), 0);

  ae_slider = lv_slider_create(panel);
  lv_slider_set_range(ae_slider, AE_TARGET_MIN, AE_TARGET_MAX);
  lv_slider_set_value(ae_slider, settings_get_ae_target(), LV_ANIM_OFF);
  style_settings_slider(ae_slider);
  lv_obj_add_event_cb(ae_slider, ae_slider_cb, LV_EVENT_VALUE_CHANGED, NULL);

  // Focus label + slider (only if motor detected, inverted: left=near
  // right=far)
  if (has_focus_motor) {
    lv_obj_t *focus_title = lv_label_create(panel);
    lv_label_set_text(focus_title, i18n_tr_or("camera.focus", "Focus"));
    lv_obj_set_style_text_font(focus_title, theme_font_small(), 0);
    lv_obj_set_style_text_color(focus_title, main_color(), 0);

    focus_slider = lv_slider_create(panel);
    lv_slider_set_range(focus_slider, 0, FOCUS_POSITION_MAX);
    lv_slider_set_value(focus_slider,
                        FOCUS_POSITION_MAX - settings_get_focus_position(),
                        LV_ANIM_OFF);
    style_settings_slider(focus_slider);
    lv_obj_add_event_cb(focus_slider, focus_slider_cb, LV_EVENT_VALUE_CHANGED,
                        NULL);
  }

  // Close button
  lv_obj_t *close_btn =
      theme_create_button(panel, i18n_tr_or("dialog.close", "Close"), true);
  lv_obj_set_width(close_btn, LV_PCT(60));
  lv_obj_set_style_bg_opa(close_btn, LV_OPA_COVER, 0);
  lv_obj_set_style_margin_top(close_btn, 16, 0);
  lv_obj_add_event_cb(close_btn, settings_close_cb, LV_EVENT_CLICKED, NULL);
}

static void settings_btn_cb(lv_event_t *e) { create_settings_overlay(); }

static uint8_t *allocate_buffer_with_fallback(size_t size) {
  // PPA writes directly into these buffers, so they must be cache-line
  // aligned in size and base address.
  size_t aligned = (size + CONFIG_CACHE_L2_CACHE_LINE_SIZE - 1) &
                   ~(CONFIG_CACHE_L2_CACHE_LINE_SIZE - 1);
  uint8_t *buffer = heap_caps_aligned_calloc(CONFIG_CACHE_L2_CACHE_LINE_SIZE,
                                             aligned, 1, MALLOC_CAP_SPIRAM);
  if (!buffer) {
    buffer = heap_caps_aligned_calloc(CONFIG_CACHE_L2_CACHE_LINE_SIZE, aligned,
                                      1, MALLOC_CAP_INTERNAL);
  }
  return buffer;
}

static bool allocate_display_buffers(uint32_t width, uint32_t height) {
  display_buffer_size = width * height * 2;
  display_buffer_size =
      (display_buffer_size + CONFIG_CACHE_L2_CACHE_LINE_SIZE - 1) &
      ~(CONFIG_CACHE_L2_CACHE_LINE_SIZE - 1);

  display_buffer_a = allocate_buffer_with_fallback(display_buffer_size);
  if (!display_buffer_a) {
    ESP_LOGE(TAG, "Failed to allocate display buffer A");
    display_buffer_size = 0;
    return false;
  }

  display_buffer_b = allocate_buffer_with_fallback(display_buffer_size);
  if (!display_buffer_b) {
    ESP_LOGE(TAG, "Failed to allocate display buffer B");
    SAFE_FREE_STATIC(display_buffer_a);
    display_buffer_size = 0;
    return false;
  }

  return true;
}

static bool allocate_decode_buffer(void) {
  decode_buffer_size = CAMERA_DECODE_MAX_SIZE * CAMERA_DECODE_MAX_SIZE;
  decode_buffer_size =
      (decode_buffer_size + CONFIG_CACHE_L2_CACHE_LINE_SIZE - 1) &
      ~(CONFIG_CACHE_L2_CACHE_LINE_SIZE - 1);

  decode_buffer = allocate_buffer_with_fallback(decode_buffer_size);
  if (!decode_buffer) {
    ESP_LOGE(TAG, "Failed to allocate high-resolution decode buffer");
    decode_buffer_size = 0;
    return false;
  }
  return true;
}

static bool allocate_qr_preprocess_buffer(uint32_t width, uint32_t height) {
  qr_preprocess_buffer_size = width * height;
  qr_preprocess_buffer_size =
      (qr_preprocess_buffer_size + CONFIG_CACHE_L2_CACHE_LINE_SIZE - 1) &
      ~(CONFIG_CACHE_L2_CACHE_LINE_SIZE - 1);

  qr_preprocess_buffer = allocate_buffer_with_fallback(qr_preprocess_buffer_size);
  if (!qr_preprocess_buffer) {
    ESP_LOGW(TAG, "Failed to allocate QR dot-code preprocessing buffer");
    qr_preprocess_buffer_size = 0;
    return false;
  }
  return true;
}

static bool ensure_qr_preprocess_buffer(uint32_t width, uint32_t height) {
  if (width == 0 || height == 0)
    return false;

  size_t required = (size_t)width * height;
  required = (required + CONFIG_CACHE_L2_CACHE_LINE_SIZE - 1) &
             ~(CONFIG_CACHE_L2_CACHE_LINE_SIZE - 1);
  if (qr_preprocess_buffer && qr_preprocess_buffer_size >= required)
    return true;

  free_qr_preprocess_buffer();
  return allocate_qr_preprocess_buffer(width, height);
}

static void free_display_buffers(void) {
  current_display_buffer = NULL;
  SAFE_FREE_STATIC(display_buffer_a);
  SAFE_FREE_STATIC(display_buffer_b);
  display_buffer_size = 0;
}

static void free_decode_buffer(void) {
  __atomic_store_n(&decode_frame_in_flight, false, __ATOMIC_RELEASE);
  SAFE_FREE_STATIC(decode_buffer);
  decode_buffer_size = 0;
}

static void free_qr_preprocess_buffer(void) {
  SAFE_FREE_STATIC(qr_preprocess_buffer);
  qr_preprocess_buffer_size = 0;
}

static qr_decode_profile_t select_decode_profile(uint32_t frame_seq,
                                                 bool payload_seen,
                                                 uint32_t miss_streak) {
  qr_decode_profile_t profile = {
      .output_size = CAMERA_DECODE_SIZE,
      .crop_percent = 100,
      .smooth_downscale = false,
      .rescue_profile = false,
  };

  if (payload_seen)
    return profile;

  if (miss_streak >= 8) {
    profile.output_size = CAMERA_DECODE_RESCUE_SIZE;
    profile.crop_percent = 82;
    profile.smooth_downscale = true;
    profile.rescue_profile = true;
    return profile;
  }

  switch (frame_seq % QR_RESCUE_PROFILE_PERIOD) {
  case 5:
    profile.crop_percent = 88;
    profile.smooth_downscale = true;
    profile.rescue_profile = true;
    break;
  case 11:
    profile.crop_percent = 78;
    profile.smooth_downscale = true;
    profile.rescue_profile = true;
    break;
  case 17:
    profile.output_size = CAMERA_DECODE_RESCUE_SIZE;
    profile.smooth_downscale = true;
    profile.rescue_profile = true;
    break;
  case 23:
    profile.output_size = CAMERA_DECODE_RESCUE_SIZE;
    profile.crop_percent = 82;
    profile.smooth_downscale = true;
    profile.rescue_profile = true;
    break;
  default:
    break;
  }

  return profile;
}

static uint8_t rgb565_luma_fallback(uint16_t pixel) {
  uint8_t r5 = (pixel >> 11) & 0x1F;
  uint8_t g6 = (pixel >> 5) & 0x3F;
  uint8_t b5 = pixel & 0x1F;
  uint8_t r8 = (r5 * 255 + 15) / 31;
  uint8_t g8 = (g6 * 255 + 31) / 63;
  uint8_t b8 = (b5 * 255 + 15) / 31;
  return (uint8_t)((77 * r8 + 150 * g8 + 29 * b8) >> 8);
}

static void rgb565_crop_to_grayscale(const uint8_t *rgb565_data,
                                     uint8_t *gray_data, uint32_t src_w,
                                     uint32_t src_h, uint32_t crop,
                                     uint32_t crop_ox, uint32_t crop_oy,
                                     uint32_t dst_size,
                                     bool smooth_downscale) {
  if (!rgb565_data || !gray_data || src_w == 0 || src_h == 0 || crop == 0 ||
      dst_size == 0)
    return;
  if (dst_size > CAMERA_DECODE_MAX_SIZE)
    return;

  if (crop_ox >= src_w || crop_oy >= src_h)
    return;
  if (crop_ox + crop > src_w)
    crop = src_w - crop_ox;
  if (crop_oy + crop > src_h)
    crop = src_h - crop_oy;

  static uint16_t x_offsets[CAMERA_DECODE_MAX_SIZE];
  static uint16_t y_offsets[CAMERA_DECODE_MAX_SIZE];
  static uint32_t cached_src_w;
  static uint32_t cached_src_h;
  static uint32_t cached_crop;
  static uint32_t cached_crop_ox;
  static uint32_t cached_crop_oy;
  static uint32_t cached_dst_size;

  if (cached_src_w != src_w || cached_src_h != src_h ||
      cached_crop != crop || cached_crop_ox != crop_ox ||
      cached_crop_oy != crop_oy || cached_dst_size != dst_size) {
    for (uint32_t x = 0; x < dst_size; x++) {
      uint32_t sx = crop_ox + (x * crop) / dst_size;
      if (sx >= src_w)
        sx = src_w - 1;
      x_offsets[x] = (uint16_t)sx;
    }
    for (uint32_t y = 0; y < dst_size; y++) {
      uint32_t sy = crop_oy + (y * crop) / dst_size;
      if (sy >= src_h)
        sy = src_h - 1;
      y_offsets[y] = (uint16_t)sy;
    }
    cached_src_w = src_w;
    cached_src_h = src_h;
    cached_crop = crop;
    cached_crop_ox = crop_ox;
    cached_crop_oy = crop_oy;
    cached_dst_size = dst_size;
  }

  uint32_t crop_x_max = crop_ox + crop - 1;
  uint32_t crop_y_max = crop_oy + crop - 1;
  const uint16_t *pixels = (const uint16_t *)rgb565_data;
  for (uint32_t y = 0; y < dst_size; y++) {
    uint16_t sy = y_offsets[y];
    const uint16_t *src_row = pixels + sy * src_w;
    const uint16_t *src_row_next =
        pixels + ((sy + 1 <= crop_y_max) ? (sy + 1) : sy) * src_w;
    uint8_t *dst_row = gray_data + y * dst_size;
    for (uint32_t x = 0; x < dst_size; x++) {
      uint16_t sx = x_offsets[x];
      uint16_t pixel = src_row[sx];
      uint8_t gray = rgb565_gray_lut ? rgb565_gray_lut[pixel]
                                     : rgb565_luma_fallback(pixel);
      if (smooth_downscale) {
        uint16_t sx_next = (sx + 1 <= crop_x_max) ? (sx + 1) : sx;
        uint16_t p01 = src_row[sx_next];
        uint16_t p10 = src_row_next[sx];
        uint16_t p11 = src_row_next[sx_next];
        uint16_t sum = gray;
        sum += rgb565_gray_lut ? rgb565_gray_lut[p01]
                               : rgb565_luma_fallback(p01);
        sum += rgb565_gray_lut ? rgb565_gray_lut[p10]
                               : rgb565_luma_fallback(p10);
        sum += rgb565_gray_lut ? rgb565_gray_lut[p11]
                               : rgb565_luma_fallback(p11);
        gray = (uint8_t)((sum + 2) >> 2);
      }
      dst_row[x] = gray;
    }
  }
}

static void dilate_dark_grayscale(uint8_t *gray_data, uint8_t *scratch,
                                  uint32_t width, uint32_t height,
                                  uint32_t radius) {
  if (!gray_data || !scratch || width == 0 || height == 0 || radius == 0)
    return;

  for (uint32_t y = 0; y < height; y++) {
    const uint8_t *row = gray_data + y * width;
    uint8_t *out = scratch + y * width;
    for (uint32_t x = 0; x < width; x++) {
      uint32_t x0 = (x > radius) ? x - radius : 0;
      uint32_t x1 = x + radius;
      if (x1 >= width)
        x1 = width - 1;
      uint8_t min_gray = 255;
      for (uint32_t sx = x0; sx <= x1; sx++) {
        if (row[sx] < min_gray)
          min_gray = row[sx];
      }
      out[x] = min_gray;
    }
  }

  for (uint32_t y = 0; y < height; y++) {
    uint32_t y0 = (y > radius) ? y - radius : 0;
    uint32_t y1 = y + radius;
    if (y1 >= height)
      y1 = height - 1;
    uint8_t *out = gray_data + y * width;
    for (uint32_t x = 0; x < width; x++) {
      uint8_t min_gray = 255;
      for (uint32_t sy = y0; sy <= y1; sy++) {
        uint8_t value = scratch[sy * width + x];
        if (value < min_gray)
          min_gray = value;
      }
      out[x] = min_gray;
    }
  }
}

static bool normalize_grayscale_contrast(uint8_t *gray_data, uint32_t width,
                                         uint32_t height) {
  if (!gray_data || width == 0 || height == 0)
    return false;

  uint32_t histogram[256] = {0};
  uint32_t total = width * height;
  for (uint32_t i = 0; i < total; i++)
    histogram[gray_data[i]]++;

  uint32_t low_target = total / 100;
  uint32_t high_target = total - low_target;
  uint32_t cumulative = 0;
  uint8_t low = 0;
  uint8_t high = 255;

  for (uint32_t i = 0; i < 256; i++) {
    cumulative += histogram[i];
    if (cumulative >= low_target) {
      low = (uint8_t)i;
      break;
    }
  }

  cumulative = 0;
  for (uint32_t i = 0; i < 256; i++) {
    cumulative += histogram[i];
    if (cumulative >= high_target) {
      high = (uint8_t)i;
      break;
    }
  }

  if (high <= low + 24)
    return false;

  uint8_t lut[256];
  uint32_t range = high - low;
  for (uint32_t i = 0; i < 256; i++) {
    if (i <= low) {
      lut[i] = 0;
    } else if (i >= high) {
      lut[i] = 255;
    } else {
      lut[i] = (uint8_t)(((i - low) * 255 + (range / 2)) / range);
    }
  }

  for (uint32_t i = 0; i < total; i++)
    gray_data[i] = lut[gray_data[i]];

  return true;
}

static bool handle_decoded_qr_payload(const uint8_t *payload, int payload_len) {
  if (!payload || payload_len <= 0 || !qr_parser)
    return false;

#ifdef QR_PERF_DEBUG
  __atomic_add_fetch(&perf_metrics.qr_detections, 1, __ATOMIC_RELAXED);
#endif

  int part_index =
      qr_parser_parse_with_len(qr_parser, (const char *)payload, payload_len);

  if (part_index >= 0 || qr_parser->total == 1) {
    if (qr_parser->format > FORMAT_NONE)
      __atomic_store_n(&qr_payload_seen, true, __ATOMIC_RELEASE);
    if (qr_parser->format == FORMAT_UR || qr_parser->total > 1)
      preview_paused_for_payload = true;
    if (qr_parser->format != FORMAT_UR)
      update_scan_status("QR detected");
    if (qr_parser->format == FORMAT_PMOFN) {
      if (qr_parser->total > 1 && !progress_frame)
        create_progress_indicators(qr_parser->total);
      if (part_index >= 0 && qr_parser->total > 1)
        update_progress_indicator(part_index);
    } else if (qr_parser->format == FORMAT_UR && qr_parser->ur_decoder) {
      if (!ur_progress_bar)
        create_ur_progress_bar();
      double percent_complete = ur_decoder_estimated_percent_complete(
          (ur_decoder_t *)qr_parser->ur_decoder);
      update_ur_progress_bar(percent_complete);
    } else if (qr_parser->format == FORMAT_BBQR) {
      if (qr_parser->total > 1 && !progress_frame)
        create_progress_indicators(qr_parser->total);
      if (part_index >= 0 && qr_parser->total > 1)
        update_progress_indicator(part_index);
    } else if (qr_parser->format == FORMAT_RELAY ||
               qr_parser->format == FORMAT_TP_MULTI) {
      if (qr_parser->total > 1 && !progress_frame)
        create_progress_indicators(qr_parser->total);
      if (part_index >= 0 && qr_parser->total > 1)
        update_progress_indicator(part_index);
    }

    if (qr_parser_is_complete(qr_parser))
      scan_completed = true;
  } else {
    update_scan_status("QR detected");
  }

  return true;
}

static bool process_zbar_result(const uint8_t *gray_data, uint32_t width,
                                uint32_t height) {
  if (!qr_zbar_decoder || !qr_decode_result || !gray_data || width == 0 ||
      height == 0)
    return false;

  if (!zbar_qr_decode_grayscale(qr_zbar_decoder, gray_data, (int)width,
                                (int)height, qr_decode_result))
    return false;

  return handle_decoded_qr_payload(qr_decode_result->data.payload,
                                   qr_decode_result->data.payload_len);
}

static bool qr_decoder_ensure_size(uint32_t width, uint32_t height) {
  if (!qr_decoder || width == 0 || height == 0)
    return false;
  if (qr_decoder_width == width && qr_decoder_height == height)
    return true;
  if (k_quirc_resize(qr_decoder, (int)width, (int)height) < 0) {
    ESP_LOGW(TAG, "Failed to resize QR decoder to %" PRIu32 "x%" PRIu32,
             width, height);
    return false;
  }
  qr_decoder_width = width;
  qr_decoder_height = height;
  return true;
}

static bool should_run_slow_fallback(uint32_t frame_seq) {
  if (!qr_zbar_decoder || !qr_parser)
    return true;

  // Once an animated UR sequence has started, prefer sampling more fresh frames.
  // ZBar handles OKX-style round/dot QR well; quirc + dilation is more useful
  // before the format is known.
  if (qr_parser->format == FORMAT_UR)
    return false;

  if (qr_parser->format > FORMAT_NONE)
    return true;

  return (frame_seq % QR_SLOW_FALLBACK_PERIOD) == 0;
}

static bool process_quirc_results(qr_scan_diag_t *diag, uint32_t radius,
                                  const k_quirc_debug_info_t *debug) {
  if (!qr_decoder)
    return false;

  bool got_payload = false;
  if (!qr_decode_result)
    return false;

  int num_codes = k_quirc_count(qr_decoder);
  for (int i = 0; i < num_codes; i++) {
    if (closing || destruction_in_progress)
      break;

    k_quirc_error_t err = k_quirc_decode(qr_decoder, i, qr_decode_result);
    if (diag) {
      diag->decode_attempts++;
      if (err != K_QUIRC_SUCCESS) {
        diag->last_error = err;
        diag->last_error_radius = radius;
#ifdef K_QUIRC_DEBUG
        if (debug && i < debug->num_grids) {
          diag->last_error_grid_size = debug->grids[i].grid_size;
          diag->last_error_timing_bias = debug->grids[i].timing_bias;
        }
#else
        (void)debug;
#endif
      }
    }
    if (err != K_QUIRC_SUCCESS || !qr_decode_result->valid)
      continue;

    got_payload =
        handle_decoded_qr_payload(qr_decode_result->data.payload,
                                  qr_decode_result->data.payload_len);
    if (scan_completed)
      break;
  }

  return got_payload;
}

static void qr_decode_task(void *pvParameters) {
  qr_frame_data_t frame_data;

  while (true) {
    if (closing || destruction_in_progress)
      break;

#ifdef QR_PERF_DEBUG
    log_perf_metrics();
#endif

    if (xQueueReceive(qr_frame_queue, &frame_data, pdMS_TO_TICKS(100)) !=
        pdTRUE)
      continue;

    if (closing || destruction_in_progress) {
      if (frame_data.uses_decode_slot)
        __atomic_store_n(&decode_frame_in_flight, false, __ATOMIC_RELEASE);
      break;
    }

    // Skip decoding while settings panel is open (camera feed continues)
    if (settings_active) {
      if (frame_data.uses_decode_slot)
        __atomic_store_n(&decode_frame_in_flight, false, __ATOMIC_RELEASE);
      continue;
    }

#ifdef QR_PERF_DEBUG
    int64_t frame_start = esp_timer_get_time();
    int64_t gray_start = frame_start;
    int64_t gray_end = frame_start;
    int64_t quirc_start = frame_start;
    int64_t quirc_end = frame_start;
#endif

    static uint32_t dot_retry_frame = 0;
    static uint32_t decode_frame_seq = 0;
    uint32_t frame_seq = decode_frame_seq++;
    bool got_payload = false;
    bool run_slow_fallback =
        should_run_slow_fallback(frame_seq) || frame_data.rescue_profile;
    bool quirc_ready =
        run_slow_fallback &&
        qr_decoder_ensure_size(frame_data.width, frame_data.height);
    bool preprocess_ready =
        quirc_ready &&
        ensure_qr_preprocess_buffer(frame_data.width, frame_data.height);
    uint32_t retry_radius = 0;
    if (run_slow_fallback && preprocess_ready) {
      retry_radius =
          (dot_retry_frame++ % QR_DOT_RETRY_DILATE_RADIUS_MAX) + 1;
    }

    bool run_contrast_retry = frame_data.rescue_profile && quirc_ready;
    int pass_count = 1 + (retry_radius ? 1 : 0) +
                     (run_contrast_retry ? 1 : 0);
    for (int pass = 0; pass < pass_count && !got_payload; pass++) {
      bool contrast_retry =
          run_contrast_retry && pass == (pass_count - 1);
      uint32_t dot_radius =
          (!contrast_retry && pass > 0 && retry_radius) ? retry_radius : 0;

#ifdef QR_PERF_DEBUG
      gray_start = esp_timer_get_time();
#endif
      const uint8_t *zbar_buf = frame_data.frame_data;
      uint8_t *qr_buf = NULL;
      if (contrast_retry) {
        qr_buf = k_quirc_begin(qr_decoder, NULL, NULL);
        if (!qr_buf)
          break;
        memcpy(qr_buf, frame_data.frame_data,
               (size_t)frame_data.width * frame_data.height);
        if (!normalize_grayscale_contrast(qr_buf, frame_data.width,
                                          frame_data.height))
          continue;
        zbar_buf = qr_buf;
      } else if (dot_radius > 0 && preprocess_ready) {
        qr_buf = k_quirc_begin(qr_decoder, NULL, NULL);
        if (!qr_buf)
          break;
        memcpy(qr_buf, frame_data.frame_data,
               (size_t)frame_data.width * frame_data.height);
        dilate_dark_grayscale(qr_buf, qr_preprocess_buffer, frame_data.width,
                              frame_data.height, dot_radius);
        zbar_buf = qr_buf;
      } else if (dot_radius > 0) {
        break;
      }

      got_payload =
          process_zbar_result(zbar_buf, frame_data.width, frame_data.height);
      if (got_payload || closing || destruction_in_progress || scan_completed)
        break;

      if (!run_slow_fallback) {
        break;
      }
      if (!quirc_ready) {
        break;
      }

      if (dot_radius > 0) {
        if (got_payload || closing || destruction_in_progress ||
            scan_completed)
          break;
      }
      if (!qr_buf) {
        qr_buf = k_quirc_begin(qr_decoder, NULL, NULL);
        if (!qr_buf)
          break;
        memcpy(qr_buf, frame_data.frame_data,
               (size_t)frame_data.width * frame_data.height);
      }
#ifdef QR_PERF_DEBUG
      gray_end = esp_timer_get_time();
      quirc_start = esp_timer_get_time();
#endif
      bool find_inverted =
          pass == 0 &&
          (frame_data.rescue_profile ||
           ((frame_seq % QR_INVERTED_FALLBACK_PERIOD) == 0));
      k_quirc_end(qr_decoder, find_inverted);
#ifdef QR_PERF_DEBUG
      quirc_end = esp_timer_get_time();
#endif

      got_payload = process_quirc_results(NULL, dot_radius, NULL);
      if (closing || destruction_in_progress || scan_completed)
        break;
    }

#ifdef QR_PERF_DEBUG
      int64_t frame_end = esp_timer_get_time();
      __atomic_add_fetch(&perf_metrics.decode_frames, 1, __ATOMIC_RELAXED);
      __atomic_add_fetch(&perf_metrics.total_grayscale_time_us,
                         (gray_end - gray_start), __ATOMIC_RELAXED);
      __atomic_add_fetch(&perf_metrics.total_quirc_time_us,
                         (quirc_end - quirc_start), __ATOMIC_RELAXED);
      __atomic_add_fetch(&perf_metrics.total_decode_time_us,
                         (frame_end - frame_start), __ATOMIC_RELAXED);
#endif

    if (frame_data.uses_decode_slot)
      __atomic_store_n(&decode_frame_in_flight, false, __ATOMIC_RELEASE);

    if (got_payload) {
      __atomic_store_n(&qr_decode_miss_streak, 0, __ATOMIC_RELEASE);
    } else if (!closing && !destruction_in_progress && !scan_completed) {
      __atomic_add_fetch(&qr_decode_miss_streak, 1, __ATOMIC_RELAXED);
    }
  }

  if (qr_task_done_sem)
    xSemaphoreGive(qr_task_done_sem);
  vTaskSuspend(NULL);
}

static bool qr_decoder_init(uint32_t width, uint32_t height) {

  // Build direct RGB565->grayscale LUT (64KB) for single-lookup conversion.
  if (!rgb565_gray_lut) {
    rgb565_gray_lut = heap_caps_malloc(65536, MALLOC_CAP_SPIRAM);
    if (rgb565_gray_lut) {
      for (uint32_t i = 0; i < 65536; i++) {
        uint8_t r5 = (i >> 11) & 0x1F;
        uint8_t g6 = (i >> 5) & 0x3F;
        uint8_t b5 = i & 0x1F;
        // BT.601 luma with full 8-bit precision:
        // expand RGB565 to 8-bit, then Y = (77*R + 150*G + 29*B) >> 8
        uint8_t r8 = (r5 * 255 + 15) / 31;
        uint8_t g8 = (g6 * 255 + 31) / 63;
        uint8_t b8 = (b5 * 255 + 15) / 31;
        rgb565_gray_lut[i] = (uint8_t)((77 * r8 + 150 * g8 + 29 * b8) >> 8);
      }
    } else {
      ESP_LOGW(TAG, "Failed to allocate RGB565 grayscale LUT");
    }
  }

  qr_decoder = k_quirc_new();
  if (!qr_decoder) {
    ESP_LOGE(TAG, "Failed to create QR decoder");
    goto error;
  }

  qr_zbar_decoder = zbar_qr_decoder_create();
  if (!qr_zbar_decoder)
    ESP_LOGW(TAG, "Failed to create zbar QR fallback decoder");

  if (k_quirc_resize(qr_decoder, width, height) < 0) {
    ESP_LOGE(TAG, "Failed to resize QR decoder");
    goto error;
  }
  qr_decoder_width = width;
  qr_decoder_height = height;

  qr_decode_result =
      heap_caps_calloc(1, sizeof(*qr_decode_result),
                       MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!qr_decode_result)
    qr_decode_result = heap_caps_calloc(1, sizeof(*qr_decode_result),
                                        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!qr_decode_result) {
    ESP_LOGE(TAG, "Failed to allocate QR decode result buffer");
    goto error;
  }

  if (!qr_preprocess_buffer)
    (void)allocate_qr_preprocess_buffer(width, height);

  qr_frame_queue = xQueueCreate(QR_FRAME_QUEUE_SIZE, sizeof(qr_frame_data_t));
  if (!qr_frame_queue) {
    ESP_LOGE(TAG, "Failed to create QR frame queue");
    goto error;
  }

  qr_task_done_sem = xSemaphoreCreateBinary();
  if (!qr_task_done_sem) {
    ESP_LOGE(TAG, "Failed to create QR task done semaphore");
    goto error;
  }

  // Pin decode task to Core 1 to avoid competing with camera task on Core 0
  BaseType_t task_result = xTaskCreatePinnedToCore(
      qr_decode_task, "qr_decode", QR_DECODE_TASK_STACK_SIZE, NULL,
      QR_DECODE_TASK_PRIORITY, &qr_decode_task_handle, 1);
  qr_decode_task_with_caps = false;
  if (task_result != pdPASS) {
    ESP_LOGW(TAG,
             "QR decode task internal stack failed; internal=%u spiram=%u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    task_result = xTaskCreatePinnedToCoreWithCaps(
        qr_decode_task, "qr_decode", QR_DECODE_TASK_STACK_SIZE, NULL,
        QR_DECODE_TASK_PRIORITY, &qr_decode_task_handle, 1,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    qr_decode_task_with_caps = task_result == pdPASS;
  }
  if (task_result != pdPASS) {
    ESP_LOGE(TAG, "Failed to create QR decode task; internal=%u spiram=%u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    goto error;
  }

  qr_parser = qr_parser_create();
  if (!qr_parser) {
    ESP_LOGE(TAG, "Failed to create QR parser");
    goto error;
  }
  return true;

error:
  if (qr_parser) {
    qr_parser_destroy(qr_parser);
    qr_parser = NULL;
  }
  if (qr_decode_task_handle) {
    if (qr_decode_task_with_caps)
      vTaskDeleteWithCaps(qr_decode_task_handle);
    else
      vTaskDelete(qr_decode_task_handle);
    qr_decode_task_handle = NULL;
    qr_decode_task_with_caps = false;
  }
  if (qr_task_done_sem) {
    vSemaphoreDelete(qr_task_done_sem);
    qr_task_done_sem = NULL;
  }
  if (qr_frame_queue) {
    vQueueDelete(qr_frame_queue);
    qr_frame_queue = NULL;
  }
  if (qr_decoder) {
    k_quirc_destroy(qr_decoder);
    qr_decoder = NULL;
  }
  qr_decoder_width = 0;
  qr_decoder_height = 0;
  if (qr_zbar_decoder) {
    zbar_qr_decoder_destroy(qr_zbar_decoder);
    qr_zbar_decoder = NULL;
  }
  if (qr_decode_result) {
    heap_caps_free(qr_decode_result);
    qr_decode_result = NULL;
  }
  free_qr_preprocess_buffer();
  return false;
}

static void qr_decoder_cleanup(void) {
  closing = true;

  if (qr_decode_task_handle && qr_task_done_sem) {
    if (xSemaphoreTake(qr_task_done_sem, pdMS_TO_TICKS(500)) != pdTRUE)
      ESP_LOGW(TAG, "Timeout waiting for QR decode task");
    if (qr_decode_task_with_caps)
      vTaskDeleteWithCaps(qr_decode_task_handle);
    else
      vTaskDelete(qr_decode_task_handle);
    qr_decode_task_handle = NULL;
    qr_decode_task_with_caps = false;
  }

  if (qr_task_done_sem) {
    vSemaphoreDelete(qr_task_done_sem);
    qr_task_done_sem = NULL;
  }

  if (qr_frame_queue) {
    qr_frame_data_t frame_data;
    while (xQueueReceive(qr_frame_queue, &frame_data, 0) == pdTRUE) {
      if (frame_data.uses_decode_slot)
        __atomic_store_n(&decode_frame_in_flight, false, __ATOMIC_RELEASE);
    }
    vQueueDelete(qr_frame_queue);
    qr_frame_queue = NULL;
  }

  if (qr_decoder) {
    k_quirc_destroy(qr_decoder);
    qr_decoder = NULL;
  }
  qr_decoder_width = 0;
  qr_decoder_height = 0;
  if (qr_zbar_decoder) {
    zbar_qr_decoder_destroy(qr_zbar_decoder);
    qr_zbar_decoder = NULL;
  }
  if (qr_decode_result) {
    heap_caps_free(qr_decode_result);
    qr_decode_result = NULL;
  }
  free_qr_preprocess_buffer();

  if (qr_parser) {
    qr_parser_destroy(qr_parser);
    qr_parser = NULL;
  }

  if (rgb565_gray_lut) {
    heap_caps_free(rgb565_gray_lut);
    rgb565_gray_lut = NULL;
  }
}

static void camera_video_frame_operation(uint8_t *camera_buf,
                                         uint8_t camera_buf_index,
                                         uint32_t camera_buf_hes,
                                         uint32_t camera_buf_ves,
                                         size_t camera_buf_len) {
  __atomic_add_fetch(&active_frame_operations, 1, __ATOMIC_SEQ_CST);

  if (closing || destruction_in_progress || !is_fully_initialized ||
      !camera_event_group) {
    __atomic_sub_fetch(&active_frame_operations, 1, __ATOMIC_SEQ_CST);
    return;
  }

  EventBits_t current_bits = xEventGroupGetBits(camera_event_group);
  if (!(current_bits & CAMERA_EVENT_TASK_RUN) ||
      (current_bits & CAMERA_EVENT_DELETE)) {
    __atomic_sub_fetch(&active_frame_operations, 1, __ATOMIC_SEQ_CST);
    return;
  }

#ifdef QR_PERF_DEBUG
  __atomic_add_fetch(&perf_metrics.camera_frames, 1, __ATOMIC_RELAXED);
#endif

  if (!decode_buffer) {
    __atomic_sub_fetch(&active_frame_operations, 1, __ATOMIC_SEQ_CST);
    return;
  }

  if (camera_buf_hes == 0 || camera_buf_ves == 0) {
    __atomic_sub_fetch(&active_frame_operations, 1, __ATOMIC_SEQ_CST);
    return;
  }

  static bool resolution_mismatch_logged = false;
  if (!resolution_mismatch_logged && (camera_buf_hes != CAMERA_INPUT_WIDTH ||
                                      camera_buf_ves != CAMERA_INPUT_HEIGHT)) {
    ESP_LOGW(TAG,
             "Camera resolution %" PRIu32 "x%" PRIu32
             " differs from expected %dx%d; cropping dynamically",
             camera_buf_hes, camera_buf_ves, CAMERA_INPUT_WIDTH,
             CAMERA_INPUT_HEIGHT);
    resolution_mismatch_logged = true;
  }

  uint32_t in_w = camera_buf_hes;
  uint32_t in_h = camera_buf_ves;
  uint32_t preview_crop = (in_w < in_h) ? in_w : in_h;
  if (preview_crop > CAMERA_INPUT_CROP) {
    preview_crop = CAMERA_INPUT_CROP;
  }
  uint32_t crop_ox = (in_w - preview_crop) / 2;
  uint32_t crop_oy = (in_h - preview_crop) / 2;

  bool decode_ready = false;
  qr_decode_profile_t decode_profile = {
      .output_size = CAMERA_DECODE_SIZE,
      .crop_percent = 100,
      .smooth_downscale = false,
      .rescue_profile = false,
  };
  if (qr_frame_queue && !settings_active && !closing &&
      !__atomic_load_n(&decode_frame_in_flight, __ATOMIC_ACQUIRE)) {
    __atomic_store_n(&decode_frame_in_flight, true, __ATOMIC_RELEASE);
    bool payload_seen =
        __atomic_load_n(&qr_payload_seen, __ATOMIC_ACQUIRE);
    uint32_t miss_streak =
        __atomic_load_n(&qr_decode_miss_streak, __ATOMIC_ACQUIRE);
    decode_profile = select_decode_profile(decode_profile_seq++, payload_seen,
                                           miss_streak);
    if (decode_profile.output_size > preview_crop)
      decode_profile.output_size = preview_crop;
    uint32_t decode_crop = preview_crop;
    if (decode_profile.crop_percent < 100) {
      decode_crop = (preview_crop * decode_profile.crop_percent) / 100;
      if (decode_crop < decode_profile.output_size)
        decode_crop = decode_profile.output_size;
      if (decode_crop > preview_crop)
        decode_crop = preview_crop;
    }
    uint32_t decode_crop_ox = (in_w - decode_crop) / 2;
    uint32_t decode_crop_oy = (in_h - decode_crop) / 2;
    rgb565_crop_to_grayscale(camera_buf, decode_buffer, in_w, in_h,
                             decode_crop, decode_crop_ox, decode_crop_oy,
                             decode_profile.output_size,
                             decode_profile.smooth_downscale);
    decode_ready = true;
  }

  // Preview is intentionally low-frequency in scan mode. It is useful for
  // aiming, but refreshing it every camera frame steals PPA/display bandwidth
  // from animated QR decoding.
  static uint32_t preview_frame_seq = 0;
  uint32_t preview_period = preview_paused_for_payload
                                ? CAMERA_PREVIEW_AFTER_PAYLOAD_FRAME_PERIOD
                                : CAMERA_PREVIEW_FRAME_PERIOD;
  bool refresh_preview =
      cam_ppa_client &&
      (settings_active || ((preview_frame_seq++ % preview_period) == 0));
  if (refresh_preview && display_buffer_a && display_buffer_b &&
      current_display_buffer && !closing) {
    uint8_t *back_buffer = (current_display_buffer == display_buffer_a)
                               ? display_buffer_b
                               : display_buffer_a;
    uint8_t *display_src = back_buffer;
    float sim_scale = (float)CAMERA_SCREEN_WIDTH / (float)preview_crop;
    ppa_srm_oper_config_t srm = {
        .in.buffer = camera_buf,
        .in.pic_w = in_w,
        .in.pic_h = in_h,
        .in.block_w = preview_crop,
        .in.block_h = preview_crop,
        .in.block_offset_x = crop_ox,
        .in.block_offset_y = crop_oy,
        .in.srm_cm = PPA_SRM_COLOR_MODE_RGB565,
        .out.buffer = back_buffer,
        .out.buffer_size = display_buffer_size,
        .out.pic_w = CAMERA_SCREEN_WIDTH,
        .out.pic_h = CAMERA_SCREEN_HEIGHT,
        .out.block_offset_x = 0,
        .out.block_offset_y = 0,
        .out.srm_cm = PPA_SRM_COLOR_MODE_RGB565,
        .rotation_angle = PPA_SRM_ROTATION_ANGLE_0,
        .scale_x = sim_scale,
        .scale_y = sim_scale,
        .mode = PPA_TRANS_MODE_BLOCKING,
    };

    if (ppa_do_scale_rotate_mirror(cam_ppa_client, &srm) == ESP_OK) {
      buffer_swap_needed = true;
      if (!closing && !destruction_in_progress && bsp_display_lock(1)) {
        if (!closing && !destruction_in_progress && camera_img) {
          current_display_buffer = back_buffer;
          img_refresh_dsc.data = display_src;
          lv_img_set_src(camera_img, &img_refresh_dsc);
        }
        buffer_swap_needed = false;
        bsp_display_unlock();
      }
    }
  }

  // QR decoder gets the decode crop selected for this frame. When that slot is
  // still being decoded, keep the queued frame instead of replacing it with
  // the lower-resolution preview.
  if (qr_frame_queue && decode_ready) {
    qr_frame_data_t dummy;
    while (xQueueReceive(qr_frame_queue, &dummy, 0) == pdTRUE) {
      if (dummy.uses_decode_slot)
        __atomic_store_n(&decode_frame_in_flight, false, __ATOMIC_RELEASE);
    }
    qr_frame_data_t frame_data = {.frame_data = decode_buffer,
                                  .width = decode_profile.output_size,
                                  .height = decode_profile.output_size,
                                  .uses_decode_slot = true,
                                  .rescue_profile =
                                      decode_profile.rescue_profile};
    if (xQueueSend(qr_frame_queue, &frame_data, 0) != pdTRUE) {
      __atomic_store_n(&decode_frame_in_flight, false, __ATOMIC_RELEASE);
    }
  }

  __atomic_sub_fetch(&active_frame_operations, 1, __ATOMIC_SEQ_CST);
}

static void camera_init(void) {
  if (video_system_initialized) {
    closing = false;
    destruction_in_progress = false;
    if (camera_event_group) {
      xEventGroupClearBits(camera_event_group, CAMERA_EVENT_DELETE);
      xEventGroupSetBits(camera_event_group, CAMERA_EVENT_TASK_RUN);
    }
    if (!decode_buffer && !allocate_decode_buffer()) {
      ESP_LOGE(TAG, "Failed to reallocate high-resolution decode buffer");
    }
    if (!qr_decode_task_handle || !qr_parser || !qr_frame_queue) {
      if (!qr_decoder_init(CAMERA_DECODE_WIDTH, CAMERA_DECODE_HEIGHT)) {
        ESP_LOGE(TAG, "Failed to initialize QR decoder on active camera");
      }
    }
    if (!cam_ppa_client) {
      ppa_client_config_t ppa_cfg = {.oper_type = PPA_OPERATION_SRM};
      if (ppa_register_client(&ppa_cfg, &cam_ppa_client) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register PPA client for camera scaler");
        cam_ppa_client = NULL;
      }
    }
    if (camera_ctlr_handle >= 0 && video_stream_stopped_for_destroy) {
      esp_err_t start_err = app_video_stream_task_start(camera_ctlr_handle, 0);
      if (start_err == ESP_OK || start_err == ESP_ERR_INVALID_STATE) {
        video_stream_stopped_for_destroy = false;
        (void)app_video_register_frame_operation_cb(
            camera_video_frame_operation);
      } else {
        ESP_LOGW(TAG, "Failed to resume camera stream: %s",
                 esp_err_to_name(start_err));
      }
    }
    return;
  }

  camera_event_group = xEventGroupCreate();
  if (!camera_event_group) {
    ESP_LOGE(TAG, "Failed to create camera event group");
    return;
  }

  xEventGroupSetBits(camera_event_group, CAMERA_EVENT_TASK_RUN);

  i2c_master_bus_handle_t i2c_handle = bsp_i2c_get_handle();
  if (!i2c_handle) {
    ESP_LOGE(TAG, "Failed to get I2C bus handle");
    return;
  }

  esp_err_t err = app_video_main(i2c_handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize camera: %s", esp_err_to_name(err));
    return;
  }

  video_system_initialized = true;

  camera_ctlr_handle = app_video_open(CAM_DEV_PATH, APP_VIDEO_FMT_RGB565);
  if (camera_ctlr_handle < 0) {
    ESP_LOGE(TAG, "Failed to open camera device");
    return;
  }

#if BSP_CAM_HAS_MOTOR
  has_focus_motor = app_video_has_focus_motor(camera_ctlr_handle);
#else
  has_focus_motor = false;
#endif

  err = app_video_register_frame_operation_cb(camera_video_frame_operation);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to register camera frame callback: %s",
             esp_err_to_name(err));
    return;
  }

  img_refresh_dsc = (lv_img_dsc_t){
      .header = {.cf = LV_COLOR_FORMAT_RGB565,
                 .w = CAMERA_SCREEN_WIDTH,
                 .h = CAMERA_SCREEN_HEIGHT},
      .data_size = CAMERA_SCREEN_WIDTH * CAMERA_SCREEN_HEIGHT * 2,
      .data = NULL,
  };

  if (!allocate_display_buffers(CAMERA_SCREEN_WIDTH, CAMERA_SCREEN_HEIGHT)) {
    ESP_LOGE(TAG, "Failed to allocate display buffers");
    return;
  }
  if (!allocate_decode_buffer()) {
    free_display_buffers();
    return;
  }

  current_display_buffer = display_buffer_a;
  img_refresh_dsc.data = current_display_buffer;

  err = app_video_set_bufs(camera_ctlr_handle, CAM_BUF_NUM, NULL);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to configure camera buffers: %s",
             esp_err_to_name(err));
    return;
  }

  esp_err_t start_err = app_video_stream_task_start(camera_ctlr_handle, 0);
  if (start_err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to start camera stream task: %s",
             esp_err_to_name(start_err));
    return;
  }
  video_stream_stopped_for_destroy = false;

  // Apply camera settings after stream starts (V4L2 controls register with the
  // sensor device only once streaming).
  app_video_set_ae_target(camera_ctlr_handle, settings_get_ae_target());
  if (has_focus_motor) {
    app_video_set_focus(camera_ctlr_handle, settings_get_focus_position());
  }

  if (!qr_decoder_init(CAMERA_DECODE_WIDTH, CAMERA_DECODE_HEIGHT)) {
    ESP_LOGE(TAG, "Failed to initialize QR decoder");
    (void)app_video_register_frame_operation_cb(NULL);
    (void)app_video_stream_task_stop(camera_ctlr_handle);
    int wait_count = 0;
    while (__atomic_load_n(&active_frame_operations, __ATOMIC_SEQ_CST) > 0 &&
           wait_count < 30) {
      vTaskDelay(pdMS_TO_TICKS(10));
      wait_count++;
    }
    (void)app_video_close(camera_ctlr_handle);
    camera_ctlr_handle = -1;
    video_stream_stopped_for_destroy = false;
    free_decode_buffer();
    free_display_buffers();
    return;
  }

  // PPA does centered crop + downscale on every frame.
  ppa_client_config_t ppa_cfg = {.oper_type = PPA_OPERATION_SRM};
  if (ppa_register_client(&ppa_cfg, &cam_ppa_client) != ESP_OK) {
    ESP_LOGE(TAG, "Failed to register PPA client for camera scaler");
    cam_ppa_client = NULL;
  }
}

static bool camera_run(void) {
  if (camera_ctlr_handle < 0 || !video_system_initialized) {
    camera_init();
  } else if (!qr_decode_task_handle || !qr_parser || !qr_frame_queue ||
             !cam_ppa_client || !decode_buffer) {
    camera_init();
  } else {
    closing = false;
    destruction_in_progress = false;
    if (camera_event_group) {
      xEventGroupClearBits(camera_event_group, CAMERA_EVENT_DELETE);
      xEventGroupSetBits(camera_event_group, CAMERA_EVENT_TASK_RUN);
    }
    if (video_stream_stopped_for_destroy) {
      esp_err_t start_err = app_video_stream_task_start(camera_ctlr_handle, 0);
      if (start_err == ESP_OK || start_err == ESP_ERR_INVALID_STATE) {
        video_stream_stopped_for_destroy = false;
        (void)app_video_register_frame_operation_cb(
            camera_video_frame_operation);
      } else {
        ESP_LOGW(TAG, "Failed to resume camera stream: %s",
                 esp_err_to_name(start_err));
      }
    }
  }
  return camera_ctlr_handle >= 0 && video_system_initialized &&
         qr_decode_task_handle && qr_parser && qr_frame_queue;
}

void qr_scanner_page_create(lv_obj_t *parent, void (*return_cb)(void)) {
  (void)parent;

  return_callback = return_cb;
  closing = false;
  scan_completed = false;
  is_fully_initialized = false;
  active_frame_operations = 0;
  preview_paused_for_payload = false;
  decode_profile_seq = 0;
  __atomic_store_n(&qr_payload_seen, false, __ATOMIC_RELEASE);
  __atomic_store_n(&qr_decode_miss_streak, 0, __ATOMIC_RELEASE);

  qr_scanner_screen = lv_obj_create(lv_screen_active());
  lv_obj_set_size(qr_scanner_screen, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_color(qr_scanner_screen, bg_color(), 0);
  lv_obj_set_style_bg_opa(qr_scanner_screen, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(qr_scanner_screen, 0, 0);
  lv_obj_set_style_pad_all(qr_scanner_screen, 0, 0);
  lv_obj_set_style_radius(qr_scanner_screen, 0, 0);
  lv_obj_set_style_shadow_width(qr_scanner_screen, 0, 0);
  lv_obj_clear_flag(qr_scanner_screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(qr_scanner_screen, touch_event_cb, LV_EVENT_CLICKED,
                      NULL);

  lv_obj_t *frame_buffer = lv_obj_create(qr_scanner_screen);
  lv_obj_set_size(frame_buffer, CAMERA_SCREEN_WIDTH, CAMERA_SCREEN_HEIGHT);
  lv_obj_center(frame_buffer);
  lv_obj_set_style_bg_opa(frame_buffer, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(frame_buffer, 0, 0);
  lv_obj_set_style_pad_all(frame_buffer, 0, 0);
  lv_obj_set_style_radius(frame_buffer, 0, 0);
  lv_obj_clear_flag(frame_buffer, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(frame_buffer, touch_event_cb, LV_EVENT_CLICKED, NULL);

  camera_img = lv_img_create(frame_buffer);
  lv_obj_set_size(camera_img, CAMERA_SCREEN_WIDTH, CAMERA_SCREEN_HEIGHT);
  lv_obj_center(camera_img);
  lv_obj_clear_flag(camera_img, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(camera_img, bg_color(), 0);
  lv_obj_set_style_bg_opa(camera_img, LV_OPA_COVER, 0);

  scan_status_label = NULL;

#ifdef QR_PERF_DEBUG
  fps_label = lv_label_create(qr_scanner_screen);
  lv_label_set_text(fps_label, "Camera:-- Decode:--");
  lv_obj_set_style_text_color(fps_label, lv_color_hex(0x00FF00), 0);
  lv_obj_set_style_text_font(fps_label, theme_font_small(), 0);
  lv_obj_align(fps_label, LV_ALIGN_TOP_LEFT, 10, 8);
  reset_perf_metrics();
#endif

  if (!camera_run()) {
    ESP_LOGE(TAG, "Failed to initialize camera");
    return;
  }

  completion_timer = lv_timer_create(completion_timer_cb, 100, NULL);
  is_fully_initialized = true;
}

void qr_scanner_page_show(void) {
  if (is_fully_initialized && !closing && qr_scanner_screen) {
    lv_obj_clear_flag(qr_scanner_screen, LV_OBJ_FLAG_HIDDEN);
  }
}

void qr_scanner_page_hide(void) {
  if (qr_scanner_screen) {
    lv_obj_add_flag(qr_scanner_screen, LV_OBJ_FLAG_HIDDEN);
  }
}

void qr_scanner_page_destroy(void) {
  destruction_in_progress = true;
  closing = true;
  is_fully_initialized = false;
  if (qr_scanner_screen)
    lv_obj_add_flag(qr_scanner_screen, LV_OBJ_FLAG_HIDDEN);
  destroy_settings_overlay();
  has_focus_motor = false;

  if (completion_timer) {
    lv_timer_del(completion_timer);
    completion_timer = NULL;
  }
  scan_completed = false;

  if (camera_event_group) {
    xEventGroupClearBits(camera_event_group, CAMERA_EVENT_TASK_RUN);
    xEventGroupSetBits(camera_event_group, CAMERA_EVENT_DELETE);
  }

  if (camera_ctlr_handle >= 0) {
    (void)app_video_register_frame_operation_cb(NULL);
    (void)app_video_stream_task_stop(camera_ctlr_handle);
    video_stream_stopped_for_destroy = true;
  }

  int wait_count = 0;
  while (__atomic_load_n(&active_frame_operations, __ATOMIC_SEQ_CST) > 0 &&
         wait_count < 30) {
    vTaskDelay(pdMS_TO_TICKS(10));
    wait_count++;
  }

  int remaining_ops =
      __atomic_load_n(&active_frame_operations, __ATOMIC_SEQ_CST);
  if (remaining_ops > 0)
    ESP_LOGW(TAG, "Timeout waiting for frame operations (remaining: %d)",
             remaining_ops);

  if (camera_ctlr_handle >= 0) {
    esp_err_t close_ret = app_video_close(camera_ctlr_handle);
    if (close_ret == ESP_ERR_TIMEOUT) {
      ESP_LOGE(TAG, "Video close timeout; forced scanner cleanup");
    }
    camera_ctlr_handle = -1;
    video_stream_stopped_for_destroy = false;
  }

  qr_decoder_cleanup();

  bool display_locked = bsp_display_lock(1000);
  if (!display_locked)
    ESP_LOGW(TAG, "Failed to lock display for UI cleanup");

  camera_img = NULL;
  scan_status_label = NULL;
#ifdef QR_PERF_DEBUG
  fps_label = NULL;
#endif
  cleanup_progress_indicators();
  cleanup_ur_progress_bar();
  if (qr_scanner_screen) {
    lv_obj_del(qr_scanner_screen);
    qr_scanner_screen = NULL;
  }

  if (display_locked)
    bsp_display_unlock();

  free_display_buffers();
  free_decode_buffer();

  if (cam_ppa_client) {
    ppa_unregister_client(cam_ppa_client);
    cam_ppa_client = NULL;
  }

  if (video_system_initialized) {
    app_video_deinit();
    video_system_initialized = false;
  }

  if (camera_event_group) {
    vEventGroupDelete(camera_event_group);
    camera_event_group = NULL;
  }

  return_callback = NULL;
  buffer_swap_needed = false;
  destruction_in_progress = false;
  closing = false;
  active_frame_operations = 0;
  preview_paused_for_payload = false;
  decode_profile_seq = 0;
  __atomic_store_n(&qr_payload_seen, false, __ATOMIC_RELEASE);
  __atomic_store_n(&qr_decode_miss_streak, 0, __ATOMIC_RELEASE);
}

char *qr_scanner_get_completed_content(void) {
  return qr_scanner_get_completed_content_with_len(NULL);
}

char *qr_scanner_get_completed_content_with_len(size_t *content_len) {
  if (content_len) {
    *content_len = 0;
  }

  if (qr_parser && qr_parser_is_complete(qr_parser)) {
    size_t result_len = 0;
    char *complete_result = qr_parser_result(qr_parser, &result_len);
    if (complete_result && content_len) {
      *content_len = result_len;
    }
    return complete_result; // Caller must free this
  }
  return NULL;
}

bool qr_scanner_is_ready(void) { return is_fully_initialized && !closing; }

int qr_scanner_get_format(void) {
  if (qr_parser) {
    return qr_parser_get_format(qr_parser);
  }
  return -1;
}

bool qr_scanner_get_ur_result(const char **ur_type_out,
                              const uint8_t **cbor_data_out,
                              size_t *cbor_len_out) {
  if (qr_parser) {
    return qr_parser_get_ur_result(qr_parser, ur_type_out, cbor_data_out,
                                   cbor_len_out);
  }
  return false;
}
