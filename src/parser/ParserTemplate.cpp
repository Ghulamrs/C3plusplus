// The parser: templates. Template parameters and the scope they are bound in,
// argument deduction, partial ordering and specialisation, and the token replay
// that turns a pattern into an instantiation. Rung 5.
#include "Parser.h"
#include "ParserInternal.h"
#include "../Mangle.h"
#include "../Operator.h"
#include "../Source.h"

#include <climits>
#include <cstdlib>
#include <cstring>

// ------------------------------------------------------------------ templates
// **Rung 5.1: the table exists and nothing is instantiated.** `f<int>(x)` and
// `a<b>(c)` are the same tokens; only a name in the table opens an argument list.

// A `>` may be the front half of a `>>`. See the note on angleSplit_.
bool Parser::atClosingAngle() const {
    return peek().is(">") || peek().is(">>");
}

void Parser::takeClosingAngle() {
    if (consume(">")) return;
    if (!peek().is(">>"))
        src_.fail(peek().pos, "expected '>' to close this template argument list");
    if (angleSplit_ == at_) {
        angleSplit_ = static_cast<std::size_t>(-1);
        at_++;
        return;
    }
    angleSplit_ = at_;
}

// `template < class T, int N >`. Everything C++11 puts here that this rung
// does not implement is refused by name rather than misread.
void Parser::templateParameters(std::vector<TemplateParam> &params, bool allowUnnamed) {
    expect("<");
    if (atClosingAngle())
        src_.fail(peek().pos, "'template <>' is an explicit specialization, "
                              "and that is not supported yet");
    for (;;) {
        const std::size_t pos = peek().pos;
        TemplateParam p;
        p.pos = pos;
        // **`template <class> class C`** - [temp.param]/1. Its own list may go unnamed; the
        // keyword after it is `class` in C++11, `typename` there being C++17's (P0522's neighbour N4051).
        if (consume("template")) {
            p.isTemplate = true;
            templateParameters(p.inner, true);
            if (peek().is("typename"))
                src_.fail(peek().pos, "'typename' after a template template parameter's list is "
                                      "C++17, and this compiler is C++11 - write 'class' there");
            if (!consume("class"))
                src_.fail(peek().pos, "expected 'class' after a template template parameter's list");
            if (consume("..."))
                src_.fail(pos, "a pack of template template parameters is not supported yet");
            p.namePos = peek().pos;
            if (peek().kind == TokenKind::Ident) { p.name = peek().text; at_++; }
        } else if (consume("class") || consume("typename")) {
            if (consume("...")) p.isPack = true;
            p.namePos = peek().pos;
            if (peek().kind == TokenKind::Ident) { p.name = peek().text; at_++; }
        } else {
            // A non-type parameter is written exactly like a function's, so it is read like one.
            // **`T v` after `class T`**, [temp.param]/4: the earlier type parameters stand for
            // themselves while this one's type is read, as a function template's signature does.
            std::vector<Shadow> undo;
            std::vector<TemplateParam> earlier;
            std::vector<const Type *> stand;
            for (std::size_t k = 0; k < params.size(); k++)
                if (params[k].type == nullptr && !params[k].isPack) {
                    earlier.push_back(params[k]);
                    stand.push_back(types_.templateParam(static_cast<int>(k)));
                }
            tmpl_.bindTemplateParameters(earlier, stand, std::vector<long long>(earlier.size(), 0),
                                   Packs(earlier.size()), &undo);
            struct Unbind {
                Parser *p; std::vector<Shadow> *u;
                ~Unbind() { p->tmpl_.unbindTemplateParameters(*u); }
            } unbind{ this, &undo };
            StorageClass sc;
            Qualifiers quals;
            const Type *base = specifiers(&sc, &quals);
            if (sc != StorageNone)
                src_.fail(pos, "a template parameter has no storage class");
            // **`int... Ns`** - [temp.param]/15: a pack of values. The `...` follows the type's
            // own specifiers, so a pointer or a reference in front of it is a shape not built.
            if (consume("...")) {
                p.isPack = true;
            } else {
                for (std::size_t k = 0; peekAt(k).kind != TokenKind::End; k++) {
                    if (peekAt(k).is(",") || peekAt(k).is(">") || peekAt(k).is(">>")) break;
                    if (peekAt(k).is("..."))
                        src_.fail(peekAt(k).pos, "a non-type parameter pack of a pointer, reference or "
                                                 "array type is not supported yet - a pack of an "
                                                 "integral or enumeration type, 'int... Ns', is");
                }
            }
            Declared d = declarator(base, true);
            if (p.isPack && !base->isInteger() && base->kind() != Kind::TemplateParam)
                src_.fail(d.pos, "a non-type parameter pack of type '" + base->describe() + "' is not "
                                 "supported yet - it must be an integral or enumeration type");
            p.name = d.name;
            p.type = d.type;
            p.namePos = d.pos;
            if (d.type->kind() == Kind::TemplateParam)
                p.typeOf = static_cast<int>(d.type->length());
            else if ([&] {
                         for (const Type *t = d.type; t != nullptr; ) {
                             if (t->kind() == Kind::TemplateParam) return true;
                             if (t->unqualified() != t) t = t->unqualified();
                             else if (t->isPointer() || t->isReference()) t = t->pointee();
                             else return false;
                         }
                         return false;
                     }())
                src_.fail(pos, "a non-type template parameter whose type is built from an earlier "
                               "parameter, '" + d.type->describe() + "', is not supported yet - "
                               "the earlier parameter itself, 'T v', is");
        }
        // [temp.param]/1 lets a parameter go unnamed: it is bound by its place under a name no source can spell.
        if (p.name.empty()) p.name = "$tp" + std::to_string(params.size());
        // **A default argument, kept as tokens.** [temp.param]/9 lets a
        // parameter carry one, and a use that omits it replays these tokens
        // with the earlier parameters bound - which is how `N = M` sees `M`.
        if (consume("=")) {
            p.defBegin = at_;
            int depth = 0;
            for (;;) {
                const Token &t = peek();
                if (t.kind == TokenKind::End) break;
                if (depth == 0 && (t.is(",") || t.is(">") || t.is(">>"))) break;
                if (t.is("<") || t.is("(") || t.is("[")) depth++;
                else if (depth > 0 && (t.is(">") || t.is(")") || t.is("]"))) depth--;
                at_++;
            }
            p.defEnd = at_;
            if (p.defEnd == p.defBegin)
                src_.fail(peek().pos, "a default template argument has no value");
        }
        for (std::size_t i = 0; i < params.size(); i++)
            if (!p.name.empty() && params[i].name == p.name)
                src_.fail(p.pos, "'" + p.name + "' is declared twice in this "
                                 "template parameter list");
        params.push_back(p);
        if (consume(",")) continue;
        break;
    }
    takeClosingAngle();
    // A template template parameter's own list is a class template's: [temp.param]/11's first sentence.
    if (allowUnnamed) refuseAfterPack(params);
}

// The first parameter written after a pack, or npos.
std::size_t Parser::afterPack(const std::vector<TemplateParam> &params) {
    for (std::size_t i = 0; i + 1 < params.size(); i++)
        if (params[i].isPack) return i + 1;
    return static_cast<std::size_t>(-1);
}

// **[temp.param]/11: in a class template a pack is the last parameter.** A pack takes every
// argument that is left, so anything written after one could never be given a value.
void Parser::refuseAfterPack(const std::vector<TemplateParam> &params) {
    const std::size_t i = afterPack(params);
    if (i == static_cast<std::size_t>(-1)) return;
    src_.fail(params[i - 1].namePos, "'" + params[i - 1].name + "' is a parameter pack and takes every "
                                     "argument that is left, so '" + params[i].name + "' after it could "
                                     "never be given one");
}

// The name the template is being given, and nothing else about it. A class
// template's is read straight off the keyword; **a function template's sits behind
// a return type that mentions the parameters**, so `T` denotes a stand-in first.
std::string Parser::templatedName(const std::vector<TemplateParam> &params,
                                  bool *isClass, std::string *qualifier,
                                  bool *construction, const Type **pattern) {
    qualifier->clear();
    if (peek().is("struct") || peek().is("class") || peek().is("union")) {
        if (peekAt(1).kind != TokenKind::Ident)
            src_.fail(peek().pos, "this class template has no name");
        *isClass = true;
        return peekAt(1).text;
    }
    *isClass = false;

    // **Read as a pattern, with the parameters standing for themselves.** The
    // stand-in used to be `int`, which was enough to find a name - but it would
    // instantiate `Box<int>` at a declaration that asks for no class at all.
    TemplateDecl scratch;
    scratch.params = params;
    scratch.afterParams = at_;
    std::vector<const Type *> binding(params.size());
    std::vector<long long> values(params.size(), 1);
    // A non-type parameter stands for itself too, or `enable_if<N == 3, int>` folds N to 1 here.
    for (std::size_t i = 0; i < params.size(); i++)
        binding[i] = types_.templateParam(static_cast<int>(i));
    std::string name;
    const Type *fn = readTemplateDeclaration(scratch, binding, values, &name, qualifier, nullptr,
                                             construction);
    if (name.empty())
        src_.fail(peek().pos, "this function template has no name");
    if (pattern != nullptr) *pattern = fn;
    return name;
}

const Type *Parser::templateNameTypeOf(const TemplateDecl &decl) const {
    return types_.templateNameType(decl.name, decl.ns, decl.isAlias, decl.classKey);
}

// `$TT3` names template template parameter 3, and nothing a program writes begins with a `$`.
bool ttRefName(const std::string &name, int *index) {
    if (name.compare(0, 3, "$TT") != 0) return false;
    *index = std::atoi(name.c_str() + 3);
    return true;
}

bool Parser::templateParamRef(const std::string &name, int *index) { return ttRefName(name, index); }

// `Box`, `ns::NBox`, an alias template, or a template template parameter of the template around this one.
const Parser::TemplateDecl &Parser::templateTemplateArgument(const TemplateDecl &decl,
                                                              const TemplateParam &p) {
    const std::size_t pos = peek().pos;
    consume("::");
    if (peek().kind != TokenKind::Ident)
        src_.fail(pos, "'" + p.name + "' is a template template parameter of '" + decl.name +
                       "', so its argument is the name of a class template");
    std::string written = peek().text;
    at_++;
    while (peek().is("::") && peekAt(1).kind == TokenKind::Ident) {
        written += "::" + peekAt(1).text;
        at_ += 2;
    }
    if (peek().is("<"))
        src_.fail(pos, "'" + written + "' is given arguments here, and '" + p.name + "' is a template "
                       "template parameter, which takes a template's name alone");
    std::map<std::string, TemplateDecl>::const_iterator it = findTemplate(written);
    if (it == tmpl_.templates_.end() || !it->second.isClass)
        src_.fail(pos, "'" + written + "' does not name a class template or an alias template, "
                       "which is what '" + p.name + "' of '" + decl.name + "' takes");
    std::string why;
    std::size_t at = pos;
    if (!tmpl_.templateTemplateMatches(p.inner, it->second.params, &why, &at))
        src_.fail(at, "'" + written + "' cannot be the argument for the template template "
                      "parameter '" + p.name + "' of '" + decl.name + "': " + why);
    return it->second;
}

// The declaration read again with the arguments in force. Nothing is
// registered: this answers what the signature *is*, and the caller decides
// what to do with it.
const Type *Parser::readTemplateDeclaration(const TemplateDecl &decl,
                                            const std::vector<const Type *> &binding,
                                            const std::vector<long long> &values,
                                            std::string *name,
                                            std::string *qualifier,
                                            const Packs *packs,
                                            bool *construction) {
    const std::size_t resume = at_;
    // **Put back even if this throws.** Forming a signature is what a trial runs,
    // and a failed one must leave the parameter names unbound for the next
    // candidate - all that stands between a failure and a table still saying int.
    struct Unbind {
        Parser *p;
        std::vector<Shadow> undo;
        ~Unbind() { p->tmpl_.unbindTemplateParameters(undo); }
    } guard{ this, std::vector<Shadow>() };
    // **The caller's locals are not in the template's scope**: `Mx<4,4> M; M * v` read the operator's
    // `CVector<M>` as that object, and the operator was never a candidate.
    const LocalsHidden hidden(this);
    tmpl_.bindTemplateParameters(decl.params, binding, values,
                           packs != nullptr
                               ? *packs
                               : Packs(),
                           &guard.undo);
    const bool wasPattern = patternOnly_;
    for (std::size_t i = 0; i < binding.size(); i++)
        if (binding[i] != nullptr && binding[i]->kind() == Kind::TemplateParam)
            patternOnly_ = true;

    at_ = decl.afterParams;
    // **A constructor template has no type in front of its name**, so it is read the way a
    // held `X(` is: under its owner, which the declarator takes one-shot and clears.
    struct Owner {
        Parser *p; std::string tag, name;
        ~Owner() { p->inlineOwner_ = tag; p->inlineOwnerName_ = name; }
    } owner{ this, inlineOwner_, inlineOwnerName_ };
    if (decl.isConstructor) { inlineOwner_ = decl.ownerTag; inlineOwnerName_ = decl.ownerLocal; }
    // Any other member template's signature names its class's members, its own name included: `Box &set(U)`.
    struct Scope {
        Parser *p; const Type *was;
        ~Scope() { p->signatureClass_ = was; }
    } scope{ this, signatureClass_ };
    if (decl.isMember && !decl.isConstructor) {
        auto own = lookup_.typedefIndex_.find(decl.ownerTag);
        if (own != lookup_.typedefIndex_.end()) signatureClass_ = lookup_.typedefs_[own->second].type;
    }
    // **The signature names things in the namespace the template was declared in**, as its body
    // does when replayed: `template <class T> int adl(T, Tag)` inside NS reads NS's Tag from a call
    // outside NS (1.27, 2026-10-21, review of 1.25, item 3 - "expected a type" at the instantiation).
    struct Where {
        Parser *p; std::vector<std::string> was;
        ~Where() { p->namespaceStack_ = was; }
    } where{ this, namespaceStack_ };
    if (!decl.ns.empty()) namespaceStack_ = namespacePathStack(decl.ns);
    StorageClass sc;
    Qualifiers quals;
    const Type *base = specifiers(&sc, &quals);
    Declared d = declarator(base);

    // **The declarator records where the parameter list is, and does not read it.**
    bool constructs = false;
    // `S K<T>::m(4);` - a static member defined out of line by a construction,
    // told from a parameter list as file scope tells them: a list is empty or
    // begins with a type.
    if (qualifier != nullptr && !d.qualifier.empty() && d.paramsAt == 0 &&
        peek().is("(") && d.type->isStructOrUnion() &&
        !d.type->tag().empty() &&
        hasConstructors(d.type->tag())) {
        const std::size_t save = at_;
        at_++;
        constructs = !(peek().is(")") || atDeclarationStart());
        at_ = save;
    }
    if (construction != nullptr) *construction = constructs;
    if (!constructs && (d.paramsAt != 0 || peek().is("("))) {
        if (d.paramsAt != 0) at_ = d.paramsAt;
        std::vector<const Type *> params;
        bool variadic = false;
        const std::size_t paramsOpen = at_;
        const Type *written = d.type;
        parameterTypes(params, variadic);
        d.type = types_.functionType(d.type, std::move(params), variadic);
        // **A bare `noexcept`/`throw()` is the specialization's too** (std::declval's), read with the pattern's
        // signature so the replay finds the same promise (1.27, review of 1.25, item 3). A `noexcept(e)`
        // with a dependent e is left to the body's reading.
        patternNoexcept_ = false;
        {
            std::size_t q = at_;
            while (tokens_[q].is("const") || tokens_[q].is("volatile") || tokens_[q].is("&") ||
                   tokens_[q].is("&&"))
                q++;
            if (tokens_[q].is("noexcept") && !tokens_[q + 1].is("(")) patternNoexcept_ = true;
            if (tokens_[q].is("throw") && tokens_[q + 1].is("(") && tokens_[q + 2].is(")"))
                patternNoexcept_ = true;
        }
        // `template <class T> auto f(T) -> T`: the pattern's return type is the trailing one.
        if (tmpl_.mentionsDeduced(written) && tokens_[skipFunctionQualifiers(at_)].is("->")) {
            at_ = skipFunctionQualifiers(at_);
            d.type = types_.functionType(readTrailingReturn(paramsOpen), d.type->params(), variadic);
        }
    } else if (qualifier == nullptr) {
        src_.fail(d.pos, "'" + d.name + "' is a template and not a function, "
                         "and only function templates are supported yet");
    }

    patternOnly_ = wasPattern;
    at_ = resume;
    *name = d.name;
    if (qualifier != nullptr) *qualifier = d.qualifier;
    return d.type;
}

// From here to the `;` that ends the declaration, or to the `}` that closes
// the body. Nothing inside is looked at - that is what "no instantiation"
// means. Answers whether a body was there.
bool Parser::skipTemplatedDefinition(bool *sawInit, bool *sawParen) {
    bool body = false, initialiser = false;
    int depth = 0, parens = 0;
    if (sawInit != nullptr) *sawInit = false;
    if (sawParen != nullptr) *sawParen = false;
    for (;;) {
        if (peek().kind == TokenKind::End)
            src_.fail(peek().pos, "this template's definition is never closed");
        // **An `=` at depth zero is an initialiser**, so what is being skipped is a definition even
        // though it has no braces - `template <class T> const R C<T>::k = R();` - and a `(` is a
        // parameter list or a construction; neither, and no braces, is `T Tm<T>::st;`, a definition.
        if (depth == 0 && peek().is("(")) parens++;
        if (depth == 0 && peek().is(")")) parens--;
        // ...but not the `=` of `operator=`, a name (M1, std::function's `template <class F> operator=(F)`).
        const bool opName = at_ > 0 && tokens_[at_ - 1].is("operator");
        if (depth == 0 && parens == 0 && peek().is("=") && !opName) initialiser = true;
        if (sawInit != nullptr && depth == 0 && peek().is("=") && !opName) *sawInit = true;
        if (sawParen != nullptr && depth == 0 && peek().is("(")) *sawParen = true;
        // Braces after that `=` are a list, `C<T>::arr[2] = { 1, 2 };`, and no body.
        if (peek().is("{")) { depth++; body = body || !initialiser; at_++; continue; }
        if (peek().is("}")) {
            at_++;
            // A function try block's handlers follow its braces and are the body too.
            if (--depth == 0 && !(body && peek().is("catch"))) { consume(";"); return body; }
            continue;
        }
        if (peek().is(";") && depth == 0) { at_++; return body; }
        at_++;
    }
}

// Defined below, beside the pattern matching it belongs with.

// `template <class T> ...` at file scope. Answers false where the token is
// something else, so topLevel can ask without committing.
bool Parser::templateDeclaration() {
    if (!peek().is("template")) return false;

    TemplateDecl decl;
    decl.start = at_;
    at_++;
    if (!peek().is("<"))
        src_.fail(peek().pos, "explicit instantiation is not supported yet");
    if (peekAt(1).is(">")) return explicitSpecialization();
    templateParameters(decl.params);

    decl.afterParams = at_;
    decl.pos = peek().pos;

    // **An alias template is C++11 and begins with `using`** - [temp.alias].
    // It is asked before the scan below, which would see an '=' with no '('
    // in front of it and call a conforming C++11 program C++14.
    if (peek().is("using")) {
        refuseAfterPack(decl.params);
        at_++;
        decl.name = expectIdent("the alias template's name");
        expect("=");
        decl.aliasAt = at_;
        decl.isAlias = decl.isClass = decl.defined = true;
        decl.ns = namespacePrefix();
        if (tmpl_.templates_.count(decl.name) != 0)
            src_.fail(decl.pos, "'" + decl.name + "' is already a template");
        int depth = 0;
        while (peek().kind != TokenKind::End && !(depth == 0 && peek().is(";"))) {
            if (peek().is("(") || peek().is("{")) depth++;
            else if (peek().is(")") || peek().is("}")) depth--;
            at_++;
        }
        expect(";");
        tmpl_.templates_[decl.name] = decl;
        return true;
    }

    // **A variable template is C++14, and it is told from the two C++11
    // declarations by a token scan**: a class or function reaches '(' or a class
    // key before any '=', and an out-of-line member writes '::' before its own.
    if (!peek().is("struct") && !peek().is("class") && !peek().is("union")) {
        int depth = 0;
        for (std::size_t i = at_; i < tokens_.size(); i++) {
            const Token &t = tokens_[i];
            if (depth == 0) {
                // `operator<` names a function, and its `<` opens nothing.
                if (t.is("(") || t.is("::") || t.is("{") || t.is(";") || t.is("operator")) break;
                if (t.is("="))
                    src_.fail(t.pos, "a variable template is C++14, and this "
                                     "compiler is C++11 - a class template "
                                     "with a static member says the same "
                                     "thing here");
            }
            if (t.is("[") || t.is("<")) depth++;
            else if (t.is("]") || t.is(">")) depth--;
            if (depth < 0) break;
        }
    }

    // **`template <class T> Box<T>::Box(...)`**: from the `Box` after `::` it is shaped as a constructor
    // held inside the class, so it is kept on the class and replayed by that path.
    std::string special, nestedPath;
    if (atOutOfLineSpecial(&special, &nestedPath)) {
        TemplateDecl &owner = tmpl_.templates_[peek().text];
        at_++;
        skipTemplateArguments();
        expect("::");
        std::string local = owner.name;
        while (peek().kind == TokenKind::Ident && peekAt(1).is("::")) {
            local = peek().text;
            at_ += 2;
        }
        TemplateDecl::OutOfLine ool;
        ool.start = at_;
        ool.constructor = special == "constructor";
        ool.destructor = !ool.constructor;
        ool.member = local;
        ool.nested = nestedPath;
        at_ = decl.afterParams;
        skipTemplatedDefinition();
        owner.outOfLine.push_back(ool);
        return true;
    }

    // **`template <class T> struct Box<T *>` - a partial specialization.** It is
    // told from the primary by the `<` after the name: a class template being
    // *declared* has nothing there, and one already declared is being specialized.
    const std::size_t q = peek().is("struct") || peek().is("class") || peek().is("union")
                        ? namespaceQualifierTokens(1) : 0;
    if ((peek().is("struct") || peek().is("class") || peek().is("union")) &&
        peekAt(1 + q).kind == TokenKind::Ident && peekAt(2 + q).is("<")) {
        auto primary = tmpl_.templates_.find(peekAt(1 + q).text);
        if (primary == tmpl_.templates_.end() || !primary->second.isClass)
            src_.fail(peekAt(1 + q).pos, "'" + peekAt(1 + q).text + "' is not a class "
                                         "template, so there is nothing here to "
                                         "specialize");
        refuseAfterPack(decl.params);
        TemplateDecl::Partial ps;
        ps.params = decl.params;
        ps.pos = decl.pos;
        at_ += 2 + q;
        tmpl_.partialArguments(&ps, primary->second.params);
        // **`:` as well as `{`.** A specialization may have a base clause and a
        // recursive variadic one always does - `struct A<T, R...> : A<R...>`. The
        // replay goes through structOrUnionSpecifier, which reads both in order.
        if (!peek().is("{") && !peek().is(":"))
            src_.fail(peek().pos, "a partial specialization is a definition, "
                                  "and this one has no body");
        ps.bodyAt = at_;
        for (std::size_t i = 0; i < ps.params.size(); i++) {
            bool mentioned = false;
            for (std::size_t k = 0; k < ps.args.size(); k++)
                if (ps.args[k].isPackExpansion) {
                    // `R...` names its parameter and holds no pattern to
                    // walk, so it is asked about directly rather than through
                    // mentionsParam - whose argument would be null.
                    if (ps.args[k].param == i) mentioned = true;
                } else if (!ps.args[k].isType) {
                    if (ps.args[k].isParam && ps.args[k].param == i) mentioned = true;
                } else if (mentionsParam(ps.args[k].type, i)) {
                    mentioned = true;
                }
            if (!mentioned)
                src_.fail(ps.params[i].pos, "'" + ps.params[i].name + "' is "
                          "never used in this specialization's arguments, so "
                          "nothing could ever work it out");
        }
        // [temp.class.spec]/8: a value given for `T v` may not specialize it while T is still a parameter.
        const std::vector<TemplateParam> &primaryParams = primary->second.params;
        for (std::size_t k = 0; k < ps.args.size() && k < primaryParams.size(); k++) {
            const int of = primaryParams[k].typeOf;
            if (ps.args[k].isType || ps.args[k].isPackExpansion || ps.args[k].isParam || of < 0 ||
                static_cast<std::size_t>(of) >= ps.args.size() || !ps.args[of].isType)
                continue;
            for (std::size_t i = 0; i < ps.params.size(); i++)
                if (mentionsParam(ps.args[of].type, i))
                    src_.fail(ps.pos, "this partial specialization gives '" + primaryParams[k].name +
                                      "' a value while its type '" + primaryParams[of].name + "' is still "
                                      "a parameter - a non-type argument may not specialize a parameter "
                                      "of dependent type");
        }
        at_ = decl.afterParams;
        skipTemplatedDefinition();
        primary->second.partials.push_back(ps);
        return true;
    }

    std::string qualifier;
    decl.classKey = peek().is("class");
    bool constructs = false;
    const Type *fnPattern = nullptr;
    decl.name = templatedName(decl.params, &decl.isClass, &qualifier, &constructs, &fnPattern);
    if (decl.isClass) refuseAfterPack(decl.params);
    else              tmpl_.requireDeducibleAfterPack(decl, fnPattern);
    if (!decl.isClass && !literalSuffix(decl.name).empty())
        checkLiteralOperatorTemplate(decl, fnPattern);
    // Where it was written, for the manglers - the table's key stays bare.
    decl.ns = namespacePrefix();
    at_ = decl.afterParams;
    bool sawInit = false, sawParen = false;
    const bool defined = skipTemplatedDefinition(&sawInit, &sawParen);

    // **A member of a class template defined outside it belongs to the class**, not
    // to a template of its own. The declarator already reads a qualified name; what
    // is new is that the qualifier is a template-id, naming the pattern.
    if (!qualifier.empty()) {
        const Type *of = lookup_.findTypedef(qualifier);
        // A nested class of the pattern, `Tree<T>::It::Deep`: the member belongs to the template whose
        // specialization encloses it, and its path down is kept for the replay to find it by.
        std::string nestedIn;
        while (of != nullptr && !of->isSpecialization() && of->enclosing() != nullptr) {
            nestedIn = of->localName() + (nestedIn.empty() ? "" : "::" + nestedIn);
            of = of->enclosing();
        }
        // A pattern is read shallow, with no nested classes to find, so the qualifier's text is split:
        // the template-id up to its closing '>', then the path down.
        if (of == nullptr) {
            int depth = 0;
            for (std::size_t c = 0; c + 1 < qualifier.size(); c++) {
                if (qualifier[c] == '<') depth++;
                else if (qualifier[c] == '>') depth--;
                else if (depth == 0 && qualifier[c] == ':' && qualifier[c + 1] == ':') {
                    const Type *head = lookup_.findTypedef(qualifier.substr(0, c));
                    if (head != nullptr && head->isSpecialization()) {
                        of = head;
                        nestedIn = qualifier.substr(c + 2);
                    }
                    break;
                }
            }
        }
        if (of == nullptr || !of->isSpecialization())
            src_.fail(decl.pos, "'" + qualifier + "' is not a class template, "
                                "so this defines a member of nothing");
        auto owner = tmpl_.templates_.find(of->templateName());
        if (owner == tmpl_.templates_.end())
            src_.fail(decl.pos, "'" + of->templateName() + "' is not a class "
                                "template");
        // The template's own name, not the qualifier: that is the pattern's
        // internal tag and holds a `$` no reader ever wrote.
        const bool dataNoInit = !defined && !sawInit && !constructs && !sawParen;
        if (!defined && !sawInit && !constructs && !dataNoInit)
            src_.fail(decl.pos, "'" + of->templateName() + "::" + decl.name +
                                "' is declared here and not defined - a member "
                                "is declared inside its class");
        TemplateDecl::OutOfLine ool;
        ool.start = decl.afterParams;
        ool.member = decl.name;
        ool.destructor = !decl.name.empty() && decl.name[0] == '~';
        ool.isData = !defined && (sawInit || constructs || dataNoInit);
        ool.nested = nestedIn;
        owner->second.outOfLine.push_back(ool);
        return true;
    }

    decl.defined = defined;
    // **Function templates overload; class templates do not.** A function
    // template of a name already seen is another overload, kept beside the
    // first; a call tries each by deduction.
    if (!decl.isClass) {
        if (decl.defined) tmpl_.fnTemplates_[decl.name].push_back(decl);
        auto it = tmpl_.templates_.find(decl.name);
        if (it == tmpl_.templates_.end()) {
            tmpl_.templates_[decl.name] = decl;
        } else if (decl.defined && !it->second.defined && !it->second.isClass) {
            decl.outOfLine = it->second.outOfLine;
            it->second = decl;
        }
        return true;
    }

    // A template may be declared and then defined. The definition is the one
    // worth keeping, since instantiating is replaying its tokens - but any
    // out-of-line members gathered against the declaration come with it.
    auto it = tmpl_.templates_.find(decl.name);
    if (it == tmpl_.templates_.end()) {
        tmpl_.templates_[decl.name] = decl;
    } else if (decl.defined && !it->second.defined) {
        decl.outOfLine = it->second.outOfLine;
        // [temp.param]/10: the defaults are merged - one written on the declaration is the definition's too.
        for (std::size_t i = 0; i < decl.params.size() && i < it->second.params.size(); i++)
            if (decl.params[i].defBegin == 0 && it->second.params[i].defBegin != 0) {
                decl.params[i].defBegin = it->second.params[i].defBegin;
                decl.params[i].defEnd = it->second.params[i].defEnd;
            }
        it->second = decl;
    } else if (decl.defined) {
        src_.fail(decl.pos, "'" + decl.name + "' is already a template, and "
                            "two class templates of one name are not supported "
                            "yet - a specialization is written 'template <> "
                            "struct " + decl.name + "<...>'");
    }
    return true;
}

// **`template <> int twice<int>(int x) { ... }`**, the arguments written or deduced from the
// parameters. It is the specialization the primary would have made - its key and its name, which
// is the primary's pattern plus the arguments - with these tokens as the body the replay reads.
bool Parser::functionExplicitSpecialization(std::size_t pos) {
    const std::size_t declStart = at_;
    std::size_t nameAt = 0, depth = 0;
    for (std::size_t i = declStart; i < tokens_.size() && tokens_[i].kind != TokenKind::End; i++) {
        const Token &t = tokens_[i];
        if (t.is("(") && depth == 0 && nameAt != 0) break;
        if (t.is("(")) depth++;
        else if (t.is(")") && depth > 0) depth--;
        else if (t.kind == TokenKind::Ident && depth == 0 && tmpl_.fnTemplates_.count(t.text) != 0 &&
                 (tokens_[i + 1].is("<") || tokens_[i + 1].is("("))) nameAt = i;
        if (t.is(";") || t.is("{")) break;
    }
    if (nameAt == 0)
        src_.fail(peek().pos, "an explicit specialization of anything but a class template or a "
                              "namespace-scope function template is not supported yet");
    const std::string name = tokens_[nameAt].text;
    std::size_t argsEnd = nameAt + 1;
    if (tokens_[nameAt + 1].is("<")) {
        at_ = nameAt + 1;
        skipTemplateArguments();
        argsEnd = at_;
    }
    at_ = declStart;
    if (!skipTemplatedDefinition())
        src_.fail(pos, "an explicit specialization of '" + name + "' declared without its "
                       "definition is not supported yet - give it its body here");
    const std::size_t defEnd = at_;

    // The declaration again without its `<...>`, appended to the stream, as a lambda's body is.
    const std::size_t copy = tokens_.size();
    for (std::size_t i = declStart; i < defEnd; i++)
        if (i <= nameAt || i >= argsEnd) tokens_.push_back(tokens_[i]);
    Token stop;
    stop.kind = TokenKind::End;
    tokens_.push_back(stop);
    TemplateDecl plain;
    plain.afterParams = copy;
    std::string ignored;
    const Type *wanted = readTemplateDeclaration(plain, std::vector<const Type *>(),
                                                 std::vector<long long>(), &ignored);

    struct Match {
        std::size_t which;
        std::vector<const Type *> binding;
        std::vector<long long> values;
        Packs packs;
        std::vector<TemplateArg> args;
        const Type *pattern;
    };
    std::vector<Match> matches;
    const std::vector<TemplateDecl> overloads = tmpl_.fnTemplates_[name];
    for (std::size_t k = 0; k < overloads.size(); k++) {
        const TemplateDecl &decl = overloads[k];
        Match m;
        m.which = k;
        std::string why;
        if (argsEnd != nameAt + 1) {
            at_ = nameAt + 1;
            try {
                Trial trial(this);
                templateArguments(decl, &m.binding, &m.values, &m.args, &m.packs);
            } catch (const SubstitutionFailure &f) {
                if (f.unsupported) src_.fail(f.pos, f.why);
                continue;
            }
        } else {
            if (!tmpl_.deduceTemplateArguments(decl, wanted->params(), &m.binding, &m.values, &m.packs, &why))
                continue;
            m.args = tmpl_.argumentsFromBinding(decl.params, m.binding, m.values, m.packs);
        }
        const Type *made = readTemplateDeclaration(decl, m.binding, m.values, &ignored, nullptr, &m.packs);
        if (made->describe() != wanted->describe()) continue;
        std::vector<const Type *> self(decl.params.size());
        for (std::size_t i = 0; i < self.size(); i++) self[i] = types_.templateParam(static_cast<int>(i));
        m.pattern = readTemplateDeclaration(decl, self, m.values, &ignored);
        matches.push_back(m);
    }
    // [temp.expl.spec]/12 with [temp.func.order]: of the primaries it fits, the most specialized.
    struct Order {
        static bool atLeast(const Parser &p, const Type *a, const Type *b) {
            const std::vector<const Type *> &pa = a->params(), &pb = b->params();
            if (pa.size() != pb.size()) return false;
            std::vector<const Type *> binding(pb.size() + pa.size());
            std::string why;
            for (std::size_t i = 0; i < pa.size(); i++)
                if (!p.tmpl_.matchPattern(pb[i], pa[i], &binding, &why)) return false;
            return true;
        }
    };
    for (std::size_t c = 0; c < matches.size(); c++) {
        bool best = true;
        for (std::size_t o = 0; o < matches.size() && best; o++)
            if (o != c && !(Order::atLeast(*this, matches[c].pattern, matches[o].pattern) &&
                            !Order::atLeast(*this, matches[o].pattern, matches[c].pattern)))
                best = false;
        if (!best) continue;
        const Match &m = matches[c];
        const std::size_t before = lookup_.functions_.size();
        instantiate(overloads[m.which], m.binding, m.values, m.args, pos, m.packs);
        if (lookup_.functions_.size() == before)
            src_.fail(pos, "'" + tmpl_.specializationKey(name, m.args) + "' has already been used further up, "
                           "so specializing it here is too late - the specialization goes before the first use");
        tmpl_.specializations_.back().start = copy;
        lookup_.functions_.back().used = true;          // a definition the program wrote, emitted as one
        at_ = defEnd;
        return true;
    }
    src_.fail(pos, matches.empty() ? "this explicit specialization matches no declaration of the function "
                                     "template '" + name + "'"
                                   : "this explicit specialization fits more than one declaration of '" +
                                     name + "' and none is more specialized than the rest");
    return false;
}

// `template <> struct Box<int> { ... };` - rung 5.6. A class written out for one
// argument list: the tag is `Box<int>` as the template would have made it, so every
// lookup and mangling is the same. **The list is read against the primary's.**
bool Parser::explicitSpecialization() {
    const std::size_t pos = peek().pos;
    expect("<");
    takeClosingAngle();

    if (!peek().is("struct") && !peek().is("class") && !peek().is("union"))
        return functionExplicitSpecialization(pos);
    const Kind kind = peek().is("union") ? Kind::Union : Kind::Struct;
    const bool isClass = peek().is("class");
    at_++;

    // `template <> struct lib::Box<int>`: a namespace qualifier, an inline one's left out or not.
    at_ += namespaceQualifierTokens(0);
    if (peek().kind != TokenKind::Ident)
        src_.fail(peek().pos, "this specialization names no class");
    const std::string name = peek().text;
    auto primary = tmpl_.templates_.find(name);
    if (primary == tmpl_.templates_.end() || !primary->second.isClass)
        src_.fail(peek().pos, "'" + name + "' is not a class template, so "
                              "there is nothing here to specialize");
    at_++;
    if (!peek().is("<"))
        src_.fail(peek().pos, "'" + name + "' is a class template and a "
                              "specialization of it needs its arguments");

    std::vector<const Type *> binding;
    std::vector<long long> values;
    std::vector<TemplateArg> args;
    templateArguments(primary->second, &binding, &values, &args);

    const std::string tag = tmpl_.specializationKey(name, args);
    // **Too late is an error, not a redefinition.** [temp.expl.spec]: a
    // specialization has to be declared before the first use that would instantiate
    // the template, or two different classes have been given one name.
    if (lookup_.findTypedef(tag) != nullptr)
        src_.fail(pos, "'" + tag + "' has already been used further up, so "
                       "specializing it here is too late - the specialization "
                       "goes before the first use");
    // **A base-clause is a body too.**
    if (!peek().is("{") && !peek().is(":") && !peek().is("final"))
        src_.fail(peek().pos, "an explicit specialization is a definition, and "
                              "this one has no body");

    classInstantiationTag_ = tag;
    classInstantiationOf_ = name;
    instantiatingArgs_ = args;
    instantiatingNamespace_ = primary->second.ns;
    const Type *made = structOrUnionSpecifier(kind, isClass);
    classInstantiationTag_.clear();
    classInstantiationOf_.clear();
    instantiatingParams_.clear();
    instantiatingBinding_.clear();
    instantiatingValues_.clear();
    instantiatingArgs_.clear();

    lookup_.declareTypeName(tag, made);
    expect(";");
    return true;
}

// The tokens read without being consumed. skipTemplateArguments is what walks
// the argument list, so the `>>` split has to be put back too.
bool Parser::atOutOfLineSpecial(std::string *what, std::string *nested) {
    if (peek().kind != TokenKind::Ident) return false;
    auto t = tmpl_.templates_.find(peek().text);
    if (t == tmpl_.templates_.end() || !t->second.isClass || !peekAt(1).is("<"))
        return false;

    const std::string name = peek().text;
    const std::size_t resume = at_;
    const std::size_t wasSplit = angleSplit_;
    at_++;
    skipTemplateArguments();

    bool yes = false;
    if (peek().is("::")) {
        std::size_t n = 1;
        // `Tree<T>::It::It(`: the class whose constructor this is may be nested, a name per `::`.
        std::string path, cls = name;
        while (peekAt(n).kind == TokenKind::Ident && peekAt(n + 1).is("::") &&
               (peekAt(n + 2).kind == TokenKind::Ident || peekAt(n + 2).is("~"))) {
            cls = peekAt(n).text;
            path += (path.empty() ? "" : "::") + cls;
            n += 2;
        }
        if (peekAt(n).is("~")) { n++; *what = "destructor"; }
        else                   { *what = "constructor"; }
        if (peekAt(n).kind == TokenKind::Ident && peekAt(n).text == cls &&
            peekAt(n + 1).is("(")) yes = true;
        if (nested != nullptr) *nested = path;
    }
    at_ = resume;
    angleSplit_ = wasSplit;
    return yes;
}

// The argument list, read only far enough to step over it - and stepping over it is
// what proves the `>>` split, since `Box<Box<int>>` cannot be got past any other
// way. A nested list is recognised by its name being a template.
void Parser::skipTemplateArguments() {
    expect("<");
    for (;;) {
        if (peek().kind == TokenKind::End)
            src_.fail(peek().pos, "this template argument list is never closed");
        if (atClosingAngle()) { takeClosingAngle(); return; }
        if (peek().kind == TokenKind::Ident && isTemplateName(peek().text) &&
            peekAt(1).is("<")) {
            at_++;
            skipTemplateArguments();
            continue;
        }
        // A parenthesised argument may hold a `>` that closes nothing.
        if (peek().is("(")) {
            int depth = 0;
            do {
                if (peek().kind == TokenKind::End)
                    src_.fail(peek().pos, "this template argument list is "
                                          "never closed");
                if (peek().is("(")) depth++;
                else if (peek().is(")")) depth--;
                at_++;
            } while (depth > 0);
            continue;
        }
        at_++;
    }
}

// `<int, 3>` at a use, read against the parameter list it is for. A type parameter
// takes a type-id and a non-type one a constant expression, so which is which is
// decided by the template and never by the shape of what is written.
void Parser::templateArguments(const TemplateDecl &decl,
                               std::vector<const Type *> *binding,
                               std::vector<long long> *values,
                               std::vector<TemplateArg> *args,
                               Packs *packs) {
    expect("<");
    const bool wasInArgs = inTemplateArgs_;
    inTemplateArgs_ = true;
    if (packs != nullptr) packs->assign(decl.params.size(),
                                        std::vector<const Type *>());
    bool deduceRest = false;
    // **Defaults are replayed with the earlier parameters bound.**
    std::vector<Shadow> undo;
    struct Unbind {
        Parser *p; std::vector<Shadow> *u;
        ~Unbind() { p->tmpl_.unbindTemplateParameters(*u); }
    } unbind{ this, &undo };

    for (std::size_t i = 0; i < decl.params.size(); i++) {
        const TemplateParam &p = decl.params[i];
        TemplateArg a;

        // If the arguments have run out, the rest must carry defaults - or, for a function template, be left to the
        // call, whose caller deduces them ([temp.arg.explicit]/3, M1, `duration_cast<To>(d)`). A comma is due only
        // before a real argument, so it is consumed here and not at the closing angle.
        bool defaulted = !p.isPack && atClosingAngle();
        if (!decl.isClass && atClosingAngle() && (p.isPack ? i > 0 : p.defBegin == 0 && p.type == nullptr)) {
            deduceRest = true;
            break;
        }
        if (!defaulted && i > 0 && !(p.isPack && atClosingAngle()) && !consume(","))
            src_.fail(peek().pos, "'" + decl.name + "' takes " +
                                  std::to_string(decl.params.size()) +
                                  " template arguments and this gives " +
                                  std::to_string(i));
        if (defaulted) {
            if (p.defBegin == 0)
                src_.fail(peek().pos, "'" + decl.name + "' takes " +
                                      std::to_string(decl.params.size()) +
                                      " template arguments and this gives " +
                                      std::to_string(i));
            // The first default binds every parameter before it, so its tokens
            // can name them; each default binds itself for the next one.
            if (undo.empty())
                for (std::size_t k = 0; k < i; k++) {
                    std::vector<TemplateParam> one(1, decl.params[k]);
                    std::vector<const Type *> ob(1, (*binding)[k]);
                    std::vector<long long> ov(1, (*values)[k]);
                    Packs op(1, std::vector<const Type *>());
                    tmpl_.bindTemplateParameters(one, ob, ov, op, &undo);
                }
            const std::size_t saved = at_;
            at_ = p.defBegin;
            if (p.isTemplate) {
                const TemplateDecl &named = templateTemplateArgument(decl, p);
                a.isType = a.isTemplate = true;
                a.type = named.ttParam >= 0 ? types_.templateParam(named.ttParam) : templateNameTypeOf(named);
                binding->push_back(a.type); values->push_back(0);
            } else if (p.type == nullptr) {
                StorageClass sc; Qualifiers quals;
                const Type *base = specifiers(&sc, &quals);
                Declared d = declarator(base, true);
                binding->push_back(d.type); values->push_back(0);
                a.isType = true; a.type = d.type;
            } else {
                const long long v = constantExpression("a default template argument");
                const Type *pt = paramTypeIn(p, *binding);
                binding->push_back(pt); values->push_back(v);
                a.isType = false; a.type = pt; a.value = v;
            }
            at_ = saved;
            args->push_back(a);
            std::vector<TemplateParam> one(1, p);
            std::vector<const Type *> ob(1, binding->back());
            std::vector<long long> ov(1, values->back());
            Packs op(1, std::vector<const Type *>());
            tmpl_.bindTemplateParameters(one, ob, ov, op, &undo);
            continue;
        }
        // **A pack takes everything that is left**, including nothing. It is
        // the last parameter by construction, so there is no ambiguity about
        // where it stops: the closing angle stops it.
        if (p.isPack) {
            a.isPack = true;
            a.isType = true;
            // **A non-type pack takes values of its type** - the written one, or T's binding for `T... Vs`.
            a.packIsValue = p.type != nullptr;
            std::size_t expandedAt = 0;      // where a pattern's `Ns...` was written
            if (a.packIsValue) {
                a.type = paramTypeIn(p, *binding);
                if (!a.type->isInteger() && a.type->kind() != Kind::TemplateParam)
                    src_.fail(p.pos, "a non-type template parameter pack of type '" + a.type->describe() +
                                     "' is not supported yet - it must be an integral or enumeration type");
            }
            // **`first` and not `a.pack.empty()`.** An expansion may contribute
            // nothing - `A<R...>` where R is empty is how a recursion ends - so
            // emptiness cannot say whether a comma is due.
            bool first = true;
            while (!atClosingAngle()) {
                if (!first) expect(",");
                first = false;
                // **`R...` - one pack expanded into another's argument list**,
                // which is what makes a recursive variadic class possible: each
                // step passes on all but the head until the empty case stops it.
                if (peek().kind == TokenKind::Ident && peekAt(1).is("...")) {
                    auto pk = packs_.find(peek().text);
                    if (pk != packs_.end()) {
                        const PackBinding &pb = pk->second;
                        if (pb.isValue != a.packIsValue)
                            src_.fail(peek().pos, "'" + peek().text + "' is a pack of " +
                                                  (pb.isValue ? "values" : "types") + " and '" + p.name +
                                                  "' of '" + decl.name + "' takes " +
                                                  (a.packIsValue ? "values" : "types"));
                        at_ += 2;
                        // **In a pattern the pack stands for itself**, and the argument is its expansion -
                        // `JXspT_EE`, `JDpT_E` - which deduction binds from the members it is given.
                        if (pb.standsForItself()) {
                            expandedAt = tokens_[at_ - 2].pos;
                            a.expandsParam = static_cast<int>(pb.types[0]->length());
                            a.pack.push_back(pb.types[0]);
                            if (a.packIsValue) a.packValues.push_back(0);
                            continue;
                        }
                        for (std::size_t k = 0; k < pb.types.size(); k++) {
                            a.pack.push_back(pb.types[k]);
                            if (a.packIsValue) a.packValues.push_back(pb.values[k]);
                        }
                        continue;
                    }
                }
                if (a.packIsValue) {
                    // `List<M>` in a signature, M a non-type parameter: the member is a reference to it, `XT0_E`.
                    if (peek().kind == TokenKind::Ident && nonTypePatternParams_.count(peek().text) != 0 &&
                        (peekAt(1).is(",") || peekAt(1).is(">") || peekAt(1).is(">>"))) {
                        a.pack.push_back(types_.templateParam(nonTypePatternParams_[peek().text]));
                        a.packValues.push_back(0);
                        at_++;
                        continue;
                    }
                    if (expandPackValues(a)) continue;
                    const std::size_t argPos = peek().pos;
                    const long long v = constantExpression("a template argument");
                    tmpl_.checkNonTypeArgument(a.type, v, argPos);
                    a.pack.push_back(a.type);
                    a.packValues.push_back(v);
                    continue;
                }
                StorageClass sc;
                Qualifiers quals;
                const Type *base = specifiers(&sc, &quals);
                Declared d = declarator(base, true);
                if (!d.name.empty())
                    src_.fail(d.pos, "a template argument is a type here, and "
                                     "this names something");
                a.pack.push_back(d.type);
            }
            // `List<Ns..., 4>` in a signature is `JXspT_ELi4EE`: an expansion beside other arguments, not spelled here.
            if (a.expandsParam >= 0 && a.pack.size() != 1)
                src_.fail(expandedAt, "a parameter pack expanded beside other arguments in a template's "
                                      "signature, '" + decl.name + "<" + p.name + "..., ...>', is not "
                                      "supported yet - the expansion alone is");
            binding->push_back(nullptr);
            values->push_back(0);
            if (packs != nullptr) { (*packs)[i] = a.pack; (*packs)[i].values = a.packValues; }
            args->push_back(a);
            break;
        }
        // **A template's name, for a template template parameter** - a pattern's `Holder<C, T>`
        // records the parameter itself, as a type argument naming T does.
        if (p.isTemplate) {
            const TemplateDecl &named = templateTemplateArgument(decl, p);
            a.isType = a.isTemplate = true;
            a.type = named.ttParam >= 0 ? types_.templateParam(named.ttParam) : templateNameTypeOf(named);
            binding->push_back(a.type);
            values->push_back(0);
            args->push_back(a);
            continue;
        }
        if (p.type == nullptr) {
            StorageClass sc;
            Qualifiers quals;
            const Type *base = specifiers(&sc, &quals);
            Declared d = declarator(base, true);
            if (!d.name.empty())
                src_.fail(d.pos, "a template argument is a type here, and this "
                                 "names something");
            binding->push_back(d.type);
            values->push_back(0);
            a.isType = true;
            a.type = d.type;
        } else {
            const Type *pt = paramTypeIn(p, *binding);
            if (!pt->isInteger() && pt->kind() != Kind::TemplateParam)
                src_.fail(p.pos, "a non-type template parameter of type '" +
                                 pt->describe() + "' is not supported yet - "
                                 "it must be an integer type");
            // **`V<N>` while a signature is read for deduction**: a bare name
            // that is a pattern non-type parameter is a reference to it, not a
            // value to fold. Anything else is an ordinary constant expression.
            if (peek().kind == TokenKind::Ident &&
                nonTypePatternParams_.count(peek().text) != 0 &&
                (peekAt(1).is(",") || peekAt(1).is(">") || peekAt(1).is(">>"))) {
                a.isType = false;
                a.isParam = true;
                a.paramIndex = nonTypePatternParams_[peek().text];
                a.type = pt;
                binding->push_back(pt);
                values->push_back(0);
                at_++;
            } else if (patternOnly_ && dependentAhead(true)) {
                // **An argument that is an expression of the parameters** - `sizeof(T) == 4`,
                // `N + M` - is kept as the tree the Itanium name spells, `X...E`, and folds nothing.
                const std::size_t from = at_;
                DepExpr tree = *dependentExpression(true, false);
                tree.text = depText(from, at_);
                a.isType = false;
                a.type = pt;
                a.dependentValue = true;
                a.expr = types_.intern(tree);
                binding->push_back(pt);
                values->push_back(0);
            } else {
                const std::size_t argPos = peek().pos;
                bool namesParam = false;
                if (patternOnly_)
                    for (std::size_t k = 0, depth = 0; peekAt(k).kind != TokenKind::End; k++) {
                        const Token &t = peekAt(k);
                        if (t.is("(") || t.is("[")) depth++;
                        else if ((t.is(")") || t.is("]")) && depth > 0) depth--;
                        else if (depth == 0 && (t.is(",") || t.is(">") || t.is(">>"))) break;
                        else if (t.kind == TokenKind::Ident &&
                                 (nonTypePatternParams_.count(t.text) != 0 || boundTypeParams_.count(t.text) != 0))
                            namesParam = true;
                    }
                const long long v = constantExpression("a template argument");
                tmpl_.checkNonTypeArgument(pt, v, argPos);
                binding->push_back(pt);
                values->push_back(v);
                a.isType = false;
                a.type = pt;
                a.value = v;
                a.dependentValue = namesParam;
            }
        }
        args->push_back(a);
    }
    if (!atClosingAngle())
        src_.fail(peek().pos, "'" + decl.name + "' takes " +
                              std::to_string(decl.params.size()) +
                              " template arguments and this gives more");
    // A pack took the rest of the list: a pack after it is empty, and anything else is left to deduction.
    while (!deduceRest && binding->size() < decl.params.size() && decl.params[binding->size()].isPack) {
        TemplateArg empty;
        empty.isPack = true;
        empty.packIsValue = decl.params[binding->size()].type != nullptr;
        if (empty.packIsValue) empty.type = paramTypeIn(decl.params[binding->size()], *binding);
        binding->push_back(nullptr);
        values->push_back(0);
        args->push_back(empty);
    }
    if (!deduceRest && binding->size() < decl.params.size())
        src_.fail(peek().pos, "'" + decl.name + "' has a parameter after the pack '" +
                              decl.params[binding->size() - 1].name + "', and deducing it from the "
                              "call once the pack's arguments are written is not supported yet");
    takeClosingAngle();
    inTemplateArgs_ = wasInArgs;
}

// The specialization these arguments ask for, made if it is new. **The two ABIs are
// handed two different things**: Itanium the pattern, since its name spells `T_`,
// and Microsoft the substituted signature. So the declaration is read twice.
const Parser::Signature &
Parser::instantiate(const TemplateDecl &decl,
                    const std::vector<const Type *> &binding,
                    const std::vector<long long> &values,
                    const std::vector<TemplateArg> &args, std::size_t pos,
                    const Packs &packs) {
    if (!decl.defined)
        src_.fail(pos, "'" + decl.name + "' is declared but never defined, so "
                       "there is nothing to instantiate");

    std::string name;
    const Type *fn = readTemplateDeclaration(decl, binding, values, &name,
                                             nullptr, &packs);

    // **Two function-template overloads can share a name and arguments and differ only in
    // signature** - operator*(V<N>, double) and operator*(double, V<N>) both key operator*<3>
    // without the parameter types, so the second was deduped to the first and the wrong one called.
    const std::string display = tmpl_.specializationKey(decl.name, args);
    std::string key = display;
    for (std::size_t i = 0; i < fn->params().size(); i++)
        key += "#" + fn->params()[i]->describe();
    // And which template: two differing only in `decltype(...)` returns were one key, the first.
    key += "@" + std::to_string(decl.afterParams);

    if (const std::vector<std::size_t> *had = lookup_.overloadsOf(key))
        return lookup_.functions_[(*had)[0]];

    // **The pattern the Itanium name is spelled from** keeps every parameter
    // as a reference to itself - a type parameter as Kind::TemplateParam, a
    // non-type one as the param-ref `V<N>` reads to.
    std::vector<const Type *> pattern(decl.params.size());
    for (std::size_t i = 0; i < decl.params.size(); i++)
        pattern[i] = types_.templateParam(static_cast<int>(i));
    std::string patternName;
    const Type *patternFn =
        readTemplateDeclaration(decl, pattern, values, &patternName);

    // A `static` template has internal linkage, which Itanium writes as an L: `_ZL5near1...`.
    bool internal = false;
    for (std::size_t i = decl.afterParams; i < tokens_.size() && !tokens_[i].is("(") &&
                                            tokens_[i].text != decl.name; i++)
        internal = internal || tokens_[i].is("static");
    std::string symbol, why;
    const bool ok = target_.microsoftNames()
        ? microsoftTemplateFunctionName(decl.ns + decl.name, fn, args, &symbol, &why)
        : itaniumTemplateFunctionName(decl.ns + decl.name, patternFn, args, internal,
                                      &symbol, &why);
    if (!ok)
        src_.fail(pos, "'" + key + "' cannot be given a name the linker can "
                       "hold: " + why);

    Specialization sp;
    sp.key = key;
    sp.name = decl.name;
    sp.display = decl.ns + display;
    sp.params = decl.params;
    sp.packs = packs;
    sp.symbol = symbol;
    sp.fn = fn;
    sp.binding = binding;
    sp.values = values;
    sp.start = decl.afterParams;
    sp.pos = pos;
    sp.depth = instantiationDepth_ + 1;
    tmpl_.refuseInstantiationDepth(decl.name, sp.depth, pos);
    tmpl_.specializations_.push_back(sp);

    // **Under two keys, on purpose.** "twice<int>" is what the replayed definition
    // declares and what a repeat of the same arguments finds; "twice" is what
    // overload resolution must see, a specialization competing with the ordinary.
    const std::size_t at = lookup_.functions_.size();
    lookup_.functionIndex_[key].push_back(at);
    lookup_.functionIndex_[decl.name].push_back(at);
    // And under the template's namespace, where that namespace's ordinary functions of the name
    // are: a template and a non-template there compete as one set, and the non-template wins a tie.
    // Where that namespace is inline, under the enclosing one's as well - `lib::twice(1)`.
    if (!decl.ns.empty()) lookup_.functionIndex_[decl.ns + decl.name].push_back(at);
    else lookup_.functionIndex_["::" + decl.name].push_back(at);   // the global namespace's own
    {
        const std::vector<std::string> alts = lookup_.inlineAliasPrefixes(decl.ns);
        for (std::size_t a = 0; a < alts.size(); a++) lookup_.functionIndex_[alts[a] + decl.name].push_back(at);
    }
    lookup_.functions_.push_back(Signature{ display, symbol, fn->returns(), fn->params(),
                                    fn->isVariadicFn(), false, pos, false,
                                    std::string(), false, Access::Public });
    lookup_.functions_.back().fromTemplate = true;
    lookup_.functions_.back().pattern = patternFn;
    lookup_.functions_.back().isNoexcept = patternNoexcept_;   // the pattern's bare `noexcept`, read above (1.27)
    // **The defaults the pattern's parameter list just read.**
    if (!pendingDefaults_.empty()) {
        defaultArgs_[symbol] = pendingDefaults_;
        defaultArgNamespace_[symbol] = namespaceStack_;
    }
    return lookup_.functions_.back();
}

// **The initialiser is read twice: once to learn its type, once to build it.** The
// tokens are put back in between, so the ordinary declaration path sees exactly
// what it would have seen with the type written out.
const Type *Parser::deduceAuto(const Type *declared, const std::string &name,
                               std::size_t pos) {
    // `auto x{...}` has an initialiser and is refused for the reason below,
    // not for having none - the braces are what cannot be deduced from,
    // however they were introduced.
    const bool paren = peek().is("(") && atParenInitialiser();
    if (!peek().is("=") && !peek().is("{") && !paren)
        src_.fail(pos, "'" + name + "' is declared 'auto' and has no "
                       "initialiser, so there is nothing to deduce its type "
                       "from");
    // [dcl.spec.auto]/3: in scope there, it has no type until the initialiser gives one.
    if (initialiserNames(name))
        src_.fail(pos, "'" + name + "' is declared 'auto' and names itself in its "
                       "own initialiser - its type is what that initialiser "
                       "decides, so there is none to use");

    const std::size_t resume = at_;
    if (paren) at_++;
    else if (!consume("=") && peek().is("{"))
        src_.fail(peek().pos, "'auto " + name + "{...}' deduces 'std::initializer_list' by "
                              "C++11's words and the element's type by N3922's, which "
                              "compilers apply to C++11 - not supported yet; write '= {...}' "
                              "for the list, or '= ...' for the one value");
    if (peek().is("{")) {
        const Type *il = deduceBracedAuto(name, pos);
        at_ = resume;
        if (declared->kind() != Kind::Deduced &&
            !(declared->unqualified() != declared && declared->unqualified()->kind() == Kind::Deduced))
            src_.fail(pos, "'" + name + "' is declared '" + declared->describe() + "' from a braced "
                           "list - only 'auto' and 'const auto' deduce one here, a reference or a "
                           "pointer to the list is not supported yet");
        return tmpl_.deduceAutoFrom(declared, il, name, pos);
    }
    // **Read for its type and rewound**, so this reading built nothing: the
    // real one happens below, from the same tokens.
    const Type *from = nullptr;
    {
        Discarded held(this);
        ExprPtr init = assign();
        from = tmpl_.deductionArgType(*init);
    }
    at_ = resume;

    return tmpl_.deduceAutoFrom(declared, from, name, pos);
}

// **What a parameter sees of an argument.** [temp.deduct.call]: an array becomes a
// pointer to its first element, a function a pointer to itself, and the top-level
// qualifier goes - which is also just what passing something does.
const Type *Parser::decayedArgument(const Type *a) const {
    if (a->isReference()) a = a->referent();
    if (a->isArray()) return types_.pointerTo(a->pointee());
    if (a->isFunction()) return types_.pointerTo(a);
    return a->unqualified();
}

// Bit 1 const, bit 2 `&`, bit 4 `&&`; a pack is a known pack name, so nothing else is read as one.
std::size_t Parser::packDeclarator(int *wrap, std::size_t *nameAt) const {
    std::size_t k = 0;
    *wrap = 0;
    if (peekAt(k).is("const")) { *wrap |= 1; k++; }
    if (peekAt(k).kind != TokenKind::Ident || packs_.count(peekAt(k).text) == 0) return 0;
    *nameAt = k++;
    if (peekAt(k).is("&")) { *wrap |= 2; k++; }
    else if (peekAt(k).is("&&")) { *wrap |= 4; k++; }
    if (!peekAt(k).is("...")) return 0;
    return k + 1;
}

// One member as its parameter is written - `A &&` with A bound to `int &` collapses to `int &`.
const Type *Parser::wrapPackMember(const Type *m, int wrap) {
    if ((wrap & 1) && !m->isReference()) m = types_.withConst(m);
    if (wrap & 2) return types_.referenceTo(m->isReference() ? m->referent() : m);
    if (wrap & 4) return m->isReference() ? m : types_.rvalueReferenceTo(m);
    return m;
}

// [temp.variadic]/5: a pattern followed by `...` is one argument per member. Where the pattern ends -
// past its `...`, which the next `,`, `)`, `]`, `}` or (with `angle`) `>` at depth 0 must follow - and
// how many members the packs it names have, which must agree; 0 where the tokens are not that.
std::size_t Parser::packPatternEnd(bool angle, std::size_t *members) const {
    std::size_t end = at_;
    int depth = 0;
    for (; end < tokens_.size() && tokens_[end].kind != TokenKind::End; end++) {
        const Token &t = tokens_[end];
        if (t.is("(") || t.is("[") || t.is("{")) depth++;
        else if (t.is(")") || t.is("]") || t.is("}")) { if (depth == 0) break; depth--; }
        else if (t.is("<") && end > at_ &&
                 (isTemplateName(tokens_[end - 1].text) || tokens_[end - 1].text.find("_cast") != std::string::npos))
            depth++;
        else if ((t.is(">") || t.is(">>")) && depth > 0) depth--;
        else if ((t.is(">") || t.is(">>")) && angle) break;
        else if (t.is(",") && depth == 0) break;
    }
    if (end < at_ + 2 || !tokens_[end - 1].is("...")) return 0;
    *members = 0;
    bool any = false;
    for (std::size_t i = at_; i + 1 < end; i++) {
        auto pk = tokens_[i].kind == TokenKind::Ident ? packs_.find(tokens_[i].text) : packs_.end();
        if (pk == packs_.end()) continue;
        if (any && pk->second.types.size() != *members)
            src_.fail(tokens_[end - 1].pos, "the packs expanded by this '...' have different lengths");
        *members = pk->second.types.size();
        any = true;
    }
    return any ? end : 0;
}

// The pattern's tokens [from, end - 1) copied to the end of the stream - as a lambda's body is - with
// the function parameter pack renamed to `a$m` and every other pack bound to its m-th member, a type
// as a type name and a value as an enumerator; at_ is left at the copy, which ends in an End token.
void Parser::bindPatternMember(std::size_t from, std::size_t end, std::size_t m, std::vector<Shadow> *undo) {
    std::vector<TemplateParam> bound;
    std::vector<const Type *> types;
    std::vector<long long> values;
    const std::size_t copy = tokens_.size();
    for (std::size_t i = from; i + 1 < end; i++) {
        Token t = tokens_[i];
        auto pk = t.kind == TokenKind::Ident ? packs_.find(t.text) : packs_.end();
        if (pk != packs_.end()) {
            const PackBinding &pb = pk->second;
            // **In a pattern the pack stands for itself** and has no m-th member to bind: a pattern over
            // it in a signature is what Itanium spells `XspmlT_Li2EE`, which is not written here.
            if (pb.standsForItself())
                src_.fail(t.pos, "a pattern over the parameter pack '" + t.text + "' in a template's "
                                 "signature is not supported yet - '" + t.text + "...' alone is");
            if (m < pb.names.size() && pb.names[m] == t.text + "$" + std::to_string(m)) {
                t.text = pb.names[m];
            } else {
                TemplateParam p;
                p.name = t.text;
                if (pb.isValue) p.type = pb.valueType;
                bound.push_back(p);
                types.push_back(pb.isValue ? pb.valueType : pb.types[m]);
                values.push_back(pb.isValue ? pb.values[m] : 0);
            }
        }
        tokens_.push_back(t);
    }
    Token stop;
    stop.kind = TokenKind::End;
    tokens_.push_back(stop);
    tmpl_.bindTemplateParameters(bound, types, values, Packs(bound.size()), undo);
    at_ = copy;
}

// `f(g(a)...)`, `std::forward<A>(a)...`, `f(Ns...)`, `f((Ns * 2)...)`: one argument per member.
bool Parser::expandPackPattern(std::vector<ExprPtr> &args) {
    std::size_t members = 0;
    const std::size_t end = packPatternEnd(false, &members);
    if (end == 0) return false;
    const std::size_t from = at_;
    for (std::size_t m = 0; m < members; m++) {
        std::vector<Shadow> undo;
        struct Unbind {
            Parser *p; std::vector<Shadow> *u;
            ~Unbind() { p->tmpl_.unbindTemplateParameters(*u); }
        } unbind{ this, &undo };
        bindPatternMember(from, end, m, &undo);
        args.push_back(assign());
        if (peek().kind != TokenKind::End)
            src_.fail(peek().pos, "this pack expansion's pattern is not one expression");
    }
    at_ = end;
    return true;
}

// `{ 0, Ns... }`, `{ (Ns * 2)... }`: one element per member, each read as an initialiser.
bool Parser::expandPackInitialiser(std::vector<Init> &items) {
    std::size_t members = 0;
    const std::size_t end = packPatternEnd(false, &members);
    if (end == 0) return false;
    const std::size_t from = at_;
    for (std::size_t m = 0; m < members; m++) {
        std::vector<Shadow> undo;
        struct Unbind {
            Parser *p; std::vector<Shadow> *u;
            ~Unbind() { p->tmpl_.unbindTemplateParameters(*u); }
        } unbind{ this, &undo };
        bindPatternMember(from, end, m, &undo);
        items.push_back(parseInitialiser());
        if (peek().kind != TokenKind::End)
            src_.fail(peek().pos, "this pack expansion's pattern is not one initialiser");
    }
    at_ = end;
    return true;
}

// `List<(Ns * 2)...>`: one value per member, each a constant expression of the element type.
bool Parser::expandPackValues(TemplateArg &a) {
    std::size_t members = 0;
    const std::size_t end = packPatternEnd(true, &members);
    if (end == 0) return false;
    const std::size_t from = at_;
    for (std::size_t m = 0; m < members; m++) {
        std::vector<Shadow> undo;
        struct Unbind {
            Parser *p; std::vector<Shadow> *u;
            ~Unbind() { p->tmpl_.unbindTemplateParameters(*u); }
        } unbind{ this, &undo };
        bindPatternMember(from, end, m, &undo);
        const std::size_t argPos = peek().pos;
        const long long v = constantExpression("a template argument");
        if (peek().kind != TokenKind::End)
            src_.fail(peek().pos, "this pack expansion's pattern is not one expression");
        tmpl_.checkNonTypeArgument(a.type, v, argPos);
        a.pack.push_back(a.type);
        a.packValues.push_back(v);
    }
    at_ = end;
    return true;
}

// [temp.param]/9 on the deduced path: a parameter the call could not work out takes its
// default, replayed with every parameter before it bound - `class U = T` reads T's binding.
bool Parser::defaultTemplateArgument(const TemplateDecl &decl, std::size_t i,
                                     std::vector<const Type *> *binding,
                                     std::vector<long long> *values) {
    const TemplateParam &p = decl.params[i];
    if (p.defBegin == 0) return false;
    std::vector<Shadow> undo;
    struct Unbind {
        Parser *p; std::vector<Shadow> *u;
        ~Unbind() { p->tmpl_.unbindTemplateParameters(*u); }
    } unbind{ this, &undo };
    const Packs nopacks(1, std::vector<const Type *>());
    for (std::size_t k = 0; k < i; k++) {
        if ((*binding)[k] == nullptr) return false;
        std::vector<TemplateParam> one(1, decl.params[k]);
        std::vector<const Type *> ob(1, (*binding)[k]);
        std::vector<long long> ov(1, (*values)[k]);
        tmpl_.bindTemplateParameters(one, ob, ov, nopacks, &undo);
    }
    const std::size_t saved = at_;
    struct InArgs {
        bool &f; bool was;
        ~InArgs() { f = was; }
    } inArgs{ inTemplateArgs_, inTemplateArgs_ };
    inTemplateArgs_ = true;                    // the `>` after `N = 3` closes the list
    // Read where the signature is: a member template's in its class, any in its namespace (M1, std::thread).
    struct Scopes {
        Parser *p; const Type *cls; std::vector<std::string> ns;
        ~Scopes() { p->signatureClass_ = cls; p->namespaceStack_ = ns; }
    } scopes{ this, signatureClass_, namespaceStack_ };
    if (decl.isMember) {
        auto own = lookup_.typedefIndex_.find(decl.ownerTag);
        if (own != lookup_.typedefIndex_.end()) signatureClass_ = lookup_.typedefs_[own->second].type;
    }
    if (!decl.ns.empty()) namespaceStack_ = namespacePathStack(decl.ns);
    at_ = p.defBegin;
    if (p.isTemplate) {
        const TemplateDecl &named = templateTemplateArgument(decl, p);
        (*binding)[i] = named.ttParam >= 0 ? types_.templateParam(named.ttParam) : templateNameTypeOf(named);
    } else if (p.type == nullptr) {
        StorageClass sc;
        Qualifiers quals;
        const Type *base = specifiers(&sc, &quals);
        (*binding)[i] = declarator(base, true).type;
    } else {
        (*binding)[i] = paramTypeIn(p, *binding);
        (*values)[i] = constantExpression("a default template argument");
    }
    at_ = saved;
    return true;
}

// Whether parameter `i` appears anywhere in a pattern. One a specialization never
// mentions could not be worked out from any argument list, so it could never be
// chosen - worth refusing where it is written rather than leaving it silent.
bool mentionsParam(const Type *t, std::size_t i) {
    if (t == nullptr) return false;
    if (t->unqualified() != t) return mentionsParam(t->unqualified(), i);
    if (t->kind() == Kind::TemplateParam)
        return static_cast<std::size_t>(t->length()) == i;
    if (t->isArray() && t->boundParam() >= 0 && static_cast<std::size_t>(t->boundParam()) == i) return true;
    if (t->isPointer() || t->isArray()) return mentionsParam(t->pointee(), i);
    if (t->isReference()) return mentionsParam(t->referent(), i);
    if (t->kind() == Kind::PackExpansion) return mentionsParam(t->pointee(), i);
    // `R (*)(A)` and `R(Args...)`: the return and every parameter (M1 L1b).
    if (t->isFunction()) {
        if (mentionsParam(t->returns(), i)) return true;
        for (const Type *p : t->params()) if (mentionsParam(p, i)) return true;
        return false;
    }
    if (t->isMemberPointer() || t->isMemberFunctionPointer())
        return mentionsParam(t->pointee(), i) || mentionsParam(t->enclosing(), i);
    if (t->isSpecialization()) {
        int tt;
        if (ttRefName(t->templateName(), &tt) && static_cast<std::size_t>(tt) == i) return true;
        for (std::size_t k = 0; k < t->templateArgs().size(); k++) {
            const TemplateArg &a = t->templateArgs()[k];
            if (a.isPack) {
                if (a.expandsParam == static_cast<int>(i)) return true;
                for (std::size_t m = 0; m < a.pack.size(); m++)
                    if (mentionsParam(a.pack[m], i)) return true;
            } else if (a.isType && mentionsParam(a.type, i)) {
                return true;
            }
        }
        return false;
    }
    return false;
}

Parser::Trial::Trial(Parser *parser)
    : p(parser), at(parser->at_), classes(parser->classStack_.size()),
      pattern(parser->patternOnly_), split(parser->angleSplit_), syntaxMark(parser->syntaxTree_.size()) {
    p->src_.beginTrial();
}

Parser::Trial::~Trial() {
    p->src_.endTrial();
    p->at_ = at;
    p->classStack_.resize(classes);
    p->patternOnly_ = pattern;
    p->angleSplit_ = split;
    p->syntaxTree_.kill(syntaxMark);     // the tokens are read again, and what this read recorded is not the tree's
}

// `a::b::` as the stack of namespaces it names, which is what namespaceStack_ holds.
std::vector<std::string> Parser::namespacePathStack(const std::string &ns) {
    std::vector<std::string> stack;
    for (std::size_t s = 0, e; s < ns.size(); s = e + 2) {
        e = ns.find("::", s);
        if (e == std::string::npos) e = ns.size();
        if (e > s) stack.push_back(ns.substr(s, e - s));
    }
    return stack;
}

// A template named in an expression. 5.2 wants the arguments written out:
// deducing them from the call is 5.3, and a class template is 5.4.
ExprPtr Parser::templateCall(Program *program, const std::string &scope) {
    const std::string name = peek().text;
    const std::size_t pos = peek().pos;
    const TemplateDecl decl = tmpl_.templates_[name];
    if (decl.isClass) {
        // **`vector<int>()` is a temporary, not an instantiation this cannot
        // do.**
        const std::size_t save = at_;
        at_++;
        if (peek().is("<")) {
            const Type *cls = tmpl_.instantiateClass(decl, pos);
            if (cls != nullptr && cls->isStructOrUnion() && peek().is("(")) {
                at_++;
                return classTemporary(cls, pos);
            }
            if (cls != nullptr && cls->isStructOrUnion() && peek().is("{"))
                return listTemporary(cls, pos);
            // **`CNeeds<(N == 3)>::check()` - a static member through a
            // template-id.**
            if (cls != nullptr && cls->isStructOrUnion() && peek().is("::")) {
                at_++;
                return templateIdMember(cls, pos);
            }
        }
        // **The injected class name.** Inside V<T>'s own members `V` means
        // this specialization, not the template - [temp.local] - so `V(x)` is
        // a temporary of it and needs no argument list.
        if (peek().is("(")) {
            if (const Type *self = lookup_.findTypedef(name))
                if (self->isStructOrUnion()) {
                    at_++;
                    return classTemporary(self, pos);
                }
        }
        // **And before `::`**: `Tree::next(n)` inside Tree<T>'s members, its nested and derived
        // classes is the road `Tree<T>::next(n)` takes - [temp.local]/1. At file scope nothing
        // answers for the bare name and the refusal below stands.
        if (peek().is("::")) {
            if (const Type *self = lookup_.findTypedef(name))
                if (self->isStructOrUnion() && self->isSpecialization() &&
                    self->templateName() == name) {
                    at_++;
                    return templateIdMember(self, pos);
                }
        }
        at_ = save;
        refuseTemplateId();
    }
    at_++;

    // **No argument list, so they come from the call.** The arguments are parsed
    // before anything can be deduced from them, the other way round from the
    // written case - and the order overload resolution wants.
    if (!peek().is("<")) {
        if (!peek().is("("))
            src_.fail(pos, "'" + name + "' is a function template, and naming "
                           "one without calling it is not supported yet");
        at_++;
        std::vector<ExprPtr> callArgs;
        parseArguments(callArgs);

        std::vector<const Type *> argTypes;
        for (std::size_t i = 0; i < callArgs.size(); i++)
            argTypes.push_back(tmpl_.deductionArgType(*callArgs[i]));

        // **Every function template the name reaches is a candidate** - one namespace's, the nearest.
        std::string named;
        const std::vector<TemplateDecl> reached = tmpl_.templatesNamed(name, scope, &named);
        tmpl_.instantiateViableTemplates(reached, argTypes, pos);

        // Nothing deduced and no ordinary function - so say why the template
        // failed, which beats "not declared". With more than one overload the
        // first is representative enough for the message.
        if (lookup_.overloadsOf(name) == nullptr) {
            std::vector<const Type *> b;
            std::vector<long long> v;
            Packs p;
            std::string why;
            tmpl_.deduceTemplateArguments(decl, argTypes, &b, &v, &p, &why);
            // Deduction can succeed and the instantiation still fail, and then
            // there is no reason to print - so say that rather than end the
            // sentence on "and".
            if (why.empty())
                why = "no specialization of it could be made for these "
                      "arguments";
            src_.fail(pos, "'" + name + "' is a function template and " + why);
        }

        const std::string bare = name.substr(name.rfind("::") == std::string::npos ? 0 : name.rfind("::") + 2);
        // **In the template's own namespace**, whose ordinary functions of this name are candidates
        // beside its specializations - [over.match.best]: an exact non-template beats a template.
        // The global namespace's is its ordinary functions and its own specializations, no other's.
        if (named == "::") {
            std::vector<std::size_t> mine;
            if (const std::vector<std::size_t> *all = lookup_.overloadsOf(bare))
                for (std::size_t i : *all) if (!lookup_.functions_[i].fromTemplate) mine.push_back(i);
            if (const std::vector<std::size_t> *own = lookup_.overloadsOf("::" + bare))
                mine.insert(mine.end(), own->begin(), own->end());
            lookup_.functionIndex_["$global:" + bare] = mine;
            named = "$global:";
        }
        const std::string key = !named.empty() && lookup_.overloadsOf(named + bare) != nullptr ? named + bare
                              : !decl.ns.empty() && lookup_.overloadsOf(decl.ns + name) != nullptr
                              ? decl.ns + name : name;
        const Signature sig = overload_.resolveOverload(key, callArgs, pos);
        // **A specialization's defaults, as every ordinary call reads them.**
        applyDefaults(sig, callArgs, pos);
        return completeCall(sig.name, sig.symbol, nullptr, sig.returns,
                            sig.params, sig.variadic, pos, std::move(callArgs));
    }

    // **Every function template of this name is a candidate here too.** The written
    // list is read once per overload - `forward<T>` has one on `type &` and one on
    // `type &&` - and what each makes is ranked against the call's arguments below.
    std::string named;
    std::vector<TemplateDecl> overloads = tmpl_.templatesNamed(name, scope, &named);
    if (overloads.empty()) overloads.push_back(decl);
    // **The call's arguments are read first**, past the list, so a list that leaves parameters to the call
    // ([temp.arg.explicit]/3, `duration_cast<To>(d)`) can deduce them from these.
    const std::size_t listAt = at_;
    skipTemplateArguments();
    if (!peek().is("("))
        src_.fail(peek().pos, "'" + name + "' is a function template, and "
                              "naming one without calling it is not supported "
                              "yet");
    at_++;
    std::vector<ExprPtr> callArgs;
    parseArguments(callArgs);
    const std::size_t afterCall = at_;
    std::vector<const Type *> argTypes;
    for (std::size_t i = 0; i < callArgs.size(); i++) argTypes.push_back(tmpl_.deductionArgType(*callArgs[i]));
    // One overload's list, then what the call deduces of the rest; false with a reason where it cannot.
    auto specialize = [&](const TemplateDecl &t, std::string *why) -> std::string {
        std::vector<const Type *> binding;
        std::vector<long long> values;
        std::vector<TemplateArg> args;
        Packs packs;
        at_ = listAt;
        templateArguments(t, &binding, &values, &args, &packs);
        if (binding.size() < t.params.size()) {
            const std::size_t written = binding.size();
            binding.resize(t.params.size(), nullptr);
            values.resize(t.params.size(), 0);
            if (!tmpl_.deduceTemplateArguments(t, argTypes, &binding, &values, &packs, why, written)) return std::string();
            args = tmpl_.argumentsFromBinding(t.params, binding, values, packs);
        }
        return instantiate(t, binding, values, args, pos, packs).symbol;
    };
    std::vector<std::size_t> made;
    (void)program;
    for (std::size_t k = 0; k < overloads.size(); k++) {
        const bool wasInArgs = inTemplateArgs_;
        std::string symbol, why;
        try {
            Trial trial(this);
            symbol = specialize(overloads[k], &why);
        } catch (const SubstitutionFailure &f) {
            inTemplateArgs_ = wasInArgs;
            if (f.unsupported) src_.fail(f.pos, f.why);
            continue;
        }
        for (std::size_t i = 0; i < lookup_.functions_.size() && !symbol.empty(); i++)
            if (lookup_.functions_[i].symbol == symbol) { made.push_back(i); break; }
    }
    // Nothing applied: read the one template outside the trial, so its own diagnostic is printed.
    if (made.empty()) {
        std::string why;
        if (specialize(decl, &why).empty())
            src_.fail(pos, "'" + name + "' is a function template and " + why);
        src_.fail(pos, "'" + name + "' has no specialization for these template arguments");
    }
    at_ = afterCall;

    // One candidate is the answer; several are ranked under a key of their own, so nothing
    // else of this name joins. **A copy, not a reference**: parsing can move `lookup_.functions_`.
    Signature sig = lookup_.functions_[made[0]];
    if (made.size() > 1) {
        const std::string key = "$explicit:" + name;
        lookup_.functionIndex_[key] = made;
        sig = overload_.resolveOverload(key, callArgs, pos);
        lookup_.functionIndex_.erase(key);
    }
    // Written out rather than deduced, so no ranking chose it and nothing else
    // will mark it - a specialization is defined only where it was chosen.
    for (std::size_t i = 0; i < lookup_.functions_.size(); i++)
        if (lookup_.functions_[i].symbol == sig.symbol) { lookup_.functions_[i].used = true; break; }
    // The same defaults, on the path where the arguments were written out.
    applyDefaults(sig, callArgs, pos);
    return completeCall(sig.name, sig.symbol, nullptr, sig.returns, sig.params,
                        sig.variadic, pos, std::move(callArgs));
}

void Parser::refuseTemplateId() {
    const std::string name = peek().text;
    const std::size_t pos = peek().pos;
    at_++;
    if (peek().is("<")) skipTemplateArguments();
    // **`A<int>::n` is not an instantiation this cannot do.** The type is made
    // perfectly well in a declaration; what is missing is reading a template-id as
    // the qualifier of a name. The typedef it names is the whole workaround.
    if (peek().is("::"))
        src_.fail(pos, "'" + name + "<...>::' - naming a member through a "
                       "class template's argument list is not supported yet; "
                       "the type itself is made, so 'typedef " + name +
                       "<...> Name;' and then 'Name::' reaches the member");
    src_.fail(pos, "'" + name + "' is a " +
                   (tmpl_.templates_[name].isClass ? "class" : "function") +
                   " template, and instantiating one is not supported yet");
}


// **A member function template call, v.head<3>().**
ExprPtr Parser::memberTemplateCall(ExprPtr object, const Type *obj,
                                   const std::string &name, std::size_t pos) {
    // The class whose scope declares it - `d.twice(3)` finds B's template through a D.
    const Type *named = obj->unqualified();
    const Type *plain = memberTemplateOwner(named, name);
    if (plain == nullptr) plain = named;
    const std::vector<TemplateDecl> &templates = tmpl_.memberTemplates_[plain->tag() + "::" + name];
    TemplateDecl mt = templates.front();
    // [class.access.base]/5: the member's own access, and every base on the way from the class it was named in.
    if (!insideAccessOf(plain, mt.memberAccess) && !isFriendOf(plain) &&
        !memberReachable(named, plain, mt.memberAccess))
        src_.fail(pos, "'" + name + "' is a member template of '" + plain->describe() +
                       "' that is not accessible here" +
                       (plain != named ? " through '" + named->describe() + "'" : std::string()));

    std::vector<const Type *> binding;
    std::vector<long long> values;
    std::vector<TemplateArg> args;
    Packs packs;
    std::vector<ExprPtr> deducedArgs;
    bool deduced = false;

    if (peek().is("<")) {
        templateArguments(mt, &binding, &values, &args, &packs);
    } else {
        // **No argument list, so they come from the call** - the same order the
        // free-function path uses: the arguments are parsed first and the
        // parameters worked out from their types, [temp.deduct.call].
        expect("(");
        parseArguments(deducedArgs);
        deduced = true;
        std::vector<const Type *> argTypes;
        for (std::size_t i = 0; i < deducedArgs.size(); i++)
            argTypes.push_back(tmpl_.deductionArgType(*deducedArgs[i]));
        std::string why;
        bool ok = false;
        // **The first of the name's templates that deduces is the one called.**
        for (std::size_t t = 0; t < templates.size() && !ok; t++) {
            BoundParams bound(this);
            if (!templates[t].classParams.empty())
                tmpl_.bindTemplateParameters(templates[t].classParams, templates[t].classBinding, templates[t].classValues, templates[t].classPacks,
                                       &bound.undo);
            ok = tmpl_.deduceTemplateArguments(templates[t], argTypes, &binding, &values,
                                         &packs, &why);
            bound.unbindNow();
            if (ok) mt = templates[t];
        }
        if (!ok)
            src_.fail(pos, "'" + plain->describe() + "::" + name + "' is a "
                           "member function template and " + why);
        args = tmpl_.argumentsFromBinding(mt.params, binding, values, packs);
    }

    const Signature *sig = tmpl_.instantiateMemberTemplate(mt, binding, values, args, pos);
    if (sig == nullptr)
        src_.fail(pos, "'" + plain->describe() + "::" + name + "' could not be "
                       "instantiated with these template arguments");

    std::vector<ExprPtr> callArgs;
    if (deduced) {
        callArgs = std::move(deducedArgs);
    } else {
        if (!peek().is("("))
            src_.fail(peek().pos, "'" + name + "' is a member function "
                                  "template, and naming one without calling it "
                                  "is not supported yet");
        at_++;
        parseArguments(callArgs);
    }

    // **Odr-used here**: `s.m<int>(1)` bypasses resolveOverload, so pruning dropped it - a link error (1.28).
    markUsed(sig);
    // A copy, since applyDefaults and completeCall read it while the arguments
    // can grow lookup_.functions_ out from under a reference.
    const Signature chosen = *sig;
    applyDefaults(chosen, callArgs, pos);

    // A static member template takes no `this`; an object it was named through is still evaluated.
    if (chosen.isStaticMember) {
        ExprPtr call = completeCall(name, chosen.symbol, nullptr, chosen.returns, chosen.params,
                                    chosen.variadic, pos, std::move(callArgs), false);
        if (object == nullptr || clonePure(*object) != nullptr) return call;
        const Type *rt = call->type();
        ExprPtr both(new Comma(std::move(object), std::move(call)));
        both->setType(rt);
        return both;
    }
    if (object == nullptr)
        src_.fail(pos, "'" + plain->describe() + "::" + name + "' is a member function "
                       "template that is not static, so it needs an object to be called on");

    if (obj->isConst() && !chosen.constThis)
        src_.fail(pos, "'" + name + "' is not a const member function, and this "
                       "object is const");
    if (!overload_.refQualifierViable(chosen.refThis, chosen.constThis, objectIsLvalue(*object)))
        src_.fail(pos, overload_.refQualifierRefusal(name, objectIsLvalue(*object)));

    const Type *pointee = chosen.constThis ? types_.withConst(plain) : plain;
    const Type *thisType = types_.pointerTo(pointee);
    // `this` is the declaring base's address, which convert() moves to where that base sits.
    ExprPtr addr(new Unary('&', std::move(object)));
    addr->setType(types_.pointerTo(obj->isConst() ? types_.withConst(named) : named));
    if (plain != named) addr = convert(std::move(addr), thisType);
    addr->setType(thisType);

    std::vector<const Type *> full;
    full.push_back(thisType);
    for (std::size_t i = 0; i < chosen.params.size(); i++)
        full.push_back(chosen.params[i]);

    std::vector<ExprPtr> all;
    all.push_back(std::move(addr));
    for (std::size_t i = 0; i < callArgs.size(); i++)
        all.push_back(std::move(callArgs[i]));

    return completeCall(name, chosen.symbol, nullptr, chosen.returns, full,
                        chosen.variadic, pos, std::move(all), true);
}

// **A static member reached through a class template-id**, `CNeeds<(N==3)>::
// check()` or `std::numeric_limits<int>::max()`.
ExprPtr Parser::templateIdMember(const Type *cls, std::size_t pos) {
    (void)pos;
    std::size_t mpos = peek().pos;
    std::string member = declaredName("a member name");
    // `Tree<int>::It::Deep::twice(2)`: a nested class is a scope of its own, walked a name at a time.
    while (peek().is("::") && peekAt(1).kind == TokenKind::Ident) {
        const Type *nested = lookup_.lookupInClass(cls, member);
        if (nested == nullptr || !nested->isStructOrUnion()) break;
        at_++;
        cls = nested;
        mpos = peek().pos;
        member = declaredName("a member name");
    }
    // [class.qual]/2: `Tree::Tree` and `Tree<int>::Tree` name the constructor, not a type.
    if (cls->isSpecialization() && member == cls->templateName())
        src_.fail(mpos, "'" + cls->describe() + "::" + member + "' names the constructor, "
                        "not a type - write '" + member + "(...)'");
    const std::string key = cls->tag() + "::" + member;
    // **`C<int>::T(3)` and `C<int>::Nested(1)`** - a member type, cast to or built as a temporary.
    if (peek().is("(") && lookup_.overloadsOf(key) == nullptr)
        if (const Type *named = lookup_.lookupInClass(cls, member)) {
            if (named->isStructOrUnion()) { at_++; return classTemporary(named, mpos); }
            return functionalCast(named, mpos);
        }
    // **`C<int>::f(x)` or `C<int>::f<T>(x)` where f is a member template**: static, or through `*this`
    // from a member of C<int> or of a class derived from it - `FlowOf<Entry>::build(...)` (M1 L14).
    if ((peek().is("(") || peek().is("<")) && memberTemplateOwner(cls, member) != nullptr) {
        const bool related = lookup_.findLocal("this") != nullptr && !inStaticMember_ && currentClass_ != nullptr &&
                             (currentClass_->unqualified() == cls->unqualified() ||
                              publicBaseOffset(currentClass_, cls->unqualified()) >= 0);
        if (!related) return memberTemplateCall(nullptr, cls, member, mpos);
        // On the qualifier's subobject: the derived class's own member of the name is not asked, [class.qual].
        ExprPtr obj = thisObject();
        if (obj->type()->unqualified() != cls->unqualified()) obj = baseSubobject(std::move(obj), cls->unqualified());
        const Type *objType = obj->type();
        return memberTemplateCall(std::move(obj), objType, member, mpos);
    }
    if (peek().is("(")) {
        at_++;
        std::vector<ExprPtr> callArgs;
        parseArguments(callArgs);
        // **`CVector<I>::operator*(s)` in a member of CVector<4> or of a class derived from it** is
        // `Base::f(...)`: called on `this`, never dispatched, [expr.call]/1 - as for a plain base name.
        const Local *held = lookup_.findLocal("this");
        if (held != nullptr && !inStaticMember_ && currentClass_ != nullptr && lookup_.overloadsOf(key) != nullptr &&
            (currentClass_->unqualified() == cls->unqualified() ||
             publicBaseOffset(currentClass_, cls->unqualified()) >= 0)) {
            bool anyMember = false;
            for (std::size_t i : *lookup_.overloadsOf(key)) anyMember = anyMember || needsThis(lookup_.functions_[i]);
            if (anyMember) {
                ExprPtr obj = thisObject();
                const Type *objType = obj->type();
                return memberCallWith(std::move(obj), objType, member, mpos, std::move(callArgs), cls);
            }
        }
        if (lookup_.overloadsOf(key) == nullptr)
            src_.fail(mpos, "'" + cls->describe() + "' has no static "
                            "member function '" + member + "' - only "
                            "a static member is reachable through a "
                            "template-id with no object");
        const Signature &sig = overload_.resolveOverload(key, callArgs, mpos);
        applyDefaults(sig, callArgs, mpos);
        if (needsThis(sig))
            src_.fail(mpos, "'" + key + "' is not a static member "
                            "function, so it has to be called on an "
                            "object");
        if (sig.access != Access::Public) {
            const Type *ownerType = lookup_.findTypedef(sig.owner);
            if (ownerType == nullptr ||
                (!insideAccessOf(ownerType, sig.access) &&
                 !isFriendOf(ownerType))) {
                const char *how = sig.access == Access::Private
                                      ? "private" : "protected";
                src_.fail(mpos, "'" + key + "' is " + how + " in '" +
                                sig.owner + "'");
            }
        }
        return completeCall(key, sig.symbol, nullptr, sig.returns,
                            sig.params, sig.variadic, mpos,
                            std::move(callArgs), false);
    }
    if (const Type::StaticMember *sm = cls->findStaticMember(member))
        return staticMemberRef(cls, *sm, cls->tag(), mpos);
    // `&C<int>::f`, f a static member function named and not called - a function as a value (M1).
    if (const std::vector<std::size_t> *set = lookup_.overloadsOf(key)) {
        bool allStatic = true;
        for (std::size_t i : *set) allStatic = allStatic && lookup_.functions_[i].isStaticMember;
        if (allStatic)
            if (ExprPtr f = functionAsValue(key, mpos)) return f;
    }
    // **An enumerator of the specialization is reached the same way**, and is
    // a value rather than an object - so there is nothing to take the address
    // of and the number is the whole of it.
    if (const EnumConst *e = lookup_.enumInClass(cls, member)) {
        return enumeratorValue(*e);
    }
    // `Base<T>::v = x` inside a member of a class derived from the specialization: `(*this).Base<T>::v`,
    // [class.mfct.non-static]/3 (1.27, 2026-10-21, review of 1.25, item 3).
    if (const Member *m = cls->findMember(member))
        if (ExprPtr mine = qualifiedMemberThroughThis(cls, *m, member, mpos)) return mine;
    if (unevaluated_ > 0)
        if (const Member *m = cls->findMember(member)) return memberWithoutObject(cls, *m, mpos);
    src_.fail(mpos, "'" + cls->describe() + "' has no static member or "
                    "enumerator called '" + member + "'");
}

bool Parser::dependentArgs(const std::vector<TemplateArg> &args) const {
    for (const TemplateArg &a : args) {
        if (a.isParam || a.dependentValue || a.expr != nullptr) return true;
        if (a.isPack) {
            for (const Type *m : a.pack)
                if (dependentType(m)) return true;
        } else if (a.isType && dependentType(a.type)) {
            return true;
        }
    }
    return false;
}

bool Parser::dependentType(const Type *t) const {
    for (int depth = 0; t != nullptr && depth < 64; depth++) {
        if (t->kind() == Kind::TemplateParam || t->kind() == Kind::DependentMember ||
            t->kind() == Kind::DependentExpr)
            return true;
        if (t->unqualified() != t) { t = t->unqualified(); continue; }
        if (t->isStructOrUnion()) {
            int tt;
            return t->isSpecialization() &&
                   (ttRefName(t->templateName(), &tt) || dependentArgs(t->templateArgs()));
        }
        if (t->kind() == Kind::Function) {
            for (const Type *p : t->params())
                if (dependentType(p)) return true;
            t = t->returns();
            continue;
        }
        if (t->isPointer() || t->isReference() || t->isArray()) { t = t->pointee(); continue; }
        return false;
    }
    return false;
}
