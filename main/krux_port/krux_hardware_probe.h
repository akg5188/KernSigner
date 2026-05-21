#ifndef KRUX_HARDWARE_PROBE_H
#define KRUX_HARDWARE_PROBE_H

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct {
  int screen_width;
  int screen_height;
  uint32_t free_heap;
  uint32_t min_free_heap;
  size_t free_spiram;
  size_t min_free_spiram;
  const char *display_status;
  const char *touch_status;
  const char *sd_capability;
  const char *camera_status;
  const char *usb_status;
} krux_hardware_snapshot_t;

void krux_hardware_probe_snapshot(krux_hardware_snapshot_t *out);
void krux_hardware_probe_format_snapshot(char *buf, size_t buf_len);
esp_err_t krux_hardware_probe_storage_rw(char *detail, size_t detail_len);

#endif // KRUX_HARDWARE_PROBE_H
