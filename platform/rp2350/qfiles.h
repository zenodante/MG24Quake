#ifndef QRP_QFILES_H
#define QRP_QFILES_H
#include "qpak.h"
/* Core-0-only, read-only Quake Sys_File* bridge. One shared 4 KiB cache,
 * bounded handles, independent cursors; no whole-file allocation.
 * Mounting closes all handles. The mounted QPAK must outlive every handle.
 * Paths are QPAK-relative (e.g. gfx/palette.lmp), not host filesystem paths. */
#define QFILES_MAX_HANDLES 16
void qfiles_mount(const qpak_t *pak);
/* Direct XIP access where possible; does not move the file cursor. */
const uint8_t *qfiles_map(int handle, uint32_t offset, size_t length);
/* MG24 model.c asks for an external-memory pointer to a complete converted
 * model file.  On RP2350/QXIP this is simply the immutable memory-mapped file
 * payload; no file copy, cache fill or level-time flash write is performed. */
void *getExtMemPointerToFileInPak(const char *path, unsigned int *size);
/* Signatures match Quake/sys.h. Open returns length or -1; handle -1 on failure.
 * Reads stop at EOF; invalid reads return 0. Out-of-range seeks are ignored.
 * Writing is unsupported until a separate save/config filesystem is added. */
int Sys_FileOpenRead(char *path, int *handle);
int Sys_FileOpenWrite(char *path);
void Sys_FileClose(int handle);
void Sys_FileSeek(int handle, int position);
int Sys_FileRead(int handle, void *dest, int count);
int Sys_FileWrite(int handle, void *data, int count);
int Sys_FileTime(char *path);
#endif
