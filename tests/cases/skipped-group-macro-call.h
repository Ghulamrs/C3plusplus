// Included twice by skipped-group-macro-call.cpp: the second time this whole
// file is a skipped group, and the check below is a call spanning two lines -
// the shape of an assert, without <cassert>, which the cross targets lack.
#ifndef SKIPPED_GROUP_MACRO_CALL_H
#define SKIPPED_GROUP_MACRO_CALL_H
#define REQUIRE(cond, why) ((void)(cond), (void)(why))
inline int bump(int n) {
    REQUIRE(n >= 0,
            "bump: n must not be negative");
    return n + 1;
}
#endif
