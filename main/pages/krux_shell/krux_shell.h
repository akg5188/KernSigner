#ifndef KRUX_SHELL_H
#define KRUX_SHELL_H

#include <lvgl.h>
#include <stdbool.h>
#include <stddef.h>

void krux_shell_create(lv_obj_t *parent);
bool krux_shell_show_screen(const char *screen_id);
size_t krux_shell_screen_count(void);
const char *krux_shell_screen_id_at(size_t index);
const char *krux_shell_screen_title_at(size_t index);
const char *krux_shell_current_screen_id(void);

#endif // KRUX_SHELL_H
