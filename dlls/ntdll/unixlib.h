/*
 * Ntdll Unix interface
 *
 * Copyright (C) 2020 Alexandre Julliard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#ifndef __NTDLL_UNIXLIB_H
#define __NTDLL_UNIXLIB_H

#include "wine/unixlib.h"

struct _DISPATCHER_CONTEXT;

struct wine_dbg_write_params
{
    const char  *str;
    unsigned int len;
};

struct wine_server_fd_to_handle_params
{
    int          fd;
    unsigned int access;
    unsigned int attributes;
    HANDLE      *handle;
};

struct wine_server_handle_to_fd_params
{
    HANDLE        handle;
    unsigned int  access;
    int          *unix_fd;
    unsigned int *options;
};

struct wine_spawnvp_params
{
    char       **argv;
    int          wait;
};

struct load_so_dll_params
{
    UNICODE_STRING              nt_name;
    void                      **module;
};

struct unwind_builtin_dll_params
{
    ULONG                       type;
    struct _DISPATCHER_CONTEXT *dispatch;
    CONTEXT                    *context;
};

struct wine_get_unix_env_params
{
    const char *name;
    char *val;
    unsigned int buffer_len;
};

struct wine_set_unix_env_params
{
    const char *name;
    const char *val;
};

struct wine_dbg_ftrace_params
{
    char *str;
    unsigned int len;
    unsigned int ctx;
};


struct steamclient_setup_trampolines_params
{
    HMODULE src_mod;
    HMODULE tgt_mod;
};

struct debugstr_pc_args
{
    void *pc;
    char *buffer;
    unsigned int size;
};

struct compat_wine_nt_to_unix_file_name_params
{
    const OBJECT_ATTRIBUTES *attr;
    char *nameA;
    ULONG *size;
    unsigned int disposition;
};

/* WowBox64 bridge (see dlls/ntdll/loader.c's wine_nx_jit_allocate/free and
 * dlls/ntdll/unix/horizon.c's unixcall_wine_nx_jit_allocate/free): Box64's
 * WOW64 CPU backend (wowbox64.dll) can't link libnx directly -- it's built
 * via aarch64-w64-mingw32-clang (Windows PE/COFF), while libnx is built for
 * aarch64-none-elf (ELF/newlib), confirmed link-incompatible by an actual
 * build attempt. These two calls let it reach the native, ELF-side
 * jitCreate()-based dual-alias allocator through this project's existing
 * __wine_unix_call bridge instead. */
struct wine_nx_jit_allocate_params
{
    SIZE_T size;
    PVOID *rx_addr;
    PVOID *rw_addr;
};

struct wine_nx_jit_free_params
{
    PVOID  rw_addr;
    SIZE_T size;
};

enum ntdll_unix_funcs
{
    unix_load_so_dll,
    unix_unwind_builtin_dll,
    unix_wine_dbg_write,
    unix_wine_server_call,
    unix_wine_server_fd_to_handle,
    unix_wine_server_handle_to_fd,
    unix_wine_spawnvp,
    unix_system_time_precise,
    unix___wine_get_unix_env,
    unix___wine_set_unix_env,
    unix_wine_dbg_ftrace,
    unix_steamclient_setup_trampolines,
    unix_debugstr_pc,
    unix_compat_wine_nt_to_unix_file_name,
    unix_wine_nx_jit_allocate,
    unix_wine_nx_jit_free,
};

extern unixlib_handle_t __wine_unixlib_handle;

#define WINE_BACKTRACE_LOG_ON() WARN_ON(seh)

#define WINE_BACKTRACE_LOG(args...) do { \
        WARN_(seh)("backtrace: " args); \
    } while (0)

#endif /* __NTDLL_UNIXLIB_H */
