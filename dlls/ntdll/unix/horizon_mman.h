/*
 * Minimal mmap definitions for Horizon.
 *
 * Copyright 2026 Diogo Silva
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#ifndef __NTDLL_UNIX_HORIZON_MMAN_H
#define __NTDLL_UNIX_HORIZON_MMAN_H

#include <stddef.h>
#include <sys/types.h>

#define PROT_NONE  0x00
#define PROT_READ  0x01
#define PROT_WRITE 0x02
#define PROT_EXEC  0x04

#define MAP_SHARED    0x0001
#define MAP_PRIVATE   0x0002
#define MAP_FIXED     0x0010
#define MAP_ANON      0x1000
#define MAP_ANONYMOUS MAP_ANON
#define MAP_NORESERVE 0x4000
#define MAP_FIXED_NOREPLACE 0x100000
#define MAP_TRYFIXED        0x200000
#define MAP_EXCL            0x400000
#define MAP_FAILED    ((void *)-1)

#define MADV_DONTNEED    4
#define MADV_WILLNEED    3
#define MADV_NOHUGEPAGE  15

extern void *horizon_mmap( void *start, size_t size, int prot, int flags, int fd, off_t offset );
extern int horizon_munmap( void *start, size_t size );
extern int horizon_mprotect( void *start, size_t size, int prot );
extern int horizon_madvise( void *start, size_t size, int advice );

#endif /* __NTDLL_UNIX_HORIZON_MMAN_H */
