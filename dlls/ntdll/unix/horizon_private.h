/*
 * ntdll Horizon private interface
 *
 * Copyright 2026 Diogo Silva
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#ifndef __NTDLL_UNIX_HORIZON_PRIVATE_H
#define __NTDLL_UNIX_HORIZON_PRIVATE_H

#ifdef __SWITCH__
#include <stddef.h>

extern ULONG_PTR horizon_get_system_affinity_mask(void);
extern unsigned int horizon_get_processor_count(void);
extern void horizon_get_address_space_limits( void **start, void **limit );
extern void horizon_trace( const char *fmt, ... );
extern void horizon_pin_current_thread( ULONG_PTR requested_mask );
extern void *horizon_anon_mmap_fixed( void *start, size_t size, int prot, int flags );
extern void *horizon_anon_mmap_alloc( size_t size, int prot );
extern int horizon_pipe( int fd[2] );
extern void horizon_server_queue_fd( int fd, unsigned int handle );
extern int horizon_server_take_client_fd( unsigned int *handle );
extern unsigned int horizon_server_protocol_version(void);
extern int horizon_server_connect(void);
extern void horizon_server_send_fd( int fd );
extern int horizon_server_receive_fd( unsigned int *handle );
#endif

#endif /* __NTDLL_UNIX_HORIZON_PRIVATE_H */
