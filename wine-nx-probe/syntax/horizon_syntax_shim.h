/*
 * Standalone syntax shim for dlls/ntdll/unix/horizon.c.
 *
 * This is intentionally not a Wine runtime header. It only provides enough
 * Wine-side types/macros to compile the Horizon backend against real libnx
 * headers before a full Wine build tree exists.
 */

#ifndef HORIZON_SYNTAX_SHIM_H
#define HORIZON_SYNTAX_SHIM_H

#include <errno.h>
#include <fcntl.h>
#include <malloc.h>
#include <pthread.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <switch.h>

typedef uintptr_t ULONG_PTR;
typedef uint64_t DWORD64;
typedef int32_t LONG;
typedef int BOOL;
typedef int NTSTATUS;

typedef struct _EXCEPTION_RECORD
{
    NTSTATUS ExceptionCode;
    unsigned int ExceptionFlags;
    struct _EXCEPTION_RECORD *ExceptionRecord;
    void *ExceptionAddress;
    unsigned int NumberParameters;
    ULONG_PTR ExceptionInformation[15];
} EXCEPTION_RECORD;

#define STATUS_SUCCESS ((NTSTATUS)0)
#define STATUS_ACCESS_VIOLATION ((NTSTATUS)0xc0000005)
#define EXCEPTION_READ_FAULT 0
#define EXCEPTION_WRITE_FAULT 1
#define EXCEPTION_EXECUTE_FAULT 8

#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif

#ifndef INVALID_HANDLE
#define INVALID_HANDLE ((Handle)0)
#endif

#ifndef max
#define max(a,b) (((a) > (b)) ? (a) : (b))
#endif

#ifndef min
#define min(a,b) (((a) < (b)) ? (a) : (b))
#endif

#define WINE_DEFAULT_DEBUG_CHANNEL(channel)
#define TRACE(...) ((void)0)
#define WARN(...)  ((void)0)
#define ERR(...)   ((void)0)

static inline LONG InterlockedIncrement( LONG *value )
{
    return __sync_add_and_fetch( value, 1 );
}

static inline NTSTATUS virtual_handle_fault( EXCEPTION_RECORD *rec, void *stack )
{
    (void)rec;
    (void)stack;
    return STATUS_ACCESS_VIOLATION;
}

#include "horizon_mman.h"
#include "horizon_private.h"

#endif /* HORIZON_SYNTAX_SHIM_H */
