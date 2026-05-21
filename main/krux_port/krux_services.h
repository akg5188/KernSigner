#ifndef KRUX_SERVICES_H
#define KRUX_SERVICES_H

#include "krux_feature_catalog.h"

#include <stddef.h>

typedef enum {
  KRUX_SERVICE_WALLET,
  KRUX_SERVICE_CRYPTO,
  KRUX_SERVICE_QR,
  KRUX_SERVICE_CAMERA,
  KRUX_SERVICE_STORAGE,
  KRUX_SERVICE_DISPLAY,
  KRUX_SERVICE_SMARTCARD,
  KRUX_SERVICE_COUNT,
} krux_service_id_t;

typedef enum {
  KRUX_SERVICE_READY,
  KRUX_SERVICE_CANDIDATE,
  KRUX_SERVICE_STUB,
  KRUX_SERVICE_BLOCKED,
} krux_service_state_t;

typedef struct {
  krux_service_id_t id;
  const char *title;
  krux_service_state_t state;
  const char *summary;
} krux_service_status_t;

const krux_service_status_t *krux_service_status_at(size_t index);
size_t krux_service_status_count(void);
const char *krux_service_state_name(krux_service_state_t state);
const char *krux_service_guard_for_feature(const krux_feature_t *feature);
const char *krux_service_next_step_for_feature(const krux_feature_t *feature);

#endif // KRUX_SERVICES_H
