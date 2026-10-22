// lib/windows.h's structs as the x64 SDK lays them out: the system fills them,
// so a member at the wrong offset is a wrong answer. The same asserts compile
// with cl against the real SDK, which is what they were checked by.
#include <windows.h>
#include <stddef.h>
#include <stdio.h>

static_assert(sizeof(STARTUPINFOA) == 104, "STARTUPINFOA");
static_assert(offsetof(STARTUPINFOA, dwFlags) == 60, "dwFlags");
static_assert(offsetof(STARTUPINFOA, wShowWindow) == 64, "wShowWindow");
static_assert(offsetof(STARTUPINFOA, lpReserved2) == 72, "lpReserved2");
static_assert(offsetof(STARTUPINFOA, hStdInput) == 80, "hStdInput");
static_assert(offsetof(STARTUPINFOA, hStdError) == 96, "hStdError");
static_assert(sizeof(PROCESS_INFORMATION) == 24, "PROCESS_INFORMATION");
static_assert(offsetof(PROCESS_INFORMATION, dwThreadId) == 20, "dwThreadId");
static_assert(sizeof(WIN32_FIND_DATAA) == 320, "WIN32_FIND_DATAA");
static_assert(offsetof(WIN32_FIND_DATAA, nFileSizeHigh) == 28, "nFileSizeHigh");
static_assert(offsetof(WIN32_FIND_DATAA, cFileName) == 44, "cFileName");
static_assert(offsetof(WIN32_FIND_DATAA, cAlternateFileName) == 304, "cAlternateFileName");
static_assert(sizeof(SECURITY_ATTRIBUTES) == 24, "SECURITY_ATTRIBUTES");
static_assert(sizeof(SYSTEM_INFO) == 48, "SYSTEM_INFO");
static_assert(MAX_PATH == 260, "MAX_PATH");

int main() {
    STARTUPINFOA si;
    ZeroMemory(&si, sizeof si);
    si.cb = sizeof si;
    printf("%u %d\n", (unsigned)si.cb, GetCurrentProcessId() != 0);
    return 0;
}
