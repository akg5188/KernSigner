#ifndef KRUX_FEATURE_CATALOG_H
#define KRUX_FEATURE_CATALOG_H

#include <stdbool.h>
#include <stddef.h>

typedef enum {
  KRUX_FEATURE_GROUP,
  KRUX_FEATURE_ACTION,
} krux_feature_kind_t;

typedef enum {
  KRUX_FEATURE_NOT_STARTED,
  KRUX_FEATURE_UI_READY,
  KRUX_FEATURE_SERVICE_STUB,
  KRUX_FEATURE_HARDWARE_WIRED,
  KRUX_FEATURE_VERIFIED,
} krux_feature_status_t;

typedef enum {
  KRUX_FEATURE_RISK_VIEW_ONLY,
  KRUX_FEATURE_RISK_SECRET_MATERIAL,
  KRUX_FEATURE_RISK_SIGNING,
  KRUX_FEATURE_RISK_EXTERNAL_IO,
  KRUX_FEATURE_RISK_DEVICE_CONTROL,
} krux_feature_risk_t;

typedef struct {
  const char *id;
  const char *parent_id;
  const char *title;
  const char *subtitle;
  const char *summary;
  const char *source_path;
  krux_feature_kind_t kind;
  krux_feature_status_t status;
  krux_feature_risk_t risk;
} krux_feature_t;

size_t krux_feature_count(void);
const krux_feature_t *krux_feature_at(size_t index);
const krux_feature_t *krux_feature_find(const char *id);

size_t krux_feature_child_count(const char *parent_id);
const krux_feature_t *krux_feature_child_at(const char *parent_id,
                                            size_t child_index);

bool krux_feature_has_children(const krux_feature_t *feature);
const char *krux_feature_status_name(krux_feature_status_t status);
const char *krux_feature_status_detail(krux_feature_status_t status);
const char *krux_feature_risk_name(krux_feature_risk_t risk);
const char *krux_feature_risk_detail(krux_feature_risk_t risk);

size_t krux_feature_count_by_status(krux_feature_status_t status);
size_t krux_feature_action_count(void);

#endif // KRUX_FEATURE_CATALOG_H
