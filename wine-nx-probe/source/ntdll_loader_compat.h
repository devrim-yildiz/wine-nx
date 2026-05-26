#ifndef WINE_NX_NTDLL_LOADER_COMPAT_H
#define WINE_NX_NTDLL_LOADER_COMPAT_H

#include <strings.h>
#include <wchar.h>

#define _stricmp strcasecmp
#define _wcsicmp wine_nx_wcsicmp
#define wcsicmp wine_nx_wcsicmp
#define wcsnicmp wine_nx_wcsnicmp

/*
 * The Switch toolchain (newlib / devkitA64) uses 4-byte wchar_t in its libc,
 * but Wine compiles with -fshort-wchar which makes wchar_t 2 bytes.
 * All libc wcs* functions silently break under this mismatch — they stride
 * 4 bytes per character through 2-byte data, producing garbage.
 * We must override every wcs* function used in loader.c.
 */
#define wcslen   wine_nx_wcslen
#define wcsrchr  wine_nx_wcsrchr
#define wcschr   wine_nx_wcschr
#define wcscmp   wine_nx_wcscmp
#define wcsncmp  wine_nx_wcsncmp
#define wcscpy   wine_nx_wcscpy
#define wcscat   wine_nx_wcscat
#define wcsstr   wine_nx_wcsstr
#define swprintf wine_nx_swprintf

#define __wine_syscall_dispatcher wine_nx_syscall_dispatcher_ptr
#define __wine_unix_call_dispatcher wine_nx_unix_call_dispatcher_ptr
#define __wine_unix_spawnvp wine_nx_pe_unix_spawnvp
#define wine_server_call wine_nx_pe_wine_server_call
#define wine_server_fd_to_handle wine_nx_pe_wine_server_fd_to_handle
#define wine_server_handle_to_fd wine_nx_pe_wine_server_handle_to_fd

static inline wchar_t wine_nx_fold_wchar( wchar_t ch )
{
    if (ch >= L'A' && ch <= L'Z') return ch - L'A' + L'a';
    return ch;
}

static inline int wine_nx_wcsicmp( const wchar_t *left, const wchar_t *right )
{
    wchar_t left_ch, right_ch;

    do
    {
        left_ch = wine_nx_fold_wchar( *left++ );
        right_ch = wine_nx_fold_wchar( *right++ );
        if (left_ch != right_ch) return left_ch - right_ch;
    } while (left_ch);
    return 0;
}

static inline int wine_nx_wcsnicmp( const wchar_t *left, const wchar_t *right, int count )
{
    wchar_t left_ch, right_ch;

    if (count <= 0) return 0;
    do
    {
        left_ch = wine_nx_fold_wchar( *left++ );
        right_ch = wine_nx_fold_wchar( *right++ );
        if (left_ch != right_ch) return left_ch - right_ch;
    } while (left_ch && --count);
    return 0;
}

static inline size_t wine_nx_wcslen( const wchar_t *s )
{
    const wchar_t *p = s;
    while (*p) p++;
    return p - s;
}

static inline wchar_t *wine_nx_wcsrchr( const wchar_t *s, wchar_t ch )
{
    const wchar_t *ret = 0;
    for (; *s; s++) if (*s == ch) ret = s;
    return (wchar_t *)ret;
}

static inline wchar_t *wine_nx_wcschr( const wchar_t *s, wchar_t ch )
{
    for (; *s; s++) if (*s == ch) return (wchar_t *)s;
    return 0;
}

static inline int wine_nx_wcscmp( const wchar_t *a, const wchar_t *b )
{
    for (; *a && *a == *b; a++, b++);
    return (int)*a - (int)*b;
}

static inline int wine_nx_wcsncmp( const wchar_t *a, const wchar_t *b, size_t n )
{
    for (; n && *a && *a == *b; a++, b++, n--);
    return n ? (int)*a - (int)*b : 0;
}

static inline wchar_t *wine_nx_wcscpy( wchar_t *dst, const wchar_t *src )
{
    wchar_t *ret = dst;
    while ((*dst++ = *src++));
    return ret;
}

static inline wchar_t *wine_nx_wcscat( wchar_t *dst, const wchar_t *src )
{
    wchar_t *ret = dst;
    while (*dst) dst++;
    while ((*dst++ = *src++));
    return ret;
}

static inline wchar_t *wine_nx_wcsstr( const wchar_t *haystack, const wchar_t *needle )
{
    size_t nlen = wine_nx_wcslen( needle );
    if (!nlen) return (wchar_t *)haystack;
    for (; *haystack; haystack++)
        if (!wine_nx_wcsncmp( haystack, needle, nlen )) return (wchar_t *)haystack;
    return 0;
}

#include <stdarg.h>
#include <stdio.h>

static inline int wine_nx_swprintf( wchar_t *dst, size_t count, const wchar_t *fmt, ... )
{
    /* Minimal implementation: only handles the specific "%u" pattern used in loader.c
       for building WINEDLLDIR0, WINEDLLDIR1 etc. variable names. */
    va_list args;
    size_t fi = 0, di = 0;
    va_start( args, fmt );
    while (fmt[fi] && di < count - 1)
    {
        if (fmt[fi] == L'%' && fmt[fi + 1] == L'u')
        {
            unsigned int val = va_arg( args, unsigned int );
            char tmp[16];
            int len = snprintf( tmp, sizeof(tmp), "%u", val );
            int i;
            for (i = 0; i < len && di < count - 1; i++)
                dst[di++] = (wchar_t)(unsigned char)tmp[i];
            fi += 2;
        }
        else if (fmt[fi] == L'%' && fmt[fi + 1] == L's')
        {
            const wchar_t *s = va_arg( args, const wchar_t * );
            while (*s && di < count - 1) dst[di++] = *s++;
            fi += 2;
        }
        else dst[di++] = fmt[fi++];
    }
    va_end( args );
    if (di < count) dst[di] = 0;
    return (int)di;
}

#endif

