#pragma once

// **sema::Lookup, out of the parser in place** - M2 step 1, doc-cxx3/M2-PLAN.md. Its functions moved here
// whole; they still read the parser's state through p_, and the state follows in a later commit.

#include "Entities.h"
#include "../Lexer.h"
#include "../Type.h"

#include <cstddef>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

class Parser;

namespace sema {

class Lookup {
public:
    explicit Lookup(Parser &p) : p_(p) {}

    const Type *findTypedef(const std::string &name) const;
    // **`::Lexer` - the global scope and nothing nearer**, which is the whole
    // of what a leading `::` asks for.
    const Type *findGlobalTypedef(const std::string &name) const;
    void declareTypeName(const std::string &name, const Type *type);
    const EnumConst *findEnum(const std::string &name) const;
    // The class up the chain that declares this member function, searching every base rather than only the first.
    const Type *findMemberOwner(const Type *cls, const std::string &name) const;
    const Type *lookupInClass(const Type *cls, const std::string &name) const;
    // Whether class scope declares the name, so that it hides a global of that name.
    bool classScopeDeclares(const std::string &name);
    // The enclosing function's linkage name, for a class that is local to one.
    const std::string *localOwnerOf(const std::string &tag) const;
    const Local *findLocal(const std::string &name) const;
    const GlobalSym *findGlobal(const std::string &name) const;
    GlobalSym *findGlobalToUpdate(const std::string &name);
    const Signature &lookupFunction(const std::string &name, std::size_t pos) const;
    const Signature *findFunction(const std::string &name) const;
    const std::vector<std::size_t> *overloadsOf(const std::string &name) const;
    const Signature &lookupSignature(const std::string &name,
                                     const std::vector<const Type *> &params,
                                     bool variadic, std::size_t pos) const;
    // Every prefix `a::b::` becomes with inline components taken out: `lib::v2::`'s member is `lib::`'s.
    std::vector<std::string> inlineAliasPrefixes(const std::string &prefix) const;
    TableSizes tableSizes() const;
    void aliasInlineMembers(const TableSizes &before);
    void aliasTypeKey(const std::string &key, std::size_t index);
    void aliasFunctionKey(const std::string &key, std::size_t index);
    // [namespace.udir]/6: a name found in the namespace and in an inline one of it, as two
    // entities, is ambiguous - refused where the key is turned into the entity it names.
    void refuseInlineAmbiguity(const std::string &key, std::size_t index,
                               const std::unordered_map<std::string, std::size_t> &table,
                               bool (Lookup::*same)(std::size_t, std::size_t) const = nullptr) const;
    // Two typedef entries naming one type are one entity, as a typedef written twice is.
    bool sameTypedef(std::size_t a, std::size_t b) const;
    // The namespace a class was declared in, as namespaceStack_ spells it (n::m as {n, m}).
    std::vector<std::string> classNamespacePath(const Type *t) const;
    // The qualified name to look a written one up under: the enclosing namespaces from
    // the innermost outwards, then whatever a `using namespace` has opened, then the
    // name itself. `exists` says which table to ask, functions and variables differing.
    std::string qualifyForLookup(const std::string &name,
                                 bool (Lookup::*exists)(const std::string &) const) const;
    // The namespace a first component names, innermost out: `inner` in `outer` is "outer::inner".
    std::string namespaceNamed(const std::string &first) const;
    // An unqualified name declared in an enclosing namespace, innermost first, or "" -
    // asked before the flat table, since [basic.lookup.unqual]/1 lets the nearer one hide a global.
    // A type named in the classes this is inside, innermost first, or null.
    const Type *classScopeType(const std::string &name) const;
    std::string enclosingNamespaceKey(const std::string &name,
                                      bool (Lookup::*exists)(const std::string &) const) const;
    // The name a using-declaration redirects `key` to, following a chain of them
    // and stopping at a fixed depth so that `using A::x;` and `using B::x;`
    // written into a cycle cannot hang the parser instead of failing a lookup.
    std::string followUsingDeclaration(const std::string &key) const;
    bool hasFunctionNamed(const std::string &key) const;
    bool hasGlobalNamed(const std::string &key) const;
    bool hasTypeNamed(const std::string &key) const;
    bool hasEnumNamed(const std::string &key) const;
    // An enumerator named through the class it was written in, and through that class's bases.
    const EnumConst *enumInClass(const Type *cls, const std::string &name) const;
    // findEnum's class half on its own: an enumerator of the class being
    // parsed is at *class* scope and beats a global, where one at namespace
    // scope does not beat a member.
    const EnumConst *findClassEnum(const std::string &name) const;
    std::vector<std::string> lookupKeys(const std::string &name,
                                        const Type *left,
                                        const Type *right) const;

private:
    Parser &p_;
};

}   // namespace sema
