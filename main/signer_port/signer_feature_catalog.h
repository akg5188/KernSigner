#ifndef SIGNER_FEATURE_CATALOG_H
#define SIGNER_FEATURE_CATALOG_H

#include <stdbool.h>
#include <stddef.h>

typedef enum {
  SIGNER_FEATURE_GROUP,
  SIGNER_FEATURE_ACTION,
} signer_feature_kind_t;

typedef enum {
  SIGNER_FEATURE_NOT_STARTED,
  SIGNER_FEATURE_UI_READY,
  SIGNER_FEATURE_SERVICE_STUB,
  SIGNER_FEATURE_HARDWARE_WIRED,
  SIGNER_FEATURE_VERIFIED,
} signer_feature_status_t;

typedef enum {
  SIGNER_FEATURE_RISK_VIEW_ONLY,
  SIGNER_FEATURE_RISK_SECRET_MATERIAL,
  SIGNER_FEATURE_RISK_SIGNING,
  SIGNER_FEATURE_RISK_EXTERNAL_IO,
  SIGNER_FEATURE_RISK_DEVICE_CONTROL,
} signer_feature_risk_t;

typedef struct {
  const char *id;
  const char *parent_id;
  const char *title;
  const char *subtitle;
  const char *summary;
  const char *source_path;
  signer_feature_kind_t kind;
  signer_feature_status_t status;
  signer_feature_risk_t risk;
} signer_feature_t;

size_t signer_feature_count(void);
const signer_feature_t *signer_feature_at(size_t index);
const signer_feature_t *signer_feature_find(const char *id);

size_t signer_feature_child_count(const char *parent_id);
const signer_feature_t *signer_feature_child_at(const char *parent_id,
                                            size_t child_index);

bool signer_feature_has_children(const signer_feature_t *feature);
const char *signer_feature_status_name(signer_feature_status_t status);
const char *signer_feature_status_detail(signer_feature_status_t status);
const char *signer_feature_risk_name(signer_feature_risk_t risk);
const char *signer_feature_risk_detail(signer_feature_risk_t risk);

size_t signer_feature_count_by_status(signer_feature_status_t status);
size_t signer_feature_action_count(void);

#endif // SIGNER_FEATURE_CATALOG_H
