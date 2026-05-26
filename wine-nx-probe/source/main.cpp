#include <switch.h>

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <malloc.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

extern "C"
{
#include "horizon_mman.h"
int horizon_pipe(int fd[2]);
void horizon_server_queue_fd(int fd, unsigned int handle);
int horizon_server_take_client_fd(unsigned int* handle);
unsigned int horizon_server_protocol_version(void);
int horizon_server_connect(void);
void horizon_server_send_fd(int fd);
int horizon_server_receive_fd(unsigned int* handle);
}

extern "C"
{
u32 __nx_applet_type = AppletType_Application;
size_t __nx_heap_size = 256 * 1024 * 1024;
alignas(16) uint8_t __nx_exception_stack[0x10000];
uint64_t __nx_exception_stack_size = sizeof(__nx_exception_stack);
}

namespace
{
constexpr const char* kLogDir = "sdmc:/switch/wine-nx-probe";
constexpr const char* kLogPath = "sdmc:/switch/wine-nx-probe/probe.log";
constexpr size_t kPageSize = 0x1000;
constexpr size_t kLargePageSize = 0x200000;
constexpr size_t kGuestReservationSize = size_t{4} * 1024 * 1024 * 1024;
constexpr uint32_t kServerProbeMagic = 0x574e5853; // WNXS
constexpr uint32_t kServerProbeRequestId = 0x574e5801;
constexpr uint32_t kServerProbeValue = 0x12345678;
constexpr uint32_t kServerProbeReplyValue = 0x87654321;
constexpr int kReqInitProcessDone = 4;
constexpr int kReqInitFirstThread = 5;
constexpr int kReqInitThread = 6;
constexpr int kReqCloseHandle = 21;
constexpr int kReqDupHandle = 23;
constexpr int kReqAllocateReserveObject = 24;
constexpr int kReqCompareObjects = 25;
constexpr int kReqSelect = 29;
constexpr int kReqCreateEvent = 30;
constexpr int kReqEventOp = 31;
constexpr int kReqQueryEvent = 32;
constexpr int kReqOpenEvent = 33;
constexpr int kReqCreateMutex = 36;
constexpr int kReqReleaseMutex = 37;
constexpr int kReqQueryMutex = 39;
constexpr int kReqCreateSemaphore = 40;
constexpr int kReqReleaseSemaphore = 41;
constexpr int kReqQuerySemaphore = 42;
constexpr int kReqOpenSemaphore = 43;
constexpr int kReqCreateTimer = 101;
constexpr int kReqOpenTimer = 102;
constexpr int kReqSetTimer = 103;
constexpr int kReqCancelTimer = 104;
constexpr int kReqGetTimerInfo = 105;
constexpr uint16_t kImageFileMachineArm64 = 0xaa64;
constexpr uint32_t kProbeProcessId = 0x574e5802;
constexpr uint32_t kProbeThreadId = 0x574e5803;
constexpr uint32_t kProbeSecondThreadId = 0x574e5804;
constexpr size_t kWineServerFixedMessageSize = 64;
constexpr size_t kWineApcResultSize = 40;
constexpr uint32_t kStatusSuccess = 0x00000000;
constexpr uint32_t kStatusObjectNameExists = 0x40000000;
constexpr uint32_t kStatusTimeout = 0x00000102;
constexpr uint32_t kStatusObjectNameNotFound = 0xc0000034;
constexpr uint32_t kStatusSemaphoreLimitExceeded = 0xc0000047;
constexpr uint32_t kStatusNotSameObject = 0xc00001ac;
constexpr int kEventSet = 1;
constexpr int kEventReset = 2;

struct HorizonRequestHeader
{
    int req;
    uint32_t request_size;
    uint32_t reply_size;
};

struct HorizonReplyHeader
{
    uint32_t error;
    uint32_t reply_size;
};

struct HorizonInitFirstThreadRequest
{
    HorizonRequestHeader header;
    int unix_pid;
    int unix_tid;
    int debug_level;
    int reply_fd;
    int wait_fd;
};

struct HorizonInitFirstThreadReply
{
    HorizonReplyHeader header;
    uint32_t pid;
    uint32_t tid;
    int64_t server_start;
    uint32_t session_id;
    uint32_t inproc_device;
    uint32_t info_size;
    char pad[4];
};

struct HorizonInitProcessDoneReply
{
    HorizonReplyHeader header;
    int suspend;
    char pad[4];
};

struct HorizonInitThreadRequest
{
    HorizonRequestHeader header;
    int unix_tid;
    int reply_fd;
    int wait_fd;
    uint64_t teb;
    uint64_t entry;
};

struct HorizonInitThreadReply
{
    HorizonReplyHeader header;
    int suspend;
    char pad[4];
};

struct HorizonCloseHandleRequest
{
    HorizonRequestHeader header;
    uint32_t handle;
};

struct HorizonDupHandleRequest
{
    HorizonRequestHeader header;
    uint32_t src_process;
    uint32_t src_handle;
    uint32_t dst_process;
    uint32_t access;
    uint32_t attributes;
    uint32_t options;
    char pad[4];
};

struct HorizonDupHandleReply
{
    HorizonReplyHeader header;
    uint32_t handle;
    char pad[4];
};

struct HorizonAllocateReserveObjectRequest
{
    HorizonRequestHeader header;
    int type;
};

struct HorizonAllocateReserveObjectReply
{
    HorizonReplyHeader header;
    uint32_t handle;
    char pad[4];
};

struct HorizonCompareObjectsRequest
{
    HorizonRequestHeader header;
    uint32_t first;
    uint32_t second;
    char pad[4];
};

struct HorizonSelectRequest
{
    HorizonRequestHeader header;
    int flags;
    uint64_t cookie;
    int64_t timeout;
    uint32_t size;
    uint32_t prev_apc;
};

struct HorizonSelectReply
{
    HorizonReplyHeader header;
    uint32_t apc_handle;
    int signaled;
};

struct HorizonObjectAttributes
{
    uint32_t rootdir;
    uint32_t attributes;
    uint32_t sd_len;
    uint32_t name_len;
};

struct HorizonCreateEventRequest
{
    HorizonRequestHeader header;
    uint32_t access;
    int manual_reset;
    int initial_state;
};

struct HorizonCreateEventReply
{
    HorizonReplyHeader header;
    uint32_t handle;
    char pad[4];
};

struct HorizonEventOpRequest
{
    HorizonRequestHeader header;
    uint32_t handle;
    int op;
    char pad[4];
};

struct HorizonEventOpReply
{
    HorizonReplyHeader header;
    int state;
    char pad[4];
};

struct HorizonQueryEventRequest
{
    HorizonRequestHeader header;
    uint32_t handle;
};

struct HorizonQueryEventReply
{
    HorizonReplyHeader header;
    int manual_reset;
    int state;
};

struct HorizonOpenNamedObjectRequest
{
    HorizonRequestHeader header;
    uint32_t access;
    uint32_t attributes;
    uint32_t rootdir;
};

struct HorizonCreateMutexRequest
{
    HorizonRequestHeader header;
    uint32_t access;
    int owned;
    char pad[4];
};

struct HorizonCreateMutexReply
{
    HorizonReplyHeader header;
    uint32_t handle;
    char pad[4];
};

struct HorizonReleaseMutexRequest
{
    HorizonRequestHeader header;
    uint32_t handle;
};

struct HorizonReleaseMutexReply
{
    HorizonReplyHeader header;
    uint32_t prev_count;
    char pad[4];
};

struct HorizonQueryMutexRequest
{
    HorizonRequestHeader header;
    uint32_t handle;
};

struct HorizonQueryMutexReply
{
    HorizonReplyHeader header;
    uint32_t count;
    int owned;
    int abandoned;
    char pad[4];
};

struct HorizonCreateSemaphoreRequest
{
    HorizonRequestHeader header;
    uint32_t access;
    uint32_t initial;
    uint32_t max;
};

struct HorizonCreateSemaphoreReply
{
    HorizonReplyHeader header;
    uint32_t handle;
    char pad[4];
};

struct HorizonReleaseSemaphoreRequest
{
    HorizonRequestHeader header;
    uint32_t handle;
    uint32_t count;
    char pad[4];
};

struct HorizonReleaseSemaphoreReply
{
    HorizonReplyHeader header;
    uint32_t prev_count;
    char pad[4];
};

struct HorizonQuerySemaphoreRequest
{
    HorizonRequestHeader header;
    uint32_t handle;
};

struct HorizonQuerySemaphoreReply
{
    HorizonReplyHeader header;
    uint32_t current;
    uint32_t max;
};

struct HorizonCreateTimerRequest
{
    HorizonRequestHeader header;
    uint32_t access;
    int manual;
    char pad[4];
};

struct HorizonCreateTimerReply
{
    HorizonReplyHeader header;
    uint32_t handle;
    char pad[4];
};

struct HorizonSetTimerRequest
{
    HorizonRequestHeader header;
    uint32_t handle;
    int64_t expire;
    uint64_t callback;
    uint64_t arg;
    int period;
    char pad[4];
};

struct HorizonSetTimerReply
{
    HorizonReplyHeader header;
    int signaled;
    char pad[4];
};

struct HorizonCancelTimerRequest
{
    HorizonRequestHeader header;
    uint32_t handle;
};

struct HorizonCancelTimerReply
{
    HorizonReplyHeader header;
    int signaled;
    char pad[4];
};

struct HorizonGetTimerInfoRequest
{
    HorizonRequestHeader header;
    uint32_t handle;
};

struct HorizonGetTimerInfoReply
{
    HorizonReplyHeader header;
    int64_t when;
    int signaled;
    char pad[4];
};

static_assert(sizeof(HorizonRequestHeader) == 12);
static_assert(sizeof(HorizonReplyHeader) == 8);
static_assert(sizeof(HorizonInitFirstThreadRequest) == 32);
static_assert(sizeof(HorizonInitFirstThreadReply) == 40);
static_assert(sizeof(HorizonInitProcessDoneReply) == 16);
static_assert(sizeof(HorizonInitThreadRequest) == 40);
static_assert(sizeof(HorizonInitThreadReply) == 16);
static_assert(sizeof(HorizonCloseHandleRequest) == 16);
static_assert(sizeof(HorizonDupHandleRequest) == 40);
static_assert(sizeof(HorizonDupHandleReply) == 16);
static_assert(sizeof(HorizonAllocateReserveObjectRequest) == 16);
static_assert(sizeof(HorizonAllocateReserveObjectReply) == 16);
static_assert(sizeof(HorizonCompareObjectsRequest) == 24);
static_assert(sizeof(HorizonSelectRequest) == 40);
static_assert(sizeof(HorizonSelectReply) == 16);
static_assert(sizeof(HorizonObjectAttributes) == 16);
static_assert(sizeof(HorizonCreateEventRequest) == 24);
static_assert(sizeof(HorizonCreateEventReply) == 16);
static_assert(sizeof(HorizonEventOpRequest) == 24);
static_assert(sizeof(HorizonEventOpReply) == 16);
static_assert(sizeof(HorizonQueryEventRequest) == 16);
static_assert(sizeof(HorizonQueryEventReply) == 16);
static_assert(sizeof(HorizonOpenNamedObjectRequest) == 24);
static_assert(sizeof(HorizonCreateMutexRequest) == 24);
static_assert(sizeof(HorizonCreateMutexReply) == 16);
static_assert(sizeof(HorizonReleaseMutexRequest) == 16);
static_assert(sizeof(HorizonReleaseMutexReply) == 16);
static_assert(sizeof(HorizonQueryMutexRequest) == 16);
static_assert(sizeof(HorizonQueryMutexReply) == 24);
static_assert(sizeof(HorizonCreateSemaphoreRequest) == 24);
static_assert(sizeof(HorizonCreateSemaphoreReply) == 16);
static_assert(sizeof(HorizonReleaseSemaphoreRequest) == 24);
static_assert(sizeof(HorizonReleaseSemaphoreReply) == 16);
static_assert(sizeof(HorizonQuerySemaphoreRequest) == 16);
static_assert(sizeof(HorizonQuerySemaphoreReply) == 16);
static_assert(sizeof(HorizonCreateTimerRequest) == 24);
static_assert(sizeof(HorizonCreateTimerReply) == 16);
static_assert(sizeof(HorizonSetTimerRequest) == 48);
static_assert(sizeof(HorizonSetTimerReply) == 16);
static_assert(sizeof(HorizonCancelTimerRequest) == 16);
static_assert(sizeof(HorizonCancelTimerReply) == 16);
static_assert(sizeof(HorizonGetTimerInfoRequest) == 16);
static_assert(sizeof(HorizonGetTimerInfoReply) == 24);

FILE* g_log = nullptr;
thread_local uint64_t g_tls_cookie = 0;
thread_local uint64_t g_tls_counter = 0;
volatile uintptr_t g_exec_fault_addr = 0;
volatile size_t g_exec_fault_size = 0;
volatile uintptr_t g_exec_backing = 0;
volatile uint32_t g_exception_probe_hits = 0;
volatile Result g_exception_probe_rc = 0;

static_assert(offsetof(ThreadExceptionDump, cpu_gprs) == 16);
static_assert(offsetof(ThreadExceptionDump, fp) == 248);
static_assert(offsetof(ThreadExceptionDump, pc) == 272);
static_assert(offsetof(ThreadExceptionDump, fpu_gprs) == 288);
static_assert(offsetof(ThreadExceptionDump, pstate) == 800);

[[noreturn]] void restore_thread_exception_context(ThreadExceptionDump* ctx)
{
    asm volatile(
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

void log_line(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    printf("\n");
    va_end(args);

    if (!g_log)
        return;

    va_start(args, fmt);
    vfprintf(g_log, fmt, args);
    fprintf(g_log, "\n");
    fflush(g_log);
    va_end(args);
}

const char* rc_state(Result rc)
{
    return R_SUCCEEDED(rc) ? "OK" : "FAIL";
}

const char* kernel_result_name(uint32_t desc)
{
    switch (desc)
    {
        case KernelError_OutOfSessions: return "OutOfSessions";
        case KernelError_NotImplemented: return "NotImplemented";
        case KernelError_OutOfMemory: return "OutOfMemory";
        case KernelError_OutOfHandles: return "OutOfHandles";
        case KernelError_InvalidCombination: return "InvalidCombination";
        case KernelError_InvalidState: return "InvalidState";
        case KernelError_ConnectionRefused: return "ConnectionRefused";
        case KernelError_OutOfResource: return "OutOfResource";
        default: return "unknown";
    }
}

void log_result_decode(const char* tag, Result rc)
{
    const uint32_t module = R_MODULE(rc);
    const uint32_t desc = R_DESCRIPTION(rc);

    if (module == Module_Kernel)
        log_line("%s decode module=%u desc=%u kernel=%s", tag, module, desc, kernel_result_name(desc));
    else
        log_line("%s decode module=%u desc=%u", tag, module, desc);
}

size_t align_up(size_t value, size_t alignment)
{
    return (value + alignment - 1) & ~(alignment - 1);
}

bool write_exact(int fd, const void* data, size_t size)
{
    const auto* ptr = static_cast<const uint8_t*>(data);

    while (size)
    {
        const ssize_t ret = write(fd, ptr, size);
        if (ret < 0)
        {
            if (errno == EINTR)
                continue;
            return false;
        }
        if (!ret)
            return false;
        ptr += ret;
        size -= static_cast<size_t>(ret);
    }
    return true;
}

bool read_exact(int fd, void* data, size_t size)
{
    auto* ptr = static_cast<uint8_t*>(data);

    while (size)
    {
        const ssize_t ret = read(fd, ptr, size);
        if (ret < 0)
        {
            if (errno == EINTR)
                continue;
            return false;
        }
        if (!ret)
            return false;
        ptr += ret;
        size -= static_cast<size_t>(ret);
    }
    return true;
}

extern "C" void __libnx_exception_handler(ThreadExceptionDump* ctx)
{
    const uintptr_t pc = static_cast<uintptr_t>(ctx->pc.x);
    const uintptr_t far = static_cast<uintptr_t>(ctx->far.x);
    const uintptr_t start = g_exec_fault_addr;
    const uintptr_t end = start + g_exec_fault_size;
    const bool pc_in_range = start && pc >= start && pc < end;
    const bool far_in_range = start && far >= start && far < end;
    const bool instruction_abort = (ctx->esr & 0xf0000000) == 0x80000000;

    if (g_log)
    {
        fprintf(g_log,
            "[EXC-H] desc=0x%08x esr=0x%08x pc=%p far=%p start=%p size=0x%zx instr=%d pc_in=%d far_in=%d\n",
            ctx->error_desc, ctx->esr, reinterpret_cast<void*>(pc),
            reinterpret_cast<void*>(far), reinterpret_cast<void*>(start),
            g_exec_fault_size, instruction_abort, pc_in_range, far_in_range);
        fflush(g_log);
    }

    if (start && (pc_in_range || far_in_range))
    {
        const Handle self = envGetOwnProcessHandle();
        const size_t size = g_exec_fault_size;
        const uintptr_t backing = g_exec_backing;

        armDCacheFlush(reinterpret_cast<void*>(backing), kPageSize);
        g_exception_probe_rc = svcMapProcessCodeMemory(self, start, backing, size);
        if (R_SUCCEEDED(g_exception_probe_rc))
            g_exception_probe_rc = svcSetProcessMemoryPermission(self, start, size, Perm_Rx);

        if (R_SUCCEEDED(g_exception_probe_rc))
        {
            armICacheInvalidate(reinterpret_cast<void*>(start), kPageSize);
            g_exception_probe_hits++;
            restore_thread_exception_context(ctx);
        }

        if (g_log)
        {
            fprintf(g_log, "[EXC-H] permission switch failed rc=0x%08x\n", g_exception_probe_rc);
            fflush(g_log);
        }
    }
}

bool get_allowed_core_mask(uint64_t* out)
{
    uint64_t mask = 0;
    const Result rc = svcGetInfo(&mask, InfoType_CoreMask, CUR_PROCESS_HANDLE, 0);
    if (R_FAILED(rc) || mask == 0)
    {
        log_line("[CORE] svcGetInfo(InfoType_CoreMask) failed rc=0x%08x, fallback mask=0x7", rc);
        mask = 0x7;
    }
    *out = mask;
    return true;
}

int lowest_set_core(uint64_t mask)
{
    for (int i = 0; i < 32; i++)
    {
        if (mask & (uint64_t{1} << i))
            return i;
    }
    return 0;
}

uint64_t nth_core_mask(uint64_t allowed_mask, unsigned index)
{
    unsigned seen = 0;
    for (unsigned i = 0; i < 32; i++)
    {
        const uint64_t bit = uint64_t{1} << i;
        if ((allowed_mask & bit) == 0)
            continue;
        if (seen == index)
            return bit;
        seen++;
    }
    return uint64_t{1} << lowest_set_core(allowed_mask);
}

Result pin_current_thread(uint64_t mask, int* preferred_out)
{
    if (mask == 0)
        get_allowed_core_mask(&mask);

    const int preferred = lowest_set_core(mask);
    if (preferred_out)
        *preferred_out = preferred;

    return svcSetThreadCoreMask(CUR_THREAD_HANDLE, preferred, static_cast<u32>(mask));
}

struct ThreadProbeArgs
{
    unsigned index;
    uint64_t mask;
    uintptr_t tls_addr;
    uint64_t tls_value;
    int preferred;
    Result pin_rc;
};

void* thread_probe_proc(void* opaque)
{
    auto* args = static_cast<ThreadProbeArgs*>(opaque);
    g_tls_cookie = 0x57494e4500000000ULL + args->index;
    g_tls_counter++;

    args->tls_addr = reinterpret_cast<uintptr_t>(&g_tls_cookie);
    args->tls_value = g_tls_cookie + g_tls_counter;
    args->pin_rc = pin_current_thread(args->mask, &args->preferred);

    log_line("[TLS] thread %u tls=%p value=0x%016llx",
        args->index, reinterpret_cast<void*>(args->tls_addr),
        static_cast<unsigned long long>(args->tls_value));
    log_line("[CORE] thread %u pin preferred=%d mask=0x%llx rc=0x%08x %s",
        args->index, args->preferred,
        static_cast<unsigned long long>(args->mask), args->pin_rc,
        rc_state(args->pin_rc));

    svcSleepThread(10 * 1000 * 1000);
    return nullptr;
}

bool run_tls_and_affinity_probe()
{
    uint64_t allowed_mask = 0;
    get_allowed_core_mask(&allowed_mask);
    log_line("[CORE] allowed mask=0x%llx", static_cast<unsigned long long>(allowed_mask));

    g_tls_cookie = 0x57494e4500001000ULL;
    g_tls_counter++;

    int main_preferred = 0;
    const uint64_t main_mask = nth_core_mask(allowed_mask, 0);
    const Result main_rc = pin_current_thread(main_mask, &main_preferred);

    log_line("[TLS] main tls=%p value=0x%016llx",
        static_cast<void*>(&g_tls_cookie),
        static_cast<unsigned long long>(g_tls_cookie + g_tls_counter));
    log_line("[CORE] main pin preferred=%d mask=0x%llx rc=0x%08x %s",
        main_preferred, static_cast<unsigned long long>(main_mask), main_rc, rc_state(main_rc));

    pthread_t threads[3] = {};
    ThreadProbeArgs args[3] = {};
    bool ok = R_SUCCEEDED(main_rc);

    for (unsigned i = 0; i < 3; i++)
    {
        args[i].index = i + 1;
        args[i].mask = nth_core_mask(allowed_mask, i + 1);

        const int create_rc = pthread_create(&threads[i], nullptr, thread_probe_proc, &args[i]);
        if (create_rc != 0)
        {
            log_line("[THREAD] pthread_create(%u) failed errno=%d", i + 1, create_rc);
            ok = false;
            threads[i] = {};
        }
    }

    for (unsigned i = 0; i < 3; i++)
    {
        if (threads[i])
            pthread_join(threads[i], nullptr);
    }

    for (const ThreadProbeArgs& arg : args)
    {
        ok = ok && arg.tls_addr != 0 && R_SUCCEEDED(arg.pin_rc);
        if (arg.tls_addr == reinterpret_cast<uintptr_t>(&g_tls_cookie))
        {
            log_line("[TLS] thread %u reused main TLS address", arg.index);
            ok = false;
        }
    }

    log_line("[TLS] result=%s", ok ? "OK" : "FAIL");
    return ok;
}

bool run_jit_probe()
{
    Jit jit = {};
    Result rc = jitCreate(&jit, kPageSize);
    log_line("[JIT] jitCreate rc=0x%08x %s", rc, rc_state(rc));
    if (R_FAILED(rc))
        return false;

    rc = jitTransitionToWritable(&jit);
    log_line("[JIT] jitTransitionToWritable rc=0x%08x %s", rc, rc_state(rc));
    if (R_FAILED(rc))
    {
        jitClose(&jit);
        return false;
    }

    auto* rw = static_cast<uint32_t*>(jitGetRwAddr(&jit));
    auto* rx = static_cast<uint32_t*>(jitGetRxAddr(&jit));
    memset(rw, 0, kPageSize);
    rw[0] = 0x52824680; // movz w0, #0x1234
    rw[1] = 0xd65f03c0; // ret

    rc = jitTransitionToExecutable(&jit);
    log_line("[JIT] jitTransitionToExecutable rc=0x%08x %s", rc, rc_state(rc));
    if (R_FAILED(rc))
    {
        jitClose(&jit);
        return false;
    }

    using ProbeFn = int (*)();
    const int value = reinterpret_cast<ProbeFn>(rx)();
    const bool ok = value == 0x1234;
    log_line("[JIT] rw=%p rx=%p call=%d result=%s", static_cast<void*>(rw), static_cast<void*>(rx), value, ok ? "OK" : "FAIL");

    rc = jitClose(&jit);
    log_line("[JIT] jitClose rc=0x%08x %s", rc, rc_state(rc));
    return ok && R_SUCCEEDED(rc);
}

bool blr_vm_syscalls_available()
{
    const bool has = envIsSyscallHinted(0x73) && envIsSyscallHinted(0x74) &&
                     envIsSyscallHinted(0x75) && envIsSyscallHinted(0x77) &&
                     envIsSyscallHinted(0x78);
    log_line("[VM] syscall hints 73=%d 74=%d 75=%d 77=%d 78=%d",
        envIsSyscallHinted(0x73), envIsSyscallHinted(0x74),
        envIsSyscallHinted(0x75), envIsSyscallHinted(0x77),
        envIsSyscallHinted(0x78));
    return has;
}

void log_wineserver_syscall_hints()
{
    log_line("[SERVER] syscall hints 30=%d 31=%d 40=%d 43=%d 45=%d 70=%d 72=%d",
        envIsSyscallHinted(0x30), envIsSyscallHinted(0x31),
        envIsSyscallHinted(0x40), envIsSyscallHinted(0x43),
        envIsSyscallHinted(0x45), envIsSyscallHinted(0x70),
        envIsSyscallHinted(0x72));
}

const char* limitable_resource_name(LimitableResource resource)
{
    switch (resource)
    {
        case LimitableResource_Memory: return "Memory";
        case LimitableResource_Threads: return "Threads";
        case LimitableResource_Events: return "Events";
        case LimitableResource_TransferMemories: return "TransferMemories";
        case LimitableResource_Sessions: return "Sessions";
        default: return "unknown";
    }
}

void run_horizon_server_resource_limit_probe()
{
    u64 resource_limit = 0;
    Result rc = svcGetInfo(&resource_limit, InfoType_ResourceLimit, INVALID_HANDLE, 0);
    log_line("[SERVER] resource_limit handle=0x%lx rc=0x%08x %s",
        resource_limit, rc, rc_state(rc));
    if (R_FAILED(rc))
    {
        log_result_decode("[SERVER] resource_limit", rc);
        return;
    }

    const auto resource_limit_handle = static_cast<Handle>(resource_limit);
    const LimitableResource resources[] = {
        LimitableResource_Memory,
        LimitableResource_Threads,
        LimitableResource_Events,
        LimitableResource_TransferMemories,
        LimitableResource_Sessions,
    };

    for (LimitableResource resource : resources)
    {
        s64 limit = -1;
        s64 current = -1;
        const Result limit_rc = svcGetResourceLimitLimitValue(&limit, resource_limit_handle, resource);
        const Result current_rc = svcGetResourceLimitCurrentValue(&current, resource_limit_handle, resource);
        log_line("[SERVER] resource %-18s limit=%lld current=%lld limit_rc=0x%08x %s current_rc=0x%08x %s",
            limitable_resource_name(resource), static_cast<long long>(limit),
            static_cast<long long>(current), limit_rc, rc_state(limit_rc),
            current_rc, rc_state(current_rc));
        if (R_FAILED(limit_rc))
            log_result_decode("[SERVER] resource limit value", limit_rc);
        if (R_FAILED(current_rc))
            log_result_decode("[SERVER] resource current value", current_rc);
    }
}

struct UserEventProbeContext
{
    UEvent request_event;
    UEvent reply_event;
    Result wait_rc;
    uint32_t observed_value;
};

void user_event_probe_thread(void* arg)
{
    auto* ctx = static_cast<UserEventProbeContext*>(arg);
    ctx->wait_rc = waitSingle(waiterForUEvent(&ctx->request_event), 2000000000ULL);
    if (R_SUCCEEDED(ctx->wait_rc))
    {
        ctx->observed_value = kServerProbeValue;
        ueventClear(&ctx->request_event);
        ueventSignal(&ctx->reply_event);
    }
}

bool run_horizon_server_user_event_probe()
{
    Thread thread = {};
    UserEventProbeContext ctx = {};
    ctx.wait_rc = MAKERESULT(Module_Libnx, LibnxError_BadInput);

    ueventCreate(&ctx.request_event, false);
    ueventCreate(&ctx.reply_event, false);

    Result rc = threadCreate(&thread, user_event_probe_thread, &ctx, nullptr, 0x4000, 0x3b, -2);
    log_line("[SERVER] uevent threadCreate rc=0x%08x %s", rc, rc_state(rc));
    if (R_FAILED(rc))
        return false;

    rc = threadStart(&thread);
    log_line("[SERVER] uevent threadStart rc=0x%08x %s", rc, rc_state(rc));
    if (R_FAILED(rc))
    {
        threadClose(&thread);
        return false;
    }

    ueventSignal(&ctx.request_event);
    const Result reply_rc = waitSingle(waiterForUEvent(&ctx.reply_event), 2000000000ULL);
    const Result wait_rc = threadWaitForExit(&thread);
    threadClose(&thread);

    const bool ok = R_SUCCEEDED(reply_rc) && R_SUCCEEDED(wait_rc) &&
        R_SUCCEEDED(ctx.wait_rc) && ctx.observed_value == kServerProbeValue;
    log_line("[SERVER] uevent reply_rc=0x%08x %s thread_wait_rc=0x%08x %s observed=0x%08x result=%s",
        reply_rc, rc_state(reply_rc), wait_rc, rc_state(wait_rc),
        ctx.observed_value, ok ? "OK" : "FAIL");
    return ok;
}

bool run_horizon_server_pipe_probe()
{
    int fds[2] = {-1, -1};
    uint32_t out_value = kServerProbeValue;
    uint32_t in_value = 0;

    int ret = horizon_pipe(fds);
    log_line("[SERVER] horizon_pipe read_fd=%d write_fd=%d ret=%d errno=%d",
        fds[0], fds[1], ret, ret ? errno : 0);
    if (ret)
        return false;

    ssize_t written = write(fds[1], &out_value, sizeof(out_value));
    ssize_t read_count = read(fds[0], &in_value, sizeof(in_value));
    close(fds[1]);
    close(fds[0]);

    const bool ok = written == static_cast<ssize_t>(sizeof(out_value)) &&
        read_count == static_cast<ssize_t>(sizeof(in_value)) &&
        in_value == out_value;
    log_line("[SERVER] horizon_pipe written=%zd read=%zd value=0x%08x result=%s",
        written, read_count, in_value, ok ? "OK" : "FAIL");
    return ok;
}

bool run_horizon_server_fd_passing_probe()
{
    int server_to_client_fds[2] = {-1, -1};
    int client_to_server_fds[2] = {-1, -1};
    unsigned int handle = 0;
    unsigned int client_handle = 0;
    uint32_t server_out_value = kServerProbeReplyValue;
    uint32_t client_out_value = kServerProbeValue;
    uint32_t server_in_value = 0;
    uint32_t client_in_value = 0;

    int ret = horizon_pipe(server_to_client_fds);
    log_line("[SERVER] fdpass s2c pipe read_fd=%d write_fd=%d ret=%d errno=%d",
        server_to_client_fds[0], server_to_client_fds[1], ret, ret ? errno : 0);
    if (ret)
        return false;

    horizon_server_queue_fd(server_to_client_fds[0], kServerProbeRequestId);
    int received_fd = horizon_server_receive_fd(&handle);
    ssize_t written = write(server_to_client_fds[1], &server_out_value, sizeof(server_out_value));
    ssize_t read_count = read(received_fd, &server_in_value, sizeof(server_in_value));
    close(server_to_client_fds[1]);
    close(received_fd);
    if (received_fd != server_to_client_fds[0])
        close(server_to_client_fds[0]);

    const bool server_to_client_ok = received_fd >= 0 &&
        handle == kServerProbeRequestId &&
        written == static_cast<ssize_t>(sizeof(server_out_value)) &&
        read_count == static_cast<ssize_t>(sizeof(server_in_value)) &&
        server_in_value == server_out_value;
    log_line("[SERVER] fdpass s2c received_fd=%d handle=0x%08x written=%zd read=%zd value=0x%08x result=%s",
        received_fd, handle, written, read_count, server_in_value,
        server_to_client_ok ? "OK" : "FAIL");

    ret = horizon_pipe(client_to_server_fds);
    log_line("[SERVER] fdpass c2s pipe read_fd=%d write_fd=%d ret=%d errno=%d",
        client_to_server_fds[0], client_to_server_fds[1], ret, ret ? errno : 0);
    if (ret)
        return false;

    horizon_server_send_fd(client_to_server_fds[1]);
    int server_fd = horizon_server_take_client_fd(&client_handle);
    written = write(server_fd, &client_out_value, sizeof(client_out_value));
    read_count = read(client_to_server_fds[0], &client_in_value, sizeof(client_in_value));
    close(server_fd);
    close(client_to_server_fds[0]);
    if (server_fd != client_to_server_fds[1])
        close(client_to_server_fds[1]);

    const bool client_to_server_ok = server_fd >= 0 &&
        client_handle == 0 &&
        written == static_cast<ssize_t>(sizeof(client_out_value)) &&
        read_count == static_cast<ssize_t>(sizeof(client_in_value)) &&
        client_in_value == client_out_value;
    log_line("[SERVER] fdpass c2s server_fd=%d handle=0x%08x written=%zd read=%zd value=0x%08x result=%s",
        server_fd, client_handle, written, read_count, client_in_value,
        client_to_server_ok ? "OK" : "FAIL");

    const bool ok = server_to_client_ok && client_to_server_ok;
    log_line("[SERVER] fdpass result=%s", ok ? "OK" : "FAIL");
    return ok;
}

bool run_horizon_server_connect_probe()
{
    unsigned int version = 0;
    const int control_fd = horizon_server_connect();
    const int request_fd = horizon_server_receive_fd(&version);
    const unsigned int expected = horizon_server_protocol_version();

    const bool ok = control_fd >= 0 && request_fd >= 0 && version == expected;
    log_line("[SERVER] connect control_fd=%d request_fd=%d version=%u expected=%u result=%s",
        control_fd, request_fd, version, expected, ok ? "OK" : "FAIL");
    if (request_fd != -1)
        close(request_fd);
    if (control_fd != -1)
        close(control_fd);
    return ok;
}

bool run_horizon_server_request_reply_probe()
{
    int reply_pipe[2] = {-1, -1};
    int wait_pipe[2] = {-1, -1};
    int thread_reply_pipe[2] = {-1, -1};
    int thread_wait_pipe[2] = {-1, -1};
    unsigned int version = 0;
    const int control_fd = horizon_server_connect();
    const int request_fd = horizon_server_receive_fd(&version);

    const int reply_ret = horizon_pipe(reply_pipe);
    const int wait_ret = horizon_pipe(wait_pipe);
    if (reply_ret || wait_ret)
    {
        log_line("[SERVER] reqreply pipe setup reply_ret=%d wait_ret=%d errno=%d result=FAIL",
            reply_ret, wait_ret, errno);
        return false;
    }

    horizon_server_send_fd(reply_pipe[1]);
    horizon_server_send_fd(wait_pipe[1]);

    uint8_t request_message[kWineServerFixedMessageSize] = {};
    auto* request = reinterpret_cast<HorizonInitFirstThreadRequest*>(request_message);
    request->header.req = kReqInitFirstThread;
    request->header.reply_size = sizeof(kImageFileMachineArm64);
    request->unix_pid = kProbeProcessId;
    request->unix_tid = kProbeThreadId;
    request->debug_level = 1;
    request->reply_fd = reply_pipe[1];
    request->wait_fd = wait_pipe[1];

    const bool wrote_request = control_fd >= 0 && request_fd >= 0 &&
        version == horizon_server_protocol_version() &&
        write_exact(request_fd, request_message, sizeof(request_message));

    uint8_t reply_message[kWineServerFixedMessageSize] = {};
    HorizonInitFirstThreadReply client_reply = {};
    uint16_t client_machine = 0;
    const bool read_reply = wrote_request &&
        read_exact(reply_pipe[0], reply_message, sizeof(reply_message)) &&
        read_exact(reply_pipe[0], &client_machine, sizeof(client_machine));
    memcpy(&client_reply, reply_message, sizeof(client_reply));

    const bool reply_ok = read_reply &&
        client_reply.header.error == 0 &&
        client_reply.header.reply_size == sizeof(client_machine) &&
        client_reply.pid == kProbeProcessId &&
        client_reply.tid == kProbeThreadId &&
        client_reply.session_id == 1 &&
        client_reply.inproc_device == 0 &&
        client_reply.info_size == 0 &&
        client_machine == kImageFileMachineArm64;

    uint8_t done_request[kWineServerFixedMessageSize] = {};
    auto* done_header = reinterpret_cast<HorizonRequestHeader*>(done_request);
    done_header->req = kReqInitProcessDone;
    const bool wrote_done = reply_ok && write_exact(request_fd, done_request, sizeof(done_request));

    uint8_t done_reply_message[kWineServerFixedMessageSize] = {};
    HorizonInitProcessDoneReply done_reply = {};
    const bool read_done = wrote_done && read_exact(reply_pipe[0], done_reply_message, sizeof(done_reply_message));
    memcpy(&done_reply, done_reply_message, sizeof(done_reply));
    const bool done_ok = read_done &&
        done_reply.header.error == 0 &&
        done_reply.header.reply_size == 0 &&
        done_reply.suspend == 0;

    const int thread_reply_ret = horizon_pipe(thread_reply_pipe);
    const int thread_wait_ret = horizon_pipe(thread_wait_pipe);
    bool thread_ok = false;
    bool objects_ok = false;
    bool select_ok = false;
    bool sync_ok = false;
    if (!thread_reply_ret && !thread_wait_ret)
    {
        horizon_server_send_fd(thread_reply_pipe[1]);
        horizon_server_send_fd(thread_wait_pipe[1]);

        uint8_t thread_request[kWineServerFixedMessageSize] = {};
        auto* init_thread = reinterpret_cast<HorizonInitThreadRequest*>(thread_request);
        init_thread->header.req = kReqInitThread;
        init_thread->unix_tid = kProbeSecondThreadId;
        init_thread->reply_fd = thread_reply_pipe[1];
        init_thread->wait_fd = thread_wait_pipe[1];
        init_thread->teb = 0x12340000;
        init_thread->entry = 0x56780000;

        const bool wrote_thread = done_ok && write_exact(request_fd, thread_request, sizeof(thread_request));
        uint8_t thread_reply_message[kWineServerFixedMessageSize] = {};
        HorizonInitThreadReply thread_reply = {};
        const bool read_thread = wrote_thread &&
            read_exact(thread_reply_pipe[0], thread_reply_message, sizeof(thread_reply_message));
        memcpy(&thread_reply, thread_reply_message, sizeof(thread_reply));
        thread_ok = read_thread &&
            thread_reply.header.error == 0 &&
            thread_reply.header.reply_size == 0 &&
            thread_reply.suspend == 0;

        auto fixed_call = [&](const uint8_t* call, const void* data, size_t data_size,
                              void* out, size_t out_size) {
            uint8_t out_message[kWineServerFixedMessageSize] = {};
            if (!write_exact(request_fd, call, kWineServerFixedMessageSize))
                return false;
            if (data_size && !write_exact(request_fd, data, data_size))
                return false;
            if (!read_exact(thread_reply_pipe[0], out_message, sizeof(out_message)))
                return false;
            memcpy(out, out_message, out_size);
            return true;
        };

        uint8_t alloc_message[kWineServerFixedMessageSize] = {};
        auto* alloc_req = reinterpret_cast<HorizonAllocateReserveObjectRequest*>(alloc_message);
        alloc_req->header.req = kReqAllocateReserveObject;
        alloc_req->type = 1;
        HorizonAllocateReserveObjectReply alloc_reply = {};
        const bool alloc_ok = thread_ok &&
            fixed_call(alloc_message, nullptr, 0, &alloc_reply, sizeof(alloc_reply)) &&
            alloc_reply.header.error == kStatusSuccess &&
            alloc_reply.header.reply_size == 0 &&
            alloc_reply.handle != 0;

        uint8_t dup_message[kWineServerFixedMessageSize] = {};
        auto* dup_req = reinterpret_cast<HorizonDupHandleRequest*>(dup_message);
        dup_req->header.req = kReqDupHandle;
        dup_req->src_process = 0xffffffffu;
        dup_req->src_handle = alloc_reply.handle;
        dup_req->dst_process = 0xffffffffu;
        dup_req->access = 0x001f0003;
        HorizonDupHandleReply dup_reply = {};
        const bool dup_ok = alloc_ok &&
            fixed_call(dup_message, nullptr, 0, &dup_reply, sizeof(dup_reply)) &&
            dup_reply.header.error == kStatusSuccess &&
            dup_reply.header.reply_size == 0 &&
            dup_reply.handle != 0 &&
            dup_reply.handle != alloc_reply.handle;

        uint8_t compare_same_message[kWineServerFixedMessageSize] = {};
        auto* compare_same_req = reinterpret_cast<HorizonCompareObjectsRequest*>(compare_same_message);
        compare_same_req->header.req = kReqCompareObjects;
        compare_same_req->first = alloc_reply.handle;
        compare_same_req->second = alloc_reply.handle;
        HorizonReplyHeader compare_same_reply = {};
        const bool compare_same_ok = dup_ok &&
            fixed_call(compare_same_message, nullptr, 0, &compare_same_reply, sizeof(compare_same_reply)) &&
            compare_same_reply.error == kStatusSuccess &&
            compare_same_reply.reply_size == 0;

        uint8_t compare_dup_message[kWineServerFixedMessageSize] = {};
        auto* compare_dup_req = reinterpret_cast<HorizonCompareObjectsRequest*>(compare_dup_message);
        compare_dup_req->header.req = kReqCompareObjects;
        compare_dup_req->first = alloc_reply.handle;
        compare_dup_req->second = dup_reply.handle;
        HorizonReplyHeader compare_dup_reply = {};
        const bool compare_dup_ok = compare_same_ok &&
            fixed_call(compare_dup_message, nullptr, 0, &compare_dup_reply, sizeof(compare_dup_reply)) &&
            compare_dup_reply.error == kStatusSuccess &&
            compare_dup_reply.reply_size == 0;

        uint8_t alloc_other_message[kWineServerFixedMessageSize] = {};
        auto* alloc_other_req = reinterpret_cast<HorizonAllocateReserveObjectRequest*>(alloc_other_message);
        alloc_other_req->header.req = kReqAllocateReserveObject;
        alloc_other_req->type = 2;
        HorizonAllocateReserveObjectReply alloc_other_reply = {};
        const bool alloc_other_ok = compare_dup_ok &&
            fixed_call(alloc_other_message, nullptr, 0, &alloc_other_reply, sizeof(alloc_other_reply)) &&
            alloc_other_reply.header.error == kStatusSuccess &&
            alloc_other_reply.handle != 0 &&
            alloc_other_reply.handle != alloc_reply.handle;

        uint8_t compare_diff_message[kWineServerFixedMessageSize] = {};
        auto* compare_diff_req = reinterpret_cast<HorizonCompareObjectsRequest*>(compare_diff_message);
        compare_diff_req->header.req = kReqCompareObjects;
        compare_diff_req->first = alloc_reply.handle;
        compare_diff_req->second = alloc_other_reply.handle;
        HorizonReplyHeader compare_diff_reply = {};
        const bool compare_diff_ok = alloc_other_ok &&
            fixed_call(compare_diff_message, nullptr, 0, &compare_diff_reply, sizeof(compare_diff_reply)) &&
            compare_diff_reply.error == kStatusNotSameObject &&
            compare_diff_reply.reply_size == 0;

        struct SelectOpPayload
        {
            int op;
            uint32_t handle;
        };
        SelectOpPayload select_payload = {1, alloc_reply.handle};
        uint8_t select_message[kWineServerFixedMessageSize] = {};
        auto* select_req = reinterpret_cast<HorizonSelectRequest*>(select_message);
        select_req->header.req = kReqSelect;
        select_req->header.request_size = sizeof(select_payload);
        select_req->size = sizeof(select_payload);
        HorizonSelectReply select_reply = {};
        const bool select_signal_ok = compare_diff_ok &&
            fixed_call(select_message, &select_payload, sizeof(select_payload),
                &select_reply, sizeof(select_reply)) &&
            select_reply.header.error == kStatusSuccess &&
            select_reply.header.reply_size == 0 &&
            select_reply.signaled == 1;

        struct
        {
            uint8_t apc_result[kWineApcResultSize];
            SelectOpPayload select;
        } real_select_payload = {};
        real_select_payload.select = {1, alloc_reply.handle};
        uint8_t real_select_message[kWineServerFixedMessageSize] = {};
        auto* real_select_req = reinterpret_cast<HorizonSelectRequest*>(real_select_message);
        real_select_req->header.req = kReqSelect;
        real_select_req->header.request_size = sizeof(real_select_payload);
        real_select_req->size = sizeof(real_select_payload.select);
        HorizonSelectReply real_select_reply = {};
        const bool real_select_signal_ok = select_signal_ok &&
            fixed_call(real_select_message, &real_select_payload, sizeof(real_select_payload),
                &real_select_reply, sizeof(real_select_reply)) &&
            real_select_reply.header.error == kStatusSuccess &&
            real_select_reply.header.reply_size == 0 &&
            real_select_reply.signaled == 1;

        uint8_t select_timeout_message[kWineServerFixedMessageSize] = {};
        auto* select_timeout_req = reinterpret_cast<HorizonSelectRequest*>(select_timeout_message);
        select_timeout_req->header.req = kReqSelect;
        select_timeout_req->timeout = 0;
        HorizonSelectReply select_timeout_reply = {};
        const bool select_timeout_ok = real_select_signal_ok &&
            fixed_call(select_timeout_message, nullptr, 0,
                &select_timeout_reply, sizeof(select_timeout_reply)) &&
            select_timeout_reply.header.error == kStatusTimeout &&
            select_timeout_reply.header.reply_size == 0 &&
            select_timeout_reply.signaled == 1;

        uint8_t create_event_message[kWineServerFixedMessageSize] = {};
        auto* create_event_req = reinterpret_cast<HorizonCreateEventRequest*>(create_event_message);
        create_event_req->header.req = kReqCreateEvent;
        create_event_req->access = 0x001f0003;
        create_event_req->manual_reset = 0;
        create_event_req->initial_state = 0;
        HorizonCreateEventReply create_event_reply = {};
        const bool create_event_ok = select_timeout_ok &&
            fixed_call(create_event_message, nullptr, 0, &create_event_reply, sizeof(create_event_reply)) &&
            create_event_reply.header.error == kStatusSuccess &&
            create_event_reply.handle != 0;

        uint8_t query_event_message[kWineServerFixedMessageSize] = {};
        auto* query_event_req = reinterpret_cast<HorizonQueryEventRequest*>(query_event_message);
        query_event_req->header.req = kReqQueryEvent;
        query_event_req->handle = create_event_reply.handle;
        HorizonQueryEventReply query_event_reply = {};
        const bool query_event_initial_ok = create_event_ok &&
            fixed_call(query_event_message, nullptr, 0, &query_event_reply, sizeof(query_event_reply)) &&
            query_event_reply.header.error == kStatusSuccess &&
            query_event_reply.manual_reset == 0 &&
            query_event_reply.state == 0;

        uint8_t set_event_message[kWineServerFixedMessageSize] = {};
        auto* set_event_req = reinterpret_cast<HorizonEventOpRequest*>(set_event_message);
        set_event_req->header.req = kReqEventOp;
        set_event_req->handle = create_event_reply.handle;
        set_event_req->op = kEventSet;
        HorizonEventOpReply set_event_reply = {};
        const bool set_event_ok = query_event_initial_ok &&
            fixed_call(set_event_message, nullptr, 0, &set_event_reply, sizeof(set_event_reply)) &&
            set_event_reply.header.error == kStatusSuccess &&
            set_event_reply.state == 0;

        HorizonQueryEventReply query_event_set_reply = {};
        const bool query_event_set_ok = set_event_ok &&
            fixed_call(query_event_message, nullptr, 0, &query_event_set_reply, sizeof(query_event_set_reply)) &&
            query_event_set_reply.header.error == kStatusSuccess &&
            query_event_set_reply.state == 1;

        uint8_t reset_event_message[kWineServerFixedMessageSize] = {};
        auto* reset_event_req = reinterpret_cast<HorizonEventOpRequest*>(reset_event_message);
        reset_event_req->header.req = kReqEventOp;
        reset_event_req->handle = create_event_reply.handle;
        reset_event_req->op = kEventReset;
        HorizonEventOpReply reset_event_reply = {};
        const bool reset_event_ok = query_event_set_ok &&
            fixed_call(reset_event_message, nullptr, 0, &reset_event_reply, sizeof(reset_event_reply)) &&
            reset_event_reply.header.error == kStatusSuccess &&
            reset_event_reply.state == 1;

        constexpr uint16_t named_event_name[] = {
            'W', 'i', 'n', 'e', 'N', 'X', 'N', 'a', 'm', 'e', 'd', 'E', 'v'
        };
        struct
        {
            HorizonObjectAttributes attr;
            uint16_t name[sizeof(named_event_name) / sizeof(named_event_name[0])];
        } named_event_payload = {};
        named_event_payload.attr.name_len = sizeof(named_event_name);
        memcpy(named_event_payload.name, named_event_name, sizeof(named_event_name));

        uint8_t create_named_event_message[kWineServerFixedMessageSize] = {};
        auto* create_named_event_req = reinterpret_cast<HorizonCreateEventRequest*>(create_named_event_message);
        create_named_event_req->header.req = kReqCreateEvent;
        create_named_event_req->header.request_size = sizeof(named_event_payload);
        create_named_event_req->access = 0x001f0003;
        create_named_event_req->manual_reset = 1;
        create_named_event_req->initial_state = 1;
        HorizonCreateEventReply create_named_event_reply = {};
        const bool create_named_event_ok = reset_event_ok &&
            fixed_call(create_named_event_message, &named_event_payload, sizeof(named_event_payload),
                &create_named_event_reply, sizeof(create_named_event_reply)) &&
            create_named_event_reply.header.error == kStatusSuccess &&
            create_named_event_reply.handle != 0;

        uint8_t create_named_event_again_message[kWineServerFixedMessageSize] = {};
        auto* create_named_event_again_req =
            reinterpret_cast<HorizonCreateEventRequest*>(create_named_event_again_message);
        create_named_event_again_req->header.req = kReqCreateEvent;
        create_named_event_again_req->header.request_size = sizeof(named_event_payload);
        create_named_event_again_req->access = 0x001f0003;
        create_named_event_again_req->manual_reset = 0;
        create_named_event_again_req->initial_state = 0;
        HorizonCreateEventReply create_named_event_again_reply = {};
        const bool create_named_event_again_ok = create_named_event_ok &&
            fixed_call(create_named_event_again_message, &named_event_payload, sizeof(named_event_payload),
                &create_named_event_again_reply, sizeof(create_named_event_again_reply)) &&
            create_named_event_again_reply.header.error == kStatusObjectNameExists &&
            create_named_event_again_reply.handle != 0 &&
            create_named_event_again_reply.handle != create_named_event_reply.handle;

        uint8_t open_named_event_message[kWineServerFixedMessageSize] = {};
        auto* open_named_event_req = reinterpret_cast<HorizonOpenNamedObjectRequest*>(open_named_event_message);
        open_named_event_req->header.req = kReqOpenEvent;
        open_named_event_req->header.request_size = sizeof(named_event_name);
        open_named_event_req->access = 0x001f0003;
        HorizonCreateEventReply open_named_event_reply = {};
        const bool open_named_event_ok = create_named_event_again_ok &&
            fixed_call(open_named_event_message, named_event_name, sizeof(named_event_name),
                &open_named_event_reply, sizeof(open_named_event_reply)) &&
            open_named_event_reply.header.error == kStatusSuccess &&
            open_named_event_reply.handle != 0;

        uint8_t query_named_event_message[kWineServerFixedMessageSize] = {};
        auto* query_named_event_req = reinterpret_cast<HorizonQueryEventRequest*>(query_named_event_message);
        query_named_event_req->header.req = kReqQueryEvent;
        query_named_event_req->handle = open_named_event_reply.handle;
        HorizonQueryEventReply query_named_event_reply = {};
        const bool query_named_event_ok = open_named_event_ok &&
            fixed_call(query_named_event_message, nullptr, 0,
                &query_named_event_reply, sizeof(query_named_event_reply)) &&
            query_named_event_reply.header.error == kStatusSuccess &&
            query_named_event_reply.manual_reset == 1 &&
            query_named_event_reply.state == 1;

        constexpr uint16_t missing_event_name[] = {'M', 'i', 's', 's', 'i', 'n', 'g'};
        uint8_t open_missing_event_message[kWineServerFixedMessageSize] = {};
        auto* open_missing_event_req = reinterpret_cast<HorizonOpenNamedObjectRequest*>(open_missing_event_message);
        open_missing_event_req->header.req = kReqOpenEvent;
        open_missing_event_req->header.request_size = sizeof(missing_event_name);
        open_missing_event_req->access = 0x001f0003;
        HorizonCreateEventReply open_missing_event_reply = {};
        const bool open_missing_event_ok = query_named_event_ok &&
            fixed_call(open_missing_event_message, missing_event_name, sizeof(missing_event_name),
                &open_missing_event_reply, sizeof(open_missing_event_reply)) &&
            open_missing_event_reply.header.error == kStatusObjectNameNotFound &&
            open_missing_event_reply.handle == 0;

        uint8_t create_mutex_message[kWineServerFixedMessageSize] = {};
        auto* create_mutex_req = reinterpret_cast<HorizonCreateMutexRequest*>(create_mutex_message);
        create_mutex_req->header.req = kReqCreateMutex;
        create_mutex_req->access = 0x001f0001;
        create_mutex_req->owned = 1;
        HorizonCreateMutexReply create_mutex_reply = {};
        const bool create_mutex_ok = open_missing_event_ok &&
            fixed_call(create_mutex_message, nullptr, 0, &create_mutex_reply, sizeof(create_mutex_reply)) &&
            create_mutex_reply.header.error == kStatusSuccess &&
            create_mutex_reply.handle != 0;

        uint8_t query_mutex_message[kWineServerFixedMessageSize] = {};
        auto* query_mutex_req = reinterpret_cast<HorizonQueryMutexRequest*>(query_mutex_message);
        query_mutex_req->header.req = kReqQueryMutex;
        query_mutex_req->handle = create_mutex_reply.handle;
        HorizonQueryMutexReply query_mutex_owned_reply = {};
        const bool query_mutex_owned_ok = create_mutex_ok &&
            fixed_call(query_mutex_message, nullptr, 0, &query_mutex_owned_reply, sizeof(query_mutex_owned_reply)) &&
            query_mutex_owned_reply.header.error == kStatusSuccess &&
            query_mutex_owned_reply.count == 1 &&
            query_mutex_owned_reply.owned == 1 &&
            query_mutex_owned_reply.abandoned == 0;

        uint8_t release_mutex_message[kWineServerFixedMessageSize] = {};
        auto* release_mutex_req = reinterpret_cast<HorizonReleaseMutexRequest*>(release_mutex_message);
        release_mutex_req->header.req = kReqReleaseMutex;
        release_mutex_req->handle = create_mutex_reply.handle;
        HorizonReleaseMutexReply release_mutex_reply = {};
        const bool release_mutex_ok = query_mutex_owned_ok &&
            fixed_call(release_mutex_message, nullptr, 0, &release_mutex_reply, sizeof(release_mutex_reply)) &&
            release_mutex_reply.header.error == kStatusSuccess &&
            release_mutex_reply.prev_count == 1;

        HorizonQueryMutexReply query_mutex_free_reply = {};
        const bool query_mutex_free_ok = release_mutex_ok &&
            fixed_call(query_mutex_message, nullptr, 0, &query_mutex_free_reply, sizeof(query_mutex_free_reply)) &&
            query_mutex_free_reply.header.error == kStatusSuccess &&
            query_mutex_free_reply.count == 0 &&
            query_mutex_free_reply.owned == 0;

        uint8_t create_semaphore_message[kWineServerFixedMessageSize] = {};
        auto* create_semaphore_req = reinterpret_cast<HorizonCreateSemaphoreRequest*>(create_semaphore_message);
        create_semaphore_req->header.req = kReqCreateSemaphore;
        create_semaphore_req->access = 0x001f0003;
        create_semaphore_req->initial = 1;
        create_semaphore_req->max = 3;
        HorizonCreateSemaphoreReply create_semaphore_reply = {};
        const bool create_semaphore_ok = query_mutex_free_ok &&
            fixed_call(create_semaphore_message, nullptr, 0,
                &create_semaphore_reply, sizeof(create_semaphore_reply)) &&
            create_semaphore_reply.header.error == kStatusSuccess &&
            create_semaphore_reply.handle != 0;

        uint8_t query_semaphore_message[kWineServerFixedMessageSize] = {};
        auto* query_semaphore_req = reinterpret_cast<HorizonQuerySemaphoreRequest*>(query_semaphore_message);
        query_semaphore_req->header.req = kReqQuerySemaphore;
        query_semaphore_req->handle = create_semaphore_reply.handle;
        HorizonQuerySemaphoreReply query_semaphore_reply = {};
        const bool query_semaphore_ok = create_semaphore_ok &&
            fixed_call(query_semaphore_message, nullptr, 0,
                &query_semaphore_reply, sizeof(query_semaphore_reply)) &&
            query_semaphore_reply.header.error == kStatusSuccess &&
            query_semaphore_reply.current == 1 &&
            query_semaphore_reply.max == 3;

        uint8_t release_semaphore_message[kWineServerFixedMessageSize] = {};
        auto* release_semaphore_req = reinterpret_cast<HorizonReleaseSemaphoreRequest*>(release_semaphore_message);
        release_semaphore_req->header.req = kReqReleaseSemaphore;
        release_semaphore_req->handle = create_semaphore_reply.handle;
        release_semaphore_req->count = 2;
        HorizonReleaseSemaphoreReply release_semaphore_reply = {};
        const bool release_semaphore_ok = query_semaphore_ok &&
            fixed_call(release_semaphore_message, nullptr, 0,
                &release_semaphore_reply, sizeof(release_semaphore_reply)) &&
            release_semaphore_reply.header.error == kStatusSuccess &&
            release_semaphore_reply.prev_count == 1;

        HorizonReleaseSemaphoreReply release_semaphore_limit_reply = {};
        const bool release_semaphore_limit_ok = release_semaphore_ok &&
            fixed_call(release_semaphore_message, nullptr, 0,
                &release_semaphore_limit_reply, sizeof(release_semaphore_limit_reply)) &&
            release_semaphore_limit_reply.header.error == kStatusSemaphoreLimitExceeded;

        uint8_t create_timer_message[kWineServerFixedMessageSize] = {};
        auto* create_timer_req = reinterpret_cast<HorizonCreateTimerRequest*>(create_timer_message);
        create_timer_req->header.req = kReqCreateTimer;
        create_timer_req->access = 0x001f0003;
        create_timer_req->manual = 0;
        HorizonCreateTimerReply create_timer_reply = {};
        const bool create_timer_ok = release_semaphore_limit_ok &&
            fixed_call(create_timer_message, nullptr, 0, &create_timer_reply, sizeof(create_timer_reply)) &&
            create_timer_reply.header.error == kStatusSuccess &&
            create_timer_reply.handle != 0;

        uint8_t set_timer_message[kWineServerFixedMessageSize] = {};
        auto* set_timer_req = reinterpret_cast<HorizonSetTimerRequest*>(set_timer_message);
        set_timer_req->header.req = kReqSetTimer;
        set_timer_req->handle = create_timer_reply.handle;
        set_timer_req->expire = -10000;
        set_timer_req->period = 0;
        HorizonSetTimerReply set_timer_reply = {};
        const bool set_timer_ok = create_timer_ok &&
            fixed_call(set_timer_message, nullptr, 0, &set_timer_reply, sizeof(set_timer_reply)) &&
            set_timer_reply.header.error == kStatusSuccess &&
            set_timer_reply.signaled == 0;

        uint8_t query_timer_message[kWineServerFixedMessageSize] = {};
        auto* query_timer_req = reinterpret_cast<HorizonGetTimerInfoRequest*>(query_timer_message);
        query_timer_req->header.req = kReqGetTimerInfo;
        query_timer_req->handle = create_timer_reply.handle;
        HorizonGetTimerInfoReply query_timer_reply = {};
        const bool query_timer_ok = set_timer_ok &&
            fixed_call(query_timer_message, nullptr, 0, &query_timer_reply, sizeof(query_timer_reply)) &&
            query_timer_reply.header.error == kStatusSuccess &&
            query_timer_reply.when == set_timer_req->expire &&
            query_timer_reply.signaled == 1;

        SelectOpPayload timer_select_payload = {1, create_timer_reply.handle};
        uint8_t timer_select_message[kWineServerFixedMessageSize] = {};
        auto* timer_select_req = reinterpret_cast<HorizonSelectRequest*>(timer_select_message);
        timer_select_req->header.req = kReqSelect;
        timer_select_req->header.request_size = sizeof(timer_select_payload);
        timer_select_req->size = sizeof(timer_select_payload);
        HorizonSelectReply timer_select_reply = {};
        const bool timer_select_ok = query_timer_ok &&
            fixed_call(timer_select_message, &timer_select_payload, sizeof(timer_select_payload),
                &timer_select_reply, sizeof(timer_select_reply)) &&
            timer_select_reply.header.error == kStatusSuccess &&
            timer_select_reply.signaled == 1;

        HorizonGetTimerInfoReply query_timer_consumed_reply = {};
        const bool query_timer_consumed_ok = timer_select_ok &&
            fixed_call(query_timer_message, nullptr, 0,
                &query_timer_consumed_reply, sizeof(query_timer_consumed_reply)) &&
            query_timer_consumed_reply.header.error == kStatusSuccess &&
            query_timer_consumed_reply.signaled == 0;

        uint8_t cancel_timer_message[kWineServerFixedMessageSize] = {};
        auto* cancel_timer_req = reinterpret_cast<HorizonCancelTimerRequest*>(cancel_timer_message);
        cancel_timer_req->header.req = kReqCancelTimer;
        cancel_timer_req->handle = create_timer_reply.handle;
        HorizonCancelTimerReply cancel_timer_reply = {};
        const bool cancel_timer_ok = query_timer_consumed_ok &&
            fixed_call(cancel_timer_message, nullptr, 0,
                &cancel_timer_reply, sizeof(cancel_timer_reply)) &&
            cancel_timer_reply.header.error == kStatusSuccess &&
            cancel_timer_reply.signaled == 0;

        const bool sync_state_ok = create_event_ok && query_event_initial_ok && set_event_ok &&
            query_event_set_ok && reset_event_ok && create_named_event_ok &&
            create_named_event_again_ok && open_named_event_ok && query_named_event_ok &&
            open_missing_event_ok && create_mutex_ok && query_mutex_owned_ok &&
            release_mutex_ok && query_mutex_free_ok && create_semaphore_ok &&
            query_semaphore_ok && release_semaphore_ok && release_semaphore_limit_ok &&
            create_timer_ok && set_timer_ok && query_timer_ok && timer_select_ok &&
            query_timer_consumed_ok && cancel_timer_ok;

        uint8_t close_timer_message[kWineServerFixedMessageSize] = {};
        auto* close_timer_req = reinterpret_cast<HorizonCloseHandleRequest*>(close_timer_message);
        close_timer_req->header.req = kReqCloseHandle;
        close_timer_req->handle = create_timer_reply.handle;
        HorizonReplyHeader close_timer_reply = {};
        const bool close_timer_ok = sync_state_ok &&
            fixed_call(close_timer_message, nullptr, 0, &close_timer_reply, sizeof(close_timer_reply)) &&
            close_timer_reply.error == kStatusSuccess;

        uint8_t close_named_event_open_message[kWineServerFixedMessageSize] = {};
        auto* close_named_event_open_req =
            reinterpret_cast<HorizonCloseHandleRequest*>(close_named_event_open_message);
        close_named_event_open_req->header.req = kReqCloseHandle;
        close_named_event_open_req->handle = open_named_event_reply.handle;
        HorizonReplyHeader close_named_event_open_reply = {};
        const bool close_named_event_open_ok = close_timer_ok &&
            fixed_call(close_named_event_open_message, nullptr, 0,
                &close_named_event_open_reply, sizeof(close_named_event_open_reply)) &&
            close_named_event_open_reply.error == kStatusSuccess;

        uint8_t close_named_event_again_message[kWineServerFixedMessageSize] = {};
        auto* close_named_event_again_req =
            reinterpret_cast<HorizonCloseHandleRequest*>(close_named_event_again_message);
        close_named_event_again_req->header.req = kReqCloseHandle;
        close_named_event_again_req->handle = create_named_event_again_reply.handle;
        HorizonReplyHeader close_named_event_again_reply = {};
        const bool close_named_event_again_ok = close_named_event_open_ok &&
            fixed_call(close_named_event_again_message, nullptr, 0,
                &close_named_event_again_reply, sizeof(close_named_event_again_reply)) &&
            close_named_event_again_reply.error == kStatusSuccess;

        uint8_t close_named_event_message[kWineServerFixedMessageSize] = {};
        auto* close_named_event_req = reinterpret_cast<HorizonCloseHandleRequest*>(close_named_event_message);
        close_named_event_req->header.req = kReqCloseHandle;
        close_named_event_req->handle = create_named_event_reply.handle;
        HorizonReplyHeader close_named_event_reply = {};
        const bool close_named_event_ok = close_named_event_again_ok &&
            fixed_call(close_named_event_message, nullptr, 0,
                &close_named_event_reply, sizeof(close_named_event_reply)) &&
            close_named_event_reply.error == kStatusSuccess;

        uint8_t close_event_message[kWineServerFixedMessageSize] = {};
        auto* close_event_req = reinterpret_cast<HorizonCloseHandleRequest*>(close_event_message);
        close_event_req->header.req = kReqCloseHandle;
        close_event_req->handle = create_event_reply.handle;
        HorizonReplyHeader close_event_reply = {};
        const bool close_event_ok = close_named_event_ok &&
            fixed_call(close_event_message, nullptr, 0, &close_event_reply, sizeof(close_event_reply)) &&
            close_event_reply.error == kStatusSuccess;

        uint8_t close_mutex_message[kWineServerFixedMessageSize] = {};
        auto* close_mutex_req = reinterpret_cast<HorizonCloseHandleRequest*>(close_mutex_message);
        close_mutex_req->header.req = kReqCloseHandle;
        close_mutex_req->handle = create_mutex_reply.handle;
        HorizonReplyHeader close_mutex_reply = {};
        const bool close_mutex_ok = close_event_ok &&
            fixed_call(close_mutex_message, nullptr, 0, &close_mutex_reply, sizeof(close_mutex_reply)) &&
            close_mutex_reply.error == kStatusSuccess;

        uint8_t close_semaphore_message[kWineServerFixedMessageSize] = {};
        auto* close_semaphore_req = reinterpret_cast<HorizonCloseHandleRequest*>(close_semaphore_message);
        close_semaphore_req->header.req = kReqCloseHandle;
        close_semaphore_req->handle = create_semaphore_reply.handle;
        HorizonReplyHeader close_semaphore_reply = {};
        const bool close_semaphore_ok = close_mutex_ok &&
            fixed_call(close_semaphore_message, nullptr, 0,
                &close_semaphore_reply, sizeof(close_semaphore_reply)) &&
            close_semaphore_reply.error == kStatusSuccess;

        uint8_t close_other_message[kWineServerFixedMessageSize] = {};
        auto* close_other_req = reinterpret_cast<HorizonCloseHandleRequest*>(close_other_message);
        close_other_req->header.req = kReqCloseHandle;
        close_other_req->handle = alloc_other_reply.handle;
        HorizonReplyHeader close_other_reply = {};
        const bool close_other_ok = close_semaphore_ok &&
            fixed_call(close_other_message, nullptr, 0, &close_other_reply, sizeof(close_other_reply)) &&
            close_other_reply.error == kStatusSuccess &&
            close_other_reply.reply_size == 0;

        uint8_t close_dup_message[kWineServerFixedMessageSize] = {};
        auto* close_dup_req = reinterpret_cast<HorizonCloseHandleRequest*>(close_dup_message);
        close_dup_req->header.req = kReqCloseHandle;
        close_dup_req->handle = dup_reply.handle;
        HorizonReplyHeader close_dup_reply = {};
        const bool close_dup_ok = close_other_ok &&
            fixed_call(close_dup_message, nullptr, 0, &close_dup_reply, sizeof(close_dup_reply)) &&
            close_dup_reply.error == kStatusSuccess &&
            close_dup_reply.reply_size == 0;

        uint8_t close_alloc_message[kWineServerFixedMessageSize] = {};
        auto* close_alloc_req = reinterpret_cast<HorizonCloseHandleRequest*>(close_alloc_message);
        close_alloc_req->header.req = kReqCloseHandle;
        close_alloc_req->handle = alloc_reply.handle;
        HorizonReplyHeader close_alloc_reply = {};
        const bool close_alloc_ok = close_dup_ok &&
            fixed_call(close_alloc_message, nullptr, 0, &close_alloc_reply, sizeof(close_alloc_reply)) &&
            close_alloc_reply.error == kStatusSuccess &&
            close_alloc_reply.reply_size == 0;

        objects_ok = alloc_ok && dup_ok && compare_same_ok && compare_dup_ok &&
            alloc_other_ok && compare_diff_ok && close_timer_ok && close_named_event_open_ok &&
            close_named_event_again_ok && close_named_event_ok && close_event_ok &&
            close_mutex_ok && close_semaphore_ok && close_other_ok && close_dup_ok && close_alloc_ok;
        select_ok = select_signal_ok && real_select_signal_ok && select_timeout_ok;
        sync_ok = sync_state_ok && close_timer_ok && close_named_event_open_ok &&
            close_named_event_again_ok && close_named_event_ok && close_event_ok &&
            close_mutex_ok && close_semaphore_ok;
    }

    const bool ok = wrote_request && reply_ok && done_ok && thread_ok && objects_ok && select_ok && sync_ok;
    log_line("[SERVER] reqreply control_fd=%d request_fd=%d req=%d request_size=%u reply_size=%u pid=0x%08x tid=0x%08x machine=0x%04x done=%s thread=%s objects=%s select=%s sync=%s result=%s",
        control_fd, request_fd, request->header.req,
        request->header.request_size, request->header.reply_size,
        client_reply.pid, client_reply.tid, client_machine,
        done_ok ? "OK" : "FAIL", thread_ok ? "OK" : "FAIL",
        objects_ok ? "OK" : "FAIL", select_ok ? "OK" : "FAIL",
        sync_ok ? "OK" : "FAIL",
        ok ? "OK" : "FAIL");

    if (reply_pipe[0] != -1) close(reply_pipe[0]);
    if (reply_pipe[1] != -1) close(reply_pipe[1]);
    if (wait_pipe[0] != -1) close(wait_pipe[0]);
    if (wait_pipe[1] != -1) close(wait_pipe[1]);
    if (thread_reply_pipe[0] != -1) close(thread_reply_pipe[0]);
    if (thread_reply_pipe[1] != -1) close(thread_reply_pipe[1]);
    if (thread_wait_pipe[0] != -1) close(thread_wait_pipe[0]);
    if (thread_wait_pipe[1] != -1) close(thread_wait_pipe[1]);
    if (request_fd != -1) close(request_fd);
    if (control_fd != -1) close(control_fd);

    return ok;
}

bool run_horizon_server_event_probe()
{
    Handle write_event = INVALID_HANDLE;
    Handle read_event = INVALID_HANDLE;
    s32 index = -1;

    Result rc = svcCreateEvent(&write_event, &read_event);
    log_line("[SERVER] svcCreateEvent write=0x%x read=0x%x rc=0x%08x %s",
        write_event, read_event, rc, rc_state(rc));
    if (R_FAILED(rc))
    {
        log_result_decode("[SERVER] svcCreateEvent", rc);
        return false;
    }

    Result signal_rc = svcSignalEvent(write_event);
    log_line("[SERVER] svcSignalEvent rc=0x%08x %s", signal_rc, rc_state(signal_rc));
    Result wait_rc = svcWaitSynchronization(&index, &read_event, 1, 0);
    log_line("[SERVER] event wait rc=0x%08x %s index=%d", wait_rc, rc_state(wait_rc), index);

    svcCloseHandle(read_event);
    svcCloseHandle(write_event);

    const bool ok = R_SUCCEEDED(signal_rc) && R_SUCCEEDED(wait_rc) && index == 0;
    log_line("[SERVER] event result=%s", ok ? "OK" : "FAIL");
    return ok;
}

struct ServerProbePayload
{
    uint32_t magic;
    uint32_t value;
};

struct ServerProbeContext
{
    Handle server_handle;
    Result receive_rc;
    Result reply_rc;
    uint32_t request_type;
    uint32_t request_words;
    uint32_t request_id;
    uint32_t request_magic;
    uint32_t request_value;
    uint32_t received_handle;
    bool received_copy_handle;
};

void server_session_probe_thread(void* arg)
{
    auto* ctx = static_cast<ServerProbeContext*>(arg);
    Handle server_handle = ctx->server_handle;
    s32 index = -1;

    memset(armGetTls(), 0, 0x100);
    ctx->receive_rc = svcReplyAndReceive(&index, &server_handle, 1, INVALID_HANDLE, UINT64_MAX);
    if (R_FAILED(ctx->receive_rc))
        return;

    HipcParsedRequest parsed = hipcParseRequest(armGetTls());
    ctx->request_type = parsed.meta.type;
    ctx->request_words = parsed.meta.num_data_words;
    ctx->received_copy_handle = parsed.meta.num_copy_handles > 0;
    if (ctx->received_copy_handle)
    {
        ctx->received_handle = parsed.data.copy_handles[0];
        svcCloseHandle(ctx->received_handle);
    }

    auto* in_header = static_cast<CmifInHeader*>(cmifGetAlignedDataStart(parsed.data.data_words, armGetTls()));
    ctx->request_id = in_header->command_id;
    const auto* payload = reinterpret_cast<const ServerProbePayload*>(in_header + 1);
    ctx->request_magic = payload->magic;
    ctx->request_value = payload->value;

    memset(armGetTls(), 0, 0x100);

    const size_t raw_size = sizeof(CmifOutHeader) + sizeof(ServerProbePayload);
    HipcMetadata meta = {};
    meta.type = CmifCommandType_Invalid;
    meta.num_data_words = static_cast<uint32_t>((align_up(raw_size, 4) + 0x10) / sizeof(uint32_t));
    meta.num_copy_handles = 1;

    HipcRequest response = hipcMakeRequest(armGetTls(), meta);
    auto* out_header = static_cast<CmifOutHeader*>(cmifGetAlignedDataStart(response.data_words, armGetTls()));
    *out_header = { CMIF_OUT_HEADER_MAGIC, 0, 0, 0 };

    auto* out_payload = reinterpret_cast<ServerProbePayload*>(out_header + 1);
    out_payload->magic = kServerProbeMagic;
    out_payload->value = kServerProbeReplyValue;
    response.copy_handles[0] = CUR_PROCESS_HANDLE;

    s32 reply_index = -1;
    ctx->reply_rc = svcReplyAndReceive(&reply_index, &server_handle, 1, server_handle, 0);
}

bool run_horizon_server_session_probe()
{
    Handle server_handle = INVALID_HANDLE;
    Handle client_handle = INVALID_HANDLE;
    Thread server_thread = {};
    ServerProbeContext ctx = {};

    Result rc = svcCreateSession(&server_handle, &client_handle, 0, 0);
    log_line("[SERVER] svcCreateSession server=0x%x client=0x%x rc=0x%08x %s",
        server_handle, client_handle, rc, rc_state(rc));
    if (R_FAILED(rc))
    {
        log_result_decode("[SERVER] svcCreateSession", rc);
        return false;
    }

    ctx.server_handle = server_handle;
    rc = threadCreate(&server_thread, server_session_probe_thread, &ctx, nullptr, 0x4000, 0x3b, -2);
    log_line("[SERVER] threadCreate rc=0x%08x %s", rc, rc_state(rc));
    if (R_FAILED(rc))
    {
        svcCloseHandle(client_handle);
        svcCloseHandle(server_handle);
        return false;
    }

    rc = threadStart(&server_thread);
    log_line("[SERVER] threadStart rc=0x%08x %s", rc, rc_state(rc));
    if (R_FAILED(rc))
    {
        threadClose(&server_thread);
        svcCloseHandle(client_handle);
        svcCloseHandle(server_handle);
        return false;
    }

    memset(armGetTls(), 0, 0x100);

    CmifRequestFormat format = {};
    format.request_id = kServerProbeRequestId;
    format.data_size = sizeof(ServerProbePayload);
    format.num_handles = 1;

    CmifRequest request = cmifMakeRequest(armGetTls(), format);
    auto* payload = static_cast<ServerProbePayload*>(request.data);
    payload->magic = kServerProbeMagic;
    payload->value = kServerProbeValue;
    cmifRequestHandle(&request, CUR_PROCESS_HANDLE);

    Result send_rc = svcSendSyncRequest(client_handle);
    log_line("[SERVER] svcSendSyncRequest rc=0x%08x %s", send_rc, rc_state(send_rc));

    Result parse_rc = MAKERESULT(Module_Libnx, LibnxError_BadInput);
    ServerProbePayload reply_payload = {};
    Handle reply_handle = INVALID_HANDLE;
    if (R_SUCCEEDED(send_rc))
    {
        CmifResponse response = {};
        parse_rc = cmifParseResponse(&response, armGetTls(), false, sizeof(ServerProbePayload));
        if (R_SUCCEEDED(parse_rc))
        {
            reply_payload = *static_cast<ServerProbePayload*>(response.data);
            reply_handle = response.copy_handles[0];
            svcCloseHandle(reply_handle);
        }
    }
    log_line("[SERVER] parse reply rc=0x%08x %s magic=0x%08x value=0x%08x handle=0x%x",
        parse_rc, rc_state(parse_rc), reply_payload.magic, reply_payload.value, reply_handle);

    Result wait_rc = threadWaitForExit(&server_thread);
    log_line("[SERVER] threadWaitForExit rc=0x%08x %s", wait_rc, rc_state(wait_rc));
    threadClose(&server_thread);
    svcCloseHandle(client_handle);
    svcCloseHandle(server_handle);

    const bool request_ok = R_SUCCEEDED(ctx.receive_rc) &&
        ctx.request_type == CmifCommandType_Request &&
        ctx.request_id == kServerProbeRequestId &&
        ctx.request_magic == kServerProbeMagic &&
        ctx.request_value == kServerProbeValue &&
        ctx.received_copy_handle;
    const bool reply_ok = R_SUCCEEDED(send_rc) && R_SUCCEEDED(parse_rc) &&
        reply_payload.magic == kServerProbeMagic &&
        reply_payload.value == kServerProbeReplyValue &&
        reply_handle != INVALID_HANDLE;
    const bool ok = request_ok && reply_ok && R_SUCCEEDED(wait_rc);

    log_line("[SERVER] received rc=0x%08x type=%u words=%u cmd=0x%08x magic=0x%08x value=0x%08x copy_handle=%d reply_rc=0x%08x",
        ctx.receive_rc, ctx.request_type, ctx.request_words, ctx.request_id,
        ctx.request_magic, ctx.request_value, ctx.received_copy_handle ? 1 : 0, ctx.reply_rc);
    log_line("[SERVER] session result=%s", ok ? "OK" : "FAIL");
    return ok;
}

bool reserve_large_guest_window()
{
    VirtmemReservation* reservation = nullptr;
    void* region = nullptr;

    virtmemLock();
    region = virtmemFindAslr(kGuestReservationSize, kLargePageSize);
    if (region)
        reservation = virtmemAddReservation(region, kGuestReservationSize);
    virtmemUnlock();

    log_line("[VM] 4GB reservation region=%p reservation=%p", region, static_cast<void*>(reservation));
    if (!reservation)
        return false;

    virtmemLock();
    virtmemRemoveReservation(reservation);
    virtmemUnlock();
    return true;
}

bool run_process_memory_alias_probe()
{
    if (!blr_vm_syscalls_available())
    {
        log_line("[VM] BLR/process-memory syscalls unavailable, alias probe skipped");
        return true;
    }

    void* backing = memalign(kLargePageSize, kLargePageSize);
    if (!backing)
    {
        log_line("[VM] memalign backing failed");
        return false;
    }
    memset(backing, 0, kLargePageSize);
    static_cast<uint32_t*>(backing)[0] = 0x57494e45;

    void* code_mirror = nullptr;
    Result rc = 0;

    virtmemLock();
    code_mirror = virtmemFindCodeMemory(kLargePageSize, kLargePageSize);
    if (code_mirror)
    {
        rc = svcMapProcessCodeMemory(envGetOwnProcessHandle(),
            reinterpret_cast<uint64_t>(code_mirror),
            reinterpret_cast<uint64_t>(backing), kLargePageSize);
        if (R_SUCCEEDED(rc))
        {
            rc = svcSetProcessMemoryPermission(envGetOwnProcessHandle(),
                reinterpret_cast<uint64_t>(code_mirror), kLargePageSize, Perm_Rw);
        }
    }
    else
    {
        rc = MAKERESULT(Module_Libnx, LibnxError_OutOfMemory);
    }
    virtmemUnlock();

    log_line("[VM] code mirror=%p rc=0x%08x %s", code_mirror, rc, rc_state(rc));
    if (R_FAILED(rc))
    {
        free(backing);
        return false;
    }

    VirtmemReservation* map_reservation = nullptr;
    void* map_region = nullptr;

    virtmemLock();
    map_region = virtmemFindAslr(kLargePageSize, kLargePageSize);
    if (map_region)
        map_reservation = virtmemAddReservation(map_region, kLargePageSize);
    virtmemUnlock();

    log_line("[VM] map reservation region=%p reservation=%p", map_region, static_cast<void*>(map_reservation));
    if (!map_reservation)
    {
        svcUnmapProcessCodeMemory(envGetOwnProcessHandle(),
            reinterpret_cast<uint64_t>(code_mirror),
            reinterpret_cast<uint64_t>(backing), kLargePageSize);
        free(backing);
        return false;
    }

    rc = svcMapProcessMemory(map_region, envGetOwnProcessHandle(),
        reinterpret_cast<uint64_t>(code_mirror), kLargePageSize);
    log_line("[VM] svcMapProcessMemory rc=0x%08x %s", rc, rc_state(rc));

    bool ok = R_SUCCEEDED(rc);
    if (ok)
    {
        auto* mapped = static_cast<uint32_t*>(map_region);
        auto* mirror = static_cast<uint32_t*>(code_mirror);
        ok = mapped[0] == 0x57494e45;
        mapped[1] = 0x50524f42;
        ok = ok && mirror[1] == 0x50524f42;
        log_line("[VM] alias read/write mapped[0]=0x%08x mirror[1]=0x%08x result=%s",
            mapped[0], mirror[1], ok ? "OK" : "FAIL");

        const Result unmap_rc = svcUnmapProcessMemory(map_region, envGetOwnProcessHandle(),
            reinterpret_cast<uint64_t>(code_mirror), kLargePageSize);
        log_line("[VM] svcUnmapProcessMemory rc=0x%08x %s", unmap_rc, rc_state(unmap_rc));
        ok = ok && R_SUCCEEDED(unmap_rc);
    }

    virtmemLock();
    virtmemRemoveReservation(map_reservation);
    virtmemUnlock();

    const Result unmap_code_rc = svcUnmapProcessCodeMemory(envGetOwnProcessHandle(),
        reinterpret_cast<uint64_t>(code_mirror),
        reinterpret_cast<uint64_t>(backing), kLargePageSize);
    log_line("[VM] svcUnmapProcessCodeMemory rc=0x%08x %s", unmap_code_rc, rc_state(unmap_code_rc));
    free(backing);
    return ok && R_SUCCEEDED(unmap_code_rc);
}

bool run_exception_resume_probe()
{
    if (!blr_vm_syscalls_available())
    {
        log_line("[EXC] BLR/process-memory syscalls unavailable, exception resume probe skipped");
        return true;
    }

    void* backing = memalign(kLargePageSize, kLargePageSize);
    if (!backing)
    {
        log_line("[EXC] memalign backing failed");
        return false;
    }

    memset(backing, 0, kLargePageSize);
    auto* code = static_cast<uint32_t*>(backing);
    code[0] = 0x52886420; // movz w0, #0x4321
    code[1] = 0xd65f03c0; // ret
    armDCacheFlush(backing, kPageSize);

    void* code_mirror = nullptr;
    VirtmemReservation* code_reservation = nullptr;
    Result rc = 0;

    virtmemLock();
    code_mirror = virtmemFindCodeMemory(kLargePageSize, kLargePageSize);
    if (code_mirror)
        code_reservation = virtmemAddReservation(code_mirror, kLargePageSize);
    else
        rc = MAKERESULT(Module_Libnx, LibnxError_OutOfMemory);
    virtmemUnlock();

    log_line("[EXC] code mirror=%p reservation=%p rc=0x%08x %s",
        code_mirror, static_cast<void*>(code_reservation), rc, rc_state(rc));
    if (!code_reservation || R_FAILED(rc))
    {
        free(backing);
        return false;
    }

    g_exception_probe_hits = 0;
    g_exception_probe_rc = 0;
    g_exec_fault_addr = reinterpret_cast<uintptr_t>(code_mirror);
    g_exec_fault_size = kLargePageSize;
    g_exec_backing = reinterpret_cast<uintptr_t>(backing);

    using ProbeFn = int (*)();
    const int value = reinterpret_cast<ProbeFn>(code_mirror)();

    g_exec_fault_addr = 0;
    g_exec_fault_size = 0;
    g_exec_backing = 0;

    const bool ok = value == 0x4321 && g_exception_probe_hits == 1 &&
                    R_SUCCEEDED(g_exception_probe_rc);
    log_line("[EXC] call=%d hits=%u handler_rc=0x%08x result=%s",
        value, static_cast<unsigned>(g_exception_probe_hits),
        g_exception_probe_rc, ok ? "OK" : "FAIL");

    const Result unmap_code_rc = svcUnmapProcessCodeMemory(envGetOwnProcessHandle(),
        reinterpret_cast<uint64_t>(code_mirror),
        reinterpret_cast<uint64_t>(backing), kLargePageSize);
    log_line("[EXC] svcUnmapProcessCodeMemory rc=0x%08x %s", unmap_code_rc, rc_state(unmap_code_rc));
    virtmemLock();
    virtmemRemoveReservation(code_reservation);
    virtmemUnlock();
    free(backing);
    return ok && R_SUCCEEDED(unmap_code_rc);
}

bool run_horizon_backend_probe()
{
    constexpr size_t size = kPageSize * 2;
    constexpr size_t reservation_size = kPageSize * 4;
    void* mapping = horizon_mmap(nullptr, size, PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANON, -1, 0);

    log_line("[WINE] horizon_mmap rw size=0x%zx addr=%p errno=%d",
        size, mapping, mapping == MAP_FAILED ? errno : 0);
    if (mapping == MAP_FAILED)
        return false;

    auto* words = static_cast<uint32_t*>(mapping);
    words[0] = 0x52864200; // movz w0, #0x3210
    words[1] = 0xd65f03c0; // ret

    auto* second_page = reinterpret_cast<uint32_t*>(
        static_cast<uint8_t*>(mapping) + kPageSize);
    second_page[0] = 0x57494e45;

    int ret = horizon_mprotect(mapping, kPageSize, PROT_READ | PROT_EXEC);
    log_line("[WINE] mprotect first page rx ret=%d errno=%d", ret, ret ? errno : 0);
    bool ok = ret == 0;

    int first_value = -1;
    if (ok)
    {
        using ProbeFn = int (*)();
        first_value = reinterpret_cast<ProbeFn>(mapping)();
        ok = first_value == 0x3210;
    }

    second_page[1] = 0x50524f42;
    ok = ok && second_page[0] == 0x57494e45 && second_page[1] == 0x50524f42;
    log_line("[WINE] partial rx call=%d second[0]=0x%08x second[1]=0x%08x result=%s",
        first_value, second_page[0], second_page[1], ok ? "OK" : "FAIL");

    ret = horizon_mprotect(mapping, kPageSize, PROT_READ | PROT_WRITE);
    log_line("[WINE] mprotect first page back rw ret=%d errno=%d", ret, ret ? errno : 0);
    ok = ok && ret == 0;

    if (ret == 0)
    {
        words[0] = 0x52886420; // movz w0, #0x4321
        words[1] = 0xd65f03c0; // ret
    }

    ret = horizon_mprotect(mapping, kPageSize, PROT_READ | PROT_EXEC);
    log_line("[WINE] mprotect first page rx again ret=%d errno=%d", ret, ret ? errno : 0);
    ok = ok && ret == 0;

    int second_value = -1;
    if (ret == 0)
    {
        using ProbeFn = int (*)();
        second_value = reinterpret_cast<ProbeFn>(mapping)();
        ok = ok && second_value == 0x4321;
    }

    log_line("[WINE] rw-rx-rw-rx call=%d result=%s",
        second_value, ok ? "OK" : "FAIL");

    ret = horizon_munmap(mapping, size);
    log_line("[WINE] horizon_munmap ret=%d errno=%d", ret, ret ? errno : 0);
    ok = ok && ret == 0;

    const char* path = "sdmc:/switch/wine-nx-probe/horizon-file.bin";
    int fd = open(path, O_CREAT | O_TRUNC | O_RDWR, 0666);
    if (fd == -1)
    {
        log_line("[WINE] open file mapping fixture failed errno=%d", errno);
        return false;
    }

    void* file_buffer = memalign(kPageSize, size);
    if (!file_buffer)
    {
        close(fd);
        log_line("[WINE] file fixture allocation failed");
        return false;
    }

    memset(file_buffer, 0, size);
    auto* fixture = static_cast<uint32_t*>(file_buffer);
    fixture[0] = 0x46494c45;
    fixture[1] = 0x4d415000;
    if (write(fd, file_buffer, size) != static_cast<ssize_t>(size))
    {
        log_line("[WINE] file fixture write failed errno=%d", errno);
        free(file_buffer);
        close(fd);
        return false;
    }
    lseek(fd, 0, SEEK_SET);

    void* file_mapping = horizon_mmap(nullptr, size, PROT_READ | PROT_WRITE,
        MAP_SHARED, fd, 0);
    log_line("[WINE] file horizon_mmap shared addr=%p errno=%d",
        file_mapping, file_mapping == MAP_FAILED ? errno : 0);
    close(fd);

    bool file_ok = file_mapping != MAP_FAILED;
    if (file_ok)
    {
        auto* mapped = static_cast<uint32_t*>(file_mapping);
        file_ok = mapped[0] == 0x46494c45 && mapped[1] == 0x4d415000;
        mapped[2] = 0x57524954;
        mapped[3] = 0x45424143;
        ret = horizon_munmap(file_mapping, size);
        file_ok = file_ok && ret == 0;
        log_line("[WINE] file horizon_munmap ret=%d errno=%d", ret, ret ? errno : 0);
    }

    fd = open(path, O_RDONLY);
    if (fd != -1)
    {
        memset(file_buffer, 0, size);
        const ssize_t got = read(fd, file_buffer, size);
        close(fd);
        fixture = static_cast<uint32_t*>(file_buffer);
        file_ok = file_ok && got == static_cast<ssize_t>(size) &&
                  fixture[2] == 0x57524954 && fixture[3] == 0x45424143;
        log_line("[WINE] file writeback got=%zd word2=0x%08x word3=0x%08x result=%s",
            got, fixture[2], fixture[3], file_ok ? "OK" : "FAIL");
    }
    else
    {
        log_line("[WINE] reopen file mapping fixture failed errno=%d", errno);
        file_ok = false;
    }

    free(file_buffer);

    void* reservation = horizon_mmap(nullptr, reservation_size, PROT_NONE,
        MAP_PRIVATE | MAP_ANON, -1, 0);
    log_line("[WINE] reservation mmap none size=0x%zx addr=%p errno=%d",
        reservation_size, reservation, reservation == MAP_FAILED ? errno : 0);

    bool fixed_ok = reservation != MAP_FAILED;
    if (fixed_ok)
    {
        void* commit_addr = static_cast<uint8_t*>(reservation) + kPageSize;
        void* committed = horizon_mmap(commit_addr, size, PROT_READ | PROT_WRITE,
            MAP_PRIVATE | MAP_ANON | MAP_FIXED, -1, 0);
        log_line("[WINE] fixed commit addr=%p ret=%p errno=%d",
            commit_addr, committed, committed == MAP_FAILED ? errno : 0);
        fixed_ok = committed == commit_addr;

        if (fixed_ok)
        {
            auto* committed_words = static_cast<uint32_t*>(committed);
            committed_words[0] = 0x528acf00; // movz w0, #0x5678
            committed_words[1] = 0xd65f03c0; // ret
            committed_words[kPageSize / sizeof(uint32_t)] = 0x434f4d4d;

            ret = horizon_mprotect(committed, kPageSize, PROT_READ | PROT_EXEC);
            log_line("[WINE] fixed commit first page rx ret=%d errno=%d", ret, ret ? errno : 0);
            fixed_ok = ret == 0;

            int fixed_value = -1;
            if (fixed_ok)
            {
                using ProbeFn = int (*)();
                fixed_value = reinterpret_cast<ProbeFn>(committed)();
                fixed_ok = fixed_value == 0x5678 &&
                           committed_words[kPageSize / sizeof(uint32_t)] == 0x434f4d4d;
            }

            log_line("[WINE] fixed commit call=%d second_page=0x%08x result=%s",
                fixed_value, committed_words[kPageSize / sizeof(uint32_t)],
                fixed_ok ? "OK" : "FAIL");
        }

        ret = horizon_munmap(reservation, reservation_size);
        log_line("[WINE] reservation munmap ret=%d errno=%d", ret, ret ? errno : 0);
        fixed_ok = fixed_ok && ret == 0;
    }

    void* protect_reservation = horizon_mmap(nullptr, reservation_size, PROT_NONE,
        MAP_PRIVATE | MAP_ANON, -1, 0);
    log_line("[WINE] protect reservation mmap none addr=%p errno=%d",
        protect_reservation, protect_reservation == MAP_FAILED ? errno : 0);

    bool protect_ok = protect_reservation != MAP_FAILED;
    if (protect_ok)
    {
        void* commit_addr = static_cast<uint8_t*>(protect_reservation) + kPageSize;

        ret = horizon_mprotect(commit_addr, size, PROT_READ | PROT_WRITE);
        log_line("[WINE] reservation mprotect commit ret=%d errno=%d", ret, ret ? errno : 0);
        protect_ok = ret == 0;

        if (protect_ok)
        {
            auto* committed_words = static_cast<uint32_t*>(commit_addr);
            committed_words[0] = 0x528e1300; // movz w0, #0x7098
            committed_words[1] = 0xd65f03c0; // ret
            committed_words[kPageSize / sizeof(uint32_t)] = 0x50524f54;

            ret = horizon_mprotect(commit_addr, kPageSize, PROT_READ | PROT_EXEC);
            log_line("[WINE] reservation commit first page rx ret=%d errno=%d", ret, ret ? errno : 0);
            protect_ok = ret == 0;

            int protect_value = -1;
            if (protect_ok)
            {
                using ProbeFn = int (*)();
                protect_value = reinterpret_cast<ProbeFn>(commit_addr)();
                protect_ok = protect_value == 0x7098 &&
                             committed_words[kPageSize / sizeof(uint32_t)] == 0x50524f54;
            }

            log_line("[WINE] reservation commit call=%d second_page=0x%08x result=%s",
                protect_value, committed_words[kPageSize / sizeof(uint32_t)],
                protect_ok ? "OK" : "FAIL");
        }

        ret = horizon_munmap(protect_reservation, reservation_size);
        log_line("[WINE] protect reservation munmap ret=%d errno=%d", ret, ret ? errno : 0);
        protect_ok = protect_ok && ret == 0;
    }

    return ok && file_ok && fixed_ok && protect_ok;
}

bool run_vm_probe()
{
    const bool reserve_ok = reserve_large_guest_window();
    const bool alias_ok = run_process_memory_alias_probe();
    const bool exception_ok = run_exception_resume_probe();
    const bool horizon_ok = run_horizon_backend_probe();
    log_line("[VM] result=%s", (reserve_ok && alias_ok && exception_ok && horizon_ok) ? "OK" : "FAIL");
    return reserve_ok && alias_ok && exception_ok && horizon_ok;
}
} // namespace

int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;

    consoleInit(nullptr);
    mkdir("sdmc:/switch", 0777);
    mkdir(kLogDir, 0777);
    g_log = fopen(kLogPath, "w");

    log_line("Wine-NX Horizon probe starting");
    log_line("TLS ABI: -mtp=soft -ftls-model=local-exec");
    log_wineserver_syscall_hints();
    run_horizon_server_resource_limit_probe();

    const bool server_uevent_ok = run_horizon_server_user_event_probe();
    const bool server_pipe_ok = run_horizon_server_pipe_probe();
    const bool server_fdpass_ok = run_horizon_server_fd_passing_probe();
    const bool server_connect_ok = run_horizon_server_connect_probe();
    const bool server_reqreply_ok = run_horizon_server_request_reply_probe();
    const bool server_kernel_event_ok = run_horizon_server_event_probe();
    const bool server_kernel_session_ok = run_horizon_server_session_probe();
    const bool tls_ok = run_tls_and_affinity_probe();
    const bool jit_ok = run_jit_probe();
    const bool vm_ok = run_vm_probe();
    const bool all_ok = server_uevent_ok && server_pipe_ok && server_fdpass_ok &&
        server_connect_ok && server_reqreply_ok && tls_ok && jit_ok && vm_ok;

    log_line("SUMMARY server_uevent=%s server_pipe=%s server_fdpass=%s server_connect=%s server_reqreply=%s server_kernel_event=%s server_kernel_session=%s tls=%s jit=%s vm=%s overall=%s",
        server_uevent_ok ? "OK" : "FAIL",
        server_pipe_ok ? "OK" : "FAIL",
        server_fdpass_ok ? "OK" : "FAIL",
        server_connect_ok ? "OK" : "FAIL",
        server_reqreply_ok ? "OK" : "FAIL",
        server_kernel_event_ok ? "OK" : "FAIL",
        server_kernel_session_ok ? "OK" : "FAIL", tls_ok ? "OK" : "FAIL",
        jit_ok ? "OK" : "FAIL", vm_ok ? "OK" : "FAIL",
        all_ok ? "OK" : "FAIL");
    log_line("Log: %s", kLogPath);

    log_line("[EXIT] parked after summary; close the application from HOME");

    if (g_log)
    {
        fclose(g_log);
        g_log = nullptr;
    }

    (void)all_ok;
    for (;;)
    {
        consoleUpdate(nullptr);
        svcSleepThread(1000 * 1000 * 1000LL);
    }
}
