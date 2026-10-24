// The parser: the pieces the rest of it is built on. Reading tokens, looking a
// name up as a type or an enumerator, deciding what is being declared, replaying
// the bodies a class held back, and below those the scopes and symbol tables.

#include "Parser.h"
#include "ParserInternal.h"
#include "../Mangle.h"
#include "../Source.h"

#include <algorithm>
#include <climits>
#include <cstring>

int alignTo(int n, int a) { return (n + a - 1) / a * a; }

// Recognised by the lexer, with no rule in this parser yet. Naming the keyword is
// the whole point: without this the word reaches expression parsing as an unknown
// identifier and the error lands on whatever follows it.
const char *notYetSupported(const std::string &word) {
    static const char *const pending[] = {
        "asm",
        "export"
    };
    for (const char *k : pending)
        if (word == k) return k;
    return nullptr;
}

// **A keyword this parser does implement, standing where it has no rule for it.**
// Each was measured in a place of its own before being moved here, because the
// two answers are different claims: one says the compiler cannot, one says not here.
const char *implementedElsewhere(const std::string &word) {
    static const char *const elsewhere[] = {
        "catch", "friend", "inline", "mutable", "namespace",
        "operator", "template", "using", "virtual"
    };
    for (const char *k : elsewhere)
        if (word == k) return k;
    return nullptr;
}

// **A null pointer constant**, which C++11 spells two ways: the integer
// literal 0, and `nullptr`. [conv.ptr]/1 - and everything that asks this
// question wants both, which is why `nullptr` needed no new call site here.
bool isNullConstant(const Expr &e) {
    if (e.type() != nullptr && e.type()->isNullPtr()) return true;
    const Num *n = dynamic_cast<const Num *>(&e);
    return n != nullptr && n->type()->isInteger() && n->value() == 0;
}

// Does this expression name an object at all - what [basic.lval] calls a glvalue?
// An lvalue and an xvalue both do and the difference is not asked here: reference
// binding wants an address, and either will give one.
bool isGlvalue(const Expr &e) {
    if (dynamic_cast<const Var *>(&e)) return true;
    if (dynamic_cast<const MemberAccess *>(&e)) return true;
    if (const Unary *u = dynamic_cast<const Unary *>(&e)) return u->op() == '*';
    // **A comma has the value category of its right operand** - [expr.comma], and
    // it is C++'s rule rather than C's. It matters because `b.count` for a static
    // member is built as one: the object is dropped, the shared object is named.
    if (const Comma *c = dynamic_cast<const Comma *>(&e))
        return isGlvalue(c->right());
    return false;
}

// An object something else may still be looking at: the question above minus what
// `static_cast<T &&>` hands over, and that subtraction is what an rvalue reference
// is for - it refuses an lvalue, so a move can empty what it is given.
bool isLvalue(const Expr &e) {
    return !e.isXvalue() && isGlvalue(e);
}

// An lvalue, or an expression marked as one where its node is not an object's - an assignment and a
// prefix `++` or `--`, which name their left operand.
bool markedLvalue(const Expr &e) {
    return isLvalue(e) || (!e.isXvalue() && e.declCategory() == Expr::Category::Lvalue);
}

// A class temporary is a dereference of its slot and carries the prvalue mark, so the mark is
// asked first; an xvalue is an rvalue; a reference cast marked Lvalue names an object.
bool objectIsLvalue(const Expr &e) {
    if (e.declCategory() == Expr::Category::Prvalue || e.isXvalue()) return false;
    return e.declCategory() == Expr::Category::Lvalue || isLvalue(e);
}

const Token &Parser::peekAt(std::size_t n) const {
    std::size_t i = at_ + n;
    return i < tokens_.size() ? tokens_[i] : tokens_.back();
}

bool Parser::consume(const char *s) {
    if (!peek().is(s)) return false;
    at_++;
    return true;
}

void Parser::expect(const char *s) {
    if (!peek().is(s)) {
        // A missing ';' belongs to the statement before it: said just after its last token, as clang says it,
        // and not at the next token - a '}' a line further down, or the next statement's first word.
        const Token *before = at_ > 0 && at_ <= tokens_.size() ? &tokens_[at_ - 1] : 0;
        const bool after = std::string(s) == ";" && before && before->end > before->pos;
        src_.fail(after ? before->end : peek().pos, std::string("expected '") + s + "'");
    }
    at_++;
}

std::string Parser::expectIdent(const char *what) {
    if (peek().kind != TokenKind::Ident) {
        // A keyword standing where a name was wanted is a feature this parser has
        // not grown rather than a slip of the finger, and "expected a name" points
        // at the word without saying what is wrong. `int operator+(int);` is why.
        if (const char *pending = notYetSupported(peek().text))
            src_.fail(peek().pos, std::string("'") + pending +
                                  "' is not supported yet");
        if (const char *here = implementedElsewhere(peek().text))
            src_.fail(peek().pos, std::string("'") + here + "' is implemented, "
                                  "but it cannot be a name - " + what +
                                  " was wanted here");
        src_.fail(peek().pos, std::string("expected ") + what);
    }
    std::string name = peek().text;
    at_++;
    return name;
}

long long Parser::expectNumber(const char *what) {
    if (peek().kind != TokenKind::Num)
        src_.fail(peek().pos, std::string("expected ") + what);
    long long v = peek().value;
    at_++;
    return v;
}

bool Parser::insideClass(const Type *cls) const {
    for (std::size_t i = 0; i < classStack_.size(); i++)
        for (const Type *c = classStack_[i]; c != nullptr; c = c->enclosing())
            if (c == cls) return true;
    for (const Type *c = currentClass_; c != nullptr; c = c->enclosing())
        if (c == cls) return true;
    return false;
}

// **Does the declarator after `from` define a member of `cls`?**
bool Parser::definesMemberOf(const Type *cls, std::size_t from) const {
    if (cls == nullptr) return false;
    for (std::size_t k = from; ; k++) {
        const Token &t = peekAt(k);
        if (t.kind == TokenKind::End) return false;
        if (t.is("(") || t.is(";") || t.is("{") || t.is("=")) return false;
        if (t.kind != TokenKind::Ident || !peekAt(k + 1).is("::")) continue;

        // The longest run of `Ident ::` starting here that names a type.
        std::string name = t.text;
        const Type *best = nullptr;
        for (std::size_t j = k; peekAt(j).kind == TokenKind::Ident &&
                                peekAt(j + 1).is("::"); j += 2) {
            if (j != k) name += "::" + peekAt(j).text;
            if (const Type *found = lookup_.findTypedef(name)) best = found;
        }
        if (best != nullptr)
            for (const Type *c = best; c != nullptr; c = c->enclosing())
                if (c == cls) return true;
    }
}

std::size_t Parser::namespaceQualifierTokens(std::size_t from) const {
    if (peekAt(from).kind != TokenKind::Ident || !peekAt(from + 1).is("::")) return 0;
    std::string scope = lookup_.namespaceNamed(peekAt(from).text);
    if (scope.empty()) return 0;
    std::size_t n = 2;
    while (peekAt(from + n).kind == TokenKind::Ident && peekAt(from + n + 1).is("::") &&
           lookup_.namespaces_.count(scope + "::" + peekAt(from + n).text)) {
        scope += "::" + peekAt(from + n).text;
        n += 2;
    }
    return n;
}

// `Point::Point(`, `Outer::Inner::Inner(` and `Outer::Inner::~Inner(` - a
// member definition whose name is its class's own, and so has no type in
// front of it. Every level is asked, because the class may itself be nested.
bool Parser::atUntypedMemberDefinition() const {
    if (peek().kind != TokenKind::Ident || !peekAt(1).is("::")) return false;
    std::string q = peek().text;
    for (std::size_t k = 1; peekAt(k).is("::"); ) {
        std::size_t n = k + 1;
        const bool destructor = peekAt(n).is("~");
        if (destructor) n++;
        if (peekAt(n).kind != TokenKind::Ident) return false;
        const std::string component = peekAt(n).text;
        const Type *cls = lookup_.findTypedef(q);
        if (cls != nullptr && cls->isStructOrUnion() &&
            cls->localName() == component && peekAt(n + 1).is("("))
            return true;
        if (destructor) return false;
        q += "::" + component;
        k = n + 1;
    }
    return false;
}

bool Parser::atTypeName() const {
    static const char *const t[] = { "void", "bool", "char", "short", "int",
                                     "long", "signed", "unsigned", "wchar_t",
                                     "char16_t", "char32_t",
                                     "float", "double",
                                     "struct", "union", "enum",
                                     "const", "volatile",
                                     // It says the next thing is a type, and saying so is the whole of what it does.
                                     "typename",
                                     // And this one *is* a type, written as a
                                     // question about an expression.
                                     "decltype" };
    for (const char *k : t)
        if (peek().is(k)) return true;
    // `::Lexer *p;` declares a p - the leading `::` says where to look and
    // nothing else, so the question is about the name after it.
    if (peek().is("::") && peekAt(1).kind == TokenKind::Ident)
        return lookup_.findGlobalTypedef(peekAt(1).text) != nullptr;
    if (peek().kind != TokenKind::Ident) return false;
    if (localHidesType()) return false;
    // `Box<int> b;` declares a variable, so the name has to answer yes here -
    // but only with a `<` after it, since the bare name is not a type.
    auto tmpl = tmpl_.templates_.find(peek().text);
    if (tmpl != tmpl_.templates_.end() && tmpl->second.isClass && peekAt(1).is("<"))
        return true;
    if (lookup_.findTypedef(peek().text) != nullptr) return true;
    // `N::S` names a type as much as `Outer::Inner` does, and neither answers to
    // its leading name alone. One of those is a namespace and the other a class;
    // past that first token the walk is the same walk.
    return qualifiedTypeEnd() != 0;
}

// How many tokens of an `A::B::C` chain starting here reach a type, or 0 if
// none does. The longest prefix that names one wins, so `Outer::Inner::shared`
// stops at Inner and `N::M::S` runs to the end.
std::size_t Parser::qualifiedTypeEnd() const {
    if (peek().kind != TokenKind::Ident || !peekAt(1).is("::")) return 0;
    std::string q = peek().text;
    std::size_t typeEnd = 0;
    const Type *cur = lookup_.findTypedef(q);
    for (std::size_t k = 1; peekAt(k).is("::") &&
                            peekAt(k + 1).kind == TokenKind::Ident;
         k += 2) {
        q += "::" + peekAt(k + 1).text;
        if (const Type *t = lookup_.findTypedef(q)) { typeEnd = k + 2; cur = t; }
        // `std::string::size_type`: a member type of the class a typedef reached.
        else if (cur != nullptr && cur->isStructOrUnion() &&
                 (cur = lookup_.lookupInClass(cur, peekAt(k + 1).text)) != nullptr)
            typeEnd = k + 2;
        // **`std::vector<int> v;` - the name stops at a class template too.**
        if (peekAt(k + 2).is("<") && isClassTemplate(q)) typeEnd = k + 2;
    }
    return typeEnd;
}

const Type *Parser::qualifiedTypeAt(std::size_t end) const {
    std::string q = peek().text;
    const Type *cur = lookup_.findTypedef(q);
    for (std::size_t k = 1; k + 1 < end; k += 2) {
        q += "::" + peekAt(k + 1).text;
        if (const Type *t = lookup_.findTypedef(q)) cur = t;
        else cur = cur != nullptr && cur->isStructOrUnion()
                       ? lookup_.lookupInClass(cur, peekAt(k + 1).text) : nullptr;
    }
    return cur;
}

std::size_t Parser::qualifiedTypeEndPastArgs() const {
    std::size_t after = qualifiedTypeEnd();
    if (after == 0 || !peekAt(after).is("<")) return after;
    // By depth, counting a `>>` as two - which is how the lexer hands it over.
    int depth = 0;
    for (;; after++) {
        const Token &t = peekAt(after);
        if (t.kind == TokenKind::End) return 0;
        if (t.is("<")) depth++;
        else if (t.is(">")) { if (--depth == 0) return after + 1; }
        else if (t.is(">>")) { depth -= 2; if (depth <= 0) return after + 1; }
    }
}

// [stmt.ambig]/1: `T(x).f();` cannot be a declaration - no declarator goes on with `.` or `->`
// after its `)` - so it is a temporary of T and a member call. True when that is what is here.
// Where `T(...)` opening the statement closes: the index of its `)`, or 0 where it does not open so.
std::size_t Parser::temporaryCallEnd() const {
    if (peek().kind != TokenKind::Ident) return 0;
    std::size_t k = 0;
    if (peekAt(1).is("::")) k = qualifiedTypeEndPastArgs();
    else if (localHidesType()) return 0;
    else if (peekAt(1).is("<") && isClassTemplate(peek().text)) {
        int depth = 0;
        for (k = 1;; k++) {
            const Token &t = peekAt(k);
            if (t.kind == TokenKind::End || t.is(";")) return 0;
            if (t.is("<")) depth++;
            else if (t.is(">")) { if (--depth == 0) { k++; break; } }
            else if (t.is(">>")) { depth -= 2; if (depth <= 0) { k++; break; } }
        }
    } else if (lookup_.findTypedef(peek().text) != nullptr) k = 1;
    if (k == 0 || !peekAt(k).is("(")) return 0;
    int depth = 0;
    for (;; k++) {
        const Token &t = peekAt(k);
        if (t.kind == TokenKind::End) return 0;
        if (t.is("(") || t.is("[") || t.is("{")) depth++;
        else if (t.is(")") || t.is("]") || t.is("}")) { if (--depth == 0) return k; }
    }
}

bool Parser::atTemporaryMemberAccess() const {
    const std::size_t k = temporaryCallEnd();
    return k != 0 && (peekAt(k + 1).is(".") || peekAt(k + 1).is("->"));
}

// **[stmt.select]/1: a condition declares only where `=` or `{` follows its declarator**, so after
// `T(x)` anything else - `==`, `<`, `&&`, `)` - makes `T(x)` a temporary and the condition an expression.
bool Parser::atConditionDeclaration() const {
    if (!atDeclarationStart()) return false;
    const std::size_t k = temporaryCallEnd();
    return k == 0 || peekAt(k + 1).is("=") || peekAt(k + 1).is("{");
}

bool Parser::atDeclarationStart() const {
    if (atTemporaryMemberAccess()) return false;
    // **`Counter::total = 1;` is a statement, not a declaration**, though it opens
    // with a name that names a type: a declaration spelled `C::something` needs a
    // nested class, so a class name before '::' always begins an expression here.
    if (peek().kind == TokenKind::Ident && peekAt(1).is("::")) {
        const Type *named = lookup_.findTypedef(peek().text);
        // A namespace comes here for the same reason a class does: `N::v = 1;`
        // is a statement and `N::S s;` is a declaration, and only the walk
        // below tells them apart.
        if ((named != nullptr && named->isStructOrUnion()) ||
            !lookup_.namespaceNamed(peek().text).empty()) {
            // ...**unless the qualified name is itself a type**: it declares only
            // where the name *stops* at one. `Outer::Inner x;` declares an x;
            // `Outer::Inner::shared = 1;` goes on past the type and assigns.
            const std::size_t typeEnd = qualifiedTypeEnd();
            if (typeEnd == 0 || peekAt(typeEnd).is("::")) return false;
            // **`std::vector<int>().swap(v);` is a statement**, and an empty pair after the type is
            // what says so: a declaration needs a name between the type and the `(`, so `T ();`
            // could only be a function declaration with no name, which is not a thing.
            const std::size_t after = qualifiedTypeEndPastArgs();
            if (after == 0) return true;
            return !(peekAt(after).is("(") && peekAt(after + 1).is(")"));
        }
    }

    // **A leading class template-id as a qualifier**:
    // `CNeeds<(N==3)>::check()` is a static-member call, a statement, where
    // `CNeeds<3>::Inner x;` would be a declaration.
    if (peek().kind == TokenKind::Ident && peekAt(1).is("<") &&
        isClassTemplate(peek().text)) {
        std::size_t after = 1;
        int depth = 0;
        for (;; after++) {
            const Token &t = peekAt(after);
            if (t.kind == TokenKind::End) { after = 0; break; }
            if (t.is("<")) depth++;
            else if (t.is(">")) { if (--depth == 0) { after++; break; } }
            else if (t.is(">>")) { depth -= 2; if (depth <= 0) { after++; break; } }
        }
        // And `Tm<P>::st.a = 3;` names a static member: a declaration goes on
        // from the member with a declarator - a name, `*`, `&`, `::` or `<` -
        // and anything else is an expression built on it (the cl review's A13).
        if (after != 0 && peekAt(after).is("::") &&
            peekAt(after + 1).kind == TokenKind::Ident) {
            const Token &next = peekAt(after + 2);
            if (next.kind != TokenKind::Ident && !next.is("*") && !next.is("&") &&
                !next.is("&&") && !next.is("::") && !next.is("<"))
                return false;
        }
    }

    // The same empty pair, for a type named without a qualifier: `P().get();`
    // is the temporary and not a declaration of anything.
    if (peek().kind == TokenKind::Ident && peekAt(1).is("(") &&
        peekAt(2).is(")") && lookup_.findTypedef(peek().text) != nullptr)
        return false;

    // `constexpr` beside the storage classes, and here rather than in atTypeName:
    // it is a decl-specifier that names no type, so a statement starting with it
    // is a declaration and nothing else - or an expression is asked for instead.
    return atTypeName() || peek().is("static") || peek().is("extern")
        || peek().is("register") || peek().is("auto") || peek().is("typedef")
        || peek().is("constexpr") || peek().is("alignas") || peek().is("class")
        || peek().is("thread_local")
        || (peek().is("[") && peekAt(1).is("["));
}

// From the '{' to the '}' that closes it, counting depth.
void Parser::skipBracedBlock() {
    // A function try block's handlers are its body too, [dcl.fct.def.general]/1.
    const bool functionTry = peek().is("try");
    // A constructor's body may be preceded by its mem-initializer list, so what is skipped starts at the
    // ':' - and a '{' inside an argument's parentheses, `i(Info{...})`, or after a name, `i{1}`, is an
    // initialiser's, not the body's (M1 L15).
    int parens = 0;
    bool memInits = false;
    while (!peek().is("{") || parens > 0 ||
           (memInits && at_ > 0 && (tokens_[at_ - 1].kind == TokenKind::Ident || tokens_[at_ - 1].is(">") ||
                                    tokens_[at_ - 1].is(">>")))) {
        if (peek().kind == TokenKind::End)
            src_.fail(peek().pos, "this member function has no body after all");
        if (peek().is(":") && parens == 0) memInits = true;
        if (peek().is("(")) parens++;
        else if (peek().is(")")) parens--;
        else if (peek().is("{") && parens == 0) {      // a braced mem-initialiser, stepped over whole
            for (int depth = 0;; at_++) {
                if (peek().kind == TokenKind::End)
                    src_.fail(peek().pos, "this member function has no body after all");
                if (peek().is("{")) depth++;
                else if (peek().is("}") && --depth == 0) break;
            }
        }
        at_++;
    }
    int depth = 0;
    for (;;) {
        if (peek().kind == TokenKind::End)
            src_.fail(peek().pos, "this member function's body is never closed");
        if (peek().is("{")) depth++;
        else if (peek().is("}")) {
            depth--;
            at_++;
            if (depth == 0 && !(functionTry && peek().is("catch"))) return;
            continue;
        }
        at_++;
    }
}

// Each held body re-read as though it had been written outside the class. The
// tokens are the ones already there, so the ordinary definition path runs over
// them, and `inlineOwner_` supplies the `Class::` the source does not have.
Parser::FunctionState Parser::captureFunctionState() const {
    FunctionState s;
    s.at = at_;
    s.currentFunction = currentFunction_;
    s.currentFunctionName = currentFunctionName_;
    s.functionName = functionName_;
    s.localTypes = localTypes_;
    s.localTypeUndo = localTypeUndo_;
    s.localTypeUndoStarts = localTypeUndoStarts_;
    s.locals = locals_;
    s.fnVars = fnVars_;
    s.scopeStarts = scopeStarts_;
    s.blocks = blocks_;
    s.blockStack = blockStack_;
    s.labels = labels_;
    s.gotos = gotos_;
    s.frameSize = frameSize_;
    s.frameAlign = frameAlign_;
    s.freeSlots = freeSlots_;
    s.localSerial = localSerial_;
    s.thisOffset = thisOffset_;
    s.currentClass = currentClass_;
    s.returnType = returnType_;
    s.sretSlot = sretSlot_;
    s.nrvoName = nrvoName_;
    s.deducingReturn = deducingReturn_;
    s.unaryOperand = unaryOperand_;
    s.condParts = condParts_;
    s.callArgs = callArgs_;
    s.unevaluated = unevaluated_;
    s.absoluteLookup = absoluteLookup_;
    s.lambdaCount = lambdaCount_;
    s.atFunctionBody = atFunctionBody_;
    s.alive = alive_;
    s.pendingTemps = pendingTemps_;
    s.bodyCleanupFrom = bodyCleanupFrom_;
    s.functionHasPads = functionHasPads_;
    s.functionHasTry = functionHasTry_;
    s.functionTypes = functionTypes_;
    s.variadicBody = variadicBody_;
    s.inStaticMember = inStaticMember_;
    s.inParams = inParams_;
    s.staticSymbols = linkage_.staticSymbols_;
    s.pendingDefaults = pendingDefaults_;
    s.pendingNoexcept = pendingNoexcept_;
    s.pendingSpec = pendingSpec_;
    s.inNoexceptFunction = inNoexceptFunction_;
    s.replayingInline = replayingInline_;
    s.currentFunctionInline = currentFunctionInline_;
    s.tssSeq = tssSeq_;
    s.loopDepth = loopDepth_;
    s.switchDepth = switchDepth_;
    s.switches = switches_;
    s.breakMarks = breakMarks_;
    s.inTryBody = inTryBody_;
    s.inMsHandler = inMsHandler_;
    s.msExit = msExit_;
    s.inHandlerBody = inHandlerBody_;
    s.inCtorTryHandler = inCtorTryHandler_;
    s.handlerDepth = handlerDepth_;
    s.handlerLoopDepth = handlerLoopDepth_;
    s.handlerSwitchDepth = handlerSwitchDepth_;
    s.handlerFrom = handlerFrom_;
    s.tryFrom = tryFrom_;
    s.handlerResumeLabel = handlerResumeLabel_;
    s.tryChainLabel = tryChainLabel_;
    s.tryChainPointerSlot = tryChainPointerSlot_;
    s.tryChainSelectorSlot = tryChainSelectorSlot_;
    s.tryChainAliveFrom = tryChainAliveFrom_;
    s.tryBodySegments = tryBodySegments_;
    s.handlerResumePtr = handlerResumePtr_;
    s.handlerResumeSel = handlerResumeSel_;
    s.mayThrow = mayThrow_;
    s.conditionDecl = conditionDecl_;
    s.conditionName = conditionName_;
    s.usingNamespaces = lookup_.usingNamespaces_;
    return s;
}

void Parser::restoreFunctionState(const FunctionState &s) {
    at_ = s.at;
    currentFunction_ = s.currentFunction;
    currentFunctionName_ = s.currentFunctionName;
    functionName_ = s.functionName;
    localTypes_ = s.localTypes;
    localTypeUndo_ = s.localTypeUndo;
    localTypeUndoStarts_ = s.localTypeUndoStarts;
    locals_ = s.locals;
    fnVars_ = s.fnVars;
    scopeStarts_ = s.scopeStarts;
    blocks_ = s.blocks;
    blockStack_ = s.blockStack;
    labels_ = s.labels;
    gotos_ = s.gotos;
    frameSize_ = s.frameSize;
    frameAlign_ = s.frameAlign;
    freeSlots_ = s.freeSlots;
    localSerial_ = s.localSerial;
    thisOffset_ = s.thisOffset;
    currentClass_ = s.currentClass;
    returnType_ = s.returnType;
    sretSlot_ = s.sretSlot;
    nrvoName_ = s.nrvoName;
    deducingReturn_ = s.deducingReturn;
    unaryOperand_ = s.unaryOperand;
    condParts_ = s.condParts;
    callArgs_ = s.callArgs;
    unevaluated_ = s.unevaluated;
    absoluteLookup_ = s.absoluteLookup;
    lambdaCount_ = s.lambdaCount;
    atFunctionBody_ = s.atFunctionBody;
    alive_ = s.alive;
    pendingTemps_ = s.pendingTemps;
    bodyCleanupFrom_ = s.bodyCleanupFrom;
    functionHasPads_ = s.functionHasPads;
    functionHasTry_ = s.functionHasTry;
    functionTypes_ = s.functionTypes;
    variadicBody_ = s.variadicBody;
    inStaticMember_ = s.inStaticMember;
    inParams_ = s.inParams;
    linkage_.staticSymbols_ = s.staticSymbols;
    pendingDefaults_ = s.pendingDefaults;
    pendingNoexcept_ = s.pendingNoexcept;
    pendingSpec_ = s.pendingSpec;
    inNoexceptFunction_ = s.inNoexceptFunction;
    replayingInline_ = s.replayingInline;
    currentFunctionInline_ = s.currentFunctionInline;
    tssSeq_ = s.tssSeq;
    loopDepth_ = s.loopDepth;
    switchDepth_ = s.switchDepth;
    switches_ = s.switches;
    breakMarks_ = s.breakMarks;
    inTryBody_ = s.inTryBody;
    inMsHandler_ = s.inMsHandler;
    msExit_ = s.msExit;
    inHandlerBody_ = s.inHandlerBody;
    inCtorTryHandler_ = s.inCtorTryHandler;
    handlerDepth_ = s.handlerDepth;
    handlerLoopDepth_ = s.handlerLoopDepth;
    handlerSwitchDepth_ = s.handlerSwitchDepth;
    handlerFrom_ = s.handlerFrom;
    tryFrom_ = s.tryFrom;
    handlerResumeLabel_ = s.handlerResumeLabel;
    tryChainLabel_ = s.tryChainLabel;
    tryChainPointerSlot_ = s.tryChainPointerSlot;
    tryChainSelectorSlot_ = s.tryChainSelectorSlot;
    tryChainAliveFrom_ = s.tryChainAliveFrom;
    tryBodySegments_ = s.tryBodySegments;
    handlerResumePtr_ = s.handlerResumePtr;
    handlerResumeSel_ = s.handlerResumeSel;
    mayThrow_ = s.mayThrow;
    conditionDecl_ = s.conditionDecl;
    conditionName_ = s.conditionName;
    lookup_.usingNamespaces_ = s.usingNamespaces;
}

// A body parsed behind one of these doors is a *different* function, so it
// starts with none of the enclosing one's state rather than a copy of it.
// `at_` is left alone: the caller has just pointed it at what to read.
void Parser::clearFunctionState() {
    currentFunction_.clear();
    currentFunctionName_.clear();
    functionName_.clear();
    localTypes_.clear();
    localTypeUndo_.clear();
    localTypeUndoStarts_.clear();
    locals_.clear();
    fnVars_.clear();
    scopeStarts_.clear();
    blocks_.clear();
    blockStack_.clear();
    labels_.clear();
    gotos_.clear();
    frameSize_ = 0;
    frameAlign_ = 16;
    freeSlots_.clear();
    localSerial_ = 0;
    thisOffset_ = 0;
    currentClass_ = nullptr;
    returnType_ = nullptr;
    sretSlot_ = 0;
    nrvoName_.clear();
    deducingReturn_ = nullptr;
    unaryOperand_ = syntax::ExprId();
    condParts_ = CondParts();
    callArgs_.clear();
    lambdaCount_ = 0;
    unevaluated_ = 0;
    absoluteLookup_ = false;
    atFunctionBody_ = false;
    alive_.clear();
    pendingTemps_.clear();
    bodyCleanupFrom_ = 0;
    functionHasPads_ = false;
    functionHasTry_ = false;
    functionTypes_.clear();
    variadicBody_ = false;
    inStaticMember_ = false;
    inParams_ = false;
    linkage_.staticSymbols_.clear();
    pendingDefaults_.clear();
    pendingNoexcept_ = false;
    pendingSpec_ = DynamicSpec();
    inNoexceptFunction_ = false;
    currentFunctionInline_ = false;
    tssSeq_ = 0;
    loopDepth_ = 0;
    switchDepth_ = 0;
    switches_.clear();
    breakMarks_.clear();
    inTryBody_ = false;
    inMsHandler_ = false;
    msExit_ = nullptr;
    inHandlerBody_ = false;
    inCtorTryHandler_ = false;
    handlerDepth_ = 0;
    handlerLoopDepth_.clear();
    handlerSwitchDepth_.clear();
    handlerFrom_.clear();
    tryFrom_.clear();
    handlerResumeLabel_.clear();
    tryChainLabel_.clear();
    tryChainPointerSlot_ = tryChainSelectorSlot_ = 0;
    tryChainAliveFrom_ = 0;
    tryBodySegments_.clear();
    handlerResumePtr_ = 0;
    handlerResumeSel_ = 0;
    mayThrow_ = 0;
    conditionDecl_ = false;
    conditionName_.clear();
}

void Parser::replayInlineBodies(std::vector<PendingBody> mine) {
    if (mine.empty()) return;
    // **One capture for the whole nested parse.**
    const FunctionState outer = captureFunctionState();
    clearFunctionState();
    // A local class's bodies replay inside its function, where it and its neighbours are local types (1.27).
    localTypes_ = outer.localTypes;
    // A body replayed here was written inside a class body, so every definition it produces is implicitly inline.
    replayingInline_ = true;

    // **The outer function comes back on every way out**: a member template instantiated
    // inside a trial may throw from here, and the call that follows still reads its locals.
    try {
    for (std::size_t i = 0; i < mine.size(); i++) {
        localTypes_ = outer.localTypes;     // each body's end clears them, and the next names them too
        at_ = mine[i].start;
        // A friend's body is written inside the class and belongs outside it,
        // so it is replayed with no owner to supply a `Class::` it must not have.
        inlineOwner_ = mine[i].freeFunction ? std::string() : mine[i].tag;
        inlineOwnerName_ = mine[i].freeFunction ? std::string()
                         : (mine[i].local.empty() ? mine[i].tag : mine[i].local);
        // **A friend is not a member, but it is still looked up in the
        // class.**
        pendingDefaults_.clear();
        pendingNoexcept_ = false;
        pendingSpec_ = DynamicSpec();
        const bool scoped = mine[i].freeFunction && !mine[i].tag.empty();
        const Type *scope = scoped ? lookup_.findTypedef(mine[i].tag) : nullptr;
        if (scope != nullptr) classStack_.push_back(scope);
        topLevel(*current_);
        if (scope != nullptr) classStack_.pop_back();
        inlineOwner_.clear();
        inlineOwnerName_.clear();
    }
    } catch (...) {
        inlineOwner_.clear();
        inlineOwnerName_.clear();
        restoreFunctionState(outer);
        throw;
    }
    restoreFunctionState(outer);
}

// **One level of the program's nesting is one level of the parser's**, and the limit is said
// where it is reached, as the '#include' and constant-expression depths are: a bound named is
// better than a stack that is not, and the standard lets an implementation set one.
Parser::Nesting::Nesting(Parser *pp, std::size_t pos, const char *what) : p(pp) {
    if (++p->nesting_ <= kMaxNesting) return;
    p->nesting_--;
    p->src_.fail(pos, std::string("this ") + what + " is nested more than " +
                      std::to_string(kMaxNesting) + " levels deep - parentheses, blocks, "
                      "statements, lambdas and template instantiations counted together - and "
                      "the compiler, which follows the program's nesting with its own, stops "
                      "here rather than run out of stack");
}

void Parser::enterScope() {
    scopeStarts_.push_back(locals_.size());
    localTypeUndoStarts_.push_back(localTypeUndo_.size());
}

int Parser::enterBlock() {
    blocks_.push_back(currentBlock());
    int id = static_cast<int>(blocks_.size()) - 1;
    blockStack_.push_back(id);
    return id;
}

void Parser::leaveBlock() { blockStack_.pop_back(); }

// **A scope's frame slots are given back as it closes**, for a later local of exactly the type stored there: the
// objects are dead ([basic.life]), nothing but a sibling scope can declare into the gap, and one type per slot is
// what every backend's slot-keyed bookkeeping - the C6000's register planner, the hosts' promotion - already assumes.
void Parser::leaveScope() {
    for (std::size_t i = scopeStarts_.back(); i < locals_.size(); i++) {
        const Local &l = locals_[i];
        if (l.offset <= 0 || !l.staticName.empty() || l.isParameter || inParams_ || l.sharesSlot) continue;
        const Type *stored = l.type->isReference() ? types_.pointerTo(l.type->referent()) : l.type;
        freeSlots_.push_back(FreeSlot{ stored, l.offset, objectAlign(stored, target_) });
    }
    locals_.resize(scopeStarts_.back());
    scopeStarts_.pop_back();
    if (localTypeUndoStarts_.empty()) return;
    for (std::size_t i = localTypeUndo_.size(); i-- > localTypeUndoStarts_.back(); ) {
        if (localTypeUndo_[i].had != nullptr) localTypes_[localTypeUndo_[i].name] = localTypeUndo_[i].had;
        else localTypes_.erase(localTypeUndo_[i].name);
    }
    localTypeUndo_.resize(localTypeUndoStarts_.back());
    localTypeUndoStarts_.pop_back();
}

// A freed slot belongs to the frame that freed it: a function opened mid-parse starts with none, or it reuses an offset its own frame never reached.
Parser::FrameSave Parser::openFrame() {
    FrameSave saved{ frameSize_, std::move(freeSlots_) };
    frameSize_ = 0;
    freeSlots_.clear();
    return saved;
}

void Parser::closeFrame(const FrameSave &saved) {
    frameSize_ = saved.size;
    freeSlots_ = saved.free;
}

int Parser::allocateFrameSlot(const Type *type, int alignAtLeast) {
    // What a reference occupies is a pointer, even though sizeof asks about
    // what it refers to. This is the one place the difference shows.
    const Type *stored = type->isReference()
                       ? types_.pointerTo(type->referent()) : type;
    int align = objectAlign(stored, target_);
    if (alignAtLeast > align) align = alignAtLeast;
    frameSize_ += stored->size(target_);
    frameSize_ = alignTo(frameSize_, align);
    // Past the stack's alignment the whole frame is realigned to the widest slot, by its symbol.
    if (align > frameAlign_) {
        frameAlign_ = align;
        const std::string &owner = currentFunction_.empty() ? functionName_ : currentFunction_;
        if (frameAligns_[owner] < align) frameAligns_[owner] = align;
    }
    return frameSize_;
}

int Parser::declare(const std::string &name, const Type *type, std::size_t pos,
                    int alignAtLeast) {
    refuseWeakAlignas(alignAtLeast, type, pos);
    // A frame slot is only as aligned as the stack it hangs from; past that the
    // frame is realigned by the hosts' prologues, and the C6000's is not yet.
    const int typeAlign = type->isReference() ? 0 : type->align(target_);
    if ((alignAtLeast > target_.stackAlign() || typeAlign > target_.stackAlign()) && !target_.realignsFrame())
        src_.fail(pos, "'alignas(" + std::to_string(alignAtLeast > typeAlign ? alignAtLeast : typeAlign) + ")' on a "
                       "local is not supported yet on " + target_.name() + ": the stack is kept " +
                       std::to_string(target_.stackAlign()) + "-aligned there, and a local past that "
                       "would need the frame realigned, which that backend does not do. A global or "
                       "a member may ask for more");
    if (type->isVoid())
        src_.fail(pos, "'" + name + "' cannot have type void");
    std::size_t from = scopeStarts_.empty() ? 0 : scopeStarts_.back();
    for (std::size_t i = from; i < locals_.size(); i++)
        if (locals_[i].name == name)
            src_.fail(pos, "'" + name + "' is declared twice in this block");

    int offset = 0;
    const Type *storedType = type->isReference() ? types_.pointerTo(type->referent()) : type;
    for (std::size_t i = freeSlots_.size(); i-- > 0 && offset == 0;)
        if (freeSlots_[i].stored == storedType && freeSlots_[i].align >= alignAtLeast) { offset = freeSlots_[i].offset; freeSlots_.erase(freeSlots_.begin() + static_cast<long>(i)); }
    if (offset == 0) offset = allocateFrameSlot(type, alignAtLeast);
    locals_.push_back(Local{ name, offset, type, false, std::string() });
    locals_.back().isParameter = inParams_;
    locals_.back().serial = ++localSerial_;
    // The debug record describes the storage, which for a reference is the
    // pointer it really is. DWARF has a tag for a reference and this does not
    // use it yet.
    const Type *stored = type->isReference()
                       ? types_.pointerTo(type->referent()) : type;
    fnVars_.push_back(::Local{ name, stored, offset, inParams_, std::string(),
                              currentBlock() });
    fnVars_.back().align = alignAtLeast > objectAlign(stored, target_) ? alignAtLeast : objectAlign(stored, target_);
    return offset;
}

void Parser::declareStaticLocal(const std::string &name, const Type *type,
                                std::size_t pos, const std::string &symbol) {
    if (type->isVoid())
        src_.fail(pos, "'" + name + "' cannot have type void");
    std::size_t from = scopeStarts_.empty() ? 0 : scopeStarts_.back();
    for (std::size_t i = from; i < locals_.size(); i++)
        if (locals_[i].name == name)
            src_.fail(pos, "'" + name + "' is declared twice in this block");
    locals_.push_back(Local{ name, 0, type, false, symbol });
    fnVars_.push_back(::Local{ name, type, 0, false, symbol, currentBlock() });
}

// A thread_local has no address a debug record could hold - DWARF reaches one through the thread
// pointer, which these records do not write yet - so its static-local record is withdrawn.
void Parser::markThreadLocal(std::size_t local, bool threadLocal) {
    if (!threadLocal) return;
    locals_[local].isThreadLocal = true;
    for (std::size_t i = fnVars_.size(); i-- > 0;)
        if (fnVars_[i].staticName == locals_[local].staticName) { fnVars_.erase(fnVars_.begin() + static_cast<long>(i)); break; }
}

void Parser::tlsVar(Var *v, bool viaWrapper, bool external) {
    v->setThreadLocal(true);
    // Inside __tls_init an object of this unit is reached directly, as clang reaches it: the
    // function is already running, and on Windows its guard would call it again.
    if (currentFunction_ == "__tls_init" && tlsHere_.count(v->symbol()) != 0) viaWrapper = false;
    v->setViaWrapper(viaWrapper);
    if (external) tlsReached_.insert(v->symbol());
}

void Parser::tlsDefining(const std::string &symbol, bool external) {
    tlsHere_.insert(symbol);
    if (external) tlsDefined_.insert(symbol);
}

void Parser::refuseThreadLocalHere(bool written, std::size_t pos) {
    if (written) src_.fail(pos, "'thread_local' is only allowed on variable declarations");
}

void Parser::noteThreadLocal(GlobalSym &g, bool prior, bool threadLocal, const std::string &name,
                             bool viaWrapper, bool external, bool defines, std::size_t pos) {
    if (prior && g.isThreadLocal != threadLocal)
        src_.fail(pos, std::string(threadLocal ? "thread-local" : "non-thread-local") + " declaration of '" +
                       name + "' follows " + (threadLocal ? "non-thread-local" : "thread-local") + " declaration");
    g.isThreadLocal = threadLocal;
    if (!threadLocal) return;
    g.tlsViaWrapper = viaWrapper;
    g.tlsExternal = external;
    if (!defines) return;
    tlsDefining(g.symbol, external);
    if (viaWrapper && external) tlsDynamic_.insert(g.symbol);
    if (viaWrapper && !external) tlsInternalDynamic_.insert(g.symbol);
}

// [basic.scope.hiding]/2: `void f(tm *tm)` - in the body the parameter hides the class, so `tm`
// is an object there; a name before `::` still looks at types only, [basic.lookup.qual]/1.
bool Parser::localHidesType() const {
    if (peekAt(1).is("::") || localTypes_.count(peek().text) != 0) return false;
    if (lookup_.findLocal(peek().text) != nullptr) return true;
    // [basic.scope.hiding]/2: a function declared in the class's own scope under its name hides
    // it, so `stat(p, &st)` beside `struct stat` is a call; `struct stat` still names the class.
    const Type *named = lookup_.findTypedef(peek().text);
    if (named == nullptr || !named->isStructOrUnion() || named->localName() != peek().text) return false;
    for (std::size_t i = 0; i < classStack_.size(); i++)
        if (classStack_[i]->unqualified() == named->unqualified()) return false;
    if (currentClass_ != nullptr && currentClass_->unqualified() == named->unqualified()) return false;
    return lookup_.hasFunctionNamed(named->tag());
}

const Type *Parser::composite(const Type *a, const Type *b) {
    if (a == b) return a;
    if (!a->isArray() || !b->isArray()) return nullptr;

    const Type *elem = composite(a->pointee(), b->pointee());
    if (elem == nullptr) return nullptr;

    long long la = a->length(), lb = b->length();
    if (la >= 0 && lb >= 0 && la != lb) return nullptr;
    return types_.arrayOf(elem, la >= 0 ? la : lb);
}

ExprPtr Parser::defaultPromote(ExprPtr e) {
    if (e->type()->kind() == Kind::Float)
        return convert(std::move(e), types_.doubleType());
    if (e->type()->isInteger()) {
        const Type *to = promote(e->type());
        return convert(std::move(e), to);
    }
    return e;
}

// Building an object: the constructor is chosen from the arguments as any overload
// is, then called with the object's address in front of them. The object exists
// before the call - a frame slot like any local - and the constructor fills it.
