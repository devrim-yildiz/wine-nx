/*
 * Switch software-window build: D3DKMT / NtGdiDdDDI* GPU kernel thunks stubbed.
 *
 * The real d3dkmt.c pulls in the full d3d9/10/11/12/dxgi header chain (for
 * struct-layout cross-checks) which the Switch build does not generate, and
 * implements GPU resource sharing irrelevant to the software-rendered window
 * path.  These stubs keep every NtGdiDdDDI* syscall entry point defined so the
 * win32u syscall table (KeServiceDescriptorTable[1]) stays complete, and keep
 * the d3dkmt_* helpers opengl.c references resolvable.  Restore the real file
 * when a GPU backend lands.
 */

#if 0
#pragma makedep unix
#endif

#include "config.h"

#include "ntstatus.h"
#define WIN32_NO_STATUS
/* ntgdi_private.h pulls the full header chain (wingdi/winbase -> SYSTEMTIME)
 * in the order ntgdi.h/winspool.h expect; mirror the real d3dkmt.c. */
#include "ntgdi_private.h"
#include "win32u_private.h"
#include "ntuser_private.h"

/* --- NtGdiDdDDI* syscall entry points (auto-generated from ntgdi.h) --- */
NTSTATUS WINAPI NtGdiDdDDIAcquireKeyedMutex( D3DKMT_ACQUIREKEYEDMUTEX *params ) { return STATUS_PROCEDURE_NOT_FOUND; }
NTSTATUS WINAPI NtGdiDdDDIAcquireKeyedMutex2( D3DKMT_ACQUIREKEYEDMUTEX2 *params ) { return STATUS_PROCEDURE_NOT_FOUND; }
NTSTATUS WINAPI NtGdiDdDDICheckOcclusion( const D3DKMT_CHECKOCCLUSION *desc ) { return STATUS_PROCEDURE_NOT_FOUND; }
NTSTATUS WINAPI NtGdiDdDDICheckVidPnExclusiveOwnership( const D3DKMT_CHECKVIDPNEXCLUSIVEOWNERSHIP *desc ) { return STATUS_PROCEDURE_NOT_FOUND; }
NTSTATUS WINAPI NtGdiDdDDICloseAdapter( const D3DKMT_CLOSEADAPTER *desc ) { return STATUS_PROCEDURE_NOT_FOUND; }
NTSTATUS WINAPI NtGdiDdDDICreateAllocation( D3DKMT_CREATEALLOCATION *params ) { return STATUS_PROCEDURE_NOT_FOUND; }
NTSTATUS WINAPI NtGdiDdDDICreateAllocation2( D3DKMT_CREATEALLOCATION *params ) { return STATUS_PROCEDURE_NOT_FOUND; }
NTSTATUS WINAPI NtGdiDdDDICreateDevice( D3DKMT_CREATEDEVICE *desc ) { return STATUS_PROCEDURE_NOT_FOUND; }
NTSTATUS WINAPI NtGdiDdDDICreateKeyedMutex( D3DKMT_CREATEKEYEDMUTEX *params ) { return STATUS_PROCEDURE_NOT_FOUND; }
NTSTATUS WINAPI NtGdiDdDDICreateKeyedMutex2( D3DKMT_CREATEKEYEDMUTEX2 *params ) { return STATUS_PROCEDURE_NOT_FOUND; }
NTSTATUS WINAPI NtGdiDdDDICreateSynchronizationObject( D3DKMT_CREATESYNCHRONIZATIONOBJECT *params ) { return STATUS_PROCEDURE_NOT_FOUND; }
NTSTATUS WINAPI NtGdiDdDDICreateSynchronizationObject2( D3DKMT_CREATESYNCHRONIZATIONOBJECT2 *params ) { return STATUS_PROCEDURE_NOT_FOUND; }
NTSTATUS WINAPI NtGdiDdDDIDestroyAllocation( const D3DKMT_DESTROYALLOCATION *params ) { return STATUS_PROCEDURE_NOT_FOUND; }
NTSTATUS WINAPI NtGdiDdDDIDestroyAllocation2( const D3DKMT_DESTROYALLOCATION2 *params ) { return STATUS_PROCEDURE_NOT_FOUND; }
NTSTATUS WINAPI NtGdiDdDDIDestroyDevice( const D3DKMT_DESTROYDEVICE *desc ) { return STATUS_PROCEDURE_NOT_FOUND; }
NTSTATUS WINAPI NtGdiDdDDIDestroyKeyedMutex( const D3DKMT_DESTROYKEYEDMUTEX *params ) { return STATUS_PROCEDURE_NOT_FOUND; }
NTSTATUS WINAPI NtGdiDdDDIDestroySynchronizationObject( const D3DKMT_DESTROYSYNCHRONIZATIONOBJECT *params ) { return STATUS_PROCEDURE_NOT_FOUND; }
NTSTATUS WINAPI NtGdiDdDDIEscape( const D3DKMT_ESCAPE *desc ) { return STATUS_PROCEDURE_NOT_FOUND; }
NTSTATUS WINAPI NtGdiDdDDIOpenAdapterFromHdc( D3DKMT_OPENADAPTERFROMHDC *desc ) { return STATUS_PROCEDURE_NOT_FOUND; }
NTSTATUS WINAPI NtGdiDdDDIOpenAdapterFromLuid( D3DKMT_OPENADAPTERFROMLUID *desc ) { return STATUS_PROCEDURE_NOT_FOUND; }
NTSTATUS WINAPI NtGdiDdDDIOpenKeyedMutex( D3DKMT_OPENKEYEDMUTEX *params ) { return STATUS_PROCEDURE_NOT_FOUND; }
NTSTATUS WINAPI NtGdiDdDDIOpenKeyedMutex2( D3DKMT_OPENKEYEDMUTEX2 *params ) { return STATUS_PROCEDURE_NOT_FOUND; }
NTSTATUS WINAPI NtGdiDdDDIOpenKeyedMutexFromNtHandle( D3DKMT_OPENKEYEDMUTEXFROMNTHANDLE *params ) { return STATUS_PROCEDURE_NOT_FOUND; }
NTSTATUS WINAPI NtGdiDdDDIOpenNtHandleFromName( D3DKMT_OPENNTHANDLEFROMNAME *params ) { return STATUS_PROCEDURE_NOT_FOUND; }
NTSTATUS WINAPI NtGdiDdDDIOpenResource( D3DKMT_OPENRESOURCE *params ) { return STATUS_PROCEDURE_NOT_FOUND; }
NTSTATUS WINAPI NtGdiDdDDIOpenResource2( D3DKMT_OPENRESOURCE *params ) { return STATUS_PROCEDURE_NOT_FOUND; }
NTSTATUS WINAPI NtGdiDdDDIOpenResourceFromNtHandle( D3DKMT_OPENRESOURCEFROMNTHANDLE *params ) { return STATUS_PROCEDURE_NOT_FOUND; }
NTSTATUS WINAPI NtGdiDdDDIOpenSynchronizationObject( D3DKMT_OPENSYNCHRONIZATIONOBJECT *params ) { return STATUS_PROCEDURE_NOT_FOUND; }
NTSTATUS WINAPI NtGdiDdDDIOpenSyncObjectFromNtHandle( D3DKMT_OPENSYNCOBJECTFROMNTHANDLE *params ) { return STATUS_PROCEDURE_NOT_FOUND; }
NTSTATUS WINAPI NtGdiDdDDIOpenSyncObjectFromNtHandle2( D3DKMT_OPENSYNCOBJECTFROMNTHANDLE2 *params ) { return STATUS_PROCEDURE_NOT_FOUND; }
NTSTATUS WINAPI NtGdiDdDDIOpenSyncObjectNtHandleFromName( D3DKMT_OPENSYNCOBJECTNTHANDLEFROMNAME *params ) { return STATUS_PROCEDURE_NOT_FOUND; }
NTSTATUS WINAPI NtGdiDdDDIQueryAdapterInfo( D3DKMT_QUERYADAPTERINFO *desc ) { return STATUS_PROCEDURE_NOT_FOUND; }
NTSTATUS WINAPI NtGdiDdDDIQueryResourceInfo( D3DKMT_QUERYRESOURCEINFO *params ) { return STATUS_PROCEDURE_NOT_FOUND; }
NTSTATUS WINAPI NtGdiDdDDIQueryResourceInfoFromNtHandle( D3DKMT_QUERYRESOURCEINFOFROMNTHANDLE *params ) { return STATUS_PROCEDURE_NOT_FOUND; }
NTSTATUS WINAPI NtGdiDdDDIQueryStatistics( D3DKMT_QUERYSTATISTICS *stats ) { return STATUS_PROCEDURE_NOT_FOUND; }
NTSTATUS WINAPI NtGdiDdDDIQueryVideoMemoryInfo( D3DKMT_QUERYVIDEOMEMORYINFO *desc ) { return STATUS_PROCEDURE_NOT_FOUND; }
NTSTATUS WINAPI NtGdiDdDDIReleaseKeyedMutex( D3DKMT_RELEASEKEYEDMUTEX *params ) { return STATUS_PROCEDURE_NOT_FOUND; }
NTSTATUS WINAPI NtGdiDdDDIReleaseKeyedMutex2( D3DKMT_RELEASEKEYEDMUTEX2 *params ) { return STATUS_PROCEDURE_NOT_FOUND; }
NTSTATUS WINAPI NtGdiDdDDISetQueuedLimit( D3DKMT_SETQUEUEDLIMIT *desc ) { return STATUS_PROCEDURE_NOT_FOUND; }
NTSTATUS WINAPI NtGdiDdDDISetVidPnSourceOwner( const D3DKMT_SETVIDPNSOURCEOWNER *desc ) { return STATUS_PROCEDURE_NOT_FOUND; }
NTSTATUS WINAPI NtGdiDdDDIShareObjects( UINT count, const D3DKMT_HANDLE *handles, OBJECT_ATTRIBUTES *attr, UINT access, HANDLE *handle ) { return STATUS_PROCEDURE_NOT_FOUND; }
NTSTATUS WINAPI NtGdiDdDDISignalSynchronizationObjectFromCpu( const D3DKMT_SIGNALSYNCHRONIZATIONOBJECTFROMCPU *params ) { return STATUS_PROCEDURE_NOT_FOUND; }
NTSTATUS WINAPI NtGdiDdDDIWaitForSynchronizationObjectFromCpu( const D3DKMT_WAITFORSYNCHRONIZATIONOBJECTFROMCPU *params ) { return STATUS_PROCEDURE_NOT_FOUND; }

/* --- d3dkmt_* helpers referenced by opengl.c (GPU resource sharing) --- */
int d3dkmt_object_get_fd( D3DKMT_HANDLE local ) { return -1; }
NTSTATUS d3dkmt_destroy_mutex( D3DKMT_HANDLE local ) { return STATUS_PROCEDURE_NOT_FOUND; }
D3DKMT_HANDLE d3dkmt_create_resource( int fd, D3DKMT_HANDLE *global ) { if (global) *global = 0; return 0; }
D3DKMT_HANDLE d3dkmt_open_resource( D3DKMT_HANDLE global, HANDLE shared, D3DKMT_HANDLE *mutex_local, D3DKMT_HANDLE *sync_local ) { if (mutex_local) *mutex_local = 0; if (sync_local) *sync_local = 0; return 0; }
NTSTATUS d3dkmt_destroy_resource( D3DKMT_HANDLE local ) { return STATUS_PROCEDURE_NOT_FOUND; }
D3DKMT_HANDLE d3dkmt_create_sync( int fd, D3DKMT_HANDLE *global ) { if (global) *global = 0; return 0; }
D3DKMT_HANDLE d3dkmt_open_sync( D3DKMT_HANDLE global, HANDLE shared ) { return 0; }
NTSTATUS d3dkmt_destroy_sync( D3DKMT_HANDLE local ) { return STATUS_PROCEDURE_NOT_FOUND; }

/* sysparams.c enumerates Vulkan GPUs to build the display device list; no GPU
 * on the software path, so report none (it falls back to a default adapter). */
BOOL get_vulkan_gpus( struct list *gpus ) { (void)gpus; return FALSE; }
