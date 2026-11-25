// lib/windows.h's unwind API, <intrin.h>'s interlocked family, <eh.h> and __declspec(thread), as
// cppparse's runtime uses them. The asserts are the x64 SDK's layouts, measured with cl and clang there.
#include <windows.h>
#include <eh.h>
#include <intrin.h>
#include <stddef.h>
#include <stdio.h>

static_assert(sizeof(M128A) == 16, "M128A");
static_assert(sizeof(CONTEXT) == 1232, "CONTEXT");
static_assert(offsetof(CONTEXT, Rbx) == 144, "Rbx");
static_assert(offsetof(CONTEXT, Rsp) == 152, "Rsp");
static_assert(offsetof(CONTEXT, R15) == 240, "R15");
static_assert(offsetof(CONTEXT, Rip) == 248, "Rip");
static_assert(offsetof(CONTEXT, Xmm6) == 512, "Xmm6");
static_assert(offsetof(CONTEXT, VectorRegister) == 768, "VectorRegister");
static_assert(offsetof(CONTEXT, LastExceptionFromRip) == 1224, "LastExceptionFromRip");
static_assert(sizeof(EXCEPTION_RECORD) == 152, "EXCEPTION_RECORD");
static_assert(offsetof(EXCEPTION_RECORD, ExceptionInformation) == 32, "ExceptionInformation");
static_assert(sizeof(RUNTIME_FUNCTION) == 12, "RUNTIME_FUNCTION");
static_assert(sizeof(DISPATCHER_CONTEXT) == 80, "DISPATCHER_CONTEXT");
static_assert(offsetof(DISPATCHER_CONTEXT, HandlerData) == 56, "HandlerData");
static_assert(offsetof(DISPATCHER_CONTEXT, ScopeIndex) == 72, "ScopeIndex");
static_assert(UNW_FLAG_EHANDLER == 1 && UNW_FLAG_UHANDLER == 2, "UNW_FLAG");

static __declspec(thread) int t_count = 3;
static long volatile g_counter = 0;

static void onTerminate() {}

int main() {
    CONTEXT ctx;
    RtlCaptureContext(&ctx);
    DWORD64 base = 0;
    PRUNTIME_FUNCTION rf = RtlLookupFunctionEntry(ctx.Rip, &base, 0);
    int inside = rf != 0 && ctx.Rip >= base + rf->BeginAddress && ctx.Rip < base + rf->EndAddress;
    printf("aligned %d table %d inside %d\n", (int)((DWORD64)&ctx % 16 == 0), rf != 0, inside);
    _InterlockedIncrement(&g_counter);
    _InterlockedIncrement(&g_counter);
    long was = _InterlockedCompareExchange(&g_counter, 10, 2);
    long old = _InterlockedExchange(&g_counter, 7);
    long now = _InterlockedDecrement(&g_counter);
    printf("interlocked %ld %ld %ld %ld\n", was, old, now, (long)g_counter);
    t_count += 4;
    printf("thread %d\n", t_count);
    terminate_handler before = set_terminate(onTerminate);
    terminate_handler mine = set_terminate(before);
    printf("terminate %d uncaught %d\n", mine == onTerminate, (int)__uncaught_exception());
    return 0;
}
