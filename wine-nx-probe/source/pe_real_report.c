#include <switch.h>

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <locale.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "horizon_mman.h"

#ifndef PE_REAL_EXECUTE
#define PE_REAL_EXECUTE 0
#endif

u32 __nx_applet_type = AppletType_Application;
size_t __nx_heap_size = 256 * 1024 * 1024;
unsigned char __attribute__((aligned(16))) __nx_exception_stack[0x10000];
uint64_t __nx_exception_stack_size = sizeof(__nx_exception_stack);

#define PE_MACHINE_ARM64 0xaa64
#define PE_MAGIC_PE32_PLUS 0x20b
#define PAGE_SIZE 0x1000
#define MAX_DATA_DIRS 16
#define IMAGE_REL_BASED_ABSOLUTE 0
#define IMAGE_REL_BASED_DIR64 10
#define DLL_PROCESS_ATTACH 1
#define PAGE_READONLY_WIN 0x02
#define PAGE_READWRITE_WIN 0x04
#define PAGE_EXECUTE_READ_WIN 0x20
#define MEM_COMMIT_WIN 0x1000
#define MEM_PRIVATE_WIN 0x20000
#define PE_MAX_ARGS 32
#define PE_ARGS_BUFFER_SIZE 1024
#define PE_TARGET_BUFFER_SIZE 256
#define WIN_INVALID_HANDLE_VALUE ((void *)(intptr_t)-1)
#define WIN_STD_INPUT_HANDLE ((uint32_t)-10)
#define WIN_STD_OUTPUT_HANDLE ((uint32_t)-11)
#define WIN_STD_ERROR_HANDLE ((uint32_t)-12)
#define WIN_FILE_TYPE_CHAR 0x0002
#define WIN_FILE_TYPE_DISK 0x0001
#define WIN_FILE_ATTRIBUTE_NORMAL 0x80
#define WIN_WAIT_OBJECT_0 0
#define WIN_WAIT_TIMEOUT 258
#define WIN_INFINITE 0xffffffffu
#define WIN_ERROR_CALL_NOT_IMPLEMENTED 120
#define WIN_ERROR_INVALID_PARAMETER 87
#define WIN_ERROR_FILE_NOT_FOUND 2
#define WIN_ERROR_INVALID_HANDLE 6
#define WIN_ERROR_INSUFFICIENT_BUFFER 122
#define WIN_ERROR_NO_MORE_FILES 18
#define WIN_ERROR_NOT_ENOUGH_MEMORY 8
#define WIN_ERROR_NOT_SUPPORTED 50
#define WIN_ERROR_SUCCESS 0
#define WIN_FILE_ATTRIBUTE_DIRECTORY 0x10
#define WIN_MAX_PATH 260
#define WIN_FORMAT_MESSAGE_ALLOCATE_BUFFER 0x00000100
#define WIN_WAIT_FAILED 0xffffffffu
#define WIN_TIME_ZONE_ID_UNKNOWN 0
#define WIN_INVALID_SOCKET (~(uintptr_t)0)
#define WIN_SOCKET_ERROR (-1)
#define WIN_EAI_NONAME 11001
#define WIN_EAI_AGAIN 11002
#define WIN_EAI_FAIL 11003
#define WIN_EAI_MEMORY 11006
#define WIN_AF_INET6 23
#define WIN_FD_SETSIZE 64
#define WIN_FIONBIO 0x8004667eUL
#define WIN_FIONREAD 0x4004667fUL
#define WSAEINTR 10004
#define WSAEBADF 10009
#define WSAEACCES 10013
#define WSAEFAULT 10014
#define WSAEINVAL 10022
#define WSAEMFILE 10024
#define WSAEWOULDBLOCK 10035
#define WSAEINPROGRESS 10036
#define WSAEALREADY 10037
#define WSAENOTSOCK 10038
#define WSAEDESTADDRREQ 10039
#define WSAEMSGSIZE 10040
#define WSAEPROTOTYPE 10041
#define WSAENOPROTOOPT 10042
#define WSAEPROTONOSUPPORT 10043
#define WSAESOCKTNOSUPPORT 10044
#define WSAEOPNOTSUPP 10045
#define WSAEAFNOSUPPORT 10047
#define WSAEADDRINUSE 10048
#define WSAEADDRNOTAVAIL 10049
#define WSAENETDOWN 10050
#define WSAENETUNREACH 10051
#define WSAENETRESET 10052
#define WSAECONNABORTED 10053
#define WSAECONNRESET 10054
#define WSAENOBUFS 10055
#define WSAEISCONN 10056
#define WSAENOTCONN 10057
#define WSAETIMEDOUT 10060
#define WSAECONNREFUSED 10061
#define WSAEHOSTUNREACH 10065

enum pe_directory
{
    DIR_EXPORT = 0,
    DIR_IMPORT = 1,
    DIR_EXCEPTION = 3,
    DIR_RELOC = 5,
    DIR_TLS = 9,
    DIR_LOAD_CONFIG = 10,
    DIR_IAT = 12,
    DIR_DELAY_IMPORT = 13,
};

struct pe_image
{
    const unsigned char *file;
    size_t file_size;
    uint32_t pe_offset;
    uint32_t coff_offset;
    uint32_t opt_offset;
    uint32_t section_offset;
    uint16_t optional_size;
    uint16_t section_count;
    uint32_t entry_rva;
    uint64_t image_base;
    uint32_t image_size;
    uint32_t header_size;
    uint32_t section_alignment;
    uint32_t file_alignment;
    uint32_t dir_rva[MAX_DATA_DIRS];
    uint32_t dir_size[MAX_DATA_DIRS];
};

struct run_report
{
    unsigned int dll_count;
    unsigned int import_count;
    unsigned int unsupported_import_count;
    unsigned int ordinal_import_count;
    unsigned int reloc_count;
    unsigned int unsupported_reloc_count;
    unsigned int tls_callback_count;
    unsigned int run_blockers;
    int has_tls;
    int has_load_config;
    int has_delay_imports;
};

static FILE *log_file;
static int loader_failures;

#if PE_REAL_EXECUTE
struct memory_basic_information_win
{
    void *BaseAddress;
    void *AllocationBase;
    uint32_t AllocationProtect;
    uint32_t __alignment1;
    size_t RegionSize;
    uint32_t State;
    uint32_t Protect;
    uint32_t Type;
    uint32_t __alignment2;
};

static jmp_buf pe_exit_jmp;
static int pe_exit_active;
static int pe_exit_code;
static int pe_argc;
static char pe_arg_storage[PE_ARGS_BUFFER_SIZE];
static char *pe_argv[PE_MAX_ARGS + 1];
static char *pe_envp[] = { NULL };
static char **pe_argv_value = pe_argv;
static char **pe_envp_value = pe_envp;
static char pe_target_path[PE_TARGET_BUFFER_SIZE];
static int pe_errno_value;
static int pe_commode;
static int pe_fmode;
static int pe_last_error;
static Thread *pe_main_thread;
static void *pe_stdout_handle = (void *)1;
static void *pe_stderr_handle = (void *)2;
static void *pe_stdin_handle = (void *)3;
static int pe_socket_initialized;
static unsigned char *loaded_image;
static uint32_t loaded_image_size;
static void *pe_tls_slots[64];
static unsigned char pe_fake_teb[0x1000] __attribute__((aligned(16)));
static int peout_at_line_start = 1;
#endif

static const char *default_paths[] =
{
    "sdmc:/switch/wine/drive_c/curl/curl.exe",
    "sdmc:/switch/wine/drive_c/curl/trurl.exe",
    "sdmc:/switch/wine/curl.exe",
    "sdmc:/switch/wine/trurl.exe",
};

#if PE_REAL_EXECUTE
static const char *target_paths[] =
{
    "sdmc:/switch/wine/target.txt",
};

static const char *args_paths[] =
{
    "sdmc:/switch/wine/args.txt",
    "sdmc:/switch/wine/drive_c/curl/curl.args",
    "sdmc:/switch/wine/drive_c/curl/trurl.args",
};

static const char default_args[] =
    "curl.exe --version";
#endif

static void log_line( const char *fmt, ... )
{
    va_list args;
    int main_thread = 1;

#if PE_REAL_EXECUTE
    main_thread = !pe_main_thread || threadGetSelf() == pe_main_thread;
#endif

    if (main_thread)
    {
        va_start( args, fmt );
        vprintf( fmt, args );
        va_end( args );
        putchar( '\n' );
    }

    if (log_file)
    {
        va_start( args, fmt );
        vfprintf( log_file, fmt, args );
        va_end( args );
        fputc( '\n', log_file );
        fflush( log_file );
    }
    if (main_thread) consoleUpdate( NULL );
}

static int fail_line( const char *fmt, ... )
{
    va_list args;
    int main_thread = 1;

    loader_failures++;

#if PE_REAL_EXECUTE
    main_thread = !pe_main_thread || threadGetSelf() == pe_main_thread;
#endif

    if (main_thread)
    {
        va_start( args, fmt );
        printf( "[FAIL] " );
        vprintf( fmt, args );
        va_end( args );
        putchar( '\n' );
    }

    if (log_file)
    {
        va_start( args, fmt );
        fprintf( log_file, "[FAIL] " );
        vfprintf( log_file, fmt, args );
        va_end( args );
        fputc( '\n', log_file );
        fflush( log_file );
    }
    if (main_thread) consoleUpdate( NULL );
    return 0;
}

#if PE_REAL_EXECUTE
static void *resolve_real_import( const char *dll, const char *name );
static uint32_t align_up_u32( uint32_t value, uint32_t align );
static uintptr_t call_pe_function3( void *func, uintptr_t arg0, uintptr_t arg1, uintptr_t arg2 );
static int pe_ensure_socket(void);
static void pe_thread_entry( void *arg );
static void put64( unsigned char *ptr, uint64_t value );
static void log_pe_output( const void *buffer, size_t bytes );

#define PE_SYNC_TABLE_SIZE 128

struct pe_critical_section_entry
{
    void *key;
    Mutex mutex;
    Thread *owner;
    uint32_t recursion;
};

struct pe_condition_variable_entry
{
    void *key;
    CondVar cond;
};

static struct pe_critical_section_entry *pe_get_critical_section( void *key, int create );
static void pe_delete_critical_section( void *key );
static struct pe_condition_variable_entry *pe_get_condition_variable( void *key, int create );

static void pe_exit_now( int code )
{
    pe_exit_code = code;
    log_line( "[EXITIMP] process exit code=%d", code );
    if (pe_exit_active) longjmp( pe_exit_jmp, 1 );
    for (;;) svcSleepThread( 1000000000LL );
}

static int win_protect_to_horizon( uint32_t protect )
{
    switch (protect & 0xff)
    {
    case PAGE_READONLY_WIN:
        return PROT_READ;
    case PAGE_READWRITE_WIN:
        return PROT_READ | PROT_WRITE;
    case PAGE_EXECUTE_READ_WIN:
        return PROT_READ | PROT_EXEC;
    default:
        return PROT_READ | PROT_WRITE;
    }
}

static void *shim___acrt_iob_func( unsigned int index )
{
    if (index == 0) return stdin;
    if (index == 1) return stdout;
    return stderr;
}

static int *shim___p__commode(void) { return &pe_commode; }
static int *shim___p__fmode(void) { return &pe_fmode; }
static int *shim___p___argc(void) { return &pe_argc; }
static char ***shim___p___argv(void) { return &pe_argv_value; }
static char ***shim___p__environ(void) { return &pe_envp_value; }
static int *shim__errno(void) { return &pe_errno_value; }
static int shim___C_specific_handler(void) { return 0; }
static int shim___setusermatherr(void *handler) { (void)handler; return 0; }
static void shim__fpreset(void) {}
static int shim__configure_narrow_argv(int mode) { (void)mode; return 0; }
static int shim__initialize_narrow_environment(void) { return 0; }
static int shim__configthreadlocale(int mode) { (void)mode; return 0; }
static int shim__set_app_type(int type) { (void)type; return 0; }
static int shim__set_new_mode(int mode) { (void)mode; return 0; }
static void *shim__set_invalid_parameter_handler(void *handler) { (void)handler; return NULL; }
static int shim__crt_atexit(void (*func)(void)) { (void)func; return 0; }
static void shim__cexit(void) {}
static void shim_abort(void) { pe_exit_now( 3 ); }
static void shim_exit(int code) { pe_exit_now( code ); }
static void shim__exit(int code) { pe_exit_now( code ); }
static void *shim_signal(int sig, void *handler) { (void)sig; (void)handler; return NULL; }

static void shim__initterm( void (**first)(void), void (**last)(void) )
{
    while (first && first < last)
    {
        if (*first) (*first)();
        first++;
    }
}

static int shim__initterm_e( int (**first)(void), int (**last)(void) )
{
    while (first && first < last)
    {
        int ret;

        if (*first)
        {
            ret = (*first)();
            if (ret) return ret;
        }
        first++;
    }
    return 0;
}

static uintptr_t win_va_next( uintptr_t **cursor )
{
    uintptr_t value = **cursor;
    (*cursor)++;
    return value;
}

static void append_char_counted( char **buffer, size_t *remaining, int *total, char ch )
{
    if (*remaining > 1)
    {
        **buffer = ch;
        (*buffer)++;
        (*remaining)--;
    }
    (*total)++;
}

static void append_text_counted( char **buffer, size_t *remaining, int *total,
                                 const char *text, size_t len )
{
    size_t copy;

    if (!text) text = "(null)";
    if (len == (size_t)-1) len = strlen( text );
    copy = len;
    if (copy >= *remaining) copy = *remaining ? *remaining - 1 : 0;
    if (copy)
    {
        memcpy( *buffer, text, copy );
        *buffer += copy;
        *remaining -= copy;
    }
    *total += (int)len;
}

static void append_win_wide_counted( char **buffer, size_t *remaining, int *total,
                                     const uint16_t *text, int precision )
{
    size_t i;

    if (!text)
    {
        append_text_counted( buffer, remaining, total, "(null)", (size_t)-1 );
        return;
    }

    for (i = 0; text[i] && (precision < 0 || (int)i < precision); i++)
    {
        char ch = text[i] < 0x80 ? (char)text[i] : '?';
        append_char_counted( buffer, remaining, total, ch );
    }
}

static size_t bounded_strlen( const char *text, size_t max_len )
{
    size_t len;

    if (!text) return 0;
    for (len = 0; len < max_len && text[len]; len++);
    return len;
}

static int format_from_win_va( char *out, size_t out_size, const char *format, void *args )
{
    const char *src = format ? format : "";
    uintptr_t *cursor = (uintptr_t *)args;
    char *dst = out;
    size_t remaining = out_size;
    int total = 0;

    if (!out || !out_size) return 0;

    while (*src)
    {
        int precision = -1;
        int long_count = 0;
        int size_t_arg = 0;
        char temp[128];
        const char *start;

        if (*src != '%' || !cursor)
        {
            append_char_counted( &dst, &remaining, &total, *src++ );
            continue;
        }

        start = src++;
        if (*src == '%')
        {
            append_char_counted( &dst, &remaining, &total, *src++ );
            continue;
        }

        while (*src == '-' || *src == '+' || *src == ' ' || *src == '#' || *src == '0') src++;
        if (*src == '*')
        {
            (void)win_va_next( &cursor );
            src++;
        }
        else while (*src >= '0' && *src <= '9') src++;

        if (*src == '.')
        {
            src++;
            precision = 0;
            if (*src == '*')
            {
                precision = (int)win_va_next( &cursor );
                src++;
            }
            else
            {
                while (*src >= '0' && *src <= '9')
                {
                    precision = precision * 10 + (*src - '0');
                    src++;
                }
            }
        }

        if (*src == 'I' && src[1] == '6' && src[2] == '4')
        {
            long_count = 2;
            src += 3;
        }
        else if (*src == 'I')
        {
            size_t_arg = 1;
            src++;
        }
        else if (*src == 'z')
        {
            size_t_arg = 1;
            src++;
        }
        else
        {
            while (*src == 'l')
            {
                long_count++;
                src++;
            }
            while (*src == 'h') src++;
        }

        switch (*src)
        {
        case 's':
            if (long_count)
                append_win_wide_counted( &dst, &remaining, &total,
                                         (const uint16_t *)win_va_next( &cursor ), precision );
            else
            {
                const char *text = (const char *)win_va_next( &cursor );
                append_text_counted( &dst, &remaining, &total, text,
                                     precision >= 0 ? bounded_strlen( text ? text : "(null)", precision ) : (size_t)-1 );
            }
            src++;
            break;
        case 'S':
            append_win_wide_counted( &dst, &remaining, &total,
                                     (const uint16_t *)win_va_next( &cursor ), precision );
            src++;
            break;
        case 'c':
        {
            char ch = (char)win_va_next( &cursor );
            append_char_counted( &dst, &remaining, &total, ch );
            src++;
            break;
        }
        case 'd':
        case 'i':
            if (long_count >= 2 || size_t_arg)
                snprintf( temp, sizeof(temp), "%lld", (long long)win_va_next( &cursor ) );
            else
                snprintf( temp, sizeof(temp), "%d", (int)win_va_next( &cursor ) );
            append_text_counted( &dst, &remaining, &total, temp, (size_t)-1 );
            src++;
            break;
        case 'u':
            if (long_count >= 2 || size_t_arg)
                snprintf( temp, sizeof(temp), "%llu", (unsigned long long)win_va_next( &cursor ) );
            else
                snprintf( temp, sizeof(temp), "%u", (unsigned int)win_va_next( &cursor ) );
            append_text_counted( &dst, &remaining, &total, temp, (size_t)-1 );
            src++;
            break;
        case 'x':
        case 'X':
            if (long_count >= 2 || size_t_arg)
                snprintf( temp, sizeof(temp), *src == 'X' ? "%llX" : "%llx",
                          (unsigned long long)win_va_next( &cursor ) );
            else
                snprintf( temp, sizeof(temp), *src == 'X' ? "%X" : "%x",
                          (unsigned int)win_va_next( &cursor ) );
            append_text_counted( &dst, &remaining, &total, temp, (size_t)-1 );
            src++;
            break;
        case 'o':
            if (long_count >= 2 || size_t_arg)
                snprintf( temp, sizeof(temp), "%llo", (unsigned long long)win_va_next( &cursor ) );
            else
                snprintf( temp, sizeof(temp), "%o", (unsigned int)win_va_next( &cursor ) );
            append_text_counted( &dst, &remaining, &total, temp, (size_t)-1 );
            src++;
            break;
        case 'f':
        case 'F':
        case 'e':
        case 'E':
        case 'g':
        case 'G':
        {
            union
            {
                uint64_t bits;
                double value;
            } fp;
            fp.bits = (uint64_t)win_va_next( &cursor );
            snprintf( temp, sizeof(temp), "%g", fp.value );
            append_text_counted( &dst, &remaining, &total, temp, (size_t)-1 );
            src++;
            break;
        }
        case 'p':
            snprintf( temp, sizeof(temp), "%p", (void *)win_va_next( &cursor ) );
            append_text_counted( &dst, &remaining, &total, temp, (size_t)-1 );
            src++;
            break;
        default:
            append_text_counted( &dst, &remaining, &total, start, (size_t)(src - start) );
            if (*src) append_char_counted( &dst, &remaining, &total, *src++ );
            break;
        }
    }

    *dst = 0;
    return total;
}

static int shim___stdio_common_vfprintf( uint64_t options, FILE *stream, const char *format,
                                         void *locale, void *args )
{
    char buffer[4096];
    int len;

    (void)options;
    (void)locale;
    if (!stream) stream = stdout;
    len = format_from_win_va( buffer, sizeof(buffer), format, args );
    fputs( buffer, stream );
    fflush( stream );
    if (stream == stdout || stream == stderr)
        log_pe_output( buffer, strlen( buffer ) );
    return len;
}

static int shim___stdio_common_vsprintf( uint64_t options, char *buffer, size_t size,
                                         const char *format, void *locale, void *args )
{
    int len;

    (void)options;
    (void)locale;
    if (!buffer || !size) return -1;
    len = format_from_win_va( buffer, size, format, args );
    return len;
}

static void log_pe_output( const void *buffer, size_t bytes )
{
    const char *ptr = buffer;
    size_t i;

    if (!log_file || !buffer || !bytes) return;

    for (i = 0; i < bytes; i++)
    {
        if (peout_at_line_start)
        {
            fputs( "[PEOUT] ", log_file );
            peout_at_line_start = 0;
        }

        fputc( ptr[i], log_file );
        if (ptr[i] == '\n') peout_at_line_start = 1;
    }
    fflush( log_file );
}

static int shim__write( int fd, const void *buffer, unsigned int count )
{
    FILE *stream = fd == 2 ? stderr : stdout;
    int ret = (int)fwrite( buffer, 1, count, stream );

    log_pe_output( buffer, count );
    return ret;
}

static int shim_fputc( int ch, FILE *stream )
{
    int ret = fputc( ch, stream );

    if (stream == stdout || stream == stderr)
    {
        char out = ch;
        log_pe_output( &out, 1 );
    }
    return ret;
}

static int shim_fputs( const char *string, FILE *stream )
{
    int ret = fputs( string, stream );

    if (string && (stream == stdout || stream == stderr))
        log_pe_output( string, strlen( string ) );
    return ret;
}

static size_t shim_fwrite( const void *buffer, size_t size, size_t count, FILE *stream )
{
    size_t ret = fwrite( buffer, size, count, stream );
    size_t bytes = size * count;

    if (stream == stdout || stream == stderr)
        log_pe_output( buffer, bytes );
    return ret;
}

static int shim_putchar( int ch )
{
    return shim_fputc( ch, stdout );
}

static int shim_puts( const char *string )
{
    int ret = puts( string );

    if (string) log_pe_output( string, strlen( string ) );
    log_pe_output( "\n", 1 );
    return ret;
}

static char *shim__strdup( const char *string )
{
    size_t len;
    char *copy;

    if (!string) return NULL;
    len = strlen( string ) + 1;
    copy = malloc( len );
    if (copy) memcpy( copy, string, len );
    return copy;
}

static char *shim_setlocale( int category, const char *locale_name )
{
    char *ret = setlocale( category, locale_name );
    return ret ? ret : (char *)"C";
}

static void shim_AcquireSRWLockExclusive(void *lock) { (void)lock; }
static void shim_ReleaseSRWLockExclusive(void *lock) { (void)lock; }
static void shim_InitializeCriticalSection( void *section )
{
    pe_get_critical_section( section, 1 );
}

static void shim_DeleteCriticalSection( void *section )
{
    pe_delete_critical_section( section );
}

static void shim_EnterCriticalSection( void *section )
{
    struct pe_critical_section_entry *entry = pe_get_critical_section( section, 1 );
    Thread *self = threadGetSelf();

    if (!entry) return;
    if (entry->owner == self)
    {
        entry->recursion++;
        return;
    }
    mutexLock( &entry->mutex );
    entry->owner = self;
    entry->recursion = 1;
}

static void shim_LeaveCriticalSection( void *section )
{
    struct pe_critical_section_entry *entry = pe_get_critical_section( section, 0 );
    Thread *self = threadGetSelf();

    if (!entry || entry->owner != self || !entry->recursion) return;
    if (--entry->recursion) return;
    entry->owner = NULL;
    mutexUnlock( &entry->mutex );
}
static void *shim_GetCurrentProcess(void) { return (void *)(intptr_t)-1; }
static uint32_t shim_GetLastError(void) { return (uint32_t)pe_last_error; }
static void *shim_GetModuleHandleA(const char *name) { (void)name; return loaded_image; }

static void *shim_GetProcAddress( void *module, const char *name )
{
    (void)module;
    return resolve_real_import( NULL, name );
}

static int shim_IsProcessorFeaturePresent( uint32_t feature )
{
    (void)feature;
    return 0;
}

static int shim_QueryPerformanceFrequency( int64_t *frequency )
{
    if (frequency) *frequency = 1000000000LL;
    return 1;
}

static void shim_Sleep( uint32_t milliseconds )
{
    svcSleepThread( (int64_t)milliseconds * 1000000LL );
}

static int shim_TerminateProcess( void *process, unsigned int code )
{
    (void)process;
    pe_exit_now( (int)code );
    return 1;
}

static void *shim_TlsGetValue( uint32_t index )
{
    if (index >= sizeof(pe_tls_slots) / sizeof(pe_tls_slots[0]))
    {
        pe_last_error = 87;
        return NULL;
    }
    pe_last_error = 0;
    return pe_tls_slots[index];
}

static int shim_VirtualProtect( void *address, size_t size, uint32_t new_protect, uint32_t *old_protect )
{
    uintptr_t start = (uintptr_t)address & ~(uintptr_t)(PAGE_SIZE - 1);
    uintptr_t end = ((uintptr_t)address + size + PAGE_SIZE - 1) & ~(uintptr_t)(PAGE_SIZE - 1);

    if (old_protect) *old_protect = PAGE_READWRITE_WIN;
    if (!address || !size || end <= start)
    {
        pe_last_error = 87;
        return 0;
    }
    if (horizon_mprotect( (void *)start, end - start, win_protect_to_horizon( new_protect ) ) == -1)
    {
        pe_last_error = errno;
        return 0;
    }
    pe_last_error = 0;
    return 1;
}

static size_t shim_VirtualQuery( const void *address, struct memory_basic_information_win *info, size_t size )
{
    uintptr_t addr = (uintptr_t)address;
    uintptr_t base = (uintptr_t)loaded_image;

    if (!info || size < sizeof(*info) || !loaded_image || addr < base || addr >= base + loaded_image_size)
    {
        pe_last_error = 87;
        return 0;
    }

    memset( info, 0, sizeof(*info) );
    info->BaseAddress = loaded_image;
    info->AllocationBase = loaded_image;
    info->AllocationProtect = PAGE_READWRITE_WIN;
    info->RegionSize = loaded_image_size;
    info->State = MEM_COMMIT_WIN;
    info->Protect = PAGE_READWRITE_WIN;
    info->Type = MEM_PRIVATE_WIN;
    pe_last_error = 0;
    return sizeof(*info);
}

static int shim_WSAStartup( uint16_t version, void *data )
{
    uint16_t negotiated = 0x0202;

    if (!pe_ensure_socket()) return WSAENETDOWN;
    if (data)
    {
        unsigned char *bytes = data;

        memset( data, 0, 400 );
        bytes[0] = negotiated & 0xff;
        bytes[1] = negotiated >> 8;
        bytes[2] = negotiated & 0xff;
        bytes[3] = negotiated >> 8;
        snprintf( (char *)bytes + 4, 257, "winsock shim" );
        snprintf( (char *)bytes + 261, 129, "running" );
        bytes[390] = 0xff;
        bytes[391] = 0x7f;
    }
    log_line( "[SHIM] WSAStartup requested=0x%04x negotiated=0x%04x", version, negotiated );
    return 0;
}

static int shim_WSACleanup(void) { return 0; }

struct pe_win_handle
{
    uint32_t magic;
    uint32_t type;
    FILE *file;
    Thread *thread;
    Mutex mutex;
    CondVar cond;
    int signaled;
    int manual_reset;
    int sync_ready;
    uint32_t exit_code;
};

struct win_filetime
{
    uint32_t low;
    uint32_t high;
};

struct win_systemtime
{
    uint16_t year;
    uint16_t month;
    uint16_t day_of_week;
    uint16_t day;
    uint16_t hour;
    uint16_t minute;
    uint16_t second;
    uint16_t milliseconds;
};

struct win_system_info
{
    union
    {
        uint32_t oem_id;
        struct
        {
            uint16_t processor_architecture;
            uint16_t reserved;
        } arch;
    } u;
    uint32_t page_size;
    void *minimum_application_address;
    void *maximum_application_address;
    uintptr_t active_processor_mask;
    uint32_t number_of_processors;
    uint32_t processor_type;
    uint32_t allocation_granularity;
    uint16_t processor_level;
    uint16_t processor_revision;
};

struct win_addrinfo
{
    int ai_flags;
    int ai_family;
    int ai_socktype;
    int ai_protocol;
    size_t ai_addrlen;
    char *ai_canonname;
    struct sockaddr *ai_addr;
    struct win_addrinfo *ai_next;
};

struct win_fd_set
{
    uint32_t fd_count;
    uintptr_t fd_array[WIN_FD_SETSIZE];
};

struct win_timeval
{
    int32_t tv_sec;
    int32_t tv_usec;
};

struct pe_thread_start
{
    struct pe_win_handle *handle;
    void *start;
    void *param;
};

#define PE_WIN_HANDLE_MAGIC 0x50454844u
#define PE_WIN_HANDLE_FILE 1u
#define PE_WIN_HANDLE_EVENT 2u
#define PE_WIN_HANDLE_MUTEX 3u
#define PE_WIN_HANDLE_THREAD 4u

static Mutex pe_sync_table_mutex;
static int pe_sync_table_ready;
static struct pe_critical_section_entry pe_critical_sections[PE_SYNC_TABLE_SIZE];
static struct pe_condition_variable_entry pe_condition_variables[PE_SYNC_TABLE_SIZE];

static uintptr_t shim_return_0(void) { return 0; }
static uintptr_t shim_return_1(void) { return 1; }
static uintptr_t shim_return_minus1(void) { return (uintptr_t)-1; }
static void shim_noop(void) {}

static void pe_set_last_error( uint32_t error )
{
    pe_last_error = (int)error;
}

static void pe_sync_table_init(void)
{
    if (pe_sync_table_ready) return;
    mutexInit( &pe_sync_table_mutex );
    pe_sync_table_ready = 1;
}

static struct pe_critical_section_entry *pe_get_critical_section( void *key, int create )
{
    struct pe_critical_section_entry *free_entry = NULL;
    unsigned int i;

    if (!key) return NULL;
    pe_sync_table_init();
    mutexLock( &pe_sync_table_mutex );
    for (i = 0; i < PE_SYNC_TABLE_SIZE; i++)
    {
        if (pe_critical_sections[i].key == key)
        {
            mutexUnlock( &pe_sync_table_mutex );
            return &pe_critical_sections[i];
        }
        if (!pe_critical_sections[i].key && !free_entry)
            free_entry = &pe_critical_sections[i];
    }
    if (create && free_entry)
    {
        free_entry->key = key;
        mutexInit( &free_entry->mutex );
        mutexUnlock( &pe_sync_table_mutex );
        return free_entry;
    }
    mutexUnlock( &pe_sync_table_mutex );
    return NULL;
}

static void pe_delete_critical_section( void *key )
{
    unsigned int i;

    if (!key || !pe_sync_table_ready) return;
    mutexLock( &pe_sync_table_mutex );
    for (i = 0; i < PE_SYNC_TABLE_SIZE; i++)
    {
        if (pe_critical_sections[i].key == key)
        {
            pe_critical_sections[i].key = NULL;
            pe_critical_sections[i].owner = NULL;
            pe_critical_sections[i].recursion = 0;
            break;
        }
    }
    mutexUnlock( &pe_sync_table_mutex );
}

static struct pe_condition_variable_entry *pe_get_condition_variable( void *key, int create )
{
    struct pe_condition_variable_entry *free_entry = NULL;
    unsigned int i;

    if (!key) return NULL;
    pe_sync_table_init();
    mutexLock( &pe_sync_table_mutex );
    for (i = 0; i < PE_SYNC_TABLE_SIZE; i++)
    {
        if (pe_condition_variables[i].key == key)
        {
            mutexUnlock( &pe_sync_table_mutex );
            return &pe_condition_variables[i];
        }
        if (!pe_condition_variables[i].key && !free_entry)
            free_entry = &pe_condition_variables[i];
    }
    if (create && free_entry)
    {
        free_entry->key = key;
        condvarInit( &free_entry->cond );
        mutexUnlock( &pe_sync_table_mutex );
        return free_entry;
    }
    mutexUnlock( &pe_sync_table_mutex );
    return NULL;
}

static uint64_t win_timeout_to_ns( uint32_t milliseconds )
{
    if (milliseconds == WIN_INFINITE) return UINT64_MAX;
    return (uint64_t)milliseconds * 1000000ULL;
}

static uint64_t pe_monotonic_ms(void)
{
    return armTicksToNs( armGetSystemTick() ) / 1000000ULL;
}

static void pe_handle_sync_init( struct pe_win_handle *handle )
{
    if (!handle || handle->sync_ready) return;
    mutexInit( &handle->mutex );
    condvarInit( &handle->cond );
    handle->sync_ready = 1;
}

static int errno_to_wsa( int error )
{
    switch (error)
    {
    case 0: return 0;
    case EINTR: return WSAEINTR;
    case EBADF: return WSAEBADF;
    case EACCES: return WSAEACCES;
    case EFAULT: return WSAEFAULT;
    case EINVAL: return WSAEINVAL;
    case EMFILE: return WSAEMFILE;
    case EWOULDBLOCK: return WSAEWOULDBLOCK;
    case EINPROGRESS: return WSAEINPROGRESS;
    case EALREADY: return WSAEALREADY;
    case ENOTSOCK: return WSAENOTSOCK;
    case EDESTADDRREQ: return WSAEDESTADDRREQ;
    case EMSGSIZE: return WSAEMSGSIZE;
    case EPROTOTYPE: return WSAEPROTOTYPE;
    case ENOPROTOOPT: return WSAENOPROTOOPT;
    case EPROTONOSUPPORT: return WSAEPROTONOSUPPORT;
    case EOPNOTSUPP: return WSAEOPNOTSUPP;
    case EAFNOSUPPORT: return WSAEAFNOSUPPORT;
    case EADDRINUSE: return WSAEADDRINUSE;
    case EADDRNOTAVAIL: return WSAEADDRNOTAVAIL;
    case ENETDOWN: return WSAENETDOWN;
    case ENETUNREACH: return WSAENETUNREACH;
    case ENETRESET: return WSAENETRESET;
    case ECONNABORTED: return WSAECONNABORTED;
    case ECONNRESET: return WSAECONNRESET;
    case ENOBUFS: return WSAENOBUFS;
    case EISCONN: return WSAEISCONN;
    case ENOTCONN: return WSAENOTCONN;
    case ETIMEDOUT: return WSAETIMEDOUT;
    case ECONNREFUSED: return WSAECONNREFUSED;
    case EHOSTUNREACH: return WSAEHOSTUNREACH;
    default: return error;
    }
}

static void pe_set_wsa_errno(void)
{
    pe_set_last_error( errno_to_wsa( errno ) );
}

static int win_family_to_posix( int family )
{
    if (family == WIN_AF_INET6) return AF_INET6;
    return family;
}

static int posix_family_to_win( int family )
{
    if (family == AF_INET6) return WIN_AF_INET6;
    return family;
}

static int pe_ensure_socket(void)
{
    Result rc;

    if (pe_socket_initialized) return 1;
    rc = socketInitializeDefault();
    if (R_FAILED( rc ))
    {
        log_line( "[SHIM] socketInitializeDefault failed rc=0x%x", rc );
        pe_set_last_error( WSAENETDOWN );
        return 0;
    }
    pe_socket_initialized = 1;
    log_line( "[SHIM] socketInitializeDefault OK" );
    return 1;
}

static int pe_is_std_handle( const void *handle )
{
    return handle == pe_stdout_handle || handle == pe_stderr_handle || handle == pe_stdin_handle;
}

static struct pe_win_handle *pe_get_handle( void *handle )
{
    struct pe_win_handle *win = handle;

    if (!win || handle == WIN_INVALID_HANDLE_VALUE || pe_is_std_handle( handle ) ||
        handle == (void *)(intptr_t)-1)
        return NULL;
    return win->magic == PE_WIN_HANDLE_MAGIC ? win : NULL;
}

static void *pe_alloc_handle( uint32_t type )
{
    struct pe_win_handle *handle = calloc( 1, sizeof(*handle) );

    if (!handle)
    {
        pe_set_last_error( WIN_ERROR_NOT_ENOUGH_MEMORY );
        return NULL;
    }
    handle->magic = PE_WIN_HANDLE_MAGIC;
    handle->type = type;
    handle->signaled = 1;
    pe_handle_sync_init( handle );
    return handle;
}

static uint64_t win_filetime_now(void)
{
    return ((uint64_t)time( NULL ) + 11644473600ULL) * 10000000ULL;
}

static void put_filetime( struct win_filetime *out, uint64_t value )
{
    if (!out) return;
    out->low = (uint32_t)value;
    out->high = (uint32_t)(value >> 32);
}

static uint64_t get_filetime( const struct win_filetime *in )
{
    if (!in) return 0;
    return in->low | ((uint64_t)in->high << 32);
}

static size_t win_wcslen16( const uint16_t *text )
{
    size_t len = 0;

    if (!text) return 0;
    while (text[len]) len++;
    return len;
}

static size_t win_wcsnlen16( const uint16_t *text, size_t max_len )
{
    size_t len = 0;

    if (!text) return 0;
    while (len < max_len && text[len]) len++;
    return len;
}

static int win_wcsncmp16( const uint16_t *left, const uint16_t *right, size_t count )
{
    size_t i;

    for (i = 0; i < count; i++)
    {
        uint16_t a = left ? left[i] : 0;
        uint16_t b = right ? right[i] : 0;

        if (a != b || !a || !b) return (int)a - (int)b;
    }
    return 0;
}

static size_t shim_strnlen( const char *text, size_t max_len )
{
    return bounded_strlen( text, max_len );
}

static int shim__stricmp( const char *left, const char *right )
{
    if (!left) left = "";
    if (!right) right = "";
    return strcasecmp( left, right );
}

static int shim__strnicmp( const char *left, const char *right, size_t count )
{
    if (!left) left = "";
    if (!right) right = "";
    return strncasecmp( left, right, count );
}

static int shim_isalnum( int ch ) { return isalnum( (unsigned char)ch ); }
static int shim_isdigit( int ch ) { return isdigit( (unsigned char)ch ); }
static int shim_isspace( int ch ) { return isspace( (unsigned char)ch ); }
static int shim_isxdigit( int ch ) { return isxdigit( (unsigned char)ch ); }
static int shim_tolower( int ch ) { return tolower( (unsigned char)ch ); }

static size_t shim_mbrlen( const char *text, size_t count, void *state )
{
    (void)state;
    if (!text || !count) return 0;
    return *text ? 1 : 0;
}

static int shim_strncpy_s( char *dst, size_t dst_size, const char *src, size_t count )
{
    size_t len;

    if (!dst || !dst_size) return WIN_ERROR_INVALID_PARAMETER;
    if (!src)
    {
        dst[0] = 0;
        return WIN_ERROR_INVALID_PARAMETER;
    }
    len = count == (size_t)-1 ? strlen( src ) : bounded_strlen( src, count );
    if (len >= dst_size)
    {
        dst[0] = 0;
        return WIN_ERROR_INSUFFICIENT_BUFFER;
    }
    memcpy( dst, src, len );
    dst[len] = 0;
    return 0;
}

static int shim_strerror_s( char *dst, size_t dst_size, int error )
{
    const char *text = strerror( error );

    return shim_strncpy_s( dst, dst_size, text ? text : "error", (size_t)-1 );
}

static size_t shim_wcslen( const uint16_t *text )
{
    return win_wcslen16( text );
}

static size_t shim_wcsnlen( const uint16_t *text, size_t max_len )
{
    return win_wcsnlen16( text, max_len );
}

static int shim_wcsncmp( const uint16_t *left, const uint16_t *right, size_t count )
{
    return win_wcsncmp16( left, right, count );
}

static int shim_wcscpy_s( uint16_t *dst, size_t dst_size, const uint16_t *src )
{
    size_t len;

    if (!dst || !dst_size) return WIN_ERROR_INVALID_PARAMETER;
    if (!src)
    {
        dst[0] = 0;
        return WIN_ERROR_INVALID_PARAMETER;
    }
    len = win_wcslen16( src );
    if (len >= dst_size)
    {
        dst[0] = 0;
        return WIN_ERROR_INSUFFICIENT_BUFFER;
    }
    memcpy( dst, src, (len + 1) * sizeof(*dst) );
    return 0;
}

static int shim_wcsncpy_s( uint16_t *dst, size_t dst_size, const uint16_t *src, size_t count )
{
    size_t len;

    if (!dst || !dst_size) return WIN_ERROR_INVALID_PARAMETER;
    if (!src)
    {
        dst[0] = 0;
        return WIN_ERROR_INVALID_PARAMETER;
    }
    len = count == (size_t)-1 ? win_wcslen16( src ) : win_wcsnlen16( src, count );
    if (len >= dst_size)
    {
        dst[0] = 0;
        return WIN_ERROR_INSUFFICIENT_BUFFER;
    }
    memcpy( dst, src, len * sizeof(*dst) );
    dst[len] = 0;
    return 0;
}

static int shim__close( int fd )
{
    if (fd >= 0 && fd <= 2) return 0;
    return close( fd );
}

static int shim__fileno( FILE *stream )
{
    return stream ? fileno( stream ) : -1;
}

static int shim__fseeki64( FILE *stream, int64_t offset, int origin )
{
    return fseek( stream, (long)offset, origin );
}

static FILE *shim__fsopen( const char *path, const char *mode, int shflag )
{
    (void)shflag;
    return fopen( path, mode );
}

static intptr_t shim__get_osfhandle( int fd )
{
    if (fd == 0) return (intptr_t)pe_stdin_handle;
    if (fd == 1) return (intptr_t)pe_stdout_handle;
    if (fd == 2) return (intptr_t)pe_stderr_handle;
    return (intptr_t)fd;
}

static int shim__isatty( int fd )
{
    return fd >= 0 && fd <= 2;
}

static int64_t shim__lseeki64( int fd, int64_t offset, int origin )
{
    return (int64_t)lseek( fd, (off_t)offset, origin );
}

static int shim__read( int fd, void *buffer, unsigned int count )
{
    if (fd == 0) return 0;
    return (int)read( fd, buffer, count );
}

static int shim__setmode( int fd, int mode )
{
    (void)fd;
    return mode;
}

static int shim__sopen_s( int *out_fd, const char *path, int oflag, int shflag, int pmode )
{
    int fd;

    (void)shflag;
    if (!out_fd) return WIN_ERROR_INVALID_PARAMETER;
    fd = open( path, oflag, pmode );
    if (fd == -1)
    {
        *out_fd = -1;
        return errno;
    }
    *out_fd = fd;
    return 0;
}

static int shim__chsize_s( int fd, int64_t size )
{
    return ftruncate( fd, (off_t)size ) == -1 ? errno : 0;
}

static int shim_freopen_s( FILE **out, const char *path, const char *mode, FILE *stream )
{
    FILE *ret;

    if (!out) return WIN_ERROR_INVALID_PARAMETER;
    ret = freopen( path, mode, stream );
    *out = ret;
    return ret ? 0 : errno;
}

static int64_t shim__time64( int64_t *out )
{
    int64_t now = (int64_t)time( NULL );

    if (out) *out = now;
    return now;
}

static double shim__difftime64( int64_t end, int64_t begin )
{
    return difftime( (time_t)end, (time_t)begin );
}

static int shim__gmtime64_s( struct tm *out, const int64_t *time_value )
{
    time_t value;
    struct tm *tmp;

    if (!out || !time_value) return WIN_ERROR_INVALID_PARAMETER;
    value = (time_t)*time_value;
    tmp = gmtime( &value );
    if (!tmp) return errno ? errno : WIN_ERROR_INVALID_PARAMETER;
    *out = *tmp;
    return 0;
}

static int shim__localtime64_s( struct tm *out, const int64_t *time_value )
{
    time_t value;
    struct tm *tmp;

    if (!out || !time_value) return WIN_ERROR_INVALID_PARAMETER;
    value = (time_t)*time_value;
    tmp = localtime( &value );
    if (!tmp) return errno ? errno : WIN_ERROR_INVALID_PARAMETER;
    *out = *tmp;
    return 0;
}

static intptr_t shim__findfirst64i32( const char *pattern, void *data )
{
    (void)pattern;
    (void)data;
    errno = ENOENT;
    return -1;
}

static int shim__findnext64i32( intptr_t handle, void *data )
{
    (void)handle;
    (void)data;
    errno = ENOENT;
    return -1;
}

static int shim__findclose( intptr_t handle )
{
    (void)handle;
    return 0;
}

static int shim__fstat64( int fd, void *buffer )
{
    struct stat st;

    if (!buffer) return -1;
    memset( buffer, 0, 128 );
    return fstat( fd, &st );
}

static char *shim__fullpath( char *out, const char *path, size_t out_size )
{
    size_t len;

    if (!path) return NULL;
    len = strlen( path ) + 1;
    if (!out)
    {
        out = malloc( len );
        out_size = len;
    }
    if (!out || out_size < len) return NULL;
    memcpy( out, path, len );
    return out;
}

static void shim__lock_file( FILE *stream ) { (void)stream; }
static void shim__unlock_file( FILE *stream ) { (void)stream; }
static int shim__mkdir( const char *path ) { return mkdir( path, 0777 ); }
static int shim__stat64( const char *path, void *buffer )
{
    struct stat st;

    if (!buffer) return -1;
    memset( buffer, 0, 144 );
    return stat( path, &st );
}
static int shim__unlink( const char *path ) { return unlink( path ); }
static FILE *shim__fdopen( int fd, const char *mode ) { return fdopen( fd, mode ); }
static uint64_t shim__byteswap_uint64( uint64_t value ) { return __builtin_bswap64( value ); }
static int shim__getch(void) { return 0; }

static size_t shim_mbrtowc( uint16_t *out, const char *src, size_t count, void *state )
{
    (void)state;
    if (!src || !count) return 0;
    if (out) *out = (unsigned char)*src;
    return *src ? 1 : 0;
}

static int shim_mbstowcs_s( size_t *converted, uint16_t *dst, size_t dst_size,
                            const char *src, size_t count )
{
    size_t i;
    size_t limit;

    if (!src) return WIN_ERROR_INVALID_PARAMETER;
    limit = count == (size_t)-1 ? strlen( src ) : bounded_strlen( src, count );
    if (!dst)
    {
        if (converted) *converted = limit + 1;
        return 0;
    }
    if (!dst_size) return WIN_ERROR_INVALID_PARAMETER;
    for (i = 0; i < limit && i + 1 < dst_size; i++) dst[i] = (unsigned char)src[i];
    dst[i] = 0;
    if (converted) *converted = i + 1;
    return i < limit ? WIN_ERROR_INSUFFICIENT_BUFFER : 0;
}

static size_t shim_wcrtomb( char *dst, uint16_t wc, void *state )
{
    (void)state;
    if (dst) *dst = wc < 0x80 ? (char)wc : '?';
    return 1;
}

static int shim_wcstombs_s( size_t *converted, char *dst, size_t dst_size,
                            const uint16_t *src, size_t count )
{
    size_t i;
    size_t limit;

    if (!src) return WIN_ERROR_INVALID_PARAMETER;
    limit = count == (size_t)-1 ? win_wcslen16( src ) : win_wcsnlen16( src, count );
    if (!dst)
    {
        if (converted) *converted = limit + 1;
        return 0;
    }
    if (!dst_size) return WIN_ERROR_INVALID_PARAMETER;
    for (i = 0; i < limit && i + 1 < dst_size; i++) dst[i] = src[i] < 0x80 ? (char)src[i] : '?';
    dst[i] = 0;
    if (converted) *converted = i + 1;
    return i < limit ? WIN_ERROR_INSUFFICIENT_BUFFER : 0;
}

static void *shim_CreateEventA( void *attrs, int manual_reset, int initial_state, const char *name )
{
    struct pe_win_handle *handle;

    (void)attrs;
    (void)name;
    handle = pe_alloc_handle( PE_WIN_HANDLE_EVENT );
    if (handle)
    {
        handle->manual_reset = manual_reset;
        handle->signaled = initial_state;
    }
    return handle;
}

static void *shim_CreateMutexA( void *attrs, int initial_owner, const char *name )
{
    struct pe_win_handle *handle;

    (void)attrs;
    (void)name;
    handle = pe_alloc_handle( PE_WIN_HANDLE_MUTEX );
    if (handle)
    {
        handle->manual_reset = 0;
        handle->signaled = !initial_owner;
    }
    return handle;
}

static int shim_CloseHandle( void *handle )
{
    struct pe_win_handle *win = pe_get_handle( handle );

    if (pe_is_std_handle( handle ) || handle == (void *)(intptr_t)-1) return 1;
    if (!win)
    {
        pe_set_last_error( WIN_ERROR_INVALID_HANDLE );
        return 0;
    }
    if (win->type == PE_WIN_HANDLE_FILE && win->file) fclose( win->file );
    if (win->type == PE_WIN_HANDLE_THREAD && win->thread)
    {
        if (!win->signaled)
        {
            log_line( "[SHIM] CloseHandle thread=%p still running; keeping backing handle", handle );
            pe_set_last_error( WIN_ERROR_SUCCESS );
            return 1;
        }
        threadWaitForExit( win->thread );
        threadClose( win->thread );
        free( win->thread );
    }
    win->magic = 0;
    free( win );
    pe_set_last_error( WIN_ERROR_SUCCESS );
    return 1;
}

static int shim_CompareFileTime( const struct win_filetime *left, const struct win_filetime *right )
{
    uint64_t a = get_filetime( left );
    uint64_t b = get_filetime( right );

    return a < b ? -1 : a > b ? 1 : 0;
}

static void *shim_CreateFileA( const char *path, uint32_t access, uint32_t share,
                               void *security, uint32_t creation, uint32_t flags, void *template_file )
{
    struct pe_win_handle *handle;
    const char *mode;

    (void)share;
    (void)security;
    (void)creation;
    (void)flags;
    (void)template_file;
    if (!path)
    {
        pe_set_last_error( WIN_ERROR_INVALID_PARAMETER );
        return WIN_INVALID_HANDLE_VALUE;
    }
    if (!strcasecmp( path, "NUL" ))
        return pe_stdout_handle;

    mode = access & 0x40000000u ? "wb" : "rb";
    handle = pe_alloc_handle( PE_WIN_HANDLE_FILE );
    if (!handle) return WIN_INVALID_HANDLE_VALUE;
    handle->file = fopen( path, mode );
    if (!handle->file)
    {
        free( handle );
        pe_set_last_error( WIN_ERROR_FILE_NOT_FOUND );
        return WIN_INVALID_HANDLE_VALUE;
    }
    pe_set_last_error( WIN_ERROR_SUCCESS );
    return handle;
}

static void *shim_CreateFileMappingA( void *file, void *attrs, uint32_t protect,
                                      uint32_t size_high, uint32_t size_low, const char *name )
{
    (void)file;
    (void)attrs;
    (void)protect;
    (void)size_high;
    (void)size_low;
    (void)name;
    pe_set_last_error( WIN_ERROR_NOT_SUPPORTED );
    return NULL;
}

static void *shim_CreateThread( void *attrs, size_t stack_size, void *start, void *param,
                                uint32_t flags, uint32_t *thread_id )
{
    struct pe_win_handle *handle;
    struct pe_thread_start *thread_start;
    Result rc;

    (void)attrs;
    (void)flags;
    if (!start)
    {
        pe_set_last_error( WIN_ERROR_INVALID_PARAMETER );
        return NULL;
    }

    handle = pe_alloc_handle( PE_WIN_HANDLE_THREAD );
    thread_start = calloc( 1, sizeof(*thread_start) );
    if (!handle || !thread_start)
    {
        free( handle );
        free( thread_start );
        pe_set_last_error( WIN_ERROR_NOT_ENOUGH_MEMORY );
        return NULL;
    }
    handle->thread = calloc( 1, sizeof(*handle->thread) );
    if (!handle->thread)
    {
        free( thread_start );
        free( handle );
        pe_set_last_error( WIN_ERROR_NOT_ENOUGH_MEMORY );
        return NULL;
    }

    thread_start->handle = handle;
    thread_start->start = start;
    thread_start->param = param;
    handle->signaled = 0;
    if (stack_size < 0x40000) stack_size = 0x40000;

    rc = threadCreate( handle->thread, pe_thread_entry, thread_start, NULL,
                       align_up_u32( (uint32_t)stack_size, PAGE_SIZE ), 0x2c, -2 );
    if (R_FAILED( rc ) || R_FAILED( threadStart( handle->thread ) ))
    {
        log_line( "[SHIM] CreateThread failed rc=0x%x", rc );
        free( handle->thread );
        free( thread_start );
        free( handle );
        pe_set_last_error( WIN_ERROR_NOT_ENOUGH_MEMORY );
        return NULL;
    }

    if (thread_id) *thread_id = (uint32_t)(uintptr_t)handle;
    log_line( "[SHIM] CreateThread start=%p param=%p handle=%p", start, param, handle );
    return handle;
}

static int shim_DuplicateHandle( void *src_process, void *src, void *dst_process,
                                 void **dst, uint32_t access, int inherit, uint32_t options )
{
    (void)src_process;
    (void)dst_process;
    (void)access;
    (void)inherit;
    (void)options;
    if (dst) *dst = src;
    return 1;
}

static uint32_t shim_FormatMessageA( uint32_t flags, const void *source, uint32_t message_id,
                                     uint32_t language_id, char *buffer, uint32_t size, void *args )
{
    char temp[96];
    size_t len;

    (void)source;
    (void)language_id;
    (void)args;
    snprintf( temp, sizeof(temp), "Win32 error %u", message_id );
    len = strlen( temp );
    if (flags & WIN_FORMAT_MESSAGE_ALLOCATE_BUFFER)
    {
        char **out = (char **)buffer;
        if (!out) return 0;
        *out = malloc( len + 1 );
        if (!*out) return 0;
        memcpy( *out, temp, len + 1 );
        return (uint32_t)len;
    }
    if (!buffer || !size) return 0;
    if (len >= size) len = size - 1;
    memcpy( buffer, temp, len );
    buffer[len] = 0;
    return (uint32_t)len;
}

static int shim_GetConsoleMode( void *handle, uint32_t *mode )
{
    if (!pe_is_std_handle( handle ))
    {
        pe_set_last_error( WIN_ERROR_INVALID_HANDLE );
        return 0;
    }
    if (mode) *mode = 0;
    return 1;
}

static int shim_GetConsoleScreenBufferInfo( void *handle, void *info )
{
    unsigned char *bytes = info;

    if (!pe_is_std_handle( handle ) || !info) return 0;
    memset( info, 0, 22 );
    bytes[0] = 80;
    bytes[4] = 25;
    bytes[8] = 7;
    bytes[18] = 80;
    bytes[20] = 25;
    return 1;
}

static uint32_t shim_GetCurrentThreadId(void) { return 1; }

static uint32_t shim_GetEnvironmentVariableA( const char *name, char *buffer, uint32_t size )
{
    const char *value = getenv( name ? name : "" );
    size_t len;

    if (!value)
    {
        pe_set_last_error( WIN_ERROR_FILE_NOT_FOUND );
        return 0;
    }
    len = strlen( value );
    if (buffer && size)
    {
        size_t copy = len < size ? len : size - 1;
        memcpy( buffer, value, copy );
        buffer[copy] = 0;
    }
    return (uint32_t)len;
}

static uint32_t shim_GetFileAttributesA( const char *path )
{
    struct stat st;

    if (!path || stat( path, &st ) == -1)
    {
        pe_set_last_error( WIN_ERROR_FILE_NOT_FOUND );
        return 0xffffffffu;
    }
    return S_ISDIR( st.st_mode ) ? WIN_FILE_ATTRIBUTE_DIRECTORY : WIN_FILE_ATTRIBUTE_NORMAL;
}

static int shim_GetFileTime( void *handle, struct win_filetime *created,
                             struct win_filetime *accessed, struct win_filetime *written )
{
    uint64_t now = win_filetime_now();

    (void)handle;
    put_filetime( created, now );
    put_filetime( accessed, now );
    put_filetime( written, now );
    return 1;
}

static uint32_t shim_GetFileType( void *handle )
{
    struct pe_win_handle *win = pe_get_handle( handle );

    if (pe_is_std_handle( handle )) return WIN_FILE_TYPE_CHAR;
    if (win && win->type == PE_WIN_HANDLE_FILE) return WIN_FILE_TYPE_DISK;
    pe_set_last_error( WIN_ERROR_INVALID_HANDLE );
    return 0;
}

static uint32_t shim_GetFullPathNameW( const uint16_t *path, uint32_t size,
                                       uint16_t *buffer, uint16_t **file_part )
{
    size_t len = win_wcslen16( path );
    size_t i;

    if (!path) return 0;
    if (file_part) *file_part = buffer;
    if (!buffer || size <= len) return (uint32_t)(len + 1);
    for (i = 0; i <= len; i++) buffer[i] = path[i];
    return (uint32_t)len;
}

static uint32_t shim_GetModuleFileNameA( void *module, char *buffer, uint32_t size )
{
    const char *path = pe_target_path[0] ? pe_target_path : (pe_argc ? pe_argv[0] : "curl.exe");
    size_t len = strlen( path );

    (void)module;
    if (!buffer || !size) return (uint32_t)len;
    if (len >= size) len = size - 1;
    memcpy( buffer, path, len );
    buffer[len] = 0;
    return (uint32_t)len;
}

static int shim_GetOverlappedResult( void *handle, void *overlapped, uint32_t *bytes, int wait )
{
    (void)handle;
    (void)overlapped;
    (void)wait;
    if (bytes) *bytes = 0;
    return 1;
}

static void pe_thread_entry( void *arg )
{
    struct pe_thread_start *start = arg;
    struct pe_win_handle *handle;

    if (!start) threadExit();
    handle = start->handle;
    log_line( "[SHIM] thread entry handle=%p start=%p param=%p", handle, start->start, start->param );
    handle->exit_code = (uint32_t)call_pe_function3( start->start, (uintptr_t)start->param, 0, 0 );
    pe_handle_sync_init( handle );
    mutexLock( &handle->mutex );
    handle->signaled = 1;
    condvarWakeAll( &handle->cond );
    mutexUnlock( &handle->mutex );
    log_line( "[SHIM] thread exit handle=%p code=%u", handle, handle->exit_code );
    free( start );
    threadExit();
}

static void *shim_GetStdHandle( uint32_t std_handle )
{
    if (std_handle == WIN_STD_OUTPUT_HANDLE) return pe_stdout_handle;
    if (std_handle == WIN_STD_ERROR_HANDLE) return pe_stderr_handle;
    if (std_handle == WIN_STD_INPUT_HANDLE) return pe_stdin_handle;
    pe_set_last_error( WIN_ERROR_INVALID_PARAMETER );
    return WIN_INVALID_HANDLE_VALUE;
}

static void shim_GetSystemInfo( struct win_system_info *info )
{
    if (!info) return;
    memset( info, 0, sizeof(*info) );
    info->u.arch.processor_architecture = 12;
    info->page_size = PAGE_SIZE;
    info->minimum_application_address = (void *)0x10000;
    info->maximum_application_address = (void *)(uintptr_t)0x0000ffffffffffffULL;
    info->active_processor_mask = 0xf;
    info->number_of_processors = 4;
    info->allocation_granularity = 0x10000;
}

static void shim_GetSystemTime( struct win_systemtime *out )
{
    time_t now;
    struct tm *tm;

    if (!out) return;
    now = time( NULL );
    tm = gmtime( &now );
    memset( out, 0, sizeof(*out) );
    if (!tm) return;
    out->year = (uint16_t)(tm->tm_year + 1900);
    out->month = (uint16_t)(tm->tm_mon + 1);
    out->day_of_week = (uint16_t)tm->tm_wday;
    out->day = (uint16_t)tm->tm_mday;
    out->hour = (uint16_t)tm->tm_hour;
    out->minute = (uint16_t)tm->tm_min;
    out->second = (uint16_t)tm->tm_sec;
}

static void shim_GetSystemTimeAsFileTime( struct win_filetime *out )
{
    put_filetime( out, win_filetime_now() );
}

static uint64_t shim_GetTickCount64(void)
{
    return armTicksToNs( armGetSystemTick() ) / 1000000ULL;
}

static uint32_t shim_GetTimeZoneInformation( void *info )
{
    if (info) memset( info, 0, 172 );
    return WIN_TIME_ZONE_ID_UNKNOWN;
}

static int shim_InitOnceExecuteOnce( void *once, void *callback, void *parameter, void **context )
{
    if (!callback) return 0;
    return (int)call_pe_function3( callback, (uintptr_t)once, (uintptr_t)parameter,
                                  (uintptr_t)context );
}

static void shim_InitializeConditionVariable( void *condition ) { (void)condition; }
static int shim_InitializeCriticalSectionEx( void *section, uint32_t spin, uint32_t flags )
{
    (void)spin;
    (void)flags;
    pe_get_critical_section( section, 1 );
    return 1;
}

static void *shim_MapViewOfFile( void *mapping, uint32_t access, uint32_t offset_high,
                                 uint32_t offset_low, size_t bytes )
{
    (void)mapping;
    (void)access;
    (void)offset_high;
    (void)offset_low;
    (void)bytes;
    pe_set_last_error( WIN_ERROR_NOT_SUPPORTED );
    return NULL;
}

static int shim_MultiByteToWideChar( uint32_t codepage, uint32_t flags, const char *src,
                                     int src_len, uint16_t *dst, int dst_len )
{
    int len;
    int i;

    (void)codepage;
    (void)flags;
    if (!src) return 0;
    len = src_len < 0 ? (int)strlen( src ) + 1 : src_len;
    if (!dst || !dst_len) return len;
    for (i = 0; i < len && i < dst_len; i++) dst[i] = (unsigned char)src[i];
    if (i == dst_len && src_len >= 0) return i;
    if (i == dst_len) dst[dst_len - 1] = 0;
    return i;
}

static int shim_PeekNamedPipe( void *handle, void *buffer, uint32_t size, uint32_t *read,
                               uint32_t *total, uint32_t *left )
{
    (void)handle;
    (void)buffer;
    (void)size;
    if (read) *read = 0;
    if (total) *total = 0;
    if (left) *left = 0;
    return 0;
}

static int shim_QueryPerformanceCounter( int64_t *counter )
{
    if (counter) *counter = (int64_t)armTicksToNs( armGetSystemTick() );
    return 1;
}

static int shim_ReadFile( void *handle, void *buffer, uint32_t size, uint32_t *read_bytes, void *overlapped )
{
    struct pe_win_handle *win = pe_get_handle( handle );
    size_t got = 0;

    (void)overlapped;
    if (handle == pe_stdin_handle)
    {
        if (read_bytes) *read_bytes = 0;
        return 1;
    }
    if (win && win->type == PE_WIN_HANDLE_FILE && win->file)
        got = fread( buffer, 1, size, win->file );
    else
    {
        pe_set_last_error( WIN_ERROR_INVALID_HANDLE );
        return 0;
    }
    if (read_bytes) *read_bytes = (uint32_t)got;
    return 1;
}

static int shim_ReleaseMutex( void *handle )
{
    struct pe_win_handle *win = pe_get_handle( handle );

    if (win)
    {
        pe_handle_sync_init( win );
        mutexLock( &win->mutex );
        win->signaled = 1;
        condvarWakeOne( &win->cond );
        mutexUnlock( &win->mutex );
    }
    return 1;
}

static int shim_SetConsoleCtrlHandler( void *handler, int add )
{
    (void)handler;
    (void)add;
    return 1;
}

static int shim_SetConsoleMode( void *handle, uint32_t mode )
{
    (void)mode;
    return pe_is_std_handle( handle );
}

static int shim_SetFileTime( void *handle, const struct win_filetime *created,
                             const struct win_filetime *accessed, const struct win_filetime *written )
{
    (void)handle;
    (void)created;
    (void)accessed;
    (void)written;
    return 1;
}

static int shim_SetHandleInformation( void *handle, uint32_t mask, uint32_t flags )
{
    (void)handle;
    (void)mask;
    (void)flags;
    return 1;
}

static void shim_SetLastError( uint32_t error )
{
    pe_set_last_error( error );
}

static int shim_SetEvent( void *handle )
{
    struct pe_win_handle *win = pe_get_handle( handle );

    if (!win || win->type != PE_WIN_HANDLE_EVENT)
    {
        pe_set_last_error( WIN_ERROR_INVALID_HANDLE );
        return 0;
    }
    pe_handle_sync_init( win );
    mutexLock( &win->mutex );
    win->signaled = 1;
    condvarWakeAll( &win->cond );
    mutexUnlock( &win->mutex );
    pe_set_last_error( WIN_ERROR_SUCCESS );
    return 1;
}

static int shim_ResetEvent( void *handle )
{
    struct pe_win_handle *win = pe_get_handle( handle );

    if (!win || win->type != PE_WIN_HANDLE_EVENT)
    {
        pe_set_last_error( WIN_ERROR_INVALID_HANDLE );
        return 0;
    }
    pe_handle_sync_init( win );
    mutexLock( &win->mutex );
    win->signaled = 0;
    mutexUnlock( &win->mutex );
    pe_set_last_error( WIN_ERROR_SUCCESS );
    return 1;
}

static int shim_SetStdHandle( uint32_t std_handle, void *handle )
{
    if (std_handle == WIN_STD_OUTPUT_HANDLE) pe_stdout_handle = handle;
    else if (std_handle == WIN_STD_ERROR_HANDLE) pe_stderr_handle = handle;
    else if (std_handle == WIN_STD_INPUT_HANDLE) pe_stdin_handle = handle;
    else return 0;
    return 1;
}

static int shim_SleepConditionVariableCS( void *condition, void *section, uint32_t milliseconds )
{
    struct pe_condition_variable_entry *cv = pe_get_condition_variable( condition, 1 );
    struct pe_critical_section_entry *cs = pe_get_critical_section( section, 1 );
    Thread *self = threadGetSelf();
    uint32_t recursion;
    Result rc;

    log_line( "[SHIM] SleepConditionVariableCS cv=%p cs=%p timeout=%u",
              condition, section, milliseconds );
    if (!cv || !cs)
    {
        pe_set_last_error( WIN_ERROR_NOT_ENOUGH_MEMORY );
        return 0;
    }

    if (cs->owner != self)
    {
        mutexLock( &cs->mutex );
        cs->owner = self;
        cs->recursion = 1;
    }
    recursion = cs->recursion ? cs->recursion : 1;
    cs->owner = NULL;
    cs->recursion = 0;
    rc = condvarWaitTimeout( &cv->cond, &cs->mutex, win_timeout_to_ns( milliseconds ) );
    cs->owner = self;
    cs->recursion = recursion;
    if (R_SUCCEEDED( rc ))
    {
        pe_set_last_error( WIN_ERROR_SUCCESS );
        log_line( "[SHIM] SleepConditionVariableCS wake cv=%p", condition );
        return 1;
    }

    pe_set_last_error( rc == 0xea01 ? WIN_WAIT_TIMEOUT : WIN_WAIT_FAILED );
    log_line( "[SHIM] SleepConditionVariableCS ret=0x%x last=%d", rc, pe_last_error );
    return 0;
}

static uint32_t shim_SleepEx( uint32_t milliseconds, int alertable )
{
    (void)alertable;
    shim_Sleep( milliseconds );
    return 0;
}

static int shim_SystemTimeToFileTime( const struct win_systemtime *system_time,
                                      struct win_filetime *file_time )
{
    struct tm tm;
    time_t value;

    if (!system_time || !file_time) return 0;
    memset( &tm, 0, sizeof(tm) );
    tm.tm_year = system_time->year - 1900;
    tm.tm_mon = system_time->month - 1;
    tm.tm_mday = system_time->day;
    tm.tm_hour = system_time->hour;
    tm.tm_min = system_time->minute;
    tm.tm_sec = system_time->second;
    value = mktime( &tm );
    put_filetime( file_time, ((uint64_t)value + 11644473600ULL) * 10000000ULL );
    return 1;
}

static int shim_TerminateThread( void *thread, uint32_t code )
{
    (void)thread;
    (void)code;
    return 1;
}

static int shim_UnmapViewOfFile( const void *address )
{
    (void)address;
    return 1;
}

static uint64_t shim_VerSetConditionMask( uint64_t mask, uint32_t type_mask, unsigned char condition )
{
    return mask | ((uint64_t)condition << (type_mask & 0x3f));
}

static int shim_VerifyVersionInfoW( void *version_info, uint32_t type_mask, uint64_t condition_mask )
{
    (void)version_info;
    (void)type_mask;
    (void)condition_mask;
    return 1;
}

static void *shim_VirtualAlloc( void *address, size_t size, uint32_t type, uint32_t protect )
{
    void *ret;

    (void)type;
    if (!size) return NULL;
    ret = horizon_mmap( address, align_up_u32( (uint32_t)size, PAGE_SIZE ),
                        win_protect_to_horizon( protect ), MAP_PRIVATE | MAP_ANON, -1, 0 );
    if (ret == MAP_FAILED)
    {
        pe_set_last_error( errno );
        return NULL;
    }
    return ret;
}

static int shim_VirtualFree( void *address, size_t size, uint32_t type )
{
    (void)address;
    (void)size;
    (void)type;
    return 1;
}

static uint32_t shim_WaitForMultipleObjects( uint32_t count, const void **handles,
                                             int wait_all, uint32_t milliseconds )
{
    uint64_t start_ms = pe_monotonic_ms();

    log_line( "[SHIM] WaitForMultipleObjects count=%u all=%d timeout=%u",
              count, wait_all, milliseconds );
    if (!count || !handles) return WIN_WAIT_FAILED;

    for (;;)
    {
        uint32_t i;
        int ready_count = 0;

        for (i = 0; i < count; i++)
        {
            struct pe_win_handle *win = pe_get_handle( (void *)handles[i] );
            int ready = !win || win->signaled;

            if (ready)
            {
                ready_count++;
                if (!wait_all) return WIN_WAIT_OBJECT_0 + i;
            }
        }

        if (wait_all && ready_count == (int)count) return WIN_WAIT_OBJECT_0;
        if (milliseconds == 0 ||
            (milliseconds != WIN_INFINITE && pe_monotonic_ms() - start_ms >= milliseconds))
        {
            pe_set_last_error( WIN_WAIT_TIMEOUT );
            return WIN_WAIT_TIMEOUT;
        }
        shim_Sleep( milliseconds == WIN_INFINITE ? 10 :
                    (milliseconds - (uint32_t)(pe_monotonic_ms() - start_ms) > 10 ? 10 :
                     milliseconds - (uint32_t)(pe_monotonic_ms() - start_ms)) );
    }
}

static uint32_t shim_WaitForSingleObject( void *handle, uint32_t milliseconds )
{
    struct pe_win_handle *win = pe_get_handle( handle );
    uint64_t start_ms = pe_monotonic_ms();

    if (win && win->type == PE_WIN_HANDLE_THREAD && win->thread)
    {
        log_line( "[SHIM] WaitForSingleObject thread=%p timeout=%u", handle, milliseconds );
        if (milliseconds == WIN_INFINITE)
        {
            threadWaitForExit( win->thread );
            win->signaled = 1;
            return WIN_WAIT_OBJECT_0;
        }
        while (!win->signaled)
        {
            if (milliseconds == 0 || pe_monotonic_ms() - start_ms >= milliseconds)
            {
                pe_set_last_error( WIN_WAIT_TIMEOUT );
                return WIN_WAIT_TIMEOUT;
            }
            shim_Sleep( 1 );
        }
        return WIN_WAIT_OBJECT_0;
    }
    if (win && (win->type == PE_WIN_HANDLE_EVENT || win->type == PE_WIN_HANDLE_MUTEX))
    {
        uint32_t ret = WIN_WAIT_OBJECT_0;

        pe_handle_sync_init( win );
        mutexLock( &win->mutex );
        while (!win->signaled)
        {
            Result rc;

            if (milliseconds == 0)
            {
                ret = WIN_WAIT_TIMEOUT;
                break;
            }
            rc = condvarWaitTimeout( &win->cond, &win->mutex, win_timeout_to_ns( milliseconds ) );
            if (R_FAILED( rc ))
            {
                ret = WIN_WAIT_TIMEOUT;
                break;
            }
            if (milliseconds != WIN_INFINITE && pe_monotonic_ms() - start_ms >= milliseconds)
            {
                ret = WIN_WAIT_TIMEOUT;
                break;
            }
        }
        if (ret == WIN_WAIT_OBJECT_0 && win->type == PE_WIN_HANDLE_EVENT && !win->manual_reset)
            win->signaled = 0;
        if (ret == WIN_WAIT_OBJECT_0 && win->type == PE_WIN_HANDLE_MUTEX)
            win->signaled = 0;
        mutexUnlock( &win->mutex );
        pe_set_last_error( ret == WIN_WAIT_OBJECT_0 ? WIN_ERROR_SUCCESS : WIN_WAIT_TIMEOUT );
        return ret;
    }
    (void)milliseconds;
    return WIN_WAIT_OBJECT_0;
}

static uint32_t shim_WaitForSingleObjectEx( void *handle, uint32_t milliseconds, int alertable )
{
    (void)alertable;
    return shim_WaitForSingleObject( handle, milliseconds );
}

static int shim_WakeConditionVariable( void *condition )
{
    struct pe_condition_variable_entry *cv = pe_get_condition_variable( condition, 0 );

    log_line( "[SHIM] WakeConditionVariable cv=%p", condition );
    if (cv) condvarWakeOne( &cv->cond );
    return 1;
}

static int shim_WideCharToMultiByte( uint32_t codepage, uint32_t flags, const uint16_t *src,
                                     int src_len, char *dst, int dst_len,
                                     const char *default_char, int *used_default )
{
    int len;
    int i;

    (void)codepage;
    (void)flags;
    (void)default_char;
    if (used_default) *used_default = 0;
    if (!src) return 0;
    len = src_len < 0 ? (int)win_wcslen16( src ) + 1 : src_len;
    if (!dst || !dst_len) return len;
    for (i = 0; i < len && i < dst_len; i++) dst[i] = src[i] < 0x80 ? (char)src[i] : '?';
    if (i == dst_len && src_len < 0) dst[dst_len - 1] = 0;
    return i;
}

static int shim_WriteConsoleW( void *handle, const uint16_t *buffer, uint32_t chars,
                               uint32_t *written, void *reserved )
{
    uint32_t i;

    (void)reserved;
    if (!pe_is_std_handle( handle )) return 0;
    for (i = 0; i < chars; i++)
    {
        char ch = buffer[i] < 0x80 ? (char)buffer[i] : '?';
        FILE *stream = handle == pe_stderr_handle ? stderr : stdout;
        fputc( ch, stream );
        log_pe_output( &ch, 1 );
    }
    if (written) *written = chars;
    return 1;
}

static int shim_WriteFile( void *handle, const void *buffer, uint32_t size,
                           uint32_t *written, void *overlapped )
{
    struct pe_win_handle *win = pe_get_handle( handle );
    FILE *stream = NULL;
    size_t ret;

    (void)overlapped;
    if (handle == pe_stdout_handle) stream = stdout;
    else if (handle == pe_stderr_handle) stream = stderr;
    else if (win && win->type == PE_WIN_HANDLE_FILE) stream = win->file;

    if (!stream)
    {
        pe_set_last_error( WIN_ERROR_INVALID_HANDLE );
        return 0;
    }
    ret = fwrite( buffer, 1, size, stream );
    fflush( stream );
    if (stream == stdout || stream == stderr)
        log_pe_output( buffer, ret );
    if (written) *written = (uint32_t)ret;
    return ret == size;
}

static int gai_to_wsa( int error )
{
    switch (error)
    {
    case 0: return 0;
    case EAI_AGAIN: return WIN_EAI_AGAIN;
    case EAI_MEMORY: return WIN_EAI_MEMORY;
    case EAI_FAIL: return WIN_EAI_FAIL;
    default: return WIN_EAI_NONAME;
    }
}

static void sockaddr_win_to_posix( const struct sockaddr *win_addr, int win_len,
                                   struct sockaddr_storage *posix_addr, socklen_t *posix_len )
{
    size_t copy = win_len > (int)sizeof(*posix_addr) ? sizeof(*posix_addr) : (size_t)win_len;

    memset( posix_addr, 0, sizeof(*posix_addr) );
    memcpy( posix_addr, win_addr, copy );
    ((struct sockaddr *)posix_addr)->sa_family = win_family_to_posix( win_addr->sa_family );
    *posix_len = (socklen_t)copy;
}

static void sockaddr_posix_to_win( struct sockaddr *win_addr, const struct sockaddr *posix_addr,
                                   socklen_t posix_len )
{
    memcpy( win_addr, posix_addr, posix_len );
    win_addr->sa_family = posix_family_to_win( posix_addr->sa_family );
}

static void win_fd_set_to_posix( const struct win_fd_set *win_set, fd_set *posix_set, int *maxfd )
{
    uint32_t i;

    FD_ZERO( posix_set );
    if (!win_set) return;
    for (i = 0; i < win_set->fd_count && i < WIN_FD_SETSIZE; i++)
    {
        int fd = (int)win_set->fd_array[i];
        if (fd >= 0)
        {
            FD_SET( fd, posix_set );
            if (fd > *maxfd) *maxfd = fd;
        }
    }
}

static void posix_fd_set_to_win( struct win_fd_set *win_set, const struct win_fd_set *old_set,
                                 const fd_set *posix_set )
{
    uint32_t i;
    uint32_t count = 0;

    if (!win_set || !old_set) return;
    for (i = 0; i < old_set->fd_count && i < WIN_FD_SETSIZE; i++)
    {
        int fd = (int)old_set->fd_array[i];
        if (fd >= 0 && FD_ISSET( fd, posix_set ))
            win_set->fd_array[count++] = old_set->fd_array[i];
    }
    win_set->fd_count = count;
}

static int parse_service_port( const char *service )
{
    if (!service || !*service) return 0;
    if (!strcasecmp( service, "http" )) return 80;
    if (!strcasecmp( service, "https" )) return 443;
    return atoi( service );
}

static struct win_addrinfo *alloc_ipv4_addrinfo( const char *ip, int port, const struct win_addrinfo *hints )
{
    struct win_addrinfo *entry = calloc( 1, sizeof(*entry) );
    struct sockaddr_in *addr;

    if (!entry) return NULL;
    addr = calloc( 1, sizeof(*addr) );
    if (!addr)
    {
        free( entry );
        return NULL;
    }

    entry->ai_family = AF_INET;
    entry->ai_socktype = hints && hints->ai_socktype ? hints->ai_socktype : SOCK_STREAM;
    entry->ai_protocol = hints ? hints->ai_protocol : 0;
    entry->ai_addrlen = sizeof(*addr);
    entry->ai_addr = (struct sockaddr *)addr;

    addr->sin_family = AF_INET;
    addr->sin_port = htons( (uint16_t)port );
    inet_pton( AF_INET, ip, &addr->sin_addr );
    return entry;
}

static int shim_WSAGetLastError(void) { return pe_last_error; }
static void shim_WSASetLastError( int error ) { pe_set_last_error( (uint32_t)error ); }
static void *shim_WSACreateEvent(void)
{
    struct pe_win_handle *handle = pe_alloc_handle( PE_WIN_HANDLE_EVENT );

    if (handle)
    {
        handle->manual_reset = 1;
        handle->signaled = 0;
    }
    log_line( "[SHIM] WSACreateEvent -> %p", handle );
    return handle;
}
static int shim_WSACloseEvent( void *event ) { return shim_CloseHandle( event ); }
static int shim_WSAResetEvent( void *event )
{
    log_line( "[SHIM] WSAResetEvent event=%p", event );
    return shim_ResetEvent( event );
}
static int shim_WSASetEvent( void *event )
{
    log_line( "[SHIM] WSASetEvent event=%p", event );
    return shim_SetEvent( event );
}
static uint32_t shim_WSAWaitForMultipleEvents( uint32_t count, const void **events,
                                               int wait_all, uint32_t timeout, int alertable )
{
    (void)timeout;
    (void)alertable;
    log_line( "[SHIM] WSAWaitForMultipleEvents count=%u all=%d timeout=%u",
              count, wait_all, timeout );
    return shim_WaitForMultipleObjects( count, events, wait_all, timeout );
}
static int shim_WSAEnumNetworkEvents( uintptr_t socket, void *event, void *events )
{
    log_line( "[SHIM] WSAEnumNetworkEvents fd=%d event=%p events=%p",
              (int)socket, event, events );
    if (events) memset( events, 0, 64 );
    return 0;
}
static int shim_WSAEventSelect( uintptr_t socket, void *event, long flags )
{
    (void)socket;
    (void)flags;
    if (event) shim_SetEvent( event );
    log_line( "[SHIM] WSAEventSelect fd=%d event=%p flags=0x%lx", (int)socket, event, flags );
    return 0;
}
static int shim_WSAIoctl( uintptr_t socket, uint32_t code, void *in_buffer, uint32_t in_size,
                          void *out_buffer, uint32_t out_size, uint32_t *bytes,
                          void *overlapped, void *routine )
{
    (void)in_buffer;
    (void)in_size;
    (void)out_buffer;
    (void)out_size;
    (void)overlapped;
    (void)routine;
    if (bytes) *bytes = 0;
    log_line( "[SHIM] WSAIoctl fd=%d code=0x%x in=%u out=%u -> unsupported",
              (int)socket, code, in_size, out_size );
    pe_set_last_error( WIN_ERROR_NOT_SUPPORTED );
    return WIN_SOCKET_ERROR;
}
static int shim_WSAStringToAddressW( uint16_t *text, int family, void *protocol_info,
                                     void *addr, int *addr_len )
{
    char buffer[128];
    int i;

    (void)protocol_info;
    if (!text || !addr || !addr_len)
    {
        pe_set_last_error( WSAEFAULT );
        return WIN_SOCKET_ERROR;
    }
    for (i = 0; i + 1 < (int)sizeof(buffer) && text[i]; i++)
        buffer[i] = text[i] < 0x80 ? (char)text[i] : '?';
    buffer[i] = 0;
    log_line( "[SHIM] WSAStringToAddressW text=%s family=%d", buffer, family );
    if ((family == AF_INET || family == 0) && *addr_len >= (int)sizeof(struct sockaddr_in))
    {
        struct sockaddr_in *in = addr;

        memset( in, 0, sizeof(*in) );
        in->sin_family = AF_INET;
        if (inet_pton( AF_INET, buffer, &in->sin_addr ) == 1)
        {
            *addr_len = sizeof(*in);
            pe_set_last_error( 0 );
            return 0;
        }
    }
    pe_set_last_error( WIN_ERROR_NOT_SUPPORTED );
    return WIN_SOCKET_ERROR;
}
static int shim___WSAFDIsSet( uintptr_t socket, void *set )
{
    struct win_fd_set *win_set = set;
    uint32_t i;

    if (!win_set) return 0;
    for (i = 0; i < win_set->fd_count && i < WIN_FD_SETSIZE; i++)
        if (win_set->fd_array[i] == socket) return 1;
    return 0;
}

static uintptr_t shim_socket( int af, int type, int protocol )
{
    int fd;

    if (!pe_ensure_socket()) return WIN_INVALID_SOCKET;
    log_line( "[SHIM] socket enter af=%d type=%d proto=%d", af, type, protocol );
    fd = socket( win_family_to_posix( af ), type, protocol );
    if (fd < 0)
    {
        pe_set_wsa_errno();
        log_line( "[SHIM] socket af=%d type=%d proto=%d -> err=%d", af, type, protocol, pe_last_error );
        return WIN_INVALID_SOCKET;
    }
    log_line( "[SHIM] socket af=%d type=%d proto=%d -> %d", af, type, protocol, fd );
    pe_set_last_error( 0 );
    return (uintptr_t)fd;
}

static int shim_closesocket( uintptr_t socket )
{
    int ret = close( (int)socket );

    log_line( "[SHIM] closesocket fd=%d ret=%d errno=%d", (int)socket, ret, errno );
    if (ret < 0)
    {
        pe_set_wsa_errno();
        return WIN_SOCKET_ERROR;
    }
    pe_set_last_error( 0 );
    return 0;
}

static uintptr_t shim_accept( uintptr_t socket, struct sockaddr *addr, int *addrlen )
{
    struct sockaddr_storage posix_addr;
    socklen_t posix_len = sizeof(posix_addr);
    int fd;

    fd = accept( (int)socket, addr ? (struct sockaddr *)&posix_addr : NULL,
                 addr && addrlen ? &posix_len : NULL );
    if (fd < 0)
    {
        pe_set_wsa_errno();
        log_line( "[SHIM] accept fd=%d ret=%d errno=%d wsa=%d",
                  (int)socket, fd, errno, pe_last_error );
        return WIN_INVALID_SOCKET;
    }
    if (addr && addrlen)
    {
        if (*addrlen > (int)posix_len) *addrlen = (int)posix_len;
        sockaddr_posix_to_win( addr, (struct sockaddr *)&posix_addr, (socklen_t)*addrlen );
    }
    log_line( "[SHIM] accept fd=%d -> %d", (int)socket, fd );
    pe_set_last_error( 0 );
    return (uintptr_t)fd;
}

static int shim_bind( uintptr_t socket, const struct sockaddr *addr, int addrlen )
{
    struct sockaddr_storage posix_addr;
    socklen_t posix_len;
    int ret;

    if (!addr || addrlen <= 0)
    {
        pe_set_last_error( WSAEFAULT );
        return WIN_SOCKET_ERROR;
    }
    sockaddr_win_to_posix( addr, addrlen, &posix_addr, &posix_len );
    ret = bind( (int)socket, (const struct sockaddr *)&posix_addr, posix_len );
    if (ret < 0)
    {
        pe_set_wsa_errno();
        log_line( "[SHIM] bind fd=%d family=%d len=%d ret=%d errno=%d wsa=%d",
                  (int)socket, addr->sa_family, addrlen, ret, errno, pe_last_error );
        return WIN_SOCKET_ERROR;
    }
    log_line( "[SHIM] bind fd=%d family=%d OK", (int)socket, addr->sa_family );
    pe_set_last_error( 0 );
    return 0;
}

static int shim_connect( uintptr_t socket, const struct sockaddr *addr, int addrlen )
{
    struct sockaddr_storage posix_addr;
    socklen_t posix_len;
    int ret;

    if (!addr || addrlen <= 0)
    {
        pe_set_last_error( WSAEFAULT );
        return WIN_SOCKET_ERROR;
    }
    sockaddr_win_to_posix( addr, addrlen, &posix_addr, &posix_len );
    log_line( "[SHIM] connect enter fd=%d family=%d len=%d", (int)socket, addr->sa_family, addrlen );
    ret = connect( (int)socket, (const struct sockaddr *)&posix_addr, posix_len );
    if (ret < 0)
    {
        pe_set_wsa_errno();
        log_line( "[SHIM] connect fd=%d family=%d len=%d ret=%d errno=%d wsa=%d",
                  (int)socket, addr->sa_family, addrlen, ret, errno, pe_last_error );
        return WIN_SOCKET_ERROR;
    }
    log_line( "[SHIM] connect fd=%d family=%d OK", (int)socket, addr->sa_family );
    pe_set_last_error( 0 );
    return 0;
}

static int shim_getaddrinfo( const char *node, const char *service,
                             const struct win_addrinfo *win_hints, struct win_addrinfo **out )
{
    struct addrinfo hints;
    struct addrinfo *posix = NULL;
    struct addrinfo *cur;
    struct win_addrinfo *head = NULL;
    struct win_addrinfo **tail = &head;
    int ret;
    unsigned int count = 0;

    if (!out) return WIN_EAI_FAIL;
    *out = NULL;
    memset( &hints, 0, sizeof(hints) );
    if (win_hints)
    {
        hints.ai_flags = win_hints->ai_flags;
        hints.ai_family = win_family_to_posix( win_hints->ai_family );
        hints.ai_socktype = win_hints->ai_socktype;
        hints.ai_protocol = win_hints->ai_protocol;
    }

    log_line( "[SHIM] getaddrinfo node=%s service=%s family=%d type=%d",
              node ? node : "(null)", service ? service : "(null)",
              win_hints ? win_hints->ai_family : 0, win_hints ? win_hints->ai_socktype : 0 );
    if (!pe_ensure_socket()) return WIN_EAI_FAIL;

    if (node && (!strcasecmp( node, "curl.lux.pm" ) || !strcasecmp( node, "lux.pm" )))
    {
        if (!win_hints || !win_hints->ai_family || win_hints->ai_family == AF_INET)
        {
            int port = parse_service_port( service );

            *out = alloc_ipv4_addrinfo( "92.252.16.233", port ? port : 443, win_hints );
            if (!*out) return WIN_EAI_MEMORY;
            log_line( "[SHIM] getaddrinfo static %s -> 92.252.16.233:%d out=%p addr=%p",
                      node, port ? port : 443, *out, (*out)->ai_addr );
            pe_set_last_error( 0 );
            return 0;
        }
        log_line( "[SHIM] getaddrinfo static %s no records for family=%d", node, win_hints->ai_family );
        return WIN_EAI_NONAME;
    }

    ret = getaddrinfo( node, service, win_hints ? &hints : NULL, &posix );
    if (ret)
    {
        ret = gai_to_wsa( ret );
        pe_set_last_error( ret );
        log_line( "[SHIM] getaddrinfo failed ret=%d", ret );
        return ret;
    }

    for (cur = posix; cur; cur = cur->ai_next)
    {
        struct win_addrinfo *entry = calloc( 1, sizeof(*entry) );
        if (!entry) continue;

        entry->ai_flags = cur->ai_flags;
        entry->ai_family = posix_family_to_win( cur->ai_family );
        entry->ai_socktype = cur->ai_socktype;
        entry->ai_protocol = cur->ai_protocol;
        entry->ai_addrlen = cur->ai_addrlen;
        if (cur->ai_canonname) entry->ai_canonname = shim__strdup( cur->ai_canonname );
        if (cur->ai_addr && cur->ai_addrlen)
        {
            entry->ai_addr = malloc( cur->ai_addrlen );
            if (entry->ai_addr)
                sockaddr_posix_to_win( entry->ai_addr, cur->ai_addr, cur->ai_addrlen );
        }
        *tail = entry;
        tail = &entry->ai_next;
        count++;
    }

    freeaddrinfo( posix );
    *out = head;
    log_line( "[SHIM] getaddrinfo OK count=%u", count );
    pe_set_last_error( 0 );
    return head ? 0 : WIN_EAI_NONAME;
}

static void shim_freeaddrinfo( struct win_addrinfo *addr )
{
    log_line( "[SHIM] freeaddrinfo %p", addr );
    while (addr)
    {
        struct win_addrinfo *next = addr->ai_next;
        free( addr->ai_canonname );
        free( addr->ai_addr );
        free( addr );
        addr = next;
    }
}

static int shim_gethostname( char *name, int len )
{
    const char hostname[] = "switch";
    if (!name || len <= (int)sizeof(hostname))
    {
        pe_set_last_error( WSAEFAULT );
        log_line( "[SHIM] gethostname invalid len=%d", len );
        return WIN_SOCKET_ERROR;
    }
    memcpy( name, hostname, sizeof(hostname) );
    log_line( "[SHIM] gethostname -> %s", hostname );
    pe_set_last_error( 0 );
    return 0;
}

static int shim_getsockname( uintptr_t socket, struct sockaddr *addr, int *addrlen )
{
    struct sockaddr_storage posix_addr;
    socklen_t len;
    int ret;

    if (!addr || !addrlen)
    {
        pe_set_last_error( WSAEFAULT );
        return WIN_SOCKET_ERROR;
    }
    len = sizeof(posix_addr);
    ret = getsockname( (int)socket, (struct sockaddr *)&posix_addr, &len );
    if (ret < 0)
    {
        pe_set_wsa_errno();
        log_line( "[SHIM] getsockname fd=%d ret=%d errno=%d wsa=%d",
                  (int)socket, ret, errno, pe_last_error );
        return WIN_SOCKET_ERROR;
    }
    if (*addrlen > (int)len) *addrlen = (int)len;
    sockaddr_posix_to_win( addr, (struct sockaddr *)&posix_addr, (socklen_t)*addrlen );
    log_line( "[SHIM] getsockname fd=%d family=%d len=%d", (int)socket, addr->sa_family, *addrlen );
    pe_set_last_error( 0 );
    return 0;
}

static int shim_getpeername( uintptr_t socket, struct sockaddr *addr, int *addrlen )
{
    struct sockaddr_storage posix_addr;
    socklen_t len;
    int ret;

    if (!addr || !addrlen)
    {
        pe_set_last_error( WSAEFAULT );
        return WIN_SOCKET_ERROR;
    }
    len = sizeof(posix_addr);
    ret = getpeername( (int)socket, (struct sockaddr *)&posix_addr, &len );
    if (ret < 0)
    {
        pe_set_wsa_errno();
        log_line( "[SHIM] getpeername fd=%d ret=%d errno=%d wsa=%d",
                  (int)socket, ret, errno, pe_last_error );
        return WIN_SOCKET_ERROR;
    }
    if (*addrlen > (int)len) *addrlen = (int)len;
    sockaddr_posix_to_win( addr, (struct sockaddr *)&posix_addr, (socklen_t)*addrlen );
    log_line( "[SHIM] getpeername fd=%d family=%d len=%d", (int)socket, addr->sa_family, *addrlen );
    pe_set_last_error( 0 );
    return 0;
}

static int shim_getsockopt( uintptr_t socket, int level, int optname, char *optval, int *optlen )
{
    socklen_t len;
    int ret;

    if (!optlen)
    {
        pe_set_last_error( WSAEFAULT );
        return WIN_SOCKET_ERROR;
    }
    len = (socklen_t)*optlen;
    ret = getsockopt( (int)socket, level, optname, optval, &len );
    if (ret < 0)
    {
        pe_set_wsa_errno();
        log_line( "[SHIM] getsockopt fd=%d level=%d opt=%d ret=%d errno=%d wsa=%d",
                  (int)socket, level, optname, ret, errno, pe_last_error );
        return WIN_SOCKET_ERROR;
    }
    if (level == SOL_SOCKET && optname == SO_ERROR && optval && len >= sizeof(int))
    {
        int *error = (int *)optval;
        *error = errno_to_wsa( *error );
    }
    *optlen = (int)len;
    log_line( "[SHIM] getsockopt fd=%d level=%d opt=%d len=%d", (int)socket, level, optname, *optlen );
    pe_set_last_error( 0 );
    return 0;
}

static uint32_t shim_htonl( uint32_t value ) { return __builtin_bswap32( value ); }
static uint16_t shim_htons( uint16_t value ) { return __builtin_bswap16( value ); }
static uint32_t shim_ntohl( uint32_t value ) { return __builtin_bswap32( value ); }
static uint16_t shim_ntohs( uint16_t value ) { return __builtin_bswap16( value ); }
static int shim_inet_pton( int af, const char *src, void *dst )
{
    unsigned int a, b, c, d;
    (void)af;
    if (!src || !dst) return 0;
    if (sscanf( src, "%u.%u.%u.%u", &a, &b, &c, &d ) == 4 &&
        a < 256 && b < 256 && c < 256 && d < 256)
    {
        unsigned char *bytes = dst;
        bytes[0] = (unsigned char)a;
        bytes[1] = (unsigned char)b;
        bytes[2] = (unsigned char)c;
        bytes[3] = (unsigned char)d;
        return 1;
    }
    return 0;
}

static int shim_ioctlsocket( uintptr_t socket, long cmd, unsigned long *argp )
{
    int ret;

    if (!argp) return WIN_SOCKET_ERROR;
    if ((unsigned long)cmd == WIN_FIONBIO)
    {
        int flags = fcntl( (int)socket, F_GETFL, 0 );
        if (flags < 0)
        {
            pe_set_wsa_errno();
            return WIN_SOCKET_ERROR;
        }
        if (*argp) flags |= O_NONBLOCK;
        else flags &= ~O_NONBLOCK;
        ret = fcntl( (int)socket, F_SETFL, flags );
    }
    else if ((unsigned long)cmd == WIN_FIONREAD)
    {
        int available = 0;
        ret = ioctl( (int)socket, FIONREAD, &available );
        *argp = (unsigned long)available;
    }
    else ret = ioctl( (int)socket, (unsigned long)cmd, argp );

    if (ret < 0)
    {
        pe_set_wsa_errno();
        log_line( "[SHIM] ioctlsocket fd=%d cmd=0x%lx ret=%d errno=%d wsa=%d",
                  (int)socket, cmd, ret, errno, pe_last_error );
        return WIN_SOCKET_ERROR;
    }
    log_line( "[SHIM] ioctlsocket fd=%d cmd=0x%lx arg=%lu", (int)socket, cmd, *argp );
    pe_set_last_error( 0 );
    return 0;
}

static int shim_listen( uintptr_t socket, int backlog )
{
    int ret = listen( (int)socket, backlog );

    if (ret < 0)
    {
        pe_set_wsa_errno();
        log_line( "[SHIM] listen fd=%d backlog=%d ret=%d errno=%d wsa=%d",
                  (int)socket, backlog, ret, errno, pe_last_error );
        return WIN_SOCKET_ERROR;
    }
    log_line( "[SHIM] listen fd=%d backlog=%d OK", (int)socket, backlog );
    pe_set_last_error( 0 );
    return 0;
}

static int shim_recv( uintptr_t socket, char *buffer, int len, int flags )
{
    int ret = recv( (int)socket, buffer, len, flags );

    if (ret < 0)
    {
        pe_set_wsa_errno();
        log_line( "[SHIM] recv fd=%d len=%d flags=0x%x ret=%d errno=%d wsa=%d",
                  (int)socket, len, flags, ret, errno, pe_last_error );
        return WIN_SOCKET_ERROR;
    }
    log_line( "[SHIM] recv fd=%d len=%d flags=0x%x -> %d", (int)socket, len, flags, ret );
    pe_set_last_error( 0 );
    return ret;
}

static int shim_recvfrom( uintptr_t socket, char *buffer, int len, int flags,
                          struct sockaddr *from, int *fromlen )
{
    struct sockaddr_storage posix_addr;
    socklen_t posix_len = sizeof(posix_addr);
    int ret;

    ret = recvfrom( (int)socket, buffer, len, flags, (struct sockaddr *)&posix_addr, &posix_len );
    if (ret < 0)
    {
        pe_set_wsa_errno();
        log_line( "[SHIM] recvfrom fd=%d len=%d flags=0x%x ret=%d errno=%d wsa=%d",
                  (int)socket, len, flags, ret, errno, pe_last_error );
        return WIN_SOCKET_ERROR;
    }
    if (from && fromlen)
    {
        if (*fromlen > (int)posix_len) *fromlen = (int)posix_len;
        sockaddr_posix_to_win( from, (struct sockaddr *)&posix_addr, (socklen_t)*fromlen );
    }
    log_line( "[SHIM] recvfrom fd=%d len=%d flags=0x%x -> %d", (int)socket, len, flags, ret );
    pe_set_last_error( 0 );
    return ret;
}

static int shim_select( int nfds, struct win_fd_set *readfds, struct win_fd_set *writefds,
                        struct win_fd_set *exceptfds, struct win_timeval *timeout )
{
    fd_set posix_read;
    fd_set posix_write;
    fd_set posix_except;
    struct win_fd_set old_read = {0};
    struct win_fd_set old_write = {0};
    struct win_fd_set old_except = {0};
    struct timeval posix_timeout;
    struct timeval *timeout_ptr = NULL;
    int maxfd = -1;
    int ret;

    (void)nfds;
    if (readfds) old_read = *readfds;
    if (writefds) old_write = *writefds;
    if (exceptfds) old_except = *exceptfds;
    win_fd_set_to_posix( readfds, &posix_read, &maxfd );
    win_fd_set_to_posix( writefds, &posix_write, &maxfd );
    win_fd_set_to_posix( exceptfds, &posix_except, &maxfd );
    if (timeout)
    {
        posix_timeout.tv_sec = timeout->tv_sec;
        posix_timeout.tv_usec = timeout->tv_usec;
        timeout_ptr = &posix_timeout;
    }

    log_line( "[SHIM] select enter maxfd=%d r=%u w=%u e=%u timeout=%s",
              maxfd,
              readfds ? old_read.fd_count : 0,
              writefds ? old_write.fd_count : 0,
              exceptfds ? old_except.fd_count : 0,
              timeout ? "set" : "null" );
    ret = select( maxfd + 1, readfds ? &posix_read : NULL, writefds ? &posix_write : NULL,
                  exceptfds ? &posix_except : NULL, timeout_ptr );
    if (ret < 0)
    {
        pe_set_wsa_errno();
        log_line( "[SHIM] select ret=%d errno=%d wsa=%d", ret, errno, pe_last_error );
        return WIN_SOCKET_ERROR;
    }
    posix_fd_set_to_win( readfds, &old_read, &posix_read );
    posix_fd_set_to_win( writefds, &old_write, &posix_write );
    posix_fd_set_to_win( exceptfds, &old_except, &posix_except );
    log_line( "[SHIM] select ret=%d", ret );
    pe_set_last_error( 0 );
    return ret;
}

static int shim_send( uintptr_t socket, const char *buffer, int len, int flags )
{
    int ret = send( (int)socket, buffer, len, flags );

    if (ret < 0)
    {
        pe_set_wsa_errno();
        log_line( "[SHIM] send fd=%d len=%d flags=0x%x ret=%d errno=%d wsa=%d",
                  (int)socket, len, flags, ret, errno, pe_last_error );
        return WIN_SOCKET_ERROR;
    }
    log_line( "[SHIM] send fd=%d len=%d flags=0x%x -> %d", (int)socket, len, flags, ret );
    pe_set_last_error( 0 );
    return ret;
}

static int shim_sendto( uintptr_t socket, const char *buffer, int len, int flags,
                        const struct sockaddr *to, int tolen )
{
    struct sockaddr_storage posix_addr;
    socklen_t posix_len;
    int ret;

    sockaddr_win_to_posix( to, tolen, &posix_addr, &posix_len );
    ret = sendto( (int)socket, buffer, len, flags, (struct sockaddr *)&posix_addr, posix_len );
    if (ret < 0)
    {
        pe_set_wsa_errno();
        log_line( "[SHIM] sendto fd=%d len=%d flags=0x%x ret=%d errno=%d wsa=%d",
                  (int)socket, len, flags, ret, errno, pe_last_error );
        return WIN_SOCKET_ERROR;
    }
    log_line( "[SHIM] sendto fd=%d len=%d flags=0x%x -> %d", (int)socket, len, flags, ret );
    pe_set_last_error( 0 );
    return ret;
}

static int shim_setsockopt( uintptr_t socket, int level, int optname, const char *optval, int optlen )
{
    int ret = setsockopt( (int)socket, level, optname, optval, (socklen_t)optlen );

    if (ret < 0)
    {
        pe_set_wsa_errno();
        log_line( "[SHIM] setsockopt fd=%d level=%d opt=%d ret=%d errno=%d wsa=%d",
                  (int)socket, level, optname, ret, errno, pe_last_error );
        return WIN_SOCKET_ERROR;
    }
    log_line( "[SHIM] setsockopt fd=%d level=%d opt=%d len=%d", (int)socket, level, optname, optlen );
    pe_set_last_error( 0 );
    return 0;
}

static int shim_shutdown( uintptr_t socket, int how )
{
    int ret = shutdown( (int)socket, how );

    if (ret < 0)
    {
        pe_set_wsa_errno();
        log_line( "[SHIM] shutdown fd=%d how=%d ret=%d errno=%d wsa=%d",
                  (int)socket, how, ret, errno, pe_last_error );
        return WIN_SOCKET_ERROR;
    }
    log_line( "[SHIM] shutdown fd=%d how=%d OK", (int)socket, how );
    pe_set_last_error( 0 );
    return 0;
}

static void *shim_InitSecurityInterfaceA(void)
{
    static unsigned char security_table[512];
    return security_table;
}

static int shim_BCryptGenRandom( void *algorithm, unsigned char *buffer, uint32_t size, uint32_t flags )
{
    uint64_t seed = armGetSystemTick() ^ (uintptr_t)buffer ^ flags;
    uint32_t i;

    (void)algorithm;
    for (i = 0; buffer && i < size; i++)
    {
        seed = seed * 6364136223846793005ULL + 1;
        buffer[i] = (unsigned char)(seed >> 32);
    }
    return 0;
}

static uint32_t shim_if_nametoindex( const char *name )
{
    (void)name;
    return 0;
}

static void init_fake_teb(void)
{
    uintptr_t sp;
    uint64_t thread_cookie = (uint64_t)(uintptr_t)&pe_fake_teb;

    memset( pe_fake_teb, 0, sizeof(pe_fake_teb) );
    __asm__ volatile( "mov %0, sp" : "=r"(sp) );

    put64( pe_fake_teb + 0x08, thread_cookie );
    put64( pe_fake_teb + 0x10, (uint64_t)(sp - 0x100000) );
    put64( pe_fake_teb + 0x18, (uint64_t)(sp + 0x10000) );
}

static uintptr_t call_pe_function3( void *func, uintptr_t arg0, uintptr_t arg1, uintptr_t arg2 )
{
    uintptr_t ret;
    uintptr_t teb = (uintptr_t)pe_fake_teb;

    __asm__ volatile(
        "mov x16, %[func]\n"
        "mov x17, %[teb]\n"
        "mov x0, %[arg0]\n"
        "mov x1, %[arg1]\n"
        "mov x2, %[arg2]\n"
        "mov x20, x18\n"
        "mov x18, x17\n"
        "blr x16\n"
        "mov x18, x20\n"
        "mov %[ret], x0\n"
        : [ret] "=r"(ret)
        : [func] "r"(func), [teb] "r"(teb),
          [arg0] "r"(arg0), [arg1] "r"(arg1), [arg2] "r"(arg2)
        : "x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7", "x8", "x9",
          "x10", "x11", "x12", "x13", "x14", "x15", "x16", "x17", "x20",
          "x30", "memory", "cc" );

    return ret;
}
#endif

static uint16_t get16( const unsigned char *ptr )
{
    return ptr[0] | ((uint16_t)ptr[1] << 8);
}

static uint32_t get32( const unsigned char *ptr )
{
    return ptr[0] | ((uint32_t)ptr[1] << 8) | ((uint32_t)ptr[2] << 16) | ((uint32_t)ptr[3] << 24);
}

static uint64_t get64( const unsigned char *ptr )
{
    return get32( ptr ) | ((uint64_t)get32( ptr + 4 ) << 32);
}

#if PE_REAL_EXECUTE
static void put32( unsigned char *ptr, uint32_t value )
{
    ptr[0] = value & 0xff;
    ptr[1] = (value >> 8) & 0xff;
    ptr[2] = (value >> 16) & 0xff;
    ptr[3] = (value >> 24) & 0xff;
}
#endif

static void put64( unsigned char *ptr, uint64_t value )
{
    ptr[0] = value & 0xff;
    ptr[1] = (value >> 8) & 0xff;
    ptr[2] = (value >> 16) & 0xff;
    ptr[3] = (value >> 24) & 0xff;
    ptr[4] = (value >> 32) & 0xff;
    ptr[5] = (value >> 40) & 0xff;
    ptr[6] = (value >> 48) & 0xff;
    ptr[7] = (value >> 56) & 0xff;
}

static uint32_t align_up_u32( uint32_t value, uint32_t align )
{
    return (value + align - 1) & ~(align - 1);
}

static int checked_range( size_t offset, size_t size, size_t limit )
{
    return offset <= limit && size <= limit - offset;
}

static const char *read_cstring( const unsigned char *base, size_t size, uint32_t offset )
{
    size_t pos;

    if (offset >= size) return NULL;
    for (pos = offset; pos < size; pos++)
        if (!base[pos]) return (const char *)base + offset;
    return NULL;
}

static int read_file( const char *path, unsigned char **out_data, size_t *out_size )
{
    FILE *file;
    long size;
    unsigned char *data;

    file = fopen( path, "rb" );
    if (!file) return 0;

    if (fseek( file, 0, SEEK_END ) == -1)
    {
        fclose( file );
        return 0;
    }
    size = ftell( file );
    if (size <= 0)
    {
        fclose( file );
        return 0;
    }
    rewind( file );

    data = malloc( (size_t)size );
    if (!data)
    {
        fclose( file );
        errno = ENOMEM;
        return 0;
    }

    if (fread( data, 1, (size_t)size, file ) != (size_t)size)
    {
        free( data );
        fclose( file );
        return 0;
    }

    fclose( file );
    *out_data = data;
    *out_size = (size_t)size;
    return 1;
}

static int load_default_sample( unsigned char **out_data, size_t *out_size, const char **out_path )
{
    unsigned int i;

#if PE_REAL_EXECUTE
    for (i = 0; i < sizeof(target_paths) / sizeof(target_paths[0]); i++)
    {
        FILE *file = fopen( target_paths[i], "rb" );
        size_t len;

        if (!file) continue;
        len = fread( pe_target_path, 1, sizeof(pe_target_path) - 1, file );
        fclose( file );
        pe_target_path[len] = 0;
        while (len && (pe_target_path[len - 1] == '\n' || pe_target_path[len - 1] == '\r' ||
                       pe_target_path[len - 1] == ' ' || pe_target_path[len - 1] == '\t'))
            pe_target_path[--len] = 0;
        if (!pe_target_path[0]) continue;

        log_line( "[TARGET] loaded %s -> %s", target_paths[i], pe_target_path );
        log_line( "[FILE] trying %s", pe_target_path );
        if (read_file( pe_target_path, out_data, out_size ))
        {
            *out_path = pe_target_path;
            return 1;
        }
        log_line( "[TARGET] explicit target not readable, falling back" );
    }
#endif

    for (i = 0; i < sizeof(default_paths) / sizeof(default_paths[0]); i++)
    {
        log_line( "[FILE] trying %s", default_paths[i] );
        if (read_file( default_paths[i], out_data, out_size ))
        {
#if PE_REAL_EXECUTE
            snprintf( pe_target_path, sizeof(pe_target_path), "%s", default_paths[i] );
#endif
            *out_path = default_paths[i];
            return 1;
        }
    }

    return 0;
}

#if PE_REAL_EXECUTE
static void parse_pe_args( char *line )
{
    char *src = line;

    pe_argc = 0;
    while (*src && pe_argc < PE_MAX_ARGS)
    {
        char quote = 0;
        char *dst;

        while (*src == ' ' || *src == '\t' || *src == '\r' || *src == '\n') src++;
        if (!*src || *src == '#') break;

        pe_argv[pe_argc++] = src;
        dst = src;
        while (*src)
        {
            char ch = *src++;

            if (quote)
            {
                if (ch == quote) quote = 0;
                else *dst++ = ch;
                continue;
            }

            if (ch == '"' || ch == '\'')
            {
                quote = ch;
                continue;
            }
            if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n')
                break;
            *dst++ = ch;
        }
        *dst = 0;
    }
    pe_argv[pe_argc] = NULL;

    if (!pe_argc)
    {
        snprintf( pe_arg_storage, sizeof(pe_arg_storage), "%s", default_args );
        parse_pe_args( pe_arg_storage );
    }
}

static void load_pe_args(void)
{
    unsigned int i;

    snprintf( pe_arg_storage, sizeof(pe_arg_storage), "%s", default_args );
    for (i = 0; i < sizeof(args_paths) / sizeof(args_paths[0]); i++)
    {
        FILE *file = fopen( args_paths[i], "rb" );
        size_t len;

        if (!file) continue;
        len = fread( pe_arg_storage, 1, sizeof(pe_arg_storage) - 1, file );
        fclose( file );
        pe_arg_storage[len] = 0;
        log_line( "[ARGS] loaded %s", args_paths[i] );
        break;
    }

    parse_pe_args( pe_arg_storage );
    for (i = 0; i < (unsigned int)pe_argc; i++)
        log_line( "[ARGS] argv[%u]=%s", i, pe_argv[i] );
}
#endif

static int parse_pe_headers( const unsigned char *file, size_t file_size, struct pe_image *pe )
{
    uint32_t data_dir_count;
    unsigned int i;

    memset( pe, 0, sizeof(*pe) );
    pe->file = file;
    pe->file_size = file_size;

    if (file_size < 0x100) return fail_line( "file too small" );
    if (get16( file ) != 0x5a4d) return fail_line( "missing MZ signature" );

    pe->pe_offset = get32( file + 0x3c );
    if (!checked_range( pe->pe_offset, 4 + 20, file_size ))
        return fail_line( "invalid PE header offset 0x%x", pe->pe_offset );
    if (get32( file + pe->pe_offset ) != 0x00004550)
        return fail_line( "missing PE signature" );

    pe->coff_offset = pe->pe_offset + 4;
    if (get16( file + pe->coff_offset ) != PE_MACHINE_ARM64)
        return fail_line( "unsupported machine 0x%04x", get16( file + pe->coff_offset ) );

    pe->section_count = get16( file + pe->coff_offset + 2 );
    pe->optional_size = get16( file + pe->coff_offset + 16 );
    pe->opt_offset = pe->coff_offset + 20;
    pe->section_offset = pe->opt_offset + pe->optional_size;

    if (pe->optional_size < 0xf0)
        return fail_line( "optional header too small 0x%x", pe->optional_size );
    if (!checked_range( pe->opt_offset, pe->optional_size, file_size ) ||
        !checked_range( pe->section_offset, (size_t)pe->section_count * 40, file_size ))
        return fail_line( "section table outside file" );
    if (get16( file + pe->opt_offset ) != PE_MAGIC_PE32_PLUS)
        return fail_line( "unsupported optional header magic 0x%04x", get16( file + pe->opt_offset ) );

    pe->entry_rva = get32( file + pe->opt_offset + 16 );
    pe->image_base = get64( file + pe->opt_offset + 24 );
    pe->section_alignment = get32( file + pe->opt_offset + 32 );
    pe->file_alignment = get32( file + pe->opt_offset + 36 );
    pe->image_size = get32( file + pe->opt_offset + 56 );
    pe->header_size = get32( file + pe->opt_offset + 60 );
    data_dir_count = get32( file + pe->opt_offset + 108 );
    if (data_dir_count > MAX_DATA_DIRS) data_dir_count = MAX_DATA_DIRS;

    for (i = 0; i < data_dir_count; i++)
    {
        pe->dir_rva[i] = get32( file + pe->opt_offset + 112 + i * 8 );
        pe->dir_size[i] = get32( file + pe->opt_offset + 112 + i * 8 + 4 );
    }

    if (!pe->image_size || pe->image_size > 256 * 1024 * 1024)
        return fail_line( "unreasonable image size 0x%x", pe->image_size );
    if (!pe->header_size || pe->header_size > pe->image_size)
        return fail_line( "invalid header size 0x%x", pe->header_size );

    log_line( "[PE] machine=ARM64 sections=%u entry_rva=0x%x image_base=0x%016llx",
              pe->section_count, pe->entry_rva, (unsigned long long)pe->image_base );
    log_line( "[PE] image_size=0x%x headers=0x%x section_align=0x%x file_align=0x%x",
              pe->image_size, pe->header_size, pe->section_alignment, pe->file_alignment );
    log_line( "[DIR] import=0x%x/0x%x reloc=0x%x/0x%x tls=0x%x/0x%x loadcfg=0x%x/0x%x",
              pe->dir_rva[DIR_IMPORT], pe->dir_size[DIR_IMPORT],
              pe->dir_rva[DIR_RELOC], pe->dir_size[DIR_RELOC],
              pe->dir_rva[DIR_TLS], pe->dir_size[DIR_TLS],
              pe->dir_rva[DIR_LOAD_CONFIG], pe->dir_size[DIR_LOAD_CONFIG] );
    return 1;
}

static int rva_in_image( const struct pe_image *pe, uint32_t rva, uint32_t size )
{
    return rva <= pe->image_size && size <= pe->image_size - rva;
}

static int va_to_rva( uint64_t actual_base, uint32_t image_size, uint64_t va, uint32_t *rva )
{
    if (va < actual_base || va >= actual_base + image_size) return 0;
    *rva = (uint32_t)(va - actual_base);
    return 1;
}

static unsigned char *map_pe_image( const struct pe_image *pe )
{
    unsigned char *image;
    unsigned int i;
    size_t header_copy;

    image = horizon_mmap( NULL, pe->image_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0 );
    if (image == MAP_FAILED)
    {
        fail_line( "mmap image_size=0x%x errno=%d", pe->image_size, errno );
        return NULL;
    }

    memset( image, 0, pe->image_size );
    header_copy = pe->header_size < pe->file_size ? pe->header_size : pe->file_size;
    memcpy( image, pe->file, header_copy );

    for (i = 0; i < pe->section_count; i++)
    {
        const unsigned char *hdr = pe->file + pe->section_offset + i * 40;
        char name[9];
        uint32_t virtual_size = get32( hdr + 8 );
        uint32_t virtual_address = get32( hdr + 12 );
        uint32_t raw_size = get32( hdr + 16 );
        uint32_t raw_ptr = get32( hdr + 20 );
        uint32_t characteristics = get32( hdr + 36 );

        memcpy( name, hdr, 8 );
        name[8] = 0;

        if (!rva_in_image( pe, virtual_address, virtual_size ) ||
            !rva_in_image( pe, virtual_address, raw_size ) ||
            !checked_range( raw_ptr, raw_size, pe->file_size ))
        {
            fail_line( "invalid section %u %.8s va=0x%x raw=0x%x/0x%x",
                       i, name, virtual_address, raw_ptr, raw_size );
            horizon_munmap( image, pe->image_size );
            return NULL;
        }

        if (raw_size) memcpy( image + virtual_address, pe->file + raw_ptr, raw_size );
        log_line( "[SEC] %.8s va=0x%x vsize=0x%x raw=0x%x chars=0x%08x",
                  name, virtual_address, virtual_size, raw_size, characteristics );
    }

    log_line( "[MAP] image=%p size=0x%x", image, pe->image_size );
    return image;
}

static int apply_base_relocs( const struct pe_image *pe, unsigned char *image, struct run_report *report )
{
    uint64_t actual_base = (uint64_t)(uintptr_t)image;
    int64_t delta = (int64_t)(actual_base - pe->image_base);
    uint32_t pos_rva = pe->dir_rva[DIR_RELOC];
    uint32_t end_rva = pos_rva + pe->dir_size[DIR_RELOC];

    if (!pe->dir_rva[DIR_RELOC] || !pe->dir_size[DIR_RELOC])
    {
        if (delta)
        {
            report->run_blockers++;
            log_line( "[BLOCK] image needs relocation but has no reloc directory" );
        }
        return 1;
    }

    if (!delta)
    {
        log_line( "[RELOC] preferred base matched, no relocations applied" );
        return 1;
    }

    if (end_rva < pos_rva || !rva_in_image( pe, pos_rva, pe->dir_size[DIR_RELOC] ))
        return fail_line( "reloc directory outside image" );

    while (pos_rva < end_rva)
    {
        uint32_t page_rva;
        uint32_t block_size;
        uint32_t entries;
        uint32_t i;

        if (!rva_in_image( pe, pos_rva, 8 )) return fail_line( "truncated reloc block" );
        page_rva = get32( image + pos_rva );
        block_size = get32( image + pos_rva + 4 );
        if (block_size < 8 || pos_rva + block_size > end_rva)
            return fail_line( "invalid reloc block size 0x%x", block_size );

        entries = (block_size - 8) / 2;
        for (i = 0; i < entries; i++)
        {
            uint16_t entry = get16( image + pos_rva + 8 + i * 2 );
            uint16_t type = entry >> 12;
            uint16_t offset = entry & 0x0fff;
            uint32_t target_rva = page_rva + offset;

            if (type == IMAGE_REL_BASED_ABSOLUTE) continue;
            if (type != IMAGE_REL_BASED_DIR64 || !rva_in_image( pe, target_rva, 8 ))
            {
                report->unsupported_reloc_count++;
                continue;
            }
            put64( image + target_rva, get64( image + target_rva ) + delta );
            report->reloc_count++;
        }
        pos_rva += block_size;
    }

    log_line( "[RELOC] applied=%u unsupported=%u delta=0x%llx",
              report->reloc_count, report->unsupported_reloc_count, (unsigned long long)delta );
    if (report->unsupported_reloc_count)
    {
        report->run_blockers++;
        log_line( "[BLOCK] unsupported relocation entries need loader support" );
    }
    return 1;
}

#if PE_REAL_EXECUTE
static int dll_is_crt_apiset( const char *dll )
{
    return dll && !strncasecmp( dll, "api-ms-win-crt-", 15 );
}

static void *resolve_real_import( const char *dll, const char *name )
{
    if (!name) return NULL;

    if (!dll || dll_is_crt_apiset( dll ))
    {
        if (!strcmp( name, "__C_specific_handler" )) return shim___C_specific_handler;
        if (!strcmp( name, "memchr" )) return memchr;
        if (!strcmp( name, "memcmp" )) return memcmp;
        if (!strcmp( name, "memcpy" )) return memcpy;
        if (!strcmp( name, "memmove" )) return memmove;
        if (!strcmp( name, "strchr" )) return strchr;
        if (!strcmp( name, "strrchr" )) return strrchr;
        if (!strcmp( name, "strstr" )) return strstr;
        if (!strcmp( name, "__acrt_iob_func" )) return shim___acrt_iob_func;
        if (!strcmp( name, "__p__commode" )) return shim___p__commode;
        if (!strcmp( name, "__p__fmode" )) return shim___p__fmode;
        if (!strcmp( name, "__stdio_common_vfprintf" )) return shim___stdio_common_vfprintf;
        if (!strcmp( name, "__stdio_common_vsprintf" )) return shim___stdio_common_vsprintf;
        if (!strcmp( name, "_chsize_s" )) return shim__chsize_s;
        if (!strcmp( name, "_close" )) return shim__close;
        if (!strcmp( name, "_fileno" )) return shim__fileno;
        if (!strcmp( name, "_fseeki64" )) return shim__fseeki64;
        if (!strcmp( name, "_fsopen" )) return shim__fsopen;
        if (!strcmp( name, "_get_osfhandle" )) return shim__get_osfhandle;
        if (!strcmp( name, "_isatty" )) return shim__isatty;
        if (!strcmp( name, "_lseeki64" )) return shim__lseeki64;
        if (!strcmp( name, "_read" )) return shim__read;
        if (!strcmp( name, "_setmode" )) return shim__setmode;
        if (!strcmp( name, "_sopen_s" )) return shim__sopen_s;
        if (!strcmp( name, "_write" )) return shim__write;
        if (!strcmp( name, "fclose" )) return fclose;
        if (!strcmp( name, "feof" )) return feof;
        if (!strcmp( name, "ferror" )) return ferror;
        if (!strcmp( name, "fflush" )) return fflush;
        if (!strcmp( name, "fgets" )) return fgets;
        if (!strcmp( name, "fopen" )) return fopen;
        if (!strcmp( name, "fputc" )) return shim_fputc;
        if (!strcmp( name, "fputs" )) return shim_fputs;
        if (!strcmp( name, "fread" )) return fread;
        if (!strcmp( name, "freopen_s" )) return shim_freopen_s;
        if (!strcmp( name, "fseek" )) return fseek;
        if (!strcmp( name, "ftell" )) return ftell;
        if (!strcmp( name, "fwrite" )) return shim_fwrite;
        if (!strcmp( name, "getc" )) return getc;
        if (!strcmp( name, "putchar" )) return shim_putchar;
        if (!strcmp( name, "puts" )) return shim_puts;
        if (!strcmp( name, "rewind" )) return rewind;
        if (!strcmp( name, "setvbuf" )) return setvbuf;
        if (!strcmp( name, "ungetc" )) return ungetc;
        if (!strcmp( name, "__p___argc" )) return shim___p___argc;
        if (!strcmp( name, "__p___argv" )) return shim___p___argv;
        if (!strcmp( name, "_cexit" )) return shim__cexit;
        if (!strcmp( name, "_configure_narrow_argv" )) return shim__configure_narrow_argv;
        if (!strcmp( name, "_crt_atexit" )) return shim__crt_atexit;
        if (!strcmp( name, "_errno" )) return shim__errno;
        if (!strcmp( name, "_exit" )) return shim__exit;
        if (!strcmp( name, "_fpreset" )) return shim__fpreset;
        if (!strcmp( name, "_initialize_narrow_environment" )) return shim__initialize_narrow_environment;
        if (!strcmp( name, "_initterm" )) return shim__initterm;
        if (!strcmp( name, "_initterm_e" )) return shim__initterm_e;
        if (!strcmp( name, "_set_app_type" )) return shim__set_app_type;
        if (!strcmp( name, "_set_invalid_parameter_handler" )) return shim__set_invalid_parameter_handler;
        if (!strcmp( name, "abort" )) return shim_abort;
        if (!strcmp( name, "exit" )) return shim_exit;
        if (!strcmp( name, "signal" )) return shim_signal;
        if (!strcmp( name, "strerror" )) return strerror;
        if (!strcmp( name, "strerror_s" )) return shim_strerror_s;
        if (!strcmp( name, "_configthreadlocale" )) return shim__configthreadlocale;
        if (!strcmp( name, "localeconv" )) return localeconv;
        if (!strcmp( name, "setlocale" )) return shim_setlocale;
        if (!strcmp( name, "_set_new_mode" )) return shim__set_new_mode;
        if (!strcmp( name, "calloc" )) return calloc;
        if (!strcmp( name, "free" )) return free;
        if (!strcmp( name, "malloc" )) return malloc;
        if (!strcmp( name, "realloc" )) return realloc;
        if (!strcmp( name, "_strdup" )) return shim__strdup;
        if (!strcmp( name, "_stricmp" )) return shim__stricmp;
        if (!strcmp( name, "_strnicmp" )) return shim__strnicmp;
        if (!strcmp( name, "isalnum" )) return shim_isalnum;
        if (!strcmp( name, "isdigit" )) return shim_isdigit;
        if (!strcmp( name, "isspace" )) return shim_isspace;
        if (!strcmp( name, "isxdigit" )) return shim_isxdigit;
        if (!strcmp( name, "mbrlen" )) return shim_mbrlen;
        if (!strcmp( name, "memset" )) return memset;
        if (!strcmp( name, "strcat" )) return strcat;
        if (!strcmp( name, "strcmp" )) return strcmp;
        if (!strcmp( name, "strcpy" )) return strcpy;
        if (!strcmp( name, "strcspn" )) return strcspn;
        if (!strcmp( name, "strlen" )) return strlen;
        if (!strcmp( name, "strncmp" )) return strncmp;
        if (!strcmp( name, "strncpy_s" )) return shim_strncpy_s;
        if (!strcmp( name, "strnlen" )) return shim_strnlen;
        if (!strcmp( name, "strpbrk" )) return strpbrk;
        if (!strcmp( name, "strspn" )) return strspn;
        if (!strcmp( name, "tolower" )) return shim_tolower;
        if (!strcmp( name, "wcscpy_s" )) return shim_wcscpy_s;
        if (!strcmp( name, "wcslen" )) return shim_wcslen;
        if (!strcmp( name, "wcsncmp" )) return shim_wcsncmp;
        if (!strcmp( name, "wcsncpy_s" )) return shim_wcsncpy_s;
        if (!strcmp( name, "wcsnlen" )) return shim_wcsnlen;
        if (!strcmp( name, "_difftime64" )) return shim__difftime64;
        if (!strcmp( name, "_gmtime64_s" )) return shim__gmtime64_s;
        if (!strcmp( name, "_localtime64_s" )) return shim__localtime64_s;
        if (!strcmp( name, "_time64" )) return shim__time64;
        if (!strcmp( name, "strftime" )) return strftime;
        if (!strcmp( name, "_findclose" )) return shim__findclose;
        if (!strcmp( name, "_findfirst64i32" )) return shim__findfirst64i32;
        if (!strcmp( name, "_findnext64i32" )) return shim__findnext64i32;
        if (!strcmp( name, "_fstat64" )) return shim__fstat64;
        if (!strcmp( name, "_fullpath" )) return shim__fullpath;
        if (!strcmp( name, "_lock_file" )) return shim__lock_file;
        if (!strcmp( name, "_mkdir" )) return shim__mkdir;
        if (!strcmp( name, "_stat64" )) return shim__stat64;
        if (!strcmp( name, "_unlink" )) return shim__unlink;
        if (!strcmp( name, "_unlock_file" )) return shim__unlock_file;
        if (!strcmp( name, "_fdopen" )) return shim__fdopen;
        if (!strcmp( name, "_byteswap_uint64" )) return shim__byteswap_uint64;
        if (!strcmp( name, "bsearch" )) return bsearch;
        if (!strcmp( name, "qsort" )) return qsort;
        if (!strcmp( name, "_getch" )) return shim__getch;
        if (!strcmp( name, "__p__environ" )) return shim___p__environ;
        if (!strcmp( name, "getenv" )) return getenv;
        if (!strcmp( name, "__setusermatherr" )) return shim___setusermatherr;
        if (!strcmp( name, "atoi" )) return atoi;
        if (!strcmp( name, "mbrtowc" )) return shim_mbrtowc;
        if (!strcmp( name, "mbstowcs_s" )) return shim_mbstowcs_s;
        if (!strcmp( name, "strtol" )) return strtol;
        if (!strcmp( name, "strtoll" )) return strtoll;
        if (!strcmp( name, "strtoul" )) return strtoul;
        if (!strcmp( name, "strtoull" )) return strtoull;
        if (!strcmp( name, "wcrtomb" )) return shim_wcrtomb;
        if (!strcmp( name, "wcstombs_s" )) return shim_wcstombs_s;
    }

    if (!dll || !strcasecmp( dll, "KERNEL32.dll" ))
    {
        if (!strcmp( name, "AcquireSRWLockExclusive" )) return shim_AcquireSRWLockExclusive;
        if (!strcmp( name, "CancelIo" )) return shim_return_1;
        if (!strcmp( name, "CloseHandle" )) return shim_CloseHandle;
        if (!strcmp( name, "CompareFileTime" )) return shim_CompareFileTime;
        if (!strcmp( name, "CreateEventA" )) return shim_CreateEventA;
        if (!strcmp( name, "CreateFileA" )) return shim_CreateFileA;
        if (!strcmp( name, "CreateFileMappingA" )) return shim_CreateFileMappingA;
        if (!strcmp( name, "CreateMutexA" )) return shim_CreateMutexA;
        if (!strcmp( name, "CreateThread" )) return shim_CreateThread;
        if (!strcmp( name, "CreateToolhelp32Snapshot" )) return shim_return_minus1;
        if (!strcmp( name, "DeleteCriticalSection" )) return shim_DeleteCriticalSection;
        if (!strcmp( name, "DuplicateHandle" )) return shim_DuplicateHandle;
        if (!strcmp( name, "EnterCriticalSection" )) return shim_EnterCriticalSection;
        if (!strcmp( name, "FormatMessageA" )) return shim_FormatMessageA;
        if (!strcmp( name, "GetConsoleMode" )) return shim_GetConsoleMode;
        if (!strcmp( name, "GetConsoleScreenBufferInfo" )) return shim_GetConsoleScreenBufferInfo;
        if (!strcmp( name, "GetCurrentProcess" )) return shim_GetCurrentProcess;
        if (!strcmp( name, "GetCurrentThreadId" )) return shim_GetCurrentThreadId;
        if (!strcmp( name, "GetEnvironmentVariableA" )) return shim_GetEnvironmentVariableA;
        if (!strcmp( name, "GetFileAttributesA" )) return shim_GetFileAttributesA;
        if (!strcmp( name, "GetFileTime" )) return shim_GetFileTime;
        if (!strcmp( name, "GetFileType" )) return shim_GetFileType;
        if (!strcmp( name, "GetFullPathNameW" )) return shim_GetFullPathNameW;
        if (!strcmp( name, "GetLastError" )) return shim_GetLastError;
        if (!strcmp( name, "GetModuleFileNameA" )) return shim_GetModuleFileNameA;
        if (!strcmp( name, "GetModuleHandleA" )) return shim_GetModuleHandleA;
        if (!strcmp( name, "GetOverlappedResult" )) return shim_GetOverlappedResult;
        if (!strcmp( name, "GetProcAddress" )) return shim_GetProcAddress;
        if (!strcmp( name, "GetStdHandle" )) return shim_GetStdHandle;
        if (!strcmp( name, "GetSystemInfo" )) return shim_GetSystemInfo;
        if (!strcmp( name, "GetSystemTime" )) return shim_GetSystemTime;
        if (!strcmp( name, "GetSystemTimeAsFileTime" )) return shim_GetSystemTimeAsFileTime;
        if (!strcmp( name, "GetTickCount64" )) return shim_GetTickCount64;
        if (!strcmp( name, "GetTimeZoneInformation" )) return shim_GetTimeZoneInformation;
        if (!strcmp( name, "InitOnceExecuteOnce" )) return shim_InitOnceExecuteOnce;
        if (!strcmp( name, "InitializeConditionVariable" )) return shim_InitializeConditionVariable;
        if (!strcmp( name, "InitializeCriticalSection" )) return shim_InitializeCriticalSection;
        if (!strcmp( name, "InitializeCriticalSectionEx" )) return shim_InitializeCriticalSectionEx;
        if (!strcmp( name, "IsProcessorFeaturePresent" )) return shim_IsProcessorFeaturePresent;
        if (!strcmp( name, "LeaveCriticalSection" )) return shim_LeaveCriticalSection;
        if (!strcmp( name, "MapViewOfFile" )) return shim_MapViewOfFile;
        if (!strcmp( name, "Module32First" )) return shim_return_0;
        if (!strcmp( name, "Module32Next" )) return shim_return_0;
        if (!strcmp( name, "MoveFileExA" )) return shim_return_0;
        if (!strcmp( name, "MultiByteToWideChar" )) return shim_MultiByteToWideChar;
        if (!strcmp( name, "PeekNamedPipe" )) return shim_PeekNamedPipe;
        if (!strcmp( name, "QueryPerformanceCounter" )) return shim_QueryPerformanceCounter;
        if (!strcmp( name, "QueryPerformanceFrequency" )) return shim_QueryPerformanceFrequency;
        if (!strcmp( name, "ReadFile" )) return shim_ReadFile;
        if (!strcmp( name, "ReleaseMutex" )) return shim_ReleaseMutex;
        if (!strcmp( name, "ReleaseSRWLockExclusive" )) return shim_ReleaseSRWLockExclusive;
        if (!strcmp( name, "ResetEvent" )) return shim_ResetEvent;
        if (!strcmp( name, "SetConsoleCtrlHandler" )) return shim_SetConsoleCtrlHandler;
        if (!strcmp( name, "SetConsoleMode" )) return shim_SetConsoleMode;
        if (!strcmp( name, "SetEvent" )) return shim_SetEvent;
        if (!strcmp( name, "SetFileTime" )) return shim_SetFileTime;
        if (!strcmp( name, "SetHandleInformation" )) return shim_SetHandleInformation;
        if (!strcmp( name, "SetLastError" )) return shim_SetLastError;
        if (!strcmp( name, "SetStdHandle" )) return shim_SetStdHandle;
        if (!strcmp( name, "SetUnhandledExceptionFilter" )) return shim__set_invalid_parameter_handler;
        if (!strcmp( name, "Sleep" )) return shim_Sleep;
        if (!strcmp( name, "SleepConditionVariableCS" )) return shim_SleepConditionVariableCS;
        if (!strcmp( name, "SleepEx" )) return shim_SleepEx;
        if (!strcmp( name, "SystemTimeToFileTime" )) return shim_SystemTimeToFileTime;
        if (!strcmp( name, "TerminateProcess" )) return shim_TerminateProcess;
        if (!strcmp( name, "TerminateThread" )) return shim_TerminateThread;
        if (!strcmp( name, "TlsGetValue" )) return shim_TlsGetValue;
        if (!strcmp( name, "UnmapViewOfFile" )) return shim_UnmapViewOfFile;
        if (!strcmp( name, "VerSetConditionMask" )) return shim_VerSetConditionMask;
        if (!strcmp( name, "VerifyVersionInfoW" )) return shim_VerifyVersionInfoW;
        if (!strcmp( name, "VirtualAlloc" )) return shim_VirtualAlloc;
        if (!strcmp( name, "VirtualFree" )) return shim_VirtualFree;
        if (!strcmp( name, "VirtualProtect" )) return shim_VirtualProtect;
        if (!strcmp( name, "VirtualQuery" )) return shim_VirtualQuery;
        if (!strcmp( name, "WaitForMultipleObjects" )) return shim_WaitForMultipleObjects;
        if (!strcmp( name, "WaitForSingleObject" )) return shim_WaitForSingleObject;
        if (!strcmp( name, "WaitForSingleObjectEx" )) return shim_WaitForSingleObjectEx;
        if (!strcmp( name, "WaitNamedPipeA" )) return shim_return_0;
        if (!strcmp( name, "WakeConditionVariable" )) return shim_WakeConditionVariable;
        if (!strcmp( name, "WideCharToMultiByte" )) return shim_WideCharToMultiByte;
        if (!strcmp( name, "WriteConsoleW" )) return shim_WriteConsoleW;
        if (!strcmp( name, "WriteFile" )) return shim_WriteFile;
    }

    if (!dll || !strcasecmp( dll, "WS2_32.dll" ))
    {
        if (!strcmp( name, "WSAStartup" )) return shim_WSAStartup;
        if (!strcmp( name, "WSACleanup" )) return shim_WSACleanup;
        if (!strcmp( name, "WSACloseEvent" )) return shim_WSACloseEvent;
        if (!strcmp( name, "WSACreateEvent" )) return shim_WSACreateEvent;
        if (!strcmp( name, "WSAEnumNetworkEvents" )) return shim_WSAEnumNetworkEvents;
        if (!strcmp( name, "WSAEventSelect" )) return shim_WSAEventSelect;
        if (!strcmp( name, "WSAGetLastError" )) return shim_WSAGetLastError;
        if (!strcmp( name, "WSAIoctl" )) return shim_WSAIoctl;
        if (!strcmp( name, "WSAResetEvent" )) return shim_WSAResetEvent;
        if (!strcmp( name, "WSASetEvent" )) return shim_WSASetEvent;
        if (!strcmp( name, "WSASetLastError" )) return shim_WSASetLastError;
        if (!strcmp( name, "WSAStringToAddressW" )) return shim_WSAStringToAddressW;
        if (!strcmp( name, "WSAWaitForMultipleEvents" )) return shim_WSAWaitForMultipleEvents;
        if (!strcmp( name, "__WSAFDIsSet" )) return shim___WSAFDIsSet;
        if (!strcmp( name, "accept" )) return shim_accept;
        if (!strcmp( name, "bind" )) return shim_bind;
        if (!strcmp( name, "closesocket" )) return shim_closesocket;
        if (!strcmp( name, "connect" )) return shim_connect;
        if (!strcmp( name, "freeaddrinfo" )) return shim_freeaddrinfo;
        if (!strcmp( name, "getaddrinfo" )) return shim_getaddrinfo;
        if (!strcmp( name, "gethostname" )) return shim_gethostname;
        if (!strcmp( name, "getpeername" )) return shim_getpeername;
        if (!strcmp( name, "getsockname" )) return shim_getsockname;
        if (!strcmp( name, "getsockopt" )) return shim_getsockopt;
        if (!strcmp( name, "htonl" )) return shim_htonl;
        if (!strcmp( name, "htons" )) return shim_htons;
        if (!strcmp( name, "inet_pton" )) return shim_inet_pton;
        if (!strcmp( name, "ioctlsocket" )) return shim_ioctlsocket;
        if (!strcmp( name, "listen" )) return shim_listen;
        if (!strcmp( name, "ntohl" )) return shim_ntohl;
        if (!strcmp( name, "ntohs" )) return shim_ntohs;
        if (!strcmp( name, "recv" )) return shim_recv;
        if (!strcmp( name, "recvfrom" )) return shim_recvfrom;
        if (!strcmp( name, "select" )) return shim_select;
        if (!strcmp( name, "send" )) return shim_send;
        if (!strcmp( name, "sendto" )) return shim_sendto;
        if (!strcmp( name, "setsockopt" )) return shim_setsockopt;
        if (!strcmp( name, "shutdown" )) return shim_shutdown;
        if (!strcmp( name, "socket" )) return shim_socket;
    }

    if (!dll || !strcasecmp( dll, "Secur32.dll" ))
    {
        if (!strcmp( name, "InitSecurityInterfaceA" )) return shim_InitSecurityInterfaceA;
    }

    if (!dll || !strcasecmp( dll, "Normaliz.dll" ))
    {
        if (!strcmp( name, "IdnToAscii" )) return shim_return_0;
        if (!strcmp( name, "IdnToUnicode" )) return shim_return_0;
    }

    if (!dll || !strcasecmp( dll, "WLDAP32.dll" ))
    {
        if (!strcmp( name, "ber_free" )) return shim_noop;
        if (!strcmp( name, "ldap_bind_s" )) return shim_return_minus1;
        if (!strcmp( name, "ldap_err2string" )) return shim_return_0;
        if (!strcmp( name, "ldap_first_attribute" )) return shim_return_0;
        if (!strcmp( name, "ldap_first_entry" )) return shim_return_0;
        if (!strcmp( name, "ldap_get_dn" )) return shim_return_0;
        if (!strcmp( name, "ldap_get_values_len" )) return shim_return_0;
        if (!strcmp( name, "ldap_init" )) return shim_return_0;
        if (!strcmp( name, "ldap_memfree" )) return shim_noop;
        if (!strcmp( name, "ldap_msgfree" )) return shim_return_0;
        if (!strcmp( name, "ldap_next_attribute" )) return shim_return_0;
        if (!strcmp( name, "ldap_next_entry" )) return shim_return_0;
        if (!strcmp( name, "ldap_search_s" )) return shim_return_minus1;
        if (!strcmp( name, "ldap_set_option" )) return shim_return_0;
        if (!strcmp( name, "ldap_simple_bind_s" )) return shim_return_minus1;
        if (!strcmp( name, "ldap_sslinit" )) return shim_return_0;
        if (!strcmp( name, "ldap_unbind_s" )) return shim_return_0;
        if (!strcmp( name, "ldap_value_free_len" )) return shim_noop;
    }

    if (!dll || !strcasecmp( dll, "bcrypt.dll" ))
    {
        if (!strcmp( name, "BCryptGenRandom" )) return shim_BCryptGenRandom;
    }

    if (!dll || !strcasecmp( dll, "CRYPT32.dll" ))
    {
        if (!strcmp( name, "CertCloseStore" )) return shim_return_1;
        if (!strcmp( name, "CertEnumCertificatesInStore" )) return shim_return_0;
        if (!strcmp( name, "CertFreeCertificateContext" )) return shim_return_1;
        if (!strcmp( name, "CertGetEnhancedKeyUsage" )) return shim_return_0;
        if (!strcmp( name, "CertGetIntendedKeyUsage" )) return shim_return_0;
        if (!strcmp( name, "CertOpenSystemStoreA" )) return shim_return_0;
    }

    if (!dll || !strcasecmp( dll, "IPHLPAPI.DLL" ))
    {
        if (!strcmp( name, "if_nametoindex" )) return shim_if_nametoindex;
    }

    if (!dll || !strcasecmp( dll, "USER32.dll" ))
    {
        if (!strcmp( name, "FindWindowA" )) return shim_return_0;
        if (!strcmp( name, "SendMessageA" )) return shim_return_0;
    }

    return NULL;
}
#endif

static int current_runtime_resolves( const char *dll, const char *name, void **out_func )
{
#if PE_REAL_EXECUTE
    void *func = resolve_real_import( dll, name );
    if (out_func) *out_func = func;
    return func != NULL;
#else
    (void)out_func;
    if (strcasecmp( dll, "ntdll.dll" )) return 0;
    if (!strcmp( name, "NtCreateEvent" )) return 1;
    if (!strcmp( name, "NtSetEvent" )) return 1;
    if (!strcmp( name, "NtQueryEvent" )) return 1;
    return 0;
#endif
}

static int scan_imports( const struct pe_image *pe, const unsigned char *image, struct run_report *report )
{
    uint32_t desc_rva = pe->dir_rva[DIR_IMPORT];
    uint32_t end_rva = desc_rva + pe->dir_size[DIR_IMPORT];
    unsigned int desc_count = 0;

    if (!pe->dir_rva[DIR_IMPORT] || !pe->dir_size[DIR_IMPORT])
    {
        log_line( "[IMP] no import directory" );
        return 1;
    }
    if (end_rva < desc_rva || !rva_in_image( pe, desc_rva, pe->dir_size[DIR_IMPORT] ))
        return fail_line( "import directory outside image" );

    while (desc_rva + 20 <= end_rva && desc_count++ < 256)
    {
        uint32_t ilt_rva;
        uint32_t name_rva;
        uint32_t iat_rva;
        const char *dll_name;
        unsigned int index;

        ilt_rva = get32( image + desc_rva );
        name_rva = get32( image + desc_rva + 12 );
        iat_rva = get32( image + desc_rva + 16 );
        if (!ilt_rva && !name_rva && !iat_rva) break;
        if (!ilt_rva) ilt_rva = iat_rva;

        dll_name = read_cstring( image, pe->image_size, name_rva );
        if (!dll_name) return fail_line( "invalid import DLL name rva=0x%x", name_rva );

        report->dll_count++;
        log_line( "[IMP] DLL %s", dll_name );

        for (index = 0; index < 4096; index++)
        {
            uint64_t thunk;
            const char *func_name;
            uint32_t import_name_rva;
            int resolved;
            void *func = NULL;

            if (!rva_in_image( pe, ilt_rva + index * 8, 8 ) ||
                !rva_in_image( pe, iat_rva + index * 8, 8 ))
                return fail_line( "import thunk outside image for %s", dll_name );

            thunk = get64( image + ilt_rva + index * 8 );
            if (!thunk) break;

            report->import_count++;
            if (thunk & 0x8000000000000000ULL)
            {
                report->ordinal_import_count++;
                report->unsupported_import_count++;
                log_line( "[MISS] %s ordinal import 0x%llx", dll_name, (unsigned long long)thunk );
                continue;
            }

            import_name_rva = (uint32_t)thunk;
            if (!rva_in_image( pe, import_name_rva, 2 ))
                return fail_line( "import name outside image rva=0x%x", import_name_rva );
            func_name = read_cstring( image, pe->image_size, import_name_rva + 2 );
            if (!func_name) return fail_line( "unterminated import name rva=0x%x", import_name_rva );

            resolved = current_runtime_resolves( dll_name, func_name, &func );
            if (resolved)
            {
#if PE_REAL_EXECUTE
                put64( (unsigned char *)image + iat_rva + index * 8, (uint64_t)(uintptr_t)func );
#endif
                log_line( "[OKIMP] %s!%s", dll_name, func_name );
            }
            else
            {
                report->unsupported_import_count++;
                log_line( "[MISS] %s!%s", dll_name, func_name );
            }
        }

        desc_rva += 20;
    }

    if (desc_count >= 256) return fail_line( "too many import descriptors" );
    if (report->unsupported_import_count)
    {
        report->run_blockers++;
        log_line( "[BLOCK] unresolved imports: %u of %u", report->unsupported_import_count,
                  report->import_count );
    }
    else log_line( "[IMP] all imports currently resolvable" );
    return 1;
}

static int scan_tls( const struct pe_image *pe, const unsigned char *image, struct run_report *report )
{
    uint32_t tls_rva = pe->dir_rva[DIR_TLS];
    uint64_t actual_base = (uint64_t)(uintptr_t)image;
    uint64_t callbacks_va;
    uint32_t callbacks_rva;
    unsigned int i;

    if (!tls_rva || !pe->dir_size[DIR_TLS]) return 1;
    if (!rva_in_image( pe, tls_rva, 0x28 )) return fail_line( "TLS directory outside image" );

    report->has_tls = 1;
    callbacks_va = get64( image + tls_rva + 24 );
    log_line( "[TLS] raw=0x%llx..0x%llx index=0x%llx callbacks=0x%llx",
              (unsigned long long)get64( image + tls_rva ),
              (unsigned long long)get64( image + tls_rva + 8 ),
              (unsigned long long)get64( image + tls_rva + 16 ),
              (unsigned long long)callbacks_va );

    if (callbacks_va && va_to_rva( actual_base, pe->image_size, callbacks_va, &callbacks_rva ))
    {
        for (i = 0; i < 256; i++)
        {
            uint64_t callback;

            if (!rva_in_image( pe, callbacks_rva + i * 8, 8 ))
                return fail_line( "TLS callback table outside image" );
            callback = get64( image + callbacks_rva + i * 8 );
            if (!callback) break;
            report->tls_callback_count++;
            log_line( "[TLS] callback[%u]=0x%llx", i, (unsigned long long)callback );
        }
    }

#if PE_REAL_EXECUTE
    log_line( "[TLS] runtime setup enabled before entrypoint" );
#else
    report->run_blockers++;
    log_line( "[BLOCK] TLS directory needs runtime setup before entrypoint" );
#endif
    return 1;
}

#if PE_REAL_EXECUTE
static int setup_tls_and_call_callbacks( const struct pe_image *pe, unsigned char *image )
{
    uint32_t tls_rva = pe->dir_rva[DIR_TLS];
    uint64_t actual_base = (uint64_t)(uintptr_t)image;
    uint64_t start_va;
    uint64_t end_va;
    uint64_t index_va;
    uint64_t callbacks_va;
    uint32_t start_rva;
    uint32_t end_rva;
    uint32_t index_rva;
    uint32_t callbacks_rva;
    unsigned int i;

    if (!tls_rva || !pe->dir_size[DIR_TLS]) return 1;
    if (!rva_in_image( pe, tls_rva, 0x28 )) return fail_line( "TLS directory outside image" );

    start_va = get64( image + tls_rva );
    end_va = get64( image + tls_rva + 8 );
    index_va = get64( image + tls_rva + 16 );
    callbacks_va = get64( image + tls_rva + 24 );

    if (!va_to_rva( actual_base, pe->image_size, start_va, &start_rva ) ||
        !va_to_rva( actual_base, pe->image_size, end_va, &end_rva ) ||
        !va_to_rva( actual_base, pe->image_size, index_va, &index_rva ))
        return fail_line( "TLS VA outside image" );

    if (!rva_in_image( pe, index_rva, 4 )) return fail_line( "TLS index outside image" );
    put32( image + index_rva, 0 );
    pe_tls_slots[0] = image + start_rva;
    log_line( "[TLSRUN] slot0=%p bytes=0x%x", pe_tls_slots[0], end_rva - start_rva );

    if (!callbacks_va) return 1;
    if (!va_to_rva( actual_base, pe->image_size, callbacks_va, &callbacks_rva ))
        return fail_line( "TLS callback VA outside image" );

    for (i = 0; i < 256; i++)
    {
        uint64_t callback_va;

        if (!rva_in_image( pe, callbacks_rva + i * 8, 8 ))
            return fail_line( "TLS callback table outside image" );
        callback_va = get64( image + callbacks_rva + i * 8 );
        if (!callback_va) break;

        log_line( "[TLSRUN] callback[%u]=0x%llx", i, (unsigned long long)callback_va );
        call_pe_function3( (void *)(uintptr_t)callback_va, (uintptr_t)image, DLL_PROCESS_ATTACH, 0 );
    }

    return 1;
}
#endif

static void scan_optional_features( const struct pe_image *pe, struct run_report *report )
{
    if (pe->dir_rva[DIR_EXCEPTION] && pe->dir_size[DIR_EXCEPTION])
        log_line( "[FEAT] exception/unwind directory=0x%x/0x%x",
                  pe->dir_rva[DIR_EXCEPTION], pe->dir_size[DIR_EXCEPTION] );

    if (pe->dir_rva[DIR_LOAD_CONFIG] && pe->dir_size[DIR_LOAD_CONFIG])
    {
        report->has_load_config = 1;
        log_line( "[FEAT] load config directory=0x%x/0x%x",
                  pe->dir_rva[DIR_LOAD_CONFIG], pe->dir_size[DIR_LOAD_CONFIG] );
    }

    if (pe->dir_rva[DIR_DELAY_IMPORT] && pe->dir_size[DIR_DELAY_IMPORT])
    {
        report->has_delay_imports = 1;
        report->run_blockers++;
        log_line( "[BLOCK] delay imports need loader support" );
    }

    if (pe->dir_rva[DIR_IAT] && pe->dir_size[DIR_IAT])
        log_line( "[FEAT] IAT directory=0x%x/0x%x", pe->dir_rva[DIR_IAT], pe->dir_size[DIR_IAT] );
}

static int protect_sections( const struct pe_image *pe, unsigned char *image )
{
    unsigned int i;

    for (i = 0; i < pe->section_count; i++)
    {
        const unsigned char *hdr = pe->file + pe->section_offset + i * 40;
        char name[9];
        uint32_t virtual_size = get32( hdr + 8 );
        uint32_t virtual_address = get32( hdr + 12 );
        uint32_t raw_size = get32( hdr + 16 );
        uint32_t characteristics = get32( hdr + 36 );
        uint32_t span = virtual_size > raw_size ? virtual_size : raw_size;
        uint32_t size = align_up_u32( span, PAGE_SIZE );
        int prot = PROT_READ;

        if (!size) continue;
        if (characteristics & 0x20000000) prot |= PROT_EXEC;
        if ((characteristics & 0x80000000) && !(prot & PROT_EXEC)) prot |= PROT_WRITE;

        memcpy( name, hdr, 8 );
        name[8] = 0;

        if (horizon_mprotect( image + virtual_address, size, prot ) == -1)
            return fail_line( "mprotect %.8s errno=%d", name, errno );
        log_line( "[PROT] %.8s rva=0x%x size=0x%x prot=0x%x", name, virtual_address, size, prot );
    }
    return 1;
}

#if PE_REAL_EXECUTE
static int run_pe_entry( const struct pe_image *pe, unsigned char *image )
{
    int (*entry)(void);
    int result;

    loaded_image = image;
    loaded_image_size = pe->image_size;
    pe_exit_code = 0;
    init_fake_teb();

    if (setjmp( pe_exit_jmp ))
    {
        pe_exit_active = 0;
        log_line( "[RUN] intercepted process exit code=%d", pe_exit_code );
        return 1;
    }

    pe_exit_active = 1;
    if (!setup_tls_and_call_callbacks( pe, image ))
    {
        pe_exit_active = 0;
        return 0;
    }

    entry = (int (*)(void))(void *)(image + pe->entry_rva);
    log_line( "[RUN] entering real exe entrypoint %p argc=%d", entry, pe_argc );
    result = (int)call_pe_function3( entry, 0, 0, 0 );
    pe_exit_active = 0;
    log_line( "[RUN] entrypoint returned %d / 0x%08x", result, (uint32_t)result );
    return 1;
}
#endif

static int analyze_real_pe( const unsigned char *file, size_t file_size )
{
    struct pe_image pe;
    struct run_report report;
    unsigned char *image;
    int ok = 0;

    memset( &report, 0, sizeof(report) );

    if (!parse_pe_headers( file, file_size, &pe )) return 0;

    image = map_pe_image( &pe );
    if (!image) return 0;
#if PE_REAL_EXECUTE
    loaded_image = image;
    loaded_image_size = pe.image_size;
#endif

    if (!apply_base_relocs( &pe, image, &report )) goto done;
    if (!scan_imports( &pe, image, &report )) goto done;
    if (!scan_tls( &pe, image, &report )) goto done;
    scan_optional_features( &pe, &report );
    if (!protect_sections( &pe, image )) goto done;

#if PE_REAL_EXECUTE
    if (report.run_blockers)
    {
        log_line( "[RUN] blocked before entrypoint, run_blockers=%u", report.run_blockers );
        goto summarize;
    }
    if (!run_pe_entry( &pe, image )) goto done;
#else
    log_line( "[ENTRY] not executing 0x%x yet", pe.entry_rva );
#endif

#if PE_REAL_EXECUTE
summarize:
#endif
    log_line( "[SUMMARY] dlls=%u imports=%u unresolved=%u ordinal=%u relocs=%u tls_callbacks=%u",
              report.dll_count, report.import_count, report.unsupported_import_count,
              report.ordinal_import_count, report.reloc_count, report.tls_callback_count );
    log_line( "[SUMMARY] loader_failures=%d run_blockers=%u real_exe_ready=%s",
              loader_failures, report.run_blockers,
              (!loader_failures && !report.run_blockers) ? "YES" : "NO" );
    ok = 1;

done:
    horizon_munmap( image, pe.image_size );
    return ok;
}

static void park_forever(void)
{
    log_line( "[EXIT] parked after summary; close the application from HOME" );
    for (;;) svcSleepThread( 1000000000LL );
}

int main(int argc, char **argv)
{
    unsigned char *file = NULL;
    size_t file_size = 0;
    const char *path = NULL;
    int ok;

    (void)argc;
    (void)argv;

    consoleInit( NULL );
#if PE_REAL_EXECUTE
    pe_main_thread = threadGetSelf();
    pe_sync_table_init();
#endif
    mkdir( "sdmc:/switch", 0777 );
    mkdir( "sdmc:/switch/wine", 0777 );
    mkdir( "sdmc:/switch/wine/drive_c", 0777 );
    mkdir( "sdmc:/switch/wine/drive_c/curl", 0777 );
    log_file = fopen(
#if PE_REAL_EXECUTE
        "sdmc:/switch/wine/pe-real-run.log",
#else
        "sdmc:/switch/wine/pe-real-report.log",
#endif
        "w" );

#if PE_REAL_EXECUTE
    log_line( "wine-nx-pe-real-run: real ARM64 Windows PE execution attempt" );
    pe_ensure_socket();
    load_pe_args();
#else
    log_line( "wine-nx-pe-real-report: real ARM64 Windows PE file loader/report" );
#endif
    if (!load_default_sample( &file, &file_size, &path ))
    {
        fail_line( "could not open curl.exe/trurl.exe under sdmc:/switch/wine/drive_c/curl" );
        log_line( "[SUMMARY] loader_failures=%d run_blockers=1 real_exe_ready=NO", loader_failures );
        park_forever();
        return 1;
    }

    log_line( "[FILE] loaded %s size=%zu", path, file_size );
    ok = analyze_real_pe( file, file_size );
    free( file );

    if (!ok)
        log_line( "[SUMMARY] loader_failures=%d run_blockers=unknown real_exe_ready=NO", loader_failures );

    park_forever();
    return loader_failures ? 1 : 0;
}
