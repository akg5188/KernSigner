#ifndef SIGNER_SHELL_H
#define SIGNER_SHELL_H

#include <lvgl.h>
#include <stdbool.h>
#include <stddef.h>

void signer_shell_create(lv_obj_t *parent);
bool signer_shell_show_screen(const char *screen_id);
size_t signer_shell_screen_count(void);
const char *signer_shell_screen_id_at(size_t index);
const char *signer_shell_screen_title_at(size_t index);
const char *signer_shell_current_screen_id(void);

#endif // SIGNER_SHELL_H
