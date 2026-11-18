#pragma once

// **A translation unit's data record** (M6d): what a generator lays down beside the code - objects, string literals,
// the Microsoft throw and RTTI lists, the init functions - held by `ir::Module::data`, so no generator reads Ast.h for it.

#include "Dialect.h"
#include "Mangle.h"
#include "Type.h"

#include <string>
#include <utility>
#include <vector>

struct GlobalPiece {
    int offset;
    int size;
    long long value;

    std::string symbol;
};

struct Global {
    std::string name;

    // The linker's name for it, which differs from 'name' only where the
    // platform ABI mangles a variable - Microsoft does, Itanium does not.
    std::string symbol;
    TypeId type;
    std::vector<GlobalPiece> init;
    bool hasInit;
    bool isStatic;

    bool isConst;

    // **One 8-byte pointer laid down immediately before this object's label.**
    std::string prefixWord;

    // **Folded rather than rejected**, for an object several translation units each define: a
    // vtable, a typeinfo and its name string. clang marks all three weak, and without it two units
    // that share a polymorphic class collide on every one of them.
    bool isInline;
    int align = 0;   // `alignas(N)` on the object: what it asked for beyond its type's, else 0
    // `thread_local`: one copy per thread, in the target's TLS segment rather than .data or .bss.
    bool isThreadLocal = false;

    // C++11 has no aggregate with a member initialiser, so the braces call this.
    Global(std::string name = std::string(), std::string symbol = std::string(),
           const Type *type = nullptr, std::vector<GlobalPiece> init = std::vector<GlobalPiece>(),
           bool hasInit = false, bool isStatic = false, bool isConst = false,
           std::string prefixWord = std::string(), bool isInline = false)
        : name(std::move(name)), symbol(std::move(symbol)), type(TypeTable::id(type)), init(std::move(init)),
          hasInit(hasInit), isStatic(isStatic), isConst(isConst),
          prefixWord(std::move(prefixWord)), isInline(isInline) {}
};

// **One Itanium TLS wrapper this unit writes** - `_ZTW<name>`, the function every other unit calls
// to reach a thread_local with external linkage: defined here (local-exec, calling `_ZTH` first
// where the object has dynamic initialisation) or declared elsewhere (initial-exec, `_ZTH` weak).
struct TlsWrapper {
    std::string symbol;
    bool definedHere;
    bool dynamicInit;
    bool internal;       // `static thread_local` built by __tls_init: a wrapper nobody else can name
};

struct StringLit {
    std::string label;
    std::string bytes;
    int width;
};

struct UnitData {
    std::vector<Global> globals;
    std::vector<StringLit> strings;
    // Every function's symbol, in the order the unit defined them: the order a generator emits them in.
    std::vector<std::string> order;
    // **The Microsoft ThrowInfo chains**, one per type thrown or caught, finished by the parser.
    std::vector<MicrosoftThrow> msThrows;
    // **The classes wanting a Microsoft run-time description**, five each.
    std::vector<TypeId> rtti;
    // **The function that runs before main** - [basic.start.init]/2.
    std::string initFunction;
    // Whether it names __dso_handle, which ELF wants declared hidden.
    bool usesDsoHandle = false;
    // **The function that initialises this unit's thread_locals in a thread**, `__tls_init`, which
    // Itanium's `_ZTH` aliases name and the Microsoft CRT runs from .CRT$XDU; empty where none needs one.
    std::string tlsInitFunction;
    std::vector<TlsWrapper> tlsWrappers;
    // The dialect the unit was read as (M7): the debug records' language byte is C's for C90.
    Dialect dialect = Dialect::Cxx11;
};
