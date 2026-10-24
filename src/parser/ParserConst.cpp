// The parser: what is evaluated where it is written.
#include "Parser.h"
#include "ParserInternal.h"
#include "../Mangle.h"
#include "../Source.h"

#include <climits>
#include <cstring>

// [dcl.typedef]/2: `using X = T;` names T as `typedef T X;` would, in whichever scope it stands -
// `prefix` is what the key takes there (a namespace's, a class's tag and "::", or nothing in a block).
bool Parser::aliasDeclaration(const std::string &prefix) {
    if (!peek().is("using")) return false;
    if (peekAt(1).kind != TokenKind::Ident || !(peekAt(2).is("=") || peekAt(2).is("["))) return false;
    at_++;
    const std::size_t pos = peek().pos;
    const std::string name = peek().text;
    at_++;
    skipAttributes();
    expect("=");
    const std::size_t typeAt = peek().pos;
    StorageClass sc;
    const Type *t = specifiers(&sc);
    if (sc != StorageNone)
        src_.fail(typeAt, "an alias declaration names a type, and a storage class is not part of one");
    if (t->unqualified()->kind() == Kind::Deduced)
        src_.fail(typeAt, "an alias declaration names a type, and 'auto' deduces one from an "
                          "initialiser it does not have");
    // `using F = int(int);` - a function type, whose parameter list no declarator reads.
    Declared d{ "", t, typeAt, 0, "" };
    if (peek().is("(") && !atParenInitialiser()) typedefFunctionSuffix(d);
    else d = declarator(t, true);
    if (!d.name.empty())
        src_.fail(d.pos, "an alias declaration names a type, and '" + d.name + "' would declare something "
                         "of that type - write 'using " + name + " = T;' with T a type alone");
    expect(";");
    const std::string key = prefix + name;
    std::unordered_map<std::string, std::size_t>::const_iterator had = lookup_.typedefIndex_.find(key);
    if (had != lookup_.typedefIndex_.end() && lookup_.typedefs_[had->second].type != d.type &&
        lookup_.typedefs_[had->second].name == key)
        src_.fail(pos, "'" + name + "' is declared twice, and not as the same type: it was '" +
                       lookup_.typedefs_[had->second].type->describe() + "' and is now '" + d.type->describe() + "'");
    lookup_.typedefIndex_[key] = lookup_.typedefs_.size();
    lookup_.typedefs_.push_back(TypedefName{ key, d.type });
    return true;
}

// [temp.explicit]/2: `extern template` promises a definition elsewhere, and every specialization here is
// mergeable (weak, COMDAT), so the one it would suppress folds with that one - read and dropped.
bool Parser::externTemplateDeclaration() {
    if (!peek().is("extern") || !peekAt(1).is("template")) return false;
    const std::size_t pos = peek().pos;
    at_ += 2;
    int depth = 0;
    for (;; at_++) {
        if (peek().kind == TokenKind::End) src_.fail(pos, "this 'extern template' declaration never ends");
        if (peek().is("(") || peek().is("[") || peek().is("{")) depth++;
        else if (peek().is(")") || peek().is("]") || peek().is("}")) depth--;
        else if (depth == 0 && peek().is(";")) break;
    }
    at_++;
    return true;
}

// **`= default` and `= delete` sit exactly where `= 0` does**, after the virt-specifiers, and every
// declare* that follows reads which one was written. Outside a class only `= delete` has a meaning.
void Parser::readDefaultedOrDeleted() {
    pendingSpecial_ = SpecialDef::None;
    if (!peek().is("=")) return;
    const bool def = peekAt(1).is("default");
    if (!def && !peekAt(1).is("delete")) return;
    pendingSpecialAt_ = peek().pos;
    pendingSpecial_ = def ? SpecialDef::Defaulted : SpecialDef::Deleted;
    at_ += 2;
    if (def && classStack_.empty())
        src_.fail(pendingSpecialAt_, "'= default' outside the class is not supported yet: a special "
                                     "member defaulted on a later declaration is user-provided, where "
                                     "one defaulted inside its class is the implicit one - write it there");
}

// [dcl.fct.def.delete]/2: a deleted function may not be used, and this is said where the use is.
void Parser::refuseDeleted(const Signature &f, std::size_t pos) const {
    if (!f.deleted) return;
    const std::string name = f.owner.empty() ? f.name : f.owner + "::" + f.name;
    if (f.implicit)
        src_.fail(pos, "'" + name + "' is deleted - the compiler would write it, and [class.copy] deletes "
                       "it: a move written '= default', or a base or member whose own is deleted");
    src_.fail(pos, "'" + name + "' is deleted - it was declared '= delete', and a deleted function "
                   "cannot be called, copied from or taken the address of");
}

// [dcl.fct.def.default]/1: only a special member may be defaulted, and once per class.
void Parser::defaultedMember(DefaultedMember &m, Access access, std::size_t pos, const std::string &what) {
    if (m.is) src_.fail(pos, what + " is defaulted twice");
    m.is = true;
    m.access = access;
}

// What the class wrote `= default` on, or nothing.
Parser::Defaulted Parser::defaultedFor(const std::string &tag) const {
    std::map<std::string, Defaulted>::const_iterator it = defaulted_.find(tag);
    return it != defaulted_.end() ? it->second : Defaulted();
}

// [dcl.constexpr]/4: a C++11 constexpr constructor's body is empty; statements in it are C++14.
void Parser::refuseConstexprConstructorBody(std::size_t parenAt) const {
    std::size_t k = parenAt;
    int depth = 0;
    for (;; k++) {
        const Token &t = peekAt(k);
        if (t.kind == TokenKind::End || (depth == 0 && t.is(";"))) return;
        if (t.is("(") || t.is("[")) depth++;
        else if (t.is(")") || t.is("]")) depth--;
        else if (t.is("{")) {
            const Token &before = peekAt(k - 1);
            if (depth == 0 && before.kind != TokenKind::Ident && !before.is(">")) break;
            depth++;
        } else if (t.is("}")) depth--;
    }
    if (peekAt(k + 1).is("}")) return;
    src_.fail(peekAt(k + 1).pos, "a 'constexpr' constructor with statements in its body is C++14, "
                                 "and this compiler is C++11 - [dcl.constexpr]/4 asks for an empty "
                                 "body and the work in the initialiser list");
}


// `friend class X;`, or `friend X;` for a class already declared - [class.friend]/2: every member of X reaches
// what `owner` keeps private. An unqualified X not yet declared is declared in the namespace around `owner`,
// [namespace.memdef]/3; a qualified one, `friend class n::X;`, names a class already declared ([class.friend]/3).
void Parser::befriendClass(const std::string &owner, bool keyed) {
    if (keyed) at_++;
    const bool global = peek().is("::") && peekAt(1).kind == TokenKind::Ident;
    if (global) at_++;
    std::size_t namePos = peek().pos;
    std::string name = expectIdent("the class to befriend");
    std::string scope;
    while (peek().is("::") && peekAt(1).kind == TokenKind::Ident) {
        at_++;
        scope = name;
        namePos = peek().pos;
        name += "::" + expectIdent("the class to befriend");
    }
    std::string key = name;
    const Type *t = global ? lookup_.findGlobalTypedef(name) : lookup_.findTypedef(name);
    if (t != nullptr) key = t->unqualified()->tag();
    else if (global || !scope.empty())
        src_.fail(namePos, "no class named '" + name.substr(scope.size() + (scope.empty() ? 0 : 2)) +
                           "' in " + (scope.empty() ? std::string("the global namespace")
                                          : (lookup_.findTypedef(scope) != nullptr ? "class '" : "namespace '") + scope + "'") +
                           " - a qualified friend names a class declared before it");
    else if (!namespaceStack_.empty()) key = namespacePrefix() + name;
    friendClasses_[owner].push_back(key);
    expect(";");
}

// Is the code being read inside a member of a class `cls` befriended - or of a class nested in one?
bool Parser::isFriendClassOf(const Type *cls) const {
    if (cls == nullptr) return false;
    std::map<std::string, std::vector<std::string> >::const_iterator it =
        friendClasses_.find(cls->unqualified()->tag());
    if (it == friendClasses_.end()) return false;
    const Type *here[3] = { currentClass_, classStack_.empty() ? nullptr : classStack_.back(), lambdaScope() };
    for (const Type *h : here)
        // A local class has its member function's access, [class.access]/2, so its outer class counts too.
        for (const Type *l = h; l != nullptr;) {
            for (const Type *c = l; c != nullptr; c = c->enclosing())
                for (std::size_t i = 0; i < it->second.size(); i++)
                    if (c->unqualified()->tag() == it->second[i]) return true;
            std::map<std::string, const Type *>::const_iterator in = localClassOuter_.find(l->unqualified()->tag());
            l = in != localClassOuter_.end() ? in->second : nullptr;
        }
    return false;
}

// The implicit member a `= default` stands for was declared public; it takes the access it was written under.
void Parser::applyDefaultedAccess(const std::string &tag, const Type *type) {
    std::map<std::string, Defaulted>::const_iterator it = defaulted_.find(tag);
    if (it == defaulted_.end()) return;
    const Defaulted &df = it->second;
    if (const Signature *f = classes_.defaultConstructorOf(type))
        if (df.defaultCtor.is) lookup_.functions_[static_cast<std::size_t>(f - &lookup_.functions_[0])].access = df.defaultCtor.access;
    if (const Signature *f = classes_.copyConstructorOf(type))
        if (df.copyCtor.is) lookup_.functions_[static_cast<std::size_t>(f - &lookup_.functions_[0])].access = df.copyCtor.access;
    if (const Signature *f = classes_.moveConstructorOf(type))
        if (df.moveCtor.is) lookup_.functions_[static_cast<std::size_t>(f - &lookup_.functions_[0])].access = df.moveCtor.access;
    if (const Signature *f = classes_.copyAssignOf(type))
        if (df.copyAssign.is) lookup_.functions_[static_cast<std::size_t>(f - &lookup_.functions_[0])].access = df.copyAssign.access;
    if (const Signature *f = destructorOf(type))
        if (df.dtor.is) lookup_.functions_[static_cast<std::size_t>(f - &lookup_.functions_[0])].access = df.dtor.access;
}

bool Parser::staticAssertion() {
    if (!peek().is("static_assert")) return false;
    const std::size_t pos = peek().pos;
    at_++;
    expect("(");

    const std::size_t condAt = peek().pos;
    ExprPtr cond = decay(conditional());
    long long value;
    if (!constant_.fold(*cond, &value, condAt))
        src_.fail(condAt, "the condition of a 'static_assert' has to be a "
                          "constant the compiler can work out here, and this "
                          "is not one");

    if (peek().is(")"))
        src_.fail(peek().pos, "a 'static_assert' with no message is C++17 - "
                              "write the message this one would have printed, "
                              "'static_assert(cond, \"why\")'");
    expect(",");

    if (peek().kind != TokenKind::Str)
        src_.fail(peek().pos, "the message of a 'static_assert' has to be "
                              "written out as a string literal - it is printed "
                              "by the compiler, so there is no program running "
                              "to read a variable");
    std::string message = peek().text;
    at_++;
    while (peek().kind == TokenKind::Str) {   // "a" "b" is one literal
        message += peek().text;
        at_++;
    }

    expect(")");
    expect(";");

    if (value == 0) src_.fail(pos, "static assertion failed: " + message);
    return true;
}

// **The exception specification, and the one thing it does not touch.** In C++11 it is
// *not* part of the function's type - measured, both spellings mangle alike - so what
// it buys is `noexcept(e)`, or `throw(T...)`'s list, kept in pendingSpec_ for the definition.
bool Parser::exceptionSpecification() {
    skipAttributes();
    pendingSpec_ = DynamicSpec();
    bool noexc = false;
    if (consume("noexcept")) {
        noexc = true;
        if (consume("(")) {
            const long long value = constantExpression("a constant in 'noexcept('");
            expect(")");
            noexc = value != 0;
        }
    } else if (peek().is("throw") && peekAt(1).is("(")) {
        const std::size_t at = peek().pos;
        at_ += 2;
        if (consume(")")) {
            noexc = true;
        } else {
            pendingSpec_.written = true;
            pendingSpec_.pos = at;
            do {
                const std::size_t tpos = peek().pos;
                StorageClass sc;
                const Type *t = declarator(specifiers(&sc), true).type;
                if (peek().is("..."))
                    src_.fail(peek().pos, "a pack expansion in a dynamic exception specification is not "
                                          "supported yet - write the types out");
                pendingSpec_.types.push_back(adjustedSpecType(t, tpos));
            } while (consume(","));
            expect(")");
        }
    }
    readVirtSpecifiers();
    readDefaultedOrDeleted();
    return noexc;
}

// [except.spec]/2: a reference names its referent, an array or a function decays, and the
// top-level qualifiers go - what is left must be complete, or point at a complete type or void.
const Type *Parser::adjustedSpecType(const Type *t, std::size_t pos) {
    if (t->isRValueReference())
        src_.fail(pos, "an rvalue reference type is not allowed in an exception specification - [except.spec]/2");
    if (t->isReference()) t = t->referent();
    if (t->isArray()) t = types_.pointerTo(t->pointee());
    else if (t->isFunction()) t = types_.pointerTo(t);
    t = t->unqualified();
    if (patternOnly_) return t;
    const Type *named = (t->isPointer() ? t->pointee() : t)->unqualified();
    if (named->kind() == Kind::TemplateParam || named->kind() == Kind::DependentMember) return t;
    if (!named->isVoid() && !named->isComplete())
        src_.fail(pos, "'" + named->describe() + "' is incomplete, and an exception specification may "
                       "only name a complete type, or a pointer or reference to one - [except.spec]/2");
    return t;
}

// **`override` and `final` are contextual and follow the exception specification** - [class.mem]. Inside a
// class body they are recorded for the declaration being read; a held body's replay has already checked
// them and steps over; anywhere else - a free function, an out-of-line definition - they are ill-formed.
void Parser::readVirtSpecifiers() {
    pendingVirt_ = VirtSpecifiers();
    while (peek().is("override") || peek().is("final")) {
        const bool ov = peek().is("override");
        VirtSpecifiers &v = pendingVirt_;
        if ((ov && v.isOverride) || (!ov && v.isFinal))
            src_.fail(peek().pos, std::string("'") + peek().text + "' is written twice");
        if (ov) { v.isOverride = true; v.overrideAt = peek().pos; }
        else    { v.isFinal = true;    v.finalAt = peek().pos; }
        if (classStack_.empty() && !replayingInline_)
            src_.fail(peek().pos, std::string("'") + peek().text + "' can only be written on a "
                                  "member function's declaration inside its class - "
                                  "[class.mem] - and this declaration is outside one");
        at_++;
    }
    if (!classStack_.empty()) return;
    pendingVirt_ = VirtSpecifiers();
}

// A constructor, a static member or a data member may carry neither word.
void Parser::refuseVirtSpecifiers(const char *what) {
    const VirtSpecifiers v = takeVirtSpecifiers();
    if (v.isOverride)
        src_.fail(v.overrideAt, std::string("'override' marks a virtual member function, and ") + what +
                                " cannot be virtual");
    if (v.isFinal)
        src_.fail(v.finalAt, std::string("'final' marks a virtual member function, and ") + what +
                             " cannot be virtual");
}

long long Parser::constantExpression(const char *what) {
    std::size_t pos = peek().pos;
    ExprPtr e = decay(conditional());
    long long v;
    if (!constant_.fold(*e, &v, pos))
        src_.fail(pos, std::string("expected ") + what +
                       ", and this is not an integer constant expression");
    return v;
}

// The same types, in any order and however often each is written.
bool sema::DynamicSpec::sameSet(const DynamicSpec &o) const {
    auto within = [](const std::vector<const Type *> &a, const std::vector<const Type *> &b) {
        for (const Type *t : a) {
            bool found = false;
            for (const Type *u : b) found = found || t == u || t->describe() == u->describe();
            if (!found) return false;
        }
        return true;
    };
    return within(types, o.types) && within(o.types, types);
}

// ---------------------------------------------------------------- constant objects
// **[expr.const]/2 lets a literal class's object be worked out while compiling**: a constexpr
// constructor's lowered body runs over its scalar leaves, a pointer being object and offset. A
// bit-field, an address, a union read through another member: none folds, and the need is refused.
