#ifndef SCAN_H
#define SCAN_H

#include <lvgl.h>

/**
 * Create the scan page — universal QR content detection
 * @param parent Parent LVGL object
 * @param return_cb Callback function to call when returning to home
 */
void scan_page_create(lv_obj_t *parent, void (*return_cb)(void));
void scan_page_create_unified(lv_obj_t *parent, void (*return_cb)(void));
void scan_page_create_smartcard_web3(lv_obj_t *parent,
                                      void (*return_cb)(void));

/**
 * Show the scan page
 */
void scan_page_show(void);

/**
 * Hide the scan page
 */
void scan_page_hide(void);

/**
 * Destroy the scan page and free resources
 */
void scan_page_destroy(void);

#ifdef SIMULATOR
void scan_simulator_show_btc_psbt_review(void);
void scan_simulator_show_web3_tx_review(void);
void scan_simulator_show_web3_typed_review(void);
#endif

#endif // SCAN_H
