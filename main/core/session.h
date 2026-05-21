// Session timeout: lock first, then power off after longer inactivity.

#ifndef SESSION_H
#define SESSION_H

#include <stdint.h>

typedef void (*session_expired_cb_t)(void);

/* Start monitoring inactivity. timeout_sec=0 disables the lock timer. */
void session_start(uint16_t timeout_sec);

/* Start lock + power-off protection. Either timeout may be 0. */
void session_start_protected(uint16_t lock_timeout_sec,
                             uint16_t poweroff_timeout_sec);

/* Stop monitoring (e.g. when PIN is removed). */
void session_stop(void);

/* Register callback invoked when the lock timeout expires. */
void session_set_expired_callback(session_expired_cb_t cb);

/* Register callback invoked when the power-off timeout expires. */
void session_set_poweroff_callback(session_expired_cb_t cb);

#endif // SESSION_H
