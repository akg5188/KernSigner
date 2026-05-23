#include "error.h"

#include "i18n/i18n.h"

const char *ksig_error_str(ksig_error_t err) {
  switch (err) {
  case KSIG_OK:
    return i18n_tr_or("error.success", "Success");
  case KSIG_ERR_INVALID_INPUT:
    return i18n_tr_or("error.invalid_input", "Invalid input");
  case KSIG_ERR_OUT_OF_MEMORY:
    return i18n_tr_or("error.out_of_memory", "Out of memory");
  case KSIG_ERR_CRYPTO_FAILURE:
    return i18n_tr_or("error.crypto_failure", "Cryptographic error");
  case KSIG_ERR_QR_PARSE_FAILED:
    return i18n_tr_or("error.qr_parse_failed", "QR parse failed");
  case KSIG_ERR_MNEMONIC_INVALID:
    return i18n_tr_or("error.mnemonic_invalid", "Invalid mnemonic");
  case KSIG_ERR_PSBT_INVALID:
    return i18n_tr_or("error.psbt_invalid", "Invalid transaction data");
  case KSIG_ERR_NOT_INITIALIZED:
    return i18n_tr_or("error.not_initialized", "Not initialized");
  case KSIG_ERR_TIMEOUT:
    return i18n_tr_or("error.timeout", "Timed out");
  case KSIG_ERR_CANCELLED:
    return i18n_tr_or("error.cancelled", "Cancelled");
  case KSIG_ERR_IO:
    return i18n_tr_or("error.io", "I/O error");
  case KSIG_ERR_NOT_FOUND:
    return i18n_tr_or("error.not_found", "Not found");
  case KSIG_ERR_ALREADY_EXISTS:
    return i18n_tr_or("error.already_exists", "Already exists");
  case KSIG_ERR_BUFFER_TOO_SMALL:
    return i18n_tr_or("error.buffer_too_small", "Buffer too small");
  case KSIG_ERR_UNSUPPORTED:
    return i18n_tr_or("error.unsupported", "Unsupported");
  default:
    return i18n_tr_or("error.unknown", "Unknown error");
  }
}
