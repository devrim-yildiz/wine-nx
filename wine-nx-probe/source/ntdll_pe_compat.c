#include <ctype.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "windef.h"
#include "winbase.h"
#include "winnt.h"
#include "winternl.h"
#include "wine/exception.h"

struct heap_block
{
    SIZE_T size;
};

static WCHAR fold_wchar( WCHAR ch )
{
    if (ch >= 'A' && ch <= 'Z') return ch - 'A' + 'a';
    return ch;
}

static HANDLE process_heap(void)
{
    return NtCurrentTeb()->Peb->ProcessHeap;
}

static SIZE_T strlenW( const WCHAR *str )
{
    const WCHAR *ptr = str;

    while (*ptr) ptr++;
    return ptr - str;
}

static WCHAR *strrchrW( WCHAR *str, WCHAR ch )
{
    WCHAR *ret = NULL;

    for (; *str; str++) if (*str == ch) ret = str;
    return ret;
}

HANDLE WINAPI RtlCreateHeap( ULONG flags, void *addr, SIZE_T total_size, SIZE_T commit_size,
                             void *lock, PRTL_HEAP_PARAMETERS params )
{
    (void)flags;
    (void)addr;
    (void)total_size;
    (void)commit_size;
    (void)lock;
    (void)params;
    return (HANDLE)(uintptr_t)0x1000;
}

void *WINAPI RtlAllocateHeap( HANDLE heap, ULONG flags, SIZE_T size )
{
    struct heap_block *block;

    (void)heap;
    block = (flags & HEAP_ZERO_MEMORY) ? calloc( 1, sizeof(*block) + size )
                                       : malloc( sizeof(*block) + size );
    if (!block) return NULL;
    block->size = size;
    return block + 1;
}

BOOLEAN WINAPI RtlFreeHeap( HANDLE heap, ULONG flags, void *ptr )
{
    (void)heap;
    (void)flags;
    if (ptr) free( ((struct heap_block *)ptr) - 1 );
    return TRUE;
}

void *WINAPI RtlReAllocateHeap( HANDLE heap, ULONG flags, void *ptr, SIZE_T size )
{
    struct heap_block *old_block, *new_block;
    SIZE_T old_size = 0;

    (void)heap;
    if (!ptr) return RtlAllocateHeap( heap, flags, size );
    old_block = ((struct heap_block *)ptr) - 1;
    old_size = old_block->size;
    new_block = realloc( old_block, sizeof(*new_block) + size );
    if (!new_block) return NULL;
    if ((flags & HEAP_ZERO_MEMORY) && size > old_size)
        memset( (char *)(new_block + 1) + old_size, 0, size - old_size );
    new_block->size = size;
    return new_block + 1;
}

SIZE_T WINAPI RtlSizeHeap( HANDLE heap, ULONG flags, const void *ptr )
{
    (void)heap;
    (void)flags;
    if (!ptr) return ~(SIZE_T)0;
    return (((const struct heap_block *)ptr) - 1)->size;
}

NTSTATUS WINAPI RtlEnterCriticalSection( RTL_CRITICAL_SECTION *crit )
{
    (void)crit;
    return STATUS_SUCCESS;
}

NTSTATUS WINAPI RtlLeaveCriticalSection( RTL_CRITICAL_SECTION *crit )
{
    (void)crit;
    return STATUS_SUCCESS;
}

BOOL WINAPI RtlTryEnterCriticalSection( RTL_CRITICAL_SECTION *crit )
{
    (void)crit;
    return TRUE;
}

void WINAPI RtlAcquirePebLock(void)
{
}

void WINAPI RtlReleasePebLock(void)
{
}

void WINAPI RtlInitializeBitMap( RTL_BITMAP *bitmap, ULONG *buffer, ULONG size )
{
    bitmap->SizeOfBitMap = size;
    bitmap->Buffer = buffer;
}

void WINAPI RtlSetBits( RTL_BITMAP *bitmap, ULONG start, ULONG count )
{
    ULONG i;

    for (i = 0; i < count && start + i < bitmap->SizeOfBitMap; i++)
        bitmap->Buffer[(start + i) / 32] |= 1u << ((start + i) & 31);
}

LONG WINAPI RtlCompareUnicodeStrings( const WCHAR *s1, SIZE_T len1,
                                      const WCHAR *s2, SIZE_T len2, BOOLEAN insensitive )
{
    SIZE_T i, min_len = len1 < len2 ? len1 : len2;

    for (i = 0; i < min_len; i++)
    {
        WCHAR c1 = s1[i], c2 = s2[i];
        if (insensitive)
        {
            c1 = fold_wchar( c1 );
            c2 = fold_wchar( c2 );
        }
        if (c1 != c2) return c1 < c2 ? -1 : 1;
    }
    if (len1 == len2) return 0;
    return len1 < len2 ? -1 : 1;
}

BOOLEAN WINAPI RtlEqualUnicodeString( const UNICODE_STRING *s1, const UNICODE_STRING *s2,
                                      BOOLEAN insensitive )
{
    if (s1->Length != s2->Length) return FALSE;
    return !RtlCompareUnicodeStrings( s1->Buffer, s1->Length / sizeof(WCHAR),
                                      s2->Buffer, s2->Length / sizeof(WCHAR), insensitive );
}

NTSTATUS WINAPI RtlHashUnicodeString( const UNICODE_STRING *string, BOOLEAN insensitive,
                                      ULONG algorithm, ULONG *hash )
{
    ULONG value = 5381;
    unsigned int i;

    (void)algorithm;
    for (i = 0; i < string->Length / sizeof(WCHAR); i++)
    {
        WCHAR ch = string->Buffer[i];
        if (insensitive) ch = fold_wchar( ch );
        value = ((value << 5) + value) + ch;
    }
    *hash = value;
    return STATUS_SUCCESS;
}

void WINAPI RtlCopyUnicodeString( UNICODE_STRING *dst, const UNICODE_STRING *src )
{
    USHORT len = src->Length < dst->MaximumLength ? src->Length : dst->MaximumLength;

    if (len) memcpy( dst->Buffer, src->Buffer, len );
    dst->Length = len;
    if (dst->Buffer && dst->MaximumLength >= dst->Length + sizeof(WCHAR))
        dst->Buffer[dst->Length / sizeof(WCHAR)] = 0;
}

NTSTATUS WINAPI RtlAppendUnicodeToString( UNICODE_STRING *dst, const WCHAR *src )
{
    SIZE_T src_len = strlenW( src ) * sizeof(WCHAR);

    if (dst->Length + src_len + sizeof(WCHAR) > dst->MaximumLength)
        return STATUS_BUFFER_TOO_SMALL;
    memcpy( (char *)dst->Buffer + dst->Length, src, src_len + sizeof(WCHAR) );
    dst->Length += src_len;
    return STATUS_SUCCESS;
}

NTSTATUS WINAPI RtlDuplicateUnicodeString( int flags, const UNICODE_STRING *src, UNICODE_STRING *dst )
{
    SIZE_T size = src->Length + ((flags & 1) ? sizeof(WCHAR) : 0);

    dst->Length = src->Length;
    dst->MaximumLength = size;
    dst->Buffer = RtlAllocateHeap( process_heap(), 0, size ? size : sizeof(WCHAR) );
    if (!dst->Buffer) return STATUS_NO_MEMORY;
    if (src->Length) memcpy( dst->Buffer, src->Buffer, src->Length );
    if (flags & 1) dst->Buffer[src->Length / sizeof(WCHAR)] = 0;
    return STATUS_SUCCESS;
}

void WINAPI RtlFreeUnicodeString( UNICODE_STRING *str )
{
    if (str && str->Buffer) RtlFreeHeap( process_heap(), 0, str->Buffer );
    if (str) memset( str, 0, sizeof(*str) );
}

BOOLEAN WINAPI RtlCreateUnicodeStringFromAsciiz( UNICODE_STRING *dst, const char *src )
{
    SIZE_T i, len = strlen( src );

    dst->Buffer = RtlAllocateHeap( process_heap(), 0, (len + 1) * sizeof(WCHAR) );
    if (!dst->Buffer) return FALSE;
    for (i = 0; i < len; i++) dst->Buffer[i] = (unsigned char)src[i];
    dst->Buffer[len] = 0;
    dst->Length = len * sizeof(WCHAR);
    dst->MaximumLength = (len + 1) * sizeof(WCHAR);
    return TRUE;
}

void WINAPI RtlInitAnsiString( ANSI_STRING *dst, const char *src )
{
    SIZE_T len = src ? strlen( src ) : 0;

    dst->Length = len;
    dst->MaximumLength = src ? len + 1 : 0;
    dst->Buffer = (char *)src;
}

NTSTATUS WINAPI RtlUnicodeStringToAnsiString( ANSI_STRING *dst, const UNICODE_STRING *src,
                                              BOOLEAN allocate )
{
    SIZE_T i, len = src->Length / sizeof(WCHAR);

    if (allocate)
    {
        dst->Buffer = RtlAllocateHeap( process_heap(), 0, len + 1 );
        if (!dst->Buffer) return STATUS_NO_MEMORY;
        dst->MaximumLength = len + 1;
    }
    else if (dst->MaximumLength <= len) return STATUS_BUFFER_TOO_SMALL;

    for (i = 0; i < len; i++) dst->Buffer[i] = src->Buffer[i] < 0x80 ? src->Buffer[i] : '?';
    dst->Buffer[len] = 0;
    dst->Length = len;
    return STATUS_SUCCESS;
}

void WINAPI RtlFreeAnsiString( ANSI_STRING *str )
{
    if (str && str->Buffer) RtlFreeHeap( process_heap(), 0, str->Buffer );
    if (str) memset( str, 0, sizeof(*str) );
}

RTL_PATH_TYPE WINAPI RtlDetermineDosPathNameType_U( const WCHAR *path )
{
    if (!path || !path[0]) return RtlPathTypeRelative;
    if ((path[0] == '\\' || path[0] == '/') && (path[1] == '\\' || path[1] == '/'))
        return RtlPathTypeLocalDevice;
    if (path[0] && path[1] == ':')
        return (path[2] == '\\' || path[2] == '/') ? RtlPathTypeDriveAbsolute : RtlPathTypeDriveRelative;
    if (path[0] == '\\' || path[0] == '/') return RtlPathTypeRooted;
    return RtlPathTypeRelative;
}

NTSTATUS WINAPI RtlDosPathNameToNtPathName_U_WithStatus( const WCHAR *dos_path,
                                                         UNICODE_STRING *nt_path,
                                                         WCHAR **file_part,
                                                         CURDIR *relative )
{
    static const WCHAR prefix[] = {'\\','?','?','\\',0};
    SIZE_T len = strlenW( dos_path );
    SIZE_T prefix_len = 0;
    WCHAR *slash;

    (void)relative;
    if (dos_path[0] == '\\' && dos_path[1] == '?' && dos_path[2] == '?' && dos_path[3] == '\\')
        prefix_len = 0;
    else prefix_len = 4;

    nt_path->MaximumLength = (prefix_len + len + 1) * sizeof(WCHAR);
    nt_path->Buffer = RtlAllocateHeap( process_heap(), 0, nt_path->MaximumLength );
    if (!nt_path->Buffer) return STATUS_NO_MEMORY;
    if (prefix_len) memcpy( nt_path->Buffer, prefix, prefix_len * sizeof(WCHAR) );
    memcpy( nt_path->Buffer + prefix_len, dos_path, (len + 1) * sizeof(WCHAR) );
    nt_path->Length = (prefix_len + len) * sizeof(WCHAR);

    if (file_part)
    {
        slash = strrchrW( nt_path->Buffer, '\\' );
        *file_part = slash ? slash + 1 : nt_path->Buffer;
    }
    return STATUS_SUCCESS;
}

NTSTATUS WINAPI RtlQueryEnvironmentVariable( WCHAR *env, const WCHAR *name, SIZE_T name_len,
                                             WCHAR *value, SIZE_T value_len, SIZE_T *ret_len )
{
    (void)env;
    (void)name;
    (void)name_len;
    (void)value;
    (void)value_len;
    if (ret_len) *ret_len = 0;
    return STATUS_VARIABLE_NOT_FOUND;
}

NTSTATUS WINAPI RtlQueryEnvironmentVariable_U( WCHAR *env, UNICODE_STRING *name,
                                               UNICODE_STRING *value )
{
    (void)env;
    (void)name;
    if (value) value->Length = 0;
    return STATUS_VARIABLE_NOT_FOUND;
}

NTSTATUS WINAPI RtlExpandEnvironmentStrings( const WCHAR *env, WCHAR *src, SIZE_T src_len,
                                             WCHAR *dst, SIZE_T dst_len, SIZE_T *ret_len )
{
    (void)env;
    if (ret_len) *ret_len = src_len + 1;
    if (dst && dst_len)
    {
        SIZE_T copy = src_len < dst_len - 1 ? src_len : dst_len - 1;
        memcpy( dst, src, copy * sizeof(WCHAR) );
        dst[copy] = 0;
    }
    return STATUS_SUCCESS;
}

void WINAPI RtlRbInsertNodeEx( RTL_RB_TREE *tree, RTL_BALANCED_NODE *parent,
                               BOOLEAN right, RTL_BALANCED_NODE *entry )
{
    entry->Children[0] = entry->Children[1] = NULL;
    entry->ParentValue = (ULONG_PTR)parent;
    if (parent) parent->Children[right != FALSE] = entry;
    else tree->root = entry;
    if (!tree->min || !right) tree->min = entry;
}

void WINAPI RtlRbRemoveNode( RTL_RB_TREE *tree, RTL_BALANCED_NODE *entry )
{
    (void)tree;
    (void)entry;
}

ULONG WINAPI RtlRandom( ULONG *seed )
{
    *seed = *seed * 1103515245 + 12345;
    return (*seed >> 16) & 0x7fff;
}

NTSTATUS WINAPI RtlActivateActivationContext( DWORD flags, struct _ACTIVATION_CONTEXT *ctx,
                                              ULONG_PTR *cookie )
{
    (void)flags;
    (void)ctx;
    if (cookie) *cookie = 0;
    return STATUS_SUCCESS;
}

void WINAPI RtlDeactivateActivationContext( DWORD flags, ULONG_PTR cookie )
{
    (void)flags;
    (void)cookie;
}

NTSTATUS WINAPI RtlCreateActivationContext( struct _ACTIVATION_CONTEXT **ctx, const void *ptr )
{
    (void)ctx;
    (void)ptr;
    return STATUS_SXS_KEY_NOT_FOUND;
}

void WINAPI RtlReleaseActivationContext( struct _ACTIVATION_CONTEXT *ctx )
{
    (void)ctx;
}

NTSTATUS WINAPI RtlFindActivationContextSectionString( ULONG flags, const GUID *guid,
                                                       ULONG section, const UNICODE_STRING *name,
                                                       void *data )
{
    (void)flags;
    (void)guid;
    (void)section;
    (void)name;
    (void)data;
    return STATUS_SXS_KEY_NOT_FOUND;
}

NTSTATUS WINAPI RtlQueryInformationActivationContext( ULONG flags, struct _ACTIVATION_CONTEXT *ctx,
                                                      void *subinst, ULONG class, void *buffer,
                                                      SIZE_T size, SIZE_T *needed )
{
    (void)flags;
    (void)ctx;
    (void)subinst;
    (void)class;
    (void)buffer;
    if (needed) *needed = size;
    return STATUS_SXS_KEY_NOT_FOUND;
}

NTSTATUS WINAPI LdrFindResource_U( HMODULE module, const LDR_RESOURCE_INFO *info,
                                   ULONG level, const IMAGE_RESOURCE_DATA_ENTRY **entry )
{
    (void)module;
    (void)info;
    (void)level;
    (void)entry;
    return STATUS_RESOURCE_DATA_NOT_FOUND;
}

BOOL WINAPI IsBadStringPtrW( const WCHAR *str, UINT_PTR max )
{
    (void)str;
    (void)max;
    return FALSE;
}

NTSTATUS WINAPI RtlWow64EnableFsRedirectionEx( ULONG disable, ULONG *old_value )
{
    (void)disable;
    if (old_value) *old_value = 0;
    return STATUS_SUCCESS;
}

void WINAPI RtlRaiseException( EXCEPTION_RECORD *record )
{
    (void)record;
}

void register_module_exception_directory( void *module )
{
    (void)module;
}

void unregister_module_exception_directory( void *module )
{
    (void)module;
}

int __cdecl __wine_setjmpex( __wine_jmp_buf *buf, EXCEPTION_REGISTRATION_RECORD *frame )
{
    (void)buf;
    (void)frame;
    return 0;
}

DWORD __cdecl __wine_exception_handler_page_fault( EXCEPTION_RECORD *record,
                                                   EXCEPTION_REGISTRATION_RECORD *frame,
                                                   CONTEXT *context,
                                                   EXCEPTION_REGISTRATION_RECORD **dispatcher )
{
    (void)record;
    (void)frame;
    (void)context;
    (void)dispatcher;
    return ExceptionContinueSearch;
}

DWORD __cdecl __wine_exception_handler_all( EXCEPTION_RECORD *record,
                                            EXCEPTION_REGISTRATION_RECORD *frame,
                                            CONTEXT *context,
                                            EXCEPTION_REGISTRATION_RECORD **dispatcher )
{
    (void)record;
    (void)frame;
    (void)context;
    (void)dispatcher;
    return ExceptionContinueSearch;
}

void WINAPI LdrInitializeThunk( CONTEXT *context, ULONG_PTR unk2, ULONG_PTR unk3, ULONG_PTR unk4 )
{
    (void)context;
    (void)unk2;
    (void)unk3;
    (void)unk4;
}
