/*
 * Minimal <dlfcn.h> shim for the Switch (Horizon) win32u build.
 *
 * Horizon has no runtime dynamic linker, so every dlopen() of a host driver
 * (libEGL/libvulkan/libfreetype/...) fails and the caller falls back to its
 * "feature not available" path.  These no-op stubs let the dlopen-based code
 * in opengl.c/vulkan.c/freetype.c compile; the loads themselves return NULL.
 */
#ifndef __WINE_NX_DLFCN_SHIM_H
#define __WINE_NX_DLFCN_SHIM_H

#define RTLD_LAZY    0x0001
#define RTLD_NOW     0x0002
#define RTLD_LOCAL   0x0000
#define RTLD_GLOBAL  0x0100
#define RTLD_DEFAULT ((void *)0)
#define RTLD_NEXT    ((void *)-1)

typedef struct
{
    const char *dli_fname;
    void       *dli_fbase;
    const char *dli_sname;
    void       *dli_saddr;
} Dl_info;

static inline void *dlopen( const char *file, int mode ) { (void)file; (void)mode; return (void *)0; }
static inline int   dlclose( void *handle ) { (void)handle; return 0; }
static inline void *dlsym( void *handle, const char *name ) { (void)handle; (void)name; return (void *)0; }
static inline char *dlerror( void ) { return (char *)"dlfcn not supported on Horizon"; }
static inline int   dladdr( const void *addr, Dl_info *info ) { (void)addr; (void)info; return 0; }

#endif /* __WINE_NX_DLFCN_SHIM_H */
