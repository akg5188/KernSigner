// Session timeout: lock first, then power off after longer inactivity.

#include "session.h"
#include <lvgl.h>
#include <stdbool.h>

static lv_timer_t *session_timer = NULL;
static session_expired_cb_t expired_cb = NULL;
static session_expired_cb_t poweroff_cb = NULL;
static uint32_t lock_timeout_ms = 0;
static uint32_t poweroff_timeout_ms = 0;
static bool lock_fired = false;

static void session_timer_cb(lv_timer_t *timer) {
  (void)timer;
  if (lock_timeout_ms == 0 && poweroff_timeout_ms == 0)
    return;

  uint32_t inactive = lv_display_get_inactive_time(NULL);

  if (poweroff_timeout_ms > 0 && poweroff_cb &&
      inactive >= poweroff_timeout_ms) {
    session_stop();
    poweroff_cb();
    return;
  }

  if (!lock_fired && lock_timeout_ms > 0 && expired_cb &&
      inactive >= lock_timeout_ms) {
    lock_fired = true;
    if (poweroff_timeout_ms == 0)
      session_stop();
    expired_cb();
  }
}

void session_start(uint16_t timeout_sec) {
  session_start_protected(timeout_sec, 0);
}

void session_start_protected(uint16_t lock_timeout_sec,
                             uint16_t poweroff_timeout_sec) {
  session_stop();

  if (lock_timeout_sec == 0 && poweroff_timeout_sec == 0)
    return;

  lock_timeout_ms = (uint32_t)lock_timeout_sec * 1000;
  poweroff_timeout_ms = (uint32_t)poweroff_timeout_sec * 1000;
  lock_fired = false;
  session_timer = lv_timer_create(session_timer_cb, 1000, NULL);
}

void session_stop(void) {
  if (session_timer) {
    lv_timer_delete(session_timer);
    session_timer = NULL;
  }
  lock_timeout_ms = 0;
  poweroff_timeout_ms = 0;
  lock_fired = false;
}

void session_set_expired_callback(session_expired_cb_t cb) { expired_cb = cb; }

void session_set_poweroff_callback(session_expired_cb_t cb) { poweroff_cb = cb; }
