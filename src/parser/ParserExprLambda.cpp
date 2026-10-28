// The parser: lambdas and the closures they become. A lambda is a class this
// compiler writes - the captures become members and the body `operator()` - and
// deducing the return type is here too, since nothing else needs it.
#include "Parser.h"
#include "ParserInternal.h"
#include "../Mangle.h"
#include "../Source.h"

#include <climits>
#include <cstring>


// `[](int a) { return a * 2; }` - rung 7.6. **A closure is a class with a call
// operator**: the object lives in the enclosing frame, the body is replayed as a
// member function from synthesised tokens, the return type as a hidden typedef.
const Type *Parser::deduceLambdaReturn(std::size_t paramsFrom,
                                       std::size_t paramsTo,
                                       std::size_t bodyFrom,
                                       std::size_t bodyTo,
                                       const std::vector<std::string> &capNames,
                                       const std::vector<const Type *> &capTypes) {
    // **The whole body is read, and every `return` in it reports its
    // operand's type**, the first deciding - [expr.prim.lambda]/4 by C++14's
    // relaxation, which clang applies to C++11 too; a body with none is void.

    // **This is a nested parse of a different function and was saving three fields of it.**
    const FunctionState outer = captureFunctionState();
    // Nor are the enclosing block's temporaries and guards its own: the reading is thrown away, frame and all,
    // and what it made stayed for the block's pads to destroy, guarded by slots past the frame (M1).
    std::vector<Temporary> enclosingTemps;
    enclosingTemps.swap(statementTemps_);
    const std::size_t enclosingGuards = guardSlots_.size();
    // **`this` is the exception**: a member is reached through it, and the body
    // may name one, so it is carried across the clear.
    const Local *hadThis = lookup_.findLocal("this");
    Local keptThis;
    const bool haveThis = hadThis != nullptr;
    if (haveThis) keptThis = *hadThis;

    // **This door is not `clearFunctionState`'s kind.**
    // The enclosing function's const locals with known values stay readable, [expr.prim.lambda]/12,
    // as the values they are (1.27, 2026-10-21, review of 1.25, item 3).
    const std::vector<Local> constants = constantLocals();
    locals_.clear();
    locals_.insert(locals_.end(), constants.begin(), constants.end());
    if (haveThis) locals_.push_back(keptThis);
    scopeStarts_.clear();
    alive_.clear();
    pendingTemps_.clear();
    bodyCleanupFrom_ = 0;
    labels_.clear();
    gotos_.clear();
    functionHasPads_ = false;
    functionHasTry_ = false;
    functionTypes_.clear();
    inTryBody_ = false;
    inMsHandler_ = false;
    // **A lambda's body is a different function**, so a `return` in it ends no
    // catch of the function around it - [except.handle]/16 is about leaving
    // the *handler*, and this body is left through a call.
    handlerDepth_ = 0;
    inCtorTryHandler_ = false;
    handlerLoopDepth_.clear();
    handlerSwitchDepth_.clear();
    handlerFrom_.clear();
    tryFrom_.clear();
    handlerResumeLabel_.clear();
    mayThrow_ = 0;
    loopDepth_ = 0;
    switchDepth_ = 0;
    switches_.clear();
    breakMarks_.clear();
    conditionDecl_ = false;
    conditionName_.clear();
    enterScope();

    // The captures are in scope in the body as well as the parameters - by the
    // real parsing they are members - and are declared as locals here for want of
    // a closure. **In a scope of their own**, since a parameter may shadow one.
    for (std::size_t c = 0; c < capNames.size(); c++) {
        inParams_ = true;
        declare(capNames[c], capTypes[c], bodyFrom);
        inParams_ = false;
    }
    enterScope();

    if (paramsTo > paramsFrom) {
        const std::size_t save = at_;
        at_ = paramsFrom - 1;                           // at the '('
        std::vector<const Type *> ps;
        bool var = false;
        // Read the list again, this time declaring each name, so that the
        // expression below can mention them.
        at_ = save;
        std::size_t k = paramsFrom;
        while (k < paramsTo) {
            std::size_t from = k;
            int depth = 0;
            while (k < paramsTo &&
                   !(depth == 0 && tokens_[k].is(","))) {
                if (tokens_[k].is("(")) depth++;
                if (tokens_[k].is(")")) depth--;
                k++;
            }
            at_ = from;
            StorageClass psc;
            Qualifiers pq;
            const Type *pt = specifiers(&psc, &pq);
            Declared pd = declarator(pt, true);
            if (!pd.name.empty()) {
                inParams_ = true;
                declare(pd.name, pd.type, pd.pos);
                inParams_ = false;
            }
            (void)ps; (void)var;
            if (k < paramsTo) k++;                      // the ','
        }
    }

    at_ = bodyFrom + 1;                                 // past the '{'
    // The same choice the block loop makes: calling statement() alone reads
    // `auto i = ...;` as an expression. Until 2026-09-17 only a `return` at
    // the body's own brace level was seen, so one in a `try` left it void.
    const Type *found = nullptr;
    const std::size_t functionsBefore = current_->functions.size();
    const std::size_t globalsBefore = current_->globals.size();
    const std::size_t stringsBefore = current_->strings.size();
    const int stringCount = strings_;
    deducingReturn_ = &found;
    while (at_ < bodyTo && !peek().is("}") && peek().kind != TokenKind::End)
        if (atDeclarationStart()) declaration(); else statement();
    deducingReturn_ = nullptr;
    if (found == nullptr) found = types_.get(Kind::Void);
    // **The reading is thrown away, and so are its string literals** - unless
    // it emitted a function or a global (a nested closure's call operator,
    // say), which may name one; then every literal stays, as before.
    if (current_->functions.size() == functionsBefore &&
        current_->globals.size() == globalsBefore) {
        current_->strings.resize(stringsBefore);
        strings_ = stringCount;
    }

    leaveScope();                                       // parameters and body
    leaveScope();                                       // the captures
    restoreFunctionState(outer);
    statementTemps_.swap(enclosingTemps);
    guardSlots_.resize(enclosingGuards);
    return found;
}

// The locals in scope that a lambda's body may read without capturing them: const objects of
// integral type with a constant initialiser, [expr.prim.lambda]/12 with [basic.def.odr]/2 - kept
// as the values they are, since the lambda's frame is not the one they live in (1.27).
std::vector<Parser::Local> Parser::constantLocals() const {
    std::vector<Local> out;
    for (std::size_t i = 0; i < locals_.size(); i++) {
        const Local &l = locals_[i];
        if (!l.isConstantValue || l.type->isReference() || l.sharesSlot || l.name.empty()) continue;
        Local c = l;
        c.constantOnly = true;
        c.guardsJump = false;
        out.push_back(c);
    }
    return out;
}

// **The captured pointer has the type `this` has in the enclosing function** -
// [expr.prim.lambda]/18.
const Type *Parser::capturedThisClass() {
    const Local *enclosing = lookup_.findLocal("this");
    if (enclosing != nullptr && enclosing->type->pointee() != nullptr)
        return enclosing->type->pointee();
    return currentClass_;
}

bool Parser::namesOwnMember(const std::string &name) {
    if (currentClass_ == nullptr || lookup_.findLocal("this") == nullptr) return false;
    const Type *cls = currentClass_->unqualified();
    return cls->findMember(name) != nullptr ||
           lookup_.findMemberOwner(cls, name) != nullptr;
}

ExprPtr Parser::lambdaExpression() {
    const std::size_t pos = peek().pos;
    const std::size_t lamAt = at_;
    const Nesting deep(this, pos, "lambda");

    // Already built on an earlier reading of these same tokens: hand back
    // another object of the one class rather than making a second.
    std::map<std::pair<std::string, std::size_t>, MadeLambda>::const_iterator had =
        lambdaAt_.find(std::make_pair(currentFunction_, lamAt));
    if (had != lambdaAt_.end()) {
        at_ = had->second.end;
        lambdaBodies_ = had->second.bodies;
        return buildClosure(had->second, pos);
    }
    // Inside a closure's call operator, one the deducing reading already built
    // at this very `[` - the owner is the function that reading ran in.
    if (deducingReturn_ == nullptr && currentClass_ != nullptr) {
        const std::string &owner = currentClass_->unqualified()->localOwner();
        std::map<std::pair<std::string, std::size_t>, MadeLambda>::const_iterator made =
            deducedAt_.find(std::make_pair(owner, pos));
        if (made != deducedAt_.end()) {
            at_ = lamAt + made->second.span;
            lambdaBodies_ = made->second.bodies;
            return buildClosure(made->second, pos);
        }
    }

    at_++;                                    // '['

    // Refused by name, each for its own reason: a capture is what the reader
    // wrote. **A capture by value is a member of the closure**, so reading one
    // needs no new rule - in `operator()` a name already means `this->name`.
    std::vector<std::string> capNames;
    std::vector<const Type *> capTypes;
    std::vector<int> capOffsets;
    bool captureAllByValue = false;
    bool captureAllByRef = false;
    bool defaultTakesThis = false;
    const Type *capturedThisFrom = nullptr;
    if (peek().is("=") || peek().is("&")) {
        // `[&]` only where it is the whole list: `[&x]` is one named capture
        // and is read by the loop below.
        if (peek().is("=") || peekAt(1).is("]")) {
            captureAllByValue = peek().is("=");
            captureAllByRef = peek().is("&");
            at_++;
            if (!peek().is("]"))
                src_.fail(peek().pos, "naming a capture after a default one is "
                                      "not supported yet - '[=]' and '[&]' on "
                                      "their own take everything the body "
                                      "reads");
        }
    }
    while (!peek().is("]")) {
        // `[&x]` - this one by reference, whatever the default is.
        bool byRef = consume("&");
        // `[this]` - the closure holds a pointer to the enclosing object, and
        // an unqualified member name inside the body is reached through it.
        if (peek().is("this")) {
            if (byRef)
                src_.fail(peek().pos, "'&this' is not how 'this' is captured - "
                                      "write '[this]', which copies the "
                                      "pointer");
            if (currentClass_ == nullptr)
                src_.fail(peek().pos, "'[this]' is only inside a member "
                                      "function, and this lambda is not in one");
            at_++;
            const Type *from = capturedThisClass();
            capNames.push_back(capturedThis());
            capTypes.push_back(types_.pointerTo(from));
            capturedThisFrom = from;
            if (!peek().is("]")) expect(",");
            continue;
        }
        const std::size_t cpos = peek().pos;
        const std::string cname = expectIdent("a captured name");
        // **`[n = k]` is an init-capture, which is C++14.** Named here rather than
        // left to the lookup below: the name is the closure's own and need not be
        // a local, so the reader would be told a name they declared is unknown.
        if (peek().is("="))
            src_.fail(peek().pos, "an init-capture - '" + cname + " = ...' in "
                                  "the capture list - is C++14, and this "
                                  "compiler is C++11: a capture here names "
                                  "something the enclosing function already "
                                  "declared");
        const Local *have = lookup_.findLocal(cname);
        // [expr.prim.lambda]/10: a member of an anonymous union is no variable to capture, clang's words.
        if (have != nullptr && have->sharesSlot)
            src_.fail(cpos, "'" + cname + "' in capture list does not name a variable");
        const Type *fromOuter = nullptr;
        if (have == nullptr) {
            // A capture of the lambda around this one, which is a member of
            // that closure by now rather than a local.
            if (ExprPtr reach = outerCaptureAccess(cname))
                fromOuter = reach->type();
            if (fromOuter == nullptr)
                src_.fail(cpos, "'" + cname + "' is not a local of the function "
                                "around this lambda, nor a capture of a lambda "
                                "around it, so there is nothing here to "
                                "capture");
        }
        capNames.push_back(cname);
        // **By reference the closure holds a reference member**, which takes the
        // reference layout rule: a pointer slot where `sizeof` is the referent's.
        // By value it holds a copy, and capturing a reference copies its referent.
        const Type *raw = have != nullptr ? have->type : fromOuter;
        const Type *base = raw->isReference()
                         ? raw->referent()->unqualified() : raw->unqualified();
        capTypes.push_back(byRef ? referenceCaptureType(raw) : base);
        if (!peek().is("]")) expect(",");
    }
    at_++;                                    // ']'

    // The parameter list, which may be left out entirely.
    std::size_t paramsFrom = at_, paramsTo = at_;
    std::vector<const Type *> params;
    bool variadic = false;
    if (peek().is("(")) {
        paramsFrom = at_ + 1;
        parameterTypes(params, variadic);
        paramsTo = at_ - 1;                   // the ')' just consumed
    }
    if (variadic)
        src_.fail(pos, "a lambda cannot be variadic");

    // **`mutable` decides whether `operator()` is const** - [expr.prim.lambda].
    const bool isMutable = consume("mutable");

    const Type *returns = nullptr;
    if (consume("->")) {
        StorageClass rsc;
        returns = specifiers(&rsc);
        returns = declarator(returns, true).type;
    }
    if (!peek().is("{"))
        src_.fail(peek().pos, "expected the lambda's body");
    const std::size_t bodyFrom = at_;
    skipBracedBlock();                        // leaves at_ past the '}'
    const std::size_t bodyTo = at_;

    // [expr.prim.lambda]/4 for the return type, read with the parameters in scope.
    // **`[=]` takes every local the body reads**, found by scanning the body's
    // tokens - over-capturing is harmless where under-capturing is not.
    if (captureAllByValue || captureAllByRef) {
        for (std::size_t i = bodyFrom + 1; i + 1 < bodyTo; i++) {
            if (tokens_[i].kind != TokenKind::Ident) continue;
            const Token &before = tokens_[i - 1];
            if (before.is(".") || before.is("->") || before.is("::")) continue;
            const std::string &n = tokens_[i].text;
            bool had = false;
            for (std::size_t k = 0; k < capNames.size(); k++)
                if (capNames[k] == n) { had = true; break; }
            if (had) continue;
            const Local *have = lookup_.findLocal(n);
            if (have != nullptr && have->sharesSlot)
                src_.fail(tokens_[i].pos, "unnamed variable cannot be implicitly captured in a lambda expression");
            const Type *raw = have != nullptr ? have->type : nullptr;
            if (raw == nullptr) {
                // **A lambda inside a lambda, taking the outer one's capture.**
                if (ExprPtr reach = outerCaptureAccess(n)) raw = reach->type();
                // **A capture-default captures `this` as well**, and both of
                // them do - [expr.prim.lambda]/8.
                if (raw == nullptr) {
                    if (namesOwnMember(n)) defaultTakesThis = true;
                    continue;
                }
            }
            capNames.push_back(n);
            const Type *base = raw->isReference()
                             ? raw->referent()->unqualified()
                             : raw->unqualified();
            capTypes.push_back(captureAllByRef ? referenceCaptureType(raw) : base);
        }
    }

    // Added after the scan rather than during it: one pointer however many
    // members the body reads, and `[this]` written out has taken it already.
    if (defaultTakesThis && capturedThisFrom == nullptr) {
        const Type *from = capturedThisClass();
        capNames.push_back(capturedThis());
        capTypes.push_back(types_.pointerTo(from));
        capturedThisFrom = from;
    }

    // The body is recorded as the lambda's text and no deeper yet: what reading it notes is dropped (M2.10).
    const std::size_t bodySyntax = syntaxTree_.size();
    if (returns == nullptr) {
        returns = deduceLambdaReturn(paramsFrom, paramsTo, bodyFrom, bodyTo,
                                     capNames, capTypes);
        // [expr.prim.lambda]/4 deduces after the lvalue-to-rvalue conversion, which drops a scalar's cv.
        if (!returns->unqualified()->isStructOrUnion()) returns = returns->unqualified();
    }

    // The closure type, named `$_0` upward within the enclosing function, as
    // clang names one. **One met while an enclosing lambda's body is read for
    // its return type is named apart**, that reading's count being thrown away.
    const bool deducing = deducingReturn_ != nullptr;
    const std::string local = deducing
        ? "$deduced_" + std::to_string(deducedClosures_++)
        : "$_" + std::to_string(lambdaCount_++);
    // **The tag has to be unique and the function's *name* is not enough.** In a
    // replay `currentFunctionName_` is `operator()`, so every nested lambda built
    // `operator()::$_0`; the owner decides, with a counter for the display tag.
    std::string tag = currentFunctionName_.empty()
                    ? local : currentFunctionName_ + "::" + local;
    if (!currentFunction_.empty()) {
        for (int n = 2; ; n++) {
            std::map<std::string, std::string>::const_iterator had =
                localClassOwner_.find(tag);
            if (had == localClassOwner_.end() || had->second == currentFunction_)
                break;
            tag = currentFunctionName_ + "$" + std::to_string(n) + "::" + local;
        }
    }
    Type *closure = types_.structType(Kind::Struct, tag);
    closure->setLocalName(local);
    closure->setDeclaredClass(false);
    if (!currentFunction_.empty()) {
        localClassOwner_[tag] = currentFunction_;
        closure->setLocalOwner(currentFunction_);
    }
    if (capturedThisFrom != nullptr) closureOuter_[tag] = capturedThisFrom;
    // A lambda inside a lambda is in the scope the outer one was written in.
    if (const Type *scope = lambdaScope()) closureScope_[tag] = scope;
    else if (currentClass_ != nullptr) closureScope_[tag] = currentClass_->unqualified();
    // The enclosing function's constants, for the body's replay as `operator()` (1.27).
    closureConstants_[tag] = constantLocals();
    lookup_.declareTypeName(tag, closure);
    // Laid out as any class is: each member at the next offset its own
    // alignment allows, and the whole thing aligned to the widest of them.
    std::vector<Member> members;
    int at = 0, widest = 1;
    for (std::size_t i = 0; i < capTypes.size(); i++) {
        // The same slot rule a reference data member follows: what it occupies
        // is a pointer, where `sizeof` the type is the referent's.
        const Type *slot = capTypes[i]->isReference()
                         ? types_.pointerTo(capTypes[i]->referent())
                         : capTypes[i];
        const int a = slot->align(target_);
        if (a > widest) widest = a;
        at = alignTo(at, a);
        capOffsets.push_back(at);
        members.push_back(Member{ capNames[i], capTypes[i], at, 0, 0,
                                  Access::Public });
        at += slot->size(target_);
    }
    const int size = members.empty() ? 1 : alignTo(at, widest);
    closure->complete(members, size, widest);
    // **A closure is a class and gets the members a class gets.**
    classes_.declareImplicitSpecials(tag, closure, pos);
    // And is passed as a class is, settled as a class settles it: by address where copying it is a call (M1 -
    // `std::function` moved out of its own byte copy of the closure while the caller still owned it).
    if (classes_.copyConstructorOf(closure) != nullptr || classes_.moveConstructorOf(closure) != nullptr)
        closure->setNonTrivialCopy(true);
    if (destructorOf(closure) != nullptr) closure->setHasDestructor(true);

    // `operator()`, declared as a const member of it.
    Declared d;
    d.name = "operator()";
    d.type = types_.functionType(returns, params, false);
    d.pos = pos;
    declareMember(tag, d, !isMutable, RefQual::None, Access::Public, false, false);

    // The tokens the replay will read. Appended rather than spliced: an index
    // into tokens_ is what PendingBody keeps, and an index survives the vector
    // growing where a pointer would not.
    const std::string retName = "$lret" + std::to_string(lambdaRetSeq_++);
    lookup_.declareTypeName(retName, returns);
    const std::size_t start = tokens_.size();
    Token t;
    t.pos = pos;
    t.kind = TokenKind::Ident;  t.text = retName;      tokens_.push_back(t);
    t.kind = TokenKind::Keyword; t.text = "operator";  tokens_.push_back(t);
    t.kind = TokenKind::Punct;  t.text = "(";          tokens_.push_back(t);
    t.kind = TokenKind::Punct;  t.text = ")";          tokens_.push_back(t);
    t.kind = TokenKind::Punct;  t.text = "(";          tokens_.push_back(t);
    for (std::size_t i = paramsFrom; i < paramsTo; i++) tokens_.push_back(tokens_[i]);
    t.kind = TokenKind::Punct;  t.text = ")";          tokens_.push_back(t);
    if (!isMutable) {
        t.kind = TokenKind::Keyword; t.text = "const"; tokens_.push_back(t);
    }
    for (std::size_t i = bodyFrom; i < bodyTo; i++) tokens_.push_back(tokens_[i]);
    t.kind = TokenKind::End;    t.text = "";           tokens_.push_back(t);
    closureTokens_[tag] = std::make_pair(start, tokens_.size());

    // Replayed on the deducing reading too: that closure is the one the real
    // reading hands back (A21), so it needs its call operator like any other.
    std::vector<PendingBody> mine;
    mine.push_back(PendingBody{ tag, start, local, tag + "::operator()",
                                PendingBody::npos() });
    if (capNames.empty()) captureFreeConversion(tag, local, returns, params, pos, mine);
    // The in-place readings of the body are not the tree's; the replayed call operator, recorded next, is (M2.15).
    syntaxTree_.kill(bodySyntax);
    reviveDefinedBodies(bodySyntax);
    const std::size_t bodiesFrom = syntaxTree_.size();
    replayInlineBodies(std::move(mine));

    // The object itself: a slot in this frame, and the expression is its name.
    MadeLambda record;
    for (std::uint32_t i = static_cast<std::uint32_t>(bodiesFrom) + 1; i <= syntaxTree_.size(); i++)
        if (syntaxTree_.node(syntax::NodeId(i)).kind == syntax::Kind::Function && !syntaxTree_.node(syntax::NodeId(i)).hasParent)
            record.bodies.push_back(syntax::NodeId(i));
    lambdaBodies_ = record.bodies;
    record.type = closure;
    record.end = at_;
    record.span = at_ - lamAt;
    record.names = capNames;
    record.types = capTypes;
    record.offsets = capOffsets;
    lambdaAt_[std::make_pair(currentFunction_, lamAt)] = record;
    if (deducing) deducedAt_[std::make_pair(currentFunction_, pos)] = record;
    return buildClosure(record, pos);
}

// A body defined inside a reading that was then killed - a lambda's call operator replayed during the deducing read
// of the lambda around it, a member template instantiated inside a trial - is emitted all the same, so its nodes are
// the tree's whatever reading made them (M2.15). Those past `from` are revived; the vector is in creation order.
void Parser::reviveDefinedBodies(std::size_t from) {
    for (std::size_t i = definedBodies_.size(); i > 0 && definedBodies_[i - 1].v > from; i--)
        syntaxTree_.revive(definedBodies_[i - 1]);
}

// A slot for the closure, and each capture copied into it - on every reading of the lambda, since 7.1 reads an
// `auto` initialiser twice and the second takes the cached class, so the kept object would hold the stack.
// [expr.prim.lambda]/2: a lambda-expression is a prvalue - its closure object a temporary, though it lives in a slot.
ExprPtr Parser::buildClosure(const MadeLambda &made, std::size_t pos) {
    ExprPtr e = buildClosureObject(made, pos);
    e->setXvalue();      // the two marks a class temporary carries, for the same reason
    e->setDeclCategory(Expr::Category::Prvalue);
    return e;
}

// What the adapter needs to build the closure again (M2.12n): kept only where every capture is a scalar local
// of this function, by value or by reference, or `this`, and the closure has nothing to destroy.
void Parser::noteClosure(const MadeLambda &made, const std::string &name, int off) {
    pendingLambda_ = LambdaFacts();
    pendingLambda_.simple = destructorOf(made.type) == nullptr;
    pendingLambda_.slotName = name;
    pendingLambda_.slot = off;
    pendingLambda_.type = made.type;
    for (std::size_t i = 0; i < made.names.size() && pendingLambda_.simple; i++) {
        if (made.names[i] == capturedThis()) {      // the pointer the walker copies in, whose member is noted apart
            pendingLambda_.thisOffset = made.offsets[i];
            pendingLambda_.thisType = made.types[i];
            continue;
        }
        const Local *l = lookup_.findLocal(made.names[i]);
        const Type *held = made.types[i]->isReference() ? made.types[i]->referent() : made.types[i];
        const Type *source = l != nullptr && l->type->isReference() ? l->type->referent() : l != nullptr ? l->type : nullptr;
        // A class by reference, or by value where the copy above is an assignment of its bytes (M3b.2).
        const bool classHeld = held->isStructOrUnion() && held->isComplete() &&
                               (made.types[i]->isReference() || classes_.copyConstructorOf(held->unqualified()) == nullptr);
        if (l == nullptr || !l->staticName.empty() || l->isThreadLocal || l->constantOnly || l->isRegister ||
            !(held->isScalar() || classHeld) || source->unqualified() != held->unqualified()) {
            pendingLambda_.simple = false;
            break;
        }
        LambdaCapture c;
        c.member = made.names[i];
        c.offset = made.offsets[i];
        c.type = made.types[i];
        c.src = l->name;
        c.srcOffset = l->offset;
        c.srcType = l->type;
        c.srcConst = l->isConst;
        pendingLambda_.captures.push_back(c);
    }
}

ExprPtr Parser::buildClosureObject(const MadeLambda &made, std::size_t pos) {
    const std::string name = ".lam" + std::to_string(refTemps_++);
    const int off = declare(name, made.type, pos);
    noteClosure(made, name, off);
    if (made.names.empty()) {
        ExprPtr obj(Var::local(name, off));
        obj->setType(made.type);
        return obj;
    }

    // `(c.x = x, c.y = y, &c)` and then a dereference of it - the same shape
    // classTemporary uses, and for the same reason: the address of a comma is
    // not something the backends take, and the address of `*p` is `p`.
    ExprPtr chain;
    for (std::size_t i = 0; i < made.names.size(); i++) {
        ExprPtr self(Var::local(name, off));
        self->setType(made.type);
        ExprPtr dst(new MemberAccess(std::move(self), made.names[i],
                                     made.offsets[i], 0, 0));
        ExprPtr src;
        if (made.names[i] == capturedThis()) {
            // The enclosing function's own `this`, copied in as a pointer -
            // or, inside a closure that captured it, that closure's copy.
            src = capturedThisPointer();
            if (src == nullptr) {
                const Local *self = lookup_.findLocal("this");
                src.reset(Var::local("this", self != nullptr ? self->offset
                                                             : thisOffset_));
                src->setType(self != nullptr ? self->type : made.types[i]);
            }
        } else {
            src = objectRef(made.names[i]);
            if (src == nullptr) src = outerCaptureAccess(made.names[i]);
        }
        if (src == nullptr)
            src_.fail(pos, "'" + made.names[i] + "' went missing between the "
                           "capture list and the lambda");
        // Bound, not assigned, when the capture is by reference: the slot
        // holds an address, which is what `bindReference` supplies.
        if (made.types[i]->isReference()) {
            const Type *held = types_.pointerTo(made.types[i]->referent());
            dst->setType(held);
            ExprPtr addr = bindReference(made.types[i], std::move(src), pos,
                                         "'" + made.names[i] + "'");
            ExprPtr bind(new Assign(std::move(dst), std::move(addr)));
            bind->setType(held);
            if (chain == nullptr) { chain = std::move(bind); continue; }
            ExprPtr joined(new Comma(std::move(chain), std::move(bind)));
            joined->setType(held);
            chain = std::move(joined);
            continue;
        }
        // **An array captured by value is copied element by element.**
        if (made.types[i]->isArray()) {
            // Flattened to the innermost element, so `int g[2][3]` is six copies rather than two of
            // a row: the decayed pointer to a multi-dimensional array steps by rows, and casting it
            // to the element type is what makes the arithmetic step by elements.
            const Type *elem = made.types[i];
            long long count = 1;
            while (elem->isArray()) {
                if (elem->length() < 0)
                    src_.fail(pos, "'" + made.names[i] + "' has no length, so "
                                   "capturing it by value cannot say how many "
                                   "elements to copy - capture it by reference");
                count *= elem->length();
                elem = elem->pointee();
            }
            const Type *elemPtr = types_.pointerTo(elem);
            for (long long k = 0; k < count; k++) {
                ExprPtr owner(Var::local(name, off));
                owner->setType(made.type);
                ExprPtr d(new MemberAccess(std::move(owner), made.names[i],
                                           made.offsets[i], 0, 0));
                d->setType(made.types[i]);
                ExprPtr dp(new Cast(elemPtr, decay(std::move(d))));
                dp->setType(elemPtr);
                ExprPtr kn(new Num(k));
                kn->setType(types_.intType());
                // Through `arithmetic`, never a bare Binary: pointer scaling
                // lives there, and a hand-built `+ k` would step by bytes.
                ExprPtr de(new Unary('*', arithmetic(BinOp::Add, std::move(dp),
                                                     std::move(kn), pos)));
                de->setType(elem);

                ExprPtr from = objectRef(made.names[i]);
                if (from == nullptr) from = outerCaptureAccess(made.names[i]);
                ExprPtr sp(new Cast(elemPtr, decay(std::move(from))));
                sp->setType(elemPtr);
                ExprPtr kn2(new Num(k));
                kn2->setType(types_.intType());
                ExprPtr se(new Unary('*', arithmetic(BinOp::Add, std::move(sp),
                                                     std::move(kn2), pos)));
                se->setType(elem);

                ExprPtr one(new Assign(std::move(de), std::move(se)));
                one->setType(elem);
                if (chain == nullptr) { chain = std::move(one); continue; }
                ExprPtr joined(new Comma(std::move(chain), std::move(one)));
                joined->setType(elem);
                chain = std::move(joined);
            }
            continue;
        }

        // **A class captured by copy is copy-constructed into its member**, [expr.prim.lambda]/21: a byte
        // copy left a vector's buffer in two objects and both destroyed it (M1, stage 2's frame saves).
        const Type *cls = made.types[i]->unqualified();
        if (cls->isStructOrUnion())
            if (const Signature *cc = classes_.copyConstructorOf(cls)) {
                markUsed(cc);
                const Type *ptr = types_.pointerTo(cls);
                dst->setType(cls);
                ExprPtr at(make_owned<Unary>('&', std::move(dst)));
                at->setType(ptr);
                std::vector<ExprPtr> args;
                args.push_back(std::move(at));
                args.push_back(std::move(src));
                std::vector<const Type *> params;
                params.push_back(ptr);
                params.push_back(cc->params[0]);
                ExprPtr built = completeCall(cls->tag(), cc->symbol, nullptr, types_.get(Kind::Void), params,
                                             false, pos, std::move(args));
                if (chain == nullptr) { chain = std::move(built); continue; }
                ExprPtr both(make_owned<Comma>(std::move(chain), std::move(built)));
                both->setType(types_.get(Kind::Void));
                chain = std::move(both);
                continue;
            }
        dst->setType(made.types[i]);
        ExprPtr store(new Assign(std::move(dst), decay(std::move(src))));
        store->setType(made.types[i]);
        if (chain == nullptr) { chain = std::move(store); continue; }
        ExprPtr both(new Comma(std::move(chain), std::move(store)));
        both->setType(made.types[i]);
        chain = std::move(both);
    }
    // **The closure is a temporary, and one holding a class is destroyed** at the end of its full expression
    // ([class.temporary]/3) - unless a declaration takes it whole, which claims it (M1).
    if (destructorOf(made.type) != nullptr) {
        const int guard = guardFlag();
        pendingTemps_.push_back(Temporary{ off, made.type, guard });
        ExprPtr mark(make_owned<Comma>(std::move(chain), setGuard(guard, 1)));
        mark->setType(types_.intType());
        chain = std::move(mark);
    }
    ExprPtr whole(Var::local(name, off));
    whole->setType(made.type);
    ExprPtr at2(new Unary('&', std::move(whole)));
    at2->setType(types_.pointerTo(made.type));
    ExprPtr seq(new Comma(std::move(chain), std::move(at2)));
    seq->setType(types_.pointerTo(made.type));
    ExprPtr obj(new Unary('*', std::move(seq)));
    obj->setType(made.type);
    return obj;
}

int Parser::closureSlotOf(const Expr *e) {
    if (const Var *v = dynamic_cast<const Var *>(e))
        return v->isLocal() && v->name().compare(0, 4, ".lam") == 0 ? v->offset() : -1;
    const Unary *u = dynamic_cast<const Unary *>(e);
    if (u == nullptr || u->op() != '*') return -1;
    const Comma *c = dynamic_cast<const Comma *>(&u->operand());
    if (c == nullptr) return -1;
    const Unary *a = dynamic_cast<const Unary *>(&c->right());
    if (a == nullptr || a->op() != '&') return -1;
    return closureSlotOf(&a->operand());
}

// The enclosing object's pointer, inside a closure that captured it: the
// closure's own `this`, then its `$this` member. Null when this is not such a
// closure, which is every other context.
ExprPtr Parser::capturedThisPointer() {
    if (currentClass_ == nullptr) return nullptr;
    std::map<std::string, const Type *>::const_iterator outer =
        closureOuter_.find(currentClass_->unqualified()->tag());
    if (outer == closureOuter_.end()) return nullptr;
    const Member *held = currentClass_->unqualified()->findMember(capturedThis());
    if (held == nullptr) return nullptr;
    const Local *self = lookup_.findLocal("this");
    if (self == nullptr) return nullptr;

    ExprPtr acc = thisMember(self->offset, self->type->pointee(), *held);
    acc->setType(types_.pointerTo(outer->second));
    return acc;
}

// The class whose scope the body being parsed is in, when that body is a
// closure's call operator; null for every other function. The closure itself
// is `currentClass_` there, and it has none of what the class declares.
const Type *Parser::lambdaScope() const {
    if (currentClass_ == nullptr) return nullptr;
    std::map<std::string, const Type *>::const_iterator scope =
        closureScope_.find(currentClass_->unqualified()->tag());
    return scope == closureScope_.end() ? nullptr : scope->second;
}

// [expr.prim.lambda]/15: a by-reference capture refers to the entity itself, its cv included - so a
// const object, or a const reference, is held as a `const T &` (L7, M1).
const Type *Parser::referenceCaptureType(const Type *captured) {
    return types_.referenceTo(captured->isReference() ? captured->referent() : captured);
}

// A capture of the lambda around this one, read from inside it: by then the outer
// capture is a member of the outer closure, so `findLocal` answers nothing and the
// capture machinery looks here. Null for every other name, which is most of them.
ExprPtr Parser::outerCaptureAccess(const std::string &name) {
    if (currentClass_ == nullptr) return nullptr;
    const Type *closure = currentClass_->unqualified();
    if (closureOuter_.find(closure->tag()) == closureOuter_.end() &&
        localClassOwner_.find(closure->tag()) == localClassOwner_.end())
        return nullptr;
    const Member *m = closure->findMember(name);
    if (m == nullptr) return nullptr;
    const Local *self = lookup_.findLocal("this");
    if (self == nullptr) return nullptr;

    ExprPtr acc = thisMember(self->offset, closure, *m);
    return useReference(std::move(acc));
}

// The body of each is tokens over hidden typedefs, so any type is one identifier; `__invoke`
// calls `operator()` on a null closure, which reads nothing of it - both oracles' shape.
void Parser::captureFreeConversion(const std::string &tag, const std::string &local,
                                   const Type *returns, const std::vector<const Type *> &params,
                                   std::size_t pos, std::vector<PendingBody> &bodies) {
    const Type *closure = lookup_.findTypedef(tag);
    const Type *fn = types_.functionType(returns, params, false);
    const Type *fp = types_.pointerTo(fn);
    Declared inv;
    inv.name = "__invoke";
    inv.type = fn;
    inv.pos = pos;
    declareMember(tag, inv, false, RefQual::None, Access::Public, false, false, true);
    Declared cv;
    cv.name = "operator " + fn->describe() + " *";
    cv.type = types_.functionType(fp, std::vector<const Type *>(), false);
    cv.pos = pos;
    declareMember(tag, cv, true, RefQual::None, Access::Public, false, false);

    const std::string k = std::to_string(lambdaConvSeq_++);
    const std::string cls = "$lcls" + k, ptr = "$lfp" + k, ret = "$lret" + std::to_string(lambdaRetSeq_++);
    lookup_.declareTypeName(cls, closure);
    lookup_.declareTypeName(ptr, fp);
    lookup_.declareTypeName(ret, returns);
    Token t;
    t.pos = pos;
    auto put = [&](TokenKind kind, const std::string &text) { t.kind = kind; t.text = text; tokens_.push_back(t); };
    const std::size_t invAt = tokens_.size();
    put(TokenKind::Ident, ret); put(TokenKind::Ident, "__invoke"); put(TokenKind::Punct, "(");
    for (std::size_t i = 0; i < params.size(); i++) {
        const std::string pt = "$lpt" + k + "_" + std::to_string(i);
        lookup_.declareTypeName(pt, params[i]);
        if (i > 0) put(TokenKind::Punct, ",");
        put(TokenKind::Ident, pt); put(TokenKind::Ident, "$a" + std::to_string(i));
    }
    put(TokenKind::Punct, ")"); put(TokenKind::Punct, "{");
    if (!returns->isVoid()) put(TokenKind::Keyword, "return");
    put(TokenKind::Punct, "("); put(TokenKind::Punct, "("); put(TokenKind::Ident, cls);
    put(TokenKind::Punct, "*"); put(TokenKind::Punct, ")"); put(TokenKind::Num, "0");
    tokens_.back().value = 0;
    put(TokenKind::Punct, ")"); put(TokenKind::Punct, "->"); put(TokenKind::Keyword, "operator");
    put(TokenKind::Punct, "("); put(TokenKind::Punct, ")"); put(TokenKind::Punct, "(");
    for (std::size_t i = 0; i < params.size(); i++) {
        if (i > 0) put(TokenKind::Punct, ",");
        // A by-value class or an rvalue reference is handed on as an xvalue, the others as named.
        const bool move = params[i]->isRValueReference() ||
                          (!params[i]->isReference() && params[i]->unqualified()->isStructOrUnion());
        if (move) {
            put(TokenKind::Keyword, "static_cast"); put(TokenKind::Punct, "<");
            put(TokenKind::Ident, "$lpt" + k + "_" + std::to_string(i));
            if (!params[i]->isReference()) put(TokenKind::Punct, "&&");
            put(TokenKind::Punct, ">"); put(TokenKind::Punct, "(");
        }
        put(TokenKind::Ident, "$a" + std::to_string(i));
        if (move) put(TokenKind::Punct, ")");
    }
    put(TokenKind::Punct, ")"); put(TokenKind::Punct, ";"); put(TokenKind::Punct, "}");
    put(TokenKind::End, "");
    bodies.push_back(PendingBody{ tag, invAt, local, tag + "::__invoke", PendingBody::npos() });

    const std::size_t cvAt = tokens_.size();
    put(TokenKind::Keyword, "operator"); put(TokenKind::Ident, ptr); put(TokenKind::Punct, "(");
    put(TokenKind::Punct, ")"); put(TokenKind::Keyword, "const"); put(TokenKind::Punct, "{");
    put(TokenKind::Keyword, "return"); put(TokenKind::Punct, "&"); put(TokenKind::Ident, cls);
    put(TokenKind::Punct, "::"); put(TokenKind::Ident, "__invoke"); put(TokenKind::Punct, ";");
    put(TokenKind::Punct, "}"); put(TokenKind::End, "");
    bodies.push_back(PendingBody{ tag, cvAt, local, tag + "::" + cv.name, PendingBody::npos() });
}
