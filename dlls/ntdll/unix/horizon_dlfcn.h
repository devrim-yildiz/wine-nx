/*
 * Minimal dlfcn fallback for Horizon.
 *
 * Copyright 2026 Diogo Silva
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#ifndef __NTDLL_UNIX_HORIZON_DLFCN_H
#define __NTDLL_UNIX_HORIZON_DLFCN_H

#include <errno.h>

#define RTLD_NOW     0x0002
#define RTLD_GLOBAL  0x0100
#define RTLD_DEFAULT ((void *)0)

typedef struct
{
    const char *dli_fname;
    void       *dli_fbase;
    const char *dli_sname;
    void       *dli_saddr;
} Dl_info;

static inline void *horizon_dlopen( const char *path, int flags )
{
    (void)path;
    (void)flags;
    errno = ENOSYS;
    return NULL;
}

static inline void *horizon_dlsym( void *handle, const char *symbol )
{
    (void)handle;
    (void)symbol;
    errno = ENOSYS;
    return NULL;
}

static inline int horizon_dlclose( void *handle )
{
    (void)handle;
    return 0;
}

static inline const char *horizon_dlerror(void)
{
    return "dlopen is not available on Horizon";
}

static inline int horizon_dladdr( const void *addr, Dl_info *info )
{
    (void)addr;
    (void)info;
    errno = ENOSYS;
    return 0;
}

#define dlopen  horizon_dlopen
#define dlsym   horizon_dlsym
#define dlclose horizon_dlclose
#define dlerror horizon_dlerror
#define dladdr  horizon_dladdr

#endif /* __NTDLL_UNIX_HORIZON_DLFCN_H */
