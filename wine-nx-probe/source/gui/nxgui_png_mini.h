#ifndef WINE_NX_GUI_PNG_MINI_H
#define WINE_NX_GUI_PNG_MINI_H

#include <stdint.h>

/* Decodes only the PNG subset Windows' own icon compiler (rc.exe/llvm-rc,
 * for any icon >= 256px or explicitly PNG-compressed smaller ones) ever
 * emits into RT_ICON resources: 8-bit depth, color type 2 (RGB, no
 * alpha channel -> treated as opaque) or 6 (RGBA), non-interlaced.
 * Everything else real-world PNG supports (16-bit depth, palette/
 * grayscale color types, interlacing) never occurs in an .exe's icon
 * resources and is refused outright (returns 0) rather than guessed at
 * -- this is a decoder for one specific, well-defined producer's output,
 * not a general-purpose PNG library.
 *
 * On success: returns 1, *out_pixels is malloc'd RGBA8888 (straight
 * alpha, row-major top-down, caller frees); out_w and out_h are set too.
 * On any failure (wrong subset, truncated/corrupt data, OOM): returns 0
 * and touches none of the output parameters -- callers should fall back
 * to a placeholder icon, exactly like nxgui_pe_icon.c already does for
 * an exe with no icon resource at all. */
int nxgui_png_mini_decode( const unsigned char *data, uint32_t size,
                            uint32_t **out_pixels, int *out_w, int *out_h );

#endif /* WINE_NX_GUI_PNG_MINI_H */
