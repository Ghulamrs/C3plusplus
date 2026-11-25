// intrin.h - the part of Microsoft's intrinsics a program here has asked for.
//
// Not Microsoft's header. cl makes each of these an instruction, and no library
// exports them on x64; this compiler has no such instruction to give, so each is
// an inline function that takes a slim reader/writer lock around the
// read-modify-write, as <windows.h>'s InterlockedIncrement family does (its
// comment says what that is atomic against: the other operations of one unit).
// The SDK's parameter is `long volatile *`, which this compiler refuses by name
// (a pointer to a volatile type); it is `long *`, as <windows.h>'s family is.
//
// `_AddressOfReturnAddress` and `_ReturnAddress` are not declared: they read the
// caller's frame, which only the compiler can give, and this one does not yet -
// a use is an undeclared name, not a call that links to nothing.
//
// Added 2026-11-25 for cppparse's runtime (runtime/cppparse_rt.cpp).
#ifndef _CXX1_INTRIN_H
#define _CXX1_INTRIN_H

#ifndef _WIN32
#error "<intrin.h> is here for the x86_64-windows target only"
#endif

#ifdef __cplusplus
extern "C" {
#endif

#ifndef _CXX1_RTL_SRWLOCK
#define _CXX1_RTL_SRWLOCK
// One pointer, zero when free; SRWLOCK_INIT is that zero (kernel32's, as <windows.h> declares it).
typedef struct _RTL_SRWLOCK {
    void *Ptr;
} SRWLOCK, *PSRWLOCK;
#endif
void AcquireSRWLockExclusive(PSRWLOCK lock);
void ReleaseSRWLockExclusive(PSRWLOCK lock);

static SRWLOCK __cxx1_intrinsic_lock;
static inline long _InterlockedIncrement(long *p) {
    AcquireSRWLockExclusive(&__cxx1_intrinsic_lock);
    long now = ++*p;
    ReleaseSRWLockExclusive(&__cxx1_intrinsic_lock);
    return now;
}
static inline long _InterlockedDecrement(long *p) {
    AcquireSRWLockExclusive(&__cxx1_intrinsic_lock);
    long now = --*p;
    ReleaseSRWLockExclusive(&__cxx1_intrinsic_lock);
    return now;
}
static inline long _InterlockedExchange(long *p, long value) {
    AcquireSRWLockExclusive(&__cxx1_intrinsic_lock);
    long was = *p;
    *p = value;
    ReleaseSRWLockExclusive(&__cxx1_intrinsic_lock);
    return was;
}
static inline long _InterlockedExchangeAdd(long *p, long value) {
    AcquireSRWLockExclusive(&__cxx1_intrinsic_lock);
    long was = *p;
    *p = was + value;
    ReleaseSRWLockExclusive(&__cxx1_intrinsic_lock);
    return was;
}
static inline long _InterlockedCompareExchange(long *p, long value, long comparand) {
    AcquireSRWLockExclusive(&__cxx1_intrinsic_lock);
    long was = *p;
    if (was == comparand) *p = value;
    ReleaseSRWLockExclusive(&__cxx1_intrinsic_lock);
    return was;
}

#ifdef __cplusplus
}
#endif

#endif
