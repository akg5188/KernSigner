#ifndef SIGNER_HARDWARE_PROBE_H
#define SIGNER_HARDWARE_PROBE_H

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
} signer_hardware_snapshot_t;

void signer_hardware_probe_snapshot(signer_hardware_snapshot_t *out);
void signer_hardware_probe_format_snapshot(char *buf, size_t buf_len);
esp_err_t signer_hardware_probe_storage_rw(char *detail, size_t detail_len);

#endif // SIGNER_HARDWARE_PROBE_H
