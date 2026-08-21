#ifndef WINE_NX_GUI_FONT_H
#define WINE_NX_GUI_FONT_H

/* Returns the 5-bit row pattern (bit4=leftmost pixel) for glyph `c`,
 * `row` in [0,6]. Unmapped characters and out-of-range rows both return
 * 0 (blank) rather than erroring -- text rendering degrades to a gap,
 * never a fault, for any character this font doesn't have. */
int nxgui_font_glyph_row( char c, int row );

#define NXGUI_FONT_GLYPH_W 5
#define NXGUI_FONT_GLYPH_H 7

#endif /* WINE_NX_GUI_FONT_H */
