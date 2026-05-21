#ifndef QR_VIEWER_H
#define QR_VIEWER_H

#include <lvgl.h>
#include <stdbool.h>
#include <stddef.h>

/**
 * Create the QR viewer page
 * @param parent Parent LVGL object
 * @param qr_content Content to display as QR code
 * @param title Optional title to display (can be NULL)
 * @param return_cb Callback function to call when returning
 */
bool qr_viewer_page_create(lv_obj_t *parent, const char *qr_content,
                           const char *title, void (*return_cb)(void));

/**
 * Create a low-density fullscreen QR viewer for printable backups.
 *
 * Long text is split into manual pNofM pages. No auto-rotation is used, so the
 * device can be placed on a copier/printer without the QR changing mid-copy.
 *
 * @param parent Parent LVGL object
 * @param qr_content Content to display as QR code
 * @param title Optional short title for the bottom status bar
 * @param return_cb Callback function to call when returning
 * @param max_chars_per_frame Max text chars per QR frame, 0 for default
 * @return true on success, false on failure
 */
bool qr_viewer_page_create_print(lv_obj_t *parent, const char *qr_content,
                                 const char *title, void (*return_cb)(void),
                                 size_t max_chars_per_frame);

/**
 * Create the QR viewer page with format support
 * @param parent Parent LVGL object
 * @param qr_format QR format (FORMAT_NONE, FORMAT_PMOFN, FORMAT_UR)
 * @param content Content or base64 PSBT string
 * @param title Optional title to display (can be NULL)
 * @param return_cb Callback function to call when returning
 * @return true on success, false on failure
 */
bool qr_viewer_page_create_with_format(lv_obj_t *parent, int qr_format,
                                       const char *content, const char *title,
                                       void (*return_cb)(void));

/**
 * Show the QR viewer page
 */
void qr_viewer_page_show(void);

/**
 * Hide the QR viewer page
 */
void qr_viewer_page_hide(void);

/**
 * Destroy the QR viewer page and free resources
 */
void qr_viewer_page_destroy(void);

#endif
