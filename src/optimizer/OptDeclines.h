#pragma once

// **Where the optimizer holds back, said aloud when CPP11_DECLINES is set**: one stderr line per function or
// call site a transformation declines, `decline <reason> <symbol>`. Unset it costs one cached getenv; nothing
// emitted depends on it. **Empty or 0 is unset** (1.10, 2026-10-20): IDEs pass `CPP11_DECLINES=` for off.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace opt {

inline bool reportingDeclines() {
    static const bool on = [] {
        const char *v = std::getenv("CPP11_DECLINES");
        return v != nullptr && *v != '\0' && std::strcmp(v, "0") != 0;
    }();
    return on;
}

inline void noteDecline(const char *reason, const std::string &where) {
    if (reportingDeclines()) std::fprintf(stderr, "decline %s %s\n", reason, where.c_str());
}

}
