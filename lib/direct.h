// direct.h - the Microsoft CRT's working directory and directory calls.
//
// Not Microsoft's header, and not a copy of one: the prototypes, which are
// ordinary C and must agree with the CRT's, since the program links against
// it. The underscore names are the exported ones; the plain names (getcwd,
// mkdir) the CRT keeps only through oldnames.lib, and a program built here
// does not link that, so they are not declared.
//
// It is a Windows header and nothing else declares these, as <io.h>.
//
// Added 2026-10-20 for cppparse, whose driver asks for the working directory.
#ifndef _CXX1_DIRECT_H
#define _CXX1_DIRECT_H

#ifdef __cplusplus
extern "C" {
#endif

// The working directory into buf, at most size bytes with its terminator, and
// buf; or 0 when it does not fit. A null buf is allocated with malloc.
char *_getcwd(char *buf, int size);
int _chdir(const char *path);
int _mkdir(const char *path);
int _rmdir(const char *path);

#ifdef __cplusplus
}
#endif

#endif
