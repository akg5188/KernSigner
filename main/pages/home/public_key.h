#ifndef PUBLIC_KEY_H
#define PUBLIC_KEY_H

#include <lvgl.h>

typedef enum {
  PUBLIC_KEY_EXPORT_STANDARD = 0,
  PUBLIC_KEY_EXPORT_BLUEWALLET_XPUB,
  PUBLIC_KEY_EXPORT_BLUEWALLET_ZPUB,
} public_key_export_mode_t;

void public_key_page_create(lv_obj_t *parent, void (*return_cb)(void));
void public_key_page_create_with_mode(lv_obj_t *parent, void (*return_cb)(void),
                                      public_key_export_mode_t mode);
void public_key_page_show(void);
void public_key_page_hide(void);
void public_key_page_destroy(void);

#endif // PUBLIC_KEY_H
