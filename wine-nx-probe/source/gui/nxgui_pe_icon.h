#ifndef WINE_NX_GUI_PE_ICON_H
#define WINE_NX_GUI_PE_ICON_H

#include <stdint.h>

typedef struct
{
    uint32_t *pixels;   /* RGBA8888, straight alpha, row-major top-down, malloc'd */
    int width, height;
} nxgui_icon_t;

/* Reads exe_path directly off disk (plain fopen/fread/fseek, same style
 * as source/runtime.c's peek_pe_machine() -- the exe is never mapped or
 * executed just to grab its icon), walks its PE resource directory for
 * RT_GROUP_ICON/RT_ICON, and decodes the largest available image
 * (raw DIB in 1/4/8/24/32bpp, or PNG-compressed via nxgui_png_mini.c --
 * see that file for exactly which PNG subset is supported).
 *
 * Best-effort like every other PE-parsing helper in this project: a
 * missing resource section, an unrecognized icon format, or a truncated
 * file all just return 0 rather than fault -- callers (the library
 * screen) fall back to a generic placeholder icon, never a hard error,
 * since a cosmetic miss on one game's icon shouldn't block the whole
 * library from loading.
 *
 * On success returns 1 and fills *out (caller must free(out->pixels)). */
int nxgui_pe_icon_extract( const char *exe_path, nxgui_icon_t *out );

#endif /* WINE_NX_GUI_PE_ICON_H */
