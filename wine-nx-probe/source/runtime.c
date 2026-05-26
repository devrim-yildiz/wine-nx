#include <switch.h>

#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "windef.h"
#include "winbase.h"
#include "winnt.h"
#include "winternl.h"
#include "wine/server.h"
#include "unix_private.h"
#include "horizon_private.h"

u32 __nx_applet_type = AppletType_Application;
size_t __nx_heap_size = 256 * 1024 * 1024;
unsigned char __attribute__((aligned(16))) __nx_exception_stack[0x10000];
uint64_t __nx_exception_stack_size = sizeof(__nx_exception_stack);
/* Run __libnx_exception_handler even when hbloader/Atmosphere has attached
 * as the debugger (which is always the case for NRO launches). Without this,
 * libnx's exception.s short-circuits to abort before calling our handler. */
u32 __nx_exception_ignoredebug = 1;

#define WINE_ROOT "sdmc:/switch/wine"
#define WINE_DRIVE_C WINE_ROOT "/drive_c"
#define WINE_SYSTEM_DIR WINE_DRIVE_C "/windows/system32"
#define RUNTIME_DIR WINE_ROOT
#define DEFAULT_TARGET WINE_DRIVE_C "/curl/curl.exe"
#define WINE_NX_RUNTIME_BUILD "nx-batched-present-1"
#define MAX_RUNTIME_MODULES 64
#define MAX_IMPORT_DEPTH 16

extern void wine_nx_runtime_platform_init(void);
extern void wine_nx_runtime_environment_init(void);
extern NTSTATUS wine_nx_loader_bootstrap( const UNICODE_STRING *main_nt_name );
extern NTSTATUS wine_nx_loader_fixup_main_imports(void);
extern NTSTATUS wine_nx_loader_attach_main(void);
extern const char *wine_nx_loader_last_import_dll(void);
extern NTSTATUS wine_nx_loader_last_import_status(void);
extern const char *wine_nx_loader_last_open_path(void);
extern NTSTATUS wine_nx_loader_last_open_status(void);
extern const char *wine_nx_loader_last_export_diag(void);

static FILE *log_file;

struct runtime_module
{
    char path[512];
    char dir[512];
    char name[128];
    void *base;
    SIZE_T size;
    IMAGE_NT_HEADERS64 *nt;
    int is_main;
    int resolving_imports;
    int imports_scanned;
};

struct import_stats
{
    unsigned int dlls;
    unsigned int loaded_dlls;
    unsigned int missing_dlls;
    unsigned int imports;
    unsigned int bound;
    unsigned int unresolved;
    unsigned int forwarded;
};

static struct runtime_module modules[MAX_RUNTIME_MODULES];
static unsigned int module_count;

static pthread_t log_main_thread;
static int log_main_thread_set;

/* The text console and the Wine framebuffer both own the default nwindow, so
 * once a GUI app brings up the display driver we hand the screen over to the
 * framebuffer and stop driving the console (logs still go to the file). */
static int wine_nx_console_active = 1;
static Framebuffer wine_nx_fb;
static int wine_nx_fb_ready;
static int wine_nx_touch_ready;
static pthread_mutex_t wine_nx_fb_mutex = PTHREAD_MUTEX_INITIALIZER;
static void *wine_nx_fb_pending_bits;
static int wine_nx_fb_pending_stride;
static int wine_nx_fb_pending_dirty;
static int wine_nx_fb_lock_depth;

static void log_line( const char *fmt, ... )
{
    /* The software console aborts the process (framebufferBegin →
     * diagAbortWithResult) when driven from any thread but the one that
     * called consoleInit, including exception handlers running on Wine
     * secondary threads.  Off the main thread, log to the file only. */
    int on_main = wine_nx_console_active &&
                  (!log_main_thread_set || pthread_equal( pthread_self(), log_main_thread ));
    va_list args;

    if (on_main)
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
    if (on_main) consoleUpdate( NULL );
}

void wine_nx_runtime_trace( const char *msg )
{
    log_line( "%s", msg );
}

/***********************************************************************
 * Framebuffer platform hooks used by the win32u Switch display driver
 * (dlls/win32u/winnx_drv.c).  The driver renders into ordinary DIB memory;
 * these present the dirty pixels to the libnx framebuffer.
 */
#define WINE_NX_FB_W 1280
#define WINE_NX_FB_H 720

/* Take the screen from the text console and bring up a linear framebuffer. */
int wine_nx_fb_init(void)
{
    Result rc;
    if (wine_nx_fb_ready) return 0;
    log_line( "[NXFB] fb_init: taking screen from console" );
    if (wine_nx_console_active)
    {
        consoleExit( NULL );
        wine_nx_console_active = 0;
    }
    rc = framebufferCreate( &wine_nx_fb, nwindowGetDefault(),
                            WINE_NX_FB_W, WINE_NX_FB_H, PIXEL_FORMAT_RGBA_8888, 3 );
    if (R_FAILED( rc ))
    {
        log_line( "[NXFB] framebufferCreate FAILED rc=0x%x", rc );
        return -1;
    }
    framebufferMakeLinear( &wine_nx_fb );
    wine_nx_fb_ready = 1;
    log_line( "[NXFB] framebuffer ready %dx%d", WINE_NX_FB_W, WINE_NX_FB_H );
    return 0;
}

/* Acquire the back buffer for writing; returns linear RGBA8888 pixels. */
void *wine_nx_fb_lock( int *width, int *height, int *stride_px )
{
    u32 stride = 0;
    void *bits = NULL;

    if (!wine_nx_fb_ready && wine_nx_fb_init()) return NULL;
    pthread_mutex_lock( &wine_nx_fb_mutex );
    if (!wine_nx_fb_pending_bits)
    {
        wine_nx_fb_pending_bits = framebufferBegin( &wine_nx_fb, &stride );
        wine_nx_fb_pending_stride = (int)(stride / 4);
    }
    bits = wine_nx_fb_pending_bits;
    if (!bits)
    {
        pthread_mutex_unlock( &wine_nx_fb_mutex );
        return NULL;
    }
    wine_nx_fb_lock_depth++;
    if (width)     *width     = WINE_NX_FB_W;
    if (height)    *height    = WINE_NX_FB_H;
    if (stride_px) *stride_px = wine_nx_fb_pending_stride;
    return bits;
}

void wine_nx_fb_unlock(void)
{
    wine_nx_fb_pending_dirty = 1;
    if (wine_nx_fb_lock_depth > 0) wine_nx_fb_lock_depth--;
    pthread_mutex_unlock( &wine_nx_fb_mutex );
}

void wine_nx_fb_present(void)
{
    pthread_mutex_lock( &wine_nx_fb_mutex );
    if (wine_nx_fb_ready && wine_nx_fb_pending_bits && wine_nx_fb_pending_dirty && !wine_nx_fb_lock_depth)
    {
        framebufferEnd( &wine_nx_fb );
        wine_nx_fb_pending_bits = NULL;
        wine_nx_fb_pending_stride = 0;
        wine_nx_fb_pending_dirty = 0;
    }
    pthread_mutex_unlock( &wine_nx_fb_mutex );
}

/* Return the primary touchscreen contact in native 1280x720 display
 * coordinates.  Win32u turns it into the conventional mouse stream expected
 * by desktop applications; native WM_TOUCH can be layered on later. */
int wine_nx_touch_poll( int *x, int *y )
{
    HidTouchScreenState state = {0};

    if (!wine_nx_touch_ready)
    {
        hidInitializeTouchScreen();
        wine_nx_touch_ready = 1;
        log_line( "[NXINPUT] touchscreen ready" );
    }

    if (!hidGetTouchScreenStates( &state, 1 ) || state.count <= 0) return 0;
    if (x) *x = state.touches[0].x;
    if (y) *y = state.touches[0].y;
    return 1;
}

static int call_pe_entry_point( void *entry )
{
    extern void wine_nx_set_active_pe_teb( TEB *teb );
    uintptr_t ret;
    uintptr_t teb = (uintptr_t)NtCurrentTeb();

    wine_nx_set_active_pe_teb( (TEB *)teb );
    __asm__ volatile(
        "mov x16, %[entry]\n\t"
        "mov x17, %[teb]\n\t"
        "mov x20, x18\n\t"
        "mov x18, x17\n\t"
        "blr x16\n\t"
        "mov x18, x20\n\t"
        "mov %[ret], x0\n\t"
        : [ret] "=r"(ret)
        : [entry] "r"(entry), [teb] "r"(teb)
        : "x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7", "x8", "x9",
          "x10", "x11", "x12", "x13", "x14", "x15", "x16", "x17", "x20",
          "x30", "memory", "cc" );

    return (int)ret;
}

static void park_forever(void)
{
    log_line( "[EXIT] parked after runtime handoff; close from HOME" );
    for (;;) svcSleepThread( 1000000000LL );
}

static void trim_line( char *line )
{
    size_t len = strlen( line );

    while (len && (line[len - 1] == '\n' || line[len - 1] == '\r' ||
                   line[len - 1] == ' ' || line[len - 1] == '\t'))
        line[--len] = 0;
}

static int read_first_line( const char *path, char *line, size_t size )
{
    FILE *file = fopen( path, "r" );

    if (!file) return 0;
    if (!fgets( line, size, file ))
    {
        fclose( file );
        return 0;
    }
    fclose( file );
    trim_line( line );
    return line[0] != 0;
}

static int read_bool_file( const char *path )
{
    char line[32];

    if (!read_first_line( path, line, sizeof(line) )) return 0;
    return !strcmp( line, "1" ) || !strcasecmp( line, "true" ) ||
           !strcasecmp( line, "yes" ) || !strcasecmp( line, "run" );
}

static unsigned int close_handle_object( HANDLE handle )
{
    unsigned int status;

    SERVER_START_REQ( close_handle )
    {
        req->handle = wine_server_obj_handle( handle );
        status = wine_server_call( req );
    }
    SERVER_END_REQ;

    return status;
}

static unsigned int runtime_init_process_done(void)
{
    unsigned int status;

    SERVER_START_REQ( init_process_done )
    {
        req->teb = wine_server_client_ptr( NtCurrentTeb() );
        req->peb = wine_server_client_ptr( NtCurrentTeb()->Peb );
        status = wine_server_call( req );
    }
    SERVER_END_REQ;

    return status;
}

static unsigned int runtime_open_exe( const char *path, HANDLE *handle )
{
    OBJECT_ATTRIBUTES attr;

    memset( &attr, 0, sizeof(attr) );
    attr.Length = sizeof(attr);
    return open_unix_file( handle, path, FILE_READ_DATA | SYNCHRONIZE, &attr,
                           FILE_ATTRIBUTE_NORMAL, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           FILE_OPEN, FILE_SYNCHRONOUS_IO_NONALERT, NULL, 0 );
}

static int file_exists( const char *path )
{
    struct stat st;

    return !stat( path, &st ) && S_ISREG( st.st_mode );
}

static const char *path_basename( const char *path )
{
    const char *slash = strrchr( path, '/' );
    const char *backslash = strrchr( path, '\\' );

    if (!slash || backslash > slash) slash = backslash;
    return slash ? slash + 1 : path;
}

static void path_dirname( const char *path, char *dir, size_t size )
{
    const char *base = path_basename( path );
    size_t len = base > path ? (size_t)(base - path - 1) : 0;

    if (!len)
    {
        snprintf( dir, size, "%s", WINE_DRIVE_C );
        return;
    }
    if (len >= size) len = size - 1;
    memcpy( dir, path, len );
    dir[len] = 0;
}

static int join_path( char *out, size_t size, const char *dir, const char *name )
{
    int ret = snprintf( out, size, "%s/%s", dir, name );

    return ret > 0 && (size_t)ret < size;
}

static int path_starts_with( const char *path, const char *prefix )
{
    size_t len = strlen( prefix );

    return !strncmp( path, prefix, len );
}

static void slash_to_backslash( char *path )
{
    for (; *path; path++) if (*path == '/') *path = '\\';
}

static int target_to_dos_path( const char *target, char *dos_path, size_t size )
{
    const char *drive_c = WINE_DRIVE_C "/";
    const char *relative;
    int ret;

    if (strlen( target ) > 2 && target[1] == ':')
    {
        ret = snprintf( dos_path, size, "%s", target );
        if (ret <= 0 || (size_t)ret >= size) return 0;
        slash_to_backslash( dos_path );
        return 1;
    }

    if (path_starts_with( target, drive_c ))
    {
        relative = target + strlen( drive_c );
        ret = snprintf( dos_path, size, "C:\\%s", relative );
    }
    else ret = snprintf( dos_path, size, "C:\\%s", path_basename( target ) );

    if (ret <= 0 || (size_t)ret >= size) return 0;
    slash_to_backslash( dos_path );
    return 1;
}

static void dos_dirname( const char *path, char *dir, size_t size )
{
    const char *slash = strrchr( path, '\\' );
    size_t len;

    if (!slash)
    {
        snprintf( dir, size, "C:\\" );
        return;
    }
    len = slash - path;
    if (len < 3) len = 3;
    if (len >= size) len = size - 1;
    memcpy( dir, path, len );
    dir[len] = 0;
}

static void put_process_string( WCHAR **cursor, UNICODE_STRING *string, const char *value )
{
    size_t i, len = value ? strlen( value ) : 0;

    string->Buffer = *cursor;
    string->Length = len * sizeof(WCHAR);
    string->MaximumLength = (len + 1) * sizeof(WCHAR);
    for (i = 0; i < len; i++) (*cursor)[i] = (unsigned char)value[i];
    (*cursor)[len] = 0;
    *cursor += len + 1;
}

static RTL_USER_PROCESS_PARAMETERS *runtime_create_process_params( const char *target,
                                                                   UNICODE_STRING *main_nt_name,
                                                                   char *dos_path, size_t dos_path_size )
{
    RTL_USER_PROCESS_PARAMETERS *params;
    char nt_path[640], dll_path[1024], current_dir[512];
    char cmdline[1024], args_buf[896];
    size_t chars, size;
    WCHAR *cursor;
    const char *cmdline_str;

    if (!target_to_dos_path( target, dos_path, dos_path_size )) return NULL;
    dos_dirname( dos_path, current_dir, sizeof(current_dir) );
    snprintf( nt_path, sizeof(nt_path), "\\??\\%s", dos_path );
    snprintf( dll_path, sizeof(dll_path), "%s;C:\\windows\\system32;C:\\windows;C:\\",
              current_dir );

    /* Read args.txt next to the target NRO (sdmc:/switch/wine/args.txt).
     * Format expected: "<argv[0]> <args...>" — a full Win32 command line.
     * If present, use it verbatim as CommandLine so curl etc. see args via
     * GetCommandLineA/W. Otherwise fall back to the dos_path alone. */
    cmdline_str = dos_path;
    if (read_first_line( RUNTIME_DIR "/args.txt", args_buf, sizeof(args_buf) ) && args_buf[0])
    {
        snprintf( cmdline, sizeof(cmdline), "%s", args_buf );
        cmdline_str = cmdline;
        log_line( "[ARGS] CommandLine='%s'", cmdline_str );
    }
    else
    {
        log_line( "[ARGS] no args.txt; CommandLine='%s'", cmdline_str );
    }

    chars = strlen( current_dir ) + 1;
    chars += strlen( dll_path ) + 1;
    chars += strlen( dos_path ) + 1;
    chars += strlen( cmdline_str ) + 1;
    chars += strlen( dos_path ) + 1;
    chars += strlen( nt_path ) + 1;
    chars += 2; /* empty environment */
    size = sizeof(*params) + chars * sizeof(WCHAR);

    if (!(params = calloc( 1, size ))) return NULL;
    params->AllocationSize = size;
    params->Size = size;
    params->Flags = PROCESS_PARAMS_FLAG_NORMALIZED;
    /* The Switch runtime presents one foreground desktop application.  Use
     * the standard Win32 startup hint so applications maximize their own
     * top-level window while dialogs and child windows keep normal sizing. */
    params->dwFlags = STARTF_USESHOWWINDOW;
    params->wShowWindow = SW_SHOWMAXIMIZED;
    params->ProcessGroupId = GetCurrentProcessId();

    cursor = (WCHAR *)(params + 1);
    put_process_string( &cursor, &params->CurrentDirectory.DosPath, current_dir );
    put_process_string( &cursor, &params->DllPath, dll_path );
    put_process_string( &cursor, &params->ImagePathName, dos_path );
    put_process_string( &cursor, &params->CommandLine, cmdline_str );
    put_process_string( &cursor, &params->WindowTitle, dos_path );
    put_process_string( &cursor, main_nt_name, nt_path );
    params->Environment = cursor;
    *cursor++ = 0;
    *cursor++ = 0;
    params->EnvironmentSize = 2 * sizeof(WCHAR);
    return params;
}

static void runtime_init_peb_process( TEB *teb, void *module,
                                      RTL_USER_PROCESS_PARAMETERS *params )
{
    PEB *peb = teb->Peb;

    peb->ImageBaseAddress           = module;
    peb->ProcessParameters          = params;
    peb->OSMajorVersion             = 10;
    peb->OSMinorVersion             = 0;
    peb->OSBuildNumber              = 19045;
    peb->OSPlatformId               = VER_PLATFORM_WIN32_NT;
    peb->ImageSubSystem             = main_image_info.SubSystemType;
    peb->ImageSubSystemMajorVersion = main_image_info.MajorSubsystemVersion;
    peb->ImageSubSystemMinorVersion = main_image_info.MinorSubsystemVersion;
}

static int dll_name_matches( const char *loaded, const char *wanted )
{
    return !strcasecmp( loaded, wanted );
}

static struct runtime_module *find_module_by_name( const char *name )
{
    unsigned int i;

    for (i = 0; i < module_count; i++)
        if (dll_name_matches( modules[i].name, name )) return &modules[i];
    return NULL;
}

static void *rva_ptr( const struct runtime_module *module, DWORD rva, SIZE_T bytes )
{
    if (!rva || rva >= module->size) return NULL;
    if (bytes > module->size - rva) return NULL;
    return (char *)module->base + rva;
}

static IMAGE_NT_HEADERS64 *runtime_nt_headers( void *module )
{
    IMAGE_DOS_HEADER *dos = module;

    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return NULL;
    return (IMAGE_NT_HEADERS64 *)((char *)module + dos->e_lfanew);
}

static unsigned int map_pe_image( const char *path, void **module, SIZE_T *view_size )
{
    HANDLE file = 0, section = 0;
    unsigned int status;

    *module = NULL;
    *view_size = 0;

    status = runtime_open_exe( path, &file );
    if (status) return status;

    status = NtCreateSection( &section, SECTION_MAP_READ | SECTION_MAP_EXECUTE | SECTION_QUERY,
                              NULL, NULL, PAGE_EXECUTE_READ, SEC_IMAGE, file );
    if (!status)
    {
        status = NtMapViewOfSection( section, NtCurrentProcess(), module, 0, 0, NULL,
                                     view_size, ViewShare, 0, PAGE_EXECUTE_READ );
        close_handle_object( section );
    }
    close_handle_object( file );
    return status;
}

static struct runtime_module *register_module( const char *path, void *base, SIZE_T size, int is_main )
{
    struct runtime_module *module;
    IMAGE_NT_HEADERS64 *nt;

    if (module_count >= MAX_RUNTIME_MODULES)
    {
        log_line( "[FAIL] module table full" );
        return NULL;
    }

    nt = runtime_nt_headers( base );
    if (!nt || nt->Signature != IMAGE_NT_SIGNATURE ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
    {
        log_line( "[FAIL] %s mapped image does not look like PE32+", path );
        return NULL;
    }

    module = &modules[module_count++];
    memset( module, 0, sizeof(*module) );
    snprintf( module->path, sizeof(module->path), "%s", path );
    snprintf( module->name, sizeof(module->name), "%s", path_basename( path ) );
    path_dirname( path, module->dir, sizeof(module->dir) );
    module->base = base;
    module->size = size;
    module->nt = nt;
    module->is_main = is_main;

    log_line( "[LOAD] %s base=%p size=0x%lx entry=0x%x",
              module->name, module->base, (unsigned long)module->size,
              module->nt->OptionalHeader.AddressOfEntryPoint );
    return module;
}

static int find_dll_path( const struct runtime_module *parent, const char *dll, char *path, size_t size )
{
    if (parent && join_path( path, size, parent->dir, dll ) && file_exists( path )) return 1;
    if (join_path( path, size, WINE_SYSTEM_DIR, dll ) && file_exists( path )) return 1;
    if (join_path( path, size, WINE_DRIVE_C, dll ) && file_exists( path )) return 1;
    if (join_path( path, size, RUNTIME_DIR, dll ) && file_exists( path )) return 1;
    return 0;
}

static struct runtime_module *load_dll_module( const struct runtime_module *parent, const char *dll,
                                               struct import_stats *stats )
{
    char path[512];
    void *base;
    SIZE_T size;
    unsigned int status;
    struct runtime_module *module;

    if ((module = find_module_by_name( dll ))) return module;
    if (!find_dll_path( parent, dll, path, sizeof(path) ))
    {
        log_line( "[MISS] DLL %s not found in local runtime paths", dll );
        stats->missing_dlls++;
        return NULL;
    }

    status = map_pe_image( path, &base, &size );
    if (status)
    {
        log_line( "[FAIL] load DLL %s status=%08x", path, status );
        stats->missing_dlls++;
        return NULL;
    }

    module = register_module( path, base, size, 0 );
    if (module) stats->loaded_dlls++;
    return module;
}

static void *resolve_forwarder( const struct runtime_module *parent, const char *forwarder,
                                struct import_stats *stats, int depth );

static void *resolve_export( const struct runtime_module *module, const char *name, WORD ordinal,
                             const struct runtime_module *parent, struct import_stats *stats, int depth )
{
    const IMAGE_DATA_DIRECTORY *dir = &module->nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    IMAGE_EXPORT_DIRECTORY *exports;
    DWORD *functions, *names;
    WORD *ordinals;
    DWORD function_rva = 0;
    DWORD index;
    unsigned int i;

    if (!dir->VirtualAddress || !dir->Size) return NULL;
    exports = rva_ptr( module, dir->VirtualAddress, sizeof(*exports) );
    if (!exports) return NULL;

    functions = rva_ptr( module, exports->AddressOfFunctions, exports->NumberOfFunctions * sizeof(*functions) );
    names = rva_ptr( module, exports->AddressOfNames, exports->NumberOfNames * sizeof(*names) );
    ordinals = rva_ptr( module, exports->AddressOfNameOrdinals, exports->NumberOfNames * sizeof(*ordinals) );
    if (!functions || (!names && exports->NumberOfNames) || (!ordinals && exports->NumberOfNames)) return NULL;

    if (name)
    {
        for (i = 0; i < exports->NumberOfNames; i++)
        {
            const char *export_name = rva_ptr( module, names[i], 1 );

            if (!export_name || strcmp( export_name, name )) continue;
            index = ordinals[i];
            if (index >= exports->NumberOfFunctions) return NULL;
            function_rva = functions[index];
            break;
        }
        if (!function_rva) return NULL;
    }
    else
    {
        if (ordinal < exports->Base) return NULL;
        index = ordinal - exports->Base;
        if (index >= exports->NumberOfFunctions) return NULL;
        function_rva = functions[index];
    }

    if (function_rva >= dir->VirtualAddress && function_rva < dir->VirtualAddress + dir->Size)
    {
        const char *forwarder = rva_ptr( module, function_rva, 1 );

        if (!forwarder) return NULL;
        stats->forwarded++;
        return resolve_forwarder( parent, forwarder, stats, depth + 1 );
    }

    return rva_ptr( module, function_rva, 1 );
}

static void *resolve_forwarder( const struct runtime_module *parent, const char *forwarder,
                                struct import_stats *stats, int depth )
{
    char dll[128], name[128];
    const char *dot = strrchr( forwarder, '.' );
    struct runtime_module *module;

    if (!dot || dot == forwarder || depth > MAX_IMPORT_DEPTH) return NULL;
    if ((size_t)(dot - forwarder) >= sizeof(dll)) return NULL;
    memcpy( dll, forwarder, dot - forwarder );
    dll[dot - forwarder] = 0;
    if (!strchr( dll, '.' )) strncat( dll, ".dll", sizeof(dll) - strlen(dll) - 1 );
    snprintf( name, sizeof(name), "%s", dot + 1 );

    module = load_dll_module( parent, dll, stats );
    if (!module) return NULL;
    if (name[0] == '#') return resolve_export( module, NULL, (WORD)strtoul( name + 1, NULL, 10 ),
                                               parent, stats, depth + 1 );
    return resolve_export( module, name, 0, parent, stats, depth + 1 );
}

static int write_iat_entry( ULONGLONG *slot, void *value )
{
    void *protect_base = (void *)((uintptr_t)slot & ~(uintptr_t)0xfff);
    SIZE_T protect_size = ((uintptr_t)slot - (uintptr_t)protect_base) + sizeof(*slot);
    ULONG old_protect = 0;
    unsigned int status;

    status = NtProtectVirtualMemory( NtCurrentProcess(), &protect_base, &protect_size,
                                     PAGE_READWRITE, &old_protect );
    if (status)
    {
        log_line( "[FAIL] NtProtectVirtualMemory(IAT) status=%08x", status );
        return 0;
    }

    *slot = (ULONGLONG)(uintptr_t)value;

    status = NtProtectVirtualMemory( NtCurrentProcess(), &protect_base, &protect_size,
                                     old_protect, &old_protect );
    if (status) log_line( "[WARN] restore IAT protection status=%08x", status );
    return 1;
}

static void __attribute__((unused)) resolve_module_imports( struct runtime_module *module,
                                                            struct import_stats *stats, int depth )
{
    const IMAGE_DATA_DIRECTORY *dir = &module->nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    IMAGE_IMPORT_DESCRIPTOR *desc;
    unsigned int desc_count = 0;

    if (module->imports_scanned || module->resolving_imports) return;
    if (depth > MAX_IMPORT_DEPTH)
    {
        log_line( "[MISS] import recursion limit at %s", module->name );
        stats->unresolved++;
        return;
    }

    module->resolving_imports = 1;
    if (!dir->VirtualAddress || !dir->Size)
    {
        module->imports_scanned = 1;
        module->resolving_imports = 0;
        return;
    }

    desc = rva_ptr( module, dir->VirtualAddress, sizeof(*desc) );
    if (!desc)
    {
        log_line( "[FAIL] invalid import directory in %s", module->name );
        stats->unresolved++;
        module->resolving_imports = 0;
        return;
    }

    for (; desc->Name || desc->FirstThunk || desc->OriginalFirstThunk; desc++, desc_count++)
    {
        const char *dll_name;
        IMAGE_THUNK_DATA64 *lookup, *iat;
        DWORD lookup_rva;
        struct runtime_module *dll_module;
        unsigned int thunk_count = 0;

        if (desc_count > 512)
        {
            log_line( "[FAIL] too many import descriptors in %s", module->name );
            stats->unresolved++;
            break;
        }

        dll_name = rva_ptr( module, desc->Name, 1 );
        if (!dll_name)
        {
            log_line( "[FAIL] invalid import DLL name in %s", module->name );
            stats->unresolved++;
            continue;
        }

        stats->dlls++;
        log_line( "[IMPORT] %s -> %s", module->name, dll_name );
        dll_module = load_dll_module( module, dll_name, stats );
        if (!dll_module)
        {
            stats->unresolved++;
            continue;
        }

        resolve_module_imports( dll_module, stats, depth + 1 );

        lookup_rva = desc->OriginalFirstThunk ? desc->OriginalFirstThunk : desc->FirstThunk;
        lookup = rva_ptr( module, lookup_rva, sizeof(*lookup) );
        iat = rva_ptr( module, desc->FirstThunk, sizeof(*iat) );
        if (!lookup || !iat)
        {
            log_line( "[FAIL] invalid thunk table for %s in %s", dll_name, module->name );
            stats->unresolved++;
            continue;
        }

        for (; lookup->u1.AddressOfData; lookup++, iat++, thunk_count++)
        {
            const char *import_name = NULL;
            WORD ordinal = 0;
            void *target;

            if (thunk_count > 8192)
            {
                log_line( "[FAIL] too many thunks for %s in %s", dll_name, module->name );
                stats->unresolved++;
                break;
            }

            stats->imports++;
            if (IMAGE_SNAP_BY_ORDINAL64( lookup->u1.Ordinal ))
            {
                ordinal = IMAGE_ORDINAL64( lookup->u1.Ordinal );
                target = resolve_export( dll_module, NULL, ordinal, module, stats, depth + 1 );
            }
            else
            {
                IMAGE_IMPORT_BY_NAME *by_name = rva_ptr( module, (DWORD)lookup->u1.AddressOfData,
                                                          sizeof(*by_name) );

                if (!by_name)
                {
                    log_line( "[MISS] invalid import name rva=0x%llx in %s",
                              (unsigned long long)lookup->u1.AddressOfData, module->name );
                    stats->unresolved++;
                    continue;
                }
                import_name = by_name->Name;
                target = resolve_export( dll_module, import_name, 0, module, stats, depth + 1 );
            }

            if (!target)
            {
                if (import_name) log_line( "[MISS] %s!%s", dll_name, import_name );
                else log_line( "[MISS] %s ordinal %u", dll_name, ordinal );
                stats->unresolved++;
                continue;
            }

            if (write_iat_entry( &iat->u1.Function, target ))
            {
                stats->bound++;
                if (import_name) log_line( "[BIND] %s!%s -> %p", dll_name, import_name, target );
                else log_line( "[BIND] %s ordinal %u -> %p", dll_name, ordinal, target );
            }
            else stats->unresolved++;
        }
    }

    module->imports_scanned = 1;
    module->resolving_imports = 0;
}

static int runtime_describe_image( void *module, SIZE_T size, void **entry )
{
    IMAGE_NT_HEADERS64 *nt = runtime_nt_headers( module );
    IMAGE_DATA_DIRECTORY *imports;

    if (!nt || nt->Signature != IMAGE_NT_SIGNATURE ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
    {
        log_line( "[FAIL] mapped image does not look like PE32+" );
        return 0;
    }

    imports = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    *entry = (char *)module + nt->OptionalHeader.AddressOfEntryPoint;

    main_image_info.TransferAddress = *entry;
    main_image_info.MaximumStackSize = nt->OptionalHeader.SizeOfStackReserve;
    main_image_info.CommittedStackSize = nt->OptionalHeader.SizeOfStackCommit;
    main_image_info.SubSystemType = nt->OptionalHeader.Subsystem;
    main_image_info.MajorSubsystemVersion = nt->OptionalHeader.MajorSubsystemVersion;
    main_image_info.MinorSubsystemVersion = nt->OptionalHeader.MinorSubsystemVersion;
    main_image_info.MajorOperatingSystemVersion = nt->OptionalHeader.MajorOperatingSystemVersion;
    main_image_info.MinorOperatingSystemVersion = nt->OptionalHeader.MinorOperatingSystemVersion;
    main_image_info.ImageCharacteristics = nt->FileHeader.Characteristics;
    main_image_info.DllCharacteristics = nt->OptionalHeader.DllCharacteristics;
    main_image_info.Machine = nt->FileHeader.Machine;
    main_image_info.ImageContainsCode = TRUE;
    main_image_info.ImageFlags = 0;
    if (nt->OptionalHeader.DllCharacteristics & IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE)
        main_image_info.ImageDynamicallyRelocated = 1;
    main_image_info.LoaderFlags = nt->OptionalHeader.LoaderFlags;
    main_image_info.ImageFileSize = nt->OptionalHeader.SizeOfImage;
    main_image_info.CheckSum = nt->OptionalHeader.CheckSum;

    log_line( "[IMAGE] base=%p size=0x%lx preferred=0x%llx entry_rva=0x%x machine=0x%x",
              module, (unsigned long)size,
              (unsigned long long)nt->OptionalHeader.ImageBase,
              nt->OptionalHeader.AddressOfEntryPoint, nt->FileHeader.Machine );
    log_line( "[IMAGE] subsystem=%u dll_char=0x%x imports=0x%x/0x%x sections=%u",
              nt->OptionalHeader.Subsystem, nt->OptionalHeader.DllCharacteristics,
              imports->VirtualAddress, imports->Size, nt->FileHeader.NumberOfSections );
    return 1;
}

int main( int argc, char **argv )
{
    char target[512] = DEFAULT_TARGET;
    TEB *teb;
    void *module = NULL;
    void *entry = NULL;
    SIZE_T view_size = 0;
    struct runtime_module *main_module;
    RTL_USER_PROCESS_PARAMETERS *params;
    UNICODE_STRING main_nt_name;
    char dos_path[512];
    unsigned int status;
    unsigned int ldr_status = STATUS_INVALID_IMAGE_FORMAT;
    unsigned int attach_status = STATUS_INVALID_IMAGE_FORMAT;
    int autorun;

    log_main_thread = pthread_self();
    log_main_thread_set = 1;
    consoleInit( NULL );
    mkdir( "sdmc:/switch", 0777 );
    mkdir( RUNTIME_DIR, 0777 );
    mkdir( WINE_DRIVE_C, 0777 );
    mkdir( WINE_DRIVE_C "/windows", 0777 );
    mkdir( WINE_SYSTEM_DIR, 0777 );
    log_file = fopen( RUNTIME_DIR "/wine-nx-runtime.log", "w" );

    if (argc > 1 && argv[1] && argv[1][0]) snprintf( target, sizeof(target), "%s", argv[1] );
    else read_first_line( RUNTIME_DIR "/target.txt", target, sizeof(target) );
    autorun = read_bool_file( RUNTIME_DIR "/run-entry.txt" );

    log_line( "wine-nx-runtime: generic Wine ntdll PE loader path" );
    log_line( "[BUILD] %s", WINE_NX_RUNTIME_BUILD );
    log_line( "[TARGET] %s", target );

    wine_nx_runtime_platform_init();
    log_line( "[INIT] Wine paths/unix bridge ready" );
    virtual_init();
    log_line( "[INIT] virtual memory ready" );

    /* TEMPORARY: verify __libnx_exception_handler wiring. Set to 0 to disable. */
#define WINE_NX_TEST_FAULT 0
#if WINE_NX_TEST_FAULT
    log_line( "[TEST] about to deliberately deref NULL to verify exception handler" );
    fflush( log_file );
    {
        volatile int *null_ptr = (volatile int *)(uintptr_t)0;
        volatile int observed = *null_ptr;
        log_line( "[TEST] NULL deref did NOT fault, value=%d (handler not wired correctly)", observed );
    }
#endif
    wine_nx_runtime_environment_init();
    log_line( "[INIT] Wine NLS/environment ready" );
    teb = virtual_alloc_first_teb();
    if (!teb || NtCurrentTeb() != teb || !teb->Peb)
    {
        log_line( "[FAIL] virtual_alloc_first_teb" );
        park_forever();
    }

    server_init_process();
    status = runtime_init_process_done();
    if (status)
    {
        log_line( "[FAIL] init_process_done status=%08x", status );
        park_forever();
    }

    status = map_pe_image( target, &module, &view_size );
    if (status)
    {
        log_line( "[FAIL] map target status=%08x", status );
        park_forever();
    }

    if (runtime_describe_image( module, view_size, &entry ))
    {
        params = runtime_create_process_params( target, &main_nt_name, dos_path, sizeof(dos_path) );
        if (!params)
        {
            log_line( "[FAIL] process parameter allocation" );
            park_forever();
        }
        runtime_init_peb_process( teb, module, params );
        log_line( "[PEB] image=%s nt=\\??\\%s", dos_path, dos_path );

        main_module = register_module( target, module, view_size, 1 );
        if (main_module)
        {
            status = wine_nx_loader_bootstrap( &main_nt_name );
            log_line( "[LDR] bootstrap status=%08x", status );
            if (!status)
            {
                ldr_status = wine_nx_loader_fixup_main_imports();
                log_line( "[LDR] fixup_imports status=%08x", ldr_status );
                if (ldr_status && wine_nx_loader_last_import_dll()[0])
                    log_line( "[LDR] last failed import=%s status=%08x",
                              wine_nx_loader_last_import_dll(),
                              wine_nx_loader_last_import_status() );
                if (ldr_status && wine_nx_loader_last_open_path()[0])
                    log_line( "[LDR] last dll open=%s status=%08x",
                              wine_nx_loader_last_open_path(),
                              wine_nx_loader_last_open_status() );
                if (ldr_status && wine_nx_loader_last_export_diag()[0])
                    log_line( "[LDR] export diag=%s", wine_nx_loader_last_export_diag() );
                if (!ldr_status)
                {
                    attach_status = wine_nx_loader_attach_main();
                    log_line( "[LDR] process_attach status=%08x", attach_status );
                }
            }
        }
        log_line( "[READY] PE image is mapped by Wine ntdll; entry=%p", entry );
        if (autorun && !ldr_status && !attach_status)
        {
            log_line( "[RUN] run-entry.txt enabled; jumping to PE entry after Wine loader attach" );
            log_line( "[RUN] entry returned %d", call_pe_entry_point( entry ) );
        }
        else if (autorun)
            log_line( "[BLOCK] run-entry.txt enabled, but loader status import=%08x attach=%08x",
                      ldr_status, attach_status );
    }

    park_forever();
    return 0;
}
