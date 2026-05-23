#include "signer_services.h"

#include "i18n/i18n.h"

typedef struct {
  signer_service_id_t id;
  const char *title_key;
  const char *title;
  signer_service_state_t state;
  const char *summary_key;
  const char *summary;
} signer_service_status_template_t;

static const signer_service_status_template_t service_statuses[] = {
    {SIGNER_SERVICE_WALLET, "service.wallet.title", "Wallet service",
     SIGNER_SERVICE_READY, "service.wallet.summary",
     "Import, create, public keys, addresses, backup, and signing."},
    {SIGNER_SERVICE_CRYPTO, "service.crypto.title", "Crypto service",
     SIGNER_SERVICE_READY, "service.crypto.summary",
     "Checks account, network, and path before signing."},
    {SIGNER_SERVICE_QR, "service.qr.title", "QR service",
     SIGNER_SERVICE_READY, "service.qr.summary",
     "Scanning, import, and signing are available."},
    {SIGNER_SERVICE_CAMERA, "service.camera.title", "Camera service",
     SIGNER_SERVICE_READY, "service.camera.summary",
     "Camera and recognition are available."},
    {SIGNER_SERVICE_STORAGE, "service.storage.title", "Storage service",
     SIGNER_SERVICE_READY, "service.storage.summary",
     "Storage card checks and browsing are available."},
    {SIGNER_SERVICE_DISPLAY, "service.display.title", "Display and touch",
     SIGNER_SERVICE_READY, "service.display.summary",
     "English UI and touch are available."},
    {SIGNER_SERVICE_SMARTCARD, "service.smartcard.title", "Smartcard check",
     SIGNER_SERVICE_CANDIDATE, "service.smartcard.summary",
     "CCID, connection, signing, and addresses are wired; card writes and PIN "
     "changes remain hidden."},
};

const signer_service_status_t *signer_service_status_at(size_t index) {
  if (index >= signer_service_status_count())
    return NULL;

  static signer_service_status_t localized[SIGNER_SERVICE_COUNT];
  const signer_service_status_template_t *item = &service_statuses[index];
  localized[index] = (signer_service_status_t){
      .id = item->id,
      .title = i18n_tr_or(item->title_key, item->title),
      .state = item->state,
      .summary = i18n_tr_or(item->summary_key, item->summary),
  };
  return &localized[index];
}

size_t signer_service_status_count(void) {
  return sizeof(service_statuses) / sizeof(service_statuses[0]);
}

const char *signer_service_state_name(signer_service_state_t state) {
  switch (state) {
  case SIGNER_SERVICE_READY:
    return i18n_tr_or("service.state.ready", "Ready");
  case SIGNER_SERVICE_CANDIDATE:
    return i18n_tr_or("service.state.available", "Available");
  case SIGNER_SERVICE_STUB:
    return i18n_tr_or("service.state.maintenance", "Maintenance");
  case SIGNER_SERVICE_BLOCKED:
    return i18n_tr_or("service.state.paused", "Paused");
  default:
    return i18n_tr_or("common.unknown", "Unknown");
  }
}

const char *signer_service_guard_for_feature(const signer_feature_t *feature) {
  if (!feature)
    return i18n_tr_or("service.guard.no_feature",
                      "Wallet entry: feature definition not found.");

  switch (feature->risk) {
  case SIGNER_FEATURE_RISK_VIEW_ONLY:
    return i18n_tr_or("service.guard.view_only",
                      "Wallet entry: currently showing public information or "
                      "menus.");
  case SIGNER_FEATURE_RISK_SECRET_MATERIAL:
    return i18n_tr_or("service.guard.secret_material",
                      "Wallet entry: sensitive material stays in memory and is "
                      "cleared on exit or power-off.");
  case SIGNER_FEATURE_RISK_SIGNING:
    return i18n_tr_or("service.guard.signing",
                      "Wallet entry: signing keeps transaction review and "
                      "explicit confirmation.");
  case SIGNER_FEATURE_RISK_EXTERNAL_IO:
    return i18n_tr_or("service.guard.external_io",
                      "Wallet entry: peripheral input is handled by its page; "
                      "sensitive content is not imported automatically.");
  case SIGNER_FEATURE_RISK_DEVICE_CONTROL:
    return i18n_tr_or("service.guard.device_control",
                      "Wallet entry: device controls use implemented pages, "
                      "with confirmation for risky actions.");
  default:
    return i18n_tr_or("service.guard.unknown",
                      "Wallet entry: unknown risk is treated as high risk.");
  }
}

const char *signer_service_next_step_for_feature(const signer_feature_t *feature) {
  if (!feature)
    return i18n_tr_or("service.next.no_feature",
                      "Tip: return home and choose a feature.");

  switch (feature->status) {
  case SIGNER_FEATURE_NOT_STARTED:
    return i18n_tr_or("service.next.not_started", "Tip: open it from home.");
  case SIGNER_FEATURE_UI_READY:
    return i18n_tr_or("service.next.ui_ready",
                      "Tip: press the button to continue.");
  case SIGNER_FEATURE_SERVICE_STUB:
    return i18n_tr_or("service.next.protected",
                      "Tip: this feature is currently protected.");
  case SIGNER_FEATURE_HARDWARE_WIRED:
    return i18n_tr_or("service.next.hardware_wired",
                      "Tip: you can run the check now.");
  case SIGNER_FEATURE_VERIFIED:
    return i18n_tr_or("service.next.verified",
                      "Tip: you can continue using it.");
  default:
    return i18n_tr_or("service.next.unknown",
                      "Tip: unknown status is handled as protected.");
  }
}
