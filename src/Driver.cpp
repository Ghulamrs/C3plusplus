#include "Driver.h"
#include "Name.h"
#include "Version.h"
#include "backend/X86_64Windows.h"
#include "backend/CoffEmitter.h"
#include "encoder/X64.h"

#include "backend/Backend.h"
#include "Lexer.h"
#include "parser/Parser.h"
#include "sema/Typing.h"
#include "sema/Adapter.h"
#include "ir/Lowering.h"
#include "AstDump.h"
#include "Preprocessor.h"
#include "Source.h"
#include "Type.h"

#ifdef _WIN32
#include <windows.h>
#endif

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <sstream>
#include <thread>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <direct.h>
#else
#include <unistd.h>
#include <sys/stat.h>
#include <cerrno>
#include <glob.h>
#include <spawn.h>
#include <sys/wait.h>
#endif

#ifdef __linux__
#include <sched.h>
#endif

// **Where the running program is**, which is how a *released* compiler finds
// the headers it ships with: see standardIncludeDirectories(). One call each,
// and the fallback below needs none of them.
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

// **The environment a spawned tool inherits.** Apple gives an executable its
// `environ` through this accessor rather than as a symbol; everything else
// declares the variable.
#ifndef _WIN32
#if defined(__APPLE__)
#include <crt_externs.h>
#define CXX1_ENVIRON (*_NSGetEnviron())
#else
extern char **environ;
#define CXX1_ENVIRON environ
#endif
#endif

namespace {

const std::size_t kThreadFrom = 4;

}

#ifndef CXX1_INCLUDE_DIR
#define CXX1_INCLUDE_DIR ""
#endif
// **The C++ headers are a second directory, not more files in the first.**
// `include/` holds `<cstddef>` and `<vector>`; `lib/` holds the C headers
// those wrap, and a program may reach either.
#ifndef CXX1_CXX_INCLUDE_DIR
#define CXX1_CXX_INCLUDE_DIR ""
#endif

// **The line every run of this compiler prints**, and the one thing in the output that is not about the program being compiled.
const char *Driver::bannerLine() { return CXX1_BANNER; }

// **The directory the running program is in.** A released compiler is unpacked
// somewhere nobody chose at build time, so the headers it ships with cannot be
// found by a path compiled into it - they are found *beside it*.
static std::string programDirectory(const std::string &argv0) {
    std::string full;
#if defined(_WIN32)
    char buf[4096];
    DWORD n = GetModuleFileNameA(nullptr, buf, sizeof(buf));
    if (n > 0 && n < sizeof(buf)) full.assign(buf, n);
#elif defined(__APPLE__)
    char buf[4096];
    uint32_t n = sizeof(buf);
    if (_NSGetExecutablePath(buf, &n) == 0) {
        char real[4096];
        full = realpath(buf, real) != nullptr ? real : buf;
    }
#elif defined(__linux__)
    char buf[4096];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n > 0) full.assign(buf, static_cast<std::size_t>(n));
#endif
    if (full.empty()) full = argv0;
    std::size_t cut = full.find_last_of("/\\");
    if (cut == std::string::npos) return std::string(".");
    return full.substr(0, cut);
}

static bool directoryHas(const std::string &dir, const char *name) {
    std::ifstream probe((dir + "/" + name).c_str());
    return probe.good();
}

// **Where the standard headers are, asked in the order a release wants.**
void Driver::standardIncludeDirectories(const std::string &argv0) {
    const char *envCxx = std::getenv(program::env("INCLUDE").c_str());
    const char *envC = std::getenv(program::env("LIB").c_str());
    if (envCxx != nullptr && envCxx[0] != '\0') {
        searchPath_.push_back(envCxx);
        if (envC != nullptr && envC[0] != '\0') searchPath_.push_back(envC);
        return;
    }

    // Beside the binary or one directory up. include/ is this compiler's when it
    // holds `cstddef`, a name only this library spells; the C headers <cstddef>
    // reaches are there too (an installation) or in lib/ (a checkout, or c90's).
    const std::string here = programDirectory(argv0);
    const std::string candidates[2] = { here, here + "/.." };
    for (const std::string &at : candidates) {
        const std::string cxxDir = at + "/include";
        const std::string cDir = at + "/lib";
        if (!directoryHas(cxxDir, "cstddef")) continue;
        searchPath_.push_back(cxxDir);
        if (!directoryHas(cxxDir, "stddef.h") && directoryHas(cDir, "stddef.h"))
            searchPath_.push_back(cDir);
        return;
    }

    if (CXX1_CXX_INCLUDE_DIR[0] != '\0')
        searchPath_.push_back(CXX1_CXX_INCLUDE_DIR);
    if (CXX1_INCLUDE_DIR[0] != '\0') searchPath_.push_back(CXX1_INCLUDE_DIR);
}

void Driver::usage(char *file) {
    std::fprintf(stderr,
        "usage: %s <file.cpp|file.c> [more ...] [-S|-c] [-o out] [-D n[=v]] [-U n] [-std=s]\n"
        "               [-I dir] [-j n] [-arch a] [-masm=m] [-rts=r] [-O0|-O1|-O2|-Os] [-g] [-time]\n"
        "       a .c is read as C90 and anything else as C++11; -std=c90 (c89, gnu89)\n"
        "         or -std=c++11 names the language for every input instead\n"
        "       with neither -S nor -c the inputs are compiled, assembled and\n"
        "         linked into a program, named by -o, or a.out - a.exe on a\n"
        "         Windows host; several inputs\n"
        "         link together\n"
        "       -c stops at one object file per input, named by -o or after the\n"
        "         input, in the current directory\n"
        "       -S stops after this compiler and writes assembly instead: one .s\n"
        "         per input, or -o to name the output of a single one\n"
        "       -D defines a macro, '-DN' meaning '-DN=1'; -U removes one, and\n"
        "         either may name one of the target's own\n"
        "       -I adds a directory to the ones <...> searches\n"
        "       -j sets how many files are compiled at once; -j 1 is serial\n"
        "       -arch picks the architecture the code is generated for - one of\n"
        "         x86_64-linux, x86_64-windows, arm64-darwin, tms6747; the host\n"
        "         by default, and another host's only reaches -S, since the\n"
        "         assembler here is this machine's; tms6747 is assembled on any\n"
        "         host by asm6x, the project's own C6000 assembler (CPP11_AS, or\n"
        "         the one beside this program), and linked into a .out by TI's\n"
        "         lnk6x where CCS is (CPP11_TI names its C6000 compiler directory,\n"
        "         CPP11_TILIB one holding rts6740_elf_eh.lib); its code uses the\n"
        "         C64x+ 16-bit compact instructions, as TI's cl6x does, and\n"
        "         --no_compress writes every instruction in 32 bits\n"
        "       -rts= picks the tms6747 run-time library: a directory holding\n"
        "         RTS6x's rts6x.lib (or the .lib itself), or 'ti' for TI's\n"
        "         rts6740. Unsaid: RTS6x from RTS6X or lib/rts6x-tms6747\n"
        "         beside this program - as RIDE links - unless CPP11_TI or\n"
        "         CPP11_TILIB names TI's; TI's otherwise; the link says which\n"
        "       -masm picks the assembly syntax for x86_64-windows: 'masm', the\n"
        "         default, is assembled by this compiler itself - the project's\n"
        "         masm as a library, so -c writes the object on any host - and\n"
        "         linked by the project's link.exe, beside this program (RIDE's\n"
        "         bin), named by CPP11_LD, or on PATH - COMDAT and all; 'ml64' is\n"
        "         the MASM syntax without COMDAT, for Visual Studio's ml64,\n"
        "         which links one translation unit at a time; 'gnu' is\n"
        "         assembled by clang\n"
        "       -O1 and -O2 improve the code of each function: frame slots and\n"
        "         constants forwarded, pushes paired with their pops, dead\n"
        "         instructions removed. Every spelling; -O0 is the default\n"
        "       -Os is -O1 choosing the smaller code where the two differ:\n"
        "         on tms6747 a division by a constant stays a call, no\n"
        "         entry test is duplicated ahead of the frame; elsewhere -O1\n"
        "       -g writes a line table, so a debugger can stop on a line of C++\n"
        "         and step through it: DWARF on x86_64-linux and arm64-darwin,\n"
        "         CodeView on x86_64-windows (linked with /debug into a PDB) -\n"
        "         for clang in the gnu spelling, for the project's masm as the\n"
        "         .FILE and .LINE directives it reads with -masm=masm - and cl6x's\n"
        "         .dwpsn and .dwtag on tms6747; refused for -masm=ml64, and on\n"
        "         tms6747 with -O\n"
        "       -nologo leaves out the line this compiler prints before it\n"
        "         starts, which a build script may not want\n"
        "       -version says which release this is, and which seal file\n"
        "         carries the CRC32 of the sources it was built from\n"
        "       -time reports how long each phase took\n"
        "       -dump=syntax writes the syntax tree the grammar recorded, a root a\n"
        "         line, on stdout; one input at a time; -dump=ast each function's\n"
        "         Ast.h body, a node a line; -dump=mir each x86 function's MIR\n"
        "         after the pipeline, -dump=mir=<pass> after that pass\n"
        "       -front=ssa lowers each function the SSA lowering reaches to the SSA IR\n"
        "         and selects it from there (x86 only); -dump=ssa writes the IR, -check=ssa\n"
        "         verifies it and reports on stdout\n"
        "       -front=typed builds each function the adapter reaches from Sema's typed\n"
        "         tree; -check=adapted holds that against the parser's, on stdout\n"
        "       -check=typed types the syntax tree in Sema and holds each type against\n"
        "         the parser's, on stdout; it fails where any differs\n"
        "       -keep-going[=N] reports up to N errors a file (100 if no N) and goes\n"
        "         on from the next declaration after each; the file still fails\n", file);
}

// This process's id, which keeps two runs in one directory from choosing one temporary name.
static unsigned long processId() {
#ifdef _WIN32
    return GetCurrentProcessId();
#else
    return static_cast<unsigned long>(getpid());
#endif
}

static std::string workingDirectory() {
    char buf[4096];
#ifdef _WIN32
    if (_getcwd(buf, sizeof buf) == nullptr) return std::string();
#else
    if (getcwd(buf, sizeof buf) == nullptr) return std::string();
#endif
    return std::string(buf);
}

static bool hostIsWindows() {
    return std::strcmp(defaultBackend().name(), "x86_64-windows") == 0;
}

#ifdef _WIN32
// vswhere's answer, fetched through a temporary file rather than a pipe. cc1 is
// itself run through a pipe by the editor and a nested _popen fails where the
// parent's stdio are not consoles, so this found Visual Studio everywhere else.
static std::string askVswhere() {
    char temp[MAX_PATH];
    char folder[MAX_PATH];
    if (GetTempPathA(MAX_PATH, folder) == 0) return std::string();
    if (GetTempFileNameA(folder, program::kName, 0, temp) == 0) return std::string();

    std::string command =
        "\"\"C:\\Program Files (x86)\\Microsoft Visual Studio\\Installer\\vswhere.exe\""
        " -latest -products * -property installationPath > \"";
    command += temp;
    command += "\"\"";
    std::string found;
    if (std::system(command.c_str()) == 0) {
        std::ifstream answer(temp);
        std::getline(answer, found);
    }
    std::remove(temp);
    while (!found.empty() && (found.back() == '\n' || found.back() == '\r')) {
        found.pop_back();
    }
    return found;
}

static std::string findVcvars() {
    std::string root = askVswhere();
    if (!root.empty()) {
        const std::string bat = root + "\\VC\\Auxiliary\\Build\\vcvars64.bat";
        std::ifstream there(bat.c_str());
        if (there) return bat;
    }
    // vswhere is itself part of an installation and can be absent. The
    // default places are worth trying before giving up on a machine that
    // plainly has the tools.
    static const char *const roots[] = {
        "C:\\Program Files\\Microsoft Visual Studio\\2022\\",
        "C:\\Program Files (x86)\\Microsoft Visual Studio\\2019\\"
    };
    static const char *const editions[] = {
        "Community", "Professional", "Enterprise", "BuildTools"
    };
    for (std::size_t r = 0; r < sizeof roots / sizeof roots[0]; ++r) {
        for (std::size_t e = 0; e < sizeof editions / sizeof editions[0]; ++e) {
            const std::string bat = std::string(roots[r]) + editions[e] +
                                    "\\VC\\Auxiliary\\Build\\vcvars64.bat";
            std::ifstream there(bat.c_str());
            if (there) return bat;
        }
    }
    return std::string();
}
#endif

// ml64 and link are on PATH only inside a Developer Command Prompt, which an
// editor launched from Explorer is not - so where they are unreachable the
// command runs in a shell that sourced vcvars64.bat, which sets LIB as well.
static std::string developerShell() {
#ifdef _WIN32
    const char *inside = std::getenv("VCToolsInstallDir");
    if (inside != nullptr && inside[0] != '\0') return std::string();
    static const std::string cached = findVcvars();   // asked from the tool pool: a static's initialiser is thread-safe
    return cached;
#else
    return std::string();
#endif
}

// **Per-thread, because the assembler runs on a pool now.**
static thread_local std::string lastToolCommand;

// The program a command starts: its first word, without the quotes around it.
static std::string programOf(const std::string &command) {
    const std::string::size_type start = command.find_first_not_of("\" ");
    if (start == std::string::npos) return std::string();
    const std::string::size_type end = command.find_first_of(command[0] == '"' ? "\"" : " ", start);
    return command.substr(start, end == std::string::npos ? end : end - start);
}

#ifdef _WIN32
// **Started directly, not through cmd.exe** (1.10, 2026-10-20). A machine may have cmd disabled by policy,
// as GRIDE's does: every cpp11 -c and link through cmd failed, and an IDE that starts cpp11 had no cmd to lend.
// The command line is the shell's: the program's quoted path, then its arguments, as CreateProcess reads it.
static int runDirect(const std::string &command) {
    STARTUPINFOA si;
    ZeroMemory(&si, sizeof si);
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof pi);
    std::vector<char> line(command.begin(), command.end());
    line.push_back('\0');
    if (!CreateProcessA(nullptr, line.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &si, &pi)) return -1;
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return static_cast<int>(code);
}
#endif

// **posix_spawn rather than system(), because system() serialises on macOS.**
static int runShell(const std::string &command) {
#ifdef _WIN32
    return runDirect(command);
#else
    const char *argv[] = { "/bin/sh", "-c", command.c_str(), nullptr };
    pid_t pid = 0;
    if (posix_spawn(&pid, "/bin/sh", nullptr, nullptr,
                    const_cast<char *const *>(argv), CXX1_ENVIRON) != 0)
        return -1;
    int status = 0;
    while (waitpid(pid, &status, 0) < 0)
        if (errno != EINTR) return -1;
    return status;
#endif
}

// Runs one tool, inside a developer environment where it needs one: a tool named by its path (GRIDE's
// own masm and link) is started as it is; only a bare name - Visual Studio's ml64 and link, which reach
// PATH through vcvars64.bat - runs from a batch file that calls vcvars first, and that takes cmd.
static int runTool(const std::string &command) {
    lastToolCommand = command;
    const bool byPath = programOf(command).find_first_of("/\\") != std::string::npos;
    const std::string vcvars = byPath ? std::string() : developerShell();
    if (vcvars.empty()) return runShell(command);

#ifdef _WIN32
    char folder[MAX_PATH];
    char script[MAX_PATH];
    if (GetTempPathA(MAX_PATH, folder) == 0) return runShell(command);
    if (GetTempFileNameA(folder, program::kName, 0, script) == 0) {
        return runShell(command);
    }
    // **The reserved name is kept until the script is done**: removed first, a
    // second cpp11 running at once was handed it and overwrote this script.
    std::string batch = script;
    batch += ".cmd";

    {
        std::ofstream out(batch.c_str());
        if (!out) { std::remove(script); return runShell(command); }
        out << "@echo off\n";
        out << "call \"" << vcvars << "\" >nul 2>&1\n";
        out << command << "\n";
    }
    lastToolCommand = command + "   [inside " + vcvars + "]";
    const int rc = std::system(("\"" + batch + "\"").c_str());
    std::remove(batch.c_str());
    std::remove(script);
    return rc;
#else
    return runShell(command);
#endif
}

// When this cpp11 was compiled, DD-MM-YYYY HH:MM:SS as the seal date is written.
static std::string buildStamp() {
    static const char months[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
    const char *d = __DATE__;   // "Oct  2 2026"
    const int month = static_cast<int>((std::strstr(months, std::string(d, 3).c_str()) - months) / 3) + 1;
    char out[32];
    std::snprintf(out, sizeof out, "%02d-%02d-%s %s", std::atoi(d + 4), month, d + 7, __TIME__);
    return out;
}

// Only where the tool was asked for by name, as Visual Studio's ml64 and link are: a tool named by
// its path - RIDE's own masm and link - was found and ran, and its messages above are the answer.
static void noteWindowsToolchain(const std::string &command) {
    if (!hostIsWindows()) return;
    if (programOf(command).find_first_of("/\\") != std::string::npos) return;
    std::fprintf(stderr, "  ml64 and link ship with Visual Studio and reach "
                         "PATH only after vcvars64.bat has run - a Developer "
                         "Command Prompt is that same environment.\n");
}

// **`c++` and not `cc`, because operator new lives in the C++ runtime.** The
// four allocation operators are in libc++ or libstdc++ and the C driver links
// neither; `c++` assembles a .s exactly as `cc` does, and rung 6 wants it too.
const char *Driver::hostCompiler() {
    const char *env = std::getenv(program::env("CC").c_str());
    return (env != nullptr && env[0] != '\0') ? env : "c++";
}

const char *Driver::hostAssembler(Syntax syntax) {
    const char *env = std::getenv(program::env("AS").c_str());
    if (env != nullptr && env[0] != '\0') return env;
    return syntax == Syntax::Ml64 ? "ml64.exe" : "masm.exe";
}

// **clang, and it is asked for the Microsoft target explicitly.** Its default
// target is whatever it was built for; this has to produce COFF for the linker
// beside it, and the triple is what says so.
const char *Driver::hostGnuAssembler() {
    static std::string found;
    static bool searched = false;
    if (searched) return found.c_str();
    searched = true;

    const char *env = std::getenv(program::env("AS").c_str());
    if (env != nullptr && env[0] != '\0') { found = env; return found.c_str(); }

#ifdef _WIN32
    // **Wherever Visual Studio put its clang, in any edition** - the "C++ Clang
    // tools" component - then a standalone LLVM, then PATH.
    std::vector<std::string> tries;
    const std::string llvm = "\\VC\\Tools\\Llvm\\x64\\bin\\clang.exe";
    const char *vc = std::getenv("VCINSTALLDIR");
    if (vc != nullptr && vc[0] != '\0')
        tries.push_back(std::string(vc) + "Tools\\Llvm\\x64\\bin\\clang.exe");
    const std::string vs = askVswhere();
    if (!vs.empty()) tries.push_back(vs + llvm);
    const char *pf = std::getenv("ProgramFiles");
    if (pf != nullptr && pf[0] != '\0') {
        static const char *const editions[] = {
            "Community", "Professional", "Enterprise", "BuildTools"
        };
        for (const char *e : editions)
            tries.push_back(std::string(pf) + "\\Microsoft Visual Studio\\2022\\" + e + llvm);
        tries.push_back(std::string(pf) + "\\LLVM\\bin\\clang.exe");
    }
    char onPath[MAX_PATH];
    if (SearchPathA(nullptr, "clang.exe", nullptr, MAX_PATH, onPath, nullptr) > 0)
        tries.push_back(onPath);
    for (std::size_t i = 0; i < tries.size(); i++) {
        std::ifstream probe(tries[i].c_str());
        if (probe.good()) { found = tries[i]; return found.c_str(); }
    }
    return found.c_str();   // "" - none here: the caller falls back to masm, or says so
#else
    found = "clang";
    return found.c_str();
#endif
}

const char *Driver::hostLinker() {
    const char *env = std::getenv(program::env("LD").c_str());
    return (env != nullptr && env[0] != '\0') ? env : "link.exe";
}

bool Driver::targetIsTi() const {
    return std::strcmp(backend_->name(), "tms6747") == 0;
}

// **A file name with `*` or `?` in it, expanded here**, as cl does: cmd hands the pattern through as
// written, and a POSIX shell only when it was quoted. Sorted; a file already named is not taken twice.
static bool expandPattern(const std::string &arg, std::vector<std::string> &into) {
    if (arg.find_first_of("*?") == std::string::npos || std::ifstream(arg.c_str()).good()) {
        if (std::find(into.begin(), into.end(), arg) == into.end()) into.push_back(arg);
        return true;
    }
    std::vector<std::string> found;
#ifdef _WIN32
    const std::size_t slash = arg.find_last_of("/\\");
    const std::string dir = slash == std::string::npos ? std::string() : arg.substr(0, slash + 1);
    WIN32_FIND_DATAA entry;
    HANDLE h = FindFirstFileA(arg.c_str(), &entry);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (!(entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) found.push_back(dir + entry.cFileName);
        } while (FindNextFileA(h, &entry));
        FindClose(h);
    }
    std::sort(found.begin(), found.end());
#else
    glob_t g;
    if (glob(arg.c_str(), 0, nullptr, &g) == 0)
        for (std::size_t k = 0; k < g.gl_pathc; k++) found.push_back(g.gl_pathv[k]);
    globfree(&g);
#endif
    for (const std::string &name : found)
        if (std::find(into.begin(), into.end(), name) == into.end()) into.push_back(name);
    return !found.empty();
}

// **Two jobs never write one file**: -j runs them at once, and dir1/*.cpp and dir2/*.cpp may each
// hold an x.cpp whose object is x.o. Refused by name before any job starts.
static bool distinctOutputs(const char *program, const std::vector<std::string> &inputs,
                            const std::vector<std::string> &outputs) {
    for (std::size_t i = 0; i < outputs.size(); i++)
        for (std::size_t k = 0; k < i; k++)
            if (!outputs[i].empty() && outputs[i] == outputs[k]) {
                std::fprintf(stderr, "%s: %s and %s would both be written to %s\n", program,
                             inputs[k].c_str(), inputs[i].c_str(), outputs[i].c_str());
                return false;
            }
    return true;
}

// A file beside this program - RIDE lays asm6x.exe beside cpp11.exe - or
// nothing, when argv[0] was a bare name found on PATH.
static std::string besideProgram(const std::string &program, const char *leaf) {
    std::size_t slash = program.find_last_of("/\\");
    if (slash == std::string::npos) return std::string();
    std::string path = program.substr(0, slash + 1) + leaf;
    std::ifstream probe(path.c_str());
    return probe.good() ? path : std::string();
}

// **The project's own masm and LINK, beside this program** - RIDE lays them beside cpp11.exe - for
// the MASM spelling, unless CPP11_AS or CPP11_LD names another; else the names, on PATH.
std::string Driver::masmAssembler() const {
    const char *env = std::getenv(program::env("AS").c_str());
    if (env != nullptr && env[0] != '\0') return env;
    std::string own = syntax_ == Syntax::Masm ? besideProgram(program_, "masm.exe") : std::string();
    return own.empty() ? std::string(hostAssembler(syntax_)) : own;
}

// The MASM spelling of x86_64-windows is assembled by CoffEmitter, the library masm itself is a command line
// over, so the object is byte-equal with -S + masm and no assembler is run; ml64 and gnu keep their tools.
bool Driver::assemblesInProcess() const {
    return std::strcmp(backend_->name(), "x86_64-windows") == 0 && syntax_ == Syntax::Masm;
}

// The object beside an assembly file: its name with .obj, which is what link()'s per-file step always wrote.
std::string Driver::objectBeside(const std::string &asmPath) {
    std::size_t dot = asmPath.rfind('.');
    return (dot == std::string::npos ? asmPath : asmPath.substr(0, dot)) + ".obj";
}

std::string Driver::windowsLinker() const {
    const char *env = std::getenv(program::env("LD").c_str());
    if (env != nullptr && env[0] != '\0') return env;
    std::string own = syntax_ == Syntax::Masm ? besideProgram(program_, "link.exe") : std::string();
#ifdef _WIN32
    // **-g in the gnu spelling wants a PDB, which Microsoft's link writes and the project's does not yet**: the
    // one on PATH (vcvars puts it there), looked up by path, since a bare name finds the one beside this program.
    const char *path = std::getenv("PATH");
    char found[MAX_PATH];
    if (debug_ && syntax_ == Syntax::Gnu && path != nullptr &&
        SearchPathA(path, "link.exe", nullptr, MAX_PATH, found, nullptr) > 0)
        return found;
#endif
    return own.empty() ? std::string(hostLinker()) : own;
}

// **asm6x, the project's C6000 assembler, which runs on every host.** CPP11_AS
// names another; else the one beside this program; else the name, on PATH.
std::string Driver::tiAssembler() const {
    const char *env = std::getenv(program::env("AS").c_str());
    if (env != nullptr && env[0] != '\0') return env;
    std::string found = besideProgram(program_, "asm6x.exe");
    if (found.empty()) found = besideProgram(program_, "asm6x");
    return found.empty() ? std::string("asm6x") : found;
}

// **TI's linker, which only a machine with CCS has.** CPP11_LD names it; else
// CPP11_TI names the C6000 compiler directory, the one with bin\lnk6x and
// lib\rts6740_elf.lib; else lnk6x is asked for by name.
std::string Driver::tiLinker() const {
    const char *env = std::getenv(program::env("LD").c_str());
    if (env != nullptr && env[0] != '\0') return env;
    const char *ti = std::getenv(program::env("TI").c_str());
    if (ti != nullptr && ti[0] != '\0') {
        std::string dir = std::string(ti) + (hostIsWindows() ? "\\bin\\" : "/bin/");
        std::ifstream exe((dir + "lnk6x.exe").c_str());
        if (exe.good()) return dir + "lnk6x.exe";
        return dir + "lnk6x";
    }
    std::string own = besideProgram(program_, "lnk6x.exe");
    if (own.empty()) own = besideProgram(program_, "lnk6x");
    return own.empty() ? std::string("lnk6x") : own;
}

// The per-job folders the temporaries sit in, removed once their files are.
static std::vector<std::string> &temporaryFolders() {
    static std::vector<std::string> folders;
    return folders;
}

static void removeFolders() {
    std::vector<std::string> &folders = temporaryFolders();
#ifdef _WIN32
    for (const std::string &d : folders) _rmdir(d.c_str());
#else
    for (const std::string &d : folders) rmdir(d.c_str());
#endif
    folders.clear();
}

// **A temporary named after its source**, in a folder of its own: the assembler records its input's
// name in the object, so isalpha.cpp assembles as isalpha.s and two builds give the same bytes.
std::string Driver::temporaryName(int index, const std::string &source) {
    std::string stem = source;
    std::size_t slash = stem.find_last_of(hostIsWindows() ? "/\\" : "/");
    if (slash != std::string::npos) stem = stem.substr(slash + 1);
    std::size_t dot = stem.rfind('.');
    if (dot != std::string::npos) stem = stem.substr(0, dot);
    std::string name = temporaryName(index);
    if (stem.empty()) return name;
    std::string folder = name.substr(0, name.size() - 2);
#ifdef _WIN32
    if (_mkdir(folder.c_str()) != 0) return name;
#else
    if (mkdir(folder.c_str(), 0700) != 0) return name;
#endif
    temporaryFolders().push_back(folder);
    return folder + (hostIsWindows() ? "\\" : "/") + stem + ".s";
}

std::string Driver::temporaryName(int index) {
    const char *dir = std::getenv("TMPDIR");
    if (dir == nullptr || dir[0] == '\0') dir = std::getenv("TEMP");
    if (dir == nullptr || dir[0] == '\0') dir = std::getenv("TMP");
    std::string base = (dir != nullptr && dir[0] != '\0') ? dir : "/tmp";
    while (!base.empty() && (base[base.size() - 1] == '/' ||
                             base[base.size() - 1] == '\\'))
        base.erase(base.size() - 1);
    return base + (hostIsWindows() ? "\\" : "/") + program::kName + "-" +
           std::to_string(static_cast<long>(processId())) + "-" +
           std::to_string(index) + ".s";
}

static std::vector<std::string> &temporaryNames() {
    static std::vector<std::string> names;
    return names;
}

static std::string shellQuote(const std::string &s);

#ifdef _WIN32
// The first line of a text file, its line ending and trailing blanks taken off; empty if there is none.
static std::string firstLineOf(const std::string &path) {
    std::ifstream in(path.c_str());
    std::string line;
    std::getline(in, line);
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
    return line;
}

// **/LIBPATH for a linker started without vcvars** (named by its path, so started directly): with no LIB set,
// the directories come off the installation - Visual Studio's default tools version and the SDK's manifest -
// so the project's LINK finds libcmt from a plain command line too, not only from GRIDE, which sets LIB.
static std::string installationLibPaths() {
    const char *lib = std::getenv("LIB");
    if (lib != nullptr && lib[0] != '\0') return std::string();
    static const std::string paths = [] {
        std::vector<std::string> dirs;
        const std::string vcvars = findVcvars();
        const std::string::size_type cut = vcvars.find("\\VC\\Auxiliary\\Build\\");
        if (cut != std::string::npos) {
            const std::string root = vcvars.substr(0, cut);
            const std::string tools = firstLineOf(root + "\\VC\\Auxiliary\\Build\\Microsoft.VCToolsVersion.default.txt");
            if (!tools.empty()) dirs.push_back(root + "\\VC\\Tools\\MSVC\\" + tools + "\\lib\\x64");
        }
        // SDKManifest.xml names the installed SDK as `PlatformIdentity = "UAP, Version=10.0.26100.0"`.
        const std::string kits = "C:\\Program Files (x86)\\Windows Kits\\10";
        std::ifstream manifest((kits + "\\SDKManifest.xml").c_str());
        std::string line, sdk;
        while (sdk.empty() && std::getline(manifest, line)) {
            const std::string::size_type v = line.find("Version=");
            if (v == std::string::npos) continue;
            const std::string::size_type end = line.find('"', v + 8);
            sdk = line.substr(v + 8, end == std::string::npos ? std::string::npos : end - v - 8);
        }
        if (!sdk.empty()) {
            dirs.push_back(kits + "\\Lib\\" + sdk + "\\ucrt\\x64");
            dirs.push_back(kits + "\\Lib\\" + sdk + "\\um\\x64");
        }
        std::string out;
        for (const std::string &d : dirs) out += " /LIBPATH:" + shellQuote(d);
        return out;
    }();
    return paths;
}
#endif

void Driver::removeTemporaries() {
    std::vector<std::string> &names = temporaryNames();
    for (const std::string &t : names) std::remove(t.c_str());
    names.clear();
    removeFolders();
    temporaries_.clear();
}

static std::string shellQuote(const std::string &s) {
    if (hostIsWindows()) {
        std::string out = "\"";
        for (char c : s) {
            if (c == '"') out += "\\\"";
            else          out += c;
        }
        return out + "\"";
    }
    std::string out = "'";
    for (char c : s) {
        if (c == '\'') out += "'\\''";
        else           out += c;
    }
    return out + "'";
}

void Driver::addMacroEdit(const char *text, bool undef) {
    std::string s(text);
    std::size_t eq = s.find('=');
    if (undef || eq == std::string::npos)
        macroEdits_.push_back(MacroEdit{ s, undef ? "" : "1", undef });
    else
        macroEdits_.push_back(MacroEdit{ s.substr(0, eq), s.substr(eq + 1), false });
}

std::vector<std::pair<std::string, std::string> > Driver::macrosFor(Dialect dialect) const {
    std::vector<std::pair<std::string, std::string> > macros =
        predefinedMacros(*backend_, dialect);

    for (const MacroEdit &e : macroEdits_) {
        for (std::size_t i = macros.size(); i-- > 0; )
            if (macros[i].first == e.name)
                macros.erase(macros.begin() + static_cast<long>(i));
        if (!e.undef) macros.push_back(std::make_pair(e.name, e.value));
    }
    return macros;
}

bool Driver::assembleObjects() {
    // The jobs wrote the objects themselves (M4.2); nothing is left to run.
    if (assemblesInProcess()) return true;
    // Built first and run afterwards: composing a command touches this object's
    // state, and only the running has to be concurrent.
    std::vector<std::string> commands;
    commands.reserve(temporaries_.size());
    for (std::size_t i = 0; i < temporaries_.size(); i++) {
        std::string command;
        if (targetIsTi()) {
            command = shellQuote(tiAssembler());
            if (!asmCompress_.empty()) command += " " + asmCompress_;
            command += " " + shellQuote(temporaries_[i]);
            command += " -o " + shellQuote(objects_[i]);
        } else if (hostIsWindows() && syntax_ == Syntax::Gnu) {
            // **The GNU spelling is assembled by clang, not by ml64.** ml64
            // has no COMDAT directive, so every mergeable definition - a
            // vtable, an inline member, a template's.
            command = shellQuote(hostGnuAssembler());
            command += " -target x86_64-pc-windows-msvc -c ";
            command += shellQuote(temporaries_[i]);
            command += " -o " + shellQuote(objects_[i]);
        } else if (hostIsWindows()) {
            command = shellQuote(masmAssembler());
            command += " /nologo /c /Fo " + shellQuote(objects_[i]);
            command += " " + shellQuote(temporaries_[i]);
        } else {
            command = shellQuote(hostCompiler());

            if (debug_) command += " -g";
            command += " -c " + shellQuote(temporaries_[i]);
            command += " -o " + shellQuote(objects_[i]);
        }
        commands.push_back(command);
    }
    return runCommands(commands);
}

// The linker command file lnk6x needs: one flat memory for the C6747 and
// every section the compilers and TI's runtime write placed in it - the same
// file RIDE and VM6747's tests link with.
static const char *const kTiLinkCommands =
    "--rom_model\n"
    "--stack_size=0x4000\n"
    "--heap_size=0x100000\n"
    "MEMORY\n"
    "{\n"
    "    RAM : origin = 0xC0000000, length = 0x04000000\n"
    "}\n"
    "SECTIONS\n"
    "{\n"
    "    .text        > RAM\n"
    "    .const       > RAM\n"
    "    .data        > RAM\n"
    "    .bss         > RAM\n"
    "    .far         > RAM\n"
    "    .fardata     > RAM\n"
    "    .neardata    > RAM\n"
    "    .rodata      > RAM\n"
    "    .cinit       > RAM\n"
    "    .init_array  > RAM\n"
    "    .switch      > RAM\n"
    "    .cio         > RAM\n"
    "    .stack       > RAM\n"
    "    .sysmem      > RAM\n"
    "    .vm6747.eh   > RAM\n"
    "}\n";

static bool fileExists(const std::string &path) {
    std::ifstream probe(path.c_str());
    return probe.good();
}

// **A TI program: every .s through asm6x, the objects through lnk6x against
// TI's runtime** - the exception-handling build where CPP11_TILIB names one,
// since CCS ships only the other and a C++ program that throws needs it.
bool Driver::linkTi() {
    std::vector<std::string> objects, steps;
    for (const std::string &t : temporaries_) {
        std::size_t dot = t.rfind('.');
        std::string obj = (dot == std::string::npos ? t : t.substr(0, dot)) + ".obj";
        steps.push_back(shellQuote(tiAssembler()) + (asmCompress_.empty() ? "" : " " + asmCompress_) + " " + shellQuote(t) +
                        " -o " + shellQuote(obj));
        objects.push_back(obj);
        temporaryNames().push_back(obj);
    }
    if (!runCommands(steps)) return false;

    std::string cmdfile = temporaryName(static_cast<int>(temporaries_.size()));
    cmdfile.replace(cmdfile.size() - 2, 2, ".cmd");
    {
        std::ofstream out(cmdfile.c_str());
        if (!out) {
            std::fprintf(stderr, "%s: cannot write %s\n", program_.c_str(), cmdfile.c_str());
            return false;
        }
        out << kTiLinkCommands;
    }
    temporaryNames().push_back(cmdfile);

    const std::string sep = hostIsWindows() ? "\\" : "/";
    std::vector<std::string> libraryDirs;
    const char *ti = std::getenv(program::env("TI").c_str());
    const char *tilib = std::getenv(program::env("TILIB").c_str());
    const bool tiNamed = (ti != nullptr && ti[0] != '\0') || (tilib != nullptr && tilib[0] != '\0');
    // **RTS6x, the project's own runtime, as RIDE links it** (review D9): -rts= names it or asks for TI's;
    // else RTS6X, or lib/rts6x-tms6747 beside this program, unless CPP11_TI or CPP11_TILIB names TI's.
    std::string ours, rts;
    if (rtsChoice_ != "ti") {
        std::string want = rtsChoice_;
        const char *env = std::getenv("RTS6X");
        if (want.empty() && !tiNamed && env != nullptr && env[0] != '\0') want = env;
        if (want.empty() && !tiNamed) {
            const std::string beside = besideProgram(program_, ("lib" + sep + "rts6x-tms6747" + sep + "rts6x.lib").c_str());
            if (!beside.empty()) want = beside.substr(0, beside.size() - std::strlen("rts6x.lib") - 1);
        }
        if (!want.empty()) {
            const bool named = want.size() > 4 && want.compare(want.size() - 4, 4, ".lib") == 0;
            const std::size_t cut = want.find_last_of("/\\");
            ours = named ? (cut == std::string::npos ? std::string(".") : want.substr(0, cut)) : want;
            rts = named ? want.substr(cut == std::string::npos ? 0 : cut + 1) : std::string("rts6x.lib");
            if (!fileExists(ours + sep + rts)) {
                std::fprintf(stderr, "%s: no %s in %s - -rts= names RTS6x's directory or its .lib, "
                                     "or -rts=ti asks for TI's rts6740\n", program_.c_str(), rts.c_str(), ours.c_str());
                return false;
            }
        }
    }
    if (!ours.empty()) {
        libraryDirs.push_back(ours);
    } else {
        if (ti != nullptr && ti[0] != '\0') libraryDirs.push_back(std::string(ti) + sep + "lib");
        if (tilib != nullptr && tilib[0] != '\0') libraryDirs.push_back(tilib);
        rts = "rts6740_elf.lib";
        for (const std::string &d : libraryDirs)
            if (fileExists(d + sep + "rts6740_elf_eh.lib")) rts = "rts6740_elf_eh.lib";
    }
    if (!quiet_)
        std::fprintf(stderr, "%s: linking against %s%s%s\n", program_.c_str(),
                     ours.empty() ? "TI's " : "RTS6x, ", ours.empty() ? rts.c_str() : (ours + sep + rts).c_str(),
                     ours.empty() && libraryDirs.empty() ? ", on lnk6x's own search path" : "");

    std::string command = shellQuote(tiLinker()) + " -mv6740 --abi=eabi";
    for (const std::string &d : libraryDirs) command += " -i " + shellQuote(d);
    command += " " + shellQuote(cmdfile);
    for (const std::string &o : objects) command += " " + shellQuote(o);
    for (const std::string &o : alreadyObjects_) command += " " + shellQuote(o);
    command += " -l " + rts + " -o " + shellQuote(linkTo_);

    int rc = runTool(command);
    if (rc != 0) {
        std::fprintf(stderr, "%s: the linker failed - the command was:\n  %s\n",
                     program_.c_str(), command.c_str());
        if (ours.empty())
            std::fprintf(stderr, "  rts6740_elf.lib is TI's, under CCS's C6000 "
                                 "compiler directory: CPP11_TI names it, CPP11_TILIB a "
                                 "directory holding rts6740_elf_eh.lib, CPP11_LD the "
                                 "linker; -rts= names RTS6x in its place.\n");
        return false;
    }
    return true;
}

bool Driver::link() {
    if (targetIsTi()) return linkTi();
    std::string command;
    if (hostIsWindows()) {

        // Windows assembles each file itself before linking, where the Unix
        // path hands every .s to one `c++` invocation. Same batch, same pool.
        std::vector<std::string> objects, steps;
        objects.reserve(temporaries_.size());
        steps.reserve(temporaries_.size());
        for (const std::string &t : temporaries_) {
            const std::string obj = objectBeside(t);
            // The same choice assembleObjects makes, and for the same reason -
            // ml64 cannot mark a mergeable definition COMDAT and clang can; the MASM spelling's job wrote its object.
            if (!assemblesInProcess()) {
                std::string step;
                if (syntax_ == Syntax::Gnu) {
                    step = shellQuote(hostGnuAssembler());
                    step += " -target x86_64-pc-windows-msvc -c " + shellQuote(t);
                    step += " -o " + shellQuote(obj);
                } else {
                    step = shellQuote(masmAssembler());
                    step += " /nologo /c /Fo " + shellQuote(obj) + " " + shellQuote(t);
                }
                steps.push_back(step);
            }
            objects.push_back(obj);
            // Recorded before the run, so a failure still cleans up whatever
            // the batch managed to write.
            temporaryNames().push_back(obj);
        }
        if (!runCommands(steps)) return false;

        command = shellQuote(windowsLinker());
        // **An 8 MB stack, which is what the other two targets already give.**
        command += " /nologo /subsystem:console /stack:8388608 /out:"
                 + shellQuote(linkTo_);
        // -g's CodeView goes into a PDB beside the program, which is where cdb looks.
        if (debug_) command += " /debug";
        for (const std::string &o : objects) command += " " + shellQuote(o);
        for (const std::string &o : alreadyObjects_) command += " " + shellQuote(o);

        // libcpmt holds std::set_new_handler and get_new_handler, and no other static library does.
        command += " libcmt.lib libcpmt.lib libucrt.lib libvcruntime.lib kernel32.lib"
                   " legacy_stdio_definitions.lib";
#ifdef _WIN32
        command += installationLibPaths();
#endif
    } else {
        command = shellQuote(hostCompiler());

        if (debug_) command += " -g";
        for (const std::string &t : temporaries_) command += " " + shellQuote(t);
        for (const std::string &o : alreadyObjects_) command += " " + shellQuote(o);
        command += " -o " + shellQuote(linkTo_);

        command += " -lm";
    }

    int rc = runTool(command);
    if (rc != 0) {
        std::fprintf(stderr, "%s: the assembler or linker failed - the command "
                             "was:\n  %s\n", program_.c_str(), command.c_str());
        noteWindowsToolchain(command);
        return false;
    }
    return true;
}

std::string Driver::assemblyNameFor(const std::string &source) {
    std::size_t dot = source.rfind('.');
    std::size_t slash = source.find_last_of('/');
    bool hasSuffix = dot != std::string::npos &&
                     (slash == std::string::npos || dot > slash);
    return hasSuffix ? source.substr(0, dot) + ".s" : source + ".s";
}

Dialect Driver::dialectFor(const std::string &input) const {
    if (stdNamed_) return std_;
    const std::size_t dot = input.find_last_of('.');
    return dot != std::string::npos && input.substr(dot) == ".c" ? Dialect::C90 : Dialect::Cxx11;
}

std::string Driver::objectNameFor(const std::string &source) const {
    std::size_t slash = source.find_last_of(hostIsWindows() ? "/\\" : "/");
    std::string base = slash == std::string::npos ? source
                                                  : source.substr(slash + 1);
    std::size_t dot = base.rfind('.');
    // .obj for the TI target, which is what asm6x and TI's tools call one
    return (dot == std::string::npos ? base : base.substr(0, dot)) +
           (targetIsTi() ? ".obj" : ".o");
}

bool Driver::parseArguments(int argc, char **argv) {
    std::vector<std::string> inputs;
    std::string output;

    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "-o") == 0) {
            if (++i == argc) {
                std::fprintf(stderr, "%s: -o needs a file name\n", argv[0]);
                return false;
            }
            output = argv[i];
        } else if (std::strncmp(argv[i], "-I", 2) == 0) {
            const char *dir = argv[i][2] != '\0' ? argv[i] + 2 : nullptr;
            if (!dir) {
                if (++i == argc) {
                    std::fprintf(stderr, "%s: -I needs a directory\n", argv[0]);
                    return false;
                }
                dir = argv[i];
            }
            searchPath_.push_back(dir);
        } else if (std::strncmp(argv[i], "-j", 2) == 0) {
            const char *n = argv[i][2] != '\0' ? argv[i] + 2 : nullptr;
            if (!n) {
                if (++i == argc) {
                    std::fprintf(stderr, "%s: -j needs a number\n", argv[0]);
                    return false;
                }
                n = argv[i];
            }
            char *end = nullptr;
            long value = std::strtol(n, &end, 10);
            if (*n == '\0' || (end && *end != '\0') || value < 1) {
                std::fprintf(stderr,
                    "%s: -j needs a positive number of jobs, not '%s'\n", argv[0], n);
                return false;
            }
            threads_ = static_cast<unsigned>(value);
        } else if (std::strncmp(argv[i], "-masm=", 6) == 0) {
            const char *want = argv[i] + 6;
            syntaxNamed_ = true;
            if (std::strcmp(want, "gnu") == 0) {
                syntax_ = Syntax::Gnu;
            } else if (std::strcmp(want, "masm") == 0 ||
                       std::strcmp(want, "intel") == 0) {
                syntax_ = Syntax::Masm;
            } else if (std::strcmp(want, "ml64") == 0) {
                syntax_ = Syntax::Ml64;
            } else {
                std::fprintf(stderr,
                    "%s: -masm= takes 'gnu', 'masm' or 'ml64', not '%s'\n", argv[0], want);
                return false;
            }
        } else if (std::strncmp(argv[i], "-arch", 5) == 0) {
            const char *name = argv[i][5] == '=' ? argv[i] + 6 : nullptr;
            if (!name) {
                if (++i == argc) {
                    std::fprintf(stderr, "%s: -arch needs a name - one of %s\n",
                                 argv[0], backendNames().c_str());
                    return false;
                }
                name = argv[i];
            }
            backend_ = findBackend(name);
            if (backend_ == nullptr) {
                std::fprintf(stderr, "%s: unknown architecture '%s' - one of %s\n",
                             argv[0], name, backendNames().c_str());
                return false;
            }
            if (!backend_->emits()) {
                std::fprintf(stderr, "%s: the %s backend is not written yet - it "
                             "knows what its types measure but has no instructions\n",
                             argv[0], backend_->name());
                return false;
            }
        } else if (std::strncmp(argv[i], "-D", 2) == 0 ||
                   std::strncmp(argv[i], "-U", 2) == 0) {
            bool undef = argv[i][1] == 'U';
            const char *text = argv[i][2] != '\0' ? argv[i] + 2 : nullptr;
            if (!text) {
                if (++i == argc) {
                    std::fprintf(stderr, "%s: -%c needs a name\n",
                                 argv[0], undef ? 'U' : 'D');
                    return false;
                }
                text = argv[i];
            }
            if (text[0] == '\0' || text[0] == '=') {
                std::fprintf(stderr, "%s: -%c needs a name before the '='\n",
                             argv[0], undef ? 'U' : 'D');
                return false;
            }
            addMacroEdit(text, undef);
        } else if (std::strcmp(argv[i], "-E") == 0) {
            preprocessOnly_ = true;
        } else if (std::strcmp(argv[i], "-S") == 0) {
            assemblyOnly_ = true;
        } else if (std::strcmp(argv[i], "-c") == 0) {
            objectOnly_ = true;
        } else if (std::strcmp(argv[i], "-coff=text") == 0) {
            coffText_ = true;
        } else if (std::strcmp(argv[i], "-keep-going") == 0 || std::strcmp(argv[i], "--keep-going") == 0) {
            keepGoing_ = 100;
        } else if (std::strncmp(argv[i], "-keep-going=", 12) == 0) {
            keepGoing_ = std::atoi(argv[i] + 12);
            if (keepGoing_ < 1) {
                std::fprintf(stderr, "%s: -keep-going= wants a number of errors, 1 or more\n", argv[0]);
                return false;
            }
        } else if (std::strcmp(argv[i], "-check=typed") == 0 || std::strcmp(argv[i], "--check=typed") == 0) {
            checkTyped_ = true;
        } else if (std::strcmp(argv[i], "-check=adapted") == 0 || std::strcmp(argv[i], "--check=adapted") == 0) {
            checkAdapted_ = true;
        } else if (std::strcmp(argv[i], "-front=typed") == 0 || std::strcmp(argv[i], "--front=typed") == 0) {
            frontTyped_ = true;
        } else if (std::strcmp(argv[i], "-front=ssa") == 0 || std::strcmp(argv[i], "--front=ssa") == 0) {
            frontSsa_ = true;
        } else if (std::strcmp(argv[i], "-check=ssa") == 0 || std::strcmp(argv[i], "--check=ssa") == 0) {
            checkSsa_ = true;
        } else if (std::strncmp(argv[i], "-dump=", 6) == 0 || std::strncmp(argv[i], "--dump=", 7) == 0) {
            // The boundaries a dump is written at (design section 1): the syntax tree only, so far.
            const char *what = std::strchr(argv[i], '=') + 1;
            if (std::strcmp(what, "syntax") == 0) dumpSyntax_ = true;
            else if (std::strcmp(what, "ast") == 0) dumpAst_ = true;
            else if (std::strcmp(what, "mir") == 0) dumpMir_ = true;
            else if (std::strncmp(what, "mir=", 4) == 0) { dumpMir_ = true; dumpMirAfter_ = what + 4; }
            else if (std::strcmp(what, "ssa") == 0) dumpSsa_ = true;
            else {
                std::fprintf(stderr, "%s: -dump= takes 'syntax', 'ast', 'mir' or 'ssa', the boundaries written so far - not '%s'\n",
                             argv[0], what);
                return false;
            }
        } else if (std::strcmp(argv[i], "-time") == 0) {
            timing_ = true;
        } else if (std::strcmp(argv[i], "-version") == 0 ||
                   std::strcmp(argv[i], "--version") == 0) {
            // Printed on stdout, unlike the banner: a version somebody asked
            // for is the answer to the command, not an aside beside it.
            // __DATE__ and __TIME__ are the build machine's local clock, whose zone is not known here.
            std::printf("%s\nVersion %s, sealed %s\nBuilt %s, the build machine's local time\n", CXX1_BANNER,
                        CXX1_VERSION, CXX1_SEAL_DATE, buildStamp().c_str());
            std::exit(0);
        } else if (std::strncmp(argv[i], "-rts=", 5) == 0 && argv[i][5] != '\0') {
            rtsChoice_ = argv[i] + 5;
        } else if (std::strcmp(argv[i], "--no_compress") == 0 || std::strcmp(argv[i], "--compress") == 0) {
            asmCompress_ = argv[i];
        } else if (std::strcmp(argv[i], "-nologo") == 0) {
            quiet_ = true;
        } else if (std::strcmp(argv[i], "-O0") == 0 || std::strcmp(argv[i], "-O1") == 0 ||
                   std::strcmp(argv[i], "-O2") == 0) {
            optimize_ = argv[i][2] - '0';
            forSize_ = false;
        } else if (std::strcmp(argv[i], "-Os") == 0) {
            optimize_ = 1;
            forSize_ = true;
        } else if (std::strcmp(argv[i], "-g") == 0) {
            debug_ = true;
        } else if (std::strncmp(argv[i], "-std=", 5) == 0) {
            // The language every input is read as; without it a .c is C90 and the rest C++11 (M7).
            const char *want = argv[i] + 5;
            stdNamed_ = true;
            if (std::strcmp(want, "c90") == 0 || std::strcmp(want, "c89") == 0 ||
                std::strcmp(want, "iso9899:1990") == 0 || std::strcmp(want, "gnu89") == 0 ||
                std::strcmp(want, "gnu90") == 0) {
                std_ = Dialect::C90;
            } else if (std::strcmp(want, "c++11") == 0 || std::strcmp(want, "gnu++11") == 0) {
                std_ = Dialect::Cxx11;
            } else {
                std::fprintf(stderr, "%s: -std= takes 'c90' (or 'c89', 'gnu89') or 'c++11', not '%s' - "
                                     "this compiler reads C90 and C++11 and nothing between or past them\n",
                             argv[0], want);
                return false;
            }
        } else if (argv[i][0] == '-' && argv[i][1] != '\0') {
            std::fprintf(stderr, "%s: unknown option %s\n", argv[0], argv[i]);
            return false;
        } else if (!expandPattern(argv[i], inputs)) {
            std::fprintf(stderr, "%s: no file matches %s\n", argv[0], argv[i]);
            return false;
        }
    }

    // C++ first, then C.
    standardIncludeDirectories(argv[0]);

    if (inputs.empty()) { usage(argv[0]); return false; }
    // A dump is in the order its nodes were made; two files compiled at once would interleave.
    if ((dumpSyntax_ || dumpAst_ || dumpMir_ || checkTyped_ || checkAdapted_) && inputs.size() > 1) {
        std::fprintf(stderr, "%s: %s takes one input at a time\n", argv[0],
                     dumpSyntax_ ? "-dump=syntax" : dumpAst_ ? "-dump=ast" : dumpMir_ ? "-dump=mir" : checkTyped_ ? "-check=typed" : "-check=adapted");
        return false;
    }

    // -E: each input after phase 4, on stdout, and nothing compiled.
    if (preprocessOnly_) {
        try {
            for (const std::string &in : inputs)
                std::fputs(Preprocessor(in, searchPath_, macrosFor(dialectFor(in))).run().text().c_str(), stdout);
        } catch (const CompileFailed &) {
            std::exit(1);  // the diagnostic is written; one thread here, so the exit is the verdict
        }
        std::exit(0);
    }

    // **-masm=masm is x86_64-windows's default on every host** (2026-10-25): assembled in process by CoffEmitter (M4.2), so no masm.exe and no ml64 fallback.
    if (!syntaxNamed_ && std::strcmp(backend_->name(), "x86_64-windows") == 0) syntax_ = Syntax::Masm;

    if (hostIsWindows() && syntax_ == Syntax::Gnu && syntaxNamed_ && !assemblyOnly_ &&
        std::strcmp(backend_->name(), "x86_64-windows") == 0 && hostGnuAssembler()[0] == '\0') {
        std::fprintf(stderr, "%s: -masm=gnu is assembled by clang, and none was found - install "
                             "Visual Studio's \"C++ Clang tools\" component, name one with %s, "
                             "or use -masm=masm\n", argv[0], program::env("AS").c_str());
        return false;
    }

    if (debug_ && optimize_ > 0 && std::strcmp(backend_->name(), "tms6747") == 0) {
        std::fprintf(stderr, "%s: -g with -O is not supported yet for tms6747 - the C6000 scheduler does not "
                             "carry a source position through its packets; compile with -g alone\n", argv[0]);
        return false;
    }
    if (debug_ && !backend_->emitsLineTable(syntax_)) {
        std::fprintf(stderr,
                     "%s: -g asks where each line of C++ went, and this compiler "
                     "writes no such thing for %s with -masm=ml64: MASM carries no "
                     "line table and ml64 builds none from it. -masm=masm writes the "
                     ".FILE and .LINE directives the project's masm reads, -masm=gnu a line "
                     "table clang assembles; or compile without -g.\n",
                     argv[0], backend_->name());
        return false;
    }

    // **An object file is an input too**, and it is not compiled: it is handed
    // to the linker with everything this run produces.
    {
        std::vector<std::string> sources;
        for (std::size_t i = 0; i < inputs.size(); i++) {
            const std::string &in = inputs[i];
            const std::size_t dot = in.find_last_of('.');
            const std::string ext = dot == std::string::npos
                                  ? std::string() : in.substr(dot);
            if (ext == ".o" || ext == ".obj" || ext == ".a" || ext == ".lib")
                alreadyObjects_.push_back(in);
            else
                sources.push_back(in);
        }
        inputs.swap(sources);
        if (!alreadyObjects_.empty() && (assemblyOnly_ || objectOnly_)) {
            std::fprintf(stderr,
                "%s: %s was given with %s, and there is nothing to compile in "
                "it - an object file is an input to the link step only\n",
                argv[0], alreadyObjects_[0].c_str(),
                assemblyOnly_ ? "-S" : "-c");
            return false;
        }
        if (inputs.empty() && alreadyObjects_.empty()) { usage(argv[0]); return false; }
    }

    if (!asmCompress_.empty() && !targetIsTi()) {
        std::fprintf(stderr, "%s: %s is for -arch tms6747, whose compact instructions it turns on or off\n",
                     argv[0], asmCompress_.c_str());
        return false;
    }
    if (assemblyOnly_ && objectOnly_) {
        std::fprintf(stderr, "%s: -S and -c ask for different things - -S stops "
                             "at assembly, -c goes one step further to an "
                             "object\n", argv[0]);
        return false;
    }

    if (assemblyOnly_) {
        if (!output.empty() && inputs.size() > 1) {
            std::fprintf(stderr,
                "%s: -o names a single output, but %zu inputs were given\n",
                argv[0], inputs.size());
            return false;
        }
        std::vector<std::string> outs;
        for (const std::string &in : inputs) {
            if (!output.empty()) jobs_.push_back(Job{ in, output, "", dialectFor(in) });
            else if (toStdout_)  jobs_.push_back(Job{ in, "", "", dialectFor(in) });
            else                 jobs_.push_back(Job{ in, assemblyNameFor(in), "", dialectFor(in) });
            outs.push_back(jobs_.back().output);
        }
        return distinctOutputs(argv[0], inputs, outs);
    }

    // A -c for x86_64-windows in the MASM spelling is assembled in process on any host (M4.2); a link still wants the
    // host's linker and libraries.
    if (backend_ != &defaultBackend() && !targetIsTi() && !(objectOnly_ && assemblesInProcess())) {
        std::fprintf(stderr,
            "%s: cannot assemble %s code on this machine, which is %s - use -S "
            "to write the assembly and take it there\n",
            argv[0], backend_->name(), defaultBackend().name());
        return false;
    }

    if (objectOnly_ && !output.empty() && inputs.size() > 1) {
        std::fprintf(stderr,
            "%s: -o names a single object, but %zu inputs were given\n",
            argv[0], inputs.size());
        return false;
    }

    if (!objectOnly_)
        linkTo_ = !output.empty() ? output
                : (hostIsWindows() && !targetIsTi() ? "a.exe" : "a.out");

    for (std::size_t i = 0; i < inputs.size(); i++) {
        std::string temp = temporaryName(static_cast<int>(i), inputs[i]);
        temporaries_.push_back(temp);
        temporaryNames().push_back(temp);
        if (objectOnly_) objects_.push_back(output.empty()
                                            ? objectNameFor(inputs[i]) : output);
        // In process, the job writes the object itself: -c's, or the one link() reads beside the .s.
        std::string object;
        if (assemblesInProcess()) object = objectOnly_ ? objects_.back() : objectBeside(temp);
        jobs_.push_back(Job{ inputs[i], temp, object, dialectFor(inputs[i]) });
    }
    return !objectOnly_ || distinctOutputs(argv[0], inputs, objects_);
}

// **The SSA lowering over every source body** (M3b.1): the typed tree of each function the census found is lowered;
// a function the subset does not reach is counted with its reason and keeps the parser's Ast.h path. With `report` the
// text is -check=ssa's: the verifier over the module, then `functions= lowered= outside= ok= failed=`.
static std::string lowerSsa(Parser &parser, Program &program, TypeTable &types, const Target &target, ir::Module &into, bool report) {
    sema::Typer typer(parser);
    const sema::Census c = sema::Typer::census(parser, program);
    ir::Lowering lowering(typer.tree(), parser.syntaxTree(), types, target, program, into);
    lowering.setConstructed([&typer](TypeId cls) { return typer.constructed(cls); });
    lowering.setAssigned([&typer](TypeId cls) { return typer.assigned(cls); });
    lowering.setDefaultInit([&typer](TypeId cls, const std::string &m) { return typer.defaultInit(cls, m); });
    lowering.setMemberDefault([&typer](const std::string &ctor, const std::string &m) { return typer.memberDefault(ctor, m); });
    lowering.setGuarded([&typer](const std::string &symbol) { return typer.guardedStatic(symbol); });
    lowering.setTypeInfo([&typer](TypeId t) { return typer.typeInfoSymbol(t); });
    lowering.setUnnamedCaught([&typer](syntax::NodeId id) { return typer.unnamedCaught(id); });
    lowering.setMicrosoftThrowInfo([&typer](TypeId t) { return typer.microsoftThrowInfo(t); });
    lowering.setMicrosoftCatch([&typer](TypeId t, std::string &d, bool &c) { return typer.microsoftCatchDescriptor(t, d, c); });
    lowering.setMicrosoftUnnamedCaught([&typer](syntax::NodeId id, std::string &d, bool &r, bool &c) {
        return typer.microsoftUnnamedCaught(id, d, r, c);
    });
    lowering.setNewFacts([&typer](syntax::NodeId id, sema::NewFacts &out) { return typer.newFacts(id, out); });
    lowering.setMemberBuilt([&typer](const std::string &ctor, const std::string &m, std::uint32_t pos, std::string &sym, TypeId &sig, bool &dflt) {
        return typer.memberBuilt(ctor, m, pos, sym, sig, dflt);
    });
    lowering.setDestructor([&typer](TypeId cls) { return typer.destructorSymbol(cls); });
    lowering.setVptrStores([&typer](const std::string &fn, std::vector<sema::VptrStore> &out) { return typer.vptrStores(fn, out); });
    lowering.setBaseVtt([&typer](const std::string &fn, const std::string &base) { return typer.baseVtt(fn, base); });
    lowering.setVbaseSlot([&typer](TypeId cls, TypeId vbase) { return typer.vbaseOffsetSlot(cls, vbase); });
    lowering.setMsVbase([&typer](const std::string &s) { return typer.msVbaseCtor(s); },
                        [&typer](const std::string &fn, std::vector<sema::VptrStore> &v, std::vector<std::string> &o) {
                            return typer.msVbaseInit(fn, v, o);
                        });
    lowering.setLocalBuilt([&typer](syntax::NodeId v, std::string &sym, TypeId &sig, bool &dflt, bool &zeroed) {
        return typer.localBuilt(v, sym, sig, dflt, zeroed);
    });
    lowering.setTemporaryBuilt([&typer](syntax::NodeId id, std::string &sym, TypeId &sig, bool &zero) {
        return typer.temporaryBuilt(id, sym, sig, zero);
    });
    lowering.setClosure([&typer](syntax::NodeId id, sema::ClosureFacts &out) { return typer.closureFacts(id, out); });
    lowering.setReturnBuilt([&typer](syntax::NodeId id, int &form, std::string &sym, TypeId &sig) {
        return typer.returnBuilt(id, form, sym, sig);
    });
    lowering.setNamedReturn([&typer](syntax::NodeId id) { return typer.namedReturnLocal(id); });
    lowering.setElidedInto([&typer](syntax::NodeId v, syntax::NodeId c) { return typer.elidedInto(v, c); });
    lowering.setAssignCall([&typer](syntax::NodeId v, std::string &sym, TypeId &sig) { return typer.assignCall(v, sym, sig); });
    std::map<std::string, int> reasons;
    int lowered = 0;
    for (std::size_t r = 0; r < c.bodies.size(); r++) {
        const Function &fn = program.functions[c.bodies[r].second];
        const sema::TypedId t = typer.typeFunction(c.bodies[r].first);
        ir::FunctionFacts facts;
        facts.symbol = fn.symbol();
        facts.displayName = fn.displayName();
        facts.isStatic = fn.isStatic();
        facts.isInline = fn.isInline();
        facts.variadic = fn.isVariadic();
        facts.hasThis = fn.hasThis();
        facts.isConstructor = typer.isConstructor(c.bodies[r].first);
        // Itanium's C2 or D2 handed a VTT was renamed from the C1 or D1 its records are keyed by.
        const std::string split = typer.vttSplit(fn.symbol());
        facts.recordKey = split.empty() ? fn.symbol() : split;
        facts.takesVtt = !split.empty();
        facts.msFlag = facts.isConstructor && typer.msVbaseCtor(fn.symbol());
        if (facts.isConstructor) typer.typeMemberDefaults(facts.recordKey);
        facts.sret = fn.sretSlot() != 0;
        facts.landingPads = fn.hasLandingPads();
        // A plain function's try block is a `try` in its body to the lowering, its throw(...) list a filter it
        // makes; any other wrap stays the parser's.
        facts.specFilter = typer.specFilter(facts.recordKey);
        facts.wrappedBody = facts.specFilter.empty() && ((typer.wrappedBody(facts.recordKey) && !typer.plainFunctionTry(facts.recordKey)) ||
                                                         !fn.exceptionSpec().empty());
        facts.noexcept_ = fn.isNoexcept();
        facts.pos = fn.pos();
        facts.paramCount = fn.params().size();
        if (fn.hasThis() && !fn.params().empty()) facts.thisType = TypeTable::id(fn.params()[0].type);
        const TypeId declared = typer.declaredReturn(c.bodies[r].first);
        facts.returns = declared.valid() ? declared : TypeTable::id(fn.returns());
        std::string why;
        // A local aligned past its type (alignas) or past the frame: the selector lays a slot out at its type's alignment.
        bool aligned = fn.frameAlign() > 16;
        for (const Local &l : fn.locals()) {
            if (l.align > 16) aligned = true;
            if (l.isParam || l.type == nullptr || l.type->isReference()) continue;
            int natural = l.type->align(target);
            const bool aggregate = l.type->isArray() || l.type->isStructOrUnion();      // Microsoft lays one of 16 at 16
            if (aggregate && l.type->size(target) >= 16 && natural < 16) natural = 16;   // not std::max: windows.h's macro
            if (l.align > natural) aligned = true;
        }
        if (aligned) why = "a local with alignas";
        else if (!lowering.lower(t, facts, why).valid() && why.empty()) why = "the lowering gave no reason";
        if (why.empty()) { lowered++; continue; }
        reasons[why]++;
        if (std::getenv("CXX1_SSA_WHY") != nullptr) std::fprintf(stderr, "outside %s: %s\n", fn.symbol().c_str(), why.c_str());
    }
    if (!report) return std::string();
    std::string out;
    const int failed = ir::verify(into, types, out);
    for (std::map<std::string, int>::const_iterator i = reasons.begin(); i != reasons.end(); ++i)
        out += "  outside (" + std::to_string(i->second) + "): " + i->first + "\n";
    char line[200];
    std::snprintf(line, sizeof line, "-check=ssa: functions=%zu lowered=%d outside=%zu ok=%d failed=%d\n", c.bodies.size(), lowered,
                  c.bodies.size() - lowered, lowered - failed, failed);
    return out + line;
}

bool Driver::compile(const Job &job) {
    using Clock = std::chrono::steady_clock;
    auto ms = [](Clock::time_point a, Clock::time_point b) {
        return std::chrono::duration<double, std::milli>(b - a).count();
    };

    const Target &target = backend_->target();
    TypeTable types;

    auto t0 = Clock::now();
    Source src = Preprocessor(job.input, searchPath_, job.dialect == Dialect::C90 ? macrosC_ : macros_).run();
    src.setKeepGoing(keepGoing_);
    auto t1 = Clock::now();

    std::vector<Token> tokens = Lexer(src, job.dialect).tokenize();
    auto t2 = Clock::now();

    Parser parser(src, std::move(tokens), types, target, backend_->abi(), job.dialect);
    Program program = parser.parse();
    auto t3 = Clock::now();
    if (dumpSyntax_) {
        std::string dumped;
        parser.syntaxTree().dump(dumped, src);
        std::fwrite(dumped.data(), 1, dumped.size(), stdout);
    }
    if (checkAdapted_) {
        int differ = 0;
        const std::string report = sema::Adapter::run(parser, program, false, &differ);
        std::fwrite(report.data(), 1, report.size(), stdout);
        if (differ != 0) return false;
    }
    if (frontTyped_) {
        int differ = 0;
        sema::Adapter::run(parser, program, true, &differ);
    }
    if (dumpAst_) {
        std::string dumped;
        for (std::size_t i = 0; i < program.functions.size(); i++) dumpFunction(program.functions[i], dumped);
        std::fwrite(dumped.data(), 1, dumped.size(), stdout);
    }
    if (checkTyped_) {
        int differ = 0;
        const std::string report = sema::Typer::check(parser, program, src, &differ);
        std::fwrite(report.data(), 1, report.size(), stdout);
        if (differ != 0) return false;
    }
    // **The SSA IR** (M3b.1): every source body the lowering reaches, lowered; dumped, verified, or handed to the selector.
    ir::Module ssa;
    if (frontSsa_ || dumpSsa_ || checkSsa_) {
        const std::string report = lowerSsa(parser, program, types, target, ssa, checkSsa_);
        if (checkSsa_) std::fwrite(report.data(), 1, report.size(), stdout);
        if (checkSsa_ && report.find(" failed=0") == std::string::npos) return false;
        if (dumpSsa_) { const std::string text = ir::dump(ssa, types); std::fwrite(text.data(), 1, text.size(), stdout); }
    }

    // **A file that writes `volatile` is compiled without -O**: the type system drops the qualifier, so an
    // optimizer would keep a volatile read in a register or delete it. **Said as a warning at the first
    // `volatile`** (1.10, 2026-10-20) as `file:line:col: warning:`; `cpp11: ...` stopped GRIDE 2.0's build.
    const int level = program.usesVolatile ? 0 : optimize_;
    if (optimize_ > 0 && program.usesVolatile) {
        volatileDowngrades_++;
        std::fprintf(stderr, "%s: warning: 'volatile' is something this compiler cannot keep apart "
                     "from plain memory, so %s is compiled without -O%d\n",
                     program.volatileAt.empty() ? job.input.c_str() : program.volatileAt.c_str(),
                     job.input.c_str(), optimize_);
    }

    bool ok = true;
    std::ostringstream made, mir;
    // The instructions the spelling hands the encoder as data (M4.4), for an object written in process.
    std::vector<masm::Instruction> structured;
    {
        std::unique_ptr<CodeGen> gen = backend_->codegen(made, syntax_);
        if (!job.object.empty() && !coffText_) gen->setStructured(&structured);
        if (debug_) gen->setLineSource(&src, workingDirectory());
        if (dumpMir_) gen->setMirDump(&mir, dumpMirAfter_);
        if (frontSsa_) gen->setSsa(&ssa, &types);
        gen->setOptimize(level);
        gen->setOptimizeForSize(forSize_ && level > 0);
        gen->run(program);
    }
    if (dumpMir_) std::fwrite(mir.str().data(), 1, mir.str().size(), stdout);
    std::string text = made.str();
    const std::string refused = spellWideNames(*backend_, &text);
    if (!refused.empty()) {
        std::fprintf(stderr, "%s: %s: %s\n", program_.c_str(), job.input.c_str(), refused.c_str());
        return false;
    }
    if (!job.object.empty()) {
        // **The object, in process** (M4.2): the text the .s would have held goes to the encoder library instead.
        // A refusal names the library's line of that text, and the text is written as the .s so it can be read.
        std::vector<std::string> errors;
        if (!CoffEmitter::assemble(text, job.output, job.object, errors, &structured)) {
            std::ofstream kept(job.output, std::ios::binary);
            if (kept) kept << text;
            std::fprintf(stderr, "%s: %s: the assembler refused the compiler's own output - the text is %s\n",
                         program_.c_str(), job.input.c_str(), job.output.c_str());
            for (const std::string &e : errors)
                std::fprintf(stderr, "  %s\n", e.c_str());
            return false;
        }
        if (timing_)
            std::fprintf(stderr, "%s: %s\n", job.input.c_str(), CoffEmitter::census(text, &structured).c_str());
    } else if (job.output.empty()) {
        std::cout << text;
    } else {
        // Binary, so the assembly is byte-identical on every host: text mode wrote CRLF on Windows (C5).
        std::ofstream file(job.output, std::ios::binary);
        if (!file) {
            std::fprintf(stderr, "%s: cannot write %s\n", program_.c_str(),
                         job.output.c_str());
            return false;
        }
        file << text;
    }
    auto t4 = Clock::now();

    if (timing_) {
        double read = ms(t0, t1), lex = ms(t1, t2), parse = ms(t2, t3), gen = ms(t3, t4);
        double all = ms(t0, t4);
        std::fprintf(stderr,
            "%s: read+pp %.2f  lex %.2f  parse %.2f  codegen %.2f  total %.2f ms"
            "   (front end %.0f%%)\n",
            job.input.c_str(), read, lex, parse, gen, all,
            all > 0 ? 100.0 * (read + lex + parse) / all : 0.0);
    }
    return ok;
}

unsigned Driver::availableCores() {
#ifdef __linux__
    cpu_set_t allowed;
    CPU_ZERO(&allowed);
    if (sched_getaffinity(0, sizeof allowed, &allowed) == 0) {
        std::vector<std::pair<long, long>> cores;
        for (int cpu = 0; cpu < CPU_SETSIZE; cpu++) {
            if (!CPU_ISSET(cpu, &allowed)) continue;
            std::string base = "/sys/devices/system/cpu/cpu" +
                               std::to_string(cpu) + "/topology/";
            std::ifstream pkgFile(base + "physical_package_id");
            std::ifstream coreFile(base + "core_id");
            long pkg = 0, core = cpu;
            if (!(pkgFile >> pkg) || !(coreFile >> core)) { pkg = 0; core = cpu; }

            std::pair<long, long> id(pkg, core);
            bool seen = false;
            for (const std::pair<long, long> &k : cores)
                if (k == id) { seen = true; break; }
            if (!seen) cores.push_back(id);
        }
        if (!cores.empty()) return static_cast<unsigned>(cores.size());
    }
#endif
    unsigned n = std::thread::hardware_concurrency();
    return n != 0 ? n : 1;
}

unsigned Driver::threadCount(std::size_t items) const {
    if (threads_ == 1) return 1;

    unsigned want;
    if (threads_ != 0) {
        want = threads_;
    } else {
        if (items < kThreadFrom) return 1;
        want = availableCores();
    }
    if (want > items) want = static_cast<unsigned>(items);
    return want < 1 ? 1 : want;
}

unsigned Driver::threadCount() const { return threadCount(jobs_.size()); }

// **The assembler is a job like any other, and it was the only serial phase
// left.**
bool Driver::runCommands(const std::vector<std::string> &commands) {
    if (commands.empty()) return true;
    const unsigned n = threadCount(commands.size());

    if (timing_)
        std::fprintf(stderr, "%s: %zu tool run%s on %u thread%s\n",
                     program_.c_str(), commands.size(),
                     commands.size() == 1 ? "" : "s", n, n == 1 ? "" : "s");

    std::mutex say;
    std::string failed;
    std::atomic<std::size_t> next{0};
    std::atomic<bool> ok{true};

    // **The first failure by index, not by arrival.**
    std::size_t failedAt = commands.size();

    auto work = [&] {
        for (;;) {
            std::size_t i = next.fetch_add(1);
            if (i >= commands.size()) return;
            if (!ok.load()) return;          // somebody failed; stop starting more
            if (runTool(commands[i]) == 0) continue;
            std::lock_guard<std::mutex> hold(say);
            if (i < failedAt) { failedAt = i; failed = lastToolCommand; }
            ok.store(false);
            return;
        }
    };

    if (n <= 1) {
        work();
    } else {
        std::vector<std::thread> pool;
        pool.reserve(n);
        for (unsigned t = 0; t < n; t++) pool.emplace_back(work);
        for (std::thread &t : pool) t.join();
    }

    if (!ok.load()) {
        std::fprintf(stderr, "%s: the assembler failed - the command was:\n"
                             "  %s\n", program_.c_str(), failed.c_str());
        noteWindowsToolchain(failed);
        return false;
    }
    return true;
}

bool Driver::compileCaught(const Job &job) {
    try {
        return compile(job);
    } catch (const CompileFailed &) {
        if (!job.output.empty()) std::remove(job.output.c_str());
        return false;
    }
}

// **Every job runs to its end, a failed one included**, so each file's diagnostic is printed; the
// verdict is given after the threads are joined, never by an exit from inside one (review P1).
bool Driver::runJobs() {
    unsigned n = threadCount();
    macros_ = macrosFor();
    macrosC_ = macrosFor(Dialect::C90);

    if (timing_)
        std::fprintf(stderr, "%s: %zu jobs on %u thread%s\n", program_.c_str(),
                     jobs_.size(), n, n == 1 ? "" : "s");

    if (n <= 1) {
        // **The same line for one thread**, because saying so is the point: below `kThreadFrom`
        // files this compiler does the work in the thread it was started on, and a report that
        // showed threads it did not use would be worse than none. `-j n` overrides the threshold.
        bool all = true;
        for (const Job &job : jobs_) {
            if (!quiet_ && jobs_.size() > 1)
                std::fprintf(stderr, "  [thread 1] %s\n", job.input.c_str());
            if (!compileCaught(job)) all = false;
        }
        return all;
    }

    std::atomic<std::size_t> next{0};
    std::atomic<bool> ok{true};

    // **Which thread took which file, said as it happens.** A pool that hands
    // out the next index has no fixed assignment - the numbers are the threads
    // and not the files - so this is the only place the work can be seen.
    std::vector<std::thread> pool;
    pool.reserve(n);
    for (unsigned t = 0; t < n; t++) {
        pool.emplace_back([this, t, &next, &ok] {
            for (;;) {
                std::size_t i = next.fetch_add(1);
                if (i >= jobs_.size()) return;
                if (!quiet_) {
                    std::lock_guard<std::mutex> hold(diagnosticLock());
                    std::fprintf(stderr, "  [thread %u] %s\n", t + 1, jobs_[i].input.c_str());
                }
                if (!compileCaught(jobs_[i])) ok.store(false);
            }
        });
    }
    for (std::thread &t : pool) t.join();
    return ok.load();
}

std::vector<std::string> Driver::produced() const {
    std::vector<std::string> out;
    if (assemblyOnly_) {
        if (!toStdout_)
            for (const Job &job : jobs_) out.push_back(job.output);
    } else if (objectOnly_) {
        out = objects_;
    } else {
        out.push_back(linkTo_);
    }
    return out;
}

int Driver::run(int argc, char **argv) {
    program_ = argv[0];

    int inputs = 0;
    bool sawO = false, sawS = false;
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "-o") == 0) { sawO = true; i++; }
        else if (std::strcmp(argv[i], "-I") == 0) i++;
        else if (std::strcmp(argv[i], "-j") == 0) i++;
        else if (std::strcmp(argv[i], "-arch") == 0) i++;
        else if (std::strcmp(argv[i], "-D") == 0) i++;
        else if (std::strcmp(argv[i], "-U") == 0) i++;
        else if (std::strcmp(argv[i], "-S") == 0) sawS = true;
        else if (argv[i][0] != '-')
            inputs += std::strpbrk(argv[i], "*?") != nullptr ? 2 : 1;  // a pattern is a set
    }
    toStdout_ = (sawS && inputs == 1 && !sawO);

    if (!parseArguments(argc, argv)) return 1;

    // **Printed once the arguments are known to be good**, so a usage message
    // is not preceded by a banner nobody asked for, and before any work so it
    // is the first thing on the screen. `-nologo` is the way out.
    saysDone_ = !quiet_;
    if (!quiet_) {
        std::fprintf(stderr, "%s\n", bannerLine());
        const unsigned n = threadCount();
        if (jobs_.size() > 1)
            std::fprintf(stderr, "%zu source files, %u compilation thread%s\n",
                         jobs_.size(), n, n == 1 ? "" : "s");
    }

    std::atexit([] {
        std::vector<std::string> &names = temporaryNames();
        for (const std::string &t : names) std::remove(t.c_str());
        names.clear();
        removeFolders();
    });

    if (!runJobs()) { removeTemporaries(); return 1; }
    if (assemblyOnly_) return 0;

    bool ok = objectOnly_ ? assembleObjects() : link();
    removeTemporaries();
    return ok ? 0 : 1;
}
