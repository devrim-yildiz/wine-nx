/*
 * ntdll Horizon host support
 *
 * Copyright 2026 Diogo Silva
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#if 0
#pragma makedep unix
#endif

#include "config.h"

#ifdef __SWITCH__

#ifdef HORIZON_STANDALONE_SYNTAX
#include "horizon_syntax_shim.h"
#else

#include <errno.h>
#include <dirent.h>
#include <fcntl.h>
#include <malloc.h>
#include <poll.h>
#include <pthread.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/stat.h>
#include <unistd.h>

#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "windef.h"
#include "winbase.h"
#include "wine/debug.h"
#include "unix_private.h"
#include "horizon_mman.h"
#include "horizon_private.h"

#include <switch.h>

#endif /* HORIZON_STANDALONE_SYNTAX */

#include <errno.h>
#include <dirent.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

#include <sys/iosupport.h>

WINE_DEFAULT_DEBUG_CHANNEL(horizon);

#ifndef SERVER_PROTOCOL_VERSION
#define SERVER_PROTOCOL_VERSION 931
#endif

#if defined(__aarch64__) && !defined(HORIZON_NO_LIBNX_EXCEPTION_HANDLER)
typedef char horizon_exception_dump_cpu_gprs_offset[
    offsetof(ThreadExceptionDump, cpu_gprs) == 16 ? 1 : -1];
typedef char horizon_exception_dump_fp_offset[
    offsetof(ThreadExceptionDump, fp) == 248 ? 1 : -1];
typedef char horizon_exception_dump_pc_offset[
    offsetof(ThreadExceptionDump, pc) == 272 ? 1 : -1];
typedef char horizon_exception_dump_fpu_gprs_offset[
    offsetof(ThreadExceptionDump, fpu_gprs) == 288 ? 1 : -1];
typedef char horizon_exception_dump_pstate_offset[
    offsetof(ThreadExceptionDump, pstate) == 800 ? 1 : -1];

static void horizon_restore_exception_context( ThreadExceptionDump *ctx ) __attribute__((noreturn));

static void horizon_restore_exception_context( ThreadExceptionDump *ctx )
{
    __asm__ __volatile__(
        "mov x21, %0\n"
        "ldp q0,  q1,  [x21, #288]\n"
        "ldp q2,  q3,  [x21, #320]\n"
        "ldp q4,  q5,  [x21, #352]\n"
        "ldp q6,  q7,  [x21, #384]\n"
        "ldp q8,  q9,  [x21, #416]\n"
        "ldp q10, q11, [x21, #448]\n"
        "ldp q12, q13, [x21, #480]\n"
        "ldp q14, q15, [x21, #512]\n"
        "ldp q16, q17, [x21, #544]\n"
        "ldp q18, q19, [x21, #576]\n"
        "ldp q20, q21, [x21, #608]\n"
        "ldp q22, q23, [x21, #640]\n"
        "ldp q24, q25, [x21, #672]\n"
        "ldp q26, q27, [x21, #704]\n"
        "ldp q28, q29, [x21, #736]\n"
        "ldp q30, q31, [x21, #768]\n"
        "ldr w16, [x21, #800]\n"
        "msr nzcv, x16\n"
        "ldr x16, [x21, #264]\n"
        "ldr x17, [x21, #272]\n"
        "str x17, [x16, #-16]!\n"
        "mov x17, x16\n"
        "ldr x30, [x21, #256]\n"
        "ldr x29, [x21, #248]\n"
        "ldp x0,  x1,  [x21, #16]\n"
        "ldp x2,  x3,  [x21, #32]\n"
        "ldp x4,  x5,  [x21, #48]\n"
        "ldp x6,  x7,  [x21, #64]\n"
        "ldp x8,  x9,  [x21, #80]\n"
        "ldp x10, x11, [x21, #96]\n"
        "ldp x12, x13, [x21, #112]\n"
        "ldp x14, x15, [x21, #128]\n"
        "ldr x16, [x21, #144]\n"
        "ldp x18, x19, [x21, #160]\n"
        "ldr x20, [x21, #176]\n"
        "ldp x22, x23, [x21, #192]\n"
        "ldp x24, x25, [x21, #208]\n"
        "ldp x26, x27, [x21, #224]\n"
        "ldr x28, [x21, #240]\n"
        "mov sp, x17\n"
        "ldr x21, [x21, #184]\n"
        "ldr x17, [sp], #16\n"
        "br x17\n"
        :
        : "r"(ctx)
        : "memory");

    __builtin_unreachable();
}
#endif

/* __nx_exception_stack / __nx_exception_stack_size are provided by each
 * executable's top-level source (runtime.c, the smoke main.c files, etc.).
 * We deliberately do NOT define them here to avoid multiple-definition link
 * errors. */

static ULONG_PTR cached_affinity_mask;
static LONG next_core_index;
static pthread_mutex_t mapping_mutex = PTHREAD_MUTEX_INITIALIZER;

#define HORIZON_PIPE_BUFFER_SIZE 0x10000

struct horizon_pipe
{
    pthread_mutex_t mutex;
    pthread_cond_t can_read;
    pthread_cond_t can_write;
    unsigned int refs;
    int read_open;
    int write_open;
    size_t head;
    size_t tail;
    size_t used;
    unsigned char buffer[HORIZON_PIPE_BUFFER_SIZE];
};

struct horizon_pipe_file
{
    struct horizon_pipe *pipe;
    int write_end;
};

struct horizon_fd_message
{
    int fd;
    unsigned int handle;
    struct horizon_fd_message *next;
};

struct horizon_fd_queue
{
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    struct horizon_fd_message *head;
    struct horizon_fd_message *tail;
};

#define HORIZON_SERVER_FIXED_MESSAGE_SIZE 64
#define HORIZON_REQ_NEW_THREAD 2
#define HORIZON_REQ_INIT_PROCESS_DONE 4
#define HORIZON_REQ_INIT_FIRST_THREAD 5
#define HORIZON_REQ_INIT_THREAD 6
#define HORIZON_REQ_SUSPEND_THREAD 17
#define HORIZON_REQ_RESUME_THREAD 18
#define HORIZON_REQ_CLOSE_HANDLE 21
#define HORIZON_REQ_SET_HANDLE_INFO 22
#define HORIZON_REQ_DUP_HANDLE 23
#define HORIZON_REQ_ALLOCATE_RESERVE_OBJECT 24
#define HORIZON_REQ_COMPARE_OBJECTS 25
#define HORIZON_REQ_SET_OBJECT_PERMANENCE 26
#define HORIZON_REQ_OPEN_PROCESS 27
#define HORIZON_REQ_OPEN_THREAD 28
#define HORIZON_REQ_SELECT 29
#define HORIZON_REQ_CREATE_EVENT 30
#define HORIZON_REQ_EVENT_OP 31
#define HORIZON_REQ_QUERY_EVENT 32
#define HORIZON_REQ_OPEN_EVENT 33
#define HORIZON_REQ_CREATE_KEYED_EVENT 34
#define HORIZON_REQ_OPEN_KEYED_EVENT 35
#define HORIZON_REQ_CREATE_MUTEX 36
#define HORIZON_REQ_RELEASE_MUTEX 37
#define HORIZON_REQ_OPEN_MUTEX 38
#define HORIZON_REQ_QUERY_MUTEX 39
#define HORIZON_REQ_CREATE_SEMAPHORE 40
#define HORIZON_REQ_RELEASE_SEMAPHORE 41
#define HORIZON_REQ_QUERY_SEMAPHORE 42
#define HORIZON_REQ_OPEN_SEMAPHORE 43
#define HORIZON_REQ_CREATE_FILE 44
#define HORIZON_REQ_OPEN_FILE_OBJECT 45
#define HORIZON_REQ_GET_HANDLE_FD 48
#define HORIZON_REQ_RECV_SOCKET 55
#define HORIZON_REQ_SEND_SOCKET 56
#define HORIZON_REQ_SOCKET_GET_EVENTS 57
#define HORIZON_REQ_QUERY_DIRECTORY_FILE 244
#define HORIZON_REQ_SET_ASYNC_DIRECT_RESULT 137
#define HORIZON_REQ_IOCTL 140
#define HORIZON_REQ_CREATE_MAPPING 63
#define HORIZON_REQ_OPEN_MAPPING 64
#define HORIZON_REQ_GET_MAPPING_INFO 65
#define HORIZON_REQ_GET_IMAGE_MAP_ADDRESS 66
#define HORIZON_REQ_MAP_VIEW 67
#define HORIZON_REQ_MAP_IMAGE_VIEW 68
#define HORIZON_REQ_UNMAP_VIEW 71
#define HORIZON_REQ_CREATE_TIMER 101
#define HORIZON_REQ_OPEN_TIMER 102
#define HORIZON_REQ_SET_TIMER 103
#define HORIZON_REQ_CANCEL_TIMER 104
#define HORIZON_REQ_GET_TIMER_INFO 105
#define HORIZON_REQ_ADD_ATOM 108
#define HORIZON_REQ_FIND_ATOM 110
#define HORIZON_REQ_SEND_HARDWARE_MESSAGE 122
#define HORIZON_REQ_GET_MESSAGE 124
#define HORIZON_REQ_ACCEPT_HARDWARE_MESSAGE 126
#define HORIZON_REQ_CREATE_WINDOW 144
#define HORIZON_REQ_DESTROY_WINDOW 145
#define HORIZON_REQ_GET_DESKTOP_WINDOW 146
#define HORIZON_REQ_SET_WINDOW_OWNER 147
#define HORIZON_REQ_GET_WINDOW_INFO 148
#define HORIZON_REQ_INIT_WINDOW_INFO 149
#define HORIZON_REQ_SET_WINDOW_INFO 150
#define HORIZON_REQ_GET_WINDOW_CHILDREN_FROM_POINT 155
#define HORIZON_REQ_GET_WINDOW_TREE 156
#define HORIZON_REQ_SET_WINDOW_POS 157
#define HORIZON_REQ_GET_WINDOW_RECTANGLES 158
#define HORIZON_REQ_GET_VISIBLE_REGION 162
#define HORIZON_REQ_GET_UPDATE_REGION 165
#define HORIZON_REQ_UPDATE_WINDOW_ZORDER 166
#define HORIZON_REQ_REDRAW_WINDOW 167
#define HORIZON_REQ_SET_WINDOW_PROPERTY 168
#define HORIZON_REQ_REMOVE_WINDOW_PROPERTY 169
#define HORIZON_REQ_GET_WINDOW_PROPERTY 170
#define HORIZON_REQ_GET_WINDOW_PROPERTIES 171
#define HORIZON_REQ_CREATE_WINSTATION 172
#define HORIZON_REQ_OPEN_WINSTATION 173
#define HORIZON_REQ_CLOSE_WINSTATION 174
#define HORIZON_REQ_SET_WINSTATION_MONITORS 175
#define HORIZON_REQ_GET_PROCESS_WINSTATION 176
#define HORIZON_REQ_SET_PROCESS_WINSTATION 177
#define HORIZON_REQ_ENUM_WINSTATION 178
#define HORIZON_REQ_CREATE_DESKTOP 179
#define HORIZON_REQ_OPEN_DESKTOP 180
#define HORIZON_REQ_OPEN_INPUT_DESKTOP 181
#define HORIZON_REQ_SET_INPUT_DESKTOP 182
#define HORIZON_REQ_CLOSE_DESKTOP 183
#define HORIZON_REQ_GET_THREAD_DESKTOP 184
#define HORIZON_REQ_SET_THREAD_DESKTOP 185
#define HORIZON_REQ_SET_USER_OBJECT_INFO 186
#define HORIZON_REQ_GET_THREAD_INPUT 190
#define HORIZON_REQ_GET_KEY_STATE 192
#define HORIZON_REQ_SET_KEY_STATE 193
#define HORIZON_REQ_SET_FOREGROUND_WINDOW 194
#define HORIZON_REQ_SET_FOCUS_WINDOW 195
#define HORIZON_REQ_SET_ACTIVE_WINDOW 196
#define HORIZON_REQ_SET_CAPTURE_WINDOW 197
#define HORIZON_REQ_CREATE_CLASS 205
#define HORIZON_REQ_DESTROY_CLASS 206
#define HORIZON_REQ_ALLOC_USER_HANDLE 280
#define HORIZON_REQ_FREE_USER_HANDLE 281
#define HORIZON_REQ_SET_CURSOR 282
/* Combined get_update_region + get_visible_region request, added to collapse
 * NtUserBeginPaint's two unconditional back-to-back IPC round trips into one.
 * 310 is appended after REQ_NB_REQUESTS's prior value (309) in server_protocol.h's
 * enum request -- verified via a standalone host compile, not guessed -- so it
 * cannot collide with any existing, already-numbered request type. See README,
 * "The ~14ms Per-Call IPC Floor". */
#define HORIZON_REQ_GET_PAINT_REGIONS 310
/* Peek-only get_update_region variant (always UPDATE_NOREGION semantics)
 * plus a has_children bit, added so switch_update_now() (dlls/win32u/dce.c)
 * can learn "does this window have any children" in the SAME round trip
 * it already has to make, instead of a separate call. 311 is appended
 * right after get_paint_regions's 310 -- verified via the same standalone
 * host-compile check, not guessed. */
#define HORIZON_REQ_GET_UPDATE_FLAGS_EX 311
/* Combined redraw_window + get_update_flags_ex(from_child=0), added to
 * collapse switch_update_now()'s (dlls/win32u/dce.c) very first search call
 * into the redraw_window call that always immediately precedes it with no
 * intervening app-code dispatch, for the common RDW_UPDATENOW/no-explicit-
 * rect case (UpdateWindow()'s actual call shape). 312 is appended right
 * after get_update_flags_ex's 311 -- verified via the same standalone
 * host-compile check used for 310 and 311, not guessed. */
#define HORIZON_REQ_REDRAW_WINDOW_UPDATENOW 312
#define HORIZON_STATUS_SUCCESS 0
#define HORIZON_STATUS_OBJECT_NAME_EXISTS 0x40000000u
#define HORIZON_STATUS_ALERTED 0x00000101u
#define HORIZON_STATUS_TIMEOUT 0x00000102u
#define HORIZON_STATUS_PENDING 0x00000103u
#define HORIZON_STATUS_UNSUCCESSFUL 0xc0000001u
#define HORIZON_STATUS_BUFFER_OVERFLOW 0x80000005u
#define HORIZON_STATUS_BUFFER_TOO_SMALL 0xc0000023u
#define HORIZON_STATUS_DEVICE_NOT_READY 0xc00000a3u
#define HORIZON_STATUS_IO_TIMEOUT 0xc00000b5u
#define HORIZON_STATUS_NOT_SUPPORTED 0xc00000bbu
#define HORIZON_STATUS_DEVICE_BUSY 0x80000011u
#define HORIZON_STATUS_NETWORK_BUSY 0xc00000bfu
#define HORIZON_STATUS_INVALID_CONNECTION 0xc0000140u
#define HORIZON_STATUS_CONNECTION_RESET 0xc000020du
#define HORIZON_STATUS_CONNECTION_ABORTED 0xc0000241u
#define HORIZON_STATUS_CONNECTION_REFUSED 0xc0000236u
#define HORIZON_STATUS_CONNECTION_ACTIVE 0xc000023bu
#define HORIZON_STATUS_NETWORK_UNREACHABLE 0xc000023cu
#define HORIZON_STATUS_HOST_UNREACHABLE 0xc000023du
#define HORIZON_STATUS_ADDRESS_ALREADY_ASSOCIATED 0xc0000238u
#define HORIZON_STATUS_INVALID_ADDRESS_COMPONENT 0xc0000207u
#define HORIZON_STATUS_SHARING_VIOLATION 0xc0000043u
#define HORIZON_STATUS_PIPE_DISCONNECTED 0xc00000b0u
#define HORIZON_STATUS_NOT_IMPLEMENTED 0xc0000002u
#define HORIZON_STATUS_INVALID_HANDLE 0xc0000008u
#define HORIZON_STATUS_INVALID_PARAMETER 0xc000000du
#define HORIZON_STATUS_INVALID_IMAGE_FORMAT 0xc000007bu
#define HORIZON_STATUS_NO_SUCH_FILE 0xc000000fu
#define HORIZON_STATUS_ACCESS_DENIED 0xc0000022u
#define HORIZON_STATUS_NO_MEMORY 0xc0000017u
#define HORIZON_STATUS_OBJECT_TYPE_MISMATCH 0xc0000024u
#define HORIZON_STATUS_OBJECT_NAME_NOT_FOUND 0xc0000034u
#define HORIZON_STATUS_OBJECT_NAME_COLLISION 0xc0000035u
#define HORIZON_STATUS_OBJECT_PATH_NOT_FOUND 0xc000003au
#define HORIZON_STATUS_MUTANT_NOT_OWNED 0xc0000046u
#define HORIZON_STATUS_SEMAPHORE_LIMIT_EXCEEDED 0xc0000047u
#define HORIZON_STATUS_BAD_DEVICE_TYPE 0xc00000cbu
#define HORIZON_STATUS_TOO_MANY_OPENED_FILES 0xc000011fu
#define HORIZON_STATUS_INFO_LENGTH_MISMATCH 0xc0000004u
#define HORIZON_STATUS_NO_MORE_FILES 0x80000006u
#define HORIZON_STATUS_NOT_SAME_OBJECT 0xc00001acu
#define HORIZON_IMAGE_FILE_MACHINE_ARM64 0xaa64
#define HORIZON_IMAGE_NT_OPTIONAL_HDR64_MAGIC 0x20b
#define HORIZON_IMAGE_FILE_DLL 0x2000
#define HORIZON_IMAGE_SCN_CNT_CODE 0x00000020
#define HORIZON_IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE 0x0040
#define HORIZON_IMAGE_FLAGS_IMAGE_DYNAMICALLY_RELOCATED 0x04
#define HORIZON_IMAGE_FLAGS_IMAGE_MAPPED_FLAT 0x08
#define HORIZON_SEC_IMAGE 0x01000000u
#define HORIZON_FILE_DIRECTORY_FILE 0x00000001u
#define HORIZON_FILE_NON_DIRECTORY_FILE 0x00000040u
#define HORIZON_APC_RESULT_SIZE 40
#define HORIZON_FD_TYPE_FILE 1
#define HORIZON_FD_TYPE_DIR 2
#define HORIZON_FD_TYPE_SOCKET 3
#define HORIZON_FIRST_USER_HANDLE 0x0020
#define HORIZON_LAST_USER_HANDLE 0xffef
#define HORIZON_MAX_USER_HANDLES ((HORIZON_LAST_USER_HANDLE - HORIZON_FIRST_USER_HANDLE + 1) >> 1)
#define HORIZON_MAX_ATOM_LEN 255
#define HORIZON_SESSION_MAPPING_SIZE 0x200000
#define HORIZON_DESKTOP_ATOM 32769
#define HORIZON_NTUSER_OBJ_WINDOW 0x01
#define HORIZON_NTUSER_DPI_PER_MONITOR_AWARE 0x12
#define HORIZON_SET_USER_OBJECT_SET_FLAGS 1
#define HORIZON_WSF_VISIBLE 1
#define HORIZON_GWL_EXSTYLE (-20)
#define HORIZON_GWL_STYLE (-16)
#define HORIZON_GWLP_ID (-12)
#define HORIZON_GWLP_HINSTANCE (-6)
#define HORIZON_GWLP_WNDPROC (-4)
#define HORIZON_GWLP_USERDATA (-21)
#define HORIZON_WS_VISIBLE 0x10000000u
#define HORIZON_WS_DISABLED 0x08000000u
#define HORIZON_DCX_WINDOW 0x00000001u
#define HORIZON_SWP_NOREDRAW 0x0008
#define HORIZON_SWP_SHOWWINDOW 0x0040
#define HORIZON_SWP_HIDEWINDOW 0x0080
#define HORIZON_SET_WINPOS_PAINT_SURFACE 0x01
#define HORIZON_COORDS_CLIENT 0
#define HORIZON_COORDS_WINDOW 1
#define HORIZON_COORDS_PARENT 2
#define HORIZON_COORDS_SCREEN 3
#define HORIZON_RDW_INVALIDATE 0x0001
#define HORIZON_RDW_INTERNALPAINT 0x0002
#define HORIZON_RDW_ERASE 0x0004
#define HORIZON_RDW_VALIDATE 0x0008
#define HORIZON_RDW_NOINTERNALPAINT 0x0010
#define HORIZON_RDW_NOCHILDREN 0x0040
#define HORIZON_RDW_ALLCHILDREN 0x0080
#define HORIZON_RDW_FRAME 0x0400
#define HORIZON_UPDATE_NONCLIENT 0x001
#define HORIZON_UPDATE_ERASE 0x002
#define HORIZON_UPDATE_PAINT 0x004
#define HORIZON_UPDATE_INTERNALPAINT 0x008
#define HORIZON_UPDATE_ALLCHILDREN 0x010
#define HORIZON_UPDATE_NOCHILDREN 0x020
#define HORIZON_UPDATE_NOREGION 0x040
#define HORIZON_WS_MINIMIZE 0x20000000u
#define HORIZON_WS_CLIPCHILDREN 0x02000000u
#define HORIZON_WM_PAINT 0x000f
#define HORIZON_WM_MOUSEMOVE 0x0200
#define HORIZON_WM_LBUTTONDOWN 0x0201
#define HORIZON_WM_LBUTTONUP 0x0202
#define HORIZON_WM_NCMOUSEFIRST 0x00a0
#define HORIZON_WM_MOUSEFIRST 0x0200
#define HORIZON_MK_LBUTTON 0x0001
#define HORIZON_VK_LBUTTON 0x01
#define HORIZON_MSG_POSTED 6
#define HORIZON_MSG_HARDWARE 7
#define HORIZON_INPUT_MOUSE 0
#define HORIZON_IMDT_MOUSE 0x02
#define HORIZON_IMO_HARDWARE 0x01
#define HORIZON_MOUSEEVENTF_MOVE 0x0001
#define HORIZON_MOUSEEVENTF_LEFTDOWN 0x0002
#define HORIZON_MOUSEEVENTF_LEFTUP 0x0004
#define HORIZON_MOUSEEVENTF_ABSOLUTE 0x8000
#define HORIZON_CAPTURE_MENU 0x01
#define HORIZON_CAPTURE_MOVESIZE 0x02
#define HORIZON_SET_CURSOR_HANDLE 0x01
#define HORIZON_SET_CURSOR_COUNT 0x02
#define HORIZON_SET_CURSOR_POS 0x04
#define HORIZON_SET_CURSOR_CLIP 0x08
#define HORIZON_SET_CURSOR_NOCLIP 0x10

#ifndef FILE_READ_DATA
#define FILE_READ_DATA 0x0001
#endif
#ifndef FILE_WRITE_DATA
#define FILE_WRITE_DATA 0x0002
#endif
#ifndef FILE_APPEND_DATA
#define FILE_APPEND_DATA 0x0004
#endif
#ifndef FILE_SUPERSEDE
#define FILE_SUPERSEDE 0
#define FILE_OPEN 1
#define FILE_CREATE 2
#define FILE_OPEN_IF 3
#define FILE_OVERWRITE 4
#define FILE_OVERWRITE_IF 5
#endif

/* Not visible in this file's include context despite being standard NT
 * constants (include/winnt.h) -- same situation as the FILE_READ_DATA et al
 * fallbacks just above, same fix: guarded local fallbacks. Copied as the
 * exact symbolic expressions from winnt.h, not hand-resolved hex, so the
 * preprocessor computes the values instead of a manually-computed number
 * that would be unverifiable at a glance. Needed for
 * horizon_server_map_generic_access() below. */
#ifndef SYNCHRONIZE
#define SYNCHRONIZE 0x00100000
#endif
#ifndef READ_CONTROL
#define READ_CONTROL 0x00020000
#endif
#ifndef STANDARD_RIGHTS_REQUIRED
#define STANDARD_RIGHTS_REQUIRED 0x000f0000
#endif
#ifndef STANDARD_RIGHTS_READ
#define STANDARD_RIGHTS_READ READ_CONTROL
#endif
#ifndef STANDARD_RIGHTS_WRITE
#define STANDARD_RIGHTS_WRITE READ_CONTROL
#endif
#ifndef STANDARD_RIGHTS_EXECUTE
#define STANDARD_RIGHTS_EXECUTE READ_CONTROL
#endif
#ifndef FILE_READ_EA
#define FILE_READ_EA 0x0008
#endif
#ifndef FILE_WRITE_EA
#define FILE_WRITE_EA 0x0010
#endif
#ifndef FILE_EXECUTE
#define FILE_EXECUTE 0x0020
#endif
#ifndef FILE_READ_ATTRIBUTES
#define FILE_READ_ATTRIBUTES 0x0080
#endif
#ifndef FILE_WRITE_ATTRIBUTES
#define FILE_WRITE_ATTRIBUTES 0x0100
#endif
#ifndef FILE_ALL_ACCESS
#define FILE_ALL_ACCESS (STANDARD_RIGHTS_REQUIRED|SYNCHRONIZE|0x1ff)
#endif
#ifndef FILE_GENERIC_READ
#define FILE_GENERIC_READ (STANDARD_RIGHTS_READ | FILE_READ_DATA | \
                           FILE_READ_ATTRIBUTES | FILE_READ_EA | \
                           SYNCHRONIZE)
#endif
#ifndef FILE_GENERIC_WRITE
#define FILE_GENERIC_WRITE (STANDARD_RIGHTS_WRITE | FILE_WRITE_DATA | \
                            FILE_WRITE_ATTRIBUTES | FILE_WRITE_EA | \
                            FILE_APPEND_DATA | SYNCHRONIZE)
#endif
#ifndef FILE_GENERIC_EXECUTE
#define FILE_GENERIC_EXECUTE (STANDARD_RIGHTS_EXECUTE | FILE_EXECUTE | \
                              FILE_READ_ATTRIBUTES | SYNCHRONIZE)
#endif
#ifndef GENERIC_READ
#define GENERIC_READ 0x80000000
#endif
#ifndef GENERIC_WRITE
#define GENERIC_WRITE 0x40000000
#endif
#ifndef GENERIC_EXECUTE
#define GENERIC_EXECUTE 0x20000000
#endif
#ifndef GENERIC_ALL
#define GENERIC_ALL 0x10000000
#endif

enum horizon_select_opcode
{
    HORIZON_SELECT_NONE,
    HORIZON_SELECT_WAIT,
    HORIZON_SELECT_WAIT_ALL,
    HORIZON_SELECT_SIGNAL_AND_WAIT,
    HORIZON_SELECT_KEYED_EVENT_WAIT,
    HORIZON_SELECT_KEYED_EVENT_RELEASE
};

enum horizon_event_op
{
    HORIZON_PULSE_EVENT,
    HORIZON_SET_EVENT,
    HORIZON_RESET_EVENT
};

enum horizon_server_object_type
{
    HORIZON_SERVER_OBJECT_RESERVE = 1,
    HORIZON_SERVER_OBJECT_EVENT,
    HORIZON_SERVER_OBJECT_KEYED_EVENT,
    HORIZON_SERVER_OBJECT_MUTEX,
    HORIZON_SERVER_OBJECT_SEMAPHORE,
    HORIZON_SERVER_OBJECT_TIMER,
    HORIZON_SERVER_OBJECT_FILE,
    HORIZON_SERVER_OBJECT_MAPPING,
    HORIZON_SERVER_OBJECT_PROCESS,
    HORIZON_SERVER_OBJECT_THREAD,
    HORIZON_SERVER_OBJECT_SOCK,
    HORIZON_SERVER_OBJECT_WINSTATION,
    HORIZON_SERVER_OBJECT_DESKTOP
};

struct horizon_server_request_header
{
    int req;
    unsigned int request_size;
    unsigned int reply_size;
};

struct horizon_server_reply_header
{
    unsigned int error;
    unsigned int reply_size;
};

struct horizon_init_first_thread_request
{
    struct horizon_server_request_header header;
    int unix_pid;
    int unix_tid;
    int debug_level;
    int reply_fd;
    int wait_fd;
};

struct horizon_init_first_thread_reply
{
    struct horizon_server_reply_header header;
    unsigned int pid;
    unsigned int tid;
    long long server_start;
    unsigned int session_id;
    unsigned int inproc_device;
    unsigned int info_size;
    char pad[4];
};

struct horizon_init_process_done_reply
{
    struct horizon_server_reply_header header;
    int suspend;
    char pad[4];
};

struct horizon_init_thread_request
{
    struct horizon_server_request_header header;
    int unix_tid;
    int reply_fd;
    int wait_fd;
    unsigned long long teb;
    unsigned long long entry;
};

struct horizon_init_thread_reply
{
    struct horizon_server_reply_header header;
    int suspend;
    char pad[4];
};

struct horizon_close_handle_request
{
    struct horizon_server_request_header header;
    unsigned int handle;
};

struct horizon_set_handle_info_reply
{
    struct horizon_server_reply_header header;
    int old_flags;
    char pad[4];
};

struct horizon_dup_handle_request
{
    struct horizon_server_request_header header;
    unsigned int src_process;
    unsigned int src_handle;
    unsigned int dst_process;
    unsigned int access;
    unsigned int attributes;
    unsigned int options;
    char pad[4];
};

struct horizon_dup_handle_reply
{
    struct horizon_server_reply_header header;
    unsigned int handle;
    char pad[4];
};

struct horizon_allocate_reserve_object_request
{
    struct horizon_server_request_header header;
    int type;
};

struct horizon_allocate_reserve_object_reply
{
    struct horizon_server_reply_header header;
    unsigned int handle;
    char pad[4];
};

struct horizon_compare_objects_request
{
    struct horizon_server_request_header header;
    unsigned int first;
    unsigned int second;
    char pad[4];
};

struct horizon_open_process_reply
{
    struct horizon_server_reply_header header;
    unsigned int handle;
    char pad[4];
};

struct horizon_select_request
{
    struct horizon_server_request_header header;
    int flags;
    unsigned long long cookie;
    long long timeout;
    unsigned int size;
    unsigned int prev_apc;
};

struct horizon_select_reply
{
    struct horizon_server_reply_header header;
    unsigned int apc_handle;
    int signaled;
};

struct horizon_object_attributes
{
    unsigned int rootdir;
    unsigned int attributes;
    unsigned int sd_len;
    unsigned int name_len;
};

struct horizon_object_name
{
    unsigned int rootdir;
    const unsigned char *name;
    unsigned int name_len;
};

struct horizon_create_event_request
{
    struct horizon_server_request_header header;
    unsigned int access;
    int manual_reset;
    int initial_state;
};

struct horizon_create_event_reply
{
    struct horizon_server_reply_header header;
    unsigned int handle;
    char pad[4];
};

struct horizon_event_op_request
{
    struct horizon_server_request_header header;
    unsigned int handle;
    int op;
    char pad[4];
};

struct horizon_event_op_reply
{
    struct horizon_server_reply_header header;
    int state;
    char pad[4];
};

struct horizon_query_event_request
{
    struct horizon_server_request_header header;
    unsigned int handle;
};

struct horizon_query_event_reply
{
    struct horizon_server_reply_header header;
    int manual_reset;
    int state;
};

struct horizon_open_named_object_request
{
    struct horizon_server_request_header header;
    unsigned int access;
    unsigned int attributes;
    unsigned int rootdir;
};

struct horizon_create_file_request
{
    struct horizon_server_request_header header;
    unsigned int access;
    unsigned int sharing;
    int create;
    unsigned int options;
    unsigned int attrs;
};

struct horizon_create_file_reply
{
    struct horizon_server_reply_header header;
    unsigned int handle;
    char pad[4];
};

struct horizon_get_handle_fd_request
{
    struct horizon_server_request_header header;
    unsigned int handle;
};

struct horizon_get_handle_fd_reply
{
    struct horizon_server_reply_header header;
    int type;
    int cacheable;
    unsigned int access;
    unsigned int options;
};

struct horizon_directory_file_entry
{
    unsigned int name_len;
};

struct horizon_query_directory_file_request
{
    struct horizon_server_request_header header;
    unsigned int handle;
    unsigned int restart_scan;
    char pad[4];
};

struct horizon_query_directory_file_reply
{
    struct horizon_server_reply_header header;
    unsigned int total_len;
    char pad[4];
};

struct horizon_new_thread_request
{
    struct horizon_server_request_header header;
    unsigned int process;
    unsigned int access;
    unsigned int flags;
    int request_fd;
    char pad[4];
};

struct horizon_new_thread_reply
{
    struct horizon_server_reply_header header;
    unsigned int tid;
    unsigned int handle;
};

struct horizon_resume_thread_request
{
    struct horizon_server_request_header header;
    unsigned int handle;
};

struct horizon_resume_thread_reply
{
    struct horizon_server_reply_header header;
    int count;
    char pad[4];
};

struct horizon_open_file_object_request
{
    struct horizon_server_request_header header;
    unsigned int access;
    unsigned int attributes;
    unsigned int rootdir;
    unsigned int sharing;
    unsigned int options;
    /* UTF-16LE filename follows as request data */
};

struct horizon_open_file_object_reply
{
    struct horizon_server_reply_header header;
    unsigned int handle;
    char pad[4];
};

struct horizon_async_data
{
    unsigned int handle;
    unsigned int event;
    unsigned long long iosb;
    unsigned long long user;
    unsigned long long apc;
    unsigned long long apc_context;
};

struct horizon_recv_socket_request
{
    struct horizon_server_request_header header;
    int oob;
    struct horizon_async_data async;
    int force_async;
    char pad[4];
};

struct horizon_socket_io_reply
{
    struct horizon_server_reply_header header;
    unsigned int wait;
    unsigned int options;
    int nonblocking;
    char pad[4];
};

struct horizon_send_socket_request
{
    struct horizon_server_request_header header;
    unsigned int flags;
    struct horizon_async_data async;
};

struct horizon_set_async_direct_result_request
{
    struct horizon_server_request_header header;
    unsigned int handle;
    unsigned long long information;
    unsigned int status;
    int mark_pending;
};

struct horizon_set_async_direct_result_reply
{
    struct horizon_server_reply_header header;
    unsigned int handle;
    char pad[4];
};

struct horizon_ioctl_request
{
    struct horizon_server_request_header header;
    unsigned int code;
    struct horizon_async_data async;
    /* in_data follows as request data */
};

struct horizon_ioctl_reply
{
    struct horizon_server_reply_header header;
    unsigned int wait;
    unsigned int options;
    /* out_data follows as reply data */
};

struct horizon_socket_get_events_request
{
    struct horizon_server_request_header header;
    unsigned int handle;
    unsigned int event;
    char pad[4];
};

struct horizon_socket_get_events_reply
{
    struct horizon_server_reply_header header;
    unsigned int flags;
    char pad[4];
    /* status[13] follows as reply data */
};

struct horizon_create_mapping_request
{
    struct horizon_server_request_header header;
    unsigned int access;
    unsigned int flags;
    unsigned int file_access;
    unsigned long long size;
    unsigned int file_handle;
    char pad[4];
};

struct horizon_create_mapping_reply
{
    struct horizon_server_reply_header header;
    unsigned int handle;
    char pad[4];
};

struct horizon_get_mapping_info_request
{
    struct horizon_server_request_header header;
    unsigned int handle;
    unsigned int access;
    char pad[4];
};

struct horizon_get_mapping_info_reply
{
    struct horizon_server_reply_header header;
    unsigned long long size;
    unsigned int flags;
    unsigned int shared_file;
    unsigned int name_len;
    unsigned int total;
};

struct horizon_get_image_map_address_request
{
    struct horizon_server_request_header header;
    unsigned int handle;
};

struct horizon_get_image_map_address_reply
{
    struct horizon_server_reply_header header;
    unsigned long long addr;
};

struct horizon_map_view_request
{
    struct horizon_server_request_header header;
    unsigned int mapping;
    unsigned int access;
    char pad[4];
    unsigned long long base;
    unsigned long long size;
    long long start;
};

struct horizon_map_image_view_request
{
    struct horizon_server_request_header header;
    unsigned int mapping;
    unsigned long long base;
    unsigned long long size;
    unsigned long long offset;
    unsigned int entry;
    unsigned short machine;
    char pad[2];
};

struct horizon_unmap_view_request
{
    struct horizon_server_request_header header;
    char pad[4];
    unsigned long long base;
};

struct horizon_pe_image_info
{
    unsigned long long base;
    unsigned long long map_addr;
    unsigned long long stack_size;
    unsigned long long stack_commit;
    unsigned int entry_point;
    unsigned int map_size;
    unsigned int alignment;
    unsigned int zerobits;
    unsigned int subsystem;
    unsigned short subsystem_minor;
    unsigned short subsystem_major;
    unsigned short osversion_major;
    unsigned short osversion_minor;
    unsigned short image_charact;
    unsigned short dll_charact;
    unsigned short machine;
    unsigned char contains_code : 1;
    unsigned char wine_builtin : 1;
    unsigned char wine_fakedll : 1;
    unsigned char is_hybrid : 1;
    unsigned char padding : 4;
    unsigned char image_flags;
    unsigned int loader_flags;
    unsigned int header_size;
    unsigned int header_map_size;
    unsigned int file_size;
    unsigned int checksum;
    unsigned int dbg_offset;
    unsigned int dbg_size;
};

struct horizon_create_mutex_request
{
    struct horizon_server_request_header header;
    unsigned int access;
    int owned;
    char pad[4];
};

struct horizon_create_mutex_reply
{
    struct horizon_server_reply_header header;
    unsigned int handle;
    char pad[4];
};

struct horizon_release_mutex_request
{
    struct horizon_server_request_header header;
    unsigned int handle;
};

struct horizon_release_mutex_reply
{
    struct horizon_server_reply_header header;
    unsigned int prev_count;
    char pad[4];
};

struct horizon_query_mutex_request
{
    struct horizon_server_request_header header;
    unsigned int handle;
};

struct horizon_query_mutex_reply
{
    struct horizon_server_reply_header header;
    unsigned int count;
    int owned;
    int abandoned;
    char pad[4];
};

struct horizon_create_semaphore_request
{
    struct horizon_server_request_header header;
    unsigned int access;
    unsigned int initial;
    unsigned int max;
};

struct horizon_create_semaphore_reply
{
    struct horizon_server_reply_header header;
    unsigned int handle;
    char pad[4];
};

struct horizon_release_semaphore_request
{
    struct horizon_server_request_header header;
    unsigned int handle;
    unsigned int count;
    char pad[4];
};

struct horizon_release_semaphore_reply
{
    struct horizon_server_reply_header header;
    unsigned int prev_count;
    char pad[4];
};

struct horizon_query_semaphore_request
{
    struct horizon_server_request_header header;
    unsigned int handle;
};

struct horizon_query_semaphore_reply
{
    struct horizon_server_reply_header header;
    unsigned int current;
    unsigned int max;
};

struct horizon_create_timer_request
{
    struct horizon_server_request_header header;
    unsigned int access;
    int manual;
    char pad[4];
};

struct horizon_create_timer_reply
{
    struct horizon_server_reply_header header;
    unsigned int handle;
    char pad[4];
};

struct horizon_set_timer_request
{
    struct horizon_server_request_header header;
    unsigned int handle;
    long long expire;
    unsigned long long callback;
    unsigned long long arg;
    int period;
    char pad[4];
};

struct horizon_set_timer_reply
{
    struct horizon_server_reply_header header;
    int signaled;
    char pad[4];
};

struct horizon_cancel_timer_request
{
    struct horizon_server_request_header header;
    unsigned int handle;
};

struct horizon_cancel_timer_reply
{
    struct horizon_server_reply_header header;
    int signaled;
    char pad[4];
};

struct horizon_get_timer_info_request
{
    struct horizon_server_request_header header;
    unsigned int handle;
};

struct horizon_get_timer_info_reply
{
    struct horizon_server_reply_header header;
    long long when;
    int signaled;
    char pad[4];
};

struct horizon_atom_reply
{
    struct horizon_server_reply_header header;
    unsigned int atom;
    char pad[4];
};

struct horizon_create_window_request
{
    struct horizon_server_request_header header;
    unsigned int parent;
    unsigned int owner;
    unsigned int atom;
    unsigned long long class_instance;
    unsigned long long instance;
    unsigned int dpi_context;
    unsigned int style;
    unsigned int ex_style;
    char pad[4];
};

struct horizon_create_window_reply
{
    struct horizon_server_reply_header header;
    unsigned int handle;
    unsigned int parent;
    unsigned int owner;
    int extra;
    unsigned long long class_ptr;
};

struct horizon_destroy_window_request
{
    struct horizon_server_request_header header;
    unsigned int handle;
};

struct horizon_get_desktop_window_request
{
    struct horizon_server_request_header header;
    int force;
};

struct horizon_get_desktop_window_reply
{
    struct horizon_server_reply_header header;
    unsigned int top_window;
    unsigned int msg_window;
};

struct horizon_rectangle
{
    int left;
    int top;
    int right;
    int bottom;
};

struct horizon_set_window_owner_request
{
    struct horizon_server_request_header header;
    unsigned int handle;
    unsigned int owner;
    char pad[4];
};

struct horizon_set_window_owner_reply
{
    struct horizon_server_reply_header header;
    unsigned int full_owner;
    unsigned int prev_owner;
};

struct horizon_get_window_info_request
{
    struct horizon_server_request_header header;
    unsigned int handle;
    int offset;
    unsigned int size;
};

struct horizon_get_window_info_reply
{
    struct horizon_server_reply_header header;
    unsigned int last_active;
    int is_unicode;
    unsigned long long info;
};

struct horizon_init_window_info_request
{
    struct horizon_server_request_header header;
    unsigned int handle;
    unsigned int style;
    unsigned int ex_style;
    short is_unicode;
    char pad[6];
};

struct horizon_set_window_info_request
{
    struct horizon_server_request_header header;
    unsigned int handle;
    int offset;
    unsigned int size;
    unsigned long long new_info;
};

struct horizon_set_window_info_reply
{
    struct horizon_server_reply_header header;
    unsigned long long old_info;
};

struct horizon_get_window_tree_request
{
    struct horizon_server_request_header header;
    unsigned int handle;
};

struct horizon_get_window_children_from_point_request
{
    struct horizon_server_request_header header;
    unsigned int parent;
    int x;
    int y;
    int dpi;
    char pad[4];
};

struct horizon_get_window_children_from_point_reply
{
    struct horizon_server_reply_header header;
    int count;
    char pad[4];
};

struct horizon_get_window_tree_reply
{
    struct horizon_server_reply_header header;
    unsigned int parent;
    unsigned int owner;
    unsigned int next_sibling;
    unsigned int prev_sibling;
    unsigned int first_sibling;
    unsigned int last_sibling;
    unsigned int first_child;
    unsigned int last_child;
};

struct horizon_set_window_pos_request
{
    struct horizon_server_request_header header;
    unsigned short swp_flags;
    unsigned short paint_flags;
    unsigned int monitor_dpi;
    unsigned int handle;
    unsigned int previous;
    struct horizon_rectangle window;
    struct horizon_rectangle client;
    char pad[4];
};

struct horizon_set_window_pos_reply
{
    struct horizon_server_reply_header header;
    unsigned int new_style;
    unsigned int new_ex_style;
    unsigned int surface_win;
    char pad[4];
};

struct horizon_get_window_rectangles_request
{
    struct horizon_server_request_header header;
    unsigned int handle;
    int relative;
    int dpi;
};

struct horizon_get_window_rectangles_reply
{
    struct horizon_server_reply_header header;
    struct horizon_rectangle window;
    struct horizon_rectangle client;
};

struct horizon_get_visible_region_request
{
    struct horizon_server_request_header header;
    unsigned int window;
    unsigned int flags;
    char pad[4];
};

struct horizon_get_visible_region_reply
{
    struct horizon_server_reply_header header;
    unsigned int top_win;
    struct horizon_rectangle top_rect;
    struct horizon_rectangle win_rect;
    unsigned int paint_flags;
    unsigned int total_size;
    char pad[4];
};

struct horizon_get_update_region_request
{
    struct horizon_server_request_header header;
    unsigned int window;
    unsigned int from_child;
    unsigned int flags;
};

struct horizon_get_update_region_reply
{
    struct horizon_server_reply_header header;
    unsigned int child;
    unsigned int flags;
    unsigned int total_size;
    char pad[4];
};

/* Combined get_update_region + get_visible_region, one round trip instead of
 * two. Reply is exactly 64 bytes (HORIZON_SERVER_FIXED_MESSAGE_SIZE) with no
 * padding slack left -- confirmed by field-summing before writing this, not
 * assumed. Vardata is the update rect (0 or 16 bytes, sized by
 * update_total_size) immediately followed by the visible rect (0 or 16
 * bytes, sized by visible_total_size); each part mirrors exactly what the
 * two original single-purpose handlers would have returned on their own. */
struct horizon_get_paint_regions_request
{
    struct horizon_server_request_header header;
    unsigned int window;
    unsigned int from_child;
    unsigned int update_flags;
    unsigned int visible_flags;
};

struct horizon_get_paint_regions_reply
{
    struct horizon_server_reply_header header;
    unsigned int child;
    unsigned int update_flags;
    unsigned int update_total_size;
    unsigned int top_win;
    struct horizon_rectangle top_rect;
    struct horizon_rectangle win_rect;
    unsigned int paint_flags;
    unsigned int visible_total_size;
};

/* Peek-only get_update_region variant (see HORIZON_REQ_GET_UPDATE_FLAGS_EX
 * above). has_children always reflects `window` itself, computed
 * independently of the from_child/target search -- see
 * horizon_server_handle_get_update_flags_ex(). */
struct horizon_get_update_flags_ex_request
{
    struct horizon_server_request_header header;
    unsigned int window;
    unsigned int from_child;
    unsigned int flags;
};

struct horizon_get_update_flags_ex_reply
{
    struct horizon_server_reply_header header;
    unsigned int child;
    unsigned int flags;
    unsigned int has_children;
};

/* See HORIZON_REQ_REDRAW_WINDOW_UPDATENOW above. No rect/region data --
 * this only ever handles the no-explicit-rect redraw_window call shape,
 * same as horizon_redraw_window_request's count==0 case. */
struct horizon_redraw_window_updatenow_request
{
    struct horizon_server_request_header header;
    unsigned int window;
    unsigned int redraw_flags;
    unsigned int search_flags;
};

struct horizon_redraw_window_updatenow_reply
{
    struct horizon_server_reply_header header;
    unsigned int child;
    unsigned int flags;
    unsigned int has_children;
};

struct horizon_get_message_request
{
    struct horizon_server_request_header header;
    unsigned int flags;
    unsigned int get_win;
    unsigned int get_first;
    unsigned int get_last;
    unsigned int hw_id;
    unsigned int wake_mask;
    unsigned int changed_mask;
    unsigned int internal;
    char pad[4];
};

struct horizon_get_message_reply
{
    struct horizon_server_reply_header header;
    unsigned int win;
    unsigned int msg;
    unsigned long long wparam;
    unsigned long long lparam;
    int type;
    int x;
    int y;
    unsigned int time;
    unsigned int total;
    char pad[4];
};

struct horizon_hw_mouse_input
{
    int type;
    int x;
    int y;
    unsigned int data;
    unsigned int flags;
    unsigned int time;
    unsigned long long info;
};

union horizon_hw_input
{
    int type;
    struct horizon_hw_mouse_input mouse;
    unsigned char raw[40];
};

struct horizon_send_hardware_message_request
{
    struct horizon_server_request_header header;
    unsigned int win;
    union horizon_hw_input input;
    unsigned int flags;
    char pad[4];
};

struct horizon_send_hardware_message_reply
{
    struct horizon_server_reply_header header;
    int wait;
    int prev_x;
    int prev_y;
    int new_x;
    int new_y;
    char pad[4];
};

struct horizon_accept_hardware_message_request
{
    struct horizon_server_request_header header;
    unsigned int hw_id;
};

struct horizon_hardware_msg_data
{
    unsigned long long info;
    unsigned int size;
    int pad;
    unsigned int hw_id;
    unsigned int flags;
    struct
    {
        unsigned int device;
        unsigned int origin;
    } source;
    struct
    {
        int type;
        unsigned int device;
        unsigned int wparam;
        unsigned int usage;
    } rawinput;
};

struct horizon_obj_locator
{
    unsigned long long id;
    unsigned long long offset;
};

struct horizon_get_thread_input_request
{
    struct horizon_server_request_header header;
    unsigned int tid;
};

struct horizon_get_thread_input_reply
{
    struct horizon_server_reply_header header;
    struct horizon_obj_locator locator;
};

struct horizon_input_window_request
{
    struct horizon_server_request_header header;
    unsigned int handle;
};

struct horizon_set_foreground_window_request
{
    struct horizon_server_request_header header;
    unsigned int handle;
    int internal;
    char pad[4];
};

struct horizon_set_foreground_window_reply
{
    struct horizon_server_reply_header header;
    unsigned int previous;
    int send_msg_old;
    int send_msg_new;
    char pad[4];
};

struct horizon_input_window_reply
{
    struct horizon_server_reply_header header;
    unsigned int previous;
    char pad[4];
};

struct horizon_set_capture_window_request
{
    struct horizon_server_request_header header;
    unsigned int handle;
    unsigned int flags;
    char pad[4];
};

struct horizon_set_capture_window_reply
{
    struct horizon_server_reply_header header;
    unsigned int previous;
    unsigned int full_handle;
};

struct horizon_set_cursor_request
{
    struct horizon_server_request_header header;
    unsigned int flags;
    unsigned int handle;
    int show_count;
    int x;
    int y;
    struct horizon_rectangle clip;
};

struct horizon_set_cursor_reply
{
    struct horizon_server_reply_header header;
    unsigned int prev_handle;
    int prev_count;
    int prev_x;
    int prev_y;
    int new_x;
    int new_y;
    struct horizon_rectangle new_clip;
    unsigned int last_change;
    char pad[4];
};

struct horizon_alloc_user_handle_request
{
    struct horizon_server_request_header header;
    unsigned short type;
    char pad[2];
};

struct horizon_alloc_user_handle_reply
{
    struct horizon_server_reply_header header;
    unsigned int handle;
    char pad[4];
};

struct horizon_free_user_handle_request
{
    struct horizon_server_request_header header;
    unsigned short type;
    char pad0[2];
    unsigned int handle;
    char pad1[4];
};

struct horizon_update_window_zorder_request
{
    struct horizon_server_request_header header;
    unsigned int window;
    struct horizon_rectangle rect;
};

struct horizon_redraw_window_request
{
    struct horizon_server_request_header header;
    unsigned int window;
    unsigned int flags;
    char pad[4];
};

/* See the comment on redraw_window_reply in server_protocol.h. */
struct horizon_redraw_window_reply
{
    struct horizon_server_reply_header header;
    unsigned int child;
    unsigned int flags;
    unsigned int has_children;
};

struct horizon_set_window_property_request
{
    struct horizon_server_request_header header;
    unsigned int window;
    unsigned long long data;
    unsigned short atom;
    char pad[6];
};

struct horizon_window_property_request
{
    struct horizon_server_request_header header;
    unsigned int window;
    unsigned short atom;
    char pad[6];
};

struct horizon_window_property_reply
{
    struct horizon_server_reply_header header;
    unsigned long long data;
};

struct horizon_get_window_properties_request
{
    struct horizon_server_request_header header;
    unsigned int window;
};

struct horizon_get_window_properties_reply
{
    struct horizon_server_reply_header header;
    int total;
    char pad[4];
};

struct horizon_property_data
{
    unsigned short atom;
    char pad[2];
    int string;
    unsigned long long data;
};

struct horizon_create_winstation_request
{
    struct horizon_server_request_header header;
    unsigned int flags;
    unsigned int access;
    unsigned int attributes;
    unsigned int rootdir;
    char pad[4];
};

struct horizon_winstation_handle_reply
{
    struct horizon_server_reply_header header;
    unsigned int handle;
    char pad[4];
};

struct horizon_set_winstation_monitors_request
{
    struct horizon_server_request_header header;
    int increment;
};

struct horizon_set_winstation_monitors_reply
{
    struct horizon_server_reply_header header;
    unsigned long long serial;
};

struct horizon_set_process_winstation_request
{
    struct horizon_server_request_header header;
    unsigned int handle;
};

struct horizon_enum_winstation_request
{
    struct horizon_server_request_header header;
    unsigned int handle;
};

struct horizon_enum_winstation_reply
{
    struct horizon_server_reply_header header;
    unsigned int count;
    unsigned int total;
};

struct horizon_create_desktop_request
{
    struct horizon_server_request_header header;
    unsigned int flags;
    unsigned int access;
    unsigned int attributes;
};

struct horizon_open_desktop_request
{
    struct horizon_server_request_header header;
    unsigned int winsta;
    unsigned int flags;
    unsigned int access;
    unsigned int attributes;
    char pad[4];
};

struct horizon_get_thread_desktop_request
{
    struct horizon_server_request_header header;
    unsigned int tid;
};

struct horizon_get_thread_desktop_reply
{
    struct horizon_server_reply_header header;
    struct horizon_obj_locator locator;
    unsigned int handle;
    char pad[4];
};

struct horizon_set_thread_desktop_request
{
    struct horizon_server_request_header header;
    unsigned int handle;
};

struct horizon_set_thread_desktop_reply
{
    struct horizon_server_reply_header header;
    struct horizon_obj_locator locator;
};

struct horizon_set_user_object_info_request
{
    struct horizon_server_request_header header;
    unsigned int handle;
    unsigned int flags;
    unsigned int obj_flags;
    long long close_timeout;
};

struct horizon_set_user_object_info_reply
{
    struct horizon_server_reply_header header;
    int is_desktop;
    unsigned int old_obj_flags;
};

struct horizon_create_class_request
{
    struct horizon_server_request_header header;
    int local;
    unsigned int atom;
    unsigned int style;
    unsigned long long instance;
    unsigned long long client_ptr;
    short cls_extra;
    short win_extra;
    unsigned int name_offset;
};

struct horizon_create_class_reply
{
    struct horizon_server_reply_header header;
    struct horizon_obj_locator locator;
    unsigned int atom;
    char pad[4];
};

struct horizon_destroy_class_request
{
    struct horizon_server_request_header header;
    unsigned int atom;
    unsigned long long instance;
};

struct horizon_select_wait_op
{
    int op;
    unsigned int handles[1];
};

struct horizon_select_signal_and_wait_op
{
    int op;
    unsigned int wait;
    unsigned int signal;
};

struct horizon_user_entry
{
    unsigned long long offset;
    unsigned int tid;
    unsigned int pid;
    unsigned long long id;
    union
    {
        struct
        {
            unsigned short type;
            unsigned short generation;
        };
        long long uniq;
    };
};

struct horizon_shared_cursor
{
    int x;
    int y;
    unsigned int last_change;
    struct horizon_rectangle clip;
};

struct horizon_desktop_shm
{
    unsigned int flags;
    struct horizon_shared_cursor cursor;
    unsigned char keystate[256];
    unsigned long long monitor_serial;
    unsigned long long keystate_serial;
};

struct horizon_input_shm
{
    int foreground;
    unsigned int active;
    unsigned int focus;
    unsigned int capture;
    unsigned int menu_owner;
    unsigned int move_size;
    unsigned int caret;
    struct horizon_rectangle caret_rect;
    unsigned int cursor;
    int cursor_count;
    unsigned char keystate[256];
    int keystate_lock;
    unsigned long long keystate_serial;
};

struct horizon_class_shm
{
    unsigned int atom;
    unsigned int style;
    unsigned int cls_extra;
    unsigned int win_extra;
    unsigned long long instance;
    unsigned int name_offset;
    unsigned int name_len;
    unsigned short name[HORIZON_MAX_ATOM_LEN];
    unsigned short pad;
    char extra[];
};

struct horizon_window_shm
{
    struct horizon_obj_locator class;
    unsigned int dpi_context;
};

union horizon_object_shm
{
    struct horizon_desktop_shm desktop;
    struct horizon_input_shm input;
    struct horizon_class_shm class;
    struct horizon_window_shm window;
};

struct horizon_shared_object
{
    long long seq;
    unsigned long long id;
    union horizon_object_shm shm;
};

struct horizon_session_shm
{
    struct horizon_user_entry user_entries[HORIZON_MAX_USER_HANDLES];
};

struct horizon_atom_entry
{
    unsigned int atom;
    unsigned int name_len;
    unsigned char *name;
    struct horizon_atom_entry *next;
};

struct horizon_user_class
{
    int local;
    unsigned int atom;
    unsigned int base_atom;
    unsigned int style;
    unsigned long long instance;
    unsigned long long client_ptr;
    int cls_extra;
    int win_extra;
    unsigned int name_len;
    unsigned char *name;
    struct horizon_obj_locator locator;
    struct horizon_user_class *next;
};

struct horizon_window_property
{
    unsigned short atom;
    unsigned int string;
    unsigned long long data;
    struct horizon_window_property *next;
};

struct horizon_input_message
{
    unsigned int id;
    unsigned int tid;
    unsigned int win;
    unsigned int msg;
    unsigned long long wparam;
    unsigned long long lparam;
    int x;
    int y;
    unsigned int time;
    unsigned long long info;
    struct horizon_input_message *next;
};

struct horizon_user_window
{
    unsigned int handle;
    unsigned int parent;
    unsigned int owner;
    unsigned int pid;
    unsigned int tid;
    unsigned int atom;
    unsigned int style;
    unsigned int ex_style;
    unsigned int is_unicode;
    unsigned int id;
    unsigned int monitor_dpi;
    unsigned long long instance;
    unsigned long long user_data;
    unsigned int last_active;
    unsigned int desktop_handle;
    struct horizon_rectangle window_rect;
    struct horizon_rectangle client_rect;
    struct horizon_rectangle visible_rect;
    struct horizon_rectangle surface_rect;
    struct horizon_rectangle update_rect;
    unsigned int paint_flags;
    unsigned int has_update_rect;
    unsigned int has_internal_paint;
    unsigned int needs_erase;
    unsigned int needs_nonclient;
    struct horizon_user_class *class;
    struct horizon_window_property *properties;
    struct horizon_obj_locator locator;
    struct horizon_user_window *next;
};

struct horizon_session_view
{
    unsigned long long base;
    unsigned long long offset;
    unsigned long long size;
    struct horizon_session_view *next;
};

struct horizon_server_connection
{
    int request_fd;
    int reply_fd;
    int wait_fd;
    unsigned int pid;
    unsigned int tid;
};

struct horizon_server_object
{
    unsigned int id;
    int type;
    unsigned int refs;
    int manual_reset;
    int signaled;
    unsigned int count;
    unsigned int max;
    int owned;
    unsigned int rootdir;
    unsigned int name_len;
    unsigned char *name;
    long long timer_when;
    unsigned int timer_period;
    int file_fd;
    char *file_name;
    unsigned int file_access;
    unsigned int file_options;
    int file_is_dir;
    unsigned int dir_enum_index;
    char *dir_mask;
    int sock_nonblocking;
    unsigned int sock_event_handle; /* event signaled by the poller (WSAEventSelect) */
    int sock_event_mask;            /* AFD_POLL_* bits the app asked for */
    int sock_pending_events;        /* accumulated AFD_POLL_* bits not yet fetched */
    int sock_connect_status;        /* NTSTATUS for AFD_POLL_CONNECT_ERR */
    int sock_err_ticks;             /* consecutive poller ticks with SO_ERROR set */
    unsigned int mapping_flags;
    unsigned int mapping_access;
    unsigned int mapping_file_access;
    unsigned long long mapping_size;
    int mapping_has_image;
    int mapping_is_session;
    struct horizon_pe_image_info mapping_image;
    unsigned int user_flags;
    unsigned int desktop_winstation;
    unsigned int desktop_top_window;
    unsigned int desktop_msg_window;
    struct horizon_obj_locator desktop_locator;
};

struct horizon_server_handle_entry
{
    unsigned int handle;
    struct horizon_server_object *object;
    struct horizon_server_handle_entry *next;
};

static LONG horizon_server_next_handle = 0x100;
static pthread_mutex_t horizon_server_objects_mutex = PTHREAD_MUTEX_INITIALIZER;
static struct horizon_server_handle_entry *horizon_server_handles;
static unsigned int horizon_process_winstation;
static unsigned int horizon_thread_desktop;
static unsigned int horizon_input_desktop;
static int horizon_session_fd = -1;
static unsigned char *horizon_session_data;
static unsigned long long horizon_session_used = sizeof(struct horizon_session_shm);
static unsigned long long horizon_session_next_id;
static unsigned int horizon_user_handle_count;
static unsigned int horizon_next_atom = 0xc000;
static struct horizon_atom_entry *horizon_atoms;
static struct horizon_user_class *horizon_classes;
static struct horizon_user_window *horizon_windows;
static struct horizon_session_view *horizon_session_views;
static struct horizon_obj_locator horizon_input_locator;
static struct horizon_input_message *horizon_input_messages;
static struct horizon_input_message **horizon_input_messages_tail = &horizon_input_messages;
static unsigned int horizon_next_input_message_id = 1;
static unsigned int horizon_mouse_buttons;

static int horizon_pipe_open_r( struct _reent *r, void *fdptr, const char *path, int flags, int mode );
static int horizon_pipe_close_r( struct _reent *r, void *fdptr );
static ssize_t horizon_pipe_write_r( struct _reent *r, void *fdptr, const char *ptr, size_t len );
static ssize_t horizon_pipe_read_r( struct _reent *r, void *fdptr, char *ptr, size_t len );
static int horizon_pipe_fstat_r( struct _reent *r, void *fdptr, struct stat *st );

static const devoptab_t horizon_pipe_devoptab =
{
    .name         = "winepipe",
    .structSize   = sizeof(struct horizon_pipe_file *),
    .open_r       = horizon_pipe_open_r,
    .close_r      = horizon_pipe_close_r,
    .write_r      = horizon_pipe_write_r,
    .read_r       = horizon_pipe_read_r,
    .seek_r       = NULL,
    .fstat_r      = horizon_pipe_fstat_r,
    .stat_r       = NULL,
    .link_r       = NULL,
    .unlink_r     = NULL,
    .chdir_r      = NULL,
    .rename_r     = NULL,
    .mkdir_r      = NULL,
    .dirStateSize = 0,
    .diropen_r    = NULL,
    .dirreset_r   = NULL,
    .dirnext_r    = NULL,
    .dirclose_r   = NULL,
    .statvfs_r    = NULL,
    .ftruncate_r  = NULL,
    .fsync_r      = NULL,
    .deviceData   = NULL,
    .chmod_r      = NULL,
    .fchmod_r     = NULL,
    .rmdir_r      = NULL,
    .lstat_r      = NULL,
    .utimes_r     = NULL,
    .fpathconf_r  = NULL,
    .pathconf_r   = NULL,
    .symlink_r    = NULL,
    .readlink_r   = NULL,
};

static pthread_mutex_t horizon_pipe_device_mutex = PTHREAD_MUTEX_INITIALIZER;
static int horizon_pipe_device = -1;
static struct horizon_fd_queue horizon_server_to_client_fds =
{
    PTHREAD_MUTEX_INITIALIZER,
    PTHREAD_COND_INITIALIZER,
    NULL,
    NULL
};
static struct horizon_fd_queue horizon_client_to_server_fds =
{
    PTHREAD_MUTEX_INITIALIZER,
    PTHREAD_COND_INITIALIZER,
    NULL,
    NULL
};

struct horizon_backing
{
    void *heap_addr;
    void *code_addr;
    VirtmemReservation *code_reservation;
    size_t size;
    unsigned int refs;
    int fd;
    off_t file_offset;
    BOOL write_back;
};

struct horizon_mapping
{
    void *addr;
    size_t size;
    size_t source_offset;
    int prot;
    struct horizon_backing *backing;
    VirtmemReservation *reservation;
    struct horizon_mapping *next;
};

static struct horizon_mapping *mappings;

static int lowest_set_core( ULONG_PTR mask )
{
    int i;

    for (i = 0; i < (int)(sizeof(mask) * 8); i++)
        if (mask & ((ULONG_PTR)1 << i)) return i;

    return 0;
}

static ULONG_PTR nth_core_mask( ULONG_PTR mask, LONG index )
{
    LONG seen = 0;
    int i;

    for (i = 0; i < (int)(sizeof(mask) * 8); i++)
    {
        ULONG_PTR bit = (ULONG_PTR)1 << i;

        if (!(mask & bit)) continue;
        if (seen++ == index) return bit;
    }

    return (ULONG_PTR)1 << lowest_set_core( mask );
}

static unsigned int count_mask_bits( ULONG_PTR mask )
{
    unsigned int count = 0;

    while (mask)
    {
        count += mask & 1;
        mask >>= 1;
    }

    return count;
}

ULONG_PTR horizon_get_system_affinity_mask(void)
{
    u64 mask = 0;
    Result rc;

    if (cached_affinity_mask) return cached_affinity_mask;

    rc = svcGetInfo( &mask, InfoType_CoreMask, CUR_PROCESS_HANDLE, 0 );
    if (R_FAILED(rc) || !mask)
    {
        WARN( "svcGetInfo(InfoType_CoreMask) failed %#x, falling back to applet cores.\n", rc );
        mask = 0x7;
    }

    cached_affinity_mask = (ULONG_PTR)mask;
    TRACE( "Horizon system affinity mask %#lx.\n", (unsigned long)cached_affinity_mask );
    return cached_affinity_mask;
}

unsigned int horizon_get_processor_count(void)
{
    unsigned int count = count_mask_bits( horizon_get_system_affinity_mask() );

    return count ? count : 1;
}

void horizon_pin_current_thread( ULONG_PTR requested_mask )
{
    ULONG_PTR system_mask = horizon_get_system_affinity_mask();
    ULONG_PTR mask = requested_mask & system_mask;
    LONG index;
    int preferred;
    Result rc;

    if (!mask)
    {
        unsigned int count = horizon_get_processor_count();

        index = InterlockedIncrement( &next_core_index ) - 1;
        mask = nth_core_mask( system_mask, index % count );
    }

    preferred = lowest_set_core( mask );
    rc = svcSetThreadCoreMask( CUR_THREAD_HANDLE, preferred, (u32)mask );
    if (R_FAILED(rc))
        WARN( "svcSetThreadCoreMask(preferred %u, mask %#lx) failed %#x.\n",
              preferred, (unsigned long)mask, rc );
    else
        TRACE( "pinned current thread to preferred %u, mask %#lx.\n", preferred, (unsigned long)mask );
}

static void horizon_set_reent_errno( struct _reent *r, int error )
{
    if (r) r->_errno = error;
    else errno = error;
}

static int horizon_pipe_open_r( struct _reent *r, void *fdptr, const char *path, int flags, int mode )
{
    (void)fdptr;
    (void)path;
    (void)flags;
    (void)mode;
    horizon_set_reent_errno( r, ENOSYS );
    return -1;
}

static void horizon_pipe_destroy( struct horizon_pipe *pipe )
{
    pthread_cond_destroy( &pipe->can_write );
    pthread_cond_destroy( &pipe->can_read );
    pthread_mutex_destroy( &pipe->mutex );
    free( pipe );
}

static void horizon_pipe_release( struct horizon_pipe *pipe )
{
    int destroy = 0;

    pthread_mutex_lock( &pipe->mutex );
    destroy = --pipe->refs == 0;
    pthread_mutex_unlock( &pipe->mutex );

    if (destroy) horizon_pipe_destroy( pipe );
}

static int horizon_pipe_close_r( struct _reent *r, void *fdptr )
{
    struct horizon_pipe_file *file = *(struct horizon_pipe_file **)fdptr;
    struct horizon_pipe *pipe;

    (void)r;
    if (!file) return 0;

    pipe = file->pipe;
    pthread_mutex_lock( &pipe->mutex );
    if (file->write_end) pipe->write_open = 0;
    else pipe->read_open = 0;
    pthread_cond_broadcast( &pipe->can_read );
    pthread_cond_broadcast( &pipe->can_write );
    pthread_mutex_unlock( &pipe->mutex );

    free( file );
    *(struct horizon_pipe_file **)fdptr = NULL;
    horizon_pipe_release( pipe );
    return 0;
}

/* Set (release-store) by send_request() in dlls/ntdll/unix/server.c right
 * before it writes a request into this pipe -- the client-side "I'm
 * sending now" moment. Read-and-cleared (acquire, atomic exchange) by
 * horizon_server_thread() below the instant it wakes from blocking on the
 * request pipe -- the server-side "I just woke up" moment. The delta is
 * the real OS-scheduler wake latency this session has been treating as an
 * unmeasured black box.
 *
 * Proper synchronization, not a plain global: this is written on one
 * thread and read on another with no other synchronization tying the two
 * accesses together -- the first genuinely cross-thread shared state this
 * session has added (the redraw_window cache and switch_paint_state_
 * generation earlier tonight were both single-threaded, read and written
 * only by the UI message-loop thread; their bugs were logic errors, not
 * races). A plain global here would be a real, new risk category, not
 * stylistic pedantry.
 *
 * GCC/Clang __atomic_* builtins, not C11 <stdatomic.h>: this whole
 * project builds with -std=gnu99 (cmake/switch-devkitA64.cmake via every
 * add_executable's target_compile_options), so stdatomic.h's macros
 * (atomic_exchange_explicit, memory_order_acq_rel, ...) aren't available
 * -- confirmed by a real compile error, not assumed. The __atomic_*
 * builtins are compiler intrinsics, not gated by C standard version, and
 * are what glibc's own <stdatomic.h> wraps on this target anyway -- same
 * synchronization guarantees, no build-flag change needed. Plain
 * uint64_t rather than _Atomic-qualified: every access to this variable
 * goes through an __atomic_* builtin by discipline (never a plain
 * read/write), which is the standard, GCC-documented way to get atomic
 * access on a pre-C11 type. */
uint64_t wine_nx_trace_client_send_tick;

/* wine-nx-probe/source/runtime.c -- same flag every other hot-path trace
 * in this port is gated behind. Already on in every config this session
 * has tested against, so this activates with no config change needed.
 * Weak: horizon.c links into several smoke-test binaries
 * (wine-nx-pe-real-report, wine-nx-deko3d-smoke, ...) that don't link
 * runtime.c and so never define this -- confirmed by a real link error,
 * not assumed. Checked with an address test below before every read,
 * same as this file's existing weak wine_nx_runtime_trace() checks. */
extern int wine_nx_paint_trace_enabled __attribute__((weak));

/* In-memory aggregator (sum/count/max, flushed once/second), the exact
 * same shape as switch_paint_trace()'s own design (dlls/win32u/dce.c) --
 * deliberately, not by coincidence. This session already found "the
 * diagnostic logging itself is the bottleneck" twice (unconditional
 * per-call fflush in raw syscall tracing, then again in paint-phase
 * tracing) -- an unguarded printf/log call on literally every IPC request
 * would be a near-certain third instance of the same bug, corrupting the
 * exact measurement this trace exists to take. Accumulated in memory,
 * flushed to the log once per second via the existing gated
 * wine_nx_runtime_trace() path, same as every other hot-path measurement
 * in this codebase. */
static uint64_t wine_nx_wake_latency_sum_ns;
static unsigned int wine_nx_wake_latency_count;
static uint64_t wine_nx_wake_latency_max_ns;
static uint64_t wine_nx_wake_latency_epoch_ms;

static void wine_nx_wake_latency_record( uint64_t delta_ns )
{
    uint64_t now_ms = armTicksToNs( armGetSystemTick() ) / 1000000ULL;

    wine_nx_wake_latency_sum_ns += delta_ns;
    wine_nx_wake_latency_count++;
    if (delta_ns > wine_nx_wake_latency_max_ns) wine_nx_wake_latency_max_ns = delta_ns;

    if (!wine_nx_wake_latency_epoch_ms) wine_nx_wake_latency_epoch_ms = now_ms;
    if (now_ms - wine_nx_wake_latency_epoch_ms >= 1000)
    {
        char buf[128];
        unsigned long long avg_us = wine_nx_wake_latency_count
            ? wine_nx_wake_latency_sum_ns / wine_nx_wake_latency_count / 1000ULL : 0;

        snprintf( buf, sizeof(buf), "[NXWAKE] server thread wake latency: avg=%lluus max=%lluus n=%u",
                 avg_us, (unsigned long long)(wine_nx_wake_latency_max_ns / 1000ULL),
                 wine_nx_wake_latency_count );
        horizon_trace( "%s\n", buf );

        wine_nx_wake_latency_sum_ns = 0;
        wine_nx_wake_latency_count = 0;
        wine_nx_wake_latency_max_ns = 0;
        wine_nx_wake_latency_epoch_ms = now_ms;
    }
}

static ssize_t horizon_pipe_write_r( struct _reent *r, void *fdptr, const char *ptr, size_t len )
{
    struct horizon_pipe_file *file = *(struct horizon_pipe_file **)fdptr;
    struct horizon_pipe *pipe;
    size_t total = 0;

    if (!file || !file->write_end)
    {
        horizon_set_reent_errno( r, EBADF );
        return -1;
    }
    if (!len) return 0;

    pipe = file->pipe;
    pthread_mutex_lock( &pipe->mutex );
    while (total < len)
    {
        size_t chunk, space;

        while (pipe->read_open && pipe->used == HORIZON_PIPE_BUFFER_SIZE)
            pthread_cond_wait( &pipe->can_write, &pipe->mutex );

        if (!pipe->read_open)
        {
            pthread_mutex_unlock( &pipe->mutex );
            if (total) return total;
            horizon_set_reent_errno( r, EPIPE );
            return -1;
        }

        space = HORIZON_PIPE_BUFFER_SIZE - pipe->used;
        chunk = min( len - total, space );
        chunk = min( chunk, HORIZON_PIPE_BUFFER_SIZE - pipe->tail );
        memcpy( pipe->buffer + pipe->tail, ptr + total, chunk );
        pipe->tail = (pipe->tail + chunk) % HORIZON_PIPE_BUFFER_SIZE;
        pipe->used += chunk;
        total += chunk;
        pthread_cond_signal( &pipe->can_read );
    }
    pthread_mutex_unlock( &pipe->mutex );
    return total;
}

static ssize_t horizon_pipe_read_r( struct _reent *r, void *fdptr, char *ptr, size_t len )
{
    struct horizon_pipe_file *file = *(struct horizon_pipe_file **)fdptr;
    struct horizon_pipe *pipe;
    size_t total = 0;

    if (!file || file->write_end)
    {
        horizon_set_reent_errno( r, EBADF );
        return -1;
    }
    if (!len) return 0;

    pipe = file->pipe;
    pthread_mutex_lock( &pipe->mutex );
    while (total < len)
    {
        size_t chunk;

        while (pipe->write_open && !pipe->used)
            pthread_cond_wait( &pipe->can_read, &pipe->mutex );

        if (!pipe->used)
        {
            pthread_mutex_unlock( &pipe->mutex );
            return total;
        }

        chunk = min( len - total, pipe->used );
        chunk = min( chunk, HORIZON_PIPE_BUFFER_SIZE - pipe->head );
        memcpy( ptr + total, pipe->buffer + pipe->head, chunk );
        pipe->head = (pipe->head + chunk) % HORIZON_PIPE_BUFFER_SIZE;
        pipe->used -= chunk;
        total += chunk;
        pthread_cond_signal( &pipe->can_write );
    }
    pthread_mutex_unlock( &pipe->mutex );
    return total;
}

static int horizon_pipe_fstat_r( struct _reent *r, void *fdptr, struct stat *st )
{
    struct horizon_pipe_file *file = *(struct horizon_pipe_file **)fdptr;

    if (!file)
    {
        horizon_set_reent_errno( r, EBADF );
        return -1;
    }
    memset( st, 0, sizeof(*st) );
    st->st_mode = S_IFIFO | 0600;
    return 0;
}

static int horizon_pipe_install_device(void)
{
    pthread_mutex_lock( &horizon_pipe_device_mutex );
    if (horizon_pipe_device == -1)
    {
        horizon_pipe_device = FindDevice( "winepipe:" );
        if (horizon_pipe_device == -1)
            horizon_pipe_device = AddDevice( &horizon_pipe_devoptab );
    }
    pthread_mutex_unlock( &horizon_pipe_device_mutex );

    if (horizon_pipe_device == -1)
    {
        errno = EMFILE;
        return -1;
    }
    return horizon_pipe_device;
}

int horizon_pipe( int fd[2] )
{
    struct horizon_pipe_file *read_file = NULL;
    struct horizon_pipe_file *write_file = NULL;
    struct horizon_pipe *pipe = NULL;
    int dev;

    fd[0] = -1;
    fd[1] = -1;

    if ((dev = horizon_pipe_install_device()) == -1) return -1;
    if (!(pipe = calloc( 1, sizeof(*pipe) )) ||
        !(read_file = calloc( 1, sizeof(*read_file) )) ||
        !(write_file = calloc( 1, sizeof(*write_file) )))
    {
        free( write_file );
        free( read_file );
        free( pipe );
        errno = ENOMEM;
        return -1;
    }

    pthread_mutex_init( &pipe->mutex, NULL );
    pthread_cond_init( &pipe->can_read, NULL );
    pthread_cond_init( &pipe->can_write, NULL );
    pipe->refs = 2;
    pipe->read_open = 1;
    pipe->write_open = 1;

    read_file->pipe = pipe;
    read_file->write_end = 0;
    write_file->pipe = pipe;
    write_file->write_end = 1;

    if ((fd[0] = __alloc_handle( dev )) == -1)
        goto fail;
    *(struct horizon_pipe_file **)__get_handle( fd[0] )->fileStruct = read_file;
    read_file = NULL;

    if ((fd[1] = __alloc_handle( dev )) == -1)
        goto fail;
    *(struct horizon_pipe_file **)__get_handle( fd[1] )->fileStruct = write_file;
    write_file = NULL;

    return 0;

fail:
    if (fd[0] != -1) close( fd[0] );
    else if (pipe) horizon_pipe_release( pipe );
    if (fd[1] != -1) close( fd[1] );
    else if (pipe) horizon_pipe_release( pipe );
    free( write_file );
    free( read_file );
    errno = EMFILE;
    return -1;
}

static void horizon_fd_queue_push( struct horizon_fd_queue *queue, int fd, unsigned int handle )
{
    struct horizon_fd_message *message = malloc( sizeof(*message) );

    if (!message)
    {
        close( fd );
        fprintf( stderr, "wine: out of memory queueing Horizon server fd.\n" );
        exit(1);
    }

    message->fd = fd;
    message->handle = handle;
    message->next = NULL;

    pthread_mutex_lock( &queue->mutex );
    if (queue->tail) queue->tail->next = message;
    else queue->head = message;
    queue->tail = message;
    pthread_cond_signal( &queue->cond );
    pthread_mutex_unlock( &queue->mutex );
}

static void horizon_fd_queue_push_dup( struct horizon_fd_queue *queue, int fd, unsigned int handle )
{
    int passed_fd = dup( fd );

    if (passed_fd == -1)
    {
        fprintf( stderr, "wine: failed to duplicate Horizon server fd: %s\n", strerror(errno) );
        exit(1);
    }

    horizon_fd_queue_push( queue, passed_fd, handle );
}

static int horizon_fd_queue_pop( struct horizon_fd_queue *queue, unsigned int *handle )
{
    struct horizon_fd_message *message;
    int fd;

    pthread_mutex_lock( &queue->mutex );
    while (!queue->head)
        pthread_cond_wait( &queue->cond, &queue->mutex );

    message = queue->head;
    queue->head = message->next;
    if (!queue->head) queue->tail = NULL;
    pthread_mutex_unlock( &queue->mutex );

    fd = message->fd;
    if (handle) *handle = message->handle;
    free( message );
    return fd;
}

void horizon_server_queue_fd( int fd, unsigned int handle )
{
    horizon_fd_queue_push_dup( &horizon_server_to_client_fds, fd, handle );
}

int horizon_server_take_client_fd( unsigned int *handle )
{
    return horizon_fd_queue_pop( &horizon_client_to_server_fds, handle );
}

static int horizon_read_exact( int fd, void *buffer, size_t size )
{
    char *ptr = buffer;

    while (size)
    {
        ssize_t ret = read( fd, ptr, size );

        if (ret > 0)
        {
            ptr += ret;
            size -= ret;
            continue;
        }
        if (!ret) return 0;
        if (errno == EINTR) continue;
        return -1;
    }
    return 1;
}

static int horizon_write_exact( int fd, const void *buffer, size_t size )
{
    const char *ptr = buffer;

    while (size)
    {
        ssize_t ret = write( fd, ptr, size );

        if (ret > 0)
        {
            ptr += ret;
            size -= ret;
            continue;
        }
        if (!ret) return 0;
        if (errno == EINTR) continue;
        return -1;
    }
    return 1;
}

static int horizon_server_write_reply( int fd, const void *reply, size_t reply_size,
                                       const void *data, size_t data_size )
{
    unsigned char message[HORIZON_SERVER_FIXED_MESSAGE_SIZE] = {0};

    if (reply_size > sizeof(message))
    {
        errno = EOVERFLOW;
        return -1;
    }

    memcpy( message, reply, reply_size );
    if (horizon_write_exact( fd, message, sizeof(message) ) <= 0) return -1;
    if (data_size && horizon_write_exact( fd, data, data_size ) <= 0) return -1;
    return 0;
}

static int horizon_server_write_status( int fd, unsigned int status )
{
    struct horizon_server_reply_header reply = { status, 0 };

    return horizon_server_write_reply( fd, &reply, sizeof(reply), NULL, 0 );
}

static unsigned int horizon_server_alloc_handle(void)
{
    return __sync_add_and_fetch( &horizon_server_next_handle, 4 );
}

static struct horizon_server_handle_entry *horizon_server_find_handle_locked( unsigned int handle )
{
    struct horizon_server_handle_entry *entry;

    for (entry = horizon_server_handles; entry; entry = entry->next)
        if (entry->handle == handle) return entry;

    return NULL;
}

static struct horizon_server_handle_entry *horizon_server_create_handle_locked( int type )
{
    struct horizon_server_object *object = calloc( 1, sizeof(*object) );
    struct horizon_server_handle_entry *entry = calloc( 1, sizeof(*entry) );

    if (!object || !entry)
    {
        free( object );
        free( entry );
        return NULL;
    }

    entry->handle = horizon_server_alloc_handle();
    entry->object = object;
    entry->next = horizon_server_handles;
    horizon_server_handles = entry;

    object->id = entry->handle;
    object->type = type;
    object->refs = 1;
    object->file_fd = -1;
    return entry;
}

static struct horizon_server_handle_entry *horizon_server_create_handle_for_object_locked(
    struct horizon_server_object *object )
{
    struct horizon_server_handle_entry *entry = calloc( 1, sizeof(*entry) );

    if (!entry) return NULL;
    entry->handle = horizon_server_alloc_handle();
    entry->object = object;
    object->refs++;
    entry->next = horizon_server_handles;
    horizon_server_handles = entry;
    return entry;
}

static void horizon_server_free_object( struct horizon_server_object *object )
{
    if (!object) return;
    if (object->file_fd != -1) close( object->file_fd );
    free( object->file_name );
    free( object->dir_mask );
    free( object->name );
    free( object );
}

static unsigned int horizon_server_close_object_handle( unsigned int handle )
{
    struct horizon_server_handle_entry **ptr;
    struct horizon_server_handle_entry *entry;
    struct horizon_server_object *object;

    if (!handle) return HORIZON_STATUS_INVALID_HANDLE;

    pthread_mutex_lock( &horizon_server_objects_mutex );
    for (ptr = &horizon_server_handles; *ptr; ptr = &(*ptr)->next)
    {
        if ((*ptr)->handle != handle) continue;

        entry = *ptr;
        object = entry->object;
        *ptr = entry->next;
        if (object && object->refs && !--object->refs) horizon_server_free_object( object );
        free( entry );
        pthread_mutex_unlock( &horizon_server_objects_mutex );
        return HORIZON_STATUS_SUCCESS;
    }
    pthread_mutex_unlock( &horizon_server_objects_mutex );
    return HORIZON_STATUS_INVALID_HANDLE;
}

static unsigned int horizon_server_duplicate_object_handle( unsigned int handle, unsigned int *new_handle )
{
    struct horizon_server_handle_entry *entry;
    struct horizon_server_handle_entry *duplicate;

    *new_handle = 0;
    if (!handle) return HORIZON_STATUS_INVALID_HANDLE;

    duplicate = calloc( 1, sizeof(*duplicate) );
    if (!duplicate) return HORIZON_STATUS_NO_MEMORY;

    pthread_mutex_lock( &horizon_server_objects_mutex );
    if (!(entry = horizon_server_find_handle_locked( handle )))
    {
        pthread_mutex_unlock( &horizon_server_objects_mutex );
        free( duplicate );
        return HORIZON_STATUS_INVALID_HANDLE;
    }

    duplicate->handle = horizon_server_alloc_handle();
    duplicate->object = entry->object;
    duplicate->object->refs++;
    duplicate->next = horizon_server_handles;
    horizon_server_handles = duplicate;
    *new_handle = duplicate->handle;
    pthread_mutex_unlock( &horizon_server_objects_mutex );
    return HORIZON_STATUS_SUCCESS;
}

static unsigned int horizon_server_parse_object_attributes( const unsigned char *data,
                                                            unsigned int data_size,
                                                            struct horizon_object_name *name )
{
    const struct horizon_object_attributes *attributes;
    unsigned int name_offset;

    memset( name, 0, sizeof(*name) );
    if (!data_size) return HORIZON_STATUS_SUCCESS;
    if (data_size < sizeof(*attributes)) return HORIZON_STATUS_INVALID_PARAMETER;

    attributes = (const void *)data;
    if (attributes->sd_len > data_size - sizeof(*attributes))
        return HORIZON_STATUS_INVALID_PARAMETER;
    name_offset = sizeof(*attributes) + attributes->sd_len;
    if (attributes->name_len > data_size - name_offset)
        return HORIZON_STATUS_INVALID_PARAMETER;

    name->rootdir = attributes->rootdir;
    name->name = data + name_offset;
    name->name_len = attributes->name_len;
    return HORIZON_STATUS_SUCCESS;
}

static unsigned int horizon_server_parse_open_name( const unsigned char *message,
                                                    const unsigned char *data,
                                                    unsigned int data_size,
                                                    struct horizon_object_name *name )
{
    const struct horizon_open_named_object_request *request = (const void *)message;

    memset( name, 0, sizeof(*name) );
    name->rootdir = request->rootdir;
    name->name = data;
    name->name_len = data_size;
    return HORIZON_STATUS_SUCCESS;
}

static unsigned int horizon_server_object_attributes_size( const unsigned char *data,
                                                           unsigned int data_size,
                                                           unsigned int *attr_size )
{
    const struct horizon_object_attributes *attributes;
    unsigned int size;

    *attr_size = 0;
    if (!data_size) return HORIZON_STATUS_SUCCESS;
    if (data_size < sizeof(*attributes)) return HORIZON_STATUS_INVALID_PARAMETER;

    attributes = (const void *)data;
    if (attributes->sd_len > data_size - sizeof(*attributes))
        return HORIZON_STATUS_INVALID_PARAMETER;
    size = sizeof(*attributes) + attributes->sd_len;
    if (attributes->name_len > data_size - size)
        return HORIZON_STATUS_INVALID_PARAMETER;
    size = (size + attributes->name_len + 3) & ~3u;
    if (size > data_size) return HORIZON_STATUS_INVALID_PARAMETER;

    *attr_size = size;
    return HORIZON_STATUS_SUCCESS;
}

static unsigned int horizon_server_errno_status( int error )
{
    switch (error)
    {
    case 0:
        return HORIZON_STATUS_SUCCESS;
    case EACCES:
    case EPERM:
        return HORIZON_STATUS_ACCESS_DENIED;
    case EEXIST:
        return HORIZON_STATUS_OBJECT_NAME_COLLISION;
    case ENOENT:
        return HORIZON_STATUS_NO_SUCH_FILE;
    case ENOTDIR:
        return HORIZON_STATUS_OBJECT_PATH_NOT_FOUND;
    case EMFILE:
    case ENFILE:
        return HORIZON_STATUS_TOO_MANY_OPENED_FILES;
    case ENOMEM:
        return HORIZON_STATUS_NO_MEMORY;
    default:
        return HORIZON_STATUS_INVALID_PARAMETER;
    }
}

static unsigned int horizon_server_read_exact_at( int fd, unsigned long long offset,
                                                  void *buffer, size_t size )
{
    unsigned char *ptr = buffer;

    if (lseek( fd, (off_t)offset, SEEK_SET ) == (off_t)-1)
        return horizon_server_errno_status( errno );

    while (size)
    {
        ssize_t ret = read( fd, ptr, size );

        if (ret > 0)
        {
            ptr += ret;
            size -= ret;
            continue;
        }
        if (!ret) return HORIZON_STATUS_INVALID_IMAGE_FORMAT;
        if (errno == EINTR) continue;
        return horizon_server_errno_status( errno );
    }

    return HORIZON_STATUS_SUCCESS;
}

static unsigned int horizon_server_write_exact_at( int fd, unsigned long long offset,
                                                   const void *buffer, size_t size )
{
    const unsigned char *ptr = buffer;

    if (lseek( fd, (off_t)offset, SEEK_SET ) == (off_t)-1)
        return horizon_server_errno_status( errno );

    while (size)
    {
        ssize_t ret = write( fd, ptr, size );

        if (ret > 0)
        {
            ptr += ret;
            size -= ret;
            continue;
        }
        if (!ret) return HORIZON_STATUS_UNSUCCESSFUL;
        if (errno == EINTR) continue;
        return horizon_server_errno_status( errno );
    }

    return HORIZON_STATUS_SUCCESS;
}

static unsigned short horizon_get_le16( const unsigned char *ptr )
{
    return ptr[0] | ((unsigned short)ptr[1] << 8);
}

static unsigned int horizon_get_le32( const unsigned char *ptr )
{
    return ptr[0] | ((unsigned int)ptr[1] << 8) |
           ((unsigned int)ptr[2] << 16) | ((unsigned int)ptr[3] << 24);
}

static unsigned long long horizon_get_le64( const unsigned char *ptr )
{
    return (unsigned long long)horizon_get_le32( ptr ) |
           ((unsigned long long)horizon_get_le32( ptr + 4 ) << 32);
}

static unsigned int horizon_round_up_u32( unsigned int value, unsigned int align )
{
    if (!align) return value;
    return (value + align - 1) & ~(align - 1);
}

static unsigned int horizon_server_read_pe_image_info( int fd, struct horizon_pe_image_info *info )
{
    unsigned char dos[64], nt[24];
    unsigned char *headers = NULL;
    unsigned int status = HORIZON_STATUS_SUCCESS;
    unsigned int pe_offset, opt_size, section_count, headers_size;
    unsigned int size_of_image, section_alignment, size_of_headers;
    unsigned int i;
    unsigned short machine, characteristics, dll_charact;
    struct stat st;

    memset( info, 0, sizeof(*info) );

    if (fstat( fd, &st ) == -1) return horizon_server_errno_status( errno );
    if (st.st_size < (off_t)(sizeof(dos) + sizeof(nt))) return HORIZON_STATUS_INVALID_IMAGE_FORMAT;

    if ((status = horizon_server_read_exact_at( fd, 0, dos, sizeof(dos) )))
        return status;
    if (horizon_get_le16( dos ) != 0x5a4d) return HORIZON_STATUS_INVALID_IMAGE_FORMAT;

    pe_offset = horizon_get_le32( dos + 0x3c );
    if (pe_offset > (unsigned long long)st.st_size - sizeof(nt))
        return HORIZON_STATUS_INVALID_IMAGE_FORMAT;

    if ((status = horizon_server_read_exact_at( fd, pe_offset, nt, sizeof(nt) )))
        return status;
    if (memcmp( nt, "PE\0\0", 4 )) return HORIZON_STATUS_INVALID_IMAGE_FORMAT;

    machine = horizon_get_le16( nt + 4 );
    section_count = horizon_get_le16( nt + 6 );
    opt_size = horizon_get_le16( nt + 20 );
    characteristics = horizon_get_le16( nt + 22 );

    if (machine != HORIZON_IMAGE_FILE_MACHINE_ARM64)
        return HORIZON_STATUS_INVALID_IMAGE_FORMAT;
    if (!section_count || section_count > 128 || opt_size < 112)
        return HORIZON_STATUS_INVALID_IMAGE_FORMAT;
    if (opt_size > 4096 || section_count > (0x10000 - opt_size) / 40)
        return HORIZON_STATUS_INVALID_IMAGE_FORMAT;

    headers_size = opt_size + section_count * 40;
    if (pe_offset + sizeof(nt) > (unsigned long long)st.st_size ||
        headers_size > (unsigned long long)st.st_size - pe_offset - sizeof(nt))
        return HORIZON_STATUS_INVALID_IMAGE_FORMAT;

    if (!(headers = malloc( headers_size ))) return HORIZON_STATUS_NO_MEMORY;
    status = horizon_server_read_exact_at( fd, pe_offset + sizeof(nt), headers, headers_size );
    if (status) goto done;

    if (horizon_get_le16( headers ) != HORIZON_IMAGE_NT_OPTIONAL_HDR64_MAGIC)
    {
        status = HORIZON_STATUS_INVALID_IMAGE_FORMAT;
        goto done;
    }

    section_alignment = horizon_get_le32( headers + 32 );
    size_of_image = horizon_get_le32( headers + 56 );
    size_of_headers = horizon_get_le32( headers + 60 );
    dll_charact = horizon_get_le16( headers + 70 );

    if (!section_alignment || !size_of_image || size_of_headers > size_of_image)
    {
        status = HORIZON_STATUS_INVALID_IMAGE_FORMAT;
        goto done;
    }

    info->base = horizon_get_le64( headers + 24 );
    info->stack_size = horizon_get_le64( headers + 72 );
    info->stack_commit = horizon_get_le64( headers + 80 );
    info->entry_point = horizon_get_le32( headers + 16 );
    info->map_size = size_of_image;
    info->alignment = section_alignment;
    info->zerobits = 0;
    info->subsystem = horizon_get_le16( headers + 68 );
    info->subsystem_minor = horizon_get_le16( headers + 50 );
    info->subsystem_major = horizon_get_le16( headers + 48 );
    info->osversion_major = horizon_get_le16( headers + 40 );
    info->osversion_minor = horizon_get_le16( headers + 42 );
    info->image_charact = characteristics;
    info->dll_charact = dll_charact;
    info->machine = machine;
    info->loader_flags = horizon_get_le32( headers + 104 );
    info->header_size = size_of_headers;
    info->header_map_size = horizon_round_up_u32( size_of_headers, 0x1000 );
    info->file_size = st.st_size > 0xffffffffll ? 0xffffffffu : (unsigned int)st.st_size;
    info->checksum = horizon_get_le32( headers + 64 );

    if (dll_charact & HORIZON_IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE)
        info->image_flags |= HORIZON_IMAGE_FLAGS_IMAGE_DYNAMICALLY_RELOCATED;
    if (section_alignment < 0x1000)
        info->image_flags |= HORIZON_IMAGE_FLAGS_IMAGE_MAPPED_FLAT;

    for (i = 0; i < section_count; i++)
    {
        const unsigned char *section = headers + opt_size + i * 40;

        if (horizon_get_le32( section + 36 ) & HORIZON_IMAGE_SCN_CNT_CODE)
            info->contains_code = 1;
    }

done:
    free( headers );
    return status;
}

static unsigned int horizon_server_utf16_name_len( const char *name )
{
    return name ? strlen( name ) * sizeof(unsigned short) : 0;
}

static void horizon_server_write_utf16_name( unsigned char *dst, const char *name )
{
    unsigned short *wide = (unsigned short *)dst;

    for (; name && *name; name++, wide++)
        *wide = (unsigned char)*name;
}

/* Real Wine's wineserver maps GENERIC_READ/WRITE/EXECUTE/ALL to their
 * object-type-specific rights before caching or acting on an access mask
 * (server/object.h's map_access(), applied via server/file.c's file_type
 * mapping = { FILE_GENERIC_READ, FILE_GENERIC_WRITE, FILE_GENERIC_EXECUTE,
 * FILE_ALL_ACCESS }). horizon.c, this port's in-process replacement for
 * that server, never did this step -- every CreateFileW(GENERIC_WRITE, ...)
 * handle carries the raw 0x40000000 GENERIC_WRITE bit forever, which shares
 * no bits with FILE_WRITE_DATA/FILE_APPEND_DATA, so open() picked O_RDONLY
 * here and server_get_unix_fd()'s bitwise access check (dlls/ntdll/unix/
 * server.c) denied every subsequent WriteFile()/FlushFileBuffers() with
 * STATUS_ACCESS_DENIED -- silently, since callers rarely check those
 * return values. Confirmed via gui_smoke.c's gui_timing.log being
 * permanently empty despite running for many seconds past its first
 * 1-second flush window. This mirrors server/object.h's map_access()
 * exactly, scoped to the FILE object type only (the one concretely
 * reproduced here) -- not applied to the SOCK path (horizon_server_handle_
 * open_file_object already hardcodes full access for sockets regardless of
 * what's cached, so it isn't affected) or the SECTION-rights mapping_access
 * field in horizon_server_handle_create_mapping (a different rights
 * namespace this hasn't been shown to need). */
static unsigned int horizon_server_map_generic_access( unsigned int access )
{
    if (access & GENERIC_READ)    access |= FILE_GENERIC_READ;
    if (access & GENERIC_WRITE)   access |= FILE_GENERIC_WRITE;
    if (access & GENERIC_EXECUTE) access |= FILE_GENERIC_EXECUTE;
    if (access & GENERIC_ALL)     access |= FILE_ALL_ACCESS;
    return access & ~(GENERIC_READ | GENERIC_WRITE | GENERIC_EXECUTE | GENERIC_ALL);
}

static unsigned int horizon_server_file_open_flags( unsigned int access,
                                                    const struct horizon_create_file_request *request,
                                                    int *flags )
{
    int wants_read = access & FILE_READ_DATA;
    int wants_write = access & (FILE_WRITE_DATA | FILE_APPEND_DATA);

    if (wants_read && wants_write) *flags = O_RDWR;
    else if (wants_write) *flags = O_WRONLY;
    else *flags = O_RDONLY;

    if (access & FILE_APPEND_DATA) *flags |= O_APPEND;

    switch (request->create)
    {
    case FILE_CREATE:
        *flags |= O_CREAT | O_EXCL;
        break;
    case FILE_OPEN:
        break;
    case FILE_OPEN_IF:
        *flags |= O_CREAT;
        break;
    case FILE_OVERWRITE:
        *flags |= O_TRUNC;
        break;
    case FILE_OVERWRITE_IF:
    case FILE_SUPERSEDE:
        *flags |= O_CREAT | O_TRUNC;
        break;
    default:
        return HORIZON_STATUS_INVALID_PARAMETER;
    }

    return HORIZON_STATUS_SUCCESS;
}

static int horizon_server_name_matches( const struct horizon_server_object *object, int type,
                                        const struct horizon_object_name *name )
{
    if (!name->name_len || !object->name_len) return 0;
    if (object->rootdir != name->rootdir) return 0;
    if (object->name_len != name->name_len) return 0;
    if (memcmp( object->name, name->name, name->name_len )) return 0;
    return !type || object->type == type;
}

static struct horizon_server_object *horizon_server_find_named_object_any_locked(
    const struct horizon_object_name *name )
{
    struct horizon_server_handle_entry *entry;

    for (entry = horizon_server_handles; entry; entry = entry->next)
        if (horizon_server_name_matches( entry->object, 0, name )) return entry->object;

    return NULL;
}

static struct horizon_server_object *horizon_server_find_named_object_locked(
    int type, const struct horizon_object_name *name )
{
    struct horizon_server_handle_entry *entry;

    for (entry = horizon_server_handles; entry; entry = entry->next)
        if (horizon_server_name_matches( entry->object, type, name )) return entry->object;

    return NULL;
}

static unsigned int horizon_server_set_object_name( struct horizon_server_object *object,
                                                    const struct horizon_object_name *name )
{
    if (!name->name_len) return HORIZON_STATUS_SUCCESS;

    object->name = malloc( name->name_len );
    if (!object->name) return HORIZON_STATUS_NO_MEMORY;
    memcpy( object->name, name->name, name->name_len );
    object->name_len = name->name_len;
    object->rootdir = name->rootdir;
    return HORIZON_STATUS_SUCCESS;
}

static unsigned int horizon_server_create_named_object_handle_locked(
    int type, const struct horizon_object_name *name, struct horizon_server_handle_entry **entry )
{
    struct horizon_server_object *object;

    *entry = NULL;
    if ((object = horizon_server_find_named_object_any_locked( name )))
    {
        if (object->type != type) return HORIZON_STATUS_OBJECT_TYPE_MISMATCH;
        if (!(*entry = horizon_server_create_handle_for_object_locked( object )))
            return HORIZON_STATUS_NO_MEMORY;
        return HORIZON_STATUS_OBJECT_NAME_EXISTS;
    }

    if (!(*entry = horizon_server_create_handle_locked( type )))
        return HORIZON_STATUS_NO_MEMORY;
    if (horizon_server_set_object_name( (*entry)->object, name ) != HORIZON_STATUS_SUCCESS)
    {
        struct horizon_server_handle_entry *failed = *entry;

        horizon_server_handles = failed->next;
        horizon_server_free_object( failed->object );
        free( failed );
        *entry = NULL;
        return HORIZON_STATUS_NO_MEMORY;
    }
    return HORIZON_STATUS_SUCCESS;
}

static unsigned int horizon_server_open_named_object_handle( int type,
                                                            const struct horizon_object_name *name,
                                                            unsigned int *handle )
{
    struct horizon_server_object *object;
    struct horizon_server_handle_entry *entry;

    *handle = 0;
    if (!name->name_len) return HORIZON_STATUS_OBJECT_NAME_NOT_FOUND;

    pthread_mutex_lock( &horizon_server_objects_mutex );
    object = horizon_server_find_named_object_locked( type, name );
    if (!object)
    {
        pthread_mutex_unlock( &horizon_server_objects_mutex );
        return HORIZON_STATUS_OBJECT_NAME_NOT_FOUND;
    }
    entry = horizon_server_create_handle_for_object_locked( object );
    if (entry) *handle = entry->handle;
    pthread_mutex_unlock( &horizon_server_objects_mutex );
    return entry ? HORIZON_STATUS_SUCCESS : HORIZON_STATUS_NO_MEMORY;
}

static unsigned int horizon_server_create_object_handle( int type, unsigned int *handle )
{
    struct horizon_server_handle_entry *entry;

    *handle = 0;
    pthread_mutex_lock( &horizon_server_objects_mutex );
    entry = horizon_server_create_handle_locked( type );
    if (entry) *handle = entry->handle;
    pthread_mutex_unlock( &horizon_server_objects_mutex );
    return entry ? HORIZON_STATUS_SUCCESS : HORIZON_STATUS_NO_MEMORY;
}

static unsigned char horizon_ascii_tolower( unsigned char ch )
{
    if (ch >= 'A' && ch <= 'Z') return ch - 'A' + 'a';
    return ch;
}

static int horizon_utf16_name_equals( const unsigned char *left, unsigned int left_len,
                                      const unsigned char *right, unsigned int right_len )
{
    unsigned int i;

    if (left_len != right_len) return 0;
    if (left_len & 1) return 0;
    for (i = 0; i < left_len; i += 2)
    {
        if (left[i + 1] || right[i + 1])
        {
            if (left[i] != right[i] || left[i + 1] != right[i + 1]) return 0;
        }
        else if (horizon_ascii_tolower( left[i] ) != horizon_ascii_tolower( right[i] ))
            return 0;
    }
    return 1;
}

static int horizon_utf16_name_equals_ascii( const unsigned char *name, unsigned int name_len,
                                            const char *ascii )
{
    unsigned int i;

    for (i = 0; ascii[i]; i++) {}
    if (name_len != i * sizeof(unsigned short)) return 0;
    for (i = 0; ascii[i]; i++)
    {
        if (name[i * 2 + 1]) return 0;
        if (horizon_ascii_tolower( name[i * 2] ) != horizon_ascii_tolower( ascii[i] )) return 0;
    }
    return 1;
}

static int horizon_utf16_name_has_ascii_suffix( const unsigned char *name, unsigned int name_len,
                                                const char *suffix )
{
    unsigned int suffix_len, offset;

    for (suffix_len = 0; suffix[suffix_len]; suffix_len++) {}
    suffix_len *= sizeof(unsigned short);
    if (name_len < suffix_len) return 0;
    offset = name_len - suffix_len;
    return horizon_utf16_name_equals_ascii( name + offset, suffix_len, suffix );
}

static unsigned int horizon_utf16_from_ascii( unsigned char *buffer, unsigned int buffer_size,
                                              const char *ascii )
{
    unsigned int i;

    for (i = 0; ascii[i] && i * 2 + 1 < buffer_size; i++)
    {
        buffer[i * 2] = ascii[i];
        buffer[i * 2 + 1] = 0;
    }
    return i * sizeof(unsigned short);
}

static struct horizon_atom_entry *horizon_server_find_atom_name_locked( const unsigned char *name,
                                                                        unsigned int name_len )
{
    struct horizon_atom_entry *entry;

    for (entry = horizon_atoms; entry; entry = entry->next)
        if (horizon_utf16_name_equals( entry->name, entry->name_len, name, name_len )) return entry;
    return NULL;
}

static unsigned int horizon_server_add_atom_locked( const unsigned char *name, unsigned int name_len,
                                                    unsigned int requested_atom, unsigned int *atom )
{
    struct horizon_atom_entry *entry;

    *atom = 0;
    if (!name_len && requested_atom)
    {
        *atom = requested_atom;
        return HORIZON_STATUS_SUCCESS;
    }
    if (!name_len || name_len > HORIZON_MAX_ATOM_LEN * sizeof(unsigned short))
        return HORIZON_STATUS_INVALID_PARAMETER;
    if ((entry = horizon_server_find_atom_name_locked( name, name_len )))
    {
        *atom = entry->atom;
        return HORIZON_STATUS_SUCCESS;
    }
    if (!(entry = calloc( 1, sizeof(*entry) ))) return HORIZON_STATUS_NO_MEMORY;
    if (!(entry->name = malloc( name_len )))
    {
        free( entry );
        return HORIZON_STATUS_NO_MEMORY;
    }
    memcpy( entry->name, name, name_len );
    entry->name_len = name_len;
    entry->atom = requested_atom ? requested_atom : horizon_next_atom++;
    entry->next = horizon_atoms;
    horizon_atoms = entry;
    *atom = entry->atom;
    return HORIZON_STATUS_SUCCESS;
}

static unsigned int horizon_server_find_atom_locked( const unsigned char *name, unsigned int name_len,
                                                     unsigned int *atom )
{
    struct horizon_atom_entry *entry;

    *atom = 0;
    if ((entry = horizon_server_find_atom_name_locked( name, name_len ))) *atom = entry->atom;
    return HORIZON_STATUS_SUCCESS;
}

static unsigned int horizon_server_ensure_session_locked(void)
{
    char path[128];
    int fd;

    if (horizon_session_fd != -1) return HORIZON_STATUS_SUCCESS;
    if (!(horizon_session_data = calloc( 1, HORIZON_SESSION_MAPPING_SIZE )))
        return HORIZON_STATUS_NO_MEMORY;

    snprintf( path, sizeof(path), "sdmc:/switch/wine/wine-nx-session-%u.shm", (unsigned int)getpid() );
    fd = open( path, O_RDWR | O_CREAT | O_TRUNC, 0600 );
    if (fd == -1)
    {
        snprintf( path, sizeof(path), "wine-nx-session-%u.shm", (unsigned int)getpid() );
        fd = open( path, O_RDWR | O_CREAT | O_TRUNC, 0600 );
    }
    if (fd == -1)
    {
        free( horizon_session_data );
        horizon_session_data = NULL;
        return horizon_server_errno_status( errno );
    }
    if (ftruncate( fd, HORIZON_SESSION_MAPPING_SIZE ) == -1)
    {
        static const char zero;

        if (lseek( fd, HORIZON_SESSION_MAPPING_SIZE - 1, SEEK_SET ) == (off_t)-1 ||
            write( fd, &zero, 1 ) != 1)
        {
            unsigned int status = horizon_server_errno_status( errno );

            close( fd );
            free( horizon_session_data );
            horizon_session_data = NULL;
            return status;
        }
    }
    horizon_session_fd = fd;
    return HORIZON_STATUS_SUCCESS;
}

static unsigned int horizon_server_flush_session_range_locked( unsigned long long offset,
                                                               unsigned long long size )
{
    struct horizon_session_view *view;
    unsigned int status;

    if (!size) return HORIZON_STATUS_SUCCESS;
    if (offset + size > HORIZON_SESSION_MAPPING_SIZE) return HORIZON_STATUS_INVALID_PARAMETER;
    status = horizon_server_write_exact_at( horizon_session_fd, offset, horizon_session_data + offset, size );
    if (status) return status;

    for (view = horizon_session_views; view; view = view->next)
    {
        unsigned long long start = offset > view->offset ? offset : view->offset;
        unsigned long long end = offset + size < view->offset + view->size ? offset + size : view->offset + view->size;

        if (start >= end) continue;
        memcpy( (void *)(ULONG_PTR)(view->base + start - view->offset),
                horizon_session_data + start, end - start );
    }
    return HORIZON_STATUS_SUCCESS;
}

static unsigned int horizon_server_alloc_shared_object_locked( unsigned long long shm_size,
                                                               struct horizon_obj_locator *locator )
{
    struct horizon_shared_object *object;
    unsigned long long offset, size;
    unsigned int status;

    memset( locator, 0, sizeof(*locator) );
    if ((status = horizon_server_ensure_session_locked())) return status;

    offset = (horizon_session_used + 7) & ~7ull;
    size = offsetof( struct horizon_shared_object, shm ) + shm_size;
    size = (size + 7) & ~7ull;
    if (!shm_size || offset + size > HORIZON_SESSION_MAPPING_SIZE)
        return HORIZON_STATUS_NO_MEMORY;

    object = (struct horizon_shared_object *)(horizon_session_data + offset);
    memset( object, 0, size );
    object->id = ++horizon_session_next_id;
    horizon_session_used = offset + size;
    locator->id = object->id;
    locator->offset = offset;
    return horizon_server_flush_session_range_locked( offset, size );
}

static struct horizon_shared_object *horizon_server_shared_object_locked( struct horizon_obj_locator locator )
{
    if (!locator.id || locator.offset + sizeof(struct horizon_shared_object) > HORIZON_SESSION_MAPPING_SIZE)
        return NULL;
    return (struct horizon_shared_object *)(horizon_session_data + locator.offset);
}

static unsigned int horizon_server_ensure_input_locked(void)
{
    struct horizon_shared_object *shared;
    unsigned int status;

    if (horizon_input_locator.id) return HORIZON_STATUS_SUCCESS;
    if ((status = horizon_server_alloc_shared_object_locked( sizeof(struct horizon_input_shm),
                                                             &horizon_input_locator )))
        return status;
    shared = horizon_server_shared_object_locked( horizon_input_locator );
    shared->shm.input.foreground = 1;
    shared->shm.input.cursor_count = 0;
    return horizon_server_flush_session_range_locked(
        horizon_input_locator.offset,
        offsetof( struct horizon_shared_object, shm ) + sizeof(struct horizon_input_shm) );
}

static struct horizon_input_shm *horizon_server_input_shared_locked(void)
{
    struct horizon_shared_object *shared;

    if (horizon_server_ensure_input_locked()) return NULL;
    shared = horizon_server_shared_object_locked( horizon_input_locator );
    return shared ? &shared->shm.input : NULL;
}

static unsigned int horizon_server_flush_input_locked(void)
{
    if (!horizon_input_locator.id) return HORIZON_STATUS_INVALID_HANDLE;
    return horizon_server_flush_session_range_locked(
        horizon_input_locator.offset,
        offsetof( struct horizon_shared_object, shm ) + sizeof(struct horizon_input_shm) );
}

static unsigned int horizon_server_note_session_view_locked( unsigned long long base,
                                                             unsigned long long offset,
                                                             unsigned long long size )
{
    struct horizon_session_view *view;

    if (!base || !size) return HORIZON_STATUS_SUCCESS;
    if (offset + size > HORIZON_SESSION_MAPPING_SIZE) return HORIZON_STATUS_INVALID_PARAMETER;
    for (view = horizon_session_views; view; view = view->next)
    {
        if (view->base != base) continue;
        view->offset = offset;
        view->size = size;
        horizon_mprotect( (void *)(ULONG_PTR)base, size, PROT_READ | PROT_WRITE );
        return HORIZON_STATUS_SUCCESS;
    }
    if (!(view = calloc( 1, sizeof(*view) ))) return HORIZON_STATUS_NO_MEMORY;
    view->base = base;
    view->offset = offset;
    view->size = size;
    view->next = horizon_session_views;
    horizon_session_views = view;
    horizon_mprotect( (void *)(ULONG_PTR)base, size, PROT_READ | PROT_WRITE );
    return HORIZON_STATUS_SUCCESS;
}

static void horizon_server_remove_session_view_locked( unsigned long long base )
{
    struct horizon_session_view **ptr;

    for (ptr = &horizon_session_views; *ptr; ptr = &(*ptr)->next)
    {
        struct horizon_session_view *view = *ptr;

        if (view->base != base) continue;
        *ptr = view->next;
        free( view );
        return;
    }
}

static unsigned int horizon_server_alloc_user_handle_locked( unsigned short type,
                                                             struct horizon_obj_locator locator,
                                                             unsigned int pid, unsigned int tid,
                                                             unsigned int *handle )
{
    struct horizon_user_entry *entry;
    unsigned int index;
    unsigned short generation;
    unsigned long long entry_offset;

    *handle = 0;
    if (!horizon_session_data)
    {
        unsigned int status = horizon_server_ensure_session_locked();
        if (status) return status;
    }

    for (index = 0; index < horizon_user_handle_count; index++)
    {
        entry = &((struct horizon_session_shm *)horizon_session_data)->user_entries[index];
        if (!entry->type) break;
    }
    if (index == HORIZON_MAX_USER_HANDLES) return HORIZON_STATUS_NO_MEMORY;
    if (index == horizon_user_handle_count) horizon_user_handle_count++;
    entry = &((struct horizon_session_shm *)horizon_session_data)->user_entries[index];
    generation = entry->generation + 1;
    if (!generation || generation == 0xffff) generation = 1;
    entry->offset = locator.offset;
    entry->tid = tid ? tid : 1;
    entry->pid = pid ? pid : 1;
    entry->id = locator.id;
    entry->uniq = ((unsigned int)generation << 16) | type;
    *handle = (index << 1) + HORIZON_FIRST_USER_HANDLE + ((unsigned int)generation << 16);
    entry_offset = offsetof( struct horizon_session_shm, user_entries ) + index * sizeof(*entry);
    return horizon_server_flush_session_range_locked( entry_offset, sizeof(*entry) );
}

static int horizon_server_handle_alloc_user_handle( struct horizon_server_connection *connection,
                                                    const unsigned char *message )
{
    const struct horizon_alloc_user_handle_request *request = (const void *)message;
    struct horizon_alloc_user_handle_reply reply;
    struct horizon_obj_locator locator = {0};

    memset( &reply, 0, sizeof(reply) );
    pthread_mutex_lock( &horizon_server_objects_mutex );
    reply.header.error = horizon_server_alloc_user_handle_locked( request->type, locator,
                                                                  connection->pid, connection->tid,
                                                                  &reply.handle );
    pthread_mutex_unlock( &horizon_server_objects_mutex );
    horizon_trace( "[HZUSER] alloc_user_handle type=%u -> %08x err=%08x\n",
                   request->type, reply.handle, reply.header.error );
    return horizon_server_write_reply( connection->reply_fd, &reply, sizeof(reply), NULL, 0 );
}

static int horizon_server_handle_free_user_handle( struct horizon_server_connection *connection,
                                                   const unsigned char *message )
{
    const struct horizon_free_user_handle_request *request = (const void *)message;
    struct horizon_server_reply_header reply;
    struct horizon_user_entry *entry = NULL;
    unsigned int low = request->handle & 0xffff;
    unsigned int index = HORIZON_MAX_USER_HANDLES;
    unsigned long long entry_offset;

    memset( &reply, 0, sizeof(reply) );
    pthread_mutex_lock( &horizon_server_objects_mutex );
    if (low >= HORIZON_FIRST_USER_HANDLE && !((low - HORIZON_FIRST_USER_HANDLE) & 1))
        index = (low - HORIZON_FIRST_USER_HANDLE) >> 1;
    if (index < horizon_user_handle_count && horizon_session_data)
        entry = &((struct horizon_session_shm *)horizon_session_data)->user_entries[index];
    if (!entry || entry->type != request->type ||
        entry->generation != (request->handle >> 16))
        reply.error = HORIZON_STATUS_INVALID_HANDLE;
    else
    {
        entry->offset = 0;
        entry->tid = 0;
        entry->pid = 0;
        entry->id = 0;
        entry->uniq = (unsigned int)entry->generation << 16;
        entry_offset = offsetof( struct horizon_session_shm, user_entries ) + index * sizeof(*entry);
        reply.error = horizon_server_flush_session_range_locked( entry_offset, sizeof(*entry) );
    }
    pthread_mutex_unlock( &horizon_server_objects_mutex );
    horizon_trace( "[HZUSER] free_user_handle type=%u hwnd=%08x err=%08x\n",
                   request->type, request->handle, reply.error );
    return horizon_server_write_reply( connection->reply_fd, &reply, sizeof(reply), NULL, 0 );
}

static struct horizon_user_window *horizon_server_find_window_locked( unsigned int handle )
{
    struct horizon_user_window *window;

    for (window = horizon_windows; window; window = window->next)
        if (window->handle == handle) return window;
    return NULL;
}

static struct horizon_server_object *horizon_server_find_handle_object_locked( unsigned int handle,
                                                                               int type )
{
    struct horizon_server_handle_entry *entry;

    if (!handle) return NULL;
    if (!(entry = horizon_server_find_handle_locked( handle ))) return NULL;
    if (type && entry->object->type != type) return NULL;
    return entry->object;
}

static unsigned int horizon_server_compare_object_handles( unsigned int first, unsigned int second )
{
    struct horizon_server_handle_entry *first_entry;
    struct horizon_server_handle_entry *second_entry;
    unsigned int status;

    if (!first || !second) return HORIZON_STATUS_INVALID_HANDLE;

    pthread_mutex_lock( &horizon_server_objects_mutex );
    first_entry = horizon_server_find_handle_locked( first );
    second_entry = horizon_server_find_handle_locked( second );
    if (!first_entry || !second_entry) status = HORIZON_STATUS_INVALID_HANDLE;
    else if (first_entry->object == second_entry->object) status = HORIZON_STATUS_SUCCESS;
    else status = HORIZON_STATUS_NOT_SAME_OBJECT;
    pthread_mutex_unlock( &horizon_server_objects_mutex );
    return status;
}

static unsigned int horizon_server_find_typed_object_locked( unsigned int handle, int type,
                                                             struct horizon_server_object **object )
{
    struct horizon_server_handle_entry *entry;

    if (!handle) return HORIZON_STATUS_INVALID_HANDLE;
    if (!(entry = horizon_server_find_handle_locked( handle ))) return HORIZON_STATUS_INVALID_HANDLE;
    if (entry->object->type != type) return HORIZON_STATUS_OBJECT_TYPE_MISMATCH;
    *object = entry->object;
    return HORIZON_STATUS_SUCCESS;
}

static int horizon_server_object_is_signaled( const struct horizon_server_object *object )
{
    switch (object->type)
    {
    case HORIZON_SERVER_OBJECT_EVENT:
        return object->signaled;
    case HORIZON_SERVER_OBJECT_MUTEX:
        return !object->owned;
    case HORIZON_SERVER_OBJECT_SEMAPHORE:
        return object->count > 0;
    case HORIZON_SERVER_OBJECT_TIMER:
        return object->signaled;
    case HORIZON_SERVER_OBJECT_PROCESS:
    case HORIZON_SERVER_OBJECT_THREAD:
    case HORIZON_SERVER_OBJECT_RESERVE:
    case HORIZON_SERVER_OBJECT_KEYED_EVENT:
        return 1;
    default:
        return 0;
    }
}

static void horizon_server_consume_signal( struct horizon_server_object *object )
{
    switch (object->type)
    {
    case HORIZON_SERVER_OBJECT_EVENT:
        if (!object->manual_reset) object->signaled = 0;
        break;
    case HORIZON_SERVER_OBJECT_MUTEX:
        object->owned = 1;
        object->count = 1;
        break;
    case HORIZON_SERVER_OBJECT_SEMAPHORE:
        if (object->count) object->count--;
        break;
    case HORIZON_SERVER_OBJECT_TIMER:
        if (!object->manual_reset) object->signaled = 0;
        break;
    default:
        break;
    }
}

static unsigned int horizon_server_signal_object_locked( unsigned int handle )
{
    struct horizon_server_handle_entry *entry;
    struct horizon_server_object *object;

    if (!handle) return HORIZON_STATUS_INVALID_HANDLE;
    if (!(entry = horizon_server_find_handle_locked( handle ))) return HORIZON_STATUS_INVALID_HANDLE;

    object = entry->object;
    switch (object->type)
    {
    case HORIZON_SERVER_OBJECT_EVENT:
        object->signaled = 1;
        return HORIZON_STATUS_SUCCESS;
    case HORIZON_SERVER_OBJECT_MUTEX:
        if (!object->owned || !object->count) return HORIZON_STATUS_MUTANT_NOT_OWNED;
        if (!--object->count) object->owned = 0;
        return HORIZON_STATUS_SUCCESS;
    case HORIZON_SERVER_OBJECT_SEMAPHORE:
        if (object->count == object->max) return HORIZON_STATUS_SEMAPHORE_LIMIT_EXCEEDED;
        object->count++;
        return HORIZON_STATUS_SUCCESS;
    default:
        return HORIZON_STATUS_OBJECT_TYPE_MISMATCH;
    }
}

static unsigned int horizon_server_wait_object_locked( unsigned int handle, int consume )
{
    struct horizon_server_handle_entry *entry;

    if (!handle) return HORIZON_STATUS_INVALID_HANDLE;
    if (!(entry = horizon_server_find_handle_locked( handle ))) return HORIZON_STATUS_INVALID_HANDLE;
    if (!horizon_server_object_is_signaled( entry->object )) return HORIZON_STATUS_TIMEOUT;
    if (consume) horizon_server_consume_signal( entry->object );
    return HORIZON_STATUS_SUCCESS;
}

static int horizon_server_handle_init_first_thread( struct horizon_server_connection *connection,
                                                   const unsigned char *message )
{
    const struct horizon_init_first_thread_request *request = (const void *)message;
    struct horizon_init_first_thread_reply reply;
    unsigned short machine = HORIZON_IMAGE_FILE_MACHINE_ARM64;
    unsigned int handle;
    int reply_fd, wait_fd;

    reply_fd = horizon_server_take_client_fd( &handle );
    wait_fd = horizon_server_take_client_fd( &handle );
    connection->reply_fd = reply_fd;
    connection->wait_fd = wait_fd;

    memset( &reply, 0, sizeof(reply) );
    reply.header.error = HORIZON_STATUS_SUCCESS;
    reply.header.reply_size = sizeof(machine);
    reply.pid = request->unix_pid > 0 ? request->unix_pid : 1;
    reply.tid = request->unix_tid > 0 ? request->unix_tid : 1;
    reply.session_id = 1;
    connection->pid = reply.pid;
    connection->tid = reply.tid;

    TRACE( "Horizon server init_first_thread pid %u tid %u reply fd %d/%d wait fd %d/%d.\n",
           reply.pid, reply.tid, reply_fd, request->reply_fd, wait_fd, request->wait_fd );
    return horizon_server_write_reply( connection->reply_fd, &reply, sizeof(reply),
                                       &machine, sizeof(machine) );
}

static int horizon_server_handle_init_process_done( struct horizon_server_connection *connection )
{
    struct horizon_init_process_done_reply reply;

    if (connection->reply_fd == -1)
    {
        errno = EPIPE;
        return -1;
    }

    memset( &reply, 0, sizeof(reply) );
    reply.header.error = HORIZON_STATUS_SUCCESS;
    return horizon_server_write_reply( connection->reply_fd, &reply, sizeof(reply), NULL, 0 );
}

static int horizon_server_handle_init_thread( struct horizon_server_connection *connection,
                                              const unsigned char *message )
{
    const struct horizon_init_thread_request *request = (const void *)message;
    struct horizon_init_thread_reply reply;
    unsigned int handle;
    int reply_fd, wait_fd;

    reply_fd = horizon_server_take_client_fd( &handle );
    wait_fd = horizon_server_take_client_fd( &handle );

    if (connection->reply_fd != -1) close( connection->reply_fd );
    if (connection->wait_fd != -1) close( connection->wait_fd );
    connection->reply_fd = reply_fd;
    connection->wait_fd = wait_fd;
    if (!connection->pid) connection->pid = getpid();
    if (!connection->tid) connection->tid = request->unix_tid > 0 ? request->unix_tid : 1;

    memset( &reply, 0, sizeof(reply) );
    reply.header.error = HORIZON_STATUS_SUCCESS;
    return horizon_server_write_reply( connection->reply_fd, &reply, sizeof(reply), NULL, 0 );
}

static int horizon_server_handle_close_handle( struct horizon_server_connection *connection,
                                               const unsigned char *message )
{
    const struct horizon_close_handle_request *request = (const void *)message;
    unsigned int status = horizon_server_close_object_handle( request->handle );

    return horizon_server_write_status( connection->reply_fd, status );
}

static int horizon_server_handle_set_handle_info( struct horizon_server_connection *connection )
{
    struct horizon_set_handle_info_reply reply;

    memset( &reply, 0, sizeof(reply) );
    reply.header.error = HORIZON_STATUS_SUCCESS;
    return horizon_server_write_reply( connection->reply_fd, &reply, sizeof(reply), NULL, 0 );
}

static int horizon_server_handle_dup_handle( struct horizon_server_connection *connection,
                                             const unsigned char *message )
{
    const struct horizon_dup_handle_request *request = (const void *)message;
    struct horizon_dup_handle_reply reply;

    memset( &reply, 0, sizeof(reply) );
    reply.header.error = horizon_server_duplicate_object_handle( request->src_handle, &reply.handle );

    TRACE( "Horizon server dup_handle src %08x -> %08x status %08x.\n",
           request->src_handle, reply.handle, reply.header.error );
    return horizon_server_write_reply( connection->reply_fd, &reply, sizeof(reply), NULL, 0 );
}

static int horizon_server_handle_allocate_reserve_object( struct horizon_server_connection *connection,
                                                          const unsigned char *message )
{
    const struct horizon_allocate_reserve_object_request *request = (const void *)message;
    struct horizon_allocate_reserve_object_reply reply;

    (void)request;
    memset( &reply, 0, sizeof(reply) );
    reply.header.error = horizon_server_create_object_handle( HORIZON_SERVER_OBJECT_RESERVE, &reply.handle );

    TRACE( "Horizon server allocate_reserve_object type %d -> %08x.\n",
           request->type, reply.handle );
    return horizon_server_write_reply( connection->reply_fd, &reply, sizeof(reply), NULL, 0 );
}

static int horizon_server_handle_compare_objects( struct horizon_server_connection *connection,
                                                  const unsigned char *message )
{
    const struct horizon_compare_objects_request *request = (const void *)message;
    unsigned int status = horizon_server_compare_object_handles( request->first, request->second );

    return horizon_server_write_status( connection->reply_fd, status );
}

static int horizon_server_handle_open_object( struct horizon_server_connection *connection, int type )
{
    struct horizon_open_process_reply reply;

    memset( &reply, 0, sizeof(reply) );
    reply.header.error = horizon_server_create_object_handle( type, &reply.handle );
    return horizon_server_write_reply( connection->reply_fd, &reply, sizeof(reply), NULL, 0 );
}

static int horizon_server_handle_open_named_object( struct horizon_server_connection *connection,
                                                    const unsigned char *message,
                                                    const unsigned char *data, unsigned int data_size,
                                                    int type )
{
    struct horizon_open_process_reply reply;
    struct horizon_object_name name;

    memset( &reply, 0, sizeof(reply) );
    reply.header.error = horizon_server_parse_open_name( message, data, data_size, &name );
    if (reply.header.error == HORIZON_STATUS_SUCCESS)
        reply.header.error = horizon_server_open_named_object_handle( type, &name, &reply.handle );
    return horizon_server_write_reply( connection->reply_fd, &reply, sizeof(reply), NULL, 0 );
}

static int horizon_server_handle_open_mapping( struct horizon_server_connection *connection,
                                               const unsigned char *message,
                                               const unsigned char *data, unsigned int data_size )
{
    struct horizon_open_process_reply reply;
    struct horizon_object_name name;
    struct horizon_server_object *object;
    struct horizon_server_handle_entry *entry = NULL;

    memset( &reply, 0, sizeof(reply) );
    reply.header.error = horizon_server_parse_open_name( message, data, data_size, &name );
    if (reply.header.error) return horizon_server_write_reply( connection->reply_fd, &reply, sizeof(reply), NULL, 0 );

    pthread_mutex_lock( &horizon_server_objects_mutex );
    if (!(object = horizon_server_find_named_object_locked( HORIZON_SERVER_OBJECT_MAPPING, &name )) &&
        horizon_utf16_name_has_ascii_suffix( name.name, name.name_len, "__wine_session" ))
    {
        reply.header.error = horizon_server_ensure_session_locked();
        if (!reply.header.error)
        {
            if ((entry = horizon_server_create_handle_locked( HORIZON_SERVER_OBJECT_MAPPING )))
            {
                entry->object->file_fd = dup( horizon_session_fd );
                if (entry->object->file_fd == -1)
                    reply.header.error = horizon_server_errno_status( errno );
                else
                {
                    entry->object->mapping_size = HORIZON_SESSION_MAPPING_SIZE;
                    entry->object->mapping_access = FILE_READ_DATA | FILE_WRITE_DATA;
                    entry->object->mapping_file_access = FILE_READ_DATA | FILE_WRITE_DATA;
                    entry->object->file_access = FILE_READ_DATA | FILE_WRITE_DATA;
                    entry->object->mapping_is_session = 1;
                    reply.header.error = horizon_server_set_object_name( entry->object, &name );
                    if (!reply.header.error) reply.handle = entry->handle;
                }

                if (reply.header.error)
                {
                    horizon_server_handles = entry->next;
                    horizon_server_free_object( entry->object );
                    free( entry );
                    entry = NULL;
                }
            }
            else reply.header.error = HORIZON_STATUS_NO_MEMORY;
        }
    }
    else if (object)
    {
        if ((entry = horizon_server_create_handle_for_object_locked( object ))) reply.handle = entry->handle;
        else reply.header.error = HORIZON_STATUS_NO_MEMORY;
    }
    else reply.header.error = HORIZON_STATUS_OBJECT_NAME_NOT_FOUND;
    pthread_mutex_unlock( &horizon_server_objects_mutex );

    return horizon_server_write_reply( connection->reply_fd, &reply, sizeof(reply), NULL, 0 );
}

static int horizon_server_handle_atom( struct horizon_server_connection *connection,
                                       const unsigned char *data, unsigned int data_size,
                                       int add )
{
    struct horizon_atom_reply reply;

    memset( &reply, 0, sizeof(reply) );
    pthread_mutex_lock( &horizon_server_objects_mutex );
    if (add) reply.header.error = horizon_server_add_atom_locked( data, data_size, 0, &reply.atom );
    else reply.header.error = horizon_server_find_atom_locked( data, data_size, &reply.atom );
    pthread_mutex_unlock( &horizon_server_objects_mutex );
    return horizon_server_write_reply( connection->reply_fd, &reply, sizeof(reply), NULL, 0 );
}

static unsigned int horizon_server_create_winstation_locked( const struct horizon_object_name *name,
                                                             unsigned int flags, unsigned int *handle )
{
    struct horizon_server_handle_entry *entry;
    unsigned int status;

    *handle = 0;
    status = horizon_server_create_named_object_handle_locked( HORIZON_SERVER_OBJECT_WINSTATION, name, &entry );
    if (entry)
    {
        if (status == HORIZON_STATUS_SUCCESS) entry->object->user_flags = flags;
        *handle = entry->handle;
    }
    return status;
}

static int horizon_server_handle_create_winstation( struct horizon_server_connection *connection,
                                                    const unsigned char *message,
                                                    const unsigned char *data, unsigned int data_size )
{
    const struct horizon_create_winstation_request *request = (const void *)message;
    struct horizon_winstation_handle_reply reply;
    struct horizon_object_name name;

    memset( &reply, 0, sizeof(reply) );
    memset( &name, 0, sizeof(name) );
    name.rootdir = request->rootdir;
    name.name = data;
    name.name_len = data_size;

    pthread_mutex_lock( &horizon_server_objects_mutex );
    reply.header.error = horizon_server_create_winstation_locked( &name, request->flags, &reply.handle );
    pthread_mutex_unlock( &horizon_server_objects_mutex );
    return horizon_server_write_reply( connection->reply_fd, &reply, sizeof(reply), NULL, 0 );
}

static int horizon_server_handle_open_winstation( struct horizon_server_connection *connection,
                                                  const unsigned char *message,
                                                  const unsigned char *data, unsigned int data_size )
{
    return horizon_server_handle_open_named_object( connection, message, data, data_size,
                                                   HORIZON_SERVER_OBJECT_WINSTATION );
}

static int horizon_server_handle_get_process_winstation( struct horizon_server_connection *connection )
{
    struct horizon_winstation_handle_reply reply;

    memset( &reply, 0, sizeof(reply) );
    pthread_mutex_lock( &horizon_server_objects_mutex );
    reply.handle = horizon_process_winstation;
    pthread_mutex_unlock( &horizon_server_objects_mutex );
    return horizon_server_write_reply( connection->reply_fd, &reply, sizeof(reply), NULL, 0 );
}

static int horizon_server_handle_set_process_winstation( struct horizon_server_connection *connection,
                                                         const unsigned char *message )
{
    const struct horizon_set_process_winstation_request *request = (const void *)message;
    unsigned int status = HORIZON_STATUS_SUCCESS;

    pthread_mutex_lock( &horizon_server_objects_mutex );
    if (!horizon_server_find_handle_object_locked( request->handle, HORIZON_SERVER_OBJECT_WINSTATION ))
        status = HORIZON_STATUS_INVALID_HANDLE;
    else horizon_process_winstation = request->handle;
    pthread_mutex_unlock( &horizon_server_objects_mutex );
    return horizon_server_write_status( connection->reply_fd, status );
}

static int horizon_server_handle_set_winstation_monitors( struct horizon_server_connection *connection,
                                                          const unsigned char *message )
{
    const struct horizon_set_winstation_monitors_request *request = (const void *)message;
    struct horizon_set_winstation_monitors_reply reply;
    struct horizon_server_object *winstation;

    memset( &reply, 0, sizeof(reply) );
    pthread_mutex_lock( &horizon_server_objects_mutex );
    if (!(winstation = horizon_server_find_handle_object_locked( horizon_process_winstation,
                                                                 HORIZON_SERVER_OBJECT_WINSTATION )))
        reply.header.error = HORIZON_STATUS_INVALID_HANDLE;
    else
    {
        if (request->increment) winstation->count++;
        reply.serial = winstation->count;
    }
    pthread_mutex_unlock( &horizon_server_objects_mutex );
    return horizon_server_write_reply( connection->reply_fd, &reply, sizeof(reply), NULL, 0 );
}

static int horizon_server_handle_enum_winstation( struct horizon_server_connection *connection )
{
    struct horizon_enum_winstation_reply reply;

    memset( &reply, 0, sizeof(reply) );
    return horizon_server_write_reply( connection->reply_fd, &reply, sizeof(reply), NULL, 0 );
}

static unsigned int horizon_server_create_desktop_locked( const struct horizon_object_name *name,
                                                          unsigned int flags, unsigned int *handle )
{
    struct horizon_server_handle_entry *entry;
    struct horizon_shared_object *shared;
    struct horizon_obj_locator locator;
    unsigned int status;

    *handle = 0;
    if (!horizon_process_winstation) return HORIZON_STATUS_INVALID_HANDLE;
    if (!name->name_len) return HORIZON_STATUS_INVALID_HANDLE;

    status = horizon_server_create_named_object_handle_locked( HORIZON_SERVER_OBJECT_DESKTOP, name, &entry );
    if (!entry) return status;
    *handle = entry->handle;
    if (status != HORIZON_STATUS_SUCCESS) return status;

    if ((status = horizon_server_alloc_shared_object_locked( sizeof(struct horizon_desktop_shm), &locator )))
        return status;
    shared = horizon_server_shared_object_locked( locator );
    shared->shm.desktop.flags = flags;
    shared->shm.desktop.cursor.clip.right = 1280;
    shared->shm.desktop.cursor.clip.bottom = 720;
    if ((status = horizon_server_flush_session_range_locked( locator.offset, sizeof(*shared) )))
        return status;

    entry->object->desktop_locator = locator;
    entry->object->desktop_winstation = horizon_process_winstation;
    entry->object->user_flags = flags;
    if (!horizon_input_desktop) horizon_input_desktop = entry->handle;
    return HORIZON_STATUS_SUCCESS;
}

static int horizon_server_handle_create_desktop( struct horizon_server_connection *connection,
                                                 const unsigned char *message,
                                                 const unsigned char *data, unsigned int data_size )
{
    const struct horizon_create_desktop_request *request = (const void *)message;
    struct horizon_winstation_handle_reply reply;
    struct horizon_object_name name;

    memset( &reply, 0, sizeof(reply) );
    memset( &name, 0, sizeof(name) );
    name.rootdir = horizon_process_winstation;
    name.name = data;
    name.name_len = data_size;

    pthread_mutex_lock( &horizon_server_objects_mutex );
    reply.header.error = horizon_server_create_desktop_locked( &name, request->flags, &reply.handle );
    pthread_mutex_unlock( &horizon_server_objects_mutex );
    return horizon_server_write_reply( connection->reply_fd, &reply, sizeof(reply), NULL, 0 );
}

static int horizon_server_handle_open_desktop( struct horizon_server_connection *connection,
                                               const unsigned char *message,
                                               const unsigned char *data, unsigned int data_size )
{
    const struct horizon_open_desktop_request *request = (const void *)message;
    struct horizon_winstation_handle_reply reply;
    struct horizon_object_name name;

    memset( &reply, 0, sizeof(reply) );
    memset( &name, 0, sizeof(name) );
    name.rootdir = request->winsta ? request->winsta : horizon_process_winstation;
    name.name = data;
    name.name_len = data_size;
    reply.header.error = horizon_server_open_named_object_handle( HORIZON_SERVER_OBJECT_DESKTOP,
                                                                  &name, &reply.handle );
    return horizon_server_write_reply( connection->reply_fd, &reply, sizeof(reply), NULL, 0 );
}

static int horizon_server_handle_open_input_desktop( struct horizon_server_connection *connection )
{
    struct horizon_winstation_handle_reply reply;

    memset( &reply, 0, sizeof(reply) );
    pthread_mutex_lock( &horizon_server_objects_mutex );
    if (!horizon_input_desktop) reply.header.error = HORIZON_STATUS_INVALID_HANDLE;
    else
    {
        struct horizon_server_object *desktop;
        struct horizon_server_handle_entry *entry;

        desktop = horizon_server_find_handle_object_locked( horizon_input_desktop, HORIZON_SERVER_OBJECT_DESKTOP );
        if (!desktop) reply.header.error = HORIZON_STATUS_INVALID_HANDLE;
        else if ((entry = horizon_server_create_handle_for_object_locked( desktop ))) reply.handle = entry->handle;
        else reply.header.error = HORIZON_STATUS_NO_MEMORY;
    }
    pthread_mutex_unlock( &horizon_server_objects_mutex );
    return horizon_server_write_reply( connection->reply_fd, &reply, sizeof(reply), NULL, 0 );
}

static int horizon_server_handle_set_input_desktop( struct horizon_server_connection *connection,
                                                    const unsigned char *message )
{
    const struct horizon_set_thread_desktop_request *request = (const void *)message;
    unsigned int status = HORIZON_STATUS_SUCCESS;

    pthread_mutex_lock( &horizon_server_objects_mutex );
    if (!horizon_server_find_handle_object_locked( request->handle, HORIZON_SERVER_OBJECT_DESKTOP ))
        status = HORIZON_STATUS_INVALID_HANDLE;
    else horizon_input_desktop = request->handle;
    pthread_mutex_unlock( &horizon_server_objects_mutex );
    return horizon_server_write_status( connection->reply_fd, status );
}

static int horizon_server_handle_get_thread_desktop( struct horizon_server_connection *connection,
                                                     const unsigned char *message )
{
    const struct horizon_get_thread_desktop_request *request = (const void *)message;
    struct horizon_get_thread_desktop_reply reply;
    struct horizon_server_object *desktop;

    (void)request;
    memset( &reply, 0, sizeof(reply) );
    pthread_mutex_lock( &horizon_server_objects_mutex );
    reply.handle = horizon_thread_desktop;
    if (reply.handle &&
        (desktop = horizon_server_find_handle_object_locked( reply.handle, HORIZON_SERVER_OBJECT_DESKTOP )))
        reply.locator = desktop->desktop_locator;
    pthread_mutex_unlock( &horizon_server_objects_mutex );
    return horizon_server_write_reply( connection->reply_fd, &reply, sizeof(reply), NULL, 0 );
}

static int horizon_server_handle_set_thread_desktop( struct horizon_server_connection *connection,
                                                     const unsigned char *message )
{
    const struct horizon_set_thread_desktop_request *request = (const void *)message;
    struct horizon_set_thread_desktop_reply reply;
    struct horizon_server_object *desktop;

    memset( &reply, 0, sizeof(reply) );
    (void)request;
    pthread_mutex_lock( &horizon_server_objects_mutex );
    if (!(desktop = horizon_server_find_handle_object_locked( request->handle, HORIZON_SERVER_OBJECT_DESKTOP )))
        reply.header.error = HORIZON_STATUS_INVALID_HANDLE;
    else
    {
        horizon_thread_desktop = request->handle;
        reply.locator = desktop->desktop_locator;
    }
    pthread_mutex_unlock( &horizon_server_objects_mutex );
    return horizon_server_write_reply( connection->reply_fd, &reply, sizeof(reply), NULL, 0 );
}

static int horizon_server_handle_set_user_object_info( struct horizon_server_connection *connection,
                                                       const unsigned char *message )
{
    const struct horizon_set_user_object_info_request *request = (const void *)message;
    struct horizon_set_user_object_info_reply reply;
    struct horizon_server_handle_entry *entry;

    memset( &reply, 0, sizeof(reply) );
    pthread_mutex_lock( &horizon_server_objects_mutex );
    if (!(entry = horizon_server_find_handle_locked( request->handle )))
        reply.header.error = HORIZON_STATUS_INVALID_HANDLE;
    else if (entry->object->type != HORIZON_SERVER_OBJECT_WINSTATION &&
             entry->object->type != HORIZON_SERVER_OBJECT_DESKTOP)
        reply.header.error = HORIZON_STATUS_OBJECT_TYPE_MISMATCH;
    else
    {
        reply.is_desktop = entry->object->type == HORIZON_SERVER_OBJECT_DESKTOP;
        reply.old_obj_flags = entry->object->user_flags;
        if (request->flags & HORIZON_SET_USER_OBJECT_SET_FLAGS)
            entry->object->user_flags = request->obj_flags;
    }
    pthread_mutex_unlock( &horizon_server_objects_mutex );
    return horizon_server_write_reply( connection->reply_fd, &reply, sizeof(reply), NULL, 0 );
}

static struct horizon_user_class *horizon_server_find_class_locked( unsigned int atom,
                                                                    unsigned long long instance )
{
    struct horizon_user_class *class;

    for (class = horizon_classes; class; class = class->next)
    {
        if (class->atom != atom) continue;
        if (!instance || !class->local || class->instance == instance) return class;
    }
    return NULL;
}

static int horizon_server_is_desktop_class( const struct horizon_user_class *class )
{
    return class && !class->local && class->base_atom == HORIZON_DESKTOP_ATOM;
}

static int horizon_server_is_message_class( const struct horizon_user_class *class )
{
    return class && !class->local && class->name &&
           horizon_utf16_name_equals_ascii( class->name, class->name_len, "Message" );
}

static unsigned int horizon_server_class_name_from_request_locked( const struct horizon_create_class_request *request,
                                                                   const unsigned char *data,
                                                                   unsigned int data_size,
                                                                   unsigned char *buffer,
                                                                   unsigned int buffer_size,
                                                                   const unsigned char **name,
                                                                   unsigned int *name_len )
{
    char atom_name[32];

    *name = data;
    *name_len = data_size;
    if (*name_len) return HORIZON_STATUS_SUCCESS;
    if (!request->atom) return HORIZON_STATUS_INVALID_PARAMETER;

    snprintf( atom_name, sizeof(atom_name), "#%u", request->atom );
    *name_len = horizon_utf16_from_ascii( buffer, buffer_size, atom_name );
    *name = buffer;
    return *name_len ? HORIZON_STATUS_SUCCESS : HORIZON_STATUS_INVALID_PARAMETER;
}

static int horizon_server_handle_create_class( struct horizon_server_connection *connection,
                                               const unsigned char *message,
                                               const unsigned char *data, unsigned int data_size )
{
    const struct horizon_create_class_request *request = (const void *)message;
    struct horizon_create_class_reply reply;
    struct horizon_user_class *class;
    struct horizon_shared_object *shared;
    struct horizon_obj_locator locator;
    const unsigned char *name;
    unsigned char atom_name[32];
    unsigned int name_len, atom, base_atom, base_offset = 0;
    unsigned long long shm_size;

    memset( &reply, 0, sizeof(reply) );

    pthread_mutex_lock( &horizon_server_objects_mutex );
    reply.header.error = horizon_server_class_name_from_request_locked( request, data, data_size,
                                                                        atom_name, sizeof(atom_name),
                                                                        &name, &name_len );
    if (!reply.header.error)
    {
        atom = request->atom;
        if (!atom)
            reply.header.error = horizon_server_add_atom_locked( name, name_len, 0, &atom );
    }
    if (!reply.header.error)
    {
        if (request->name_offset && request->name_offset < name_len / sizeof(unsigned short))
        {
            base_offset = request->name_offset;
            reply.header.error = horizon_server_add_atom_locked( name + base_offset * sizeof(unsigned short),
                                                                 name_len - base_offset * sizeof(unsigned short),
                                                                 0, &base_atom );
        }
        else reply.header.error = horizon_server_add_atom_locked( name, name_len, atom, &base_atom );
    }
    if (!reply.header.error && horizon_server_find_class_locked( atom, request->instance ))
        reply.header.error = HORIZON_STATUS_OBJECT_NAME_COLLISION;
    if (!reply.header.error &&
        (request->cls_extra < 0 || request->cls_extra > 4096 ||
         request->win_extra < 0 || request->win_extra > 4096 ||
         name_len > HORIZON_MAX_ATOM_LEN * sizeof(unsigned short)))
        reply.header.error = HORIZON_STATUS_INVALID_PARAMETER;
    if (!reply.header.error)
    {
        shm_size = offsetof( struct horizon_class_shm, extra ) + request->cls_extra;
        reply.header.error = horizon_server_alloc_shared_object_locked( shm_size, &locator );
    }
    if (!reply.header.error)
    {
        if (!(class = calloc( 1, sizeof(*class) ))) reply.header.error = HORIZON_STATUS_NO_MEMORY;
        else if (!(class->name = malloc( name_len )))
        {
            free( class );
            reply.header.error = HORIZON_STATUS_NO_MEMORY;
        }
        else
        {
            memcpy( class->name, name, name_len );
            class->name_len = name_len;
            class->local = request->local;
            class->atom = atom;
            class->base_atom = base_atom;
            class->style = request->style;
            class->instance = request->instance;
            class->client_ptr = request->client_ptr;
            class->cls_extra = request->cls_extra;
            class->win_extra = request->win_extra;
            class->locator = locator;
            class->next = horizon_classes;
            horizon_classes = class;

            shared = horizon_server_shared_object_locked( locator );
            shared->shm.class.atom = base_atom;
            shared->shm.class.style = request->style;
            shared->shm.class.cls_extra = request->cls_extra;
            shared->shm.class.win_extra = request->win_extra;
            shared->shm.class.instance = request->instance;
            shared->shm.class.name_offset = base_offset;
            shared->shm.class.name_len = name_len;
            memcpy( shared->shm.class.name, name, name_len );
            memset( shared->shm.class.extra, 0, request->cls_extra );
            reply.header.error = horizon_server_flush_session_range_locked(
                locator.offset, offsetof( struct horizon_shared_object, shm ) + shm_size );
            reply.locator = locator;
            reply.atom = base_atom;
        }
    }
    pthread_mutex_unlock( &horizon_server_objects_mutex );

    TRACE( "Horizon server create_class atom %x base %x len %u status %08x.\n",
           request->atom, reply.atom, data_size, reply.header.error );
    return horizon_server_write_reply( connection->reply_fd, &reply, sizeof(reply), NULL, 0 );
}

static int horizon_server_handle_destroy_class( struct horizon_server_connection *connection,
                                                const unsigned char *message,
                                                const unsigned char *data, unsigned int data_size )
{
    const struct horizon_destroy_class_request *request = (const void *)message;
    struct horizon_server_reply_header reply;
    struct horizon_user_class **ptr;
    unsigned int atom = request->atom;

    memset( &reply, 0, sizeof(reply) );
    pthread_mutex_lock( &horizon_server_objects_mutex );
    if (!atom) horizon_server_find_atom_locked( data, data_size, &atom );
    for (ptr = &horizon_classes; *ptr; ptr = &(*ptr)->next)
    {
        struct horizon_user_class *class = *ptr;

        if (class->atom != atom) continue;
        if (request->instance && class->local && class->instance != request->instance) continue;
        *ptr = class->next;
        free( class->name );
        free( class );
        pthread_mutex_unlock( &horizon_server_objects_mutex );
        return horizon_server_write_reply( connection->reply_fd, &reply, sizeof(reply), NULL, 0 );
    }
    reply.error = HORIZON_STATUS_INVALID_HANDLE;
    pthread_mutex_unlock( &horizon_server_objects_mutex );
    return horizon_server_write_reply( connection->reply_fd, &reply, sizeof(reply), NULL, 0 );
}

static unsigned int horizon_server_create_window_locked( unsigned int parent_handle,
                                                         unsigned int owner_handle,
                                                         unsigned int atom,
                                                         unsigned long long class_instance,
                                                         unsigned long long instance,
                                                         unsigned int dpi_context,
                                                         unsigned int style,
                                                         unsigned int ex_style,
                                                         unsigned int pid,
                                                         unsigned int tid,
                                                         struct horizon_user_window **ret )
{
    struct horizon_server_object *desktop;
    struct horizon_user_window *parent = NULL;
    struct horizon_user_window *window;
    struct horizon_user_class *class;
    struct horizon_shared_object *shared;
    struct horizon_obj_locator locator;
    unsigned int desktop_handle = horizon_thread_desktop;
    unsigned int status, handle;

    *ret = NULL;
    if (!desktop_handle) return HORIZON_STATUS_ACCESS_DENIED;
    if (!(desktop = horizon_server_find_handle_object_locked( desktop_handle, HORIZON_SERVER_OBJECT_DESKTOP )))
        return HORIZON_STATUS_INVALID_HANDLE;
    if (!(class = horizon_server_find_class_locked( atom, class_instance )))
        return HORIZON_STATUS_INVALID_HANDLE;
    if (parent_handle && !(parent = horizon_server_find_window_locked( parent_handle )))
        return HORIZON_STATUS_INVALID_HANDLE;

    if (!parent_handle)
    {
        if (horizon_server_is_desktop_class( class )) parent_handle = desktop->desktop_top_window;
        else if (horizon_server_is_message_class( class ))
            parent_handle = desktop->desktop_msg_window ? desktop->desktop_top_window : 0;
        else if (!(parent_handle = desktop->desktop_top_window))
            return HORIZON_STATUS_ACCESS_DENIED;
        if (parent_handle && !(parent = horizon_server_find_window_locked( parent_handle )))
            return HORIZON_STATUS_INVALID_HANDLE;
    }

    if ((status = horizon_server_alloc_shared_object_locked( sizeof(struct horizon_window_shm), &locator )))
        return status;
    if (!(window = calloc( 1, sizeof(*window) ))) return HORIZON_STATUS_NO_MEMORY;
    if ((status = horizon_server_alloc_user_handle_locked( HORIZON_NTUSER_OBJ_WINDOW, locator,
                                                           pid, tid, &handle )))
    {
        free( window );
        return status;
    }

    shared = horizon_server_shared_object_locked( locator );
    shared->shm.window.class = class->locator;
    shared->shm.window.dpi_context = dpi_context ? dpi_context : HORIZON_NTUSER_DPI_PER_MONITOR_AWARE;
    if ((status = horizon_server_flush_session_range_locked(
             locator.offset, offsetof( struct horizon_shared_object, shm ) + sizeof(struct horizon_window_shm) )))
    {
        free( window );
        return status;
    }

    window->handle = handle;
    window->parent = parent_handle;
    window->owner = owner_handle;
    window->pid = pid;
    window->tid = tid;
    window->atom = atom;
    window->style = style;
    window->ex_style = ex_style;
    window->is_unicode = 1;
    window->monitor_dpi = 96;
    window->instance = instance;
    window->last_active = handle;
    window->desktop_handle = desktop_handle;
    window->class = class;
    window->locator = locator;
    window->next = horizon_windows;
    horizon_windows = window;

    if (!parent)
    {
        if (horizon_server_is_desktop_class( class )) desktop->desktop_top_window = handle;
        else if (horizon_server_is_message_class( class )) desktop->desktop_msg_window = handle;
    }

    *ret = window;
    return HORIZON_STATUS_SUCCESS;
}

static int horizon_server_handle_create_window( struct horizon_server_connection *connection,
                                                const unsigned char *message,
                                                const unsigned char *data, unsigned int data_size )
{
    const struct horizon_create_window_request *request = (const void *)message;
    struct horizon_create_window_reply reply;
    struct horizon_user_window *window = NULL;
    unsigned int atom = request->atom;

    memset( &reply, 0, sizeof(reply) );
    pthread_mutex_lock( &horizon_server_objects_mutex );
    if (!atom) horizon_server_find_atom_locked( data, data_size, &atom );
    if (!atom) reply.header.error = HORIZON_STATUS_INVALID_HANDLE;
    else
    {
        reply.header.error = horizon_server_create_window_locked( request->parent, request->owner,
                                                                  atom, request->class_instance,
                                                                  request->instance, request->dpi_context,
                                                                  request->style, request->ex_style,
                                                                  connection->pid, connection->tid,
                                                                  &window );
        if (!reply.header.error && window)
        {
            reply.handle = window->handle;
            reply.parent = window->parent;
            reply.owner = window->owner;
            reply.extra = window->class->win_extra;
            reply.class_ptr = window->class->client_ptr;
        }
    }
    pthread_mutex_unlock( &horizon_server_objects_mutex );

    TRACE( "Horizon server create_window atom %x parent %08x -> %08x status %08x.\n",
           atom, request->parent, reply.handle, reply.header.error );
    horizon_trace( "[HZUSER] create_window atom=%#x parent=%08x pid=%u tid=%u -> hwnd=%08x err=%08x\n",
                   atom, request->parent, connection->pid, connection->tid,
                   reply.handle, reply.header.error );
    return horizon_server_write_reply( connection->reply_fd, &reply, sizeof(reply), NULL, 0 );
}

static int horizon_server_handle_destroy_window( struct horizon_server_connection *connection,
                                                 const unsigned char *message )
{
    const struct horizon_destroy_window_request *request = (const void *)message;
    struct horizon_user_window **ptr;
    unsigned int status = HORIZON_STATUS_INVALID_HANDLE;

    pthread_mutex_lock( &horizon_server_objects_mutex );
    for (ptr = &horizon_windows; *ptr; ptr = &(*ptr)->next)
    {
        struct horizon_user_window *window = *ptr;
        struct horizon_window_property *property;

        if (request->handle && window->handle != request->handle) continue;
        *ptr = window->next;
        while ((property = window->properties))
        {
            window->properties = property->next;
            free( property );
        }
        free( window );
        status = HORIZON_STATUS_SUCCESS;
        if (request->handle) break;
    }
    if (!request->handle) status = HORIZON_STATUS_SUCCESS;
    pthread_mutex_unlock( &horizon_server_objects_mutex );
    return horizon_server_write_status( connection->reply_fd, status );
}

static void horizon_server_window_screen_rects_locked( const struct horizon_user_window *window,
                                                       struct horizon_rectangle *window_rect,
                                                       struct horizon_rectangle *client_rect );

static void horizon_server_offset_rect( struct horizon_rectangle *rect, int x, int y )
{
    rect->left += x;
    rect->right += x;
    rect->top += y;
    rect->bottom += y;
}

static int horizon_server_rect_empty( const struct horizon_rectangle *rect )
{
    return rect->left >= rect->right || rect->top >= rect->bottom;
}

static struct horizon_rectangle horizon_server_intersect_rect( struct horizon_rectangle a,
                                                               struct horizon_rectangle b )
{
    struct horizon_rectangle ret;

    ret.left = a.left > b.left ? a.left : b.left;
    ret.top = a.top > b.top ? a.top : b.top;
    ret.right = a.right < b.right ? a.right : b.right;
    ret.bottom = a.bottom < b.bottom ? a.bottom : b.bottom;
    if (horizon_server_rect_empty( &ret ))
        ret.left = ret.top = ret.right = ret.bottom = 0;
    return ret;
}

static struct horizon_rectangle horizon_server_union_rect( struct horizon_rectangle a,
                                                           struct horizon_rectangle b )
{
    if (horizon_server_rect_empty( &a )) return b;
    if (horizon_server_rect_empty( &b )) return a;
    if (b.left < a.left) a.left = b.left;
    if (b.top < a.top) a.top = b.top;
    if (b.right > a.right) a.right = b.right;
    if (b.bottom > a.bottom) a.bottom = b.bottom;
    return a;
}

static void horizon_server_window_screen_rects_locked( const struct horizon_user_window *window,
                                                       struct horizon_rectangle *window_rect,
                                                       struct horizon_rectangle *client_rect );

static void horizon_server_mark_window_update_locked( struct horizon_user_window *window,
                                                      struct horizon_rectangle dirty,
                                                      int erase )
{
    if (horizon_server_rect_empty( &dirty )) return;

    if (window->has_update_rect)
        window->update_rect = horizon_server_union_rect( window->update_rect, dirty );
    else
        window->update_rect = dirty;
    window->has_update_rect = 1;
    if (erase) window->needs_erase = 1;
}

static void horizon_server_redraw_children_locked( struct horizon_user_window *parent,
                                                   struct horizon_rectangle dirty,
                                                   unsigned int flags )
{
    struct horizon_user_window *child;

    if (flags & HORIZON_RDW_NOCHILDREN) return;
    if (parent->style & HORIZON_WS_MINIMIZE) return;
    if ((parent->style & HORIZON_WS_CLIPCHILDREN) &&
        !(flags & HORIZON_RDW_ALLCHILDREN)) return;

    for (child = horizon_windows; child; child = child->next)
    {
        struct horizon_rectangle child_window, child_client, child_dirty;

        if (child->parent != parent->handle || !(child->style & HORIZON_WS_VISIBLE)) continue;
        horizon_server_window_screen_rects_locked( child, &child_window, &child_client );
        child_dirty = horizon_server_intersect_rect( dirty, child_window );
        if (horizon_server_rect_empty( &child_dirty )) continue;

        horizon_server_mark_window_update_locked( child, child_dirty, 1 );
        child->needs_nonclient = 1;
        horizon_server_redraw_children_locked( child, child_dirty,
                                               flags | HORIZON_RDW_FRAME | HORIZON_RDW_ERASE );
    }
}

static unsigned int horizon_server_window_update_flags_locked( const struct horizon_user_window *window,
                                                               unsigned int flags )
{
    unsigned int ret = 0;

    if ((flags & HORIZON_UPDATE_NONCLIENT) && window->has_update_rect && window->needs_nonclient)
        ret |= HORIZON_UPDATE_NONCLIENT;
    if ((flags & HORIZON_UPDATE_ERASE) && window->has_update_rect && window->needs_erase)
        ret |= HORIZON_UPDATE_ERASE;
    if ((flags & HORIZON_UPDATE_PAINT) && window->has_update_rect)
        ret |= HORIZON_UPDATE_PAINT;
    if ((flags & HORIZON_UPDATE_INTERNALPAINT) && window->has_internal_paint)
        ret |= HORIZON_UPDATE_INTERNALPAINT;
    return ret;
}

static unsigned int horizon_server_find_window_update_locked( struct horizon_user_window *window,
                                                              struct horizon_user_window *from_child,
                                                              unsigned int flags, int *past_from,
                                                              struct horizon_user_window **found )
{
    struct horizon_user_window *child;
    unsigned int ret;

    if (!*past_from)
    {
        if (window == from_child) *past_from = 1;
    }
    else if ((ret = horizon_server_window_update_flags_locked( window, flags )))
    {
        *found = window;
        return ret;
    }

    if (flags & HORIZON_UPDATE_NOCHILDREN) return 0;
    if (window->style & HORIZON_WS_MINIMIZE) return 0;
    if (!(flags & HORIZON_UPDATE_ALLCHILDREN) &&
        !(window->style & HORIZON_WS_CLIPCHILDREN)) return 0;

    for (child = horizon_windows; child; child = child->next)
    {
        if (child->parent != window->handle || !(child->style & HORIZON_WS_VISIBLE)) continue;
        if ((ret = horizon_server_find_window_update_locked( child, from_child, flags,
                                                             past_from, found )))
            return ret;
    }
    return 0;
}

static unsigned long long horizon_server_get_window_info_locked( const struct horizon_user_window *window,
                                                                 int offset, unsigned int size,
                                                                 unsigned int *status )
{
    *status = HORIZON_STATUS_SUCCESS;
    switch (offset)
    {
    case HORIZON_GWL_STYLE:
        return window->style;
    case HORIZON_GWL_EXSTYLE:
        return window->ex_style;
    case HORIZON_GWLP_ID:
        return window->id;
    case HORIZON_GWLP_HINSTANCE:
        return window->instance;
    case HORIZON_GWLP_WNDPROC:
        return window->is_unicode;
    case HORIZON_GWLP_USERDATA:
        return window->user_data;
    default:
        if (size > sizeof(unsigned long long) ||
            offset < 0 || offset > window->class->win_extra - (int)size)
            *status = HORIZON_STATUS_INVALID_PARAMETER;
        return 0;
    }
}

static void horizon_server_set_window_info_locked( struct horizon_user_window *window, int offset,
                                                   unsigned long long value, unsigned int size,
                                                   unsigned long long *old_value,
                                                   unsigned int *status )
{
    *old_value = horizon_server_get_window_info_locked( window, offset, size, status );
    if (*status) return;

    switch (offset)
    {
    case HORIZON_GWL_STYLE:
        window->style = value;
        break;
    case HORIZON_GWL_EXSTYLE:
        window->ex_style = value;
        break;
    case HORIZON_GWLP_ID:
        window->id = value;
        break;
    case HORIZON_GWLP_HINSTANCE:
        window->instance = value;
        break;
    case HORIZON_GWLP_WNDPROC:
        window->is_unicode = value;
        break;
    case HORIZON_GWLP_USERDATA:
        window->user_data = value;
        break;
    default:
        /* Extra window bytes are owned by the client WND for same-process windows. */
        break;
    }
}

static int horizon_server_handle_set_window_owner( struct horizon_server_connection *connection,
                                                   const unsigned char *message )
{
    const struct horizon_set_window_owner_request *request = (const void *)message;
    struct horizon_set_window_owner_reply reply;
    struct horizon_user_window *window;

    memset( &reply, 0, sizeof(reply) );
    pthread_mutex_lock( &horizon_server_objects_mutex );
    if (!(window = horizon_server_find_window_locked( request->handle )))
        reply.header.error = HORIZON_STATUS_INVALID_HANDLE;
    else if (request->owner && !horizon_server_find_window_locked( request->owner ))
        reply.header.error = HORIZON_STATUS_INVALID_HANDLE;
    else
    {
        reply.prev_owner = window->owner;
        reply.full_owner = window->owner = request->owner;
    }
    pthread_mutex_unlock( &horizon_server_objects_mutex );
    return horizon_server_write_reply( connection->reply_fd, &reply, sizeof(reply), NULL, 0 );
}

static int horizon_server_handle_get_window_info( struct horizon_server_connection *connection,
                                                  const unsigned char *message )
{
    const struct horizon_get_window_info_request *request = (const void *)message;
    struct horizon_get_window_info_reply reply;
    struct horizon_user_window *window;

    memset( &reply, 0, sizeof(reply) );
    pthread_mutex_lock( &horizon_server_objects_mutex );
    if (!(window = horizon_server_find_window_locked( request->handle )))
        reply.header.error = HORIZON_STATUS_INVALID_HANDLE;
    else
    {
        reply.last_active = window->last_active ? window->last_active : window->handle;
        reply.is_unicode = window->is_unicode;
        reply.info = horizon_server_get_window_info_locked( window, request->offset,
                                                            request->size, &reply.header.error );
    }
    pthread_mutex_unlock( &horizon_server_objects_mutex );
    return horizon_server_write_reply( connection->reply_fd, &reply, sizeof(reply), NULL, 0 );
}

static int horizon_server_handle_init_window_info( struct horizon_server_connection *connection,
                                                   const unsigned char *message )
{
    const struct horizon_init_window_info_request *request = (const void *)message;
    struct horizon_user_window *window;
    unsigned int status = HORIZON_STATUS_SUCCESS;

    pthread_mutex_lock( &horizon_server_objects_mutex );
    if (!(window = horizon_server_find_window_locked( request->handle )))
        status = HORIZON_STATUS_INVALID_HANDLE;
    else
    {
        window->style = request->style;
        window->ex_style = request->ex_style;
        window->is_unicode = request->is_unicode;
    }
    pthread_mutex_unlock( &horizon_server_objects_mutex );
    return horizon_server_write_status( connection->reply_fd, status );
}

static int horizon_server_handle_set_window_info( struct horizon_server_connection *connection,
                                                  const unsigned char *message )
{
    const struct horizon_set_window_info_request *request = (const void *)message;
    struct horizon_set_window_info_reply reply;
    struct horizon_user_window *window;

    memset( &reply, 0, sizeof(reply) );
    pthread_mutex_lock( &horizon_server_objects_mutex );
    if (!(window = horizon_server_find_window_locked( request->handle )))
        reply.header.error = HORIZON_STATUS_INVALID_HANDLE;
    else
        horizon_server_set_window_info_locked( window, request->offset, request->new_info,
                                               request->size, &reply.old_info,
                                               &reply.header.error );
    pthread_mutex_unlock( &horizon_server_objects_mutex );
    return horizon_server_write_reply( connection->reply_fd, &reply, sizeof(reply), NULL, 0 );
}

static int horizon_server_handle_get_window_tree( struct horizon_server_connection *connection,
                                                  const unsigned char *message )
{
    const struct horizon_get_window_tree_request *request = (const void *)message;
    struct horizon_get_window_tree_reply reply;
    struct horizon_user_window *window, *iter, *prev = NULL;

    memset( &reply, 0, sizeof(reply) );
    pthread_mutex_lock( &horizon_server_objects_mutex );
    if (!(window = horizon_server_find_window_locked( request->handle )))
        reply.header.error = HORIZON_STATUS_INVALID_HANDLE;
    else
    {
        reply.parent = window->parent;
        reply.owner = window->owner;
        for (iter = horizon_windows; iter; iter = iter->next)
        {
            if (iter->parent == window->handle)
            {
                if (!reply.first_child) reply.first_child = iter->handle;
                reply.last_child = iter->handle;
            }
            if (iter == window)
            {
                if (prev && prev->parent == window->parent) reply.prev_sibling = prev->handle;
            }
            else if (prev == window && iter->parent == window->parent)
                reply.next_sibling = iter->handle;

            if (iter->parent == window->parent)
            {
                if (!reply.first_sibling) reply.first_sibling = iter->handle;
                reply.last_sibling = iter->handle;
            }
            prev = iter;
        }
    }
    pthread_mutex_unlock( &horizon_server_objects_mutex );
    return horizon_server_write_reply( connection->reply_fd, &reply, sizeof(reply), NULL, 0 );
}

static int horizon_server_handle_set_window_pos( struct horizon_server_connection *connection,
                                                 const unsigned char *message,
                                                 const unsigned char *data, unsigned int data_size )
{
    const struct horizon_set_window_pos_request *request = (const void *)message;
    const struct horizon_rectangle *extra = (const void *)data;
    struct horizon_set_window_pos_reply reply;
    struct horizon_user_window *window;
    struct horizon_rectangle old_window, old_client;
    int geometry_changed;

    memset( &reply, 0, sizeof(reply) );
    pthread_mutex_lock( &horizon_server_objects_mutex );
    if (!(window = horizon_server_find_window_locked( request->handle )))
        reply.header.error = HORIZON_STATUS_INVALID_HANDLE;
    else if (request->window.right < request->window.left ||
             request->window.bottom < request->window.top)
        reply.header.error = HORIZON_STATUS_INVALID_PARAMETER;
    else
    {
        old_window = window->window_rect;
        old_client = window->client_rect;
        window->window_rect = request->window;
        window->client_rect = request->client;
        window->visible_rect = data_size >= sizeof(*extra) ? extra[0] : request->window;
        window->surface_rect = data_size >= 2 * sizeof(*extra) ? extra[1] : window->visible_rect;
        window->monitor_dpi = request->monitor_dpi ? request->monitor_dpi : 96;
        window->paint_flags = request->paint_flags;
        if (request->swp_flags & HORIZON_SWP_SHOWWINDOW) window->style |= HORIZON_WS_VISIBLE;
        if (request->swp_flags & HORIZON_SWP_HIDEWINDOW) window->style &= ~HORIZON_WS_VISIBLE;
        reply.new_style = window->style;
        reply.new_ex_style = window->ex_style;
        geometry_changed = memcmp( &old_window, &window->window_rect, sizeof(old_window) ) ||
                           memcmp( &old_client, &window->client_rect, sizeof(old_client) );
        if (geometry_changed && (window->style & HORIZON_WS_VISIBLE) &&
            !(request->swp_flags & HORIZON_SWP_NOREDRAW))
        {
            struct horizon_rectangle win_screen, client_screen;

            horizon_server_window_screen_rects_locked( window, &win_screen, &client_screen );
            window->has_update_rect = 0;
            window->needs_erase = 0;
            window->needs_nonclient = 0;
            horizon_server_mark_window_update_locked( window, client_screen, 1 );
            horizon_trace( "[HZPAINT] setpos resize hwnd=%08x rect=%d,%d-%d,%d swp=%x\n",
                           window->handle, client_screen.left, client_screen.top,
                           client_screen.right, client_screen.bottom, request->swp_flags );
        }
        if (window->paint_flags & HORIZON_SET_WINPOS_PAINT_SURFACE)
        {
            struct horizon_rectangle win_screen, client_screen;

            horizon_server_window_screen_rects_locked( window, &win_screen, &client_screen );
            if (!horizon_server_rect_empty( &client_screen ))
            {
                horizon_server_mark_window_update_locked( window, client_screen, 1 );
                horizon_trace( "[HZPAINT] setpos dirty hwnd=%08x rect=%d,%d-%d,%d flags=%x style=%x\n",
                               window->handle, client_screen.left, client_screen.top,
                               client_screen.right, client_screen.bottom,
                               window->paint_flags, window->style );
            }
            reply.surface_win = window->handle;
        }
    }
    pthread_mutex_unlock( &horizon_server_objects_mutex );
    return horizon_server_write_reply( connection->reply_fd, &reply, sizeof(reply), NULL, 0 );
}

static void horizon_server_window_screen_rects_locked( const struct horizon_user_window *window,
                                                       struct horizon_rectangle *window_rect,
                                                       struct horizon_rectangle *client_rect )
{
    struct horizon_user_window *parent;

    *window_rect = window->window_rect;
    *client_rect = window->client_rect;
    while (window->parent && (parent = horizon_server_find_window_locked( window->parent )))
    {
        horizon_server_offset_rect( window_rect, parent->client_rect.left, parent->client_rect.top );
        horizon_server_offset_rect( client_rect, parent->client_rect.left, parent->client_rect.top );
        window = parent;
    }
}

static int horizon_server_point_in_rect( const struct horizon_rectangle *rect, int x, int y );

static void horizon_server_collect_windows_from_point_locked( struct horizon_user_window *window,
                                                              int x, int y, unsigned int *handles,
                                                              unsigned int max_handles,
                                                              unsigned int *count )
{
    struct horizon_rectangle window_rect, client_rect;
    struct horizon_user_window *child;

    horizon_server_window_screen_rects_locked( window, &window_rect, &client_rect );
    if (!horizon_server_point_in_rect( &window_rect, x, y )) return;

    if (!(window->style & (HORIZON_WS_MINIMIZE | HORIZON_WS_DISABLED)) &&
        horizon_server_point_in_rect( &client_rect, x, y ))
    {
        for (child = horizon_windows; child; child = child->next)
        {
            if (child->parent != window->handle || !(child->style & HORIZON_WS_VISIBLE)) continue;
            horizon_server_collect_windows_from_point_locked( child, x, y, handles,
                                                              max_handles, count );
        }
    }

    if (*count < max_handles) handles[*count] = window->handle;
    (*count)++;
}

static int horizon_server_handle_get_window_children_from_point(
    struct horizon_server_connection *connection, const unsigned char *message )
{
    const struct horizon_get_window_children_from_point_request *request = (const void *)message;
    struct horizon_get_window_children_from_point_reply reply;
    struct horizon_user_window *parent;
    unsigned int *handles = NULL;
    unsigned int max_handles, returned, count = 0;
    unsigned int data_size = 0;

    memset( &reply, 0, sizeof(reply) );
    max_handles = request->header.reply_size / sizeof(*handles);
    if (max_handles && !(handles = malloc( max_handles * sizeof(*handles) )))
        reply.header.error = HORIZON_STATUS_NO_MEMORY;

    pthread_mutex_lock( &horizon_server_objects_mutex );
    if (!reply.header.error && !(parent = horizon_server_find_window_locked( request->parent )))
        reply.header.error = HORIZON_STATUS_INVALID_HANDLE;
    else if (!reply.header.error)
        horizon_server_collect_windows_from_point_locked( parent, request->x, request->y,
                                                          handles, max_handles, &count );
    pthread_mutex_unlock( &horizon_server_objects_mutex );

    reply.count = count;
    returned = reply.count < max_handles ? reply.count : max_handles;
    data_size = returned * sizeof(*handles);
    reply.header.reply_size = data_size;
    horizon_trace( "[HZINPUT] children_from_point parent=%08x pt=%d,%d count=%d returned=%u err=%08x\n",
                   request->parent, request->x, request->y, reply.count, returned,
                   reply.header.error );
    returned = horizon_server_write_reply( connection->reply_fd, &reply, sizeof(reply),
                                           handles, data_size );
    free( handles );
    return returned;
}

static unsigned int horizon_server_input_time(void)
{
    return (unsigned int)(armTicksToNs( armGetSystemTick() ) / 1000000ull);
}

static struct horizon_desktop_shm *horizon_server_desktop_shared_locked(
    struct horizon_obj_locator *locator )
{
    struct horizon_server_object *desktop;
    struct horizon_shared_object *shared;

    if (!(desktop = horizon_server_find_handle_object_locked( horizon_input_desktop,
                                                              HORIZON_SERVER_OBJECT_DESKTOP )))
        return NULL;
    *locator = desktop->desktop_locator;
    if (!(shared = horizon_server_shared_object_locked( *locator ))) return NULL;
    return &shared->shm.desktop;
}

static int horizon_server_point_in_rect( const struct horizon_rectangle *rect, int x, int y )
{
    return x >= rect->left && x < rect->right && y >= rect->top && y < rect->bottom;
}

static unsigned int horizon_server_window_depth_locked( const struct horizon_user_window *window )
{
    unsigned int depth = 0;

    while (window && window->parent)
    {
        depth++;
        window = horizon_server_find_window_locked( window->parent );
    }
    return depth;
}

static int horizon_server_window_is_descendant_locked( const struct horizon_user_window *window,
                                                       unsigned int ancestor )
{
    while (window)
    {
        if (window->handle == ancestor) return 1;
        window = window->parent ? horizon_server_find_window_locked( window->parent ) : NULL;
    }
    return 0;
}

static struct horizon_user_window *horizon_server_shallow_window_from_point_locked( int x, int y )
{
    struct horizon_user_window *window;

    for (window = horizon_windows; window; window = window->next)
    {
        struct horizon_rectangle rect, client;

        if (!(window->style & HORIZON_WS_VISIBLE)) continue;
        /* Wine's hardware queue stores the shallow top-level window.  The
         * client-side window_from_point() then performs authoritative child
         * and non-client hit testing before dispatch. */
        if (horizon_server_window_depth_locked( window ) != 1) continue;
        horizon_server_window_screen_rects_locked( window, &rect, &client );
        if (horizon_server_point_in_rect( &rect, x, y )) return window;
    }
    return NULL;
}

static unsigned int horizon_server_queue_mouse_locked( struct horizon_user_window *window,
                                                       unsigned int msg, unsigned long long wparam,
                                                       int x, int y, unsigned int time,
                                                       unsigned long long info )
{
    struct horizon_input_message *queued;

    if (!window) return HORIZON_STATUS_SUCCESS;
    if (!(queued = calloc( 1, sizeof(*queued) ))) return HORIZON_STATUS_NO_MEMORY;
    queued->id = horizon_next_input_message_id++;
    if (!queued->id) queued->id = horizon_next_input_message_id++;
    queued->tid = window->tid;
    queued->win = window->handle;
    queued->msg = msg;
    queued->wparam = wparam;
    queued->x = x;
    queued->y = y;
    queued->time = time;
    queued->info = info;
    *horizon_input_messages_tail = queued;
    horizon_input_messages_tail = &queued->next;
    return HORIZON_STATUS_SUCCESS;
}

static int horizon_server_mouse_message_matches( const struct horizon_get_message_request *request,
                                                 const struct horizon_input_message *queued )
{
    unsigned int nc_msg;

    if (!request->get_first && !request->get_last) return 1;
    if (queued->msg >= request->get_first && queued->msg <= request->get_last) return 1;
    nc_msg = queued->msg + HORIZON_WM_NCMOUSEFIRST - HORIZON_WM_MOUSEFIRST;
    return nc_msg >= request->get_first && nc_msg <= request->get_last;
}

static int horizon_server_handle_send_hardware_message( struct horizon_server_connection *connection,
                                                        const unsigned char *message )
{
    const struct horizon_send_hardware_message_request *request = (const void *)message;
    const struct horizon_hw_mouse_input *mouse = &request->input.mouse;
    struct horizon_send_hardware_message_reply reply;
    struct horizon_input_shm *input;
    struct horizon_desktop_shm *desktop;
    struct horizon_user_window *target = NULL;
    struct horizon_obj_locator desktop_locator;
    unsigned int status = HORIZON_STATUS_SUCCESS, time, flags, target_handle = 0;
    int x, y;

    memset( &reply, 0, sizeof(reply) );
    pthread_mutex_lock( &horizon_server_objects_mutex );
    if (request->input.type != HORIZON_INPUT_MOUSE)
        status = HORIZON_STATUS_NOT_IMPLEMENTED;
    else if (!(input = horizon_server_input_shared_locked()) ||
             !(desktop = horizon_server_desktop_shared_locked( &desktop_locator )))
        status = HORIZON_STATUS_INVALID_HANDLE;
    else
    {
        reply.prev_x = desktop->cursor.x;
        reply.prev_y = desktop->cursor.y;
        flags = mouse->flags;
        if (flags & HORIZON_MOUSEEVENTF_MOVE)
        {
            x = (flags & HORIZON_MOUSEEVENTF_ABSOLUTE) ? mouse->x : desktop->cursor.x + mouse->x;
            y = (flags & HORIZON_MOUSEEVENTF_ABSOLUTE) ? mouse->y : desktop->cursor.y + mouse->y;
            if (x < desktop->cursor.clip.left) x = desktop->cursor.clip.left;
            if (y < desktop->cursor.clip.top) y = desktop->cursor.clip.top;
            if (x >= desktop->cursor.clip.right) x = desktop->cursor.clip.right - 1;
            if (y >= desktop->cursor.clip.bottom) y = desktop->cursor.clip.bottom - 1;
            desktop->cursor.x = x;
            desktop->cursor.y = y;
        }
        else
        {
            x = desktop->cursor.x;
            y = desktop->cursor.y;
        }
        time = mouse->time ? mouse->time : horizon_server_input_time();
        desktop->cursor.last_change = time;

        if (input->capture) target = horizon_server_find_window_locked( input->capture );
        if (!target && request->win) target = horizon_server_find_window_locked( request->win );
        if (!target) target = horizon_server_shallow_window_from_point_locked( x, y );
        if (target) target_handle = target->handle;

        if ((flags & HORIZON_MOUSEEVENTF_MOVE) &&
            (status = horizon_server_queue_mouse_locked( target, HORIZON_WM_MOUSEMOVE,
                                                         horizon_mouse_buttons, x, y, time,
                                                         mouse->info )))
            goto done;
        if (flags & HORIZON_MOUSEEVENTF_LEFTDOWN)
        {
            horizon_mouse_buttons |= HORIZON_MK_LBUTTON;
            input->keystate[HORIZON_VK_LBUTTON] = 0x80;
            desktop->keystate[HORIZON_VK_LBUTTON] = 0x80;
            if ((status = horizon_server_queue_mouse_locked( target, HORIZON_WM_LBUTTONDOWN,
                                                             horizon_mouse_buttons, x, y, time,
                                                             mouse->info )))
                goto done;
        }
        if (flags & HORIZON_MOUSEEVENTF_LEFTUP)
        {
            horizon_mouse_buttons &= ~HORIZON_MK_LBUTTON;
            input->keystate[HORIZON_VK_LBUTTON] = 0;
            desktop->keystate[HORIZON_VK_LBUTTON] = 0;
            if ((status = horizon_server_queue_mouse_locked( target, HORIZON_WM_LBUTTONUP,
                                                             horizon_mouse_buttons, x, y, time,
                                                             mouse->info )))
                goto done;
        }
        input->keystate_serial++;
        desktop->keystate_serial++;
done:
        reply.new_x = desktop->cursor.x;
        reply.new_y = desktop->cursor.y;
        horizon_server_flush_input_locked();
        horizon_server_flush_session_range_locked(
            desktop_locator.offset,
            offsetof( struct horizon_shared_object, shm ) + sizeof(struct horizon_desktop_shm) );
    }
    reply.header.error = status;
    pthread_mutex_unlock( &horizon_server_objects_mutex );

    horizon_trace( "[HZINPUT] mouse flags=%x x=%d y=%d target=%08x buttons=%x err=%08x\n",
                   mouse->flags, reply.new_x, reply.new_y, target_handle,
                   horizon_mouse_buttons, status );
    return horizon_server_write_reply( connection->reply_fd, &reply, sizeof(reply), NULL, 0 );
}

static int horizon_server_handle_accept_hardware_message( struct horizon_server_connection *connection,
                                                          const unsigned char *message )
{
    const struct horizon_accept_hardware_message_request *request = (const void *)message;
    struct horizon_input_message **ptr;

    pthread_mutex_lock( &horizon_server_objects_mutex );
    for (ptr = &horizon_input_messages; *ptr; ptr = &(*ptr)->next)
    {
        struct horizon_input_message *queued = *ptr;

        if (queued->id != request->hw_id) continue;
        *ptr = queued->next;
        if (horizon_input_messages_tail == &queued->next) horizon_input_messages_tail = ptr;
        free( queued );
        break;
    }
    pthread_mutex_unlock( &horizon_server_objects_mutex );
    return horizon_server_write_status( connection->reply_fd, HORIZON_STATUS_SUCCESS );
}

static int horizon_server_handle_get_thread_input( struct horizon_server_connection *connection )
{
    struct horizon_get_thread_input_reply reply;

    memset( &reply, 0, sizeof(reply) );
    pthread_mutex_lock( &horizon_server_objects_mutex );
    reply.header.error = horizon_server_ensure_input_locked();
    if (!reply.header.error) reply.locator = horizon_input_locator;
    pthread_mutex_unlock( &horizon_server_objects_mutex );
    return horizon_server_write_reply( connection->reply_fd, &reply, sizeof(reply), NULL, 0 );
}

static int horizon_server_handle_set_foreground_window( struct horizon_server_connection *connection,
                                                        const unsigned char *message )
{
    const struct horizon_set_foreground_window_request *request = (const void *)message;
    struct horizon_set_foreground_window_reply reply;
    struct horizon_input_shm *input;

    memset( &reply, 0, sizeof(reply) );
    pthread_mutex_lock( &horizon_server_objects_mutex );
    if (request->handle && !horizon_server_find_window_locked( request->handle ))
        reply.header.error = HORIZON_STATUS_INVALID_HANDLE;
    else if (!(input = horizon_server_input_shared_locked()))
        reply.header.error = HORIZON_STATUS_INVALID_HANDLE;
    else
    {
        reply.previous = input->active;
        input->foreground = 1;
        horizon_server_flush_input_locked();
    }
    pthread_mutex_unlock( &horizon_server_objects_mutex );
    return horizon_server_write_reply( connection->reply_fd, &reply, sizeof(reply), NULL, 0 );
}

static int horizon_server_handle_set_input_window( struct horizon_server_connection *connection,
                                                   const unsigned char *message, int which )
{
    const struct horizon_input_window_request *request = (const void *)message;
    struct horizon_input_window_reply reply;
    struct horizon_input_shm *input;
    unsigned int *slot;

    memset( &reply, 0, sizeof(reply) );
    pthread_mutex_lock( &horizon_server_objects_mutex );
    if (request->handle && !horizon_server_find_window_locked( request->handle ))
        reply.header.error = HORIZON_STATUS_INVALID_HANDLE;
    else if (!(input = horizon_server_input_shared_locked()))
        reply.header.error = HORIZON_STATUS_INVALID_HANDLE;
    else
    {
        slot = which == HORIZON_REQ_SET_FOCUS_WINDOW ? &input->focus : &input->active;
        reply.previous = *slot;
        *slot = request->handle;
        horizon_server_flush_input_locked();
    }
    pthread_mutex_unlock( &horizon_server_objects_mutex );
    return horizon_server_write_reply( connection->reply_fd, &reply, sizeof(reply), NULL, 0 );
}

static int horizon_server_handle_set_capture_window( struct horizon_server_connection *connection,
                                                     const unsigned char *message )
{
    const struct horizon_set_capture_window_request *request = (const void *)message;
    struct horizon_set_capture_window_reply reply;
    struct horizon_input_shm *input;

    memset( &reply, 0, sizeof(reply) );
    pthread_mutex_lock( &horizon_server_objects_mutex );
    if (request->handle && !horizon_server_find_window_locked( request->handle ))
        reply.header.error = HORIZON_STATUS_INVALID_HANDLE;
    else if (!(input = horizon_server_input_shared_locked()))
        reply.header.error = HORIZON_STATUS_INVALID_HANDLE;
    else
    {
        reply.previous = input->capture;
        input->capture = request->handle;
        input->menu_owner = (request->flags & HORIZON_CAPTURE_MENU) ? request->handle : 0;
        input->move_size = (request->flags & HORIZON_CAPTURE_MOVESIZE) ? request->handle : 0;
        reply.full_handle = input->capture;
        horizon_server_flush_input_locked();
    }
    pthread_mutex_unlock( &horizon_server_objects_mutex );
    return horizon_server_write_reply( connection->reply_fd, &reply, sizeof(reply), NULL, 0 );
}

static int horizon_server_handle_set_cursor( struct horizon_server_connection *connection,
                                             const unsigned char *message )
{
    const struct horizon_set_cursor_request *request = (const void *)message;
    struct horizon_set_cursor_reply reply;
    struct horizon_input_shm *input;
    struct horizon_desktop_shm *desktop;
    struct horizon_obj_locator desktop_locator;

    memset( &reply, 0, sizeof(reply) );
    pthread_mutex_lock( &horizon_server_objects_mutex );
    if (!(input = horizon_server_input_shared_locked()) ||
        !(desktop = horizon_server_desktop_shared_locked( &desktop_locator )))
        reply.header.error = HORIZON_STATUS_INVALID_HANDLE;
    else
    {
        reply.prev_handle = input->cursor;
        reply.prev_count = input->cursor_count;
        reply.prev_x = desktop->cursor.x;
        reply.prev_y = desktop->cursor.y;
        if (request->flags & HORIZON_SET_CURSOR_HANDLE) input->cursor = request->handle;
        if (request->flags & HORIZON_SET_CURSOR_COUNT) input->cursor_count += request->show_count;
        if (request->flags & HORIZON_SET_CURSOR_POS)
        {
            desktop->cursor.x = request->x;
            desktop->cursor.y = request->y;
            desktop->cursor.last_change = horizon_server_input_time();
        }
        if (request->flags & HORIZON_SET_CURSOR_CLIP) desktop->cursor.clip = request->clip;
        if (request->flags & HORIZON_SET_CURSOR_NOCLIP)
        {
            desktop->cursor.clip.left = desktop->cursor.clip.top = 0;
            desktop->cursor.clip.right = 1280;
            desktop->cursor.clip.bottom = 720;
        }
        reply.new_x = desktop->cursor.x;
        reply.new_y = desktop->cursor.y;
        reply.new_clip = desktop->cursor.clip;
        reply.last_change = desktop->cursor.last_change;
        horizon_server_flush_input_locked();
        horizon_server_flush_session_range_locked(
            desktop_locator.offset,
            offsetof( struct horizon_shared_object, shm ) + sizeof(struct horizon_desktop_shm) );
    }
    pthread_mutex_unlock( &horizon_server_objects_mutex );
    return horizon_server_write_reply( connection->reply_fd, &reply, sizeof(reply), NULL, 0 );
}

static int horizon_server_handle_get_window_rectangles( struct horizon_server_connection *connection,
                                                        const unsigned char *message )
{
    const struct horizon_get_window_rectangles_request *request = (const void *)message;
    struct horizon_get_window_rectangles_reply reply;
    struct horizon_user_window *window;

    memset( &reply, 0, sizeof(reply) );
    pthread_mutex_lock( &horizon_server_objects_mutex );
    if (!(window = horizon_server_find_window_locked( request->handle )))
        reply.header.error = HORIZON_STATUS_INVALID_HANDLE;
    else
    {
        reply.window = window->window_rect;
        reply.client = window->client_rect;
        switch (request->relative)
        {
        case HORIZON_COORDS_CLIENT:
            horizon_server_offset_rect( &reply.window, -window->client_rect.left, -window->client_rect.top );
            horizon_server_offset_rect( &reply.client, -window->client_rect.left, -window->client_rect.top );
            break;
        case HORIZON_COORDS_WINDOW:
            horizon_server_offset_rect( &reply.window, -window->window_rect.left, -window->window_rect.top );
            horizon_server_offset_rect( &reply.client, -window->window_rect.left, -window->window_rect.top );
            break;
        case HORIZON_COORDS_PARENT:
            break;
        case HORIZON_COORDS_SCREEN:
            horizon_server_window_screen_rects_locked( window, &reply.window, &reply.client );
            break;
        default:
            reply.header.error = HORIZON_STATUS_INVALID_PARAMETER;
            break;
        }
    }
    pthread_mutex_unlock( &horizon_server_objects_mutex );
    return horizon_server_write_reply( connection->reply_fd, &reply, sizeof(reply), NULL, 0 );
}

static struct horizon_user_window *horizon_server_get_surface_window_locked( struct horizon_user_window *window )
{
    struct horizon_user_window *parent;

    while (!(window->paint_flags & HORIZON_SET_WINPOS_PAINT_SURFACE) && window->parent &&
           (parent = horizon_server_find_window_locked( window->parent )))
        window = parent;

    return window;
}

static int horizon_server_handle_get_visible_region( struct horizon_server_connection *connection,
                                                     const unsigned char *message )
{
    const struct horizon_get_visible_region_request *request = (const void *)message;
    struct horizon_get_visible_region_reply reply;
    struct horizon_rectangle rect, top_client, top_surface;
    struct horizon_rectangle window_rect, client_rect;
    const void *data = NULL;
    unsigned int data_size = 0;
    struct horizon_user_window *window, *top, *top_parent;

    memset( &reply, 0, sizeof(reply) );
    memset( &rect, 0, sizeof(rect) );
    pthread_mutex_lock( &horizon_server_objects_mutex );
    if (!(window = horizon_server_find_window_locked( request->window )))
        reply.header.error = HORIZON_STATUS_INVALID_HANDLE;
    else
    {
        top = horizon_server_get_surface_window_locked( window );
        horizon_server_window_screen_rects_locked( window, &window_rect, &client_rect );

        top_surface = top->surface_rect;
        if (top->parent && (top_parent = horizon_server_find_window_locked( top->parent )))
        {
            horizon_server_window_screen_rects_locked( top_parent, &rect, &top_client );
            horizon_server_offset_rect( &top_surface, top_client.left, top_client.top );
        }

        reply.top_win = top->handle;
        reply.top_rect = top_surface;
        reply.win_rect = (request->flags & HORIZON_DCX_WINDOW) ? window_rect : client_rect;
        reply.paint_flags = window->paint_flags;

        rect = horizon_server_intersect_rect( reply.win_rect, top_surface );
        if (!horizon_server_rect_empty( &rect ))
        {
            data = &rect;
            data_size = sizeof(rect);
            if (request->header.reply_size && data_size > request->header.reply_size)
            {
                reply.header.error = HORIZON_STATUS_BUFFER_OVERFLOW;
                data = NULL;
                data_size = 0;
            }
        }
        reply.total_size = horizon_server_rect_empty( &rect ) ? 0 : sizeof(rect);
        reply.header.reply_size = data_size;

        /* Same per-call fflush-to-SD-under-the-lock cost as the redraw
         * handler's trace (see the long comment there); same 5-sample
         * rate limit. This one fires on every BeginPaint/GetDCEx. */
        {
            static unsigned int logged;
            if (logged < 5)
            {
                horizon_trace( "[HZPAINT] visible hwnd=%08x top=%08x flags=%x rect=%d,%d-%d,%d "
                               "toprect=%d,%d-%d,%d data=%u err=%08x\n",
                               request->window, reply.top_win, request->flags,
                               reply.win_rect.left, reply.win_rect.top,
                               reply.win_rect.right, reply.win_rect.bottom,
                               reply.top_rect.left, reply.top_rect.top,
                               reply.top_rect.right, reply.top_rect.bottom,
                               data_size, reply.header.error );
                logged++;
            }
        }
    }
    pthread_mutex_unlock( &horizon_server_objects_mutex );

    return horizon_server_write_reply( connection->reply_fd, &reply, sizeof(reply), data, data_size );
}

static int horizon_server_handle_redraw_window( struct horizon_server_connection *connection,
                                                const unsigned char *message,
                                                const unsigned char *data, unsigned int data_size )
{
    const struct horizon_redraw_window_request *request = (const void *)message;
    const struct horizon_rectangle *rects = (const void *)data;
    struct horizon_user_window *window, *target = NULL, *child_win;
    struct horizon_redraw_window_reply reply;
    unsigned int count = data_size / sizeof(*rects);
    unsigned int status = HORIZON_STATUS_SUCCESS;

    memset( &reply, 0, sizeof(reply) );

    pthread_mutex_lock( &horizon_server_objects_mutex );
    if (!(window = horizon_server_find_window_locked( request->window )))
        status = HORIZON_STATUS_INVALID_HANDLE;
    else
    {
        if (request->flags & HORIZON_RDW_VALIDATE)
        {
            window->has_update_rect = 0;
            window->has_internal_paint = 0;
            window->needs_erase = 0;
            window->needs_nonclient = 0;
        }
        if (request->flags & HORIZON_RDW_NOINTERNALPAINT)
            window->has_internal_paint = 0;
        if (request->flags & HORIZON_RDW_INTERNALPAINT)
            window->has_internal_paint = 1;
        if (request->flags & HORIZON_RDW_ERASE)
            window->needs_erase = 1;

        if (request->flags & HORIZON_RDW_INVALIDATE)
        {
            struct horizon_rectangle win_screen, client_screen, dirty = {0};
            struct horizon_rectangle clip;
            unsigned int i;

            horizon_server_window_screen_rects_locked( window, &win_screen, &client_screen );
            clip = (request->flags & HORIZON_RDW_FRAME) ? win_screen : client_screen;
            if (!count)
                dirty = clip;
            else for (i = 0; i < count; i++)
            {
                struct horizon_rectangle rect = rects[i];

                if (horizon_server_rect_empty( &rect ))
                    rect = clip;
                else
                    horizon_server_offset_rect( &rect, win_screen.left, win_screen.top );
                rect = horizon_server_intersect_rect( rect, clip );
                dirty = horizon_server_union_rect( dirty, rect );
            }
            if (!horizon_server_rect_empty( &dirty ))
            {
                horizon_server_mark_window_update_locked( window, dirty,
                                                          !!(request->flags & HORIZON_RDW_ERASE) );
                if (request->flags & HORIZON_RDW_FRAME) window->needs_nonclient = 1;
                horizon_server_redraw_children_locked( window, dirty, request->flags );
            }
        }

        /* Same from_child=0 search as horizon_server_handle_get_update_flags_ex,
         * computed unconditionally (pure read, no IPC, cheap in-memory tree
         * walk) so switch_update_now() (dlls/win32u/dce.c) can reuse this
         * reply instead of making its own separate get_update_flags_ex call
         * right after -- see the comment on redraw_window_reply in
         * server_protocol.h. UPDATE_PAINT|UPDATE_INTERNALPAINT|UPDATE_NOREGION
         * matches exactly what switch_update_now()'s own first search flags
         * always are. */
        {
            int past_from = 1;
            unsigned int search_flags = HORIZON_UPDATE_PAINT | HORIZON_UPDATE_INTERNALPAINT |
                                        HORIZON_UPDATE_NOREGION;
            unsigned int update_flags;

            if (request->flags & HORIZON_RDW_NOCHILDREN) search_flags |= HORIZON_UPDATE_NOCHILDREN;
            else if (request->flags & HORIZON_RDW_ALLCHILDREN) search_flags |= HORIZON_UPDATE_ALLCHILDREN;

            update_flags = horizon_server_find_window_update_locked( window, NULL, search_flags,
                                                                      &past_from, &target );
            if (target)
            {
                reply.child = target->handle;
                reply.flags = update_flags;
            }
            for (child_win = horizon_windows; child_win; child_win = child_win->next)
                if (child_win->parent == window->handle) { reply.has_children = 1; break; }
        }

        /* horizon_trace() does a synchronous vfprintf+fflush to the SD
         * card on every call (it used to be a full fopen/fclose cycle per
         * call, fixed at the source since; the per-line fflush remains, by
         * design, so traces survive a crash) -- on the server thread,
         * while holding horizon_server_objects_mutex, on literally every
         * redraw_window call this whole session has been trying to speed
         * up. Invisible to every client-side phase timer built tonight,
         * since it happens entirely inside the "IPC call took Xms" black
         * box. Rate-limited to the same 5-sample convention used
         * everywhere else this session. */
        {
            static unsigned int logged;
            if (logged < 5)
            {
                horizon_trace( "[HZPAINT] redraw hwnd=%08x flags=%x count=%u has=%u internal=%u erase=%u nc=%u rect=%d,%d-%d,%d "
                               "search_child=%08x search_flags=%x search_has_children=%u err=%08x\n",
                               request->window, request->flags, count, window->has_update_rect,
                               window->has_internal_paint, window->needs_erase, window->needs_nonclient,
                               window->update_rect.left, window->update_rect.top,
                               window->update_rect.right, window->update_rect.bottom,
                               reply.child, reply.flags, reply.has_children, status );
                logged++;
            }
        }
    }
    pthread_mutex_unlock( &horizon_server_objects_mutex );
    reply.header.error = status;
    return horizon_server_write_reply( connection->reply_fd, &reply, sizeof(reply), NULL, 0 );
}

static struct horizon_window_property *horizon_server_find_window_property_locked(
    struct horizon_user_window *window, unsigned int atom )
{
    struct horizon_window_property *property;

    for (property = window->properties; property; property = property->next)
        if (property->atom == atom) return property;
    return NULL;
}

static unsigned int horizon_server_resolve_property_atom_locked( unsigned short request_atom,
                                                                 const unsigned char *name,
                                                                 unsigned int name_len, int add,
                                                                 unsigned int *atom )
{
    if (name_len)
    {
        if (add) return horizon_server_add_atom_locked( name, name_len, 0, atom );
        return horizon_server_find_atom_locked( name, name_len, atom );
    }
    *atom = request_atom;
    return HORIZON_STATUS_SUCCESS;
}

static int horizon_server_handle_set_window_property( struct horizon_server_connection *connection,
                                                       const unsigned char *message,
                                                       const unsigned char *data,
                                                       unsigned int data_size )
{
    const struct horizon_set_window_property_request *request = (const void *)message;
    struct horizon_window_property *property;
    struct horizon_user_window *window;
    unsigned int atom = 0, status = HORIZON_STATUS_SUCCESS;

    pthread_mutex_lock( &horizon_server_objects_mutex );
    if (!(window = horizon_server_find_window_locked( request->window )))
        status = HORIZON_STATUS_INVALID_HANDLE;
    else if ((status = horizon_server_resolve_property_atom_locked( request->atom, data, data_size,
                                                                    1, &atom )))
        ;
    else if (!atom)
        status = HORIZON_STATUS_INVALID_PARAMETER;
    else if ((property = horizon_server_find_window_property_locked( window, atom )))
    {
        property->data = request->data;
        property->string = !!data_size;
    }
    else if (!(property = calloc( 1, sizeof(*property) )))
        status = HORIZON_STATUS_NO_MEMORY;
    else
    {
        property->atom = atom;
        property->string = !!data_size;
        property->data = request->data;
        property->next = window->properties;
        window->properties = property;
    }
    pthread_mutex_unlock( &horizon_server_objects_mutex );

    horizon_trace( "[HZPROP] set hwnd=%08x atom=%04x data=%016llx string=%u err=%08x\n",
                   request->window, atom, request->data, !!data_size, status );
    return horizon_server_write_status( connection->reply_fd, status );
}

static int horizon_server_handle_get_window_property( struct horizon_server_connection *connection,
                                                       const unsigned char *message,
                                                       const unsigned char *data,
                                                       unsigned int data_size, int remove )
{
    const struct horizon_window_property_request *request = (const void *)message;
    struct horizon_window_property **ptr, *property = NULL;
    struct horizon_window_property_reply reply;
    struct horizon_user_window *window;
    unsigned int atom = 0;

    memset( &reply, 0, sizeof(reply) );
    pthread_mutex_lock( &horizon_server_objects_mutex );
    if (!(window = horizon_server_find_window_locked( request->window )))
        reply.header.error = HORIZON_STATUS_INVALID_HANDLE;
    else if ((reply.header.error = horizon_server_resolve_property_atom_locked(
                  request->atom, data, data_size, 0, &atom )))
        ;
    else if (atom)
    {
        for (ptr = &window->properties; *ptr; ptr = &(*ptr)->next)
        {
            if ((*ptr)->atom != atom) continue;
            property = *ptr;
            reply.data = property->data;
            if (remove)
            {
                *ptr = property->next;
                free( property );
            }
            break;
        }
    }
    pthread_mutex_unlock( &horizon_server_objects_mutex );

    horizon_trace( "[HZPROP] %s hwnd=%08x atom=%04x data=%016llx err=%08x\n",
                   remove ? "remove" : "get", request->window, atom, reply.data,
                   reply.header.error );
    return horizon_server_write_reply( connection->reply_fd, &reply, sizeof(reply), NULL, 0 );
}

static int horizon_server_handle_get_window_properties( struct horizon_server_connection *connection,
                                                         const unsigned char *message )
{
    const struct horizon_get_window_properties_request *request = (const void *)message;
    struct horizon_get_window_properties_reply reply;
    struct horizon_window_property *property;
    struct horizon_property_data *data = NULL;
    struct horizon_user_window *window;
    unsigned int count = 0, max_count, index = 0, data_size = 0;
    int status;

    memset( &reply, 0, sizeof(reply) );
    max_count = request->header.reply_size / sizeof(*data);

    pthread_mutex_lock( &horizon_server_objects_mutex );
    if (!(window = horizon_server_find_window_locked( request->window )))
        reply.header.error = HORIZON_STATUS_INVALID_HANDLE;
    else
    {
        for (property = window->properties; property; property = property->next) count++;
        reply.total = count;
        if (count > max_count) count = max_count;
        if (count && !(data = calloc( count, sizeof(*data) )))
            reply.header.error = HORIZON_STATUS_NO_MEMORY;
        else
        {
            for (property = window->properties; property && index < count; property = property->next)
            {
                data[index].atom = property->atom;
                data[index].string = property->string;
                data[index].data = property->data;
                index++;
            }
            data_size = index * sizeof(*data);
            reply.header.reply_size = data_size;
        }
    }
    pthread_mutex_unlock( &horizon_server_objects_mutex );

    horizon_trace( "[HZPROP] list hwnd=%08x total=%d returned=%u err=%08x\n",
                   request->window, reply.total, index, reply.header.error );
    status = horizon_server_write_reply( connection->reply_fd, &reply, sizeof(reply), data, data_size );
    free( data );
    return status;
}

static int horizon_server_handle_get_update_region( struct horizon_server_connection *connection,
                                                    const unsigned char *message )
{
    const struct horizon_get_update_region_request *request = (const void *)message;
    struct horizon_get_update_region_reply reply;
    struct horizon_rectangle rect;
    const void *data = NULL;
    unsigned int data_size = 0;
    struct horizon_user_window *window, *from_child = NULL, *target = NULL;
    unsigned int update_flags = 0;
    int past_from;

    memset( &reply, 0, sizeof(reply) );
    memset( &rect, 0, sizeof(rect) );

    pthread_mutex_lock( &horizon_server_objects_mutex );
    if (!(window = horizon_server_find_window_locked( request->window )))
        reply.header.error = HORIZON_STATUS_INVALID_HANDLE;
    else if (request->from_child &&
             !(from_child = horizon_server_find_window_locked( request->from_child )))
        reply.header.error = HORIZON_STATUS_INVALID_HANDLE;
    else
    {
        past_from = !from_child;
        update_flags = horizon_server_find_window_update_locked( window, from_child, request->flags,
                                                                 &past_from, &target );
        if (from_child && !past_from)
            reply.header.error = HORIZON_STATUS_INVALID_PARAMETER;
        else if (!target && !(request->flags & HORIZON_UPDATE_NOREGION) &&
                 !from_child && window->has_update_rect)
        {
            target = window;
            reply.child = window->handle;
            rect = window->update_rect;
            reply.total_size = sizeof(rect);
            if (!request->header.reply_size || request->header.reply_size >= sizeof(rect))
            {
                data = &rect;
                data_size = sizeof(rect);
                reply.header.reply_size = data_size;
            }
            else reply.header.error = HORIZON_STATUS_BUFFER_OVERFLOW;
        }
        else if (target)
        {
            reply.child = target->handle;
            reply.flags = update_flags;
            if (target->has_update_rect)
            {
                rect = target->update_rect;
                reply.total_size = sizeof(rect);
                if (!(request->flags & HORIZON_UPDATE_NOREGION))
                {
                    data = &rect;
                    data_size = sizeof(rect);
                }
            }
            reply.header.reply_size = data_size;
            if (!(request->flags & HORIZON_UPDATE_NOREGION))
            {
                if (update_flags & (HORIZON_UPDATE_PAINT | HORIZON_UPDATE_INTERNALPAINT))
                {
                    target->has_update_rect = 0;
                    target->has_internal_paint = 0;
                    target->needs_erase = 0;
                    target->needs_nonclient = 0;
                }
                else
                {
                    if (update_flags & HORIZON_UPDATE_ERASE) target->needs_erase = 0;
                    if (update_flags & HORIZON_UPDATE_NONCLIENT) target->needs_nonclient = 0;
                }
            }
        }
        /* Same per-call fflush-to-SD-under-the-lock cost as the redraw
         * handler's trace (see the long comment there); same 5-sample
         * rate limit. This one fires up to 3x per paint cycle. */
        {
            static unsigned int logged;
            if (logged < 5)
            {
                horizon_trace( "[HZPAINT] get_update hwnd=%08x from=%08x req=%x child=%08x flags=%x size=%u has=%u internal=%u erase=%u nc=%u rect=%d,%d-%d,%d err=%08x\n",
                               request->window, request->from_child, request->flags, reply.child, reply.flags,
                               reply.total_size, target ? target->has_update_rect : window->has_update_rect,
                               target ? target->has_internal_paint : window->has_internal_paint,
                               target ? target->needs_erase : window->needs_erase,
                               target ? target->needs_nonclient : window->needs_nonclient,
                               rect.left, rect.top, rect.right, rect.bottom,
                               reply.header.error );
                logged++;
            }
        }
    }
    pthread_mutex_unlock( &horizon_server_objects_mutex );

    return horizon_server_write_reply( connection->reply_fd, &reply, sizeof(reply), data, data_size );
}

/* Combined get_update_region + get_visible_region handler for
 * NtUserBeginPaint's specific call sequence (see dlls/win32u/dce.c,
 * switch_prefetch_paint_regions, gated behind WINE_NX_BATCH_PAINT_REGIONS).
 * The two halves below are a deliberately literal transcription of
 * horizon_server_handle_get_update_region and horizon_server_handle_get_visible_region
 * -- same branch structure, same field names renamed to update_* / visible_*,
 * same pre-existing asymmetry where the "!target" shortcut branch does not
 * clear has_update_rect/needs_erase/needs_nonclient (only the "else if
 * (target)" branch does) -- preserved intentionally rather than "fixed",
 * since this handler's job is to combine the two round trips, not change
 * either one's semantics. Confirmed via horizon_server_handle_get_visible_region
 * that the visible-region half has no dependency on the update-region half's
 * result, so both can be computed under one lock pass with no ordering
 * requirement between them. */
static int horizon_server_handle_get_paint_regions( struct horizon_server_connection *connection,
                                                     const unsigned char *message )
{
    const struct horizon_get_paint_regions_request *request = (const void *)message;
    struct horizon_get_paint_regions_reply reply;
    struct horizon_rectangle update_rect, visible_rect;
    struct horizon_rectangle window_rect, client_rect, top_surface;
    unsigned char combined_data[2 * sizeof(struct horizon_rectangle)];
    unsigned int update_data_size = 0, visible_data_size = 0;
    struct horizon_user_window *window, *from_child = NULL, *target = NULL;
    struct horizon_user_window *top, *top_parent;
    unsigned int update_flags = 0;
    int past_from;

    memset( &reply, 0, sizeof(reply) );
    memset( &update_rect, 0, sizeof(update_rect) );
    memset( &visible_rect, 0, sizeof(visible_rect) );

    pthread_mutex_lock( &horizon_server_objects_mutex );
    if (!(window = horizon_server_find_window_locked( request->window )))
        reply.header.error = HORIZON_STATUS_INVALID_HANDLE;
    else if (request->from_child &&
             !(from_child = horizon_server_find_window_locked( request->from_child )))
        reply.header.error = HORIZON_STATUS_INVALID_HANDLE;
    else
    {
        /* -- update-region half -- */
        past_from = !from_child;
        update_flags = horizon_server_find_window_update_locked( window, from_child, request->update_flags,
                                                                  &past_from, &target );
        if (from_child && !past_from)
            reply.header.error = HORIZON_STATUS_INVALID_PARAMETER;
        else if (!target && !(request->update_flags & HORIZON_UPDATE_NOREGION) &&
                 !from_child && window->has_update_rect)
        {
            target = window;
            reply.child = window->handle;
            update_rect = window->update_rect;
            reply.update_total_size = sizeof(update_rect);
            if (!request->header.reply_size || request->header.reply_size >= sizeof(update_rect))
                update_data_size = sizeof(update_rect);
            else
                reply.header.error = HORIZON_STATUS_BUFFER_OVERFLOW;
        }
        else if (target)
        {
            reply.child = target->handle;
            reply.update_flags = update_flags;
            if (target->has_update_rect)
            {
                update_rect = target->update_rect;
                reply.update_total_size = sizeof(update_rect);
                if (!(request->update_flags & HORIZON_UPDATE_NOREGION))
                    update_data_size = sizeof(update_rect);
            }
            if (!(request->update_flags & HORIZON_UPDATE_NOREGION))
            {
                if (update_flags & (HORIZON_UPDATE_PAINT | HORIZON_UPDATE_INTERNALPAINT))
                {
                    target->has_update_rect = 0;
                    target->has_internal_paint = 0;
                    target->needs_erase = 0;
                    target->needs_nonclient = 0;
                }
                else
                {
                    if (update_flags & HORIZON_UPDATE_ERASE) target->needs_erase = 0;
                    if (update_flags & HORIZON_UPDATE_NONCLIENT) target->needs_nonclient = 0;
                }
            }
        }

        /* -- visible-region half, only when the update half didn't already fail -- */
        if (!reply.header.error)
        {
            struct horizon_rectangle top_parent_window_rect, top_parent_client_rect;

            top = horizon_server_get_surface_window_locked( window );
            horizon_server_window_screen_rects_locked( window, &window_rect, &client_rect );

            top_surface = top->surface_rect;
            if (top->parent && (top_parent = horizon_server_find_window_locked( top->parent )))
            {
                horizon_server_window_screen_rects_locked( top_parent, &top_parent_window_rect,
                                                            &top_parent_client_rect );
                horizon_server_offset_rect( &top_surface, top_parent_client_rect.left,
                                            top_parent_client_rect.top );
            }

            reply.top_win = top->handle;
            reply.top_rect = top_surface;
            reply.win_rect = (request->visible_flags & HORIZON_DCX_WINDOW) ? window_rect : client_rect;
            reply.paint_flags = window->paint_flags;

            visible_rect = horizon_server_intersect_rect( reply.win_rect, top_surface );
            if (!horizon_server_rect_empty( &visible_rect ))
            {
                if (!request->header.reply_size ||
                    request->header.reply_size >= update_data_size + sizeof(visible_rect))
                    visible_data_size = sizeof(visible_rect);
                else
                    reply.header.error = HORIZON_STATUS_BUFFER_OVERFLOW;
            }
            reply.visible_total_size = horizon_server_rect_empty( &visible_rect ) ? 0 : sizeof(visible_rect);
        }

        if (!reply.header.error)
        {
            unsigned int off = 0;
            if (update_data_size) { memcpy( combined_data + off, &update_rect, update_data_size ); off += update_data_size; }
            if (visible_data_size) { memcpy( combined_data + off, &visible_rect, visible_data_size ); off += visible_data_size; }
            reply.header.reply_size = off;
        }

        /* Same unconditional fopen/write/fclose issue as redraw_window's
         * own trace above -- this one is worse, since get_paint_regions is
         * the single most expensive IPC call this whole session has been
         * chasing (~21-22ms average). Same fix. */
        {
            static unsigned int logged;
            if (logged < 5)
            {
                horizon_trace( "[HZPAINT] get_paint_regions hwnd=%08x from=%08x child=%08x uflags=%x top=%08x "
                               "vflags=%x paint=%x usize=%u vsize=%u err=%08x\n",
                               request->window, request->from_child, reply.child, reply.update_flags,
                               reply.top_win, request->visible_flags, reply.paint_flags,
                               reply.update_total_size, reply.visible_total_size, reply.header.error );
                logged++;
            }
        }
    }
    pthread_mutex_unlock( &horizon_server_objects_mutex );

    return horizon_server_write_reply( connection->reply_fd, &reply, sizeof(reply),
                                       reply.header.error ? NULL : combined_data,
                                       reply.header.error ? 0 : reply.header.reply_size );
}

/* Peek-only get_update_region variant + has_children, for switch_update_now()
 * (dlls/win32u/dce.c). Faithfully mirrors horizon_server_handle_get_update_region's
 * search logic for the always-UPDATE_NOREGION case (get_update_flags()'s own
 * semantics) -- no rect data, no has_update_rect/needs_erase/needs_nonclient
 * mutation, since UPDATE_NOREGION already skips all of that in the original
 * handler for every call this replaces (confirmed by reading
 * horizon_server_find_window_update_locked: it never even looks at
 * HORIZON_UPDATE_NOREGION, that flag only ever gates the outer handler's
 * data-attachment/mutation logic, which this handler simply never does).
 * has_children is a separate, flat check of `window`'s own children,
 * independent of whatever the from_child search finds -- computed under the
 * same lock as the search, so it's a consistent snapshot of server state at
 * the instant this call runs. switch_update_now() combines it with its own
 * client-side generation-counter check before trusting it across the
 * WM_PAINT dispatch that follows. */
static int horizon_server_handle_get_update_flags_ex( struct horizon_server_connection *connection,
                                                       const unsigned char *message )
{
    const struct horizon_get_update_flags_ex_request *request = (const void *)message;
    struct horizon_get_update_flags_ex_reply reply;
    struct horizon_user_window *window, *from_child = NULL, *target = NULL, *child;
    unsigned int update_flags = 0;
    int past_from;

    memset( &reply, 0, sizeof(reply) );

    pthread_mutex_lock( &horizon_server_objects_mutex );
    if (!(window = horizon_server_find_window_locked( request->window )))
        reply.header.error = HORIZON_STATUS_INVALID_HANDLE;
    else if (request->from_child &&
             !(from_child = horizon_server_find_window_locked( request->from_child )))
        reply.header.error = HORIZON_STATUS_INVALID_HANDLE;
    else
    {
        past_from = !from_child;
        update_flags = horizon_server_find_window_update_locked( window, from_child, request->flags,
                                                                  &past_from, &target );
        if (from_child && !past_from)
            reply.header.error = HORIZON_STATUS_INVALID_PARAMETER;
        else if (target)
        {
            reply.child = target->handle;
            reply.flags = update_flags;
        }

        if (!reply.header.error)
        {
            for (child = horizon_windows; child; child = child->next)
                if (child->parent == window->handle) { reply.has_children = 1; break; }
        }

        /* Same unconditional fopen/write/fclose issue as redraw_window's
         * own trace above. Same fix. This handler backs the
         * update_now()-skip fast path from earlier this session, so it
         * fires once per frame whenever that cache misses. */
        {
            static unsigned int logged;
            if (logged < 5)
            {
                horizon_trace( "[HZPAINT] get_update_flags_ex hwnd=%08x from=%08x child=%08x flags=%x "
                               "has_children=%u err=%08x\n",
                               request->window, request->from_child, reply.child, reply.flags,
                               reply.has_children, reply.header.error );
                logged++;
            }
        }
    }
    pthread_mutex_unlock( &horizon_server_objects_mutex );

    return horizon_server_write_reply( connection->reply_fd, &reply, sizeof(reply), NULL, 0 );
}

/* Combined redraw_window(count==0) + get_update_flags_ex(from_child=0) --
 * see HORIZON_REQ_REDRAW_WINDOW_UPDATENOW above. Runs the exact same
 * mark-dirty logic as horizon_server_handle_redraw_window's count==0
 * branch (verified against that function directly, not re-derived),
 * immediately followed by the exact same from_child=0 search as
 * horizon_server_handle_get_update_flags_ex, all under one lock
 * acquisition -- collapsing what would otherwise be two separate IPC round
 * trips (and two separate lock/unlock cycles) into one. */
static int horizon_server_handle_redraw_window_updatenow( struct horizon_server_connection *connection,
                                                           const unsigned char *message )
{
    const struct horizon_redraw_window_updatenow_request *request = (const void *)message;
    struct horizon_redraw_window_updatenow_reply reply;
    struct horizon_user_window *window, *target = NULL, *child;
    unsigned int update_flags = 0;
    int past_from;

    memset( &reply, 0, sizeof(reply) );

    pthread_mutex_lock( &horizon_server_objects_mutex );
    if (!(window = horizon_server_find_window_locked( request->window )))
        reply.header.error = HORIZON_STATUS_INVALID_HANDLE;
    else
    {
        /* --- redraw_window's count==0 mark-dirty step --- */
        if (request->redraw_flags & HORIZON_RDW_VALIDATE)
        {
            window->has_update_rect = 0;
            window->has_internal_paint = 0;
            window->needs_erase = 0;
            window->needs_nonclient = 0;
        }
        if (request->redraw_flags & HORIZON_RDW_NOINTERNALPAINT)
            window->has_internal_paint = 0;
        if (request->redraw_flags & HORIZON_RDW_INTERNALPAINT)
            window->has_internal_paint = 1;
        if (request->redraw_flags & HORIZON_RDW_ERASE)
            window->needs_erase = 1;

        if (request->redraw_flags & HORIZON_RDW_INVALIDATE)
        {
            struct horizon_rectangle win_screen, client_screen, dirty;

            horizon_server_window_screen_rects_locked( window, &win_screen, &client_screen );
            dirty = (request->redraw_flags & HORIZON_RDW_FRAME) ? win_screen : client_screen;
            if (!horizon_server_rect_empty( &dirty ))
            {
                horizon_server_mark_window_update_locked( window, dirty,
                                                          !!(request->redraw_flags & HORIZON_RDW_ERASE) );
                if (request->redraw_flags & HORIZON_RDW_FRAME) window->needs_nonclient = 1;
                horizon_server_redraw_children_locked( window, dirty, request->redraw_flags );
            }
        }

        /* --- get_update_flags_ex's from_child=0 search step --- */
        past_from = 1;
        update_flags = horizon_server_find_window_update_locked( window, NULL, request->search_flags,
                                                                  &past_from, &target );
        if (target)
        {
            reply.child = target->handle;
            reply.flags = update_flags;
        }

        for (child = horizon_windows; child; child = child->next)
            if (child->parent == window->handle) { reply.has_children = 1; break; }

        /* Same unconditional fopen/write/fclose issue as redraw_window's
         * own trace above. Same fix. */
        {
            static unsigned int logged;
            if (logged < 5)
            {
                horizon_trace( "[HZPAINT] redraw_updatenow hwnd=%08x redraw_flags=%x search_flags=%x "
                               "child=%08x flags=%x has_children=%u\n",
                               request->window, request->redraw_flags, request->search_flags,
                               reply.child, reply.flags, reply.has_children );
                logged++;
            }
        }
    }
    pthread_mutex_unlock( &horizon_server_objects_mutex );

    return horizon_server_write_reply( connection->reply_fd, &reply, sizeof(reply), NULL, 0 );
}

static int horizon_server_handle_get_message( struct horizon_server_connection *connection,
                                              const unsigned char *message )
{
    const struct horizon_get_message_request *request = (const void *)message;
    struct horizon_get_message_reply reply;
    struct horizon_hardware_msg_data hardware;
    struct horizon_input_message *queued;
    struct horizon_user_window *window, *parent;
    const void *reply_data = NULL;
    unsigned int reply_data_size = 0;
    int paint_requested;

    memset( &reply, 0, sizeof(reply) );
    memset( &hardware, 0, sizeof(hardware) );
    paint_requested = (!request->get_first && !request->get_last) ||
                      (request->get_first <= HORIZON_WM_PAINT &&
                       request->get_last >= HORIZON_WM_PAINT);

    pthread_mutex_lock( &horizon_server_objects_mutex );
    if (!request->internal)
    {
        for (queued = horizon_input_messages; queued; queued = queued->next)
        {
            if (queued->tid != connection->tid) continue;
            if (request->hw_id && queued->id == request->hw_id) continue;
            if (!horizon_server_mouse_message_matches( request, queued )) continue;
            if (request->get_win)
            {
                window = horizon_server_find_window_locked( queued->win );
                if (!window || !horizon_server_window_is_descendant_locked( window, request->get_win ))
                    continue;
            }

            reply.win = queued->win;
            reply.msg = queued->msg;
            reply.wparam = queued->wparam;
            reply.lparam = queued->lparam;
            reply.type = HORIZON_MSG_HARDWARE;
            reply.x = queued->x;
            reply.y = queued->y;
            reply.time = queued->time;
            reply.total = sizeof(hardware);
            reply.header.reply_size = sizeof(hardware);
            hardware.info = queued->info;
            hardware.hw_id = queued->id;
            hardware.source.device = HORIZON_IMDT_MOUSE;
            hardware.source.origin = HORIZON_IMO_HARDWARE;
            reply_data = &hardware;
            reply_data_size = sizeof(hardware);
            break;
        }
    }
    if (!reply.win && paint_requested && !request->internal)
    {
        for (window = horizon_windows; window; window = window->next)
        {
            if (window->tid != connection->tid || !(window->style & HORIZON_WS_VISIBLE)) continue;
            if (!window->has_update_rect && !window->has_internal_paint) continue;
            if (request->get_win)
            {
                for (parent = window; parent && parent->handle != request->get_win; )
                    parent = parent->parent ? horizon_server_find_window_locked( parent->parent ) : NULL;
                if (!parent) continue;
            }
            reply.win = window->handle;
            reply.msg = HORIZON_WM_PAINT;
            reply.type = HORIZON_MSG_POSTED;
            break;
        }
    }
    pthread_mutex_unlock( &horizon_server_objects_mutex );

    if (!reply.win) return horizon_server_write_status( connection->reply_fd, HORIZON_STATUS_PENDING );
    if (reply.type == HORIZON_MSG_HARDWARE)
    {
        /* Gated by the !reply.win early-return above, so this and the
         * WM_PAINT trace below don't fire on every idle GetMessage/
         * PeekMessage poll -- only when a real message is actually
         * returned. Still rate-limited: every real touch/mouse message
         * during interactive testing would otherwise hit this, same
         * audit as the four hot paint-path call sites above. */
        static unsigned int logged;
        if (logged < 5)
        {
            horizon_trace( "[HZINPUT] get_message id=%u hwnd=%08x msg=%x x=%d y=%d\n",
                           hardware.hw_id, reply.win, reply.msg, reply.x, reply.y );
            logged++;
        }
        return horizon_server_write_reply( connection->reply_fd, &reply, sizeof(reply),
                                           reply_data, reply_data_size );
    }
    {
        static unsigned int logged;
        if (logged < 5)
        {
            horizon_trace( "[HZPAINT] get_message WM_PAINT hwnd=%08x flags=%x range=%x-%x\n",
                           reply.win, request->flags, request->get_first, request->get_last );
            logged++;
        }
    }
    return horizon_server_write_reply( connection->reply_fd, &reply, sizeof(reply), NULL, 0 );
}

static int horizon_server_handle_update_window_zorder( struct horizon_server_connection *connection,
                                                       const unsigned char *message )
{
    const struct horizon_update_window_zorder_request *request = (const void *)message;
    unsigned int status = HORIZON_STATUS_SUCCESS;

    pthread_mutex_lock( &horizon_server_objects_mutex );
    if (!horizon_server_find_window_locked( request->window ))
        status = HORIZON_STATUS_INVALID_HANDLE;
    pthread_mutex_unlock( &horizon_server_objects_mutex );
    return horizon_server_write_status( connection->reply_fd, status );
}

static int horizon_server_handle_get_desktop_window( struct horizon_server_connection *connection,
                                                     const unsigned char *message )
{
    struct horizon_get_desktop_window_reply reply;
    struct horizon_server_object *desktop;

    (void)message;
    memset( &reply, 0, sizeof(reply) );
    pthread_mutex_lock( &horizon_server_objects_mutex );
    if (!horizon_thread_desktop ||
        !(desktop = horizon_server_find_handle_object_locked( horizon_thread_desktop,
                                                              HORIZON_SERVER_OBJECT_DESKTOP )))
        reply.header.error = HORIZON_STATUS_INVALID_HANDLE;
    else
    {
        if (!desktop->desktop_top_window)
        {
            struct horizon_user_window *window;
            horizon_server_create_window_locked( 0, 0, HORIZON_DESKTOP_ATOM, 0, 0,
                                                 HORIZON_NTUSER_DPI_PER_MONITOR_AWARE,
                                                 0, 0, connection->pid, connection->tid, &window );
        }
        if (!desktop->desktop_msg_window)
        {
            struct horizon_atom_entry *message_atom;
            struct horizon_user_window *window;

            if ((message_atom = horizon_server_find_atom_name_locked(
                     (const unsigned char *)"M\0e\0s\0s\0a\0g\0e\0",
                     7 * sizeof(unsigned short) )))
                horizon_server_create_window_locked( 0, 0, message_atom->atom, 0, 0,
                                                     HORIZON_NTUSER_DPI_PER_MONITOR_AWARE,
                                                     0, 0, connection->pid, connection->tid, &window );
        }
        reply.top_window = desktop->desktop_top_window;
        reply.msg_window = desktop->desktop_msg_window;
    }
    pthread_mutex_unlock( &horizon_server_objects_mutex );
    horizon_trace( "[HZUSER] get_desktop_window pid=%u tid=%u -> top=%08x msg=%08x err=%08x\n",
                   connection->pid, connection->tid, reply.top_window, reply.msg_window,
                   reply.header.error );
    return horizon_server_write_reply( connection->reply_fd, &reply, sizeof(reply), NULL, 0 );
}

static int horizon_server_handle_create_file( struct horizon_server_connection *connection,
                                              const unsigned char *message,
                                              const unsigned char *data, unsigned int data_size )
{
    const struct horizon_create_file_request *request = (const void *)message;
    struct horizon_create_file_reply reply;
    struct horizon_server_handle_entry *entry = NULL;
    char *filename = NULL;
    char *stored_name = NULL;
    unsigned int attr_size;
    unsigned int filename_size;
    unsigned int mapped_access = horizon_server_map_generic_access( request->access );
    int flags;
    int is_dir = 0;
    int fd = -1;

    memset( &reply, 0, sizeof(reply) );
    reply.header.error = horizon_server_object_attributes_size( data, data_size, &attr_size );
    if (!reply.header.error)
    {
        filename_size = data_size - attr_size;
        if (!filename_size) reply.header.error = HORIZON_STATUS_INVALID_PARAMETER;
        else if (!(filename = malloc( filename_size + 1 ))) reply.header.error = HORIZON_STATUS_NO_MEMORY;
        else
        {
            memcpy( filename, data + attr_size, filename_size );
            filename[filename_size] = 0;
            reply.header.error = horizon_server_file_open_flags( mapped_access, request, &flags );
            is_dir = !!(request->options & HORIZON_FILE_DIRECTORY_FILE);
            if (is_dir && (request->options & HORIZON_FILE_NON_DIRECTORY_FILE))
                reply.header.error = HORIZON_STATUS_INVALID_PARAMETER;
        }
    }

    if (!reply.header.error && is_dir)
    {
        DIR *dir;

        if (!(dir = opendir( filename ))) reply.header.error = horizon_server_errno_status( errno );
        else
        {
            closedir( dir );
            if (!(stored_name = strdup( filename ))) reply.header.error = HORIZON_STATUS_NO_MEMORY;
        }
    }
    else if (!reply.header.error)
    {
        fd = open( filename, flags, 0666 );
        if (fd == -1) reply.header.error = horizon_server_errno_status( errno );
        else if (!(stored_name = strdup( filename ))) reply.header.error = HORIZON_STATUS_NO_MEMORY;
    }

    if (!reply.header.error)
    {
        pthread_mutex_lock( &horizon_server_objects_mutex );
        if ((entry = horizon_server_create_handle_locked( HORIZON_SERVER_OBJECT_FILE )))
        {
            entry->object->file_fd = fd;
            entry->object->file_name = stored_name;
            entry->object->file_access = mapped_access;
            entry->object->file_options = request->options;
            entry->object->file_is_dir = is_dir;
            reply.handle = entry->handle;
            stored_name = NULL;
            fd = -1;
        }
        else reply.header.error = HORIZON_STATUS_NO_MEMORY;
        pthread_mutex_unlock( &horizon_server_objects_mutex );
    }

    if (fd != -1) close( fd );
    if (is_dir)
        horizon_trace( "[HZDIR] create path=%s handle=%08x err=%08x\n",
                       filename ? filename : "<null>", reply.handle, reply.header.error );
    free( stored_name );
    free( filename );
    return horizon_server_write_reply( connection->reply_fd, &reply, sizeof(reply), NULL, 0 );
}

static int horizon_server_handle_get_handle_fd( struct horizon_server_connection *connection,
                                                const unsigned char *message )
{
    const struct horizon_get_handle_fd_request *request = (const void *)message;
    struct horizon_get_handle_fd_reply reply;
    struct horizon_server_handle_entry *entry;

    memset( &reply, 0, sizeof(reply) );
    pthread_mutex_lock( &horizon_server_objects_mutex );
    entry = horizon_server_find_handle_locked( request->handle );
    if (!entry) reply.header.error = HORIZON_STATUS_INVALID_HANDLE;
    else if ((entry->object->type != HORIZON_SERVER_OBJECT_FILE &&
              entry->object->type != HORIZON_SERVER_OBJECT_MAPPING &&
              entry->object->type != HORIZON_SERVER_OBJECT_SOCK) ||
             entry->object->file_fd == -1)
    {
        if (entry->object->type == HORIZON_SERVER_OBJECT_FILE && entry->object->file_is_dir)
            reply.header.error = HORIZON_STATUS_BAD_DEVICE_TYPE;
        else
            reply.header.error = HORIZON_STATUS_OBJECT_TYPE_MISMATCH;
    }
    else if (entry->object->type == HORIZON_SERVER_OBJECT_SOCK)
    {
        reply.type = HORIZON_FD_TYPE_SOCKET;
        reply.cacheable = 1;
        reply.access = FILE_READ_DATA | FILE_WRITE_DATA | FILE_APPEND_DATA;
        reply.options = entry->object->file_options;
        horizon_server_queue_fd( entry->object->file_fd, request->handle );
    }
    else
    {
        reply.type = HORIZON_FD_TYPE_FILE;
        reply.cacheable = 0;
        reply.access = entry->object->file_access;
        reply.options = entry->object->file_options;
        horizon_server_queue_fd( entry->object->file_fd, request->handle );
    }
    pthread_mutex_unlock( &horizon_server_objects_mutex );
    return horizon_server_write_reply( connection->reply_fd, &reply, sizeof(reply), NULL, 0 );
}

static char horizon_server_ascii_lower( char ch )
{
    return (ch >= 'A' && ch <= 'Z') ? ch + 'a' - 'A' : ch;
}

static int horizon_server_wildcard_match_tail( const char *mask, const char *name )
{
    while (*mask)
    {
        if (*mask == '*' || *mask == '<')
        {
            while (*mask == '*' || *mask == '<') mask++;
            if (!*mask) return 1;
            while (*name)
            {
                if (horizon_server_wildcard_match_tail( mask, name )) return 1;
                name++;
            }
            return horizon_server_wildcard_match_tail( mask, name );
        }
        if (*mask == '?' || *mask == '>' || *mask == '"')
        {
            if (!*name) return 0;
            mask++;
            name++;
            continue;
        }
        if (horizon_server_ascii_lower( *mask ) != horizon_server_ascii_lower( *name )) return 0;
        mask++;
        name++;
    }
    return !*name;
}

static int horizon_server_wildcard_match( const char *mask, const char *name )
{
    if (!mask || !mask[0]) return 1;
    if (!strcmp( mask, "*" ) || !strcmp( mask, "*.*" )) return 1;
    return horizon_server_wildcard_match_tail( mask, name );
}

static char *horizon_server_dir_mask_from_utf16( const unsigned char *data, unsigned int data_size )
{
    unsigned int i, len = data_size / sizeof(unsigned short);
    char *mask;

    if (!data_size) return NULL;
    if (data_size & 1) return NULL;
    if (!(mask = malloc( len + 1 ))) return NULL;
    for (i = 0; i < len; i++)
    {
        unsigned int ch = data[i * 2] | (data[i * 2 + 1] << 8);
        mask[i] = ch < 0x80 ? ch : '?';
    }
    mask[len] = 0;
    return mask;
}

static int horizon_server_handle_query_directory_file( struct horizon_server_connection *connection,
                                                       const unsigned char *message,
                                                       const unsigned char *data, unsigned int data_size )
{
    const struct horizon_query_directory_file_request *request = (const void *)message;
    struct horizon_query_directory_file_reply reply;
    struct horizon_server_handle_entry *entry;
    struct horizon_server_object *object = NULL;
    struct horizon_directory_file_entry dir_entry;
    unsigned int entry_size = 0, name_len = 0;
    unsigned char *out = NULL;
    unsigned int out_size = 0;
    char trace_name[256] = "<none>";
    char trace_mask[128] = "<all>";
    unsigned int trace_index = 0;
    char *new_mask = NULL;
    DIR *dir = NULL;
    unsigned int raw_index = 0;
    int mask_changed = 0;
    int ret;

    memset( &reply, 0, sizeof(reply) );

    if (data_size)
    {
        if (data_size & 1) reply.header.error = HORIZON_STATUS_INVALID_PARAMETER;
        else if (!(new_mask = horizon_server_dir_mask_from_utf16( data, data_size )))
            reply.header.error = HORIZON_STATUS_NO_MEMORY;
        else mask_changed = 1;
    }
    else if (request->restart_scan) mask_changed = 1;

    pthread_mutex_lock( &horizon_server_objects_mutex );
    if (!reply.header.error)
    {
        entry = horizon_server_find_handle_locked( request->handle );
        if (!entry) reply.header.error = HORIZON_STATUS_INVALID_HANDLE;
        else if (entry->object->type != HORIZON_SERVER_OBJECT_FILE || !entry->object->file_is_dir)
            reply.header.error = HORIZON_STATUS_OBJECT_TYPE_MISMATCH;
        else if (!entry->object->file_name) reply.header.error = HORIZON_STATUS_INVALID_HANDLE;
        else object = entry->object;
    }

    if (!reply.header.error)
    {
        if (request->restart_scan) object->dir_enum_index = 0;
        if (mask_changed)
        {
            free( object->dir_mask );
            object->dir_mask = new_mask;
            new_mask = NULL;
        }

        if (!(dir = opendir( object->file_name ))) reply.header.error = horizon_server_errno_status( errno );
    }

    while (!reply.header.error)
    {
        const char *name = NULL;
        struct dirent *de = NULL;

        if (raw_index == 0) name = ".";
        else if (raw_index == 1) name = "..";
        else
        {
            while ((de = readdir( dir )))
            {
                if (!strcmp( de->d_name, "." ) || !strcmp( de->d_name, ".." )) continue;
                name = de->d_name;
                break;
            }
            if (!name)
            {
                reply.header.error = HORIZON_STATUS_NO_MORE_FILES;
                break;
            }
        }

        if (raw_index++ < object->dir_enum_index) continue;
        if (!horizon_server_wildcard_match( object->dir_mask, name )) continue;

        name_len = horizon_server_utf16_name_len( name );
        entry_size = (sizeof(dir_entry) + name_len + 3) & ~3;
        reply.total_len = name_len;
        if (entry_size > request->header.reply_size)
        {
            reply.header.error = HORIZON_STATUS_INFO_LENGTH_MISMATCH;
            break;
        }
        if (!(out = calloc( 1, entry_size )))
        {
            reply.header.error = HORIZON_STATUS_NO_MEMORY;
            break;
        }

        dir_entry.name_len = name_len;
        memcpy( out, &dir_entry, sizeof(dir_entry) );
        horizon_server_write_utf16_name( out + sizeof(dir_entry), name );
        reply.header.reply_size = entry_size;
        out_size = entry_size;
        snprintf( trace_name, sizeof(trace_name), "%s", name );
        object->dir_enum_index = raw_index;
        break;
    }

    if (object)
    {
        trace_index = object->dir_enum_index;
        if (object->dir_mask) snprintf( trace_mask, sizeof(trace_mask), "%s", object->dir_mask );
    }
    if (dir) closedir( dir );
    pthread_mutex_unlock( &horizon_server_objects_mutex );
    horizon_trace( "[HZDIR] query handle=%08x restart=%u mask=%s name=%s idx=%u err=%08x\n",
                   request->handle, request->restart_scan, trace_mask, trace_name,
                   trace_index, reply.header.error );
    free( new_mask );

    ret = horizon_server_write_reply( connection->reply_fd, &reply, sizeof(reply), out, out_size );
    free( out );
    return ret;
}

/***********************************************************************
 * Thread bridge: REQ_new_thread / REQ_resume_thread / REQ_suspend_thread
 *
 * NtCreateThreadEx queues the thread's request pipe fd, then sends
 * new_thread.  We adopt the fd as a brand new server connection (each
 * Wine thread talks to the server over its own pipe) and hand back a
 * THREAD handle + tid.  init_thread on the new connection already
 * replies suspend=0, so CREATE_SUSPENDED is intentionally ignored;
 * resume_thread just acks with the expected previous suspend count.
 */

static void *horizon_server_thread( void *param );

static LONG horizon_server_next_tid = 4;

static int horizon_server_handle_new_thread( struct horizon_server_connection *connection,
                                             const unsigned char *message )
{
    const struct horizon_new_thread_request *request = (const void *)message;
    struct horizon_new_thread_reply reply;
    struct horizon_server_connection *thread_connection = NULL;
    struct horizon_server_handle_entry *entry;
    unsigned int fd_handle;
    pthread_t thread;
    int request_fd = horizon_server_take_client_fd( &fd_handle );

    memset( &reply, 0, sizeof(reply) );

    if (!(thread_connection = calloc( 1, sizeof(*thread_connection) )))
    {
        close( request_fd );
        reply.header.error = HORIZON_STATUS_NO_MEMORY;
        return horizon_server_write_reply( connection->reply_fd, &reply, sizeof(reply), NULL, 0 );
    }
    thread_connection->request_fd = request_fd;
    thread_connection->reply_fd = -1;
    thread_connection->wait_fd = -1;
    thread_connection->pid = connection->pid;
    thread_connection->tid = __sync_add_and_fetch( &horizon_server_next_tid, 4 );

    pthread_mutex_lock( &horizon_server_objects_mutex );
    entry = horizon_server_create_handle_locked( HORIZON_SERVER_OBJECT_THREAD );
    if (entry) reply.handle = entry->handle;
    pthread_mutex_unlock( &horizon_server_objects_mutex );

    if (!entry)
    {
        close( request_fd );
        free( thread_connection );
        reply.header.error = HORIZON_STATUS_NO_MEMORY;
        return horizon_server_write_reply( connection->reply_fd, &reply, sizeof(reply), NULL, 0 );
    }

    if ((errno = pthread_create( &thread, NULL, horizon_server_thread, thread_connection )))
    {
        close( request_fd );
        free( thread_connection );
        reply.handle = 0;
        reply.header.error = HORIZON_STATUS_NO_MEMORY;
        return horizon_server_write_reply( connection->reply_fd, &reply, sizeof(reply), NULL, 0 );
    }
    pthread_detach( thread );

    reply.tid = thread_connection->tid;
    horizon_trace( "[server] new_thread flags=%#x request_fd=%d -> handle=%08x tid=%u\n",
                   request->flags, request_fd, reply.handle, reply.tid );
    return horizon_server_write_reply( connection->reply_fd, &reply, sizeof(reply), NULL, 0 );
}

static int horizon_server_handle_resume_thread( struct horizon_server_connection *connection,
                                                const unsigned char *message )
{
    const struct horizon_resume_thread_request *request = (const void *)message;
    struct horizon_resume_thread_reply reply;
    struct horizon_server_handle_entry *entry;

    memset( &reply, 0, sizeof(reply) );
    pthread_mutex_lock( &horizon_server_objects_mutex );
    entry = horizon_server_find_handle_locked( request->handle );
    if (!entry || entry->object->type != HORIZON_SERVER_OBJECT_THREAD)
        reply.header.error = HORIZON_STATUS_INVALID_HANDLE;
    else
        reply.count = 1; /* threads never actually suspend; pretend one resume did it */
    pthread_mutex_unlock( &horizon_server_objects_mutex );
    return horizon_server_write_reply( connection->reply_fd, &reply, sizeof(reply), NULL, 0 );
}

/***********************************************************************
 * Socket bridge: \Device\Afd handles backed by libnx BSD sockets.
 *
 * ws2_32 opens \Device\Afd (open_file_object), then drives the socket
 * with AFD ioctls.  Control ioctls (create/connect/poll/...) land here
 * via REQ_ioctl and run directly against the unix fd.  Data ioctls
 * (recvmsg/sendmsg/sockopts) run client-side in ntdll's socket.c using
 * the fd from get_handle_fd; recv_socket/send_socket only ask us for
 * permission, answered with ALERTED + nonblocking so the client does
 * the actual I/O itself and maps EAGAIN to WSAEWOULDBLOCK.
 */

/* AFD ioctl codes, mirrored from include/wine/afd.h (CTL_CODE expanded;
 * winsock headers must not be included here next to the BSD ones). */
#define HORIZON_IOCTL_AFD_POLL              0x00012024 /* BEEP/0x809/BUFFERED */
#define HORIZON_IOCTL_AFD_GETSOCKNAME       0x0001202f /* BEEP/0x80b/NEITHER */
#define HORIZON_IOCTL_AFD_EVENT_SELECT      0x00012087 /* BEEP/0x821/NEITHER */
#define HORIZON_IOCTL_AFD_WINE_CREATE       0x00120320 /* NETWORK/200/BUFFERED */
#define HORIZON_IOCTL_AFD_WINE_CONNECT      0x0012032c /* NETWORK/203/BUFFERED */
#define HORIZON_IOCTL_AFD_WINE_SHUTDOWN     0x00120330 /* NETWORK/204/BUFFERED */
#define HORIZON_IOCTL_AFD_WINE_FIONBIO      0x00120344 /* NETWORK/209/BUFFERED */
#define HORIZON_IOCTL_AFD_WINE_GETPEERNAME  0x00120360 /* NETWORK/216/BUFFERED */
#define HORIZON_IOCTL_AFD_WINE_GET_SO_ERROR 0x00120378 /* NETWORK/222/BUFFERED */

#define HORIZON_AFD_POLL_READ        0x0001
#define HORIZON_AFD_POLL_OOB         0x0002
#define HORIZON_AFD_POLL_WRITE       0x0004
#define HORIZON_AFD_POLL_HUP         0x0008
#define HORIZON_AFD_POLL_RESET       0x0010
#define HORIZON_AFD_POLL_CLOSE       0x0020
#define HORIZON_AFD_POLL_CONNECT     0x0040
#define HORIZON_AFD_POLL_ACCEPT      0x0080
#define HORIZON_AFD_POLL_CONNECT_ERR 0x0100

#define HORIZON_WS_AF_UNSPEC 0
#define HORIZON_WS_AF_INET   2
#define HORIZON_WS_AF_INET6  23

static unsigned int horizon_sock_errno_status( int err )
{
    switch (err)
    {
        case 0:                 return HORIZON_STATUS_SUCCESS;
        case EBADF:             return HORIZON_STATUS_INVALID_HANDLE;
        case EBUSY:             return HORIZON_STATUS_DEVICE_BUSY;
        case EPERM:
        case EACCES:            return HORIZON_STATUS_ACCESS_DENIED;
        case EFAULT:            return HORIZON_STATUS_INVALID_PARAMETER;
        case EINVAL:            return HORIZON_STATUS_INVALID_PARAMETER;
        case ENFILE:
        case EMFILE:            return HORIZON_STATUS_TOO_MANY_OPENED_FILES;
        case EINPROGRESS:
        case EWOULDBLOCK:       return HORIZON_STATUS_DEVICE_NOT_READY;
        case EALREADY:          return HORIZON_STATUS_ADDRESS_ALREADY_ASSOCIATED;
        case ENOTSOCK:          return HORIZON_STATUS_OBJECT_TYPE_MISMATCH;
        case EMSGSIZE:          return HORIZON_STATUS_BUFFER_OVERFLOW;
        case EPROTONOSUPPORT:
#ifdef ESOCKTNOSUPPORT
        case ESOCKTNOSUPPORT:
#endif
#ifdef EPFNOSUPPORT
        case EPFNOSUPPORT:
#endif
        case EAFNOSUPPORT:
        case EPROTOTYPE:
        case EOPNOTSUPP:        return HORIZON_STATUS_NOT_SUPPORTED;
        case ENOPROTOOPT:       return HORIZON_STATUS_INVALID_PARAMETER;
        case EADDRINUSE:        return HORIZON_STATUS_SHARING_VIOLATION;
        case ENODEV:
        case EADDRNOTAVAIL:     return HORIZON_STATUS_INVALID_ADDRESS_COMPONENT;
        case ECONNREFUSED:      return HORIZON_STATUS_CONNECTION_REFUSED;
#ifdef ESHUTDOWN
        case ESHUTDOWN:         return HORIZON_STATUS_PIPE_DISCONNECTED;
#endif
        case ENOTCONN:          return HORIZON_STATUS_INVALID_CONNECTION;
        case ETIMEDOUT:         return HORIZON_STATUS_IO_TIMEOUT;
        case ENETUNREACH:       return HORIZON_STATUS_NETWORK_UNREACHABLE;
        case EHOSTUNREACH:      return HORIZON_STATUS_HOST_UNREACHABLE;
        case ENETDOWN:          return HORIZON_STATUS_NETWORK_BUSY;
        case EPIPE:
        case ECONNRESET:        return HORIZON_STATUS_CONNECTION_RESET;
        case ECONNABORTED:      return HORIZON_STATUS_CONNECTION_ABORTED;
        case EISCONN:           return HORIZON_STATUS_CONNECTION_ACTIVE;
        case ENOBUFS:
        case ENOMEM:            return HORIZON_STATUS_NO_MEMORY;
        default:
            horizon_trace( "[server] unmapped socket errno %d\n", err );
            return HORIZON_STATUS_UNSUCCESSFUL;
    }
}

/* GET_SO_ERROR hands the value straight to applications, so it must be
 * a WSA error (10xxx), not an NTSTATUS. */
static unsigned int horizon_sock_errno_wsa( int err )
{
    switch (err)
    {
        case 0:             return 0;
        case EINTR:         return 10004;
        case EACCES:
        case EPERM:         return 10013;
        case EINVAL:        return 10022;
        case EMFILE:
        case ENFILE:        return 10024;
        case EINPROGRESS:
        case EWOULDBLOCK:   return 10035;
        case EALREADY:      return 10037;
        case ENOTSOCK:      return 10038;
        case EMSGSIZE:      return 10040;
        case EOPNOTSUPP:    return 10045;
        case EAFNOSUPPORT:  return 10047;
        case EADDRINUSE:    return 10048;
        case EADDRNOTAVAIL: return 10049;
        case ENETDOWN:      return 10050;
        case ENETUNREACH:   return 10051;
        case ECONNABORTED:  return 10053;
        case EPIPE:
        case ECONNRESET:    return 10054;
        case ENOBUFS:       return 10055;
        case EISCONN:       return 10056;
        case ENOTCONN:      return 10057;
        case ETIMEDOUT:     return 10060;
        case ECONNREFUSED:  return 10061;
        case EHOSTUNREACH:  return 10065;
        default:
            horizon_trace( "[server] unmapped WSA errno %d\n", err );
            return 10022; /* WSAEINVAL */
    }
}

/* Windows sockaddr_in (16-bit sin_family, no sin_len) <-> BSD sockaddr_in.
 * libnx is IPv4 only; reject everything else cleanly. */
static unsigned int horizon_ws_sockaddr_to_unix( const unsigned char *ws, unsigned int len,
                                                 struct sockaddr_in *sa )
{
    unsigned short family;

    if (len < 16) return HORIZON_STATUS_INVALID_PARAMETER;
    memcpy( &family, ws, sizeof(family) );
    if (family != HORIZON_WS_AF_INET) return HORIZON_STATUS_NOT_SUPPORTED;

    memset( sa, 0, sizeof(*sa) );
    sa->sin_len = sizeof(*sa);
    sa->sin_family = AF_INET;
    memcpy( &sa->sin_port, ws + 2, 2 );
    memcpy( &sa->sin_addr, ws + 4, 4 );
    return HORIZON_STATUS_SUCCESS;
}

static unsigned int horizon_ws_sockaddr_from_unix( const struct sockaddr_in *sa,
                                                   unsigned char *ws, unsigned int len )
{
    unsigned short family = HORIZON_WS_AF_INET;

    if (len < 16) return 0;
    memset( ws, 0, 16 );
    memcpy( ws, &family, sizeof(family) );
    memcpy( ws + 2, &sa->sin_port, 2 );
    memcpy( ws + 4, &sa->sin_addr, 4 );
    return 16;
}

/* Look up an Afd handle and return its fd (or -1 before WINE_CREATE). */
static unsigned int horizon_server_find_sock_locked( unsigned int handle,
                                                     struct horizon_server_object **object )
{
    struct horizon_server_handle_entry *entry = horizon_server_find_handle_locked( handle );

    *object = NULL;
    if (!entry) return HORIZON_STATUS_INVALID_HANDLE;
    if (entry->object->type != HORIZON_SERVER_OBJECT_SOCK) return HORIZON_STATUS_OBJECT_TYPE_MISMATCH;
    *object = entry->object;
    return HORIZON_STATUS_SUCCESS;
}

/* Poller for WSAEventSelect: scans sockets with a registered event mask and
 * sets the associated event object when new activity shows up.  Waiters are
 * poll-based (select_wait re-checks on TIMEOUT), so flipping signaled under
 * the objects mutex is all it takes to wake them. */
static pthread_t horizon_sock_poller_thread_id;
static int horizon_sock_poller_running;

static void *horizon_sock_poller_thread( void *param )
{
    (void)param;
    for (;;)
    {
        struct horizon_server_handle_entry *entry;

        usleep( 50000 );
        pthread_mutex_lock( &horizon_server_objects_mutex );
        for (entry = horizon_server_handles; entry; entry = entry->next)
        {
            struct horizon_server_object *o = entry->object;
            struct pollfd pfd;
            int bits = 0, new_bits;

            if (o->type != HORIZON_SERVER_OBJECT_SOCK || o->file_fd == -1 || !o->sock_event_mask)
                continue;

            /* Horizon's poll() does not report POLLOUT when a nonblocking
             * connect completes, so probe completion directly: getpeername
             * succeeds once the socket is connected, ENOTCONN before. */
            if ((o->sock_event_mask & HORIZON_AFD_POLL_CONNECT) &&
                !(o->sock_pending_events & (HORIZON_AFD_POLL_CONNECT | HORIZON_AFD_POLL_CONNECT_ERR)))
            {
                struct sockaddr_in peer;
                socklen_t peer_len = sizeof(peer);

                if (!getpeername( o->file_fd, (struct sockaddr *)&peer, &peer_len ))
                {
                    bits |= o->sock_event_mask & (HORIZON_AFD_POLL_CONNECT | HORIZON_AFD_POLL_WRITE);
                    o->sock_err_ticks = 0;
                    horizon_trace( "[server] poller fd=%d connect completed (getpeername)\n", o->file_fd );
                }
            }

            pfd.fd = o->file_fd;
            pfd.events = 0;
            pfd.revents = 0;
            if (o->sock_event_mask & (HORIZON_AFD_POLL_READ | HORIZON_AFD_POLL_ACCEPT)) pfd.events |= POLLIN;
            if (o->sock_event_mask & (HORIZON_AFD_POLL_WRITE | HORIZON_AFD_POLL_CONNECT)) pfd.events |= POLLOUT;
            if (o->sock_event_mask & HORIZON_AFD_POLL_OOB) pfd.events |= POLLPRI;
            if (poll( &pfd, 1, 0 ) <= 0)
            {
                int new_connect = bits & ~o->sock_pending_events;
                if (new_connect)
                {
                    o->sock_pending_events |= new_connect;
                    if (o->sock_event_handle)
                        horizon_server_signal_object_locked( o->sock_event_handle );
                }
                continue;
            }

            if (pfd.revents & POLLIN)
                bits |= o->sock_event_mask & (HORIZON_AFD_POLL_READ | HORIZON_AFD_POLL_ACCEPT);
            if (pfd.revents & POLLOUT)
                bits |= o->sock_event_mask & (HORIZON_AFD_POLL_WRITE | HORIZON_AFD_POLL_CONNECT);
            if (pfd.revents & POLLPRI)
                bits |= o->sock_event_mask & HORIZON_AFD_POLL_OOB;
            if (pfd.revents & POLLOUT) o->sock_err_ticks = 0;
            if (pfd.revents & (POLLHUP | POLLERR))
            {
                int err = 0;
                socklen_t err_len = sizeof(err);

                getsockopt( o->file_fd, SOL_SOCKET, SO_ERROR, &err, &err_len );
                if (err)
                {
                    /* Horizon's bsd service can report POLLHUP plus a stale
                     * SO_ERROR while a nonblocking connect is still in
                     * flight; require the error to persist a few ticks before
                     * declaring the connect dead so real completions win. */
                    o->sock_err_ticks++;
                    horizon_trace( "[server] poller fd=%d revents=%#x so_error=%d ticks=%d\n",
                                   o->file_fd, pfd.revents, err, o->sock_err_ticks );
                    if (o->sock_err_ticks >= 3)
                    {
                        o->sock_connect_status = (int)horizon_sock_errno_status( err );
                        bits |= o->sock_event_mask & (HORIZON_AFD_POLL_CONNECT_ERR | HORIZON_AFD_POLL_RESET);
                    }
                }
                else if ((pfd.revents & POLLHUP) && !(pfd.revents & (POLLIN | POLLOUT)) &&
                         (o->sock_event_mask & HORIZON_AFD_POLL_CONNECT))
                {
                    /* Horizon's bsd service reports POLLHUP for sockets that
                     * are merely not connected yet; with no error and no data
                     * this is just an in-flight connect, not a hangup. */
                    o->sock_err_ticks = 0;
                }
                else if (pfd.revents & POLLHUP)
                    bits |= o->sock_event_mask & (HORIZON_AFD_POLL_HUP | HORIZON_AFD_POLL_CLOSE);
            }

            new_bits = bits & ~o->sock_pending_events;
            if (new_bits)
            {
                o->sock_pending_events |= new_bits;
                horizon_trace( "[server] poller fd=%d revents=%#x -> events=%#x (pending=%#x)\n",
                               o->file_fd, pfd.revents, new_bits, o->sock_pending_events );
                if (o->sock_event_handle)
                    horizon_server_signal_object_locked( o->sock_event_handle );
            }
        }
        pthread_mutex_unlock( &horizon_server_objects_mutex );
    }
    return NULL;
}

static void horizon_sock_poller_start(void)
{
    if (horizon_sock_poller_running) return;
    if (!pthread_create( &horizon_sock_poller_thread_id, NULL, horizon_sock_poller_thread, NULL ))
    {
        pthread_detach( horizon_sock_poller_thread_id );
        horizon_sock_poller_running = 1;
    }
}

static unsigned int horizon_server_get_sock_fd( unsigned int handle, int *fd, int *nonblocking )
{
    struct horizon_server_object *object;
    unsigned int status;

    pthread_mutex_lock( &horizon_server_objects_mutex );
    status = horizon_server_find_sock_locked( handle, &object );
    if (!status && object->file_fd == -1) status = HORIZON_STATUS_INVALID_HANDLE;
    if (!status)
    {
        *fd = object->file_fd;
        if (nonblocking) *nonblocking = object->sock_nonblocking;
    }
    pthread_mutex_unlock( &horizon_server_objects_mutex );
    return status;
}

static int horizon_server_handle_open_file_object( struct horizon_server_connection *connection,
                                                   const unsigned char *message,
                                                   const unsigned char *data, unsigned int data_size )
{
    static const unsigned short afd_name[] = {'\\','D','e','v','i','c','e','\\','A','f','d'};
    const struct horizon_open_file_object_request *request = (const void *)message;
    struct horizon_open_file_object_reply reply;
    unsigned int chars = data_size / 2;
    int is_afd = 0;

    memset( &reply, 0, sizeof(reply) );

    /* accept "\Device\Afd" and "\Device\Afd\..." in any case */
    if (chars >= 11)
    {
        unsigned int i;
        is_afd = 1;
        for (i = 0; i < 11 && is_afd; i++)
        {
            unsigned short c;
            memcpy( &c, data + 2 * i, sizeof(c) );
            if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
            if (c != (afd_name[i] >= 'A' && afd_name[i] <= 'Z' ? afd_name[i] + 'a' - 'A' : afd_name[i]))
                is_afd = 0;
        }
        if (is_afd && chars > 11)
        {
            unsigned short c;
            memcpy( &c, data + 22, sizeof(c) );
            if (c != '\\') is_afd = 0;
        }
    }

    if (is_afd)
    {
        struct horizon_server_handle_entry *entry;

        pthread_mutex_lock( &horizon_server_objects_mutex );
        if ((entry = horizon_server_create_handle_locked( HORIZON_SERVER_OBJECT_SOCK )))
        {
            entry->object->file_access = request->access;
            entry->object->file_options = request->options;
            reply.handle = entry->handle;
        }
        else reply.header.error = HORIZON_STATUS_NO_MEMORY;
        pthread_mutex_unlock( &horizon_server_objects_mutex );
        horizon_trace( "[server] open \\Device\\Afd access=%#x options=%#x -> handle=%08x status=%08x\n",
                       request->access, request->options, reply.handle, reply.header.error );
    }
    else
    {
        reply.header.error = HORIZON_STATUS_OBJECT_NAME_NOT_FOUND;
        if (chars && chars < 120)
        {
            char ascii[128];
            unsigned int i;
            for (i = 0; i < chars; i++)
            {
                unsigned short c;
                memcpy( &c, data + 2 * i, sizeof(c) );
                ascii[i] = (c >= 0x20 && c < 0x7f) ? (char)c : '?';
            }
            ascii[chars] = 0;
            horizon_trace( "[server] open_file_object '%s' -> not found\n", ascii );
        }
    }

    return horizon_server_write_reply( connection->reply_fd, &reply, sizeof(reply), NULL, 0 );
}

static unsigned int horizon_sock_ioctl_create( unsigned int handle, const unsigned char *data,
                                               unsigned int data_size )
{
    struct horizon_server_object *object;
    int params[4]; /* family, type, protocol, flags (WS values) */
    unsigned int status;
    int fd, type, protocol;

    if (data_size < sizeof(params)) return HORIZON_STATUS_INVALID_PARAMETER;
    memcpy( params, data, sizeof(params) );

    if (params[0] != HORIZON_WS_AF_INET)
    {
        horizon_trace( "[server] WINE_CREATE family=%d unsupported\n", params[0] );
        return HORIZON_STATUS_NOT_SUPPORTED;
    }
    switch (params[1])
    {
        case 1: type = SOCK_STREAM; break;
        case 2: type = SOCK_DGRAM; break;
        default: return HORIZON_STATUS_NOT_SUPPORTED;
    }
    protocol = params[2]; /* 0 / IPPROTO_TCP / IPPROTO_UDP share values */

    if ((fd = socket( AF_INET, type, protocol )) == -1)
        return horizon_sock_errno_status( errno );
    fcntl( fd, F_SETFL, O_NONBLOCK );

    pthread_mutex_lock( &horizon_server_objects_mutex );
    status = horizon_server_find_sock_locked( handle, &object );
    if (!status)
    {
        if (object->file_fd != -1) close( object->file_fd );
        object->file_fd = fd;
        object->sock_nonblocking = 0;
    }
    pthread_mutex_unlock( &horizon_server_objects_mutex );

    if (status) close( fd );
    horizon_trace( "[server] WINE_CREATE handle=%08x family=%d type=%d proto=%d -> fd=%d status=%08x\n",
                   handle, params[0], params[1], params[2], fd, status );
    return status;
}

static unsigned int horizon_sock_ioctl_connect( unsigned int handle, const unsigned char *data,
                                                unsigned int data_size )
{
    struct sockaddr_in sa;
    unsigned int status;
    int addr_len, fd, nonblocking, ret;

    if (data_size < 8 + 16) return HORIZON_STATUS_INVALID_PARAMETER;
    memcpy( &addr_len, data, sizeof(addr_len) );
    if (8u + (unsigned int)addr_len > data_size) return HORIZON_STATUS_INVALID_PARAMETER;

    if ((status = horizon_server_get_sock_fd( handle, &fd, &nonblocking ))) return status;
    if ((status = horizon_ws_sockaddr_to_unix( data + 8, addr_len, &sa ))) return status;

    {
        const unsigned char *ip = (const unsigned char *)&sa.sin_addr;
        horizon_trace( "[server] connect target: fd=%d len=%u family=%u ip=%u.%u.%u.%u port=%u\n",
                       fd, sa.sin_len, sa.sin_family, ip[0], ip[1], ip[2], ip[3],
                       (unsigned)((((const unsigned char *)&sa.sin_port)[0] << 8) |
                                  ((const unsigned char *)&sa.sin_port)[1]) );
    }

    /* Horizon's bsd service never completes nonblocking connects in a way
     * poll/getpeername can observe; blocking connect (the only style Switch
     * homebrew uses) works.  Do the wait here — the client is blocked on
     * this request anyway and each Wine thread has its own server thread —
     * and hand back the final result; ws2_32 treats an immediate success
     * from a nonblocking connect as connected. */
    fcntl( fd, F_SETFL, 0 );
    ret = connect( fd, (struct sockaddr *)&sa, sizeof(sa) );
    status = ret ? horizon_sock_errno_status( errno ) : HORIZON_STATUS_SUCCESS;
    fcntl( fd, F_SETFL, O_NONBLOCK );

    horizon_trace( "[server] WINE_CONNECT handle=%08x fd=%d port=%u nonblocking=%d blocking-connect ret=%d errno=%d -> status=%08x\n",
                   handle, fd, (unsigned)((data[10] << 8) | data[11]), nonblocking, ret, ret ? errno : 0, status );
    return status;
}

static unsigned int horizon_sock_ioctl_poll( unsigned int handle, const unsigned char *data,
                                             unsigned int data_size, unsigned char *out,
                                             unsigned int out_max, unsigned int *out_size )
{
    long long timeout;
    unsigned int count, i, signaled = 0;
    struct pollfd pfds[64];
    struct { unsigned long long socket; int flags; int status; } entry;
    int timeout_ms, ret;

    (void)handle;
    *out_size = 0;
    if (data_size < 16) return HORIZON_STATUS_INVALID_PARAMETER;
    memcpy( &timeout, data, sizeof(timeout) );
    memcpy( &count, data + 8, sizeof(count) );
    if (!count || count > 64 || data_size < 16 + count * 16) return HORIZON_STATUS_INVALID_PARAMETER;
    if (out_max < 16 + count * 16) return HORIZON_STATUS_BUFFER_TOO_SMALL;

    for (i = 0; i < count; i++)
    {
        unsigned int status;
        int fd = -1;

        memcpy( &entry, data + 16 + i * 16, sizeof(entry) );
        status = horizon_server_get_sock_fd( (unsigned int)entry.socket, &fd, NULL );
        pfds[i].fd = status ? -1 : fd;
        pfds[i].events = 0;
        pfds[i].revents = 0;
        if (entry.flags & (HORIZON_AFD_POLL_READ | HORIZON_AFD_POLL_ACCEPT)) pfds[i].events |= POLLIN;
        if (entry.flags & (HORIZON_AFD_POLL_WRITE | HORIZON_AFD_POLL_CONNECT)) pfds[i].events |= POLLOUT;
        if (entry.flags & HORIZON_AFD_POLL_OOB) pfds[i].events |= POLLPRI;
    }

    if (timeout == 0x7fffffffffffffffLL) timeout_ms = -1;
    else if (timeout <= 0)
    {
        long long ms = (-timeout) / 10000;
        timeout_ms = ms > 0x7fffffff ? 0x7fffffff : (int)ms;
    }
    else
    {
        horizon_trace( "[server] AFD_POLL absolute timeout %lld not supported, polling once\n", timeout );
        timeout_ms = 0;
    }

    while ((ret = poll( pfds, count, timeout_ms )) == -1 && errno == EINTR) {}
    if (ret == -1) return horizon_sock_errno_status( errno );

    memcpy( out, data, 16 ); /* timeout/exclusive preserved */
    for (i = 0; i < count; i++)
    {
        int flags = 0;

        memcpy( &entry, data + 16 + i * 16, sizeof(entry) );
        if (pfds[i].fd == -1) flags = HORIZON_AFD_POLL_CLOSE;
        else
        {
            if (pfds[i].revents & POLLIN) flags |= entry.flags & (HORIZON_AFD_POLL_READ | HORIZON_AFD_POLL_ACCEPT);
            if (pfds[i].revents & POLLOUT) flags |= entry.flags & (HORIZON_AFD_POLL_WRITE | HORIZON_AFD_POLL_CONNECT);
            if (pfds[i].revents & POLLPRI) flags |= HORIZON_AFD_POLL_OOB;
            if (pfds[i].revents & POLLHUP) flags |= HORIZON_AFD_POLL_HUP;
            if (pfds[i].revents & POLLNVAL) flags |= HORIZON_AFD_POLL_CLOSE;
            if (pfds[i].revents & POLLERR)
            {
                int err = 0;
                socklen_t err_len = sizeof(err);
                getsockopt( pfds[i].fd, SOL_SOCKET, SO_ERROR, &err, &err_len );
                flags |= HORIZON_AFD_POLL_CONNECT_ERR;
                entry.status = (int)horizon_sock_errno_status( err );
            }
        }
        if (!flags) continue;
        entry.flags = flags;
        if (!(pfds[i].revents & POLLERR)) entry.status = 0;
        memcpy( out + 16 + signaled * 16, &entry, sizeof(entry) );
        signaled++;
    }
    memcpy( out + 8, &signaled, sizeof(signaled) );
    *out_size = 16 + signaled * 16;
    return HORIZON_STATUS_SUCCESS;
}

static int horizon_server_handle_ioctl( struct horizon_server_connection *connection,
                                        const unsigned char *message,
                                        const unsigned char *data, unsigned int data_size )
{
    const struct horizon_ioctl_request *request = (const void *)message;
    struct horizon_ioctl_reply reply;
    unsigned int handle = request->async.handle;
    unsigned char out[1056]; /* >= 16 + 64*16 for poll */
    unsigned int out_size = 0;
    unsigned int out_max = request->header.reply_size;
    unsigned int status;

    if (out_max > sizeof(out)) out_max = sizeof(out);
    memset( &reply, 0, sizeof(reply) );

    switch (request->code)
    {
    case HORIZON_IOCTL_AFD_WINE_CREATE:
        status = horizon_sock_ioctl_create( handle, data, data_size );
        break;

    case HORIZON_IOCTL_AFD_WINE_CONNECT:
        status = horizon_sock_ioctl_connect( handle, data, data_size );
        break;

    case HORIZON_IOCTL_AFD_POLL:
        status = horizon_sock_ioctl_poll( handle, data, data_size, out, out_max, &out_size );
        break;

    case HORIZON_IOCTL_AFD_GETSOCKNAME:
    case HORIZON_IOCTL_AFD_WINE_GETPEERNAME:
    {
        struct sockaddr_in sa;
        socklen_t sa_len = sizeof(sa);
        int fd, ret;

        if ((status = horizon_server_get_sock_fd( handle, &fd, NULL ))) break;
        if (request->code == HORIZON_IOCTL_AFD_GETSOCKNAME)
            ret = getsockname( fd, (struct sockaddr *)&sa, &sa_len );
        else
            ret = getpeername( fd, (struct sockaddr *)&sa, &sa_len );
        if (ret == -1) { status = horizon_sock_errno_status( errno ); break; }
        if (!(out_size = horizon_ws_sockaddr_from_unix( &sa, out, out_max )))
            status = HORIZON_STATUS_BUFFER_TOO_SMALL;
        else
            status = HORIZON_STATUS_SUCCESS;
        break;
    }

    case HORIZON_IOCTL_AFD_WINE_GET_SO_ERROR:
    {
        int fd, err = 0;
        socklen_t err_len = sizeof(err);
        unsigned int wsa_err;

        if ((status = horizon_server_get_sock_fd( handle, &fd, NULL ))) break;
        if (getsockopt( fd, SOL_SOCKET, SO_ERROR, &err, &err_len ) == -1)
        {
            status = horizon_sock_errno_status( errno );
            break;
        }
        wsa_err = horizon_sock_errno_wsa( err );
        if (out_max < sizeof(wsa_err)) { status = HORIZON_STATUS_BUFFER_TOO_SMALL; break; }
        memcpy( out, &wsa_err, sizeof(wsa_err) );
        out_size = sizeof(wsa_err);
        status = HORIZON_STATUS_SUCCESS;
        break;
    }

    case HORIZON_IOCTL_AFD_WINE_SHUTDOWN:
    {
        int fd, how;

        if (data_size < sizeof(how)) { status = HORIZON_STATUS_INVALID_PARAMETER; break; }
        memcpy( &how, data, sizeof(how) );
        if ((status = horizon_server_get_sock_fd( handle, &fd, NULL ))) break;
        if (shutdown( fd, how ) == -1) status = horizon_sock_errno_status( errno );
        else status = HORIZON_STATUS_SUCCESS;
        break;
    }

    case HORIZON_IOCTL_AFD_WINE_FIONBIO:
    {
        struct horizon_server_object *object;
        int on;

        if (data_size < sizeof(on)) { status = HORIZON_STATUS_INVALID_PARAMETER; break; }
        memcpy( &on, data, sizeof(on) );
        pthread_mutex_lock( &horizon_server_objects_mutex );
        status = horizon_server_find_sock_locked( handle, &object );
        if (!status) object->sock_nonblocking = !!on;
        pthread_mutex_unlock( &horizon_server_objects_mutex );
        horizon_trace( "[server] WINE_FIONBIO handle=%08x on=%d status=%08x\n", handle, on, status );
        break;
    }

    case HORIZON_IOCTL_AFD_EVENT_SELECT:
    {
        struct horizon_server_object *object;
        unsigned long long event;
        int mask;

        if (data_size < 12) { status = HORIZON_STATUS_INVALID_PARAMETER; break; }
        memcpy( &event, data, sizeof(event) );
        memcpy( &mask, data + 8, sizeof(mask) );
        pthread_mutex_lock( &horizon_server_objects_mutex );
        status = horizon_server_find_sock_locked( handle, &object );
        if (!status)
        {
            object->sock_event_handle = (unsigned int)event;
            object->sock_event_mask = mask;
            object->sock_pending_events = 0;
            object->sock_connect_status = 0;
            object->sock_nonblocking = 1; /* WSAEventSelect implies nonblocking */
        }
        pthread_mutex_unlock( &horizon_server_objects_mutex );
        if (!status && mask) horizon_sock_poller_start();
        horizon_trace( "[server] EVENT_SELECT handle=%08x event=%08x mask=%#x status=%08x\n",
                       handle, (unsigned int)event, mask, status );
        break;
    }

    default:
        horizon_trace( "[server] unimplemented ioctl code=%#x handle=%08x in=%u out=%u\n",
                       request->code, handle, data_size, request->header.reply_size );
        status = HORIZON_STATUS_NOT_IMPLEMENTED;
        break;
    }

    reply.header.error = status;
    reply.header.reply_size = out_size;
    reply.wait = 0;
    reply.options = 0;
    return horizon_server_write_reply( connection->reply_fd, &reply, sizeof(reply),
                                       out_size ? out : NULL, out_size );
}

static int horizon_server_handle_socket_get_events( struct horizon_server_connection *connection,
                                                    const unsigned char *message )
{
    const struct horizon_socket_get_events_request *request = (const void *)message;
    struct horizon_socket_get_events_reply reply;
    struct horizon_server_object *object;
    int status_array[13];
    unsigned int out_size = 0, out_max = request->header.reply_size;

    memset( &reply, 0, sizeof(reply) );
    memset( status_array, 0, sizeof(status_array) );

    pthread_mutex_lock( &horizon_server_objects_mutex );
    reply.header.error = horizon_server_find_sock_locked( request->handle, &object );
    if (!reply.header.error)
    {
        reply.flags = object->sock_pending_events;
        status_array[8] = object->sock_connect_status; /* AFD_POLL_BIT_CONNECT_ERR */
        object->sock_pending_events = 0;
        if (request->event)
        {
            struct horizon_server_handle_entry *event_entry =
                horizon_server_find_handle_locked( request->event );
            if (event_entry && event_entry->object->type == HORIZON_SERVER_OBJECT_EVENT)
                event_entry->object->signaled = 0;
        }
    }
    pthread_mutex_unlock( &horizon_server_objects_mutex );

    if (!reply.header.error)
    {
        out_size = sizeof(status_array);
        if (out_size > out_max) out_size = out_max;
        reply.header.reply_size = out_size;
    }
    horizon_trace( "[server] GET_EVENTS handle=%08x -> flags=%#x status=%08x\n",
                   request->handle, reply.flags, reply.header.error );
    return horizon_server_write_reply( connection->reply_fd, &reply, sizeof(reply),
                                       out_size ? status_array : NULL, out_size );
}

static int horizon_server_handle_socket_io( struct horizon_server_connection *connection,
                                            const unsigned char *message )
{
    struct horizon_socket_io_reply reply;

    (void)message;
    memset( &reply, 0, sizeof(reply) );
    /* ALERTED tells ntdll's socket.c to do the recvmsg/sendmsg itself on
     * the cached fd.  The wait token is a placeholder consumed only by
     * set_async_direct_result below; nonblocking makes EAGAIN surface as
     * WSAEWOULDBLOCK instead of waiting for an async that will never run. */
    reply.header.error = HORIZON_STATUS_ALERTED;
    reply.wait = 1;
    reply.options = 0;
    reply.nonblocking = 1;
    return horizon_server_write_reply( connection->reply_fd, &reply, sizeof(reply), NULL, 0 );
}

static int horizon_server_handle_set_async_direct_result( struct horizon_server_connection *connection,
                                                          const unsigned char *message )
{
    struct horizon_set_async_direct_result_reply reply;

    (void)message;
    memset( &reply, 0, sizeof(reply) );
    reply.handle = 0; /* nothing pending: caller skips wait_async */
    return horizon_server_write_reply( connection->reply_fd, &reply, sizeof(reply), NULL, 0 );
}

#ifdef __SWITCH__
/* Was fopen(append)+vfprintf+fclose on every single call, unconditionally,
 * across all ~50 call sites in this file -- a full filesystem open/close
 * cycle every time, not just the fflush()-on-an-already-open-handle cost
 * this whole session has already treated as the (accepted, cheap-enough)
 * baseline everywhere else. This was the third instance of "diagnostic
 * logging has hidden I/O cost" found tonight (after unconditional syscall
 * tracing, then unconditional paint-phase tracing), and by far the most
 * expensive one: it single-handedly accounted for the bulk of what this
 * whole session had been calling an "architectural floor" in
 * redraw_window/get_paint_regions, a claim that's now retracted -- see
 * the git history for the hardware numbers.
 *
 * Fixed at the source rather than by rate-limiting all ~50 call sites
 * individually: opens the log file once (lazily, on first use) and keeps
 * the handle open for the life of the process, matching the same
 * open-once-fflush-per-line convention wine-nx-probe/source/runtime.c's
 * log_line() already uses. Removes the expensive open/close cycle
 * everywhere at once; the four confirmed-hottest call sites (redraw_
 * window, get_paint_regions, get_update_flags_ex, redraw_window_
 * updatenow) additionally keep their own rate-limiting on top, since even
 * a cheap fflush() per call adds up at hundreds of calls/second -- same
 * two-layer treatment (remove the expensive part at the source, then
 * rate-limit the truly hot call sites) already used for switch_paint_
 * trace() and nxdrv_trace_hot() elsewhere in this port. */
void horizon_trace( const char *fmt, ... )
{
    static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
    static FILE *f;
    __builtin_va_list args;

    pthread_mutex_lock( &lock );
    if (!f) f = fopen( "sdmc:/switch/wine/horizon-trace.log", "a" );
    if (f)
    {
        __builtin_va_start( args, fmt );
        vfprintf( f, fmt, args );
        __builtin_va_end( args );
        fputc( '\n', f );
        fflush( f );
    }
    pthread_mutex_unlock( &lock );
}

void horizon_get_address_space_limits( void **start, void **limit )
{
    u64 base = 0, size = 0;
    Result rc_base, rc_size;

    rc_base = svcGetInfo( &base, InfoType_AslrRegionAddress, CUR_PROCESS_HANDLE, 0 );
    rc_size = svcGetInfo( &size, InfoType_AslrRegionSize, CUR_PROCESS_HANDLE, 0 );
    if (R_SUCCEEDED(rc_base) && R_SUCCEEDED(rc_size) && size && base + size > base)
    {
        *start = (void *)base;
        *limit = (void *)(base + size);
        horizon_trace( "[VA] Horizon ASLR region base=0x%llx size=0x%llx limit=0x%llx",
                       (unsigned long long)base, (unsigned long long)size,
                       (unsigned long long)(base + size) );
        return;
    }

    *start = (void *)0x08000000ULL;
    *limit = (void *)0x8000000000ULL;
    horizon_trace( "[VA] Horizon ASLR query failed base_rc=0x%x size_rc=0x%x; fallback base=%p limit=%p",
                   rc_base, rc_size, *start, *limit );
}
#endif

static int horizon_server_handle_create_mapping( struct horizon_server_connection *connection,
                                                 const unsigned char *message )
{
    const struct horizon_create_mapping_request *request = (const void *)message;
    struct horizon_create_mapping_reply reply;
    struct horizon_server_handle_entry *file_entry;
    struct horizon_server_handle_entry *mapping_entry = NULL;
    struct horizon_pe_image_info image_info;
    unsigned long long mapping_size = request->size;
    char *mapping_name = NULL;
    int fd = -1;
#ifdef __SWITCH__
    int dbg_has_image = -1;
#endif

    memset( &reply, 0, sizeof(reply) );
    memset( &image_info, 0, sizeof(image_info) );

    pthread_mutex_lock( &horizon_server_objects_mutex );
    file_entry = horizon_server_find_handle_locked( request->file_handle );
    if (!file_entry) reply.header.error = HORIZON_STATUS_INVALID_HANDLE;
    else if (file_entry->object->type != HORIZON_SERVER_OBJECT_FILE || file_entry->object->file_fd == -1)
        reply.header.error = HORIZON_STATUS_OBJECT_TYPE_MISMATCH;
    else if ((fd = dup( file_entry->object->file_fd )) == -1)
        reply.header.error = horizon_server_errno_status( errno );
    else if (file_entry->object->file_name && !(mapping_name = strdup( file_entry->object->file_name )))
        reply.header.error = HORIZON_STATUS_NO_MEMORY;
    pthread_mutex_unlock( &horizon_server_objects_mutex );

    if (!reply.header.error && (request->flags & HORIZON_SEC_IMAGE))
    {
        reply.header.error = horizon_server_read_pe_image_info( fd, &image_info );
        mapping_size = image_info.map_size;
#ifdef __SWITCH__
        horizon_trace( "[HZ] create_mapping flags=0x%x SEC_IMAGE=1 read_pe=0x%x machine=0x%x map_size=0x%x name=%s",
                       request->flags, reply.header.error, image_info.machine,
                       image_info.map_size, mapping_name ? mapping_name : "(null)" );
#endif
    }
    else if (!reply.header.error && !mapping_size)
    {
        struct stat st;

        if (fstat( fd, &st ) == -1) reply.header.error = horizon_server_errno_status( errno );
        else mapping_size = st.st_size;
    }

    if (!reply.header.error)
    {
        pthread_mutex_lock( &horizon_server_objects_mutex );
        if ((mapping_entry = horizon_server_create_handle_locked( HORIZON_SERVER_OBJECT_MAPPING )))
        {
            mapping_entry->object->file_fd = fd;
            mapping_entry->object->file_name = mapping_name;
            mapping_entry->object->file_access = request->file_access;
            mapping_entry->object->file_options = 0;
            mapping_entry->object->mapping_flags = request->flags;
            mapping_entry->object->mapping_access = request->access;
            mapping_entry->object->mapping_file_access = request->file_access;
            mapping_entry->object->mapping_size = mapping_size;
            mapping_entry->object->mapping_has_image = !!(request->flags & HORIZON_SEC_IMAGE);
            mapping_entry->object->mapping_image = image_info;
            reply.handle = mapping_entry->handle;
#ifdef __SWITCH__
            dbg_has_image = mapping_entry->object->mapping_has_image;
#endif
            mapping_name = NULL;
            fd = -1;
        }
        else reply.header.error = HORIZON_STATUS_NO_MEMORY;
        pthread_mutex_unlock( &horizon_server_objects_mutex );
    }

#ifdef __SWITCH__
    horizon_trace( "[HZ] create_mapping done err=0x%x flags=0x%x has_image=%d handle=0x%x",
                   reply.header.error, request->flags, dbg_has_image, reply.handle );
#endif

    if (fd != -1) close( fd );
    free( mapping_name );
    return horizon_server_write_reply( connection->reply_fd, &reply, sizeof(reply), NULL, 0 );
}

static int horizon_server_handle_get_mapping_info( struct horizon_server_connection *connection,
                                                   const unsigned char *message )
{
    const struct horizon_get_mapping_info_request *request = (const void *)message;
    struct horizon_get_mapping_info_reply reply;
    struct horizon_server_handle_entry *entry;
    struct horizon_pe_image_info image_info;
    unsigned char *data = NULL;
    unsigned int data_size = 0;
    int ret;
#ifdef __SWITCH__
    int dbg_found = 0, dbg_type = -1, dbg_has_image = -1;
    unsigned int dbg_flags = 0, dbg_machine = 0;
#endif

    memset( &reply, 0, sizeof(reply) );
    memset( &image_info, 0, sizeof(image_info) );

    pthread_mutex_lock( &horizon_server_objects_mutex );
    entry = horizon_server_find_handle_locked( request->handle );
    if (!entry) reply.header.error = HORIZON_STATUS_INVALID_HANDLE;
    else if (entry->object->type != HORIZON_SERVER_OBJECT_MAPPING)
        reply.header.error = HORIZON_STATUS_OBJECT_TYPE_MISMATCH;
    else
    {
        reply.size = entry->object->mapping_size;
        reply.flags = entry->object->mapping_flags;
        reply.shared_file = 0;
#ifdef __SWITCH__
        dbg_found = 1;
        dbg_type = entry->object->type;
        dbg_has_image = entry->object->mapping_has_image;
        dbg_flags = entry->object->mapping_flags;
        dbg_machine = entry->object->mapping_image.machine;
#endif
        if (entry->object->mapping_has_image)
        {
            image_info = entry->object->mapping_image;
            reply.name_len = horizon_server_utf16_name_len( entry->object->file_name );
            data_size = sizeof(image_info) + reply.name_len;
            if ((data = malloc( data_size )))
            {
                memcpy( data, &image_info, sizeof(image_info) );
                horizon_server_write_utf16_name( data + sizeof(image_info), entry->object->file_name );
                reply.total = data_size;
                /* wineserver semantics: never send more reply data than the
                 * client's wine_server_set_reply() buffer. Overrunning it
                 * corrupts the client and desyncs the reply stream. */
                if (request->header.reply_size && data_size > request->header.reply_size)
                    data_size = request->header.reply_size;
                reply.header.reply_size = data_size;
            }
            else
            {
                data_size = 0;
                reply.name_len = 0;
                reply.header.error = HORIZON_STATUS_NO_MEMORY;
            }
        }
    }
    pthread_mutex_unlock( &horizon_server_objects_mutex );

#ifdef __SWITCH__
    horizon_trace( "[HZ] get_mapping_info found=%d type=%d has_image=%d flags=0x%x machine=0x%x "
                   "req_reply_max=%u sent=%u total=%u reply_size=%u name_len=%u err=0x%x "
                   "sizeof(reply)=%u sizeof(img)=%u",
                   dbg_found, dbg_type, dbg_has_image, dbg_flags, dbg_machine,
                   request->header.reply_size, data_size, reply.total,
                   reply.header.reply_size, reply.name_len,
                   reply.header.error, (unsigned)sizeof(reply), (unsigned)sizeof(image_info) );
#endif

    ret = horizon_server_write_reply( connection->reply_fd, &reply, sizeof(reply), data, data_size );
    free( data );
    return ret;
}

static int horizon_server_handle_get_image_map_address( struct horizon_server_connection *connection,
                                                        const unsigned char *message )
{
    const struct horizon_get_image_map_address_request *request = (const void *)message;
    struct horizon_get_image_map_address_reply reply;
    struct horizon_server_handle_entry *entry;

    memset( &reply, 0, sizeof(reply) );
    pthread_mutex_lock( &horizon_server_objects_mutex );
    entry = horizon_server_find_handle_locked( request->handle );
    if (!entry) reply.header.error = HORIZON_STATUS_INVALID_HANDLE;
    else if (entry->object->type != HORIZON_SERVER_OBJECT_MAPPING || !entry->object->mapping_has_image)
        reply.header.error = HORIZON_STATUS_OBJECT_TYPE_MISMATCH;
    else reply.addr = entry->object->mapping_image.map_addr;
    pthread_mutex_unlock( &horizon_server_objects_mutex );

    return horizon_server_write_reply( connection->reply_fd, &reply, sizeof(reply), NULL, 0 );
}

static int horizon_server_handle_map_image_view( struct horizon_server_connection *connection,
                                                 const unsigned char *message )
{
    const struct horizon_map_image_view_request *request = (const void *)message;
    struct horizon_server_handle_entry *entry;
    unsigned int status = HORIZON_STATUS_SUCCESS;

    pthread_mutex_lock( &horizon_server_objects_mutex );
    entry = horizon_server_find_handle_locked( request->mapping );
    if (!entry) status = HORIZON_STATUS_INVALID_HANDLE;
    else if (entry->object->type != HORIZON_SERVER_OBJECT_MAPPING || !entry->object->mapping_has_image)
        status = HORIZON_STATUS_OBJECT_TYPE_MISMATCH;
    else
    {
        entry->object->mapping_image.map_addr = request->base;
        entry->object->mapping_image.base = request->base;
        entry->object->mapping_image.map_size = request->size;
        entry->object->mapping_image.entry_point = request->entry;
        entry->object->mapping_image.machine = request->machine;
    }
    pthread_mutex_unlock( &horizon_server_objects_mutex );

    return horizon_server_write_status( connection->reply_fd, status );
}

static int horizon_server_handle_map_view( struct horizon_server_connection *connection,
                                           const unsigned char *message )
{
    const struct horizon_map_view_request *request = (const void *)message;
    struct horizon_server_handle_entry *entry;
    unsigned int status = HORIZON_STATUS_SUCCESS;

    pthread_mutex_lock( &horizon_server_objects_mutex );
    entry = horizon_server_find_handle_locked( request->mapping );
    if (!entry) status = HORIZON_STATUS_INVALID_HANDLE;
    else if (entry->object->type != HORIZON_SERVER_OBJECT_MAPPING)
        status = HORIZON_STATUS_OBJECT_TYPE_MISMATCH;
    else if (entry->object->mapping_is_session)
        status = horizon_server_note_session_view_locked( request->base, request->start, request->size );
    pthread_mutex_unlock( &horizon_server_objects_mutex );

    return horizon_server_write_status( connection->reply_fd, status );
}

static int horizon_server_handle_unmap_view( struct horizon_server_connection *connection,
                                             const unsigned char *message )
{
    const struct horizon_unmap_view_request *request = (const void *)message;

    pthread_mutex_lock( &horizon_server_objects_mutex );
    horizon_server_remove_session_view_locked( request->base );
    pthread_mutex_unlock( &horizon_server_objects_mutex );
    return horizon_server_write_status( connection->reply_fd, HORIZON_STATUS_SUCCESS );
}

static int horizon_server_handle_create_event( struct horizon_server_connection *connection,
                                               const unsigned char *message,
                                               const unsigned char *data, unsigned int data_size )
{
    const struct horizon_create_event_request *request = (const void *)message;
    struct horizon_create_event_reply reply;
    struct horizon_server_handle_entry *entry;
    struct horizon_object_name name;

    memset( &reply, 0, sizeof(reply) );
    reply.header.error = horizon_server_parse_object_attributes( data, data_size, &name );
    if (reply.header.error == HORIZON_STATUS_SUCCESS)
    {
        pthread_mutex_lock( &horizon_server_objects_mutex );
        reply.header.error = horizon_server_create_named_object_handle_locked(
            HORIZON_SERVER_OBJECT_EVENT, &name, &entry );
        if (entry)
        {
            if (reply.header.error == HORIZON_STATUS_SUCCESS)
            {
                entry->object->manual_reset = !!request->manual_reset;
                entry->object->signaled = !!request->initial_state;
            }
            reply.handle = entry->handle;
        }
        pthread_mutex_unlock( &horizon_server_objects_mutex );
    }
    return horizon_server_write_reply( connection->reply_fd, &reply, sizeof(reply), NULL, 0 );
}

static int horizon_server_handle_event_op( struct horizon_server_connection *connection,
                                           const unsigned char *message )
{
    const struct horizon_event_op_request *request = (const void *)message;
    struct horizon_event_op_reply reply;
    struct horizon_server_object *object = NULL;
    unsigned int status;

    memset( &reply, 0, sizeof(reply) );
    pthread_mutex_lock( &horizon_server_objects_mutex );
    status = horizon_server_find_typed_object_locked( request->handle, HORIZON_SERVER_OBJECT_EVENT, &object );
    if (status == HORIZON_STATUS_SUCCESS)
    {
        reply.state = object->signaled;
        switch (request->op)
        {
        case HORIZON_PULSE_EVENT:
            object->signaled = 0;
            break;
        case HORIZON_SET_EVENT:
            object->signaled = 1;
            break;
        case HORIZON_RESET_EVENT:
            object->signaled = 0;
            break;
        default:
            status = HORIZON_STATUS_INVALID_PARAMETER;
            break;
        }
    }
    pthread_mutex_unlock( &horizon_server_objects_mutex );

    reply.header.error = status;
    return horizon_server_write_reply( connection->reply_fd, &reply, sizeof(reply), NULL, 0 );
}

static int horizon_server_handle_query_event( struct horizon_server_connection *connection,
                                              const unsigned char *message )
{
    const struct horizon_query_event_request *request = (const void *)message;
    struct horizon_query_event_reply reply;
    struct horizon_server_object *object = NULL;
    unsigned int status;

    memset( &reply, 0, sizeof(reply) );
    pthread_mutex_lock( &horizon_server_objects_mutex );
    status = horizon_server_find_typed_object_locked( request->handle, HORIZON_SERVER_OBJECT_EVENT, &object );
    if (status == HORIZON_STATUS_SUCCESS)
    {
        reply.manual_reset = object->manual_reset;
        reply.state = object->signaled;
    }
    pthread_mutex_unlock( &horizon_server_objects_mutex );

    reply.header.error = status;
    return horizon_server_write_reply( connection->reply_fd, &reply, sizeof(reply), NULL, 0 );
}

static int horizon_server_handle_create_keyed_event( struct horizon_server_connection *connection,
                                                     const unsigned char *data, unsigned int data_size )
{
    struct horizon_open_process_reply reply;
    struct horizon_server_handle_entry *entry;
    struct horizon_object_name name;

    memset( &reply, 0, sizeof(reply) );
    reply.header.error = horizon_server_parse_object_attributes( data, data_size, &name );
    if (reply.header.error == HORIZON_STATUS_SUCCESS)
    {
        pthread_mutex_lock( &horizon_server_objects_mutex );
        reply.header.error = horizon_server_create_named_object_handle_locked(
            HORIZON_SERVER_OBJECT_KEYED_EVENT, &name, &entry );
        if (entry) reply.handle = entry->handle;
        pthread_mutex_unlock( &horizon_server_objects_mutex );
    }
    return horizon_server_write_reply( connection->reply_fd, &reply, sizeof(reply), NULL, 0 );
}

static int horizon_server_handle_create_mutex( struct horizon_server_connection *connection,
                                               const unsigned char *message,
                                               const unsigned char *data, unsigned int data_size )
{
    const struct horizon_create_mutex_request *request = (const void *)message;
    struct horizon_create_mutex_reply reply;
    struct horizon_server_handle_entry *entry;
    struct horizon_object_name name;

    memset( &reply, 0, sizeof(reply) );
    reply.header.error = horizon_server_parse_object_attributes( data, data_size, &name );
    if (reply.header.error == HORIZON_STATUS_SUCCESS)
    {
        pthread_mutex_lock( &horizon_server_objects_mutex );
        reply.header.error = horizon_server_create_named_object_handle_locked(
            HORIZON_SERVER_OBJECT_MUTEX, &name, &entry );
        if (entry)
        {
            if (reply.header.error == HORIZON_STATUS_SUCCESS)
            {
                entry->object->owned = !!request->owned;
                entry->object->count = request->owned ? 1 : 0;
            }
            reply.handle = entry->handle;
        }
        pthread_mutex_unlock( &horizon_server_objects_mutex );
    }
    return horizon_server_write_reply( connection->reply_fd, &reply, sizeof(reply), NULL, 0 );
}

static int horizon_server_handle_release_mutex( struct horizon_server_connection *connection,
                                                const unsigned char *message )
{
    const struct horizon_release_mutex_request *request = (const void *)message;
    struct horizon_release_mutex_reply reply;
    struct horizon_server_object *object = NULL;
    unsigned int status;

    memset( &reply, 0, sizeof(reply) );
    pthread_mutex_lock( &horizon_server_objects_mutex );
    status = horizon_server_find_typed_object_locked( request->handle, HORIZON_SERVER_OBJECT_MUTEX, &object );
    if (status == HORIZON_STATUS_SUCCESS)
    {
        if (!object->owned || !object->count) status = HORIZON_STATUS_MUTANT_NOT_OWNED;
        else
        {
            reply.prev_count = object->count;
            if (!--object->count) object->owned = 0;
        }
    }
    pthread_mutex_unlock( &horizon_server_objects_mutex );

    reply.header.error = status;
    return horizon_server_write_reply( connection->reply_fd, &reply, sizeof(reply), NULL, 0 );
}

static int horizon_server_handle_query_mutex( struct horizon_server_connection *connection,
                                              const unsigned char *message )
{
    const struct horizon_query_mutex_request *request = (const void *)message;
    struct horizon_query_mutex_reply reply;
    struct horizon_server_object *object = NULL;
    unsigned int status;

    memset( &reply, 0, sizeof(reply) );
    pthread_mutex_lock( &horizon_server_objects_mutex );
    status = horizon_server_find_typed_object_locked( request->handle, HORIZON_SERVER_OBJECT_MUTEX, &object );
    if (status == HORIZON_STATUS_SUCCESS)
    {
        reply.count = object->count;
        reply.owned = object->owned;
        reply.abandoned = 0;
    }
    pthread_mutex_unlock( &horizon_server_objects_mutex );

    reply.header.error = status;
    return horizon_server_write_reply( connection->reply_fd, &reply, sizeof(reply), NULL, 0 );
}

static int horizon_server_handle_create_semaphore( struct horizon_server_connection *connection,
                                                   const unsigned char *message,
                                                   const unsigned char *data, unsigned int data_size )
{
    const struct horizon_create_semaphore_request *request = (const void *)message;
    struct horizon_create_semaphore_reply reply;
    struct horizon_server_handle_entry *entry;
    struct horizon_object_name name;

    memset( &reply, 0, sizeof(reply) );
    if (!request->max || request->initial > request->max)
        reply.header.error = HORIZON_STATUS_INVALID_PARAMETER;
    else if ((reply.header.error = horizon_server_parse_object_attributes( data, data_size, &name )))
    {
    }
    else
    {
        pthread_mutex_lock( &horizon_server_objects_mutex );
        reply.header.error = horizon_server_create_named_object_handle_locked(
            HORIZON_SERVER_OBJECT_SEMAPHORE, &name, &entry );
        if (entry)
        {
            if (reply.header.error == HORIZON_STATUS_SUCCESS)
            {
                entry->object->count = request->initial;
                entry->object->max = request->max;
            }
            reply.handle = entry->handle;
        }
        pthread_mutex_unlock( &horizon_server_objects_mutex );
    }
    return horizon_server_write_reply( connection->reply_fd, &reply, sizeof(reply), NULL, 0 );
}

static int horizon_server_handle_release_semaphore( struct horizon_server_connection *connection,
                                                    const unsigned char *message )
{
    const struct horizon_release_semaphore_request *request = (const void *)message;
    struct horizon_release_semaphore_reply reply;
    struct horizon_server_object *object = NULL;
    unsigned int status;

    memset( &reply, 0, sizeof(reply) );
    pthread_mutex_lock( &horizon_server_objects_mutex );
    status = horizon_server_find_typed_object_locked( request->handle, HORIZON_SERVER_OBJECT_SEMAPHORE, &object );
    if (status == HORIZON_STATUS_SUCCESS)
    {
        if (!request->count) status = HORIZON_STATUS_INVALID_PARAMETER;
        else if (request->count > object->max || object->count > object->max - request->count)
            status = HORIZON_STATUS_SEMAPHORE_LIMIT_EXCEEDED;
        else
        {
            reply.prev_count = object->count;
            object->count += request->count;
        }
    }
    pthread_mutex_unlock( &horizon_server_objects_mutex );

    reply.header.error = status;
    return horizon_server_write_reply( connection->reply_fd, &reply, sizeof(reply), NULL, 0 );
}

static int horizon_server_handle_query_semaphore( struct horizon_server_connection *connection,
                                                  const unsigned char *message )
{
    const struct horizon_query_semaphore_request *request = (const void *)message;
    struct horizon_query_semaphore_reply reply;
    struct horizon_server_object *object = NULL;
    unsigned int status;

    memset( &reply, 0, sizeof(reply) );
    pthread_mutex_lock( &horizon_server_objects_mutex );
    status = horizon_server_find_typed_object_locked( request->handle, HORIZON_SERVER_OBJECT_SEMAPHORE, &object );
    if (status == HORIZON_STATUS_SUCCESS)
    {
        reply.current = object->count;
        reply.max = object->max;
    }
    pthread_mutex_unlock( &horizon_server_objects_mutex );

    reply.header.error = status;
    return horizon_server_write_reply( connection->reply_fd, &reply, sizeof(reply), NULL, 0 );
}

static int horizon_server_handle_create_timer( struct horizon_server_connection *connection,
                                               const unsigned char *message,
                                               const unsigned char *data, unsigned int data_size )
{
    const struct horizon_create_timer_request *request = (const void *)message;
    struct horizon_create_timer_reply reply;
    struct horizon_server_handle_entry *entry;
    struct horizon_object_name name;

    memset( &reply, 0, sizeof(reply) );
    reply.header.error = horizon_server_parse_object_attributes( data, data_size, &name );
    if (reply.header.error == HORIZON_STATUS_SUCCESS)
    {
        pthread_mutex_lock( &horizon_server_objects_mutex );
        reply.header.error = horizon_server_create_named_object_handle_locked(
            HORIZON_SERVER_OBJECT_TIMER, &name, &entry );
        if (entry)
        {
            if (reply.header.error == HORIZON_STATUS_SUCCESS)
            {
                entry->object->manual_reset = !!request->manual;
                entry->object->signaled = 0;
                entry->object->timer_when = 0;
                entry->object->timer_period = 0;
            }
            reply.handle = entry->handle;
        }
        pthread_mutex_unlock( &horizon_server_objects_mutex );
    }
    return horizon_server_write_reply( connection->reply_fd, &reply, sizeof(reply), NULL, 0 );
}

static int horizon_server_handle_set_timer( struct horizon_server_connection *connection,
                                            const unsigned char *message )
{
    const struct horizon_set_timer_request *request = (const void *)message;
    struct horizon_set_timer_reply reply;
    struct horizon_server_object *object = NULL;
    unsigned int status;

    memset( &reply, 0, sizeof(reply) );
    pthread_mutex_lock( &horizon_server_objects_mutex );
    status = horizon_server_find_typed_object_locked( request->handle, HORIZON_SERVER_OBJECT_TIMER, &object );
    if (status == HORIZON_STATUS_SUCCESS)
    {
        reply.signaled = object->signaled;
        object->timer_when = request->expire;
        object->timer_period = request->period > 0 ? request->period : 0;
        object->signaled = 1;
    }
    pthread_mutex_unlock( &horizon_server_objects_mutex );

    reply.header.error = status;
    return horizon_server_write_reply( connection->reply_fd, &reply, sizeof(reply), NULL, 0 );
}

static int horizon_server_handle_cancel_timer( struct horizon_server_connection *connection,
                                               const unsigned char *message )
{
    const struct horizon_cancel_timer_request *request = (const void *)message;
    struct horizon_cancel_timer_reply reply;
    struct horizon_server_object *object = NULL;
    unsigned int status;

    memset( &reply, 0, sizeof(reply) );
    pthread_mutex_lock( &horizon_server_objects_mutex );
    status = horizon_server_find_typed_object_locked( request->handle, HORIZON_SERVER_OBJECT_TIMER, &object );
    if (status == HORIZON_STATUS_SUCCESS)
    {
        reply.signaled = object->signaled;
        object->signaled = 0;
        object->timer_period = 0;
    }
    pthread_mutex_unlock( &horizon_server_objects_mutex );

    reply.header.error = status;
    return horizon_server_write_reply( connection->reply_fd, &reply, sizeof(reply), NULL, 0 );
}

static int horizon_server_handle_get_timer_info( struct horizon_server_connection *connection,
                                                 const unsigned char *message )
{
    const struct horizon_get_timer_info_request *request = (const void *)message;
    struct horizon_get_timer_info_reply reply;
    struct horizon_server_object *object = NULL;
    unsigned int status;

    memset( &reply, 0, sizeof(reply) );
    pthread_mutex_lock( &horizon_server_objects_mutex );
    status = horizon_server_find_typed_object_locked( request->handle, HORIZON_SERVER_OBJECT_TIMER, &object );
    if (status == HORIZON_STATUS_SUCCESS)
    {
        reply.when = object->timer_when;
        reply.signaled = object->signaled;
    }
    pthread_mutex_unlock( &horizon_server_objects_mutex );

    reply.header.error = status;
    return horizon_server_write_reply( connection->reply_fd, &reply, sizeof(reply), NULL, 0 );
}

static unsigned int horizon_server_select_wait( const struct horizon_select_wait_op *op,
                                                unsigned int size, int wait_all )
{
    unsigned int count;
    unsigned int i;
    unsigned int status = HORIZON_STATUS_TIMEOUT;

    if (size < offsetof( struct horizon_select_wait_op, handles[1] ))
        return HORIZON_STATUS_INVALID_PARAMETER;

    count = (size - offsetof( struct horizon_select_wait_op, handles )) / sizeof(op->handles[0]);
    if (!count) return HORIZON_STATUS_INVALID_PARAMETER;

    pthread_mutex_lock( &horizon_server_objects_mutex );
    if (wait_all)
    {
        status = HORIZON_STATUS_SUCCESS;
        for (i = 0; i < count; i++)
        {
            status = horizon_server_wait_object_locked( op->handles[i], FALSE );
            if (status != HORIZON_STATUS_SUCCESS) break;
        }
        if (status == HORIZON_STATUS_SUCCESS)
            for (i = 0; i < count; i++)
                horizon_server_wait_object_locked( op->handles[i], TRUE );
    }
    else
    {
        for (i = 0; i < count; i++)
        {
            status = horizon_server_wait_object_locked( op->handles[i], TRUE );
            if (status == HORIZON_STATUS_SUCCESS || status == HORIZON_STATUS_INVALID_HANDLE ||
                status == HORIZON_STATUS_OBJECT_TYPE_MISMATCH)
                break;
        }
    }
    pthread_mutex_unlock( &horizon_server_objects_mutex );
    return status;
}

static unsigned int horizon_server_select_signal_and_wait( const struct horizon_select_signal_and_wait_op *op,
                                                           unsigned int size )
{
    unsigned int status;

    if (size < sizeof(*op)) return HORIZON_STATUS_INVALID_PARAMETER;

    pthread_mutex_lock( &horizon_server_objects_mutex );
    status = horizon_server_signal_object_locked( op->signal );
    if (status == HORIZON_STATUS_SUCCESS)
        status = horizon_server_wait_object_locked( op->wait, TRUE );
    pthread_mutex_unlock( &horizon_server_objects_mutex );
    return status;
}

static unsigned int horizon_server_select_status( const struct horizon_select_request *request,
                                                  const unsigned char *data, unsigned int data_size )
{
    const unsigned char *select_data = NULL;
    int op;

    if (!request->size) return HORIZON_STATUS_TIMEOUT;
    if (data_size >= HORIZON_APC_RESULT_SIZE + request->size)
        select_data = data + HORIZON_APC_RESULT_SIZE;
    else if (data_size >= request->size)
        select_data = data;
    else return HORIZON_STATUS_INVALID_PARAMETER;

    if (request->size < sizeof(op)) return HORIZON_STATUS_INVALID_PARAMETER;
    memcpy( &op, select_data, sizeof(op) );

    switch (op)
    {
    case HORIZON_SELECT_WAIT:
        return horizon_server_select_wait( (const struct horizon_select_wait_op *)select_data,
                                           request->size, FALSE );
    case HORIZON_SELECT_WAIT_ALL:
        return horizon_server_select_wait( (const struct horizon_select_wait_op *)select_data,
                                           request->size, TRUE );
    case HORIZON_SELECT_SIGNAL_AND_WAIT:
        return horizon_server_select_signal_and_wait(
            (const struct horizon_select_signal_and_wait_op *)select_data, request->size );
    case HORIZON_SELECT_KEYED_EVENT_WAIT:
    case HORIZON_SELECT_KEYED_EVENT_RELEASE:
        return HORIZON_STATUS_SUCCESS;
    default:
        return HORIZON_STATUS_SUCCESS;
    }
}

static int horizon_server_handle_select( struct horizon_server_connection *connection,
                                         const unsigned char *message,
                                         const unsigned char *data, unsigned int data_size )
{
    const struct horizon_select_request *request = (const void *)message;
    struct horizon_select_reply reply;

    memset( &reply, 0, sizeof(reply) );
    reply.header.error = horizon_server_select_status( request, data, data_size );
    reply.signaled = 1;

    TRACE( "Horizon server select size %u timeout %lld status %08x.\n",
           request->size, request->timeout, reply.header.error );
    return horizon_server_write_reply( connection->reply_fd, &reply, sizeof(reply), NULL, 0 );
}

static void *horizon_server_thread( void *param )
{
    struct horizon_server_connection *connection = param;
    unsigned char message[HORIZON_SERVER_FIXED_MESSAGE_SIZE];

    for (;;)
    {
        struct horizon_server_request_header *header = (void *)message;
        unsigned char *request_data = NULL;
        int status = 0;
        int ret = horizon_read_exact( connection->request_fd, message, sizeof(message) );

        if (ret > 0 && &wine_nx_paint_trace_enabled && wine_nx_paint_trace_enabled)
        {
            /* The server-side half of the client-send/server-wake trace --
             * see the comment on wine_nx_trace_client_send_tick above.
             * Atomic exchange: read-and-clear in one operation, no separate
             * check-then-clear window for another iteration to race. */
            u64 send_tick = __atomic_exchange_n( &wine_nx_trace_client_send_tick, 0, __ATOMIC_ACQ_REL );
            if (send_tick) wine_nx_wake_latency_record( armTicksToNs( armGetSystemTick() - send_tick ) );
        }

        if (ret <= 0) break;
        if (header->request_size)
        {
            if (!(request_data = malloc( header->request_size )))
            {
                if (connection->reply_fd != -1)
                    status = horizon_server_write_status( connection->reply_fd, HORIZON_STATUS_NO_MEMORY );
                if (status) goto done;
                continue;
            }
            if (horizon_read_exact( connection->request_fd, request_data, header->request_size ) <= 0)
            {
                free( request_data );
                break;
            }
        }

        switch (header->req)
        {
        case HORIZON_REQ_INIT_FIRST_THREAD:
            status = horizon_server_handle_init_first_thread( connection, message );
            break;
        case HORIZON_REQ_INIT_PROCESS_DONE:
            status = horizon_server_handle_init_process_done( connection );
            break;
        case HORIZON_REQ_INIT_THREAD:
            status = horizon_server_handle_init_thread( connection, message );
            break;
        case HORIZON_REQ_NEW_THREAD:
            status = horizon_server_handle_new_thread( connection, message );
            break;
        case HORIZON_REQ_SUSPEND_THREAD:
        case HORIZON_REQ_RESUME_THREAD:
            status = horizon_server_handle_resume_thread( connection, message );
            break;
        case HORIZON_REQ_OPEN_FILE_OBJECT:
            status = horizon_server_handle_open_file_object( connection, message, request_data,
                                                             header->request_size );
            break;
        case HORIZON_REQ_IOCTL:
            status = horizon_server_handle_ioctl( connection, message, request_data,
                                                  header->request_size );
            break;
        case HORIZON_REQ_RECV_SOCKET:
        case HORIZON_REQ_SEND_SOCKET:
            status = horizon_server_handle_socket_io( connection, message );
            break;
        case HORIZON_REQ_SOCKET_GET_EVENTS:
            status = horizon_server_handle_socket_get_events( connection, message );
            break;
        case HORIZON_REQ_SET_ASYNC_DIRECT_RESULT:
            status = horizon_server_handle_set_async_direct_result( connection, message );
            break;
        case HORIZON_REQ_CLOSE_HANDLE:
            status = horizon_server_handle_close_handle( connection, message );
            break;
        case HORIZON_REQ_SET_HANDLE_INFO:
            status = horizon_server_handle_set_handle_info( connection );
            break;
        case HORIZON_REQ_DUP_HANDLE:
            status = horizon_server_handle_dup_handle( connection, message );
            break;
        case HORIZON_REQ_ALLOCATE_RESERVE_OBJECT:
            status = horizon_server_handle_allocate_reserve_object( connection, message );
            break;
        case HORIZON_REQ_COMPARE_OBJECTS:
            status = horizon_server_handle_compare_objects( connection, message );
            break;
        case HORIZON_REQ_SET_OBJECT_PERMANENCE:
            status = horizon_server_write_status( connection->reply_fd, HORIZON_STATUS_SUCCESS );
            break;
        case HORIZON_REQ_OPEN_PROCESS:
            status = horizon_server_handle_open_object( connection, HORIZON_SERVER_OBJECT_PROCESS );
            break;
        case HORIZON_REQ_OPEN_THREAD:
            status = horizon_server_handle_open_object( connection, HORIZON_SERVER_OBJECT_THREAD );
            break;
        case HORIZON_REQ_ADD_ATOM:
            status = horizon_server_handle_atom( connection, request_data, header->request_size, 1 );
            break;
        case HORIZON_REQ_FIND_ATOM:
            status = horizon_server_handle_atom( connection, request_data, header->request_size, 0 );
            break;
        case HORIZON_REQ_SEND_HARDWARE_MESSAGE:
            status = horizon_server_handle_send_hardware_message( connection, message );
            break;
        case HORIZON_REQ_GET_MESSAGE:
            status = horizon_server_handle_get_message( connection, message );
            break;
        case HORIZON_REQ_ACCEPT_HARDWARE_MESSAGE:
            status = horizon_server_handle_accept_hardware_message( connection, message );
            break;
        case HORIZON_REQ_ALLOC_USER_HANDLE:
            status = horizon_server_handle_alloc_user_handle( connection, message );
            break;
        case HORIZON_REQ_FREE_USER_HANDLE:
            status = horizon_server_handle_free_user_handle( connection, message );
            break;
        case HORIZON_REQ_SELECT:
            status = horizon_server_handle_select( connection, message, request_data, header->request_size );
            break;
        case HORIZON_REQ_CREATE_EVENT:
            status = horizon_server_handle_create_event( connection, message, request_data, header->request_size );
            break;
        case HORIZON_REQ_EVENT_OP:
            status = horizon_server_handle_event_op( connection, message );
            break;
        case HORIZON_REQ_QUERY_EVENT:
            status = horizon_server_handle_query_event( connection, message );
            break;
        case HORIZON_REQ_OPEN_EVENT:
            status = horizon_server_handle_open_named_object( connection, message, request_data,
                                                              header->request_size, HORIZON_SERVER_OBJECT_EVENT );
            break;
        case HORIZON_REQ_CREATE_KEYED_EVENT:
            status = horizon_server_handle_create_keyed_event( connection, request_data, header->request_size );
            break;
        case HORIZON_REQ_OPEN_KEYED_EVENT:
            status = horizon_server_handle_open_named_object(
                connection, message, request_data, header->request_size, HORIZON_SERVER_OBJECT_KEYED_EVENT );
            break;
        case HORIZON_REQ_CREATE_MUTEX:
            status = horizon_server_handle_create_mutex( connection, message, request_data, header->request_size );
            break;
        case HORIZON_REQ_RELEASE_MUTEX:
            status = horizon_server_handle_release_mutex( connection, message );
            break;
        case HORIZON_REQ_OPEN_MUTEX:
            status = horizon_server_handle_open_named_object( connection, message, request_data,
                                                              header->request_size, HORIZON_SERVER_OBJECT_MUTEX );
            break;
        case HORIZON_REQ_QUERY_MUTEX:
            status = horizon_server_handle_query_mutex( connection, message );
            break;
        case HORIZON_REQ_CREATE_SEMAPHORE:
            status = horizon_server_handle_create_semaphore( connection, message, request_data,
                                                             header->request_size );
            break;
        case HORIZON_REQ_RELEASE_SEMAPHORE:
            status = horizon_server_handle_release_semaphore( connection, message );
            break;
        case HORIZON_REQ_QUERY_SEMAPHORE:
            status = horizon_server_handle_query_semaphore( connection, message );
            break;
        case HORIZON_REQ_OPEN_SEMAPHORE:
            status = horizon_server_handle_open_named_object(
                connection, message, request_data, header->request_size, HORIZON_SERVER_OBJECT_SEMAPHORE );
            break;
        case HORIZON_REQ_CREATE_FILE:
            status = horizon_server_handle_create_file( connection, message, request_data, header->request_size );
            break;
        case HORIZON_REQ_GET_HANDLE_FD:
            status = horizon_server_handle_get_handle_fd( connection, message );
            break;
        case HORIZON_REQ_QUERY_DIRECTORY_FILE:
            status = horizon_server_handle_query_directory_file( connection, message, request_data,
                                                                 header->request_size );
            break;
        case HORIZON_REQ_CREATE_MAPPING:
            status = horizon_server_handle_create_mapping( connection, message );
            break;
        case HORIZON_REQ_OPEN_MAPPING:
            status = horizon_server_handle_open_mapping( connection, message, request_data,
                                                         header->request_size );
            break;
        case HORIZON_REQ_GET_MAPPING_INFO:
            status = horizon_server_handle_get_mapping_info( connection, message );
            break;
        case HORIZON_REQ_GET_IMAGE_MAP_ADDRESS:
            status = horizon_server_handle_get_image_map_address( connection, message );
            break;
        case HORIZON_REQ_MAP_VIEW:
            status = horizon_server_handle_map_view( connection, message );
            break;
        case HORIZON_REQ_MAP_IMAGE_VIEW:
            status = horizon_server_handle_map_image_view( connection, message );
            break;
        case HORIZON_REQ_UNMAP_VIEW:
            status = horizon_server_handle_unmap_view( connection, message );
            break;
        case HORIZON_REQ_CREATE_TIMER:
            status = horizon_server_handle_create_timer( connection, message, request_data, header->request_size );
            break;
        case HORIZON_REQ_OPEN_TIMER:
            status = horizon_server_handle_open_named_object( connection, message, request_data,
                                                              header->request_size, HORIZON_SERVER_OBJECT_TIMER );
            break;
        case HORIZON_REQ_SET_TIMER:
            status = horizon_server_handle_set_timer( connection, message );
            break;
        case HORIZON_REQ_CANCEL_TIMER:
            status = horizon_server_handle_cancel_timer( connection, message );
            break;
        case HORIZON_REQ_GET_TIMER_INFO:
            status = horizon_server_handle_get_timer_info( connection, message );
            break;
        case HORIZON_REQ_CREATE_WINDOW:
            status = horizon_server_handle_create_window( connection, message, request_data,
                                                          header->request_size );
            break;
        case HORIZON_REQ_DESTROY_WINDOW:
            status = horizon_server_handle_destroy_window( connection, message );
            break;
        case HORIZON_REQ_GET_DESKTOP_WINDOW:
            status = horizon_server_handle_get_desktop_window( connection, message );
            break;
        case HORIZON_REQ_SET_WINDOW_OWNER:
            status = horizon_server_handle_set_window_owner( connection, message );
            break;
        case HORIZON_REQ_GET_WINDOW_INFO:
            status = horizon_server_handle_get_window_info( connection, message );
            break;
        case HORIZON_REQ_INIT_WINDOW_INFO:
            status = horizon_server_handle_init_window_info( connection, message );
            break;
        case HORIZON_REQ_SET_WINDOW_INFO:
            status = horizon_server_handle_set_window_info( connection, message );
            break;
        case HORIZON_REQ_GET_WINDOW_CHILDREN_FROM_POINT:
            status = horizon_server_handle_get_window_children_from_point( connection, message );
            break;
        case HORIZON_REQ_GET_WINDOW_TREE:
            status = horizon_server_handle_get_window_tree( connection, message );
            break;
        case HORIZON_REQ_SET_WINDOW_POS:
            status = horizon_server_handle_set_window_pos( connection, message, request_data,
                                                           header->request_size );
            break;
        case HORIZON_REQ_GET_WINDOW_RECTANGLES:
            status = horizon_server_handle_get_window_rectangles( connection, message );
            break;
        case HORIZON_REQ_GET_VISIBLE_REGION:
            status = horizon_server_handle_get_visible_region( connection, message );
            break;
        case HORIZON_REQ_GET_UPDATE_REGION:
            status = horizon_server_handle_get_update_region( connection, message );
            break;
        case HORIZON_REQ_GET_PAINT_REGIONS:
            status = horizon_server_handle_get_paint_regions( connection, message );
            break;
        case HORIZON_REQ_GET_UPDATE_FLAGS_EX:
            status = horizon_server_handle_get_update_flags_ex( connection, message );
            break;
        case HORIZON_REQ_REDRAW_WINDOW_UPDATENOW:
            status = horizon_server_handle_redraw_window_updatenow( connection, message );
            break;
        case HORIZON_REQ_UPDATE_WINDOW_ZORDER:
            status = horizon_server_handle_update_window_zorder( connection, message );
            break;
        case HORIZON_REQ_REDRAW_WINDOW:
            status = horizon_server_handle_redraw_window( connection, message, request_data,
                                                          header->request_size );
            break;
        case HORIZON_REQ_SET_WINDOW_PROPERTY:
            status = horizon_server_handle_set_window_property( connection, message, request_data,
                                                                header->request_size );
            break;
        case HORIZON_REQ_REMOVE_WINDOW_PROPERTY:
            status = horizon_server_handle_get_window_property( connection, message, request_data,
                                                                header->request_size, 1 );
            break;
        case HORIZON_REQ_GET_WINDOW_PROPERTY:
            status = horizon_server_handle_get_window_property( connection, message, request_data,
                                                                header->request_size, 0 );
            break;
        case HORIZON_REQ_GET_WINDOW_PROPERTIES:
            status = horizon_server_handle_get_window_properties( connection, message );
            break;
        case HORIZON_REQ_CREATE_WINSTATION:
            status = horizon_server_handle_create_winstation( connection, message, request_data,
                                                              header->request_size );
            break;
        case HORIZON_REQ_OPEN_WINSTATION:
            status = horizon_server_handle_open_winstation( connection, message, request_data,
                                                            header->request_size );
            break;
        case HORIZON_REQ_CLOSE_WINSTATION:
            status = horizon_server_handle_close_handle( connection, message );
            break;
        case HORIZON_REQ_SET_WINSTATION_MONITORS:
            status = horizon_server_handle_set_winstation_monitors( connection, message );
            break;
        case HORIZON_REQ_GET_PROCESS_WINSTATION:
            status = horizon_server_handle_get_process_winstation( connection );
            break;
        case HORIZON_REQ_SET_PROCESS_WINSTATION:
            status = horizon_server_handle_set_process_winstation( connection, message );
            break;
        case HORIZON_REQ_ENUM_WINSTATION:
            status = horizon_server_handle_enum_winstation( connection );
            break;
        case HORIZON_REQ_CREATE_DESKTOP:
            status = horizon_server_handle_create_desktop( connection, message, request_data,
                                                           header->request_size );
            break;
        case HORIZON_REQ_OPEN_DESKTOP:
            status = horizon_server_handle_open_desktop( connection, message, request_data,
                                                         header->request_size );
            break;
        case HORIZON_REQ_OPEN_INPUT_DESKTOP:
            status = horizon_server_handle_open_input_desktop( connection );
            break;
        case HORIZON_REQ_SET_INPUT_DESKTOP:
            status = horizon_server_handle_set_input_desktop( connection, message );
            break;
        case HORIZON_REQ_CLOSE_DESKTOP:
            status = horizon_server_handle_close_handle( connection, message );
            break;
        case HORIZON_REQ_GET_THREAD_DESKTOP:
            status = horizon_server_handle_get_thread_desktop( connection, message );
            break;
        case HORIZON_REQ_SET_THREAD_DESKTOP:
            status = horizon_server_handle_set_thread_desktop( connection, message );
            break;
        case HORIZON_REQ_SET_USER_OBJECT_INFO:
            status = horizon_server_handle_set_user_object_info( connection, message );
            break;
        case HORIZON_REQ_GET_THREAD_INPUT:
            status = horizon_server_handle_get_thread_input( connection );
            break;
        case HORIZON_REQ_SET_FOREGROUND_WINDOW:
            status = horizon_server_handle_set_foreground_window( connection, message );
            break;
        case HORIZON_REQ_SET_FOCUS_WINDOW:
        case HORIZON_REQ_SET_ACTIVE_WINDOW:
            status = horizon_server_handle_set_input_window( connection, message, header->req );
            break;
        case HORIZON_REQ_SET_CAPTURE_WINDOW:
            status = horizon_server_handle_set_capture_window( connection, message );
            break;
        case HORIZON_REQ_CREATE_CLASS:
            status = horizon_server_handle_create_class( connection, message, request_data,
                                                         header->request_size );
            break;
        case HORIZON_REQ_DESTROY_CLASS:
            status = horizon_server_handle_destroy_class( connection, message, request_data,
                                                          header->request_size );
            break;
        case HORIZON_REQ_SET_CURSOR:
            status = horizon_server_handle_set_cursor( connection, message );
            break;
        default:
            WARN( "unimplemented Horizon server request %d size %u reply_size %u.\n",
                  header->req, header->request_size, header->reply_size );
            if (connection->reply_fd != -1)
                status = horizon_server_write_status( connection->reply_fd, HORIZON_STATUS_NOT_IMPLEMENTED );
            break;
        }

        free( request_data );
        if (status) goto done;
    }

done:
    if (connection->request_fd != -1) close( connection->request_fd );
    if (connection->reply_fd != -1) close( connection->reply_fd );
    if (connection->wait_fd != -1) close( connection->wait_fd );
    free( connection );
    return NULL;
}

unsigned int horizon_server_protocol_version(void)
{
    return SERVER_PROTOCOL_VERSION;
}

int horizon_server_connect(void)
{
    int control_pipe[2] = {-1, -1};
    int request_pipe[2] = {-1, -1};
    struct horizon_server_connection *connection = NULL;
    pthread_t thread;

    if (horizon_pipe( control_pipe ) == -1 || horizon_pipe( request_pipe ) == -1)
    {
        int err = errno;

        if (control_pipe[0] != -1) close( control_pipe[0] );
        if (control_pipe[1] != -1) close( control_pipe[1] );
        if (request_pipe[0] != -1) close( request_pipe[0] );
        if (request_pipe[1] != -1) close( request_pipe[1] );

        errno = err;
        fprintf( stderr, "wine: failed to create Horizon server bootstrap pipes: %s\n", strerror(err) );
        exit(1);
    }

    if (!(connection = calloc( 1, sizeof(*connection) )))
    {
        close( control_pipe[0] );
        close( control_pipe[1] );
        close( request_pipe[0] );
        close( request_pipe[1] );
        errno = ENOMEM;
        fprintf( stderr, "wine: failed to allocate Horizon server connection.\n" );
        exit(1);
    }

    connection->request_fd = request_pipe[0];
    connection->reply_fd = -1;
    connection->wait_fd = -1;

    if ((errno = pthread_create( &thread, NULL, horizon_server_thread, connection )))
    {
        int err = errno;

        close( control_pipe[0] );
        close( control_pipe[1] );
        close( request_pipe[0] );
        close( request_pipe[1] );
        free( connection );
        errno = err;
        fprintf( stderr, "wine: failed to start Horizon server thread: %s\n", strerror(err) );
        exit(1);
    }
    pthread_detach( thread );

    close( control_pipe[1] );

    TRACE( "Horizon server bootstrap control fd %d request read fd %d write fd %d version %u.\n",
           control_pipe[0], request_pipe[0], request_pipe[1], horizon_server_protocol_version() );
    horizon_server_queue_fd( request_pipe[1], horizon_server_protocol_version() );
    close( request_pipe[1] );

    return control_pipe[0];
}

void horizon_server_send_fd( int fd )
{
    horizon_fd_queue_push_dup( &horizon_client_to_server_fds, fd, 0 );
}

int horizon_server_receive_fd( unsigned int *handle )
{
    return horizon_fd_queue_pop( &horizon_server_to_client_fds, handle );
}

#ifndef HORIZON_NO_LIBNX_EXCEPTION_HANDLER
/* Weak fallback definitions so smoke binaries (which don't link runtime.c
 * or virtual.c) still resolve.  The runtime/full builds provide strong
 * definitions that override these. */
__attribute__((weak)) void wine_nx_runtime_trace( const char *msg )
{
    (void)msg;
}

__attribute__((weak)) NTSTATUS virtual_handle_fault( EXCEPTION_RECORD *rec, void *stack )
{
    (void)rec;
    (void)stack;
    return STATUS_ACCESS_VIOLATION;
}

void __libnx_exception_handler( ThreadExceptionDump *ctx )
{
    EXCEPTION_RECORD rec = { 0 };
    DWORD64 esr = ctx->esr;
    NTSTATUS status;
    char buf[256];

    rec.ExceptionCode = STATUS_ACCESS_VIOLATION;
    rec.ExceptionAddress = (void *)ctx->pc.x;
    rec.NumberParameters = 2;
    if ((esr & 0xf0000000) == 0x80000000) rec.ExceptionInformation[0] = EXCEPTION_EXECUTE_FAULT;
    else if (esr & 0x40) rec.ExceptionInformation[0] = EXCEPTION_WRITE_FAULT;
    else rec.ExceptionInformation[0] = EXCEPTION_READ_FAULT;
    rec.ExceptionInformation[1] = (ULONG_PTR)ctx->far.x;

    snprintf( buf, sizeof(buf),
              "[EXC] desc=0x%08x esr=0x%08x pc=0x%llx far=0x%llx sp=0x%llx lr=0x%llx kind=%s",
              ctx->error_desc, ctx->esr,
              (unsigned long long)ctx->pc.x, (unsigned long long)ctx->far.x,
              (unsigned long long)ctx->sp.x, (unsigned long long)ctx->lr.x,
              rec.ExceptionInformation[0] == EXCEPTION_EXECUTE_FAULT ? "exec" :
              rec.ExceptionInformation[0] == EXCEPTION_WRITE_FAULT   ? "write" : "read" );
    wine_nx_runtime_trace( buf );
    snprintf( buf, sizeof(buf),
              "[EXC] x0=0x%llx x1=0x%llx x2=0x%llx x3=0x%llx x18=0x%llx",
              (unsigned long long)ctx->cpu_gprs[0].x, (unsigned long long)ctx->cpu_gprs[1].x,
              (unsigned long long)ctx->cpu_gprs[2].x, (unsigned long long)ctx->cpu_gprs[3].x,
              (unsigned long long)ctx->cpu_gprs[18].x );
    wine_nx_runtime_trace( buf );

    status = virtual_handle_fault( &rec, (void *)ctx->sp.x );
    if (status)
    {
        snprintf( buf, sizeof(buf), "[EXC] unhandled status=0x%08x; parking thread", (unsigned)status );
        wine_nx_runtime_trace( buf );
        /* Park rather than returning: libnx's exception_returnentry would
         * svcBreak and kill the process before stdio buffers flush. */
        for (;;) svcSleepThread( 1000ULL * 1000ULL * 1000ULL );
    }

#if defined(__aarch64__)
    horizon_restore_exception_context( ctx );
#else
    wine_nx_runtime_trace( "[EXC] handled fault but cannot restore non-AArch64 context" );
    for (;;) svcSleepThread( 1000ULL * 1000ULL * 1000ULL );
#endif
}
#endif

static size_t page_align_size( size_t size )
{
    return (size + 0xfff) & ~(size_t)0xfff;
}

static u32 get_horizon_perm( int prot )
{
    if (!(prot & (PROT_READ | PROT_WRITE | PROT_EXEC))) return Perm_None;
    if (prot & PROT_EXEC) return Perm_Rx;
    if (prot & PROT_WRITE) return Perm_Rw;
    return Perm_R;
}

static int get_effective_horizon_prot( int prot )
{
    if ((prot & PROT_EXEC) && (prot & PROT_WRITE)) prot &= ~PROT_EXEC;
    return prot;
}

static void list_add_mapping( struct horizon_mapping *mapping )
{
    mapping->next = mappings;
    mappings = mapping;
}

static void list_remove_mapping( struct horizon_mapping *mapping )
{
    struct horizon_mapping **ptr = &mappings;

    while (*ptr)
    {
        if (*ptr == mapping)
        {
            *ptr = mapping->next;
            return;
        }
        ptr = &(*ptr)->next;
    }
}

static struct horizon_mapping *find_overlap_mapping( void *addr, size_t size )
{
    struct horizon_mapping *mapping;
    char *start = addr;
    char *end = start + size;

    for (mapping = mappings; mapping; mapping = mapping->next)
    {
        char *mapping_start = mapping->addr;
        char *mapping_end = mapping_start + mapping->size;

        if (start < mapping_end && mapping_start < end) return mapping;
    }

    return NULL;
}

static int read_fd_at( int fd, void *buffer, size_t size, off_t offset )
{
    char *ptr = buffer;
    int work_fd = dup( fd );

    if (work_fd == -1) return -1;
    if (lseek( work_fd, offset, SEEK_SET ) == (off_t)-1)
    {
        int saved_errno = errno;

        close( work_fd );
        errno = saved_errno;
        return -1;
    }

    while (size)
    {
        ssize_t ret = read( work_fd, ptr, size );

        if (ret > 0)
        {
            ptr += ret;
            size -= ret;
            continue;
        }
        if (!ret)
        {
            close( work_fd );
            return 0;
        }
        if (errno == EINTR) continue;
        {
            int saved_errno = errno;

            close( work_fd );
            errno = saved_errno;
        }
        return -1;
    }

    close( work_fd );
    return 0;
}

static void write_fd_at( int fd, const void *buffer, size_t size, off_t offset )
{
    const char *ptr = buffer;
    int work_fd = dup( fd );

    if (work_fd == -1)
    {
        WARN( "failed to duplicate Horizon file mapping fd: %s.\n", strerror(errno) );
        return;
    }
    if (lseek( work_fd, offset, SEEK_SET ) == (off_t)-1)
    {
        WARN( "failed to seek Horizon file mapping to offset %#lx: %s.\n",
              (unsigned long)offset, strerror(errno) );
        close( work_fd );
        return;
    }

    while (size)
    {
        ssize_t ret = write( work_fd, ptr, size );

        if (ret > 0)
        {
            ptr += ret;
            size -= ret;
            continue;
        }
        if (ret < 0 && errno == EINTR) continue;
        WARN( "failed to write back Horizon file mapping at offset %#lx: %s.\n",
              (unsigned long)offset, strerror(errno) );
        close( work_fd );
        return;
    }

    close( work_fd );
}

static void remove_reservation( VirtmemReservation *reservation );

static void free_backing( struct horizon_backing *backing, BOOL write_back )
{
    if (!backing) return;

    if (write_back && backing->write_back && backing->fd != -1)
        write_fd_at( backing->fd, backing->heap_addr, backing->size, backing->file_offset );
    if (backing->code_reservation) remove_reservation( backing->code_reservation );
    if (backing->fd != -1) close( backing->fd );
    free( backing->heap_addr );
    free( backing );
}

static void release_backing( struct horizon_backing *backing )
{
    if (!backing) return;

    if (!backing->refs)
    {
        WARN( "releasing unreferenced Horizon backing %p.\n", backing );
        free_backing( backing, FALSE );
        return;
    }
    if (--backing->refs) return;
    free_backing( backing, TRUE );
}

static void destroy_backing( struct horizon_backing *backing )
{
    free_backing( backing, FALSE );
}

static struct horizon_mapping *alloc_mapping( void *addr, size_t size, struct horizon_backing *backing,
                                              size_t source_offset, VirtmemReservation *reservation,
                                              int prot )
{
    struct horizon_mapping *mapping = calloc( 1, sizeof(*mapping) );

    if (!mapping) return NULL;

    mapping->addr = addr;
    mapping->size = size;
    mapping->source_offset = source_offset;
    mapping->prot = get_effective_horizon_prot( prot );
    mapping->backing = backing;
    mapping->reservation = reservation;
    if (backing) backing->refs++;
    return mapping;
}

static VirtmemReservation *reserve_fixed_range_locked( void *addr, size_t size )
{
    VirtmemReservation *reservation = virtmemAddReservation( addr, size );

    if (!reservation) errno = ENOMEM;
    return reservation;
}

static VirtmemReservation *reserve_fixed_range( void *addr, size_t size )
{
    VirtmemReservation *reservation;

    virtmemLock();
    reservation = reserve_fixed_range_locked( addr, size );
    virtmemUnlock();
    return reservation;
}

static void remove_reservation_locked( VirtmemReservation *reservation )
{
    virtmemRemoveReservation( reservation );
}

static void remove_reservation( VirtmemReservation *reservation )
{
    virtmemLock();
    remove_reservation_locked( reservation );
    virtmemUnlock();
}

/* Wine-visible memory uses AliasCode mappings so RW pages can later become RX. */
static int check_code_memory_syscalls(void)
{
    if (!envIsSyscallHinted(0x73) || !envIsSyscallHinted(0x77) ||
        !envIsSyscallHinted(0x78) || envGetOwnProcessHandle() == INVALID_HANDLE)
    {
        errno = ENOSYS;
        return -1;
    }

    return 0;
}

static int set_code_memory_perm( void *addr, void *source, size_t size, int prot, BOOL source_accessible )
{
    int effective_prot = get_effective_horizon_prot( prot );
    Result rc;

    if (check_code_memory_syscalls()) return -1;

    if ((effective_prot & PROT_EXEC) && source_accessible) armDCacheFlush( source, size );

    rc = svcSetProcessMemoryPermission( envGetOwnProcessHandle(), (u64)addr, size,
                                        get_horizon_perm( effective_prot ) );
    if (R_FAILED(rc))
    {
        horizon_trace( "[HMAP] set_perm failed addr=%p source=%p size=0x%lx prot=0x%x rc=0x%x",
                       addr, source, (unsigned long)size, prot, rc );
        WARN( "svcSetProcessMemoryPermission(%p, %zu, %#x) failed %#x.\n",
              addr, size, prot, rc );
        errno = EINVAL;
        return -1;
    }

    if (effective_prot & PROT_EXEC) armICacheInvalidate( addr, size );
    return 0;
}

static int map_code_memory_range( void *addr, void *source, size_t size, int prot, int map_errno )
{
    int effective_prot = get_effective_horizon_prot( prot );
    Result rc;

    if (check_code_memory_syscalls()) return -1;

    if (effective_prot & PROT_EXEC) armDCacheFlush( source, size );

    rc = svcMapProcessCodeMemory( envGetOwnProcessHandle(), (u64)addr, (u64)source, size );
    if (R_FAILED(rc))
    {
        horizon_trace( "[HMAP] map_code failed addr=%p source=%p size=0x%lx prot=0x%x rc=0x%x",
                       addr, source, (unsigned long)size, prot, rc );
        WARN( "svcMapProcessCodeMemory(%p, %p, %zu) failed %#x.\n", addr, source, size, rc );
        errno = map_errno;
        return -1;
    }

    if (set_code_memory_perm( addr, source, size, prot, FALSE ))
    {
        svcUnmapProcessCodeMemory( envGetOwnProcessHandle(), (u64)addr, (u64)source, size );
        return -1;
    }

    return 0;
}

static int unmap_code_memory_range( void *addr, void *source, size_t size )
{
    Result rc;

    if (check_code_memory_syscalls()) return -1;

    rc = svcUnmapProcessCodeMemory( envGetOwnProcessHandle(), (u64)addr, (u64)source, size );
    if (R_FAILED(rc))
    {
        WARN( "svcUnmapProcessCodeMemory(%p, %p, %zu) failed %#x.\n", addr, source, size, rc );
        errno = EINVAL;
        return -1;
    }

    return 0;
}

static struct horizon_backing *create_backing_locked( size_t size, int prot, int fd, off_t offset, int flags )
{
    struct horizon_backing *backing;

    if (check_code_memory_syscalls()) return NULL;

    if (!(backing = calloc( 1, sizeof(*backing) )))
    {
        errno = ENOMEM;
        return NULL;
    }

    backing->fd = -1;
    backing->size = size;
    backing->heap_addr = memalign( 0x1000, size );
    if (!backing->heap_addr)
    {
        free( backing );
        errno = ENOMEM;
        return NULL;
    }
    memset( backing->heap_addr, 0, size );

    if (fd != -1)
    {
        int saved_errno;

        if (read_fd_at( fd, backing->heap_addr, size, offset ))
        {
            saved_errno = errno;
            destroy_backing( backing );
            errno = saved_errno;
            return NULL;
        }

        if ((flags & MAP_SHARED) && (prot & PROT_WRITE))
        {
            backing->fd = dup( fd );
            if (backing->fd == -1)
            {
                saved_errno = errno;
                destroy_backing( backing );
                errno = saved_errno;
                return NULL;
            }
            backing->file_offset = offset;
            backing->write_back = TRUE;
        }
    }

    return backing;
}

static int map_backing_at_locked( void *addr, size_t size, int prot, int fd, off_t offset,
                                  int flags, int map_errno )
{
    struct horizon_backing *backing;
    struct horizon_mapping *mapping;
    VirtmemReservation *reservation;

    if (!(backing = create_backing_locked( size, prot, fd, offset, flags )))
    {
        horizon_trace( "[HMAP] create_backing failed addr=%p size=0x%lx prot=0x%x errno=%d",
                       addr, (unsigned long)size, prot, errno );
        return -1;
    }

    if (!(reservation = reserve_fixed_range_locked( addr, size )))
    {
        horizon_trace( "[HMAP] reserve_target failed addr=%p size=0x%lx prot=0x%x errno=%d",
                       addr, (unsigned long)size, prot, errno );
        destroy_backing( backing );
        return -1;
    }

    backing->code_addr = addr;
    backing->code_reservation = reservation;

    if (map_code_memory_range( addr, backing->heap_addr, size, prot, map_errno ))
    {
        horizon_trace( "[HMAP] map_backing failed addr=%p source=%p size=0x%lx prot=0x%x errno=%d",
                       addr, backing->heap_addr, (unsigned long)size, prot, errno );
        backing->code_reservation = NULL;
        remove_reservation_locked( reservation );
        destroy_backing( backing );
        return -1;
    }

    if (!(mapping = alloc_mapping( addr, size, backing, 0, NULL, prot )))
    {
        unmap_code_memory_range( addr, backing->heap_addr, size );
        backing->code_reservation = NULL;
        remove_reservation_locked( reservation );
        destroy_backing( backing );
        errno = ENOMEM;
        return -1;
    }

    list_add_mapping( mapping );
    return 0;
}

static int map_backing_at( void *addr, size_t size, int prot, int fd, off_t offset, int flags,
                           int map_errno )
{
    int ret;

    virtmemLock();
    ret = map_backing_at_locked( addr, size, prot, fd, offset, flags, map_errno );
    virtmemUnlock();
    return ret;
}

static int split_reservation_mapping( struct horizon_mapping *mapping, char *start, size_t size )
{
    char *mapping_start = mapping->addr;
    char *mapping_end = mapping_start + mapping->size;
    char *end = start + size;

    list_remove_mapping( mapping );
    remove_reservation( mapping->reservation );

    if (mapping_start < start)
    {
        VirtmemReservation *reservation = reserve_fixed_range( mapping_start, start - mapping_start );
        struct horizon_mapping *left;

        if (!reservation) return -1;
        if (!(left = alloc_mapping( mapping_start, start - mapping_start, NULL, 0, reservation, PROT_NONE )))
        {
            remove_reservation( reservation );
            errno = ENOMEM;
            return -1;
        }
        list_add_mapping( left );
    }

    if (end < mapping_end)
    {
        VirtmemReservation *reservation = reserve_fixed_range( end, mapping_end - end );
        struct horizon_mapping *right;

        if (!reservation) return -1;
        if (!(right = alloc_mapping( end, mapping_end - end, NULL, 0, reservation, PROT_NONE )))
        {
            remove_reservation( reservation );
            errno = ENOMEM;
            return -1;
        }
        list_add_mapping( right );
    }

    free( mapping );
    return 0;
}

static int split_backing_mapping( struct horizon_mapping *mapping, char *start, size_t size )
{
    char *mapping_start = mapping->addr;
    char *mapping_end = mapping_start + mapping->size;
    char *end = start + size;
    size_t offset = start - mapping_start;
    void *source = (char *)mapping->backing->heap_addr + mapping->source_offset + offset;
    struct horizon_mapping *left = NULL;
    struct horizon_mapping *right = NULL;

    if (mapping_start < start &&
        !(left = alloc_mapping( mapping_start, start - mapping_start,
                                mapping->backing, mapping->source_offset, NULL, mapping->prot )))
        return -1;

    if (end < mapping_end &&
        !(right = alloc_mapping( end, mapping_end - end, mapping->backing,
                                 mapping->source_offset + (end - mapping_start), NULL, mapping->prot )))
    {
        if (left)
        {
            release_backing( left->backing );
            free( left );
        }
        return -1;
    }

    if (unmap_code_memory_range( start, source, size ))
    {
        if (left)
        {
            release_backing( left->backing );
            free( left );
        }
        if (right)
        {
            release_backing( right->backing );
            free( right );
        }
        return -1;
    }

    list_remove_mapping( mapping );

    if (left) list_add_mapping( left );
    if (right) list_add_mapping( right );

    release_backing( mapping->backing );
    free( mapping );
    return 0;
}

static struct horizon_mapping *split_backing_mapping_metadata( struct horizon_mapping *mapping,
                                                               char *start, size_t size )
{
    char *mapping_start = mapping->addr;
    char *mapping_end = mapping_start + mapping->size;
    char *end = start + size;
    struct horizon_mapping *left = NULL;
    struct horizon_mapping *middle = NULL;
    struct horizon_mapping *right = NULL;

    if (mapping_start == start && mapping_end == end) return mapping;

    if (mapping_start < start &&
        !(left = alloc_mapping( mapping_start, start - mapping_start, mapping->backing,
                                mapping->source_offset, NULL, mapping->prot )))
        goto failed;

    if (!(middle = alloc_mapping( start, size, mapping->backing,
                                  mapping->source_offset + (start - mapping_start),
                                  NULL, mapping->prot )))
        goto failed;

    if (end < mapping_end &&
        !(right = alloc_mapping( end, mapping_end - end, mapping->backing,
                                 mapping->source_offset + (end - mapping_start),
                                 NULL, mapping->prot )))
        goto failed;

    list_remove_mapping( mapping );
    if (left) list_add_mapping( left );
    if (right) list_add_mapping( right );
    list_add_mapping( middle );
    release_backing( mapping->backing );
    free( mapping );
    return middle;

failed:
    if (left)
    {
        release_backing( left->backing );
        free( left );
    }
    if (middle)
    {
        release_backing( middle->backing );
        free( middle );
    }
    if (right)
    {
        release_backing( right->backing );
        free( right );
    }
    errno = ENOMEM;
    return NULL;
}

static int protect_code_mapping( struct horizon_mapping *mapping, int prot )
{
    int old_prot = mapping->prot;
    int new_prot = get_effective_horizon_prot( prot );
    void *source = (char *)mapping->backing->heap_addr + mapping->source_offset;

    if ((old_prot & PROT_WRITE) && (new_prot & PROT_WRITE))
    {
        mapping->prot = new_prot;
        return 0;
    }

    if ((old_prot & PROT_WRITE) && !(new_prot & PROT_WRITE))
    {
        int saved_errno;

        if (unmap_code_memory_range( mapping->addr, source, mapping->size )) return -1;
        if (!map_code_memory_range( mapping->addr, source, mapping->size, prot, EINVAL ))
        {
            mapping->prot = new_prot;
            return 0;
        }

        saved_errno = errno;
        WARN( "failed to remap writable code memory %p-%p with prot %#x; restoring old permissions.\n",
              mapping->addr, (char *)mapping->addr + mapping->size, prot );
        if (map_code_memory_range( mapping->addr, source, mapping->size, old_prot, EINVAL ))
            WARN( "failed to restore writable code memory %p-%p.\n",
                  mapping->addr, (char *)mapping->addr + mapping->size );
        errno = saved_errno;
        return -1;
    }

    if (set_code_memory_perm( mapping->addr, source, mapping->size, prot, FALSE )) return -1;
    mapping->prot = new_prot;
    return 0;
}

static int unmap_range_locked( void *addr, size_t size )
{
    char *start = addr;
    char *end = start + size;
    struct horizon_mapping *mapping;

    while ((mapping = find_overlap_mapping( start, end - start )))
    {
        char *mapping_start = mapping->addr;
        char *mapping_end = mapping_start + mapping->size;
        char *unmap_start = max( start, mapping_start );
        char *unmap_end = min( end, mapping_end );
        size_t unmap_size = unmap_end - unmap_start;

        if (mapping->reservation)
        {
            if (split_reservation_mapping( mapping, unmap_start, unmap_size )) return -1;
        }
        else if (split_backing_mapping( mapping, unmap_start, unmap_size )) return -1;
    }

    return 0;
}

static int protect_range_locked( void *addr, size_t size, int prot )
{
    char *start = addr;
    char *end = start + size;
    struct horizon_mapping *mapping;

    while (start < end)
    {
        char *mapping_start;
        char *mapping_end;
        char *protect_end;
        size_t protect_size;

        mapping = find_overlap_mapping( start, end - start );
        if (!mapping || mapping->addr > (void *)start)
        {
            errno = ENOMEM;
            return -1;
        }

        mapping_start = mapping->addr;
        mapping_end = mapping_start + mapping->size;
        protect_end = min( end, mapping_end );
        protect_size = protect_end - start;

        if (mapping->reservation)
        {
            if (prot != PROT_NONE)
            {
                horizon_trace( "[HMAP] commit reservation=%p/0x%lx range=%p/0x%lx prot=0x%x",
                               mapping->addr, (unsigned long)mapping->size, start,
                               (unsigned long)protect_size, prot );
                if (split_reservation_mapping( mapping, start, protect_size ))
                {
                    horizon_trace( "[HMAP] split_reservation failed range=%p/0x%lx errno=%d",
                                   start, (unsigned long)protect_size, errno );
                    return -1;
                }
                if (map_backing_at( start, protect_size, prot, -1, 0, MAP_PRIVATE | MAP_ANON, EINVAL ))
                {
                    horizon_trace( "[HMAP] commit map_backing failed range=%p/0x%lx prot=0x%x errno=%d",
                                   start, (unsigned long)protect_size, prot, errno );
                    return -1;
                }
            }
        }
        else
        {
            if (!(mapping = split_backing_mapping_metadata( mapping, start, protect_size ))) return -1;
            if (protect_code_mapping( mapping, prot )) return -1;
        }

        start = protect_end;
    }

    return 0;
}

static int add_reservation_mapping_locked( void *start, size_t size )
{
    VirtmemReservation *reservation = reserve_fixed_range_locked( start, size );
    struct horizon_mapping *mapping;

    if (!reservation) return -1;
    if (!(mapping = alloc_mapping( start, size, NULL, 0, reservation, PROT_NONE )))
    {
        remove_reservation_locked( reservation );
        errno = ENOMEM;
        return -1;
    }
    list_add_mapping( mapping );
    return 0;
}

static void *horizon_mmap_fixed( void *start, size_t size, int prot, int flags, int fd, off_t offset )
{
    BOOL anonymous = fd == -1;

    size = page_align_size( size );
    if (!start || (ULONG_PTR)start & 0xfff || !size)
    {
        errno = EINVAL;
        return MAP_FAILED;
    }

    pthread_mutex_lock( &mapping_mutex );
    if (unmap_range_locked( start, size ))
    {
        pthread_mutex_unlock( &mapping_mutex );
        return MAP_FAILED;
    }

    if (anonymous && prot == PROT_NONE)
    {
        int ret;

        virtmemLock();
        ret = add_reservation_mapping_locked( start, size );
        virtmemUnlock();
        if (ret) start = MAP_FAILED;
    }
    else if (map_backing_at( start, size, prot, fd, offset, flags, EINVAL ))
    {
        start = MAP_FAILED;
    }

    pthread_mutex_unlock( &mapping_mutex );
    return start;
}

void *horizon_anon_mmap_fixed( void *start, size_t size, int prot, int flags )
{
    return horizon_mmap_fixed( start, size, prot, flags, -1, 0 );
}

static void *horizon_mmap_tryfixed( void *start, size_t size, int prot, int flags, int fd, off_t offset )
{
    BOOL anonymous = fd == -1;
    void *ret = start;

    size = page_align_size( size );
    if (!start || (ULONG_PTR)start & 0xfff || !size)
    {
        errno = EINVAL;
        return MAP_FAILED;
    }

    pthread_mutex_lock( &mapping_mutex );
    if (find_overlap_mapping( start, size ))
    {
        errno = EEXIST;
        ret = MAP_FAILED;
    }
    else if (anonymous && prot == PROT_NONE)
    {
        virtmemLock();
        if (add_reservation_mapping_locked( start, size )) ret = MAP_FAILED;
        virtmemUnlock();
    }
    else if (map_backing_at( start, size, prot, fd, offset, flags, EEXIST ))
    {
        ret = MAP_FAILED;
    }
    pthread_mutex_unlock( &mapping_mutex );
    return ret;
}

static void *horizon_mmap_alloc( size_t size, int prot, int flags, int fd, off_t offset )
{
    BOOL anonymous = fd == -1;
    void *addr;
    int ret;

    size = page_align_size( size );
    if (!size)
    {
        errno = EINVAL;
        return MAP_FAILED;
    }

    pthread_mutex_lock( &mapping_mutex );
    virtmemLock();
    addr = virtmemFindCodeMemory( size, 0x1000 );
    if (!addr)
    {
        errno = ENOMEM;
        ret = -1;
    }
    else if (anonymous && prot == PROT_NONE)
    {
        ret = add_reservation_mapping_locked( addr, size );
    }
    else
    {
        ret = map_backing_at_locked( addr, size, prot, fd, offset, flags, ENOMEM );
    }
    virtmemUnlock();
    pthread_mutex_unlock( &mapping_mutex );

    return ret ? MAP_FAILED : addr;
}

void *horizon_anon_mmap_alloc( size_t size, int prot )
{
    return horizon_mmap_alloc( size, prot, MAP_PRIVATE | MAP_ANON, -1, 0 );
}

void *horizon_mmap( void *start, size_t size, int prot, int flags, int fd, off_t offset )
{
    if (flags & MAP_ANON) fd = -1;
    else if (fd == -1)
    {
        errno = EINVAL;
        return MAP_FAILED;
    }

    if (flags & MAP_FIXED) return horizon_mmap_fixed( start, size, prot, flags, fd, offset );
    if (start)
    {
        void *ret = horizon_mmap_tryfixed( start, size, prot, flags, fd, offset );

        if (ret != MAP_FAILED || errno != EEXIST) return ret;
    }
    return horizon_mmap_alloc( size, prot, flags, fd, offset );
}

int horizon_munmap( void *start, size_t size )
{
    int ret;

    size = page_align_size( size );
    if (!start || (ULONG_PTR)start & 0xfff || !size)
    {
        errno = EINVAL;
        return -1;
    }

    pthread_mutex_lock( &mapping_mutex );
    ret = unmap_range_locked( start, size );
    pthread_mutex_unlock( &mapping_mutex );
    return ret;
}

int horizon_mprotect( void *start, size_t size, int prot )
{
    int ret;

    size = page_align_size( size );
    if (!start || (ULONG_PTR)start & 0xfff || !size)
    {
        errno = EINVAL;
        return -1;
    }
    pthread_mutex_lock( &mapping_mutex );
    ret = protect_range_locked( start, size, prot );
    pthread_mutex_unlock( &mapping_mutex );
    return ret;
}

int horizon_madvise( void *start, size_t size, int advice )
{
    TRACE( "madvise(%p, %zu, %#x) ignored.\n", start, size, advice );
    return 0;
}

#endif /* __SWITCH__ */
