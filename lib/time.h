// time.h - calendar time and processor time.
//
// The one header here where a **struct layout** has to be right rather than
// merely big enough, because a program declares its own 'struct tm' and hands
// the address to mktime or strftime. Get it short and the library writes past
// the end of it.
//
// C90 defines nine members. The Unix platforms add two more after them -
// tm_gmtoff and tm_zone - and the UCRT adds none, which is exactly the 56
// against 36 bytes measured on the two kinds of platform. Both are declared
// below, so a struct tm here is the same object the platform's own header
// describes.
//
// The two extra members are named rather than reserved because a program that
// reads tm_zone after localtime is doing something reasonable, and hiding the
// field would only make it declare its own struct.
//
// clock_t is the other thing that moves: eight bytes on Linux and macOS, four
// under the UCRT - and CLOCKS_PER_SEC is a million on the first two and a
// thousand on the third. A program dividing by it gets seconds on all three;
// a program assuming microseconds gets them on two.
#ifndef _CC1_TIME_H
#define _CC1_TIME_H

// Everything here is the C library, so it is named the way C names it: from C++, with C linkage.
#ifdef __cplusplus
extern "C" {
#endif


#include <stddef.h>

// Seconds since the epoch. Eight bytes on all three - the UCRT defaults
// time_t to 64 bits, which is why nothing here needs _USE_32BIT_TIME_T.
// **`long long` under the UCRT**: `long` is four bytes there (LLP64), so the
// `long` this was until 2026-10-20 handed the library a four-byte object to
// write eight into.
#ifdef _WIN32
typedef long long time_t;
#else
typedef long time_t;
#endif

#ifdef _WIN32
typedef int clock_t;
#define CLOCKS_PER_SEC 1000
#else
typedef long clock_t;
#define CLOCKS_PER_SEC 1000000
#endif

struct tm {
    int tm_sec;     // 0-60, the 60 being a leap second
    int tm_min;     // 0-59
    int tm_hour;    // 0-23
    int tm_mday;    // 1-31
    int tm_mon;     // 0-11, January being 0
    int tm_year;    // years since 1900, so 2026 is 126
    int tm_wday;    // 0-6, Sunday being 0
    int tm_yday;    // 0-365
    int tm_isdst;   // positive if daylight saving is in effect, 0 if not,
                    // negative if the answer is unknown - and mktime reads
                    // this one, so leaving it uninitialised is a real bug
#ifndef _WIN32
    // Not C90, and present because the platform's struct has them: a shorter
    // struct here would be written past by localtime.
    long tm_gmtoff;
    const char *tm_zone;
#endif
};

#ifdef _WIN32
// **The UCRT has these under their 64-bit names only**: its own header makes
// time(), localtime() and the four others inline functions over _time64 and
// its neighbours, and libucrt holds no symbol `time` - a call to the plain
// name did not link. So here they are the same inline functions - `static` alone in C90, which has no `inline`.
#ifdef __cplusplus
#define _CXX1_INLINE inline
#else
#define _CXX1_INLINE
#endif
time_t _time64(time_t *t);
double _difftime64(time_t end, time_t start);
struct tm *_localtime64(const time_t *t);
struct tm *_gmtime64(const time_t *t);
time_t _mktime64(struct tm *tm);
char *_ctime64(const time_t *t);
static _CXX1_INLINE time_t time(time_t *t) { return _time64(t); }
static _CXX1_INLINE double difftime(time_t end, time_t start) { return _difftime64(end, start); }
static _CXX1_INLINE struct tm *localtime(const time_t *t) { return _localtime64(t); }
static _CXX1_INLINE struct tm *gmtime(const time_t *t) { return _gmtime64(t); }
static _CXX1_INLINE time_t mktime(struct tm *tm) { return _mktime64(tm); }
static _CXX1_INLINE char *ctime(const time_t *t) { return _ctime64(t); }
clock_t clock(void);
char *asctime(const struct tm *tm);
#else
time_t time(time_t *t);
double difftime(time_t end, time_t start);
clock_t clock(void);

// Both return a pointer to a static object the library owns, so a second call
// overwrites the first. Copy what you need before calling again.
struct tm *localtime(const time_t *t);
struct tm *gmtime(const time_t *t);

// mktime also *normalises* what it is given - a tm_mday of 32 in January comes
// back as the 1st of February - which is the ordinary way to do date
// arithmetic in C90.
time_t mktime(struct tm *tm);

// Both write into a static buffer, and both end with a newline, which is the
// detail everybody forgets.
char *asctime(const struct tm *tm);
char *ctime(const time_t *t);
#endif

size_t strftime(char *s, size_t max, const char *fmt, const struct tm *tm);


#ifdef __cplusplus
}  // extern "C"
#endif
#endif
