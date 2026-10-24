// sema::Lookup - see Lookup.h.

#include "Lookup.h"
#include "../parser/Parser.h"
#include "../parser/ParserInternal.h"
#include "../Source.h"

#include <algorithm>

namespace sema {

// A class's own nested types, and its bases'. The one table this parser has
// for type names is flat and keyed by the *qualified* name, so the scope is
// walked here rather than kept as a stack of tables.
const Type *Lookup::lookupInClass(const Type *cls, const std::string &name) const {
    for (const Type *c = cls; c != nullptr; c = c->enclosing()) {
        auto it = p_.typedefIndex_.find(c->tag() + "::" + name);
        if (it != p_.typedefIndex_.end()) return p_.typedefs_[it->second].type;
        for (const Type::BaseSpec &b : c->bases())
            if (const Type *t = lookupInClass(b.type, name)) return t;
    }
    return nullptr;
}

const std::string *Lookup::localOwnerOf(const std::string &tag) const {
    auto it = p_.localClassOwner_.find(tag);
    return it == p_.localClassOwner_.end() ? nullptr : &it->second;
}

bool Lookup::hasFunctionNamed(const std::string &key) const {
    return p_.functionIndex_.find(key) != p_.functionIndex_.end();
}

bool Lookup::hasGlobalNamed(const std::string &key) const {
    return p_.globalIndex_.find(key) != p_.globalIndex_.end();
}

bool Lookup::hasTypeNamed(const std::string &key) const {
    return p_.typedefIndex_.find(key) != p_.typedefIndex_.end();
}

// **Unqualified lookup inside a namespace, and it is a search and not a
// rule.**
std::string Lookup::followUsingDeclaration(const std::string &key) const {
    std::string at = key;
    for (int hops = 0; hops < 16; hops++) {
        auto it = p_.usingDeclarations_.find(at);
        if (it == p_.usingDeclarations_.end()) break;
        at = it->second;
    }
    return at;
}

std::string Lookup::qualifyForLookup(const std::string &name,
                                     bool (Lookup::*exists)(const std::string &) const) const {
    if (p_.absoluteLookup_) return name;
    // Written qualified: the first component is searched for, then a using-declaration followed.
    if (name.find("::") != std::string::npos) {
        // [basic.lookup.qual]/1: the first component is looked up unqualified, so
        // `inner::S` inside `outer` is `outer::inner::S` - the enclosing namespaces
        // innermost first, then the ones a using-directive opened.
        for (std::size_t i = p_.namespaceStack_.size(); i-- > 0; ) {
            std::string prefix;
            for (std::size_t k = 0; k <= i; k++) prefix += p_.namespaceStack_[k] + "::";
            if ((this->*exists)(prefix + name)) return prefix + name;
        }
        for (std::size_t i = p_.usingNamespaces_.size(); i-- > 0; )
            if ((this->*exists)(p_.usingNamespaces_[i] + "::" + name))
                return p_.usingNamespaces_[i] + "::" + name;
        const std::string aliased = followUsingDeclaration(name);
        return aliased != name && (this->*exists)(aliased) ? aliased : name;
    }
    for (std::size_t i = p_.namespaceStack_.size(); i-- > 0; ) {
        std::string prefix;
        for (std::size_t k = 0; k <= i; k++) prefix += p_.namespaceStack_[k] + "::";
        const std::string key = followUsingDeclaration(prefix + name);
        if ((this->*exists)(key)) return key;
    }
    for (std::size_t i = p_.usingNamespaces_.size(); i-- > 0; ) {
        const std::string key =
            followUsingDeclaration(p_.usingNamespaces_[i] + "::" + name);
        if ((this->*exists)(key)) return key;
    }
    // **Last, the name as written**, which is where a using-declaration at file
    // scope is found: it declares an unqualified name and there is no prefix to
    // put in front of it.
    const std::string aliased = followUsingDeclaration(name);
    if (aliased != name && (this->*exists)(aliased)) return aliased;
    return name;
}

std::string Lookup::enclosingNamespaceKey(const std::string &name,
                                          bool (Lookup::*exists)(const std::string &) const) const {
    if (p_.absoluteLookup_ || name.find("::") != std::string::npos) return std::string();
    for (std::size_t i = p_.namespaceStack_.size(); i-- > 0; ) {
        std::string prefix;
        for (std::size_t k = 0; k <= i; k++) prefix += p_.namespaceStack_[k] + "::";
        const std::string key = followUsingDeclaration(prefix + name);
        if ((this->*exists)(key)) return key;
    }
    return std::string();
}

std::string Lookup::namespaceNamed(const std::string &first) const {
    // Innermost out, and a class of the name hides a namespace as a nearer namespace does:
    // inside `outer`, `inner::k` is `outer::inner`'s whichever kind it is.
    for (std::size_t i = p_.classStack_.size(); i-- > 0; )
        if (lookupInClass(p_.classStack_[i], first) != nullptr) return std::string();
    if (p_.currentClass_ != nullptr && lookupInClass(p_.currentClass_, first) != nullptr)
        return std::string();
    for (std::size_t i = p_.namespaceStack_.size(); i-- > 0; ) {
        std::string prefix;
        for (std::size_t k = 0; k <= i; k++) prefix += p_.namespaceStack_[k] + "::";
        if (hasTypeNamed(prefix + first)) return std::string();
        if (p_.namespaces_.count(prefix + first)) return prefix + first;
    }
    for (std::size_t i = p_.usingNamespaces_.size(); i-- > 0; )
        if (p_.namespaces_.count(p_.usingNamespaces_[i] + "::" + first))
            return p_.usingNamespaces_[i] + "::" + first;
    return p_.namespaces_.count(first) ? first : std::string();
}

std::vector<std::string> Lookup::classNamespacePath(const Type *t) const {
    const Type *o = t->unqualified();
    while (o->enclosing() != nullptr) o = o->enclosing()->unqualified();
    std::string ns = o->templateNamespace();
    if (ns.empty() && o->inNamespace()) {
        const std::string &tag = o->tag();
        int depth = 0;
        for (std::size_t i = 0; i + 1 < tag.size(); i++) {
            if (tag[i] == '<') depth++;
            else if (tag[i] == '>') depth--;
            else if (depth == 0 && tag[i] == ':' && tag[i + 1] == ':') ns = tag.substr(0, i);
        }
    }
    std::vector<std::string> path;
    int depth = 0;
    std::string part;
    for (std::size_t i = 0; i < ns.size(); i++) {
        if (ns[i] == '<') depth++;
        else if (ns[i] == '>') depth--;
        if (depth == 0 && ns[i] == ':' && i + 1 < ns.size() && ns[i + 1] == ':') {
            if (!part.empty()) path.push_back(part);
            part.clear();
            i++;
            continue;
        }
        part += ns[i];
    }
    if (!part.empty()) path.push_back(part);
    return path;
}

// **An inline namespace's members are its enclosing namespace's for every lookup**, so each
// is entered under both keys: the enclosing one is `prefix` with the inline component gone.
std::vector<std::string> Lookup::inlineAliasPrefixes(const std::string &prefix) const {
    std::vector<std::string> out;
    if (p_.inlineNamespaces_.empty() || prefix.empty()) return out;
    std::vector<std::string> parts;
    std::vector<bool> isInline;
    std::string path;
    for (std::size_t s = 0, e; s < prefix.size(); s = e + 2) {
        e = prefix.find("::", s);
        if (e == std::string::npos) e = prefix.size();
        parts.push_back(prefix.substr(s, e - s));
        path += (path.empty() ? "" : "::") + parts.back();
        isInline.push_back(p_.inlineNamespaces_.count(path) != 0);
    }
    // Every subset of the inline components deleted, the empty subset left out.
    std::vector<std::size_t> which;
    for (std::size_t i = 0; i < parts.size(); i++) if (isInline[i]) which.push_back(i);
    for (std::size_t mask = 1; mask < (std::size_t(1) << which.size()); mask++) {
        std::string alt;
        for (std::size_t i = 0, w = 0; i < parts.size(); i++) {
            if (w < which.size() && which[w] == i) { w++; if (mask & (std::size_t(1) << (w - 1))) continue; }
            alt += parts[i] + "::";
        }
        out.push_back(alt);
    }
    return out;
}

void Lookup::aliasTypeKey(const std::string &key, std::size_t index) {
    const std::vector<std::string> alts = inlineAliasPrefixes(p_.namespacePrefix());
    const std::string &prefix = p_.namespacePrefix();
    if (alts.empty() || key.compare(0, prefix.size(), prefix) != 0) return;
    for (std::size_t a = 0; a < alts.size(); a++)
        p_.typedefIndex_.insert(std::make_pair(alts[a] + key.substr(prefix.size()), index));
}

void Lookup::aliasFunctionKey(const std::string &key, std::size_t index) {
    const std::vector<std::string> alts = inlineAliasPrefixes(p_.namespacePrefix());
    const std::string &prefix = p_.namespacePrefix();
    if (alts.empty() || key.compare(0, prefix.size(), prefix) != 0) return;
    for (std::size_t a = 0; a < alts.size(); a++) {
        std::vector<std::size_t> &set = p_.functionIndex_[alts[a] + key.substr(prefix.size())];
        if (std::find(set.begin(), set.end(), index) == set.end()) set.push_back(index);
    }
}

// After each declaration in a namespace with an inline component above or at it: what it added
// to the four tables under this namespace's prefix is entered under the enclosing ones as well.
// A key already taken by another entity is left to it - the ambiguity is refused at the use.
void Lookup::aliasInlineMembers(const TableSizes &before) {
    const std::vector<std::string> alts = inlineAliasPrefixes(p_.namespacePrefix());
    if (alts.empty()) return;
    const std::string prefix = p_.namespacePrefix();
    for (std::size_t i = before.functions; i < p_.functions_.size(); i++) {
        const Signature &f = p_.functions_[i];
        if (!f.owner.empty() || f.fromTemplate || f.cLinkage) continue;
        aliasFunctionKey(f.name, i);
    }
    for (std::size_t i = before.globals; i < p_.globals_.size(); i++) {
        const std::string &key = p_.globals_[i].name;
        if (key.compare(0, prefix.size(), prefix) != 0) continue;
        for (std::size_t a = 0; a < alts.size(); a++)
            p_.globalIndex_.insert(std::make_pair(alts[a] + key.substr(prefix.size()), i));
    }
    for (std::size_t i = before.typedefs; i < p_.typedefs_.size(); i++) aliasTypeKey(p_.typedefs_[i].name, i);
    for (std::size_t i = before.enums; i < p_.enums_.size(); i++) {
        const std::string &key = p_.enums_[i].name;
        if (key.compare(0, prefix.size(), prefix) != 0) continue;
        for (std::size_t a = 0; a < alts.size(); a++)
            p_.enumIndex_.insert(std::make_pair(alts[a] + key.substr(prefix.size()), i));
    }
}

void Lookup::refuseInlineAmbiguity(const std::string &key, std::size_t index,
                                   const std::unordered_map<std::string, std::size_t> &table,
                                   bool (Lookup::*same)(std::size_t, std::size_t) const) const {
    if (p_.inlineNamespaces_.empty()) return;
    int depth = 0;
    std::size_t cut = std::string::npos;
    for (std::size_t i = 0; i + 1 < key.size(); i++) {
        if (key[i] == '<') depth++;
        else if (key[i] == '>') depth--;
        else if (depth == 0 && key[i] == ':' && key[i + 1] == ':') cut = i;
    }
    if (cut == std::string::npos) return;
    const std::string scope = key.substr(0, cut), name = key.substr(cut + 2);
    // The namespace's own entry and every inline descendant's, as distinct entities.
    for (std::set<std::string>::const_iterator it = p_.inlineNamespaces_.lower_bound(scope + "::");
         it != p_.inlineNamespaces_.end() && it->compare(0, scope.size() + 2, scope + "::") == 0; ++it) {
        std::unordered_map<std::string, std::size_t>::const_iterator at = table.find(*it + "::" + name);
        if (at == table.end() || at->second == index) continue;
        if (same != nullptr && (this->*same)(index, at->second)) continue;
        // At the name, which is the token here or the one a qualified read just took.
        const Token &named = p_.peek().kind == TokenKind::Ident || p_.at_ == 0 ? p_.peek() : p_.tokens_[p_.at_ - 1];
        p_.src_.fail(named.pos, "reference to '" + name + "' is ambiguous - it is declared in '" +
                             scope + "' and in its inline namespace '" + *it + "'");
    }
}

const Type *Lookup::findGlobalTypedef(const std::string &name) const {
    auto it = p_.typedefIndex_.find(name);
    return it == p_.typedefIndex_.end() ? nullptr : p_.typedefs_[it->second].type;
}

const Type *Lookup::findTypedef(const std::string &name) const {
    // **A class local to this function is found first, and that is what makes it
    // shadow a global of the same name** - [basic.scope.local], and the reason the
    // local scope is a table of its own rather than an entry in the global one.
    auto local = p_.localTypes_.find(name);
    if (local != p_.localTypes_.end()) return local->second;

    // A bound template parameter's scope is inside every class and namespace around it ([temp.local]/6).
    if (p_.boundTypeParams_.count(name) != 0) {
        auto it = p_.typedefIndex_.find(name);
        if (it != p_.typedefIndex_.end()) return p_.typedefs_[it->second].type;
    }
    // Unqualified, the classes this is inside come next, then the enclosing namespaces,
    // and only then a global: [basic.lookup.unqual]/1, the nearest declaration wins.
    if (name.find("::") == std::string::npos)
        if (const Type *t = classScopeType(name)) return t;
    // The entry a key names, refused where an inline namespace makes it two.
    auto typeAt = [&](const std::string &key) {
        auto q = p_.typedefIndex_.find(key);
        if (q == p_.typedefIndex_.end()) return static_cast<const Type *>(nullptr);
        refuseInlineAmbiguity(key, q->second, p_.typedefIndex_, &Lookup::sameTypedef);
        return p_.typedefs_[q->second].type;
    };
    const std::string nearer = enclosingNamespaceKey(name, &Lookup::hasTypeNamed);
    if (!nearer.empty()) return typeAt(nearer);

    // A qualified name's first component is found innermost first, so a nearer
    // `inner::S` hides one at file scope before the name as written is tried.
    if (name.find("::") != std::string::npos && !p_.namespaceStack_.empty()) {
        const std::string key = qualifyForLookup(name, &Lookup::hasTypeNamed);
        if (key != name)
            if (const Type *t = typeAt(key)) return t;
    }

    if (const Type *t = typeAt(name)) return t;
    // **`N::D` where D is in N's unnamed namespace** - [namespace.unnamed]/1
    // gives N a using-directive for it, and a qualified name honours that.
    const std::size_t last = name.rfind("::");
    if (last != std::string::npos && last > 0) {
        auto u = p_.typedefIndex_.find(name.substr(0, last) + "::_GLOBAL__N_1" + name.substr(last));
        if (u != p_.typedefIndex_.end()) return p_.typedefs_[u->second].type;
    }

    // A class named without its namespace, from inside that namespace or from
    // one a `using namespace` has opened.
    {
        const std::string key = qualifyForLookup(name, &Lookup::hasTypeNamed);
        if (key != name)
            if (const Type *t = typeAt(key)) return t;
    }

    return classScopeType(name);
}

// **The classes this is inside, innermost first**: a class body being parsed, then the class whose
// member function's body this is - a nested class is unqualified only from inside.
const Type *Lookup::classScopeType(const std::string &name) const {
    if (p_.boundTypeParams_.count(name) != 0) return nullptr;      // a bound parameter hides it
    for (std::size_t i = p_.classStack_.size(); i-- > 0; )
        if (const Type *t = lookupInClass(p_.classStack_[i], name)) return t;
    if (p_.currentClass_ != nullptr)
        if (const Type *t = lookupInClass(p_.currentClass_, name)) return t;
    if (const Type *scope = p_.lambdaScope())
        if (const Type *t = lookupInClass(scope, name)) return t;
    if (p_.signatureClass_ != nullptr)
        if (const Type *t = lookupInClass(p_.signatureClass_, name)) return t;

    // **A held body is replayed at file scope, and its return type is read before
    // anything says which class it belongs to.** `Holder *self()` is the case, and
    // inlineOwner_ is the one thing that knows, so it is asked here.
    if (!p_.inlineOwner_.empty()) {
        auto it = p_.typedefIndex_.find(p_.inlineOwner_);
        if (it != p_.typedefIndex_.end())
            if (const Type *t = lookupInClass(p_.typedefs_[it->second].type, name))
                return t;
    }
    return nullptr;
}

// A class or enum name is a type name in C++ with no typedef written - inserted into the scope
// the definition appears in, the one table this parser has; an entry an inline namespace's member
// put under the key is not a declaration of it. Not implemented: an object hiding the class name.
void Lookup::declareTypeName(const std::string &name, const Type *type) {
    auto it = p_.typedefIndex_.find(name);
    const bool aliasHere = it != p_.typedefIndex_.end() && p_.typedefs_[it->second].name != name;
    if (!aliasHere && findTypedef(name) != nullptr) return;
    p_.typedefIndex_[name] = p_.typedefs_.size();
    p_.typedefs_.push_back(TypedefName{ name, type });
    aliasTypeKey(name, p_.typedefs_.size() - 1);
}

bool Lookup::hasEnumNamed(const std::string &key) const {
    return p_.enumIndex_.find(key) != p_.enumIndex_.end();
}

const Parser::EnumConst *Lookup::enumInClass(const Type *cls,
                                             const std::string &name) const {
    for (const Type *c = cls; c != nullptr; c = c->enclosing()) {
        auto it = p_.enumIndex_.find(c->tag() + "::" + name);
        if (it != p_.enumIndex_.end()) return &p_.enums_[it->second];
        for (const Type::BaseSpec &b : c->bases())
            if (const EnumConst *e = enumInClass(b.type, name)) return e;
    }
    return nullptr;
}

// **An enumerator is named through what encloses it**, the same as a class -
// `Layout::StepBase`, `n::Red` - so the scopes findTypedef walks are walked
// here too.
const Parser::EnumConst *Lookup::findEnum(const std::string &name) const {
    if (p_.boundValueParams_.count(name) != 0) {
        auto bound = p_.enumIndex_.find(name);
        if (bound != p_.enumIndex_.end()) return &p_.enums_[bound->second];
    }
    auto enumAt = [&](const std::string &key) {
        auto q = p_.enumIndex_.find(key);
        if (q == p_.enumIndex_.end()) return static_cast<const EnumConst *>(nullptr);
        refuseInlineAmbiguity(key, q->second, p_.enumIndex_);
        return static_cast<const EnumConst *>(&p_.enums_[q->second]);
    };
    const std::string nearer = enclosingNamespaceKey(name, &Lookup::hasEnumNamed);
    if (!nearer.empty()) return enumAt(nearer);
    if (const EnumConst *e = enumAt(name)) return e;

    const std::string key = qualifyForLookup(name, &Lookup::hasEnumNamed);
    if (key != name)
        if (const EnumConst *e = enumAt(key)) return e;

    return findClassEnum(name);
}

// The class half on its own. An enumerator of the class being parsed is at
// class scope, so it beats a global and loses to a local - which the caller
// in `primary` needs to ask separately from the namespace-scope table above.
const Parser::EnumConst *Lookup::findClassEnum(const std::string &name) const {
    if (p_.boundValueParams_.count(name) != 0) return nullptr;     // a bound parameter hides it
    for (std::size_t i = p_.classStack_.size(); i-- > 0; )
        if (const EnumConst *e = enumInClass(p_.classStack_[i], name)) return e;
    if (p_.currentClass_ != nullptr)
        if (const EnumConst *e = enumInClass(p_.currentClass_, name)) return e;
    if (const Type *scope = p_.lambdaScope())
        if (const EnumConst *e = enumInClass(scope, name)) return e;
    if (p_.signatureClass_ != nullptr)
        if (const EnumConst *e = enumInClass(p_.signatureClass_, name)) return e;
    if (!p_.inlineOwner_.empty()) {
        auto owner = p_.typedefIndex_.find(p_.inlineOwner_);
        if (owner != p_.typedefIndex_.end())
            if (const EnumConst *e =
                    enumInClass(p_.typedefs_[owner->second].type, name))
                return e;
    }
    return nullptr;
}

const Parser::Local *Lookup::findLocal(const std::string &name) const {
    for (std::size_t i = p_.locals_.size(); i-- > 0; )
        if (p_.locals_[i].name == name) return &p_.locals_[i];
    return nullptr;
}

const Parser::GlobalSym *Lookup::findGlobal(const std::string &name) const {
    auto globalAt = [&](const std::string &key) {
        auto q = p_.globalIndex_.find(key);
        if (q == p_.globalIndex_.end()) return static_cast<const GlobalSym *>(nullptr);
        refuseInlineAmbiguity(key, q->second, p_.globalIndex_);
        return static_cast<const GlobalSym *>(&p_.globals_[q->second]);
    };
    const std::string nearer = enclosingNamespaceKey(name, &Lookup::hasGlobalNamed);
    if (!nearer.empty()) return globalAt(nearer);
    if (const GlobalSym *g = globalAt(name)) return g;
    // Not under the name as written: an unqualified one inside a namespace
    // names that namespace's variable if it has one.
    const std::string key = qualifyForLookup(name, &Lookup::hasGlobalNamed);
    return key == name ? nullptr : globalAt(key);
}

// The entry a declaration of this name would redeclare: one an inline namespace's member
// put here under the enclosing name is not it, and the declaration takes the key over.
Parser::GlobalSym *Lookup::findGlobalToUpdate(const std::string &name) {
    auto it = p_.globalIndex_.find(name);
    if (it == p_.globalIndex_.end() || p_.globals_[it->second].name != name) return nullptr;
    return &p_.globals_[it->second];
}

const std::vector<std::size_t> *
Lookup::overloadsOf(const std::string &name) const {
    auto it = p_.functionIndex_.find(name);
    if (it == p_.functionIndex_.end() || it->second.empty()) return nullptr;
    return &it->second;
}

// The sole function of that name, or nothing when the name is overloaded. Every caller
// wants one function without having any arguments to choose by, so "there are several"
// is not an answer it can use - each one says so in its own words instead.
const Parser::Signature *Lookup::findFunction(const std::string &name) const {
    const std::vector<std::size_t> *set = overloadsOf(name);
    if (set == nullptr || set->size() != 1) return nullptr;
    // Its one caller names the function as a value, which is a use: [dcl.fct.def.delete]/2.
    p_.refuseDeleted(p_.functions_[(*set)[0]], p_.peek().pos);
    return &p_.functions_[(*set)[0]];
}

// The one function of this name with these parameters - the only question a definition
// can ask, since a definition IS a parameter list. Going through lookupFunction broke
// the moment a name could hold two: it answers "which one", and a definition knows.
const Parser::Signature &
Lookup::lookupSignature(const std::string &name,
                        const std::vector<const Type *> &params,
                        bool variadic, std::size_t pos) const {
    // The same qualification the declaration used, or a definition written
    // inside a namespace cannot find the prototype it is defining.
    const std::string key = name.find("::") != std::string::npos
                          ? name
                          : qualifyForLookup(p_.instantiationName(name),
                                             &Lookup::hasFunctionNamed);
    // The namespace's own function before one an inline namespace put under its name.
    if (const std::vector<std::size_t> *set = overloadsOf(key)) {
        for (int pass = 0; pass < 2; pass++)
            for (std::size_t k = 0; k < set->size(); k++) {
                const Signature &f = p_.functions_[(*set)[k]];
                if ((!f.fromTemplate && f.name != key) == (pass == 1) && f.variadic == variadic &&
                    Classes::sameParameters(f.params, params))
                    return f;
            }
    }
    p_.src_.fail(pos, "'" + name + "' was not declared - a prototype must come first");
}

const Parser::Signature &Lookup::lookupFunction(const std::string &name,
                                                std::size_t pos) const {
    if (const Signature *s = findFunction(name)) return *s;
    p_.src_.fail(pos, "'" + name + "' was not declared - a prototype must come first");
}

// Does class scope, between block scope and the namespace, declare this name - a data member, a
// static one, a member function or an enumerator of the class being read, a base, or the class a
// lambda was written in. Asked of a name a global also answers, which the class then hides.
bool Lookup::classScopeDeclares(const std::string &name) {
    if (p_.boundValueParams_.count(name) != 0) return false;
    const Type *roots[2] = { p_.currentClass_ != nullptr ? p_.currentClass_
                                                      : p_.classStack_.empty() ? nullptr
                                                                            : p_.classStack_.back(),
                             p_.lambdaScope() };
    for (const Type *root : roots) {
        if (root == nullptr) continue;
        if (root->findMember(name) != nullptr || root->findStaticMember(name) != nullptr)
            return true;
        for (const Type *c = root; c != nullptr; c = c->base())
            if (name != Parser::localOf(c->tag()) && overloadsOf(c->tag() + "::" + name) != nullptr)
                return true;
    }
    return findClassEnum(name) != nullptr;
}

// A call through an object: `p.move(1, 2)`. The object's address goes in front of
// the written arguments and the declared parameters gain a matching leading
// pointer, so from here down it is an ordinary call.
const Type *Lookup::findMemberOwner(const Type *cls,
                                    const std::string &name) const {
    if (cls == nullptr) return nullptr;
    const Type *c = cls->unqualified();
    if (overloadsOf(c->tag() + "::" + name) != nullptr) return c;
    const std::vector<Type::BaseSpec> &bases = c->bases();
    for (std::size_t i = 0; i < bases.size(); i++)
        if (const Type *found = findMemberOwner(bases[i].type, name)) return found;
    return nullptr;
}

// [over.match.oper]: one candidate set with both halves in it - a member's implicit
// object parameter is what makes the two rank vectors comparable. This answers
// which half won, not which function. Then [basic.lookup.argdep], operands only.
std::vector<std::string> Lookup::lookupKeys(const std::string &name,
                                            const Type *left,
                                            const Type *right) const {
    std::vector<std::string> keys;
    auto add = [&](const std::string &k) {
        if (!hasFunctionNamed(k)) return;
        for (std::size_t i = 0; i < keys.size(); i++)
            if (keys[i] == k) return;
        keys.push_back(k);
    };
    add(name);
    add(qualifyForLookup(name, &Lookup::hasFunctionNamed));

    const Type *operands[2] = { left, right };
    for (std::size_t i = 0; i < 2; i++) {
        const Type *t = operands[i];
        while (t != nullptr && (t->isPointer() || t->isReference()))
            t = t->pointee();
        if (t == nullptr) continue;
        t = t->unqualified();
        // [basic.lookup.argdep]/2: a class's namespace; and an enumeration's, the innermost namespace
        // around it - a class-scoped one's is its class's - so that `N::A | N::B` reaches N's
        // operator| and not the built-in (1.27, review item 3; the enumeration case was missing).
        std::string prefix;
        if (t->isStructOrUnion() && t->inNamespace()) {
            const std::size_t cut = t->tag().rfind("::");
            if (cut == std::string::npos) continue;
            prefix = t->tag().substr(0, cut);
        } else if (t->isEnumeration()) {
            prefix = t->tag();
            for (;;) {
                const std::size_t cut = prefix.rfind("::");
                if (cut == std::string::npos) { prefix.clear(); break; }
                prefix = prefix.substr(0, cut);
                if (p_.namespaces_.count(prefix) != 0) break;
            }
            if (prefix.empty()) continue;   // a global enumeration's namespace is the bare name's
        } else continue;
        add(prefix + "::" + name);
        // [basic.lookup.argdep]/2: an inline namespace's class has the enclosing one too.
        const std::vector<std::string> alts = inlineAliasPrefixes(prefix + "::");
        for (std::size_t a = 0; a < alts.size(); a++) add(alts[a] + name);
    }
    return keys;
}

TableSizes Lookup::tableSizes() const {
    return TableSizes{ p_.functions_.size(), p_.globals_.size(), p_.typedefs_.size(), p_.enums_.size() };
}

bool Lookup::sameTypedef(std::size_t a, std::size_t b) const { return p_.typedefs_[a].type == p_.typedefs_[b].type; }

}   // namespace sema
