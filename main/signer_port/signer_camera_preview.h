#ifndef SIGNER_CAMERA_PREVIEW_H
#define SIGNER_CAMERA_PREVIEW_H

#include <stdbool.h>

bool signer_camera_preview_open(const char *title, const char *notice);
bool signer_camera_preview_open_ex(const char *title, const char *notice,
                                 bool reveal_qr_payload);
void signer_camera_preview_close(void);
bool signer_camera_preview_is_open(void);

#endif // SIGNER_CAMERA_PREVIEW_H
