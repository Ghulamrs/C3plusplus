// The parser: templates. Template parameters and the scope they are bound in,
// argument deduction, partial ordering and specialisation, and the token replay
// that turns a pattern into an instantiation. Rung 5.
#include "Parser.h"
#include "ParserInternal.h"
#include "../Mangle.h"
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
            bindTemplateParameters(earlier, stand, std::vector<long long>(earlier.size(), 0),
                                   Packs(earlier.size()), &undo);
            struct Unbind {
                Parser *p; std::vector<Shadow> *u;
                ~Unbind() { p->unbindTemplateParameters(*u); }
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

// **The parameters are bound to the argument list, and the tables are put back
// exactly as they were.** A type parameter becomes a type name and a non-type one
// an enumerator, which is what makes `T x` and `int a[N]` read with no new path.
void Parser::bindTemplateParameters(const std::vector<TemplateParam> &params,
                                    const std::vector<const Type *> &binding,
                                    const std::vector<long long> &values,
                                    const Packs &packs,
                                    std::vector<Shadow> *undo) {
    for (std::size_t i = 0; i < params.size(); i++) {
        const TemplateParam &p = params[i];
        Shadow s;
        s.name = p.name;
        s.isType = p.type == nullptr;
        // **A pack is not a type name.** Nothing may write `Ts` on its own;
        // what reads it is `Ts...` and `sizeof...(Ts)`, and both want the
        // list rather than a type standing for it.
        if (p.isPack) {
            s.isPack = true;
            auto had = packs_.find(p.name);
            if (had != packs_.end()) {
                s.had = true;
                s.hadPack = had->second;
            }
            PackBinding pb;
            // **A non-type pack is a list of values**, its type the written one or T's binding for `T... Vs`.
            pb.isValue = p.type != nullptr;
            if (pb.isValue) pb.valueType = paramTypeIn(p, binding);
            if (i < binding.size() && binding[i] != nullptr &&
                binding[i]->kind() == Kind::TemplateParam) {
                pb.types.push_back(binding[i]);       // reading a pattern
                if (pb.isValue) pb.values.push_back(0);
            } else if (i < packs.size()) {
                pb.types = packs[i];
                pb.values = packs[i].values;
            }
            packs_[p.name] = pb;
            undo->push_back(s);
            continue;
        }
        // **A template template parameter is an entry in `templates_` under its own name**: the
        // argument's declaration, so `C<int>` reads as `Box<int>` with nothing taught to any use; in
        // a pattern a stand-in `$TT<i>` with the parameter's own list, so `C<T>` reads as a pattern.
        if (p.isTemplate) {
            s.isTemplate = true;
            auto had = templates_.find(p.name);
            if (had != templates_.end()) { s.hadTemplate = true; s.hadDecl = had->second; }
            const Type *b = i < binding.size() ? binding[i] : nullptr;
            if (b != nullptr && b->kind() == Kind::TemplateParam) {
                TemplateDecl stand;
                stand.name = "$TT" + std::to_string(b->length());
                stand.params = p.inner;
                stand.isClass = true;
                stand.ttParam = static_cast<int>(b->length());
                templates_[p.name] = stand;
            } else if (b != nullptr && b->isTemplateName() && findTemplate(b->templateName()) != templates_.end()) {
                templates_[p.name] = findTemplate(b->templateName())->second;
            } else {
                src_.fail(p.pos, "'" + p.name + "' is a template template parameter and this binds "
                                 "it to no template");
            }
            undo->push_back(s);
            continue;
        }
        if (s.isType) {
            auto it = typedefIndex_.find(p.name);
            if (it != typedefIndex_.end()) { s.had = true; s.was = it->second; }
            typedefIndex_[p.name] = typedefs_.size();
            typedefs_.push_back(TypedefName{ p.name, binding[i] });
            boundTypeParams_[p.name]++;
        } else if (i < binding.size() && binding[i] != nullptr &&
                   binding[i]->kind() == Kind::TemplateParam) {
            // **A non-type parameter read as a pattern**, the same signal a
            // pack uses: its binding is a Kind::TemplateParam rather than a
            // value, so a bare `V<N>` reads as a reference to N.
            s.isParamRef = true;
            auto it = nonTypePatternParams_.find(p.name);
            if (it != nonTypePatternParams_.end()) {
                s.hadParamRef = true; s.paramRefWas = it->second;
            }
            nonTypePatternParams_[p.name] = binding[i]->length();
            // The concrete value, as a second undo entry so both are restored.
            Shadow ev;
            ev.name = p.name;
            ev.isType = false;
            auto en = enumIndex_.find(p.name);
            if (en != enumIndex_.end()) { ev.had = true; ev.was = en->second; }
            enumIndex_[p.name] = enums_.size();
            enums_.push_back(EnumConst{ p.name, values[i] });
            boundValueParams_[p.name]++;
            undo->push_back(ev);
        } else {
            auto it = enumIndex_.find(p.name);
            if (it != enumIndex_.end()) { s.had = true; s.was = it->second; }
            enumIndex_[p.name] = enums_.size();
            EnumConst e{ p.name, values[i] };
            // [temp.param]/5: an enumeration-typed parameter is a value of that enumeration, not an int.
            const Type *pt = paramTypeIn(p, binding);
            if (pt != nullptr && pt->isInteger() && !pt->tag().empty()) e.type = pt;
            e.paramType = pt;
            enums_.push_back(e);
            boundValueParams_[p.name]++;
        }
        undo->push_back(s);
    }
}

void Parser::unbindTemplateParameters(const std::vector<Shadow> &undo) {
    for (std::size_t k = undo.size(); k-- > 0; ) {
        const Shadow &s = undo[k];
        if (s.isPack) {
            if (s.had) packs_[s.name] = s.hadPack;
            else       packs_.erase(s.name);
            continue;
        }
        if (s.isTemplate) {
            if (s.hadTemplate) templates_[s.name] = s.hadDecl;
            else               templates_.erase(s.name);
            continue;
        }
        if (s.isType) {
            if (s.had) typedefIndex_[s.name] = s.was;
            else       typedefIndex_.erase(s.name);
            if (--boundTypeParams_[s.name] <= 0) boundTypeParams_.erase(s.name);
        } else if (s.isParamRef) {
            if (s.hadParamRef) nonTypePatternParams_[s.name] = s.paramRefWas;
            else               nonTypePatternParams_.erase(s.name);
        } else {
            if (s.had) enumIndex_[s.name] = s.was;
            else       enumIndex_.erase(s.name);
            if (--boundValueParams_[s.name] <= 0) boundValueParams_.erase(s.name);
        }
    }
}

const Type *Parser::templateNameTypeOf(const TemplateDecl &decl) const {
    return types_.templateNameType(decl.name, decl.ns, decl.isAlias, decl.classKey);
}

// `$TT3` names template template parameter 3, and nothing a program writes begins with a `$`.
static bool ttRefName(const std::string &name, int *index) {
    if (name.compare(0, 3, "$TT") != 0) return false;
    *index = std::atoi(name.c_str() + 3);
    return true;
}

bool Parser::templateParamRef(const std::string &name, int *index) { return ttRefName(name, index); }

// What a template template parameter is bound to by a specialization: its template's stand-in, or
// - while two patterns are ordered against each other - the other pattern's own parameter.
const Type *Parser::templateOf(const Type *spec) const {
    int tt;
    if (ttRefName(spec->templateName(), &tt)) return types_.templateParam(tt);
    return templateNameTypeOf(templates_.find(spec->templateName())->second);
}

// **The rule is clang's, which takes P0522 as a defect report under -std=c++11** (C++11's letter is
// an exact match): a pack of types on either side stands for every type parameter the other has
// left, each other parameter wants one of its kind, and what the argument has over needs a default.
bool Parser::templateTemplateMatches(const std::vector<TemplateParam> &param,
                                     const std::vector<TemplateParam> &arg, std::string *why,
                                     std::size_t *at) {
    std::size_t a = 0;
    for (std::size_t p = 0; p < param.size(); p++) {
        const bool packHere = param[p].isPack, packThere = a < arg.size() && arg[a].isPack;
        if (packHere || packThere) {
            const std::vector<TemplateParam> &rest = packHere ? arg : param;
            for (std::size_t k = packHere ? a : p; k < rest.size(); k++)
                if (rest[k].type != nullptr || rest[k].isTemplate) {
                    *why = "a pack of types cannot stand for the " +
                           std::string(rest[k].isTemplate ? "template" : "non-type") + " parameter '" +
                           rest[k].name + "'";
                    *at = packHere ? param[p].namePos : rest[k].namePos;
                    return false;
                }
            return true;
        }
        if (a >= arg.size()) {
            *why = "it has " + std::to_string(arg.size()) + " template parameter(s) and the parameter's list has " +
                   std::to_string(param.size());
            return false;
        }
        const TemplateParam &P = param[p], &A = arg[a++];
        // A kind mismatch is reported where clang reports it: at the parameter of the list.
        if (P.isTemplate != A.isTemplate || (P.type == nullptr) != (A.type == nullptr)) {
            *why = "its parameter '" + A.name + "' is " +
                   (A.isTemplate ? "a template" : A.type != nullptr ? "a non-type" : "a type") +
                   " parameter where the parameter's list has " +
                   (P.isTemplate ? "a template" : P.type != nullptr ? "a non-type" : "a type") + " one";
            *at = P.namePos;
            return false;
        }
        if (P.isTemplate && !templateTemplateMatches(P.inner, A.inner, why, at)) return false;
    }
    for (; a < arg.size(); a++)
        if (!arg[a].isPack && arg[a].defBegin == 0) {
            *why = "its parameter '" + arg[a].name + "' has no default, and the parameter's list gives it nothing";
            return false;
        }
    return true;
}

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
    if (it == templates_.end() || !it->second.isClass)
        src_.fail(pos, "'" + written + "' does not name a class template or an alias template, "
                       "which is what '" + p.name + "' of '" + decl.name + "' takes");
    std::string why;
    std::size_t at = pos;
    if (!templateTemplateMatches(p.inner, it->second.params, &why, &at))
        src_.fail(at, "'" + written + "' cannot be the argument for the template template "
                      "parameter '" + p.name + "' of '" + decl.name + "': " + why);
    return it->second;
}

std::vector<TemplateArg> Parser::argumentsFromBinding(
    const std::vector<TemplateParam> &params, const std::vector<const Type *> &binding,
    const std::vector<long long> &values, const Packs &packs) const {
    std::vector<TemplateArg> args;
    for (std::size_t i = 0; i < binding.size() && i < params.size(); i++) {
        TemplateArg a;
        if (params[i].type != nullptr && !params[i].isPack) {
            a.isType = false; a.type = paramTypeIn(params[i], binding); a.value = values[i];
        } else if (params[i].isPack) {
            a.isType = true; a.isPack = true; a.pack = packs[i];
            if (params[i].type != nullptr) {
                a.packIsValue = true; a.packValues = packs[i].values; a.type = paramTypeIn(params[i], binding);
            }
        } else {
            a.isType = true; a.type = binding[i]; a.isTemplate = params[i].isTemplate;
        }
        args.push_back(a);
    }
    return args;
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
        ~Unbind() { p->unbindTemplateParameters(undo); }
    } guard{ this, std::vector<Shadow>() };
    bindTemplateParameters(decl.params, binding, values,
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
        // `template <class T> auto f(T) -> T`: the pattern's return type is the trailing one.
        if (mentionsDeduced(written) && tokens_[skipFunctionQualifiers(at_)].is("->")) {
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
        if (depth == 0 && parens == 0 && peek().is("=")) initialiser = true;
        if (sawInit != nullptr && depth == 0 && peek().is("=")) *sawInit = true;
        if (sawParen != nullptr && depth == 0 && peek().is("(")) *sawParen = true;
        // Braces after that `=` are a list, `C<T>::arr[2] = { 1, 2 };`, and no body.
        if (peek().is("{")) { depth++; body = body || !initialiser; at_++; continue; }
        if (peek().is("}")) {
            at_++;
            if (--depth == 0) { consume(";"); return body; }
            continue;
        }
        if (peek().is(";") && depth == 0) { at_++; return body; }
        at_++;
    }
}

// Defined below, beside the pattern matching it belongs with.
static bool mentionsParam(const Type *t, std::size_t i);

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
        if (templates_.count(decl.name) != 0)
            src_.fail(decl.pos, "'" + decl.name + "' is already a template");
        int depth = 0;
        while (peek().kind != TokenKind::End && !(depth == 0 && peek().is(";"))) {
            if (peek().is("(") || peek().is("{")) depth++;
            else if (peek().is(")") || peek().is("}")) depth--;
            at_++;
        }
        expect(";");
        templates_[decl.name] = decl;
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
                if (t.is("(") || t.is("::") || t.is("{") || t.is(";")) break;
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
        TemplateDecl &owner = templates_[peek().text];
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
        auto primary = templates_.find(peekAt(1 + q).text);
        if (primary == templates_.end() || !primary->second.isClass)
            src_.fail(peekAt(1 + q).pos, "'" + peekAt(1 + q).text + "' is not a class "
                                         "template, so there is nothing here to "
                                         "specialize");
        refuseAfterPack(decl.params);
        TemplateDecl::Partial ps;
        ps.params = decl.params;
        ps.pos = decl.pos;
        at_ += 2 + q;
        partialArguments(&ps, primary->second.params);
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
    else              requireDeducibleAfterPack(decl, fnPattern);
    // Where it was written, for the manglers - the table's key stays bare.
    decl.ns = namespacePrefix();
    at_ = decl.afterParams;
    bool sawInit = false, sawParen = false;
    const bool defined = skipTemplatedDefinition(&sawInit, &sawParen);

    // **A member of a class template defined outside it belongs to the class**, not
    // to a template of its own. The declarator already reads a qualified name; what
    // is new is that the qualifier is a template-id, naming the pattern.
    if (!qualifier.empty()) {
        const Type *of = findTypedef(qualifier);
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
                    const Type *head = findTypedef(qualifier.substr(0, c));
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
        auto owner = templates_.find(of->templateName());
        if (owner == templates_.end())
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
        if (decl.defined) fnTemplates_[decl.name].push_back(decl);
        auto it = templates_.find(decl.name);
        if (it == templates_.end()) {
            templates_[decl.name] = decl;
        } else if (decl.defined && !it->second.defined && !it->second.isClass) {
            decl.outOfLine = it->second.outOfLine;
            it->second = decl;
        }
        return true;
    }

    // A template may be declared and then defined. The definition is the one
    // worth keeping, since instantiating is replaying its tokens - but any
    // out-of-line members gathered against the declaration come with it.
    auto it = templates_.find(decl.name);
    if (it == templates_.end()) {
        templates_[decl.name] = decl;
    } else if (decl.defined && !it->second.defined) {
        decl.outOfLine = it->second.outOfLine;
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
        else if (t.kind == TokenKind::Ident && depth == 0 && fnTemplates_.count(t.text) != 0 &&
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
    const std::vector<TemplateDecl> overloads = fnTemplates_[name];
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
            if (!deduceTemplateArguments(decl, wanted->params(), &m.binding, &m.values, &m.packs, &why))
                continue;
            m.args = argumentsFromBinding(decl.params, m.binding, m.values, m.packs);
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
                if (!p.matchPattern(pb[i], pa[i], &binding, &why)) return false;
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
        const std::size_t before = functions_.size();
        instantiate(overloads[m.which], m.binding, m.values, m.args, pos, m.packs);
        if (functions_.size() == before)
            src_.fail(pos, "'" + specializationKey(name, m.args) + "' has already been used further up, "
                           "so specializing it here is too late - the specialization goes before the first use");
        specializations_.back().start = copy;
        functions_.back().used = true;          // a definition the program wrote, emitted as one
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
    auto primary = templates_.find(name);
    if (primary == templates_.end() || !primary->second.isClass)
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

    const std::string tag = specializationKey(name, args);
    // **Too late is an error, not a redefinition.** [temp.expl.spec]: a
    // specialization has to be declared before the first use that would instantiate
    // the template, or two different classes have been given one name.
    if (findTypedef(tag) != nullptr)
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

    declareTypeName(tag, made);
    expect(";");
    return true;
}

// The tokens read without being consumed. skipTemplateArguments is what walks
// the argument list, so the `>>` split has to be put back too.
bool Parser::atOutOfLineSpecial(std::string *what, std::string *nested) {
    if (peek().kind != TokenKind::Ident) return false;
    auto t = templates_.find(peek().text);
    if (t == templates_.end() || !t->second.isClass || !peekAt(1).is("<"))
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
    // **Defaults are replayed with the earlier parameters bound.**
    std::vector<Shadow> undo;
    struct Unbind {
        Parser *p; std::vector<Shadow> *u;
        ~Unbind() { p->unbindTemplateParameters(*u); }
    } unbind{ this, &undo };

    for (std::size_t i = 0; i < decl.params.size(); i++) {
        const TemplateParam &p = decl.params[i];
        TemplateArg a;

        // If the arguments have run out, the rest must carry defaults. A comma
        // is due only before a real argument, so it is consumed here and not at
        // the closing angle.
        bool defaulted = !p.isPack && atClosingAngle();
        if (!decl.isClass && atClosingAngle() && (p.isPack ? i > 0 : p.defBegin == 0 && p.type == nullptr))
            src_.fail(peek().pos, "'" + decl.name + "' is given " + std::to_string(i) + " of its template "
                                  "arguments, and deducing the rest from the call is not supported yet - write them all");
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
                    bindTemplateParameters(one, ob, ov, op, &undo);
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
            bindTemplateParameters(one, ob, ov, op, &undo);
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
                    checkNonTypeArgument(a.type, v, argPos);
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
            if (peek().is("(") && !peekAt(1).is("*") && !peekAt(1).is("&") && !peekAt(1).is("&&"))
                src_.fail(peek().pos, "a function type as a template argument, 'R(Args...)', is not "
                                      "supported yet - a pointer to one, 'R (*)(Args...)', is read");
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
                checkNonTypeArgument(pt, v, argPos);
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
    while (binding->size() < decl.params.size() && decl.params[binding->size()].isPack) {
        TemplateArg empty;
        empty.isPack = true;
        empty.packIsValue = decl.params[binding->size()].type != nullptr;
        if (empty.packIsValue) empty.type = paramTypeIn(decl.params[binding->size()], *binding);
        binding->push_back(nullptr);
        values->push_back(0);
        args->push_back(empty);
    }
    if (binding->size() < decl.params.size())
        src_.fail(peek().pos, "'" + decl.name + "' has a parameter after the pack '" +
                              decl.params[binding->size() - 1].name + "', and deducing it from the "
                              "call once the pack's arguments are written is not supported yet");
    takeClosingAngle();
    inTemplateArgs_ = wasInArgs;
}

std::string Parser::specializationKey(const std::string &name,
                                      const std::vector<TemplateArg> &args) const {
    std::string key = name + "<";
    for (std::size_t i = 0; i < args.size(); i++) {
        if (i > 0) key += ",";
        if (args[i].isPack) {
            key += "{";
            for (std::size_t k = 0; k < args[i].pack.size(); k++) {
                if (k > 0) key += ",";
                // A pattern's `Ns...` is keyed as the expansion, apart from a pack of the one type `T`.
                if (args[i].expandsParam >= 0) key += "$T" + std::to_string(args[i].expandsParam) + "...";
                else if (args[i].packIsValue && args[i].pack[k]->kind() == Kind::TemplateParam)
                    key += "$P" + std::to_string(args[i].pack[k]->length());
                else if (args[i].packIsValue) key += std::to_string(args[i].packValues[k]);
                else if (args[i].pack[k]->kind() == Kind::TemplateParam)
                    key += "$T" + std::to_string(args[i].pack[k]->length());
                else key += args[i].pack[k]->describe();
            }
            key += "}";
        }
        else if (args[i].expr != nullptr) key += "$X" + args[i].expr->key;
        else if (args[i].isParam)
            // A non-type parameter reference - `V<N>` in a pattern - keyed by
            // which parameter, or `V<N>` and `V<M>` would be one type.
            key += "$P" + std::to_string(args[i].paramIndex);
        else if (!args[i].isType) key += std::to_string(args[i].value);
        // A pattern's argument is a template parameter, and describe() would
        // put a space in a tag every table is keyed by.
        else if (args[i].type->kind() == Kind::TemplateParam)
            key += "$T" + std::to_string(args[i].type->length());
        // A template named as an argument: `Holder<Box,int>`, the name as written.
        else if (args[i].isTemplate)
            key += args[i].type->templateNamespace() + args[i].type->templateName();
        else key += args[i].type->describe();
    }
    return key + ">";
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
    const std::string display = specializationKey(decl.name, args);
    std::string key = display;
    for (std::size_t i = 0; i < fn->params().size(); i++)
        key += "#" + fn->params()[i]->describe();
    // And which template: two differing only in `decltype(...)` returns were one key, the first.
    key += "@" + std::to_string(decl.afterParams);

    if (const std::vector<std::size_t> *had = overloadsOf(key))
        return functions_[(*had)[0]];

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
    sp.params = decl.params;
    sp.packs = packs;
    sp.symbol = symbol;
    sp.fn = fn;
    sp.binding = binding;
    sp.values = values;
    sp.start = decl.afterParams;
    sp.pos = pos;
    specializations_.push_back(sp);

    // **Under two keys, on purpose.** "twice<int>" is what the replayed definition
    // declares and what a repeat of the same arguments finds; "twice" is what
    // overload resolution must see, a specialization competing with the ordinary.
    const std::size_t at = functions_.size();
    functionIndex_[key].push_back(at);
    functionIndex_[decl.name].push_back(at);
    // And under the template's namespace, where that namespace's ordinary functions of the name
    // are: a template and a non-template there compete as one set, and the non-template wins a tie.
    // Where that namespace is inline, under the enclosing one's as well - `lib::twice(1)`.
    if (!decl.ns.empty()) functionIndex_[decl.ns + decl.name].push_back(at);
    {
        const std::vector<std::string> alts = inlineAliasPrefixes(decl.ns);
        for (std::size_t a = 0; a < alts.size(); a++) functionIndex_[alts[a] + decl.name].push_back(at);
    }
    functions_.push_back(Signature{ display, symbol, fn->returns(), fn->params(),
                                    fn->isVariadicFn(), false, pos, false,
                                    std::string(), false, Access::Public });
    functions_.back().fromTemplate = true;
    functions_.back().pattern = patternFn;
    // **The defaults the pattern's parameter list just read.**
    if (!pendingDefaults_.empty()) {
        defaultArgs_[symbol] = pendingDefaults_;
        defaultArgNamespace_[symbol] = namespaceStack_;
    }
    return functions_.back();
}

// **A body cannot be written where the call is**, so every specialization is
// recorded and the definitions replayed afterwards, to a fixed point. And whether
// anything under this key was chosen by a call, which gates every body it holds.
bool Parser::memberIsUsed(const std::string &key) const {
    const std::vector<std::size_t> *set = overloadsOf(key);
    for (std::size_t k = 0; set != nullptr && k < set->size(); k++)
        if (functions_[(*set)[k]].used) return true;
    return false;
}

void Parser::instantiatePending() {
    for (bool again = true; again; ) {
        again = false;
        for (std::size_t i = 0; i < specializations_.size(); i++) {
            if (specializations_[i].emitted) continue;
            // **Made where it was asked for, defined only where it was chosen** -
            // deduction instantiates a candidate before it can rank one, and an
            // ordinary function may win. A body skipped now may be wanted later.
            std::vector<PendingBody> now;
            std::vector<std::size_t> outsideNow;
            if (specializations_[i].isClass) {
                std::vector<PendingBody> later;
                for (std::size_t b = 0; b < specializations_[i].bodies.size(); b++) {
                    const PendingBody &body = specializations_[i].bodies[b];
                    // **This overload, not this name.** Every constructor of
                    // a class shares one key, so asking the key replays them
                    // all as soon as any is called.
                    const bool wanted =
                        body.which != PendingBody::npos() &&
                        body.which < functions_.size()
                            ? functions_[body.which].used
                            : memberIsUsed(body.key);
                    (wanted ? now : later).push_back(body);
                }
                specializations_[i].bodies = later;

                // **Looked up fresh, because the list can still be growing.** An
                // out-of-line definition may be written further down the file than
                // the use that asked for the class.
                const TemplateDecl &d = templates_[specializations_[i].name];
                std::vector<bool> &done = specializations_[i].outsideDone;
                done.resize(d.outOfLine.size(), false);
                for (std::size_t k = 0; k < d.outOfLine.size(); k++) {
                    if (done[k] || specializations_[i].fromPartial) continue;
                    const TemplateDecl::OutOfLine &o = d.outOfLine[k];
                    // The class the member is in: the specialization, or a class nested in it.
                    const std::string tag = specializations_[i].key + (o.nested.empty() ? "" : "::" + o.nested);
                    if (o.constructor || o.destructor) {
                        if (!memberIsUsed(o.constructor ? constructorKey(tag) : destructorKey(tag))) continue;
                        done[k] = true;
                        PendingBody b;
                        b.tag = tag; b.start = o.start; b.local = o.member; b.which = PendingBody::npos();
                        b.key = o.constructor ? constructorKey(tag) : destructorKey(tag);
                        now.push_back(b);
                        continue;
                    }
                    // **A static data member is defined only where an evaluated
                    // expression named it**, [temp.inst]/3, as clang emits it.
                    if (d.outOfLine[k].isData) {
                        const Type *spec = findTypedef(tag);
                        const Type::StaticMember *sm =
                            spec != nullptr ? spec->findStaticMember(d.outOfLine[k].member)
                                            : nullptr;
                        if (sm != nullptr && !usedStaticMembers_.count(sm->symbol)) continue;
                    } else if (!memberIsUsed(tag + "::" + d.outOfLine[k].member)) continue;
                    done[k] = true;
                    outsideNow.push_back(k);
                }
                if (now.empty() && outsideNow.empty()) continue;
            } else {
                const std::vector<std::size_t> *had =
                    overloadsOf(specializations_[i].key);
                if (had == nullptr || !functions_[(*had)[0]].used) continue;
                specializations_[i].emitted = true;
            }
            again = true;

            // Copied, not held by reference: replaying may append to the
            // vector and move it.
            const Specialization sp = specializations_[i];
            TemplateDecl decl = templates_[sp.name];

            std::vector<Shadow> undo;
            bindTemplateParameters(sp.params, sp.binding, sp.values, sp.packs, &undo);
            const std::string wasKey = instantiationKey_;
            const std::string wasOf = instantiationOf_;
            instantiationKey_ = sp.isClass ? std::string() : sp.key;
            instantiationOf_ = sp.isClass ? std::string() : sp.name;

            const std::size_t resume = at_;
            // **An instantiated definition has vague linkage** - [basic.link] and [temp.spec]: the
            // same specialization may be produced by every translation unit that uses it, and the
            // linker folds the copies rather than rejecting them.
            const bool wasReplayingInline = replayingInline_;
            replayingInline_ = true;
            if (sp.isClass) {
                // Its member functions, held here; inlineOwner_ gives the scope.
                replayInlineBodies(now);
                // And the ones written outside it, which need no owner: the
                // tokens say `Box<T>::get`, so with T bound the ordinary
                // member-definition path reads the qualifier itself.
                for (std::size_t k = 0; k < outsideNow.size(); k++) {
                    at_ = templates_[sp.name].outOfLine[outsideNow[k]].start;
                    topLevel(*current_);
                }
            } else {
                // Its body reads names in the namespace it was declared in, wherever it is used from.
                const std::vector<std::string> wasStack = namespaceStack_;
                if (!decl.ns.empty()) {
                    namespaceStack_.clear();
                    for (std::size_t s = 0, e; s < decl.ns.size(); s = e + 2) {
                        e = decl.ns.find("::", s);
                        if (e == std::string::npos) e = decl.ns.size();
                        if (e > s) namespaceStack_.push_back(decl.ns.substr(s, e - s));
                    }
                }
                at_ = sp.start;
                topLevel(*current_);
                namespaceStack_ = wasStack;
            }
            replayingInline_ = wasReplayingInline;
            at_ = resume;

            instantiationKey_ = wasKey;
            instantiationOf_ = wasOf;
            unbindTemplateParameters(undo);
        }
    }
}

bool Parser::mentionsDeduced(const Type *t) {
    if (t == nullptr) return false;
    if (t->kind() == Kind::Deduced) return true;
    if (t->unqualified() != t) return mentionsDeduced(t->unqualified());
    if (t->isPointer() || t->isArray()) return mentionsDeduced(t->pointee());
    if (t->isReference()) return mentionsDeduced(t->referent());
    return false;
}

// The declared type with `auto` replaced, keeping everything written around
// it: `const auto &` deduced as int is `const int &`.
const Type *Parser::substituteDeduced(const Type *t, const Type *with) {
    if (t->kind() == Kind::Deduced) return with;
    if (t->unqualified() != t)
        return types_.withConst(substituteDeduced(t->unqualified(), with));
    if (t->isPointer()) return types_.pointerTo(substituteDeduced(t->pointee(), with));
    if (t->isReference()) {
        const Type *r = substituteDeduced(t->referent(), with);
        if (r->isReference()) return t->isRValueReference() ? r : types_.referenceTo(r->referent());
        return t->isRValueReference() ? types_.rvalueReferenceTo(r) : types_.referenceTo(r);
    }
    if (t->isArray())
        return types_.arrayOf(substituteDeduced(t->pointee(), with), t->length());
    return t;
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
        return deduceAutoFrom(declared, il, name, pos);
    }
    // **Read for its type and rewound**, so this reading built nothing: the
    // real one happens below, from the same tokens.
    const Type *from = nullptr;
    {
        Discarded held(this);
        ExprPtr init = assign();
        from = deductionArgType(*init);
    }
    at_ = resume;

    return deduceAutoFrom(declared, from, name, pos);
}

const Type *Parser::deduceAutoFrom(const Type *declared, const Type *from,
                                   const std::string &name, std::size_t pos) {
    std::vector<const Type *> binding(1, static_cast<const Type *>(nullptr));
    std::string why;
    if (!deduceOne(declared, from, &binding, nullptr, &why) || binding[0] == nullptr)
        src_.fail(pos, "'" + name + "' is declared '" + declared->describe() +
                       "' and its initialiser is '" + from->describe() +
                       "', which does not fit: " + why);
    // **What `auto` itself came out as**, which is not the declarator's type:
    // `auto *p = &i` deduces `int` and declares `int *`.
    lastDeducedAuto_ = binding[0];
    return substituteDeduced(declared, binding[0]);
}

// **What a parameter sees of an argument.** [temp.deduct.call]: an array becomes a
// pointer to its first element, a function a pointer to itself, and the top-level
// qualifier goes - which is also just what passing something does.
const Type *Parser::decayedType(const Type *a) const {
    if (a->isReference()) a = a->referent();
    if (a->isArray()) return types_.pointerTo(a->pointee());
    if (a->isFunction()) return types_.pointerTo(a);
    return a->unqualified();
}

// An lvalue argument travels to deduction as `U &`: an expression's type never carries
// its value category, and a forwarding reference is the one parameter that asks for it.
const Type *Parser::deductionArgType(const Expr &e) const {
    const Type *t = e.type();
    if (isLvalue(e) && !t->isReference() && !t->isFunction()) return types_.referenceTo(t);
    return t;
}

// `T &&` with T a bare template parameter, or `auto &&` ([dcl.spec.auto]/7 deduces it the same way).
bool Parser::forwardingReference(const Type *pattern) {
    if (!pattern->isRValueReference()) return false;
    const Type *t = pattern->referent();
    return (t->kind() == Kind::TemplateParam || t->kind() == Kind::Deduced) && t->unqualified() == t;
}

// One parameter of the pattern against one argument's type. The pattern still has
// Kind::TemplateParam in it, so "does this position deduce anything" is a question
// about the type and not about a table: one reached here binds.
bool Parser::deduceOne(const Type *pattern, const Type *arg,
                       std::vector<const Type *> *binding,
                       std::vector<long long> *values,
                       std::string *why, Packs *packs) const {
    // **[temp.deduct.call]/3: `T &&` given an lvalue of type U deduces T as `U &`**, and
    // [dcl.ref]/6 then collapses the parameter to `U &` when the declaration is re-read.
    if (forwardingReference(pattern) && arg->isReference() && !arg->isRValueReference()) {
        const Type *param = pattern->referent();
        const std::size_t i = param->kind() == Kind::Deduced ? 0 : static_cast<std::size_t>(param->length());
        const Type *deduced = types_.referenceTo(arg->referent());
        if ((*binding)[i] == nullptr) { (*binding)[i] = deduced; return true; }
        if ((*binding)[i] != deduced) {
            *why = "it is '" + (*binding)[i]->describe() + "' in one argument "
                   "and '" + deduced->describe() + "' in another";
            return false;
        }
        return true;
    }
    // **A reference parameter looks *through* itself and keeps the argument's
    // qualifier; everything else decays.** `const T &` binding an `int`
    // deduces T as int, and the const on the parameter is not part of T.
    if (pattern->isReference()) {
        pattern = pattern->referent();
        if (arg->isReference()) arg = arg->referent();
        if (pattern->unqualified() != pattern) {
            pattern = pattern->unqualified();
            arg = arg->unqualified();
        }
    } else {
        arg = decayedType(arg);
        if (pattern->unqualified() != pattern) pattern = pattern->unqualified();
    }

    // Kind::Deduced is `auto`, and it is parameter zero of a deduction with
    // one parameter - which is what [dcl.spec.auto] says it is.
    if (pattern->kind() == Kind::TemplateParam ||
        pattern->kind() == Kind::Deduced) {
        const std::size_t i = pattern->kind() == Kind::Deduced
                                  ? 0
                                  : static_cast<std::size_t>(pattern->length());
        const Type *deduced = arg->unqualified();
        if ((*binding)[i] == nullptr) { (*binding)[i] = deduced; return true; }
        if ((*binding)[i] != deduced) {
            *why = "it is '" + (*binding)[i]->describe() + "' in one argument "
                   "and '" + deduced->describe() + "' in another";
            return false;
        }
        return true;
    }

    // **`Holder<T>` against `Holder<int>`.** The two have to be the same
    // template before their arguments mean anything - `Holder<T>` deduces
    // nothing from a `Box<int>`.
    if (pattern->isSpecialization()) {
        int tt;
        // **[temp.deduct.type]/8's `TT<T>`**: a template template parameter deduces from the argument's own template, then the arguments pairwise.
        const bool viaParam = ttRefName(pattern->templateName(), &tt);
        if (!arg->isSpecialization() ||
            (!viaParam && arg->templateName() != pattern->templateName()) ||
            arg->templateArgs().size() != pattern->templateArgs().size()) {
            *why = "'" + arg->describe() + "' is not a '" +
                   (viaParam ? "template's specialization" : pattern->templateName()) + "'";
            return false;
        }
        if (viaParam) {
            const Type *deduced = templateOf(arg);
            const std::size_t i = static_cast<std::size_t>(tt);
            if ((*binding)[i] == nullptr) (*binding)[i] = deduced;
            else if ((*binding)[i] != deduced) {
                *why = "it is '" + (*binding)[i]->describe() + "' in one argument and '" +
                       deduced->describe() + "' in another";
                return false;
            }
        }
        for (std::size_t i = 0; i < pattern->templateArgs().size(); i++) {
            const TemplateArg &p = pattern->templateArgs()[i];
            const TemplateArg &a = arg->templateArgs()[i];
            // **A non-type argument: `V<N>` against `V<3>` gives N=3.** The
            // pattern carries N as a parameter reference; the argument carries a
            // value, and the value is what N is deduced to be.
            if (p.isParam) {
                if (a.isType || a.isParam) {
                    *why = "'" + arg->describe() + "' does not give a value for a "
                           "non-type parameter here";
                    return false;
                }
                const std::size_t j = static_cast<std::size_t>(p.paramIndex);
                if (j < binding->size()) {
                    if ((*binding)[j] != nullptr && values != nullptr &&
                        (*values)[j] != a.value) {
                        *why = "a non-type parameter is deduced two different "
                               "values";
                        return false;
                    }
                    (*binding)[j] = a.type;
                    if (values != nullptr) (*values)[j] = a.value;
                }
                continue;
            }
            if (!p.isType || !a.isType) continue;
            if (p.isTemplate != a.isTemplate || p.isPack != a.isPack) {
                *why = "'" + arg->describe() + "' gives a " +
                       (a.isPack ? "pack" : a.isTemplate ? "template" : "type") +
                       " where the pattern has a " + (p.isPack ? "pack" : p.isTemplate ? "template" : "type");
                return false;
            }
            // `C<int>` for a `template <class...> class C` holds its arguments as one pack, as `Tup<int>` does.
            if (p.isPack) {
                // **`List<Ns...>` against `List<1, 2, 3>`**: the pack takes every member, [temp.deduct.type]/9.
                if (p.expandsParam >= 0) {
                    if (packs == nullptr || static_cast<std::size_t>(p.expandsParam) >= packs->size()) {
                        *why = "a parameter pack cannot be deduced here";
                        return false;
                    }
                    PackMembers &out = (*packs)[static_cast<std::size_t>(p.expandsParam)];
                    if (p.packIsValue != a.packIsValue) {
                        *why = "'" + arg->describe() + "' gives " + (a.packIsValue ? "values" : "types") +
                               " where the pattern's pack takes " + (p.packIsValue ? "values" : "types");
                        return false;
                    }
                    PackMembers got(a.pack);
                    got.values = a.packValues;
                    const bool bound = binding != nullptr && (*binding)[static_cast<std::size_t>(p.expandsParam)] != nullptr;
                    if (bound && (static_cast<const std::vector<const Type *> &>(out) != got ||
                                  out.values != got.values)) {
                        *why = "the parameter pack is deduced two different lists";
                        return false;
                    }
                    out = got;
                    (*binding)[static_cast<std::size_t>(p.expandsParam)] = p.pack[0];
                    continue;
                }
                if (p.pack.size() != a.pack.size()) {
                    *why = "'" + arg->describe() + "' has a different number of arguments";
                    return false;
                }
                if (p.packIsValue) {
                    if (!a.packIsValue) {
                        *why = "'" + arg->describe() + "' gives types where the pattern has values";
                        return false;
                    }
                    for (std::size_t k = 0; k < p.pack.size(); k++) {
                        // `List<M>` in the pattern: M is deduced the member's value, as `V<N>` deduces N.
                        if (p.pack[k]->kind() == Kind::TemplateParam) {
                            const std::size_t j = static_cast<std::size_t>(p.pack[k]->length());
                            if (j >= binding->size()) continue;
                            if ((*binding)[j] != nullptr && values != nullptr && (*values)[j] != a.packValues[k]) {
                                *why = "a non-type parameter is deduced two different values";
                                return false;
                            }
                            (*binding)[j] = a.pack[k];
                            if (values != nullptr) (*values)[j] = a.packValues[k];
                        } else if (p.packValues[k] != a.packValues[k]) {
                            *why = "'" + arg->describe() + "' does not give the pattern's values";
                            return false;
                        }
                    }
                    continue;
                }
                for (std::size_t k = 0; k < p.pack.size(); k++)
                    if (!deduceOne(p.pack[k], a.pack[k], binding, values, why, packs)) return false;
                continue;
            }
            if (!deduceOne(p.type, a.type, binding, values, why, packs)) return false;
        }
        return true;
    }

    if (pattern->isPointer() && arg->isPointer())
        return deduceOne(pattern->pointee(), arg->pointee(), binding, values, why, packs);
    if (pattern->isArray() && arg->isArray()) {
        // **`T (&)[N]` against an `int[3]` gives N=3**, [temp.deduct.type]/8's `type[i]`; the binding
        // marks N deduced and the value is what is read. Every level here: an element does not decay,
        // and recursing would decay `int[4]` in `T (&)[M][N]` to an `int *`.
        while (pattern->isArray() && arg->isArray()) {
            const int j = pattern->boundParam();
            if (j >= 0 && static_cast<std::size_t>(j) < binding->size()) {
                if (arg->length() < 0) {
                    *why = "'" + arg->describe() + "' has no length to give a "
                           "non-type parameter";
                    return false;
                }
                if ((*binding)[j] != nullptr && values != nullptr &&
                    (*values)[j] != arg->length()) {
                    *why = "a non-type parameter is deduced two different values";
                    return false;
                }
                (*binding)[j] = types_.intType();
                if (values != nullptr) (*values)[j] = arg->length();
            }
            pattern = pattern->pointee();
            arg = arg->pointee();
        }
        return pattern->isArray() || arg->isArray() ||
               deduceOne(pattern, arg, binding, values, why, packs);
    }
    // `T S::*` against `int S::*`: the member's type, and the class when it is
    // a parameter too.
    if (pattern->isMemberPointer() && arg->isMemberPointer())
        return deduceOne(pattern->pointee(), arg->pointee(), binding, values, why, packs) &&
               deduceOne(pattern->enclosing(), arg->enclosing(), binding, values, why, packs);

    // Nothing to deduce here. A parameter written out in full does not have to match
    // exactly - an ordinary conversion may still get the argument there - so this is
    // not where a mismatch is reported; overload resolution ranks it after.
    return true;
}

// Every base of `cls` that is a specialization of `name`, at any depth - of any template for a pattern's `C<T>`.
static void basesNamed(const Type *cls, const std::string &name, std::vector<const Type *> *out) {
    int tt;
    for (const Type::BaseSpec &b : cls->unqualified()->bases()) {
        const Type *t = b.type->unqualified();
        if (t->isSpecialization() && (t->templateName() == name || ttRefName(name, &tt))) out->push_back(t);
        basesNamed(t, name, out);
    }
}

// **[temp.deduct.call]/4.3: a parameter `V<K>`, `V<K> &` or `V<K> *` deduces from a base**
// of the argument's class when the class itself is no `V` - `q * r` for `Q<4> : V<4>` gives
// K = 4. Only at the parameter's top, and only where exactly one base deduces.
bool Parser::deduceFromArgument(const Type *param, const Type *arg,
                                std::vector<const Type *> *binding,
                                std::vector<long long> *values, std::string *why, Packs *packs) {
    const std::vector<const Type *> before = *binding;
    const std::vector<long long> beforeValues = values ? *values : std::vector<long long>();
    const Packs beforePacks = packs ? *packs : Packs();
    if (deduceOne(param, arg, binding, values, why, packs)) return true;
    if (packs) *packs = beforePacks;
    const bool viaPointer = param->isPointer() && arg != nullptr && arg->isPointer();
    const Type *p = viaPointer ? param->pointee() : param->isReference() ? param->referent() : param;
    const Type *a = viaPointer ? arg->pointee() : arg != nullptr && arg->isReference() ? arg->referent() : arg;
    if (p == nullptr || a == nullptr) return false;
    p = p->unqualified();
    if (!p->isSpecialization() || !a->unqualified()->isStructOrUnion()) return false;
    std::vector<const Type *> bases;
    basesNamed(a, p->templateName(), &bases);
    int found = 0;
    std::vector<const Type *> got;
    std::vector<long long> gotValues;
    Packs gotPacks;
    for (const Type *b : bases) {
        std::vector<const Type *> tryBinding = before;
        std::vector<long long> tryValues = beforeValues;
        Packs tryPacks = beforePacks;
        std::string ignored;
        const Type *as = viaPointer ? types_.pointerTo(b) : b;
        if (!deduceOne(p, viaPointer ? b : as, &tryBinding, values ? &tryValues : nullptr, &ignored,
                       packs ? &tryPacks : nullptr))
            continue;
        if (++found == 1) { got = tryBinding; gotValues = tryValues; gotPacks = tryPacks; }
    }
    if (found != 1) return false;
    *binding = got;
    if (values) *values = gotValues;
    if (packs) *packs = gotPacks;
    return true;
}

// The whole call. Answers false with a reason rather than failing, because a
// name may be both a template and an ordinary function: deduction not
// working is then not an error, it is one fewer candidate.
bool Parser::deduceTemplateArguments(const TemplateDecl &decl,
                                     const std::vector<const Type *> &argTypes,
                                     std::vector<const Type *> *binding,
                                     std::vector<long long> *values,
                                     Packs *packs,
                                     std::string *why) {
    packs->assign(decl.params.size(), std::vector<const Type *>());
    values->assign(decl.params.size(), 0);

    std::vector<const Type *> pattern(decl.params.size());
    for (std::size_t i = 0; i < decl.params.size(); i++)
        pattern[i] = types_.templateParam(static_cast<int>(i));
    const std::vector<long long> none(decl.params.size(), 0);
    std::string ignored;
    const Type *fn = readTemplateDeclaration(decl, pattern, none, &ignored);

    // **A trailing function parameter pack takes every argument the written parameters leave.** It is
    // `Ts... rest`, the pattern's last parameter an expansion - a pack named only inside a parameter's
    // type, `List<Ns...>`, deduces through it - and may be none, so this is `<` and not `!=` on the count.
    const bool hasPack = !decl.params.empty() && decl.params.back().isPack &&
                         !fn->params().empty() && fn->params().back()->kind() == Kind::PackExpansion;
    const std::size_t fixed = hasPack ? fn->params().size() - 1
                                      : fn->params().size();
    // **A parameter with a default needs no argument.** readTemplateDeclaration above has just read
    // the pattern's parameter list, so pendingDefaults_ says which of them have one - and they are
    // a suffix, [dcl.fct.default]/4.
    std::size_t least = fixed;
    for (std::size_t i = 0; i < pendingDefaults_.size() && i < fixed; i++)
        if (pendingDefaults_[i] != 0) { least = i; break; }
    if (hasPack ? argTypes.size() < fixed
                : (argTypes.size() < least || argTypes.size() > fixed)) {
        *why = "it takes " +
               std::string(hasPack || least < fixed ? "at least " : "") +
               std::to_string(hasPack ? fixed : least) +
               " argument(s) and this call gives " +
               std::to_string(argTypes.size());
        return false;
    }
    // Only the arguments there are can be deduced from; the rest are the
    // defaults, filled in by applyDefaults once a candidate is chosen.
    const std::size_t deduceFrom = argTypes.size() < fixed ? argTypes.size()
                                                           : fixed;

    binding->assign(decl.params.size(), nullptr);
    for (std::size_t i = 0; i < deduceFrom; i++)
        if (!deduceFromArgument(fn->params()[i], argTypes[i], binding, values, why, packs)) {
            *why = "'" + decl.params[i < decl.params.size() ? i : 0].name +
                   "' cannot be worked out from this call: " + *why;
            return false;
        }
    if (hasPack) {
        std::vector<const Type *> members;
        // Each member deduced as one parameter of the expansion's shape would be: `A &&...` forwards.
        const Type *shape = fn->params().back()->kind() == Kind::PackExpansion ? fn->params().back()->pointee() : nullptr;
        for (std::size_t i = fixed; i < argTypes.size(); i++) {
            const Type *a = argTypes[i];
            if (shape != nullptr && forwardingReference(shape) && a->isReference()) members.push_back(a);
            else if (shape != nullptr && shape->isReference())
                members.push_back(shape->referent()->unqualified() != shape->referent()
                                      ? (a->isReference() ? a->referent() : a)->unqualified()
                                      : (a->isReference() ? a->referent() : a));
            else members.push_back(decayedType(a->isReference() ? a->referent() : a));
        }
        (*packs)[decl.params.size() - 1] = members;
    }
    for (std::size_t i = 0; i < binding->size(); i++) {
        // A pack deduced from an expansion was marked in `binding` by its own parameter; the function
        // parameter pack binds through `packs` alone. Neither is a type to leave in the binding.
        if (decl.params[i].isPack) { (*binding)[i] = nullptr; continue; }
        if ((*binding)[i] == nullptr && !defaultTemplateArgument(decl, i, binding, values)) {
            *why = "'" + decl.params[i].name + "' appears in no parameter, so "
                   "there is nothing in the call to work it out from - write "
                   "the arguments out";
            return false;
        }
    }
    return true;
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
    bindTemplateParameters(bound, types, values, Packs(bound.size()), undo);
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
            ~Unbind() { p->unbindTemplateParameters(*u); }
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
            ~Unbind() { p->unbindTemplateParameters(*u); }
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
            ~Unbind() { p->unbindTemplateParameters(*u); }
        } unbind{ this, &undo };
        bindPatternMember(from, end, m, &undo);
        const std::size_t argPos = peek().pos;
        const long long v = constantExpression("a template argument");
        if (peek().kind != TokenKind::End)
            src_.fail(peek().pos, "this pack expansion's pattern is not one expression");
        checkNonTypeArgument(a.type, v, argPos);
        a.pack.push_back(a.type);
        a.packValues.push_back(v);
    }
    at_ = end;
    return true;
}

// [temp.arg.nontype]/1: a converted constant expression, which may not narrow. An 8-byte type is
// left alone: the value's own signedness is not kept here.
void Parser::checkNonTypeArgument(const Type *pt, long long v, std::size_t argPos) const {
    if (!(pt->isInteger() && pt->tag().empty() && pt->size(target_) < 8)) return;
    const int bits = pt->kind() == Kind::Bool ? 1 : 8 * pt->size(target_);
    const bool sgn = pt->kind() != Kind::Bool && pt->isSigned(target_);
    const long long lo = sgn ? -(1LL << (bits - 1)) : 0;
    const long long hi = sgn ? (1LL << (bits - 1)) - 1 : (1LL << bits) - 1;
    if (v < lo || v > hi)
        src_.fail(argPos, "the template argument is " + std::to_string(v) +
                          ", which cannot be narrowed to '" + pt->describe() + "'");
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
        ~Unbind() { p->unbindTemplateParameters(*u); }
    } unbind{ this, &undo };
    const Packs nopacks(1, std::vector<const Type *>());
    for (std::size_t k = 0; k < i; k++) {
        if ((*binding)[k] == nullptr) return false;
        std::vector<TemplateParam> one(1, decl.params[k]);
        std::vector<const Type *> ob(1, (*binding)[k]);
        std::vector<long long> ov(1, (*values)[k]);
        bindTemplateParameters(one, ob, ov, nopacks, &undo);
    }
    const std::size_t saved = at_;
    struct InArgs {
        bool &f; bool was;
        ~InArgs() { f = was; }
    } inArgs{ inTemplateArgs_, inTemplateArgs_ };
    inTemplateArgs_ = true;                    // the `>` after `N = 3` closes the list
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

// Whether a type holds a pattern's `R...` anywhere in a template-id's arguments.
bool Parser::mentionsPackExpansion(const Type *t) {
    if (t == nullptr) return false;
    if (t->unqualified() != t) return mentionsPackExpansion(t->unqualified());
    if (t->isPointer() || t->isArray()) return mentionsPackExpansion(t->pointee());
    if (t->isReference()) return mentionsPackExpansion(t->referent());
    if (!t->isSpecialization()) return false;
    for (std::size_t k = 0; k < t->templateArgs().size(); k++) {
        const TemplateArg &a = t->templateArgs()[k];
        if (a.expandsParam >= 0) return true;
        if (a.isPack) {
            for (std::size_t m = 0; m < a.pack.size(); m++)
                if (mentionsPackExpansion(a.pack[m])) return true;
        } else if (a.isType && mentionsPackExpansion(a.type)) {
            return true;
        }
    }
    return false;
}

// Whether parameter `i` appears anywhere in a pattern. One a specialization never
// mentions could not be worked out from any argument list, so it could never be
// chosen - worth refusing where it is written rather than leaving it silent.
static bool mentionsParam(const Type *t, std::size_t i) {
    if (t == nullptr) return false;
    if (t->unqualified() != t) return mentionsParam(t->unqualified(), i);
    if (t->kind() == Kind::TemplateParam)
        return static_cast<std::size_t>(t->length()) == i;
    if (t->isPointer() || t->isArray()) return mentionsParam(t->pointee(), i);
    if (t->isReference()) return mentionsParam(t->referent(), i);
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

// **[temp.param]/11's second sentence**: a function template's pack may be followed by a
// parameter the call can work out - one a function parameter names - or one with a default.
void Parser::requireDeducibleAfterPack(const TemplateDecl &decl, const Type *fn) {
    const std::size_t from = afterPack(decl.params);
    if (from == static_cast<std::size_t>(-1)) return;
    for (std::size_t i = from; i < decl.params.size(); i++) {
        if (decl.params[i].defBegin != 0 || decl.params[i].isPack) continue;
        bool named = false;
        if (fn != nullptr && fn->kind() == Kind::Function)
            for (std::size_t k = 0; k < fn->params().size() && !named; k++)
                named = mentionsParam(fn->params()[k], i);
        if (!named)
            src_.fail(decl.params[i].pos, "'" + decl.params[i].name + "' follows the parameter pack '" +
                                          decl.params[from - 1].name + "' and no function parameter "
                                          "names it, so a call could never work it out - a parameter "
                                          "after a pack has to be deducible or defaulted");
    }
}

Parser::Trial::Trial(Parser *parser)
    : p(parser), at(parser->at_), classes(parser->classStack_.size()),
      pattern(parser->patternOnly_), split(parser->angleSplit_) {
    p->src_.beginTrial();
}

Parser::Trial::~Trial() {
    p->src_.endTrial();
    p->at_ = at;
    p->classStack_.resize(classes);
    p->patternOnly_ = pattern;
    p->angleSplit_ = split;
}

// [temp.deduct.type]. A pattern that is a pointer matches a pointer and
// nothing else - there is no conversion here for a mismatch to be forgiven
// by, which is what makes this stricter than deduction from a call.
bool Parser::matchPattern(const Type *pattern, const Type *arg,
                          std::vector<const Type *> *binding,
                          std::string *why) const {
    // **The qualifier is asked about before anything else, and both sides must
    // agree.** `Box<const T>` matches `Box<const int>` and not `Box<int>`; `Box<T>`
    // matches both, binding T to the qualified type where there is one.
    if (pattern->unqualified() != pattern) {
        if (arg->unqualified() == arg) {
            *why = "'" + arg->describe() + "' is not const";
            return false;
        }
        return matchPattern(pattern->unqualified(), arg->unqualified(),
                            binding, why);
    }

    if (pattern->kind() == Kind::TemplateParam) {
        const std::size_t i = static_cast<std::size_t>(pattern->length());
        if ((*binding)[i] == nullptr) { (*binding)[i] = arg; return true; }
        if ((*binding)[i] != arg) {
            *why = "it is '" + (*binding)[i]->describe() + "' in one place and '" +
                   arg->describe() + "' in another";
            return false;
        }
        return true;
    }

    if (pattern->isPointer())
        return arg->isPointer() &&
               matchPattern(pattern->pointee(), arg->pointee(), binding, why);
    // `T &` and `T &&` are two patterns: `remove_reference<T &&>` must not take an `int &`.
    if (pattern->isReference())
        return arg->isReference() && arg->isRValueReference() == pattern->isRValueReference() &&
               matchPattern(pattern->referent(), arg->referent(), binding, why);
    if (pattern->isArray())
        return arg->isArray() && pattern->length() == arg->length() &&
               matchPattern(pattern->pointee(), arg->pointee(), binding, why);
    if (pattern->isMemberPointer())
        return arg->isMemberPointer() &&
               matchPattern(pattern->pointee(), arg->pointee(), binding, why) &&
               matchPattern(pattern->enclosing(), arg->enclosing(), binding, why);

    if (pattern->isSpecialization()) {
        // `S<C<int>>` against `S<Box<int>>`: the template itself is what C is bound to.
        int tt;
        const bool viaParam = ttRefName(pattern->templateName(), &tt);
        if (!arg->isSpecialization() ||
            (!viaParam && arg->templateName() != pattern->templateName()) ||
            arg->templateArgs().size() != pattern->templateArgs().size())
            return false;
        if (viaParam) {
            const Type *deduced = templateOf(arg);
            const std::size_t i = static_cast<std::size_t>(tt);
            if ((*binding)[i] == nullptr) (*binding)[i] = deduced;
            else if ((*binding)[i] != deduced) return false;
        }
        for (std::size_t i = 0; i < pattern->templateArgs().size(); i++) {
            const TemplateArg &p = pattern->templateArgs()[i];
            const TemplateArg &a = arg->templateArgs()[i];
            if (p.isType != a.isType || p.isTemplate != a.isTemplate || p.isPack != a.isPack) return false;
            if (p.isPack) {
                if (p.expandsParam >= 0 || p.pack.size() != a.pack.size() || p.packIsValue != a.packIsValue)
                    return false;
                if (p.packIsValue) {
                    for (std::size_t k = 0; k < p.pack.size(); k++)
                        if (p.pack[k]->kind() != Kind::TemplateParam && p.packValues[k] != a.packValues[k])
                            return false;
                    continue;
                }
                for (std::size_t k = 0; k < p.pack.size(); k++)
                    if (!matchPattern(p.pack[k], a.pack[k], binding, why)) return false;
                continue;
            }
            if (!p.isType) {
                // Two expressions are one argument only as one interned node: [temp.over.link]/5.
                if (p.expr != nullptr || a.expr != nullptr) {
                    if (p.expr != a.expr) return false;
                    continue;
                }
                if (p.value != a.value) return false;
                continue;
            }
            if (!matchPattern(p.type, a.type, binding, why)) return false;
        }
        return true;
    }

    if (pattern != arg) {
        *why = "'" + arg->describe() + "' is not '" + pattern->describe() + "'";
        return false;
    }
    return true;
}

// `Box<T *>` - read with this specialization's own parameters bound to
// themselves, so what comes out is a pattern rather than a type.
void Parser::partialArguments(TemplateDecl::Partial *ps,
                              const std::vector<TemplateParam> &primary) {
    const std::size_t count = primary.size();
    // **A variadic primary is not written a fixed number of arguments**: a pack
    // stands for a list, so the closing angle says where the pattern stops and not
    // the parameter count - which gave "more arguments than parameters" before.
    const bool variadic = !primary.empty() && primary.back().isPack;
    expect("<");
    const bool wasInArgs = inTemplateArgs_;
    const bool wasPattern = patternOnly_;
    inTemplateArgs_ = true;
    patternOnly_ = true;

    std::vector<Shadow> undo;
    std::vector<const Type *> binding(ps->params.size());
    std::vector<long long> values(ps->params.size(), 1);
    for (std::size_t i = 0; i < ps->params.size(); i++)
        binding[i] = ps->params[i].type == nullptr || ps->params[i].isPack
                         ? types_.templateParam(static_cast<int>(i))
                         : ps->params[i].type;
    bindTemplateParameters(ps->params, binding, values,
                           Packs(), &undo);

    for (std::size_t i = 0; variadic ? !atClosingAngle() : i < count; i++) {
        if (i > 0) expect(",");
        TemplateDecl::Partial::Arg a;

        // `R...` - this specialization's own pack, standing for everything
        // the fixed arguments before it did not take.
        if (peek().kind == TokenKind::Ident && peekAt(1).is("...")) {
            std::size_t k = ps->params.size();
            for (std::size_t j = 0; j < ps->params.size(); j++)
                if (ps->params[j].isPack && ps->params[j].name == peek().text)
                    k = j;
            if (k < ps->params.size()) {
                a.isType = true;
                a.isPackExpansion = true;
                a.param = k;
                at_ += 2;
                ps->args.push_back(a);
                continue;
            }
        }
        // **A non-type argument that is one of our own parameters is the only
        // shape of one that deduces**, so it is recognised by its tokens
        // before it can be folded into the value it was bound to.
        std::size_t which = ps->params.size();
        if (peek().kind == TokenKind::Ident)
            for (std::size_t k = 0; k < ps->params.size(); k++)
                if (ps->params[k].type != nullptr &&
                    ps->params[k].name == peek().text) which = k;
        if (which < ps->params.size() &&
            (peekAt(1).is(",") || peekAt(1).is(">") || peekAt(1).is(">>"))) {
            a.isType = false;
            a.isParam = true;
            a.param = which;
            at_++;
        } else if (atTypeName()) {
            const std::size_t tpos = peek().pos;
            StorageClass sc;
            Qualifiers quals;
            const Type *base = specifiers(&sc, &quals);
            if (peek().is("(") && !peekAt(1).is("*") && !peekAt(1).is("&") && !peekAt(1).is("&&"))
                src_.fail(peek().pos, "a function type as a template argument, 'R(Args...)', is not "
                                      "supported yet - a pointer to one, 'R (*)(Args...)', is read");
            Declared d = declarator(base, true);
            a.isType = true;
            a.type = d.type;
            // `Box<R...>` inside a partial's argument: matchPattern binds types, and a pack is a list.
            if (mentionsPackExpansion(d.type))
                src_.fail(tpos, "a pack expanded inside a nested template-id in a partial specialization's "
                                "arguments is not supported yet - 'R...' as an argument of its own is");
        } else {
            a.isType = false;
            a.value = constantExpression("a template argument");
        }
        ps->args.push_back(a);
    }
    if (!atClosingAngle())
        src_.fail(peek().pos, "this specialization gives more arguments than "
                              "the template has parameters");
    takeClosingAngle();
    // A pack expansion may only be last: everything after it could never be
    // told apart from a member of it. Same rule, and the same reason, as a
    // pack having to be the last *parameter*.
    for (std::size_t i = 0; i + 1 < ps->args.size(); i++)
        if (ps->args[i].isPackExpansion)
            src_.fail(ps->params.empty() ? 0 : ps->params[0].pos,
                      "a pack expansion has to be the last argument of a "
                      "specialization - anything after it could never be told "
                      "apart from one of its members");

    unbindTemplateParameters(undo);
    inTemplateArgs_ = wasInArgs;
    patternOnly_ = wasPattern;
}

// [temp.class.order], asked the standard's own way: A is at least as specialized as
// B when B's pattern matches A's. A's parameters stand as opaque types while that
// happens, which is exactly what Kind::TemplateParam already is.
bool Parser::atLeastAsSpecialized(const TemplateDecl::Partial &a,
                                  const TemplateDecl::Partial &b) const {
    std::vector<const Type *> binding(b.params.size());
    std::string why;
    for (std::size_t i = 0; i < a.args.size() && i < b.args.size(); i++) {
        if (a.args[i].isType != b.args[i].isType) return false;
        if (!a.args[i].isType) {
            if (b.args[i].isParam) continue;      // a parameter takes anything
            if (a.args[i].isParam) return false;
            if (a.args[i].value != b.args[i].value) return false;
            continue;
        }
        if (!matchPattern(b.args[i].type, a.args[i].type, &binding, &why))
            return false;
    }
    return true;
}

bool Parser::moreSpecialized(const TemplateDecl::Partial &a,
                             const TemplateDecl::Partial &b) const {
    return atLeastAsSpecialized(a, b) && !atLeastAsSpecialized(b, a);
}

// Which partial specialization these arguments ask for.
std::size_t Parser::choosePartial(const TemplateDecl &decl,
                                  const std::vector<TemplateArg> &args,
                                  std::vector<const Type *> *binding,
                                  std::vector<long long> *values,
                                  Packs *packs,
                                  std::size_t pos) {
    std::vector<std::size_t> fits;
    Packs bindings;
    std::vector<std::vector<long long> > valueSets;
    std::vector<Packs > packSets;

    // **The arguments arrive as one pack when the primary is variadic.**
    // `L<int, char>` against `template <class... Ts>` is a single TemplateArg
    // holding both, so the list is flattened here and every pattern matched on it.
    std::vector<TemplateArg> flat;
    for (std::size_t i = 0; i < args.size(); i++) {
        if (!args[i].isPack) { flat.push_back(args[i]); continue; }
        for (std::size_t k = 0; k < args[i].pack.size(); k++) {
            TemplateArg one;
            one.isType = !args[i].packIsValue;
            one.type = args[i].pack[k];
            if (args[i].packIsValue) one.value = args[i].packValues[k];
            flat.push_back(one);
        }
    }

    for (std::size_t p = 0; p < decl.partials.size(); p++) {
        const TemplateDecl::Partial &ps = decl.partials[p];

        // A trailing `R...` takes everything the fixed arguments leave, so
        // the arity it demands is a minimum rather than an equality.
        const bool takesRest = !ps.args.empty() &&
                               ps.args.back().isPackExpansion;
        const std::size_t fixed = takesRest ? ps.args.size() - 1
                                            : ps.args.size();
        if (takesRest ? flat.size() < fixed : ps.args.size() != flat.size())
            continue;

        std::vector<const Type *> b(ps.params.size());
        std::vector<long long> v(ps.params.size(), 0);
        Packs pk(ps.params.size());
        std::string why;
        bool ok = true;
        for (std::size_t i = 0; i < fixed && ok; i++) {
            const TemplateDecl::Partial::Arg &a = ps.args[i];
            if (a.isType != flat[i].isType) { ok = false; break; }
            if (!a.isType) {
                if (a.isParam) v[a.param] = flat[i].value;
                else if (a.value != flat[i].value) ok = false;
                continue;
            }
            if (!matchPattern(a.type, flat[i].type, &b, &why)) ok = false;
        }
        if (ok && takesRest) {
            const TemplateDecl::Partial::Arg &tail = ps.args.back();
            const bool wantsValues = ps.params[tail.param].type != nullptr;
            for (std::size_t i = fixed; i < flat.size() && ok; i++) {
                if (flat[i].isType == wantsValues) { ok = false; break; }
                pk[tail.param].push_back(flat[i].type);
                if (wantsValues) pk[tail.param].values.push_back(flat[i].value);
            }
        }
        // A pack parameter binds through `pk` and never through `b`, so it is
        // not the unbound-parameter fault this is looking for.
        for (std::size_t i = 0; ok && i < ps.params.size(); i++)
            if (ps.params[i].type == nullptr && !ps.params[i].isPack &&
                b[i] == nullptr) ok = false;
        if (!ok) continue;
        fits.push_back(p);
        bindings.push_back(b);
        valueSets.push_back(v);
        packSets.push_back(pk);
    }

    if (fits.empty()) return static_cast<std::size_t>(-1);

    // **One has to beat every other, and "not beaten" is not the same as "beats".**
    // `P<A, int>` and `P<int, B>` given `P<int, int>`: neither matches the other, so
    // the program is ambiguous rather than settled by whichever came first.
    std::size_t best = 0;
    for (std::size_t k = 1; k < fits.size(); k++)
        if (moreSpecialized(decl.partials[fits[k]], decl.partials[fits[best]]))
            best = k;
    for (std::size_t k = 0; k < fits.size(); k++)
        if (k != best &&
            !moreSpecialized(decl.partials[fits[best]], decl.partials[fits[k]]))
            src_.fail(pos, "'" + decl.name + "' has two partial "
                           "specializations that fit these arguments and "
                           "neither is more specialized than the other");

    *binding = bindings[best];
    *values = valueSets[best];
    if (packs != nullptr) *packs = packSets[best];
    return fits[best];
}

// `Box<int, 3>` where a type was expected - rung 5.4. The class is made by replaying
// `struct Box { ... };` with the arguments bound, and all the class path had to be
// told is what tag to take: nested classes had made tag() an arbitrary string.
const Type *Parser::instantiateClass(const TemplateDecl &decl, std::size_t pos) {
    std::vector<const Type *> binding;
    std::vector<long long> values;
    std::vector<TemplateArg> args;
    Packs packs;
    templateArguments(decl, &binding, &values, &args, &packs);

    // [temp.alias]/2: an alias template's id is its type-id with the arguments substituted.
    if (decl.isAlias) {
        const std::size_t resume = at_;
        std::vector<Shadow> undo;
        struct Unbind {
            Parser *p; std::vector<Shadow> *u;
            ~Unbind() { p->unbindTemplateParameters(*u); }
        } unbind{ this, &undo };
        bindTemplateParameters(decl.params, binding, values, packs, &undo);
        at_ = decl.aliasAt;
        StorageClass sc;
        Qualifiers quals;
        const Type *base = specifiers(&sc, &quals);
        const Type *t = declarator(base, true).type;
        at_ = resume;
        return t;
    }
    const std::string tag = specializationKey(decl.name, args);

    // **A pattern's `C<T>`**, the stand-in applied: a shape deduction matches and Itanium spells `T_I...E`.
    if (decl.ttParam >= 0) {
        Type *shallow = types_.structType(Kind::Struct, tag);
        if (!shallow->isSpecialization()) shallow->setSpecialization(decl.name, args);
        declareTypeName(tag, shallow);
        return shallow;
    }

    // Reading a pattern, not building a class: `Holder<T>` cannot be laid out, and the mangler
    // and deduction read only its name and arguments. An argument list that mentions no
    // parameter is a real class - `enable_if<2 + 2 == 4, int>`.
    if (patternOnly_ && dependentArgs(args)) {
        Type *shallow = types_.structType(Kind::Struct, tag);
        if (!shallow->isSpecialization()) {
            shallow->setSpecialization(decl.name, args);
            shallow->setTemplateNamespace(decl.ns);
        }
        // Registered so that `Box<T>::get` reads: the declarator's qualified
        // path looks the class up by name, and this is the only name it has.
        // The tag holds a `$` and so cannot collide with anything written.
        declareTypeName(tag, shallow);
        return shallow;
    }

    if (const Type *had = findTypedef(tag)) return had;

    // **A partial specialization is chosen before anything is replayed**, and what
    // it changes is which tokens get replayed and with which parameters bound. The
    // tag does not change, which keeps the mangling and every lookup the same.
    std::vector<const Type *> useBinding = binding;
    std::vector<long long> useValues = values;
    std::vector<TemplateParam> useParams = decl.params;
    Packs usePacks;
    const std::size_t which = choosePartial(decl, args, &useBinding, &useValues,
                                            &usePacks, pos);
    const bool partial = which != static_cast<std::size_t>(-1);
    if (partial) useParams = decl.partials[which].params;

    // **Asked after the partial is chosen, not before.**
    if (!partial && !decl.defined) {
        Type *shallow = types_.structType(Kind::Struct, tag);
        if (!shallow->isSpecialization()) {
            shallow->setSpecialization(decl.name, args);
            shallow->setTemplateNamespace(decl.ns);
            shallow->noteClassKey(decl.classKey);
        }
        return shallow;
    }

    const std::size_t resume = at_;
    std::vector<Shadow> undo;
    // **A chosen partial binds its own pack, not the primary's.** `L<T, R...>` on
    // `L<int, char, long>` leaves R holding {char, long}, which is what the replayed
    // body has to see - the primary's Ts is not in scope at all.
    bindTemplateParameters(useParams, useBinding, useValues,
                           partial ? usePacks : packs, &undo);

    at_ = partial ? decl.partials[which].bodyAt : decl.afterParams;
    // **A specialization is not a member of whatever class asked for it.**
    std::vector<const Type *> outerClasses;
    outerClasses.swap(classStack_);
    const Type *outerCurrent = currentClass_;
    currentClass_ = nullptr;
    const std::string outerInline = inlineOwner_;
    inlineOwner_.clear();
    // **The `>>` mark is one slot and this parse can spend it.**
    const std::size_t outerAngle = angleSplit_;

    // **The class being read around this one keeps its own**: a member template recorded after
    // `Vec<N>` in Mat's body would otherwise capture Vec's parameters as Mat's.
    const std::vector<TemplateArg> outerArgs = instantiatingArgs_;
    const std::string outerNamespace = instantiatingNamespace_;
    const auto outerParams = instantiatingParams_;
    const auto outerBinding = instantiatingBinding_;
    const auto outerValues = instantiatingValues_;
    classInstantiationTag_ = tag;
    if (partial) classInstantiationOf_ = decl.name;
    instantiatingArgs_ = args;
    instantiatingNamespace_ = decl.ns;
    instantiatingParams_ = useParams;
    instantiatingBinding_ = useBinding;
    instantiatingValues_ = useValues;
    heldForSpecialization_.clear();
    const bool wasDeferring = deferSpecializationBodies_;
    deferSpecializationBodies_ = true;
    StorageClass sc;
    Qualifiers quals;
    const Type *made = partial
        ? structOrUnionSpecifier(Kind::Struct, false)
        : specifiers(&sc, &quals);
    angleSplit_ = outerAngle;
    classInstantiationTag_.clear();
    classInstantiationOf_.clear();
    instantiatingArgs_ = outerArgs;
    instantiatingNamespace_ = outerNamespace;
    instantiatingParams_ = outerParams;
    instantiatingBinding_ = outerBinding;
    instantiatingValues_ = outerValues;
    classStack_.swap(outerClasses);
    currentClass_ = outerCurrent;
    inlineOwner_ = outerInline;
    deferSpecializationBodies_ = wasDeferring;
    std::vector<PendingBody> bodies;
    bodies.swap(heldForSpecialization_);

    unbindTemplateParameters(undo);
    at_ = resume;

    if (!made->isStructOrUnion())
        src_.fail(pos, "'" + decl.name + "' is not a class template");
    declareTypeName(tag, made);

    Specialization sp;
    sp.key = tag;
    sp.name = decl.name;
    sp.fromPartial = partial;
    sp.params = useParams;
    sp.binding = useBinding;
    sp.values = useValues;
    // **A partial's pack has to be recorded too.** Held bodies are replayed later
    // from this record, and one saying `Tuple<Rest...>` needs Rest as it was. Left
    // empty, `Tuple<char,long>::tail` returned `Tuple<long> &` and said `Tuple<> &`.
    sp.packs = partial ? usePacks : packs;
    sp.start = decl.afterParams;
    sp.pos = pos;
    sp.isClass = true;
    sp.bodies = bodies;
    specializations_.push_back(sp);
    return made;
}

// A template named in an expression. 5.2 wants the arguments written out:
// deducing them from the call is 5.3, and a class template is 5.4.
ExprPtr Parser::templateCall(Program *program) {
    const std::string name = peek().text;
    const std::size_t pos = peek().pos;
    const TemplateDecl decl = templates_[name];
    if (decl.isClass) {
        // **`vector<int>()` is a temporary, not an instantiation this cannot
        // do.**
        const std::size_t save = at_;
        at_++;
        if (peek().is("<")) {
            const Type *cls = instantiateClass(decl, pos);
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
            if (const Type *self = findTypedef(name))
                if (self->isStructOrUnion()) {
                    at_++;
                    return classTemporary(self, pos);
                }
        }
        // **And before `::`**: `Tree::next(n)` inside Tree<T>'s members, its nested and derived
        // classes is the road `Tree<T>::next(n)` takes - [temp.local]/1. At file scope nothing
        // answers for the bare name and the refusal below stands.
        if (peek().is("::")) {
            if (const Type *self = findTypedef(name))
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
            argTypes.push_back(deductionArgType(*callArgs[i]));

        // **Every function template of this name is a candidate.**
        instantiateViableTemplates(name, argTypes, pos);

        // Nothing deduced and no ordinary function - so say why the template
        // failed, which beats "not declared". With more than one overload the
        // first is representative enough for the message.
        if (overloadsOf(name) == nullptr) {
            std::vector<const Type *> b;
            std::vector<long long> v;
            Packs p;
            std::string why;
            deduceTemplateArguments(decl, argTypes, &b, &v, &p, &why);
            // Deduction can succeed and the instantiation still fail, and then
            // there is no reason to print - so say that rather than end the
            // sentence on "and".
            if (why.empty())
                why = "no specialization of it could be made for these "
                      "arguments";
            src_.fail(pos, "'" + name + "' is a function template and " + why);
        }

        // **In the template's own namespace**, whose ordinary functions of this name are candidates
        // beside its specializations - [over.match.best]: an exact non-template beats a template.
        // Under the bare name a qualified call or a class template's member body saw only the specializations.
        const std::string key = !decl.ns.empty() && overloadsOf(decl.ns + name) != nullptr
                              ? decl.ns + name : name;
        const Signature sig = resolveOverload(key, callArgs, pos);
        // **A specialization's defaults, as every ordinary call reads them.**
        applyDefaults(sig, callArgs, pos);
        return completeCall(sig.name, sig.symbol, nullptr, sig.returns,
                            sig.params, sig.variadic, pos, std::move(callArgs));
    }

    // **Every function template of this name is a candidate here too.** The written
    // list is read once per overload - `forward<T>` has one on `type &` and one on
    // `type &&` - and what each makes is ranked against the call's arguments below.
    std::vector<TemplateDecl> overloads;
    if (fnTemplates_.count(name) != 0) overloads = fnTemplates_[name];
    if (overloads.empty()) overloads.push_back(decl);
    const std::size_t listAt = at_;
    std::size_t afterList = 0;
    std::vector<std::size_t> made;
    (void)program;
    for (std::size_t k = 0; k < overloads.size(); k++) {
        std::vector<const Type *> binding;
        std::vector<long long> values;
        std::vector<TemplateArg> args;
        Packs packs;
        const bool wasInArgs = inTemplateArgs_;
        std::string symbol;
        at_ = listAt;
        try {
            Trial trial(this);
            templateArguments(overloads[k], &binding, &values, &args, &packs);
            afterList = at_;
            symbol = instantiate(overloads[k], binding, values, args, pos, packs).symbol;
        } catch (const SubstitutionFailure &f) {
            inTemplateArgs_ = wasInArgs;
            if (f.unsupported) src_.fail(f.pos, f.why);
            continue;
        }
        for (std::size_t i = 0; i < functions_.size(); i++)
            if (functions_[i].symbol == symbol) { made.push_back(i); break; }
    }
    // Nothing applied: read the one template outside the trial, so its own diagnostic is printed.
    if (made.empty()) {
        at_ = listAt;
        std::vector<const Type *> binding;
        std::vector<long long> values;
        std::vector<TemplateArg> args;
        Packs packs;
        templateArguments(decl, &binding, &values, &args, &packs);
        instantiate(decl, binding, values, args, pos, packs);
        src_.fail(pos, "'" + name + "' has no specialization for these template arguments");
    }
    at_ = afterList;
    if (!peek().is("("))
        src_.fail(peek().pos, "'" + name + "' is a function template, and "
                              "naming one without calling it is not supported "
                              "yet");
    at_++;
    std::vector<ExprPtr> callArgs;
    parseArguments(callArgs);

    // One candidate is the answer; several are ranked under a key of their own, so nothing
    // else of this name joins. **A copy, not a reference**: parsing can move `functions_`.
    Signature sig = functions_[made[0]];
    if (made.size() > 1) {
        const std::string key = "$explicit:" + name;
        functionIndex_[key] = made;
        sig = resolveOverload(key, callArgs, pos);
        functionIndex_.erase(key);
    }
    // Written out rather than deduced, so no ranking chose it and nothing else
    // will mark it - a specialization is defined only where it was chosen.
    for (std::size_t i = 0; i < functions_.size(); i++)
        if (functions_[i].symbol == sig.symbol) { functions_[i].used = true; break; }
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
                   (templates_[name].isClass ? "class" : "function") +
                   " template, and instantiating one is not supported yet");
}


// **A member function template call, v.head<3>().**
ExprPtr Parser::memberTemplateCall(ExprPtr object, const Type *obj,
                                   const std::string &name, std::size_t pos) {
    // The class whose scope declares it - `d.twice(3)` finds B's template through a D.
    const Type *named = obj->unqualified();
    const Type *plain = memberTemplateOwner(named, name);
    if (plain == nullptr) plain = named;
    const std::vector<TemplateDecl> &templates = memberTemplates_[plain->tag() + "::" + name];
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
            argTypes.push_back(deductionArgType(*deducedArgs[i]));
        std::string why;
        bool ok = false;
        // **The first of the name's templates that deduces is the one called.**
        for (std::size_t t = 0; t < templates.size() && !ok; t++) {
            std::vector<Shadow> undo;
            if (!templates[t].classParams.empty())
                bindTemplateParameters(templates[t].classParams, templates[t].classBinding,
                                       templates[t].classValues,
                                       Packs(),
                                       &undo);
            ok = deduceTemplateArguments(templates[t], argTypes, &binding, &values,
                                         &packs, &why);
            unbindTemplateParameters(undo);
            if (ok) mt = templates[t];
        }
        if (!ok)
            src_.fail(pos, "'" + plain->describe() + "::" + name + "' is a "
                           "member function template and " + why);
        args = argumentsFromBinding(mt.params, binding, values, packs);
    }

    const Signature *sig = instantiateMemberTemplate(mt, binding, values, args, pos);
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

    // A copy, since applyDefaults and completeCall read it while the arguments
    // can grow functions_ out from under a reference.
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
    if (!refQualifierViable(chosen.refThis, chosen.constThis, objectIsLvalue(*object)))
        src_.fail(pos, refQualifierRefusal(name, objectIsLvalue(*object)));

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

// The specialization these arguments ask for, made if it is new.
const Parser::Signature *Parser::instantiateMemberTemplate(
    const TemplateDecl &mt, const std::vector<const Type *> &binding,
    const std::vector<long long> &values,
    const std::vector<TemplateArg> &args, std::size_t pos) {
    // head<3>; a constructor's is spelled from the class's name, CMatrix<3>, and keyed apart from it.
    const std::string display =
        specializationKey(mt.isConstructor ? mt.ownerLocal : mt.name, args);
    const std::string key = mt.ownerTag + "::" + display;
    // Two templates of one name may each be instantiated at K = 3: the cache is per template.
    const std::string made = key + "@" + std::to_string(mt.afterParams);
    if (memberTemplateMade_.count(made) != 0) return &functions_[memberTemplateMade_[made]];
    const std::size_t before = functions_.size();
    if (!mt.defined)
        src_.fail(pos, "'" + mt.ownerTag + "::" + mt.name + "' is declared but "
                       "not defined, so there is nothing to instantiate");

    std::vector<Shadow> undo;
    // The class's parameters first (N), then the member's own (M) - two layers,
    // the whole reason a member template records the class's binding.
    if (!mt.classParams.empty())
        bindTemplateParameters(mt.classParams, mt.classBinding, mt.classValues,
                               Packs(), &undo);
    // A member template's own pack is carried in its argument list.
    Packs packs(mt.params.size());
    for (std::size_t i = 0; i < args.size() && i < packs.size(); i++)
        if (args[i].isPack) { packs[i] = args[i].pack; packs[i].values = args[i].packValues; }
    bindTemplateParameters(mt.params, binding, values, packs, &undo);

    // **The pattern the Itanium name is spelled from**: the member's own parameters as
    // references to themselves, the class's already bound to its arguments above.
    std::vector<const Type *> pattern(mt.params.size());
    for (std::size_t i = 0; i < mt.params.size(); i++)
        pattern[i] = types_.templateParam(static_cast<int>(i));
    std::string patternName;
    const Type *patternFn = readTemplateDeclaration(mt, pattern, values, &patternName);

    const bool wasInst = memberTemplateInst_;
    const std::string wasOf = memberTemplateOf_;
    const std::string wasName = memberTemplateName_;
    const std::vector<TemplateArg> wasArgs = memberTemplateArgs_;
    const Type *wasPattern = memberTemplatePattern_;
    memberTemplateInst_ = true;
    memberTemplateOf_ = mt.name;
    memberTemplateName_ = display;
    memberTemplateArgs_ = args;
    memberTemplateAccess_ = mt.memberAccess;
    memberTemplatePattern_ = patternFn;

    std::vector<PendingBody> one(1);
    one[0].tag = mt.ownerTag;
    one[0].start = mt.afterParams;
    one[0].local = mt.ownerLocal.empty() ? mt.ownerTag : mt.ownerLocal;
    one[0].key = key;
    one[0].which = PendingBody::npos();
    struct Restore {
        Parser *p; bool inst; std::string of, name; std::vector<TemplateArg> args; const Type *pattern;
        ~Restore() {
            p->memberTemplateInst_ = inst; p->memberTemplateOf_ = of; p->memberTemplateName_ = name;
            p->memberTemplateArgs_ = args; p->memberTemplatePattern_ = pattern;
        }
    } restore{ this, wasInst, wasOf, wasName, wasArgs, wasPattern };
    replayInlineBodies(one);
    unbindTemplateParameters(undo);

    // The function this replay declared under the key - the first one past `before`.
    const std::vector<std::size_t> *had = overloadsOf(key);
    if (had == nullptr) return nullptr;
    for (std::size_t i = 0; i < had->size(); i++)
        if ((*had)[i] >= before) {
            memberTemplateMade_[made] = (*had)[i];
            return &functions_[(*had)[i]];
        }
    return &functions_[(*had)[0]];
}

// **Function templates as overload-resolution candidates.**
void Parser::instantiateViableTemplates(const std::string &name,
                                        const std::vector<const Type *> &argTypes,
                                        std::size_t pos) {
    std::map<std::string, std::vector<TemplateDecl> >::const_iterator it =
        fnTemplates_.find(name);
    if (it == fnTemplates_.end()) return;
    const std::vector<TemplateDecl> overloads = it->second;
    for (std::size_t k = 0; k < overloads.size(); k++) {
        const TemplateDecl &decl = overloads[k];
        std::vector<const Type *> binding;
        std::vector<long long> values;
        Packs packs;
        std::string why;
        if (!deduceTemplateArguments(decl, argTypes, &binding, &values, &packs,
                                     &why))
            continue;
        const std::vector<TemplateArg> args = argumentsFromBinding(decl.params, binding, values, packs);
        try {
            Trial trial(this);
            instantiate(decl, binding, values, args, pos, packs);
        } catch (const SubstitutionFailure &f) {
            // This template does not apply; the others still might - unless
            // what stopped it was a feature this compiler has not built, which
            // is not [temp.deduct]/8's business and must not be swallowed.
            if (f.unsupported) src_.fail(f.pos, f.why);
        }
    }
}

// **A static member reached through a class template-id**, `CNeeds<(N==3)>::
// check()` or `std::numeric_limits<int>::max()`.
ExprPtr Parser::templateIdMember(const Type *cls, std::size_t pos) {
    (void)pos;
    std::size_t mpos = peek().pos;
    std::string member = declaredName("a member name");
    // `Tree<int>::It::Deep::twice(2)`: a nested class is a scope of its own, walked a name at a time.
    while (peek().is("::") && peekAt(1).kind == TokenKind::Ident) {
        const Type *nested = lookupInClass(cls, member);
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
    if (peek().is("(") && overloadsOf(key) == nullptr)
        if (const Type *named = lookupInClass(cls, member)) {
            if (named->isStructOrUnion()) { at_++; return classTemporary(named, mpos); }
            return functionalCast(named, mpos);
        }
    if (peek().is("(")) {
        at_++;
        std::vector<ExprPtr> callArgs;
        parseArguments(callArgs);
        // **`CVector<I>::operator*(s)` in a member of CVector<4> or of a class derived from it** is
        // `Base::f(...)`: called on `this`, never dispatched, [expr.call]/1 - as for a plain base name.
        const Local *held = findLocal("this");
        if (held != nullptr && !inStaticMember_ && currentClass_ != nullptr && overloadsOf(key) != nullptr &&
            (currentClass_->unqualified() == cls->unqualified() ||
             publicBaseOffset(currentClass_, cls->unqualified()) >= 0)) {
            bool anyMember = false;
            for (std::size_t i : *overloadsOf(key)) anyMember = anyMember || needsThis(functions_[i]);
            if (anyMember) {
                ExprPtr self(Var::local("this", held->offset));
                self->setType(held->type);
                ExprPtr obj(new Unary('*', std::move(self)));
                obj->setType(held->type->pointee());
                return memberCallWith(std::move(obj), held->type->pointee(), member, mpos,
                                      std::move(callArgs), cls);
            }
        }
        if (overloadsOf(key) == nullptr)
            src_.fail(mpos, "'" + cls->describe() + "' has no static "
                            "member function '" + member + "' - only "
                            "a static member is reachable through a "
                            "template-id with no object");
        const Signature &sig = resolveOverload(key, callArgs, mpos);
        applyDefaults(sig, callArgs, mpos);
        if (needsThis(sig))
            src_.fail(mpos, "'" + key + "' is not a static member "
                            "function, so it has to be called on an "
                            "object");
        if (sig.access != Access::Public) {
            const Type *ownerType = findTypedef(sig.owner);
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
    // **An enumerator of the specialization is reached the same way**, and is
    // a value rather than an object - so there is nothing to take the address
    // of and the number is the whole of it.
    if (const EnumConst *e = enumInClass(cls, member)) {
        ExprPtr n(new Num(e->value));
        n->setType(e->type != nullptr ? e->type : types_.intType());
        return n;
    }
    if (unevaluated_ > 0)
        if (const Member *m = cls->findMember(member)) return memberWithoutObject(cls, *m, mpos);
    src_.fail(mpos, "'" + cls->describe() + "' has no static member or "
                    "enumerator called '" + member + "'");
}

// **A member function template as an overload-resolution candidate.**
void Parser::instantiateViableMemberTemplates(
    const Type *cls, const std::string &name,
    const std::vector<const Type *> &argTypes, std::size_t pos) {
    if (cls == nullptr || cls->tag().empty()) return;
    std::map<std::string, std::vector<TemplateDecl> >::const_iterator it =
        memberTemplates_.find(cls->tag() + "::" + name);
    if (it == memberTemplates_.end()) return;
    const std::vector<TemplateDecl> all = it->second;
    for (std::size_t t = 0; t < all.size(); t++) instantiateViableMemberTemplate(all[t], argTypes, pos);
}

void Parser::instantiateViableMemberTemplatesUnder(
    const std::string &key, const std::vector<const Type *> &argTypes, std::size_t pos) {
    std::map<std::string, std::vector<TemplateDecl> >::const_iterator it =
        memberTemplates_.find(key);
    if (it == memberTemplates_.end()) return;
    const std::vector<TemplateDecl> all = it->second;
    for (std::size_t t = 0; t < all.size(); t++) instantiateViableMemberTemplate(all[t], argTypes, pos);
}

void Parser::instantiateConstructorTemplates(const Type *cls,
                                             const std::vector<const Type *> &argTypes,
                                             std::size_t pos) {
    if (cls == nullptr || !cls->isStructOrUnion() || cls->tag().empty()) return;
    instantiateViableMemberTemplatesUnder(constructorKey(cls->unqualified()->tag()), argTypes, pos);
}

void Parser::instantiateConstructorTemplates(const Type *cls, const std::vector<ExprPtr> &args,
                                             std::size_t pos) {
    std::vector<const Type *> argTypes;
    for (std::size_t i = 0; i < args.size(); i++) argTypes.push_back(deductionArgType(*args[i]));
    instantiateConstructorTemplates(cls, argTypes, pos);
}

// One member template as a candidate: deduced, and instantiated where that works.
void Parser::instantiateViableMemberTemplate(const TemplateDecl &mt,
                                             const std::vector<const Type *> &argTypes,
                                             std::size_t pos) {

    std::vector<const Type *> binding;
    std::vector<long long> values;
    Packs packs;
    std::string why;
    std::vector<Shadow> undo;
    if (!mt.classParams.empty())
        bindTemplateParameters(mt.classParams, mt.classBinding, mt.classValues,
                               Packs(), &undo);
    const bool ok = deduceTemplateArguments(mt, argTypes, &binding, &values,
                                            &packs, &why);
    unbindTemplateParameters(undo);
    if (!ok) return;

    const std::vector<TemplateArg> args = argumentsFromBinding(mt.params, binding, values, packs);
    const bool wasBody = memberTemplateBody_;
    memberTemplateBody_ = false;
    try {
        Trial trial(this);
        const Signature *made = instantiateMemberTemplate(mt, binding, values, args, pos);
        if (made != nullptr)
            candidateSpecializations_.push_back(static_cast<std::size_t>(made - &functions_[0]));
    } catch (const SubstitutionFailure &f) {
        // This one does not apply - unless it was a refusal, which is a fact about the compiler
        // rather than about this candidate, or **the body failed**, which [temp.deduct]/8 does not
        // forgive: the candidate was formed, and dropping it now leaves a call to nothing.
        if (f.unsupported) src_.fail(f.pos, f.why);
        if (memberTemplateBody_)
            src_.fail(f.pos, f.why + " - in the instantiation of '" + mt.ownerTag + "::" +
                                 specializationKey(mt.isConstructor ? mt.ownerLocal : mt.name, args) +
                                 "', asked for at " + src_.where(pos));
    }
    memberTemplateBody_ = wasBody;
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
