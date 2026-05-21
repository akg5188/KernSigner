#include "sensitive_pin.h"
#include "../../core/pin.h"
#include "../../ui/dialog.h"
#include "../pin/pin_page.h"
#include <lvgl.h>

static sensitive_pin_cb_t pending_success_cb = NULL;
static sensitive_pin_cb_t pending_cancel_cb = NULL;

static void sensitive_pin_complete(void) {
  sensitive_pin_cb_t cb = pending_success_cb;
  pending_success_cb = NULL;
  pending_cancel_cb = NULL;
  pin_page_destroy();
  if (cb)
    cb();
}

static void sensitive_pin_cancel(void) {
  sensitive_pin_cb_t cb = pending_cancel_cb;
  pending_success_cb = NULL;
  pending_cancel_cb = NULL;
  pin_page_destroy();
  if (cb)
    cb();
}

bool sensitive_pin_require(sensitive_pin_cb_t success_cb,
                           sensitive_pin_cb_t cancel_cb) {
  if (!success_cb)
    return false;

  pending_success_cb = success_cb;
  pending_cancel_cb = cancel_cb;

  if (!pin_is_configured()) {
    pending_success_cb = NULL;
    pending_cancel_cb = NULL;
    dialog_show_error("请先在设置里创建 PIN 码", cancel_cb, 0);
    return false;
  }

  pin_page_create(lv_screen_active(), PIN_PAGE_UNLOCK, sensitive_pin_complete,
                  sensitive_pin_cancel);
  pin_page_show();
  return true;
}
