/* PE resource walking here mirrors source/runtime.c's own conventions
 * for reading structures straight off disk with fopen/fread/fseek
 * (see peek_pe_machine()) -- this never maps or runs the target exe,
 * it only reads header bytes to find and decode one icon.
 *
 * PE resource directories have one well-known quirk this code leans on:
 * IMAGE_RESOURCE_DIRECTORY_ENTRY.OffsetToData (the Type/Name/Language
 * tree links) are offsets relative to the START OF THE RESOURCE
 * DIRECTORY (i.e. relative to .rsrc's own virtual address), while the
 * leaf IMAGE_RESOURCE_DATA_ENTRY.OffsetToData is an image-relative RVA
 * like everywhere else in the PE format. Loading the whole .rsrc
 * section into one buffer up front means the tree-walk offsets are
 * usable as direct buffer indices with zero translation, and the one
 * genuine RVA (the final leaf) only needs a single subtraction against
 * the section's own virtual address -- not a general RVA->file-offset
 * section-table walk, because real-world compilers/linkers always place
 * a PE image's resource *data* inside the .rsrc section itself.
 *
 * NOT YET HARDWARE-VALIDATED -- exercised so far only by eye against
 * the Microsoft PE/COFF spec's resource-directory description and the
 * classic ICONDIR/GRPICONDIRENTRY layout, not yet run against a real
 * Windows exe on device.
 */
#include "nxgui_pe_icon.h"
#include "nxgui_png_mini.h"

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NX_RT_ICON       3
#define NX_RT_GROUP_ICON 14

#pragma pack(push,1)
typedef struct
{
    WORD idReserved, idType, idCount;
} nx_grpicondir_header_t;

typedef struct
{
    BYTE  bWidth, bHeight, bColorCount, bReserved;
    WORD  wPlanes, wBitCount;
    DWORD dwBytesInRes;
    WORD  nID;
} nx_grpicondir_entry_t;
#pragma pack(pop)

/* Finds, within the resource directory at buffer offset `dir_off`, the
 * entry whose numeric id matches `id` (id < 0 means "don't care, just
 * hand back the first entry" -- used at the Name and Language levels,
 * where this code doesn't need a specific name/language, only *an*
 * icon). Returns 1 and sets *out_entry_off (also buffer-relative) on
 * a match. */
static int nx_res_find_entry( const unsigned char *rsrc_buf, uint32_t rsrc_size, uint32_t dir_off, int id, uint32_t *out_entry_off )
{
    IMAGE_RESOURCE_DIRECTORY dirhdr;
    int total, i;

    if ((uint64_t)dir_off + sizeof(dirhdr) > rsrc_size) return 0;
    memcpy( &dirhdr, rsrc_buf + dir_off, sizeof(dirhdr) );
    total = dirhdr.NumberOfNamedEntries + dirhdr.NumberOfIdEntries;

    for (i = 0; i < total; i++)
    {
        uint32_t entry_off = dir_off + (uint32_t)sizeof(IMAGE_RESOURCE_DIRECTORY) +
                              (uint32_t)i * (uint32_t)sizeof(IMAGE_RESOURCE_DIRECTORY_ENTRY);
        IMAGE_RESOURCE_DIRECTORY_ENTRY entry;

        if ((uint64_t)entry_off + sizeof(entry) > rsrc_size) return 0;
        memcpy( &entry, rsrc_buf + entry_off, sizeof(entry) );

        if (id >= 0)
        {
            if (entry.Name & IMAGE_RESOURCE_NAME_IS_STRING) continue;
            if (entry.Id != (WORD)id) continue;
        }
        *out_entry_off = entry_off;
        return 1;
    }
    return 0;
}

/* Walks Type(type_id) -> Name(name_id, or first if <0) -> Language(first)
 * down to the leaf IMAGE_RESOURCE_DATA_ENTRY, returning its buffer offset. */
static int nx_res_lookup_leaf( const unsigned char *rsrc_buf, uint32_t rsrc_size, int type_id, int name_id, uint32_t *out_data_entry_off )
{
    uint32_t off, name_dir_off, lang_dir_off;
    IMAGE_RESOURCE_DIRECTORY_ENTRY entry;

    if (!nx_res_find_entry( rsrc_buf, rsrc_size, 0, type_id, &off )) return 0;
    memcpy( &entry, rsrc_buf + off, sizeof(entry) );
    if (!(entry.OffsetToData & IMAGE_RESOURCE_DATA_IS_DIRECTORY)) return 0;
    name_dir_off = entry.OffsetToData & 0x7fffffff;

    if (!nx_res_find_entry( rsrc_buf, rsrc_size, name_dir_off, name_id, &off )) return 0;
    memcpy( &entry, rsrc_buf + off, sizeof(entry) );
    if (!(entry.OffsetToData & IMAGE_RESOURCE_DATA_IS_DIRECTORY)) return 0;
    lang_dir_off = entry.OffsetToData & 0x7fffffff;

    if (!nx_res_find_entry( rsrc_buf, rsrc_size, lang_dir_off, -1, &off )) return 0;
    memcpy( &entry, rsrc_buf + off, sizeof(entry) );
    if (entry.OffsetToData & IMAGE_RESOURCE_DATA_IS_DIRECTORY) return 0;   /* expected a leaf here */

    *out_data_entry_off = entry.OffsetToData & 0x7fffffff;
    return 1;
}

static uint32_t dib_row_stride( int width, int bpp )
{
    return (uint32_t)(((width * bpp + 31) / 32) * 4);
}

static int dib_and_bit( const unsigned char *and_row, int x )
{
    unsigned char byte = and_row[x / 8];
    return (byte >> (7 - (x % 8))) & 1;
}

/* Classic ICONIMAGE: a BITMAPINFOHEADER (or a V4/V5 superset of one --
 * only the common first-40-byte prefix is ever read) whose biHeight is
 * double the real image height (XOR color plane stacked on an AND
 * 1bpp transparency mask), optional palette for <=8bpp, both planes
 * stored bottom-up with each row padded to a 4-byte boundary -- the
 * same DIB layout a .bmp uses, just packed two deep. */
static int nx_decode_dib_icon( const unsigned char *data, uint32_t size, nxgui_icon_t *out )
{
    BITMAPINFOHEADER bih;
    int width, actual_height, bpp;
    uint32_t palette_count = 0;
    const unsigned char *palette_base = NULL;
    const unsigned char *xor_bits, *and_bits;
    uint32_t xor_stride, and_stride;
    int have_and_mask;
    uint32_t *pixels;
    int x, y;

    if (size < sizeof(bih)) return 0;
    memcpy( &bih, data, sizeof(bih) );
    if (bih.biSize < sizeof(bih)) return 0;   /* rejects BITMAPCOREHEADER (12 bytes) and anything else too small to share our field layout */

    width = bih.biWidth;
    bpp = bih.biBitCount;
    if (bih.biHeight <= 0 || (bih.biHeight % 2) != 0) return 0;
    actual_height = bih.biHeight / 2;

    if (width <= 0 || width > 1024 || actual_height <= 0 || actual_height > 1024) return 0;
    if (bpp != 1 && bpp != 4 && bpp != 8 && bpp != 24 && bpp != 32) return 0;

    if (bpp <= 8)
    {
        palette_count = bih.biClrUsed ? bih.biClrUsed : (1u << bpp);
        if ((uint64_t)bih.biSize + (uint64_t)palette_count * 4 > size) return 0;
        palette_base = data + bih.biSize;
    }

    xor_stride = dib_row_stride( width, bpp );
    and_stride = dib_row_stride( width, 1 );

    xor_bits = data + bih.biSize + (uint64_t)palette_count * 4;
    if ((uint64_t)(xor_bits - data) + (uint64_t)xor_stride * actual_height > size) return 0;
    and_bits = xor_bits + (uint64_t)xor_stride * actual_height;
    have_and_mask = ((uint64_t)(and_bits - data) + (uint64_t)and_stride * actual_height <= size);

    pixels = malloc( (size_t)width * (size_t)actual_height * sizeof(uint32_t) );
    if (!pixels) return 0;

    for (y = 0; y < actual_height; y++)
    {
        /* Bottom-up storage: file row 0 is the BOTTOM of the image. */
        const unsigned char *xor_row = xor_bits + (uint64_t)(actual_height - 1 - y) * xor_stride;
        const unsigned char *and_row = and_bits + (uint64_t)(actual_height - 1 - y) * and_stride;

        for (x = 0; x < width; x++)
        {
            unsigned char r = 0, g = 0, b = 0, a = 255;

            switch (bpp)
            {
            case 32:
            {
                const unsigned char *p = xor_row + x * 4;
                b = p[0]; g = p[1]; r = p[2]; a = p[3];
                /* Plenty of legacy-authored 32bpp icons leave their XOR
                 * alpha byte at 0 throughout (alpha wasn't part of the
                 * original art) -- treat that as "no real alpha data"
                 * and fall back to the AND mask/fully-opaque instead of
                 * rendering the icon fully invisible. */
                if (a == 0) a = have_and_mask ? (dib_and_bit( and_row, x ) ? 0 : 255) : 255;
                break;
            }
            case 24:
            {
                const unsigned char *p = xor_row + x * 3;
                b = p[0]; g = p[1]; r = p[2];
                a = have_and_mask ? (dib_and_bit( and_row, x ) ? 0 : 255) : 255;
                break;
            }
            case 8:
            {
                unsigned char idx = xor_row[x];
                if (idx < palette_count)
                {
                    const unsigned char *pe = palette_base + (uint32_t)idx * 4;
                    b = pe[0]; g = pe[1]; r = pe[2];
                }
                a = have_and_mask ? (dib_and_bit( and_row, x ) ? 0 : 255) : 255;
                break;
            }
            case 4:
            {
                unsigned char byte = xor_row[x / 2];
                unsigned char idx = (x & 1) ? (byte & 0x0f) : (byte >> 4);
                if (idx < palette_count)
                {
                    const unsigned char *pe = palette_base + (uint32_t)idx * 4;
                    b = pe[0]; g = pe[1]; r = pe[2];
                }
                a = have_and_mask ? (dib_and_bit( and_row, x ) ? 0 : 255) : 255;
                break;
            }
            case 1:
            {
                unsigned char byte = xor_row[x / 8];
                unsigned char idx = (byte >> (7 - (x % 8))) & 1;
                if (idx < palette_count)
                {
                    const unsigned char *pe = palette_base + (uint32_t)idx * 4;
                    b = pe[0]; g = pe[1]; r = pe[2];
                }
                a = have_and_mask ? (dib_and_bit( and_row, x ) ? 0 : 255) : 255;
                break;
            }
            }

            pixels[(size_t)y * width + x] = (uint32_t)r | ((uint32_t)g << 8) | ((uint32_t)b << 16) | ((uint32_t)a << 24);
        }
    }

    out->pixels = pixels;
    out->width = width;
    out->height = actual_height;
    return 1;
}

static int nx_decode_icon_image( const unsigned char *data, uint32_t size, nxgui_icon_t *out )
{
    static const unsigned char png_sig[8] = { 0x89,'P','N','G',0x0d,0x0a,0x1a,0x0a };

    if (size >= 8 && memcmp( data, png_sig, 8 ) == 0)
    {
        uint32_t *pixels;
        int w, h;
        if (!nxgui_png_mini_decode( data, size, &pixels, &w, &h )) return 0;
        out->pixels = pixels;
        out->width = w;
        out->height = h;
        return 1;
    }
    return nx_decode_dib_icon( data, size, out );
}

int nxgui_pe_icon_extract( const char *exe_path, nxgui_icon_t *out )
{
    FILE *f;
    IMAGE_DOS_HEADER dos;
    unsigned int pe_sig;
    IMAGE_FILE_HEADER file_header;
    WORD magic;
    IMAGE_DATA_DIRECTORY resource_dir;
    long section_table_off;
    IMAGE_SECTION_HEADER *sections = NULL;
    int i;
    long rsrc_file_off = -1;
    unsigned char *rsrc_buf = NULL;
    uint32_t rsrc_va, rsrc_size;
    uint32_t group_leaf_off, icon_leaf_off;
    IMAGE_RESOURCE_DATA_ENTRY data_entry;
    uint32_t group_data_off;
    const unsigned char *grp;
    nx_grpicondir_header_t grp_hdr;
    const nx_grpicondir_entry_t *entries;
    int count, best_idx;
    uint32_t best_area;
    WORD chosen_id;
    IMAGE_RESOURCE_DATA_ENTRY icon_data_entry;
    uint32_t icon_data_off;
    int rc = 0;

    memset( &resource_dir, 0, sizeof(resource_dir) );
    memset( out, 0, sizeof(*out) );

    f = fopen( exe_path, "rb" );
    if (!f) return 0;

    if (fread( &dos, sizeof(dos), 1, f ) != 1 || dos.e_magic != IMAGE_DOS_SIGNATURE) goto done;
    if (fseek( f, dos.e_lfanew, SEEK_SET ) != 0) goto done;
    if (fread( &pe_sig, sizeof(pe_sig), 1, f ) != 1 || pe_sig != IMAGE_NT_SIGNATURE) goto done;
    if (fread( &file_header, sizeof(file_header), 1, f ) != 1) goto done;

    if (fread( &magic, sizeof(magic), 1, f ) != 1) goto done;
    if (fseek( f, -(long)sizeof(magic), SEEK_CUR ) != 0) goto done;

    if (magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC)
    {
        IMAGE_OPTIONAL_HEADER32 opt;
        if (fread( &opt, sizeof(opt), 1, f ) != 1) goto done;
        if (IMAGE_DIRECTORY_ENTRY_RESOURCE < opt.NumberOfRvaAndSizes)
            resource_dir = opt.DataDirectory[IMAGE_DIRECTORY_ENTRY_RESOURCE];
    }
    else if (magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC)
    {
        IMAGE_OPTIONAL_HEADER64 opt;
        if (fread( &opt, sizeof(opt), 1, f ) != 1) goto done;
        if (IMAGE_DIRECTORY_ENTRY_RESOURCE < opt.NumberOfRvaAndSizes)
            resource_dir = opt.DataDirectory[IMAGE_DIRECTORY_ENTRY_RESOURCE];
    }
    else goto done;

    if (resource_dir.Size == 0 || resource_dir.VirtualAddress == 0) goto done;

    section_table_off = dos.e_lfanew + 4 + (long)sizeof(IMAGE_FILE_HEADER) + file_header.SizeOfOptionalHeader;
    if (file_header.NumberOfSections <= 0 || file_header.NumberOfSections > 96) goto done;   /* sane upper bound against a corrupt header */
    if (fseek( f, section_table_off, SEEK_SET ) != 0) goto done;

    sections = malloc( (size_t)sizeof(IMAGE_SECTION_HEADER) * file_header.NumberOfSections );
    if (!sections) goto done;
    if (fread( sections, sizeof(IMAGE_SECTION_HEADER), file_header.NumberOfSections, f ) != (size_t)file_header.NumberOfSections)
        goto done;

    for (i = 0; i < file_header.NumberOfSections; i++)
    {
        uint32_t va = sections[i].VirtualAddress;
        uint32_t vsize = sections[i].Misc.VirtualSize ? sections[i].Misc.VirtualSize : sections[i].SizeOfRawData;
        if (resource_dir.VirtualAddress >= va && resource_dir.VirtualAddress < va + vsize)
        {
            rsrc_file_off = (long)sections[i].PointerToRawData + (long)(resource_dir.VirtualAddress - va);
            break;
        }
    }
    if (rsrc_file_off < 0) goto done;

    rsrc_va = resource_dir.VirtualAddress;
    rsrc_size = resource_dir.Size;
    rsrc_buf = calloc( 1, rsrc_size );
    if (!rsrc_buf) goto done;
    if (fseek( f, rsrc_file_off, SEEK_SET ) != 0) goto done;
    fread( rsrc_buf, 1, rsrc_size, f );   /* short read tolerated -- unread tail stays zero and just fails whatever offset lands there */

    if (!nx_res_lookup_leaf( rsrc_buf, rsrc_size, NX_RT_GROUP_ICON, -1, &group_leaf_off )) goto done;
    memcpy( &data_entry, rsrc_buf + group_leaf_off, sizeof(data_entry) );
    if (data_entry.OffsetToData < rsrc_va) goto done;
    group_data_off = data_entry.OffsetToData - rsrc_va;
    if ((uint64_t)group_data_off + data_entry.Size > rsrc_size || data_entry.Size < sizeof(grp_hdr)) goto done;

    grp = rsrc_buf + group_data_off;
    memcpy( &grp_hdr, grp, sizeof(grp_hdr) );
    count = grp_hdr.idCount;
    if (count <= 0 || (uint64_t)sizeof(grp_hdr) + (uint64_t)count * sizeof(nx_grpicondir_entry_t) > data_entry.Size) goto done;
    entries = (const nx_grpicondir_entry_t *)(grp + sizeof(grp_hdr));

    best_idx = -1;
    best_area = 0;
    for (i = 0; i < count; i++)
    {
        /* 0 in either byte is the classic ICO convention for "256"
         * (doesn't fit in a BYTE). */
        int w = entries[i].bWidth ? entries[i].bWidth : 256;
        int h = entries[i].bHeight ? entries[i].bHeight : 256;
        uint32_t area = (uint32_t)w * (uint32_t)h;
        if (area > best_area) { best_area = area; best_idx = i; }
    }
    if (best_idx < 0) goto done;
    chosen_id = entries[best_idx].nID;

    if (!nx_res_lookup_leaf( rsrc_buf, rsrc_size, NX_RT_ICON, (int)chosen_id, &icon_leaf_off )) goto done;
    memcpy( &icon_data_entry, rsrc_buf + icon_leaf_off, sizeof(icon_data_entry) );
    if (icon_data_entry.OffsetToData < rsrc_va) goto done;
    icon_data_off = icon_data_entry.OffsetToData - rsrc_va;
    if ((uint64_t)icon_data_off + icon_data_entry.Size > rsrc_size) goto done;

    rc = nx_decode_icon_image( rsrc_buf + icon_data_off, icon_data_entry.Size, out );

done:
    free( sections );
    free( rsrc_buf );
    fclose( f );
    return rc;
}
