// eh.h - the Microsoft C runtime's terminate and the exception in flight.
//
// Not Microsoft's header, and not a copy of one: the prototypes, which must agree
// with vcruntime's and the UCRT's, since the program links against them. Each has
// C linkage there (corecrt_terminate.h and eh.h are C headers), as here.
//
// Added 2026-11-25 for cppparse's runtime (runtime/cppparse_rt.cpp).
#ifndef _CXX1_EH_H
#define _CXX1_EH_H

#ifndef _WIN32
#error "<eh.h> is here for the x86_64-windows target only"
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*terminate_handler)(void);
typedef void (*terminate_function)(void);
void terminate(void);
terminate_handler set_terminate(terminate_handler handler);
terminate_handler _get_terminate(void);
#ifdef __cplusplus
bool __uncaught_exception(void);
int __uncaught_exceptions(void);
#endif

#ifdef __cplusplus
}
#endif

#endif
