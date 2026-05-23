#ifndef SIGNER_STORAGE_BROWSER_H
#define SIGNER_STORAGE_BROWSER_H

#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t signer_storage_browser_format_root(char *buf, size_t buf_len);

#ifdef __cplusplus
}
#endif

#endif // SIGNER_STORAGE_BROWSER_H
