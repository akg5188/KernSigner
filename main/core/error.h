#ifndef KSIG_ERROR_H
#define KSIG_ERROR_H

#include <stdbool.h>

typedef enum {
  KSIG_OK = 0,
  KSIG_ERR_INVALID_INPUT,
  KSIG_ERR_OUT_OF_MEMORY,
  KSIG_ERR_CRYPTO_FAILURE,
  KSIG_ERR_QR_PARSE_FAILED,
  KSIG_ERR_MNEMONIC_INVALID,
  KSIG_ERR_PSBT_INVALID,
  KSIG_ERR_NOT_INITIALIZED,
  KSIG_ERR_TIMEOUT,
  KSIG_ERR_CANCELLED,
  KSIG_ERR_IO,
  KSIG_ERR_NOT_FOUND,
  KSIG_ERR_ALREADY_EXISTS,
  KSIG_ERR_BUFFER_TOO_SMALL,
  KSIG_ERR_UNSUPPORTED,
} ksig_error_t;

const char *ksig_error_str(ksig_error_t err);

static inline bool ksig_is_ok(ksig_error_t err) { return err == KSIG_OK; }

static inline bool ksig_is_error(ksig_error_t err) { return err != KSIG_OK; }

#endif
