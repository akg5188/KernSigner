#ifndef SENSITIVE_PIN_H
#define SENSITIVE_PIN_H

#include <stdbool.h>

typedef void (*sensitive_pin_cb_t)(void);

/* Require a PIN check before a sensitive screen or operation.
 *
 * If no PIN exists, the user must set one first. The callback runs only after
 * PIN unlock/setup succeeds. The cancel callback is used only for setup flows,
 * because normal unlock intentionally has no back button.
 */
bool sensitive_pin_require(sensitive_pin_cb_t success_cb,
                           sensitive_pin_cb_t cancel_cb);

#endif // SENSITIVE_PIN_H
