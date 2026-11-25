// windows.h - the part of the Win32 API a program here has asked for.
//
// Not Microsoft's header, and not a copy of one - that is the Windows SDK's,
// and no part of this toolchain reads the SDK's headers. What a program wants
// from it is a few types, structs and prototypes, which are ordinary C. They
// must agree with Windows' own, because the program links against kernel32.lib
// and the system fills the structs: a member at the wrong offset is a wrong
// answer, a short struct a write past the object. Each struct below is laid
// out as the x64 SDK lays it out, with the offsets written beside it.
//
// One calling convention on x64, so WINAPI and CALLBACK are nothing. `long` is
// four bytes (LLP64), so DWORD and LONG are the SDK's four-byte types.
//
// **InterlockedIncrement and its family are not in any library on x64**:
// Microsoft's compiler makes each one an instruction with a `lock` prefix, and
// kernel32 exports none of them. This compiler has no such instruction to give,
// so here each is an inline function that takes a slim reader/writer lock
// around the read-modify-write. The lock is a static of the translation unit -
// zero, which is SRWLOCK_INIT, before any code runs, so no initialisation can
// race - which makes the operations atomic **against each other within one
// translation unit**. A counter several units update would need one lock for
// all of them; no program here has asked for that yet.
//
// For the Windows target only.
//
// Added 2026-10-20 for cppparse: its main thread's 512 MB stack (CreateThread,
// WaitForSingleObject) and its worker pool (GetSystemInfo, a critical section,
// WaitForMultipleObjects and an interlocked counter of the next job). 2026-10-23,
// for the compiler's own Driver.cpp: processes, file search and temporary files.
#ifndef _CXX1_WINDOWS_H
#define _CXX1_WINDOWS_H

#ifndef _WIN32
#error "<windows.h> is here for the x86_64-windows target only"
#endif

#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WINAPI
#define CALLBACK
#define TRUE 1
#define FALSE 0
#define INFINITE 0xFFFFFFFF
#define WAIT_OBJECT_0 0x00000000
#define WAIT_TIMEOUT 0x00000102
#define WAIT_FAILED 0xFFFFFFFF
#define CREATE_SUSPENDED 0x00000004
#define STACK_SIZE_PARAM_IS_A_RESERVATION 0x00010000
#define MAX_PATH 260
#define FILE_ATTRIBUTE_DIRECTORY 0x00000010
#define STARTF_USESTDHANDLES 0x00000100
#define STD_INPUT_HANDLE ((DWORD)-10)
#define STD_OUTPUT_HANDLE ((DWORD)-11)
#define STD_ERROR_HANDLE ((DWORD)-12)
#define INVALID_HANDLE_VALUE ((HANDLE)(LONG_PTR)-1)
#define ZeroMemory(d, n) memset((d), 0, (n))

typedef int BOOL;
typedef unsigned char BYTE;
typedef unsigned short WORD;
typedef unsigned long DWORD;
typedef long LONG;
typedef unsigned long ULONG;
typedef unsigned int UINT;
typedef long long LONGLONG;
typedef unsigned long long ULONGLONG;
typedef unsigned long long ULONG_PTR;
typedef unsigned long long DWORD_PTR;
typedef unsigned long long SIZE_T;
typedef long long LONG_PTR;
typedef void *LPVOID;
typedef void *PVOID;
typedef const void *LPCVOID;
typedef void *HANDLE;
typedef DWORD *LPDWORD;
typedef char CHAR;
typedef char *LPSTR;
typedef const char *LPCSTR;
typedef BYTE *LPBYTE;
typedef void *HMODULE;

typedef DWORD (*LPTHREAD_START_ROUTINE)(LPVOID parameter);

//  0 nLength  8 lpSecurityDescriptor  16 bInheritHandle  - 24 bytes
typedef struct _SECURITY_ATTRIBUTES {
    DWORD nLength;
    LPVOID lpSecurityDescriptor;
    BOOL bInheritHandle;
} SECURITY_ATTRIBUTES, *LPSECURITY_ATTRIBUTES;

//  0 dwOemId  4 dwPageSize  8 lpMinimumApplicationAddress  16 lpMaximumApplicationAddress
// 24 dwActiveProcessorMask  32 dwNumberOfProcessors  36 dwProcessorType
// 40 dwAllocationGranularity  44 wProcessorLevel  46 wProcessorRevision  - 48 bytes.
// dwOemId is a union in the SDK with two WORDs, wProcessorArchitecture and
// wReserved; the four bytes are the same either way.
typedef struct _SYSTEM_INFO {
    DWORD dwOemId;
    DWORD dwPageSize;
    LPVOID lpMinimumApplicationAddress;
    LPVOID lpMaximumApplicationAddress;
    DWORD_PTR dwActiveProcessorMask;
    DWORD dwNumberOfProcessors;
    DWORD dwProcessorType;
    DWORD dwAllocationGranularity;
    WORD wProcessorLevel;
    WORD wProcessorRevision;
} SYSTEM_INFO, *LPSYSTEM_INFO;

//  0 DebugInfo  8 LockCount  12 RecursionCount  16 OwningThread  24 LockSemaphore
// 32 SpinCount  - 40 bytes, the system's to use between Initialize and Delete.
typedef struct _RTL_CRITICAL_SECTION {
    void *DebugInfo;
    LONG LockCount;
    LONG RecursionCount;
    HANDLE OwningThread;
    HANDLE LockSemaphore;
    ULONG_PTR SpinCount;
} CRITICAL_SECTION, *LPCRITICAL_SECTION;

//  0 cb  8 lpReserved  16 lpDesktop  24 lpTitle  32 dwX .. 60 dwFlags  64 wShowWindow
// 66 cbReserved2  72 lpReserved2  80 hStdInput  88 hStdOutput  96 hStdError  - 104 bytes.
typedef struct _STARTUPINFOA {
    DWORD cb;
    LPSTR lpReserved;
    LPSTR lpDesktop;
    LPSTR lpTitle;
    DWORD dwX, dwY, dwXSize, dwYSize, dwXCountChars, dwYCountChars, dwFillAttribute, dwFlags;
    WORD wShowWindow;
    WORD cbReserved2;
    LPBYTE lpReserved2;
    HANDLE hStdInput;
    HANDLE hStdOutput;
    HANDLE hStdError;
} STARTUPINFOA, *LPSTARTUPINFOA;

//  0 hProcess  8 hThread  16 dwProcessId  20 dwThreadId  - 24 bytes.
typedef struct _PROCESS_INFORMATION {
    HANDLE hProcess;
    HANDLE hThread;
    DWORD dwProcessId;
    DWORD dwThreadId;
} PROCESS_INFORMATION, *LPPROCESS_INFORMATION;

typedef struct _FILETIME {
    DWORD dwLowDateTime;
    DWORD dwHighDateTime;
} FILETIME;

//  0 dwFileAttributes  4/12/20 the three FILETIMEs  28 nFileSizeHigh  32 nFileSizeLow
// 36, 40 dwReserved0/1  44 cFileName[260]  304 cAlternateFileName[14]  - 320 bytes.
typedef struct _WIN32_FIND_DATAA {
    DWORD dwFileAttributes;
    FILETIME ftCreationTime;
    FILETIME ftLastAccessTime;
    FILETIME ftLastWriteTime;
    DWORD nFileSizeHigh;
    DWORD nFileSizeLow;
    DWORD dwReserved0;
    DWORD dwReserved1;
    CHAR cFileName[MAX_PATH];
    CHAR cAlternateFileName[14];
} WIN32_FIND_DATAA, *LPWIN32_FIND_DATAA;

// One pointer, zero when free; SRWLOCK_INIT is that zero. <intrin.h> defines it too.
#ifndef _CXX1_RTL_SRWLOCK
#define _CXX1_RTL_SRWLOCK
typedef struct _RTL_SRWLOCK {
    PVOID Ptr;
} SRWLOCK, *PSRWLOCK;
#endif
#define SRWLOCK_INIT {0}

// The unwind API (winnt.h, x64), added 2026-11-25 for cppparse's runtime: a
// thread's registers, a function's table entry, one frame unwound by it.
typedef unsigned long long DWORD64, *PDWORD64;

//  0 Low  8 High - 16 bytes, aligned to 16 (DECLSPEC_ALIGN(16) in the SDK).
typedef struct _M128A {
    alignas(16) ULONGLONG Low;
    LONGLONG High;
} M128A, *PM128A;

// excpt.h's answers of an exception routine.
typedef enum _EXCEPTION_DISPOSITION {
    ExceptionContinueExecution,
    ExceptionContinueSearch,
    ExceptionNestedException,
    ExceptionCollidedUnwind
} EXCEPTION_DISPOSITION;

//  0 ExceptionCode  4 ExceptionFlags  8 ExceptionRecord  16 ExceptionAddress
// 24 NumberParameters  32 ExceptionInformation[15] - 152 bytes.
typedef struct _EXCEPTION_RECORD {
    DWORD ExceptionCode;
    DWORD ExceptionFlags;
    struct _EXCEPTION_RECORD *ExceptionRecord;
    PVOID ExceptionAddress;
    DWORD NumberParameters;
    ULONG_PTR ExceptionInformation[15];
} EXCEPTION_RECORD, *PEXCEPTION_RECORD;

//  0 P1Home..P6Home  48 ContextFlags  52 MxCsr  56 SegCs..SegSs  68 EFlags  72 Dr0..Dr7
// 120 Rax..R15  248 Rip  256 the FltSave union (Header, Legacy, Xmm0..Xmm15 and 96 bytes)
// 768 VectorRegister[26]  1184 VectorControl..LastExceptionFromRip - 1232 bytes, aligned to 16.
typedef struct _CONTEXT {
    DWORD64 P1Home, P2Home, P3Home, P4Home, P5Home, P6Home;
    DWORD ContextFlags;
    DWORD MxCsr;
    WORD SegCs, SegDs, SegEs, SegFs, SegGs, SegSs;
    DWORD EFlags;
    DWORD64 Dr0, Dr1, Dr2, Dr3, Dr6, Dr7;
    DWORD64 Rax, Rcx, Rdx, Rbx, Rsp, Rbp, Rsi, Rdi;
    DWORD64 R8, R9, R10, R11, R12, R13, R14, R15;
    DWORD64 Rip;
    M128A Header[2];
    M128A Legacy[8];
    M128A Xmm0, Xmm1, Xmm2, Xmm3, Xmm4, Xmm5, Xmm6, Xmm7;
    M128A Xmm8, Xmm9, Xmm10, Xmm11, Xmm12, Xmm13, Xmm14, Xmm15;
    BYTE FltSaveRest[96];
    M128A VectorRegister[26];
    DWORD64 VectorControl;
    DWORD64 DebugControl;
    DWORD64 LastBranchToRip;
    DWORD64 LastBranchFromRip;
    DWORD64 LastExceptionToRip;
    DWORD64 LastExceptionFromRip;
} CONTEXT, *PCONTEXT;

//  0 BeginAddress  4 EndAddress  8 UnwindData - 12 bytes, offsets in the image.
typedef struct _RUNTIME_FUNCTION {
    DWORD BeginAddress;
    DWORD EndAddress;
    DWORD UnwindData;
} RUNTIME_FUNCTION, *PRUNTIME_FUNCTION;

typedef EXCEPTION_DISPOSITION (*PEXCEPTION_ROUTINE)(struct _EXCEPTION_RECORD *record, PVOID establisherFrame,
                                                    struct _CONTEXT *context, PVOID dispatcherContext);

//  0 ControlPc  8 ImageBase  16 FunctionEntry  24 EstablisherFrame  32 TargetIp
// 40 ContextRecord  48 LanguageHandler  56 HandlerData  64 HistoryTable  72 ScopeIndex  76 Fill0 - 80 bytes.
typedef struct _DISPATCHER_CONTEXT {
    DWORD64 ControlPc;
    DWORD64 ImageBase;
    PRUNTIME_FUNCTION FunctionEntry;
    DWORD64 EstablisherFrame;
    DWORD64 TargetIp;
    PCONTEXT ContextRecord;
    PEXCEPTION_ROUTINE LanguageHandler;
    PVOID HandlerData;
    struct _UNWIND_HISTORY_TABLE *HistoryTable;
    DWORD ScopeIndex;
    DWORD Fill0;
} DISPATCHER_CONTEXT, *PDISPATCHER_CONTEXT;

#define UNW_FLAG_NHANDLER 0x0
#define UNW_FLAG_EHANDLER 0x1
#define UNW_FLAG_UHANDLER 0x2
#define UNW_FLAG_CHAININFO 0x4

void RtlCaptureContext(PCONTEXT context);
PRUNTIME_FUNCTION RtlLookupFunctionEntry(DWORD64 controlPc, PDWORD64 imageBase,
                                         struct _UNWIND_HISTORY_TABLE *historyTable);
PEXCEPTION_ROUTINE RtlVirtualUnwind(DWORD handlerType, DWORD64 imageBase, DWORD64 controlPc,
                                    PRUNTIME_FUNCTION functionEntry, PCONTEXT context, PVOID *handlerData,
                                    PDWORD64 establisherFrame, struct _KNONVOLATILE_CONTEXT_POINTERS *contextPointers);
void RtlRestoreContext(PCONTEXT context, struct _EXCEPTION_RECORD *exceptionRecord);
BOOL SwitchToThread(void);

HANDLE CreateThread(LPSECURITY_ATTRIBUTES attributes, SIZE_T stackSize, LPTHREAD_START_ROUTINE start,
                    LPVOID parameter, DWORD flags, LPDWORD threadId);
DWORD WaitForSingleObject(HANDLE handle, DWORD milliseconds);
DWORD WaitForMultipleObjects(DWORD count, const HANDLE *handles, BOOL all, DWORD milliseconds);
BOOL CloseHandle(HANDLE handle);
void GetSystemInfo(LPSYSTEM_INFO info);
DWORD GetTickCount(void);
ULONGLONG GetTickCount64(void);
DWORD GetLastError(void);
DWORD GetCurrentThreadId(void);
void Sleep(DWORD milliseconds);
void InitializeCriticalSection(LPCRITICAL_SECTION section);
void EnterCriticalSection(LPCRITICAL_SECTION section);
void LeaveCriticalSection(LPCRITICAL_SECTION section);
void DeleteCriticalSection(LPCRITICAL_SECTION section);
BOOL CreateProcessA(LPCSTR application, LPSTR commandLine, LPSECURITY_ATTRIBUTES processAttributes,
                    LPSECURITY_ATTRIBUTES threadAttributes, BOOL inheritHandles, DWORD flags,
                    LPVOID environment, LPCSTR directory, LPSTARTUPINFOA startup,
                    LPPROCESS_INFORMATION information);
BOOL GetExitCodeProcess(HANDLE process, LPDWORD code);
DWORD GetCurrentProcessId(void);
DWORD GetModuleFileNameA(HMODULE module, LPSTR name, DWORD size);
HANDLE GetStdHandle(DWORD which);
DWORD GetTempPathA(DWORD size, LPSTR buffer);
UINT GetTempFileNameA(LPCSTR path, LPCSTR prefix, UINT unique, LPSTR name);
DWORD SearchPathA(LPCSTR path, LPCSTR file, LPCSTR extension, DWORD size, LPSTR buffer, LPSTR *filePart);
HANDLE FindFirstFileA(LPCSTR pattern, LPWIN32_FIND_DATAA data);
BOOL FindNextFileA(HANDLE find, LPWIN32_FIND_DATAA data);
BOOL FindClose(HANDLE find);
void AcquireSRWLockExclusive(PSRWLOCK lock);
void ReleaseSRWLockExclusive(PSRWLOCK lock);

// The interlocked operations: each answers what the standard SDK's does - the
// new value for Increment and Decrement, the old one for the other three. The
// SDK's parameter is `LONG volatile *`; a pointer to a volatile type is not in
// this compiler's type system yet, so it is `LONG *`, which a `volatile LONG`
// member's address is here - and the lock, a call the compiler cannot see
// through, is what keeps the value out of a register across it.
static SRWLOCK __cxx1_interlocked;
static inline LONG InterlockedIncrement(LONG *p) {
    AcquireSRWLockExclusive(&__cxx1_interlocked);
    LONG now = ++*p;
    ReleaseSRWLockExclusive(&__cxx1_interlocked);
    return now;
}
static inline LONG InterlockedDecrement(LONG *p) {
    AcquireSRWLockExclusive(&__cxx1_interlocked);
    LONG now = --*p;
    ReleaseSRWLockExclusive(&__cxx1_interlocked);
    return now;
}
static inline LONG InterlockedExchange(LONG *p, LONG value) {
    AcquireSRWLockExclusive(&__cxx1_interlocked);
    LONG was = *p;
    *p = value;
    ReleaseSRWLockExclusive(&__cxx1_interlocked);
    return was;
}
static inline LONG InterlockedExchangeAdd(LONG *p, LONG value) {
    AcquireSRWLockExclusive(&__cxx1_interlocked);
    LONG was = *p;
    *p = was + value;
    ReleaseSRWLockExclusive(&__cxx1_interlocked);
    return was;
}
static inline LONG InterlockedCompareExchange(LONG *p, LONG value, LONG comparand) {
    AcquireSRWLockExclusive(&__cxx1_interlocked);
    LONG was = *p;
    if (was == comparand) *p = value;
    ReleaseSRWLockExclusive(&__cxx1_interlocked);
    return was;
}

#ifdef __cplusplus
}
#endif

#endif
