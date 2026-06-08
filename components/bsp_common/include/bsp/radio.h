#pragma once

#include "esp_err.h"

/**
 * @brief Disable board radio hardware and keep firmware radio stacks off.
 *
 * Boards without controllable radio hardware provide a no-op implementation.
 */
esp_err_t bsp_wireless_disable(void);
