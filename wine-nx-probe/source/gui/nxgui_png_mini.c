/* See nxgui_png_mini.h for scope (8-bit RGB/RGBA, non-interlaced only).
 * Inflate itself is zlib's (portlibs/switch already ships libz.a, no
 * need to vendor or reimplement DEFLATE) -- everything else here
 * (chunk walking, IHDR validation, the four PNG scanline filters) is
 * plain, textbook PNG-spec code with no external dependency. */
#include "nxgui_png_mini.h"

#include <zlib.h>
#include <stdlib.h>
#include <string.h>

static uint32_t read_be32( const unsigned char *p )
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static unsigned char paeth_predictor( int a, int b, int c )
{
    int p = a + b - c;
    int pa = p > a ? p - a : a - p;
    int pb = p > b ? p - b : b - p;
    int pc = p > c ? p - c : c - p;
    if (pa <= pb && pa <= pc) return (unsigned char)a;
    if (pb <= pc) return (unsigned char)b;
    return (unsigned char)c;
}

/* Undoes PNG's per-scanline filtering in place: `raw` is
 * (1 + width*bpp) * height bytes on input (each row's leading filter-type
 * byte still present), and becomes tightly packed width*bpp*height pixel
 * bytes on output (filter bytes squeezed out as each row is resolved). */
static void unfilter( unsigned char *raw, int width, int height, int bpp )
{
    int stride = width * bpp;
    unsigned char *prev_row = NULL;
    int y;

    for (y = 0; y < height; y++)
    {
        unsigned char *src_row = raw + (size_t)y * (stride + 1);
        unsigned char filter_type = src_row[0];
        unsigned char *cur = src_row + 1;   /* decode in place, just past the filter byte */
        int x;

        for (x = 0; x < stride; x++)
        {
            int a = (x >= bpp) ? cur[x - bpp] : 0;
            int b = prev_row ? prev_row[x] : 0;
            int c = (prev_row && x >= bpp) ? prev_row[x - bpp] : 0;
            unsigned char recon;

            switch (filter_type)
            {
            case 0: recon = cur[x]; break;
            case 1: recon = (unsigned char)(cur[x] + a); break;
            case 2: recon = (unsigned char)(cur[x] + b); break;
            case 3: recon = (unsigned char)(cur[x] + ((a + b) / 2)); break;
            case 4: recon = (unsigned char)(cur[x] + paeth_predictor( a, b, c )); break;
            default: recon = cur[x]; break;   /* unknown filter -- best effort, don't fault */
            }
            cur[x] = recon;
        }
        /* Squeeze this row down to the front of the buffer (over the
         * filter bytes already-consumed rows left behind), so the whole
         * thing reads out contiguously as plain width*bpp*height bytes
         * once every row's done. */
        memmove( raw + (size_t)y * stride, cur, (size_t)stride );
        prev_row = raw + (size_t)y * stride;
    }
}

int nxgui_png_mini_decode( const unsigned char *data, uint32_t size,
                           uint32_t **out_pixels, int *out_w, int *out_h )
{
    static const unsigned char sig[8] = { 0x89,'P','N','G',0x0d,0x0a,0x1a,0x0a };
    uint32_t off;
    int width = 0, height = 0, bit_depth = 0, color_type = 0, interlace = 0;
    unsigned char *idat = NULL;
    uint32_t idat_len = 0, idat_cap = 0;
    int bpp, stride, raw_size;
    unsigned char *raw = NULL;
    uLongf raw_size_ul;
    uint32_t *pixels;
    int x, y;
    int ok = 0;

    if (size < 8 || memcmp( data, sig, 8 ) != 0) return 0;

    off = 8;
    while ((uint64_t)off + 8 <= size)
    {
        uint32_t chunk_len = read_be32( data + off );
        const unsigned char *chunk_type = data + off + 4;
        const unsigned char *chunk_data = data + off + 8;

        /* 64-bit throughout: chunk_len comes straight from file bytes, so
         * an off+8+chunk_len+4 sum computed in uint32_t could wrap around
         * and pass a bounds check it should have failed. */
        if ((uint64_t)off + 8 + (uint64_t)chunk_len + 4 > size) break;

        if (memcmp( chunk_type, "IHDR", 4 ) == 0 && chunk_len >= 13)
        {
            width      = (int)read_be32( chunk_data + 0 );
            height     = (int)read_be32( chunk_data + 4 );
            bit_depth  = chunk_data[8];
            color_type = chunk_data[9];
            interlace  = chunk_data[12];
        }
        else if (memcmp( chunk_type, "IDAT", 4 ) == 0)
        {
            if (idat_len + chunk_len > idat_cap)
            {
                uint32_t new_cap = (idat_cap ? idat_cap * 2 : 4096);
                unsigned char *grown;
                while (new_cap < idat_len + chunk_len) new_cap *= 2;
                grown = realloc( idat, new_cap );
                if (!grown) goto done;
                idat = grown;
                idat_cap = new_cap;
            }
            memcpy( idat + idat_len, chunk_data, chunk_len );
            idat_len += chunk_len;
        }
        else if (memcmp( chunk_type, "IEND", 4 ) == 0)
        {
            break;
        }

        off += 8 + chunk_len + 4;
    }

    if (width <= 0 || height <= 0 || bit_depth != 8 || interlace != 0 || !idat) goto done;
    if (color_type != 2 && color_type != 6) goto done;   /* RGB or RGBA only, see header comment */

    bpp = (color_type == 6) ? 4 : 3;
    stride = width * bpp;
    raw_size = (stride + 1) * height;
    if (raw_size <= 0) goto done;

    raw = malloc( (size_t)raw_size );
    if (!raw) goto done;

    raw_size_ul = (uLongf)raw_size;
    if (uncompress( raw, &raw_size_ul, idat, idat_len ) != Z_OK || (int)raw_size_ul != raw_size)
        goto done;

    unfilter( raw, width, height, bpp );

    pixels = malloc( (size_t)width * (size_t)height * sizeof(uint32_t) );
    if (!pixels) goto done;

    for (y = 0; y < height; y++)
    {
        const unsigned char *row = raw + (size_t)y * stride;
        for (x = 0; x < width; x++)
        {
            const unsigned char *px = row + x * bpp;
            unsigned char r = px[0], g = px[1], b = px[2];
            unsigned char a = (bpp == 4) ? px[3] : 0xff;
            pixels[(size_t)y * width + x] = (uint32_t)r | ((uint32_t)g << 8) | ((uint32_t)b << 16) | ((uint32_t)a << 24);
        }
    }

    *out_pixels = pixels;
    *out_w = width;
    *out_h = height;
    ok = 1;

done:
    free( idat );
    free( raw );
    return ok;
}
