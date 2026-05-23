#ifndef SIGNER_SERVICES_H
#define SIGNER_SERVICES_H

#include "signer_feature_catalog.h"

#include <stddef.h>

typedef enum {
  SIGNER_SERVICE_WALLET,
  SIGNER_SERVICE_CRYPTO,
  SIGNER_SERVICE_QR,
  SIGNER_SERVICE_CAMERA,
  SIGNER_SERVICE_STORAGE,
  SIGNER_SERVICE_DISPLAY,
  SIGNER_SERVICE_SMARTCARD,
  SIGNER_SERVICE_COUNT,
} signer_service_id_t;

typedef enum {
  SIGNER_SERVICE_READY,
  SIGNER_SERVICE_CANDIDATE,
  SIGNER_SERVICE_STUB,
  SIGNER_SERVICE_BLOCKED,
} signer_service_state_t;

typedef struct {
  signer_service_id_t id;
  const char *title;
  signer_service_state_t state;
  const char *summary;
} signer_service_status_t;

const signer_service_status_t *signer_service_status_at(size_t index);
size_t signer_service_status_count(void);
const char *signer_service_state_name(signer_service_state_t state);
const char *signer_service_guard_for_feature(const signer_feature_t *feature);
const char *signer_service_next_step_for_feature(const signer_feature_t *feature);

#endif // SIGNER_SERVICES_H
