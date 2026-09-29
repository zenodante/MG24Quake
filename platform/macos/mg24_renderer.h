#ifndef QMAC_MG24_RENDERER_H
#define QMAC_MG24_RENDERER_H
#include "qrender.h"
bool mg24_pack(const qpak_t *pak,const char *output);
bool mg24_image_open_range(const qpak_t *pak,const char *path,size_t offset,size_t length);
bool mg24_image_open(const qpak_t *pak,const char *path);
size_t mg24_image_bytes(void);
size_t mg24_relocations(void);
bool mg24_bind(const qbsp_t *world);
bool mg24_draw(const qr_camera_t *camera,uint8_t *pixels);
size_t mg24_binding_bytes(void);
size_t mg24_workspace_bytes(void);
void mg24_shutdown(void);
#endif
