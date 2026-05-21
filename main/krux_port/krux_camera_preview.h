#ifndef KRUX_CAMERA_PREVIEW_H
#define KRUX_CAMERA_PREVIEW_H

#include <stdbool.h>

bool krux_camera_preview_open(const char *title, const char *notice);
bool krux_camera_preview_open_ex(const char *title, const char *notice,
                                 bool reveal_qr_payload);
void krux_camera_preview_close(void);
bool krux_camera_preview_is_open(void);

#endif // KRUX_CAMERA_PREVIEW_H
