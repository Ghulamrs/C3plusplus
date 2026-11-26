// sema::Templates - see Templates.h.

#include "Templates.h"
#include "Typing.h"
#include "../parser/Parser.h"
#include "../parser/ParserInternal.h"
#include "../Source.h"

#include <cctype>
#include <climits>
#include <cstdlib>
#include <algorithm>

namespace sema {

// A type as a specialization's key spells it: describe() writes an enumeration as the int it is
// held in, and `Box<Rank>` would be `Box<int>` - one class for two (M1, `std::vector<Rank>`).
static std::string keySpelling(const Type *t) {
    if (t->unqualified() != t)
        return t->isPointer() ? keySpelling(t->unqualified()) + " const" : "const " + keySpelling(t->unqualified());
    if (t->isEnumeration()) return "enum " + t->enumTag();
    if (t->isPointer() && !t->pointee()->isFunction()) return keySpelling(t->pointee()) + " *";
    if (t->kind() == Kind::DependentMember) return keySpelling(t->pointee()) + "::" + t->tag();     // `table::iterator` by its owner
    if (t->kind() == Kind::LValueRef) return keySpelling(t->referent()) + " &";
    if (t->kind() == Kind::RValueRef) return keySpelling(t->referent()) + " &&";
    return t->describe();
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

// **The parameters are bound to the argument list, and the tables are put back
// exactly as they were.** A type parameter becomes a type name and a non-type one
// an enumerator, which is what makes `T x` and `int a[N]` read with no new path.
void Templates::bindTemplateParameters(const std::vector<TemplateParam> &params,
                                    const std::vector<const Type *> &binding,
                                    const std::vector<long long> &values,
                                    const Packs &packs,
                                    std::vector<Shadow> *undo) {
    bool standIn = false;     // R0: a parameter bound to a parameter is a pattern's read
    for (std::size_t i = 0; substitute_ && i < binding.size(); i++) standIn = standIn || (binding[i] != nullptr && binding[i]->kind() == Kind::TemplateParam);
    reentered("bind", standIn, params.empty() ? static_cast<std::size_t>(-1) : params[0].pos);
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
            auto had = p_.packs_.find(p.name);
            if (had != p_.packs_.end()) {
                s.had = true;
                s.hadPack = had->second;
            }
            PackBinding pb;
            // **A non-type pack is a list of values**, its type the written one or T's binding for `T... Vs`.
            pb.isValue = p.type != nullptr;
            if (pb.isValue) pb.valueType = Parser::paramTypeIn(p, binding);
            if (i < binding.size() && binding[i] != nullptr &&
                binding[i]->kind() == Kind::TemplateParam) {
                pb.types.push_back(binding[i]);       // reading a pattern
                if (pb.isValue) pb.values.push_back(0);
            } else if (i < packs.size()) {
                pb.types = packs[i];
                pb.values = packs[i].values;
            }
            p_.packs_[p.name] = pb;
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
            } else if (b != nullptr && b->isTemplateName() && p_.findTemplate(b->templateName()) != templates_.end()) {
                templates_[p.name] = p_.findTemplate(b->templateName())->second;
            } else {
                p_.src_.fail(p.pos, "'" + p.name + "' is a template template parameter and this binds "
                                 "it to no template");
            }
            undo->push_back(s);
            continue;
        }
        if (s.isType) {
            auto it = p_.lookup_.typedefIndex_.find(p.name);
            if (it != p_.lookup_.typedefIndex_.end()) { s.had = true; s.was = it->second; }
            p_.lookup_.typedefIndex_[p.name] = p_.lookup_.typedefs_.size();
            p_.lookup_.typedefs_.push_back(TypedefName{ p.name, binding[i] });
            p_.boundTypeParams_[p.name]++;
        } else if (i < binding.size() && binding[i] != nullptr &&
                   binding[i]->kind() == Kind::TemplateParam) {
            // **A non-type parameter read as a pattern**, the same signal a
            // pack uses: its binding is a Kind::TemplateParam rather than a
            // value, so a bare `V<N>` reads as a reference to N.
            s.isParamRef = true;
            auto it = p_.nonTypePatternParams_.find(p.name);
            if (it != p_.nonTypePatternParams_.end()) {
                s.hadParamRef = true; s.paramRefWas = it->second;
            }
            p_.nonTypePatternParams_[p.name] = binding[i]->length();
            // The concrete value, as a second undo entry so both are restored.
            Shadow ev;
            ev.name = p.name;
            ev.isType = false;
            auto en = p_.lookup_.enumIndex_.find(p.name);
            if (en != p_.lookup_.enumIndex_.end()) { ev.had = true; ev.was = en->second; }
            p_.lookup_.enumIndex_[p.name] = p_.lookup_.enums_.size();
            EnumConst read{ p.name, values[i] };
            read.valueDependent = true;
            const Type *pt = Parser::paramTypeIn(p, binding);     // its declared type, as a value's below ([temp.param]/5)
            if (pt != nullptr && pt->isInteger()) read.type = pt->unqualified();
            p_.lookup_.enums_.push_back(read);
            p_.boundValueParams_[p.name]++;
            undo->push_back(ev);
        } else {
            // **A value hides a pattern's parameter of the same name**: `Mx<3, 3>` built while a pattern
            // with an `N` of its own is read sees its own `N` as 3, not the pattern's.
            auto ref = p_.nonTypePatternParams_.find(p.name);
            if (ref != p_.nonTypePatternParams_.end()) {
                Shadow pr;
                pr.name = p.name;
                pr.isType = false;
                pr.isParamRef = true;
                pr.hadParamRef = true;
                pr.paramRefWas = ref->second;
                p_.nonTypePatternParams_.erase(ref);
                undo->push_back(pr);
            }
            auto it = p_.lookup_.enumIndex_.find(p.name);
            if (it != p_.lookup_.enumIndex_.end()) { s.had = true; s.was = it->second; }
            p_.lookup_.enumIndex_[p.name] = p_.lookup_.enums_.size();
            EnumConst e{ p.name, values[i] };
            // [temp.param]/5: a non-type parameter is a prvalue of its declared type - an enumeration,
            // `unsigned`, `unsigned long long` - and not an int.
            const Type *pt = Parser::paramTypeIn(p, binding);
            if (pt != nullptr && pt->isInteger()) e.type = pt->unqualified();
            e.paramType = pt;
            p_.lookup_.enums_.push_back(e);
            p_.boundValueParams_[p.name]++;
        }
        undo->push_back(s);
    }
}

void Templates::unbindTemplateParameters(const std::vector<Shadow> &undo) {
    for (std::size_t k = undo.size(); k-- > 0; ) {
        const Shadow &s = undo[k];
        if (s.isPack) {
            if (s.had) p_.packs_[s.name] = s.hadPack;
            else       p_.packs_.erase(s.name);
            continue;
        }
        if (s.isTemplate) {
            if (s.hadTemplate) templates_[s.name] = s.hadDecl;
            else               templates_.erase(s.name);
            continue;
        }
        if (s.isType) {
            if (s.had) p_.lookup_.typedefIndex_[s.name] = s.was;
            else       p_.lookup_.typedefIndex_.erase(s.name);
            if (--p_.boundTypeParams_[s.name] <= 0) p_.boundTypeParams_.erase(s.name);
        } else if (s.isParamRef) {
            if (s.hadParamRef) p_.nonTypePatternParams_[s.name] = s.paramRefWas;
            else               p_.nonTypePatternParams_.erase(s.name);
        } else {
            if (s.had) p_.lookup_.enumIndex_[s.name] = s.was;
            else       p_.lookup_.enumIndex_.erase(s.name);
            if (--p_.boundValueParams_[s.name] <= 0) p_.boundValueParams_.erase(s.name);
        }
    }
}

// What a template template parameter is bound to by a specialization: its template's stand-in, or
// - while two patterns are ordered against each other - the other pattern's own parameter.
const Type *Templates::templateOf(const Type *spec) const {
    int tt;
    if (ttRefName(spec->templateName(), &tt)) return p_.types_.templateParam(tt);
    return p_.templateNameTypeOf(templates_.find(spec->templateName())->second);
}

// **The rule is clang's, which takes P0522 as a defect report under -std=c++11** (C++11's letter is
// an exact match): a pack of types on either side stands for every type parameter the other has
// left, each other parameter wants one of its kind, and what the argument has over needs a default.
bool Templates::templateTemplateMatches(const std::vector<TemplateParam> &param,
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

std::vector<TemplateArg> Templates::argumentsFromBinding(
    const std::vector<TemplateParam> &params, const std::vector<const Type *> &binding,
    const std::vector<long long> &values, const Packs &packs) const {
    std::vector<TemplateArg> args;
    for (std::size_t i = 0; i < binding.size() && i < params.size(); i++) {
        TemplateArg a;
        if (params[i].type != nullptr && !params[i].isPack) {
            a.isType = false; a.type = Parser::paramTypeIn(params[i], binding); a.value = values[i];
        } else if (params[i].isPack) {
            a.isType = true; a.isPack = true; a.pack = packs[i];
            if (params[i].type != nullptr) {
                a.packIsValue = true; a.packValues = packs[i].values; a.type = Parser::paramTypeIn(params[i], binding);
            }
        } else {
            a.isType = true; a.type = binding[i]; a.isTemplate = params[i].isTemplate;
        }
        args.push_back(a);
    }
    return args;
}

std::string Templates::specializationKey(const std::string &name,
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
                else key += keySpelling(args[i].pack[k]);
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
        else if (args[i].type->kind() == Kind::TemplateParam)      // `pair<const $T0,$T1>` is not `pair<$T0,$T1>`
            key += std::string(args[i].type->unqualified() == args[i].type ? "" : args[i].type->isConst() ? "const " : "volatile ") +
                   "$T" + std::to_string(args[i].type->length());
        // A template named as an argument: `Holder<Box,int>`, the name as written.
        else if (args[i].isTemplate)
            key += args[i].type->templateNamespace() + args[i].type->templateName();
        else key += keySpelling(args[i].type);
    }
    return key + ">";
}

// **A body cannot be written where the call is**, so every specialization is
// recorded and the definitions replayed afterwards, to a fixed point. And whether
// anything under this key was chosen by a call, which gates every body it holds.
bool Templates::memberIsUsed(const std::string &key) const {
    const std::vector<std::size_t> *set = p_.lookup_.overloadsOf(key);
    for (std::size_t k = 0; set != nullptr && k < set->size(); k++)
        if (p_.lookup_.functions_[(*set)[k]].used) return true;
    return false;
}

Templates::Substitution Templates::substitute(const Substituted &s, const Type *t, Ids ids) const {
    return substituteIn(s, t, ids == Made ? Declared : ids, nullptr, static_cast<std::size_t>(-1));
}

Templates::Substitution Templates::substitute(const Substituted &s, const Type *t, At at) {
    return substituteIn(s, t, Made, this, at.pos);
}

// The kernel's one recursion: each of `from` is `to` under any declarator, a member type through its class, a function's
// parameters through the list form (a pack one per member); a leaf over the parameters `s` does not map is the failure named.
// Made is carried under every declarator and asks the map first, as Mapped does; the cv is stripped before either.
Templates::Substitution Templates::substituteIn(const Substituted &s, const Type *t, Ids ids, Templates *maker, std::size_t pos) const {
    Substitution r = { t, nullptr, nullptr };
    if (t == nullptr) return r;
    const Ids below = maker != nullptr ? Made : Mapped;
    if (ids == Declared && readId(t, nullptr, nullptr)) return specializationIn(s, t, maker, pos);
    if (t->unqualified() != t) {
        r = substituteIn(s, t->unqualified(), below, maker, pos);
        if (!r.type->isReference()) r.type = p_.types_.withConst(r.type);        // [dcl.ref]/1: `const T` with T = U & is U &
        return r;
    }
    for (std::size_t k = 0; k < s.from.size(); k++)
        if (t == s.from[k]) { r.type = s.to[k]; return r; }
    if (ids == Made && readId(t, nullptr, nullptr)) return specializationIn(s, t, maker, pos);
    const int bound = t->isArray() ? t->boundParam() : -1;
    if (t->isPointer() || (t->isArray() && bound < 0)) {
        r = substituteIn(s, t->pointee(), below, maker, pos);
        r.type = t->isPointer() ? p_.types_.pointerTo(r.type) : p_.types_.arrayOf(r.type, t->length());
        return r;
    }
    if (bound >= 0 && static_cast<std::size_t>(bound) < s.values.size() && s.values[bound] > 0) {     // a class read's `T a[N]`
        r = substituteIn(s, t->pointee(), below, maker, pos);
        r.type = p_.types_.arrayOf(r.type, s.values[bound]);
        return r;
    }
    if (t->isReference()) {
        r = substituteIn(s, t->referent(), below, maker, pos);
        const Type *u = r.type;
        if (u->isReference()) r.type = t->isRValueReference() ? u : p_.types_.referenceTo(u->referent());   // [dcl.ref]/6
        else r.type = t->isRValueReference() ? p_.types_.rvalueReferenceTo(u) : p_.types_.referenceTo(u);
        return r;
    }
    if (t->kind() == Kind::DependentMember) {     // `X<T>::m`: X<U> by its key (a cleared shallow by its kept id), m as memberTypeWalk finds it
        const bool id = readId(t->pointee(), nullptr, nullptr);
        const Substitution o = substituteIn(s, t->pointee(), id ? (ids == Made ? Made : Declared) : below, maker, pos);
        const Type *m = (id && !o.carried()) || o.type == nullptr || p_.dependentType(o.type) || !o.type->isStructOrUnion()
                        ? nullptr : p_.lookup_.lookupInClass(o.type, t->tag());
        if (m != nullptr) r.type = m;
        else r.why = o.type != nullptr && o.type->isStructOrUnion() && !p_.dependentType(o.type) && (!id || o.carried())
                     ? "a member type its class lacks" : "a member type whose class the binding does not carry";
        if (m == nullptr) r.at = t;
        return r;
    }
    if (bound >= 0) r.why = "an array bound over a non-type parameter the binding gives no value";
    else if (t->kind() == Kind::TemplateParam) r.why = "a template parameter the binding does not carry";
    else if (!t->isFunction() && (p_.dependentType(t) || readIds_.count(t) != 0 || t->kind() == Kind::PackExpansion))
        r.why = "a type over the parameters the binding does not carry";
    if (r.why != nullptr) r.at = t;
    if (bound >= 0 || !t->isFunction()) return r;
    r = substituteIn(s, t->returns(), below, maker, pos);
    const Substitutions params = substituteList(s, t->params(), maker, pos);
    if (r.carried()) r.why = params.why, r.at = params.at;
    r.type = p_.types_.functionType(r.type, params.types, t->isVariadicFn());
    return r;
}

// The id step: `X<$T0>` under `s`, the specialization the program declared by its key; where none is and `maker` is given, made
// through makeClass as the class door makes it (makeId). Failure names an argument the binding does not carry, or no class.
// At Made a partial's read class is no id (`array<$T0>`, its own parameters, not `array<$T0,0>`): the map's (readStands) or none.
Templates::Substitution Templates::specializationIn(const Substituted &s, const Type *o, Templates *maker, std::size_t pos) const {
    Substitution r = { o, nullptr, o };
    std::string name;
    std::vector<TemplateArg> args;
    if (maker != nullptr && readPartials_.count(o) != 0) {
        r.why = "a partial's read class, which the binding does not carry";
        return r;
    }
    if (!idUnder(s, o, &name, &args) || !argsUnder(s, o, name, &args, maker, pos)) {
        r.why = "a class template-id with an argument the binding does not carry";
        return r;
    }
    const Type *real = p_.lookup_.findTypedef(specializationKey(name, args));
    if (real == nullptr && maker != nullptr && templates_.count(name) != 0) real = maker->makeId(name, args, pos, nullptr);
    if (real == nullptr) r.why = "a class template-id over the parameters the program has not made";
    else { r.type = real; r.at = nullptr; }
    return r;
}

// The tag `name<args>` names: the class the program declared, or one made now through makeClass under the nesting bound, as the
// replay makes it reading the id; `*made` says which. Packs laid as the parser binds a template-id's (no type in the binding).
const Type *Templates::makeId(const std::string &name, const std::vector<TemplateArg> &args, std::size_t pos, bool *made) {
    const std::string tag = specializationKey(name, args);
    if (made != nullptr) *made = false;
    if (const Type *had = p_.lookup_.findTypedef(tag)) return had;
    std::vector<const Type *> types;
    std::vector<long long> vals;     // a value argument's, as argValued gave it
    const TemplateDecl &decl = templates_.at(name);
    Packs packed(decl.params.size());
    for (std::size_t k = 0; k < args.size(); k++) {
        types.push_back(args[k].isPack ? nullptr : args[k].type);
        vals.push_back(args[k].isType || args[k].isPack ? 0 : args[k].value);
        if (args[k].isPack && k < packed.size()) packed[k] = PackMembers(args[k].pack), packed[k].values = args[k].packValues;
    }
    const Parser::Nesting deep(&p_, pos, "template instantiation");
    if (made != nullptr) *made = true;
    return makeClass(decl, tag, types, vals, args, packed, pos);
}

// A parameter list under `s`: a read's `Args... a` (or `const Args &...`) is one parameter per member of the pack `s` binds, made
// as Parser::packParameter makes them for the replay; kept as written where `s` binds no packs. The kernel's pack step: its first failure.
Templates::Substitutions Templates::substitute(const Substituted &s, const std::vector<const Type *> &params) const {
    return substituteList(s, params, nullptr, static_cast<std::size_t>(-1));
}

Templates::Substitutions Templates::substitute(const Substituted &s, const std::vector<const Type *> &params, At at) {
    return substituteList(s, params, this, at.pos);
}

// The list form's one loop, each parameter at Mapped or (with `maker`) at Made.
Templates::Substitutions Templates::substituteList(const Substituted &s, const std::vector<const Type *> &params, Templates *maker,
                                                   std::size_t pos) const {
    const Ids ids = maker != nullptr ? Made : Mapped;
    Substitutions list;
    list.why = nullptr, list.at = nullptr;
    std::vector<const Type *> &out = list.types;
    Substitution r = { nullptr, nullptr, nullptr };
    for (const Type *t : params) {
        const Type *q = t->kind() == Kind::PackExpansion ? t->pointee() : nullptr;
        while (q != nullptr && (q->unqualified() != q || q->isPointer() || q->isReference()))
            q = q->unqualified() != q ? q->unqualified() : q->isReference() ? q->referent() : q->pointee();
        if (q == nullptr || q->kind() != Kind::TemplateParam || static_cast<std::size_t>(q->length()) >= s.packs.size()) {
            r = substituteIn(s, t, ids, maker, pos);
            out.push_back(r.type);
            if (list.why == nullptr) list.why = r.why, list.at = r.at;
            continue;
        }
        const PackMembers &members = s.packs[static_cast<std::size_t>(q->length())];
        for (std::size_t k = 0; k < members.size(); k++) {
            Substituted one = s;
            one.from.insert(one.from.begin(), q), one.to.insert(one.to.begin(), members[k]);
            r = substituteIn(one, t->pointee(), ids, maker, pos);
            out.push_back(p_.types_.withoutConst(r.type));
            if (list.why == nullptr) list.why = r.why, list.at = r.at;
        }
    }
    return list;
}

// Slice 3g: the read laid `T a[N + 1]` with no bound; classShape folded it into `s.bounds`.
const Type *Templates::dataUnder(const Substituted &s, const Member &m) const {
    const std::map<std::string, long long>::const_iterator b = s.bounds.find(m.name);
    if (b == s.bounds.end() || !m.type->isArray() || m.type->length() != -1) return substitute(s, m.type).type;
    return p_.types_.arrayOf(substitute(s, m.type->pointee()).type, b->second);
}

// Slice 3g: a bound's tokens folded as int under the class's values - its non-type parameters, int literals, + - * / % and
// parentheses (`level` 0 additive, 1 multiplicative, 2 unary); false at any other token or past int.
static bool foldBound(const std::vector<Token> &k, std::size_t *t, std::size_t end, const std::vector<TemplateParam> &params,
                      const std::vector<long long> &values, int level, long long *out) {
    if (*t >= end) return false;
    if (level == 2) {
        const Token &x = k[*t];
        ++*t;
        if (x.is("-") || x.is("+")) { if (!foldBound(k, t, end, params, values, 2, out)) return false; *out = x.is("-") ? -*out : *out; return true; }
        if (x.is("(")) return foldBound(k, t, end, params, values, 0, out) && *t < end && k[(*t)++].is(")");
        if (x.kind == TokenKind::Num && !x.isFloat && !x.isChar && !x.suffixU && !x.suffixL && !x.suffixLL && x.value >= 0) { *out = x.value; return true; }
        for (std::size_t q = 0; q < params.size() && q < values.size(); q++)
            if (params[q].type != nullptr && params[q].type->isInteger() && !params[q].name.empty() && x.text == params[q].name)
                { *out = values[q]; return true; }
        return false;
    }
    if (!foldBound(k, t, end, params, values, level + 1, out)) return false;
    while (*t < end && (level == 0 ? k[*t].is("+") || k[*t].is("-") : k[*t].is("*") || k[*t].is("/") || k[*t].is("%"))) {
        const std::string op = k[(*t)++].text;
        long long b = 0;
        if (!foldBound(k, t, end, params, values, level + 1, &b) || ((op == "/" || op == "%") && b == 0)) return false;
        *out = op == "+" ? *out + b : op == "-" ? *out - b : op == "*" ? *out * b : op == "/" ? *out / b : *out % b;
        if (*out < INT_MIN || *out > INT_MAX) return false;
    }
    return true;
}

// Slice 3h: the read's DepExpr of an argument (`X<N + 1>`) folded under `values` as foldBound folds a bound's tokens.
static bool foldArg(const DepExpr *e, const std::vector<long long> &values, long long *out) {
    long long a = 0, b = 0;
    if (e->op == DepExpr::Literal) return !e->floating && e->type != nullptr && e->type->kind() == Kind::Int && (*out = e->value) >= 0;
    if (e->op == DepExpr::TemplateParam)
        return e->index >= 0 && static_cast<std::size_t>(e->index) < values.size() && ((*out = values[static_cast<std::size_t>(e->index)]), true);
    if (e->op != DepExpr::Operator || e->kids.empty() || e->kids.size() > 2 || !foldArg(e->kids[0], values, &a)) return false;
    if (e->kids.size() == 1) return (e->code == "ng" || e->code == "ps") && ((*out = e->code == "ng" ? -a : a), true);
    if (!foldArg(e->kids[1], values, &b) || ((e->code == "dv" || e->code == "rm") && (b <= 0 || a < 0))) return false;
    *out = e->code == "pl" ? a + b : e->code == "mi" ? a - b : e->code == "ml" ? a * b : e->code == "dv" ? a / b : a % b;
    return (e->code == "pl" || e->code == "mi" || e->code == "ml" || e->code == "dv" || e->code == "rm") && *out >= INT_MIN && *out <= INT_MAX;
}

// Slice 3f: a non-type argument under `s`: the class's parameter `$P<i>` at its value, or (3h) its expression folded; a written
// one (a read class's are its stand-ins), no.
static bool argValued(const Templates::Substituted &s, TemplateArg *a) {
    long long v = 0;
    if (!a->isType && !a->isPack && !a->isTemplate && a->expr != nullptr && a->dependentValue && foldArg(a->expr, s.values, &v) && v >= 0)
        return a->expr = nullptr, a->dependentValue = false, a->value = v, true;
    if (a->isType || a->isPack || a->isTemplate || a->expr != nullptr || a->dependentValue || !a->isParam) return false;
    if (a->paramIndex < 0 || static_cast<std::size_t>(a->paramIndex) >= s.values.size()) return false;
    a->isParam = false, a->value = s.values[static_cast<std::size_t>(a->paramIndex)];
    return true;
}

// Slice 3t: a value written in a stand-in's list (`C<3>`), which idUnder laid as the bound template's argument.
static bool writtenValue(const TemplateArg &a) {
    return !a.isType && !a.isPack && !a.isTemplate && a.expr == nullptr && !a.isParam && !a.dependentValue;
}

// An id's arguments (idUnder's, of template `name`) under `s`: packs expanded, values folded, types substituted; false at one
// that stays a parameter. With `maker` (the id step at Made), an id among them made too and one not carried a failure.
bool Templates::argsUnder(const Substituted &s, const Type *o, const std::string &name, std::vector<TemplateArg> *args, Templates *maker,
                          std::size_t pos) const {
    std::string written;
    const bool bound = readId(o, &written, nullptr) && written != name;     // 3t: a stand-in's list, renamed
    for (std::size_t k = 0; k < args->size(); k++)
        if (!argUnder(s, &(*args)[k], bound, maker, pos)) return false;
    return true;
}

// One argument's type under `s`: the map's (Mapped), or at Made as the recursion carries it - false there where it is not carried.
bool Templates::argTypeUnder(const Substituted &s, const Type **t, Templates *maker, std::size_t pos) const {
    const Substitution r = substituteIn(s, *t, maker != nullptr ? Made : Mapped, maker, pos);
    *t = r.type;
    return maker == nullptr || r.carried();
}

// The kernel's argument step, one of an id's under `s` (`bound`: a stand-in's list, its written values kept): a value folded, a
// type substituted, a class template kept; a pack as the replay's template-id lists it - `Ts...` the members `s` binds to Ts, each
// other member substituted. False where one stays a parameter, `s` binds no such pack or a member does (`List<M>` of a non-type M).
bool Templates::argUnder(const Substituted &s, TemplateArg *a, bool bound, Templates *maker, std::size_t pos) const {
    if (!a->isPack || a->isTemplate) {
        if (!a->isType && (argValued(s, a) || (bound && writtenValue(*a)))) return true;
        if (a->isTemplate && a->type->isTemplateName()) return true;     // a class template, bound or written
        if (!a->isType || a->isPack || a->isTemplate || a->expr != nullptr || a->isParam) return false;
        return argTypeUnder(s, &a->type, maker, pos);
    }
    PackMembers out;
    for (std::size_t k = 0; k < a->pack.size(); k++) {
        const Type *m = a->pack[k];
        const bool expansion = a->expandsParam >= 0 && m->kind() == Kind::TemplateParam && m->length() == a->expandsParam;
        const std::size_t q = static_cast<std::size_t>(a->expandsParam);
        if (expansion && (q >= s.packs.size() || s.packs[q].values.size() != (a->packIsValue ? s.packs[q].size() : 0))) return false;
        if (expansion) out.insert(out.end(), s.packs[q].begin(), s.packs[q].end());
        if (expansion) out.values.insert(out.values.end(), s.packs[q].values.begin(), s.packs[q].values.end());
        if (expansion) continue;
        if (a->packIsValue && (m->kind() == Kind::TemplateParam || k >= a->packValues.size())) return false;
        const Type *u = m;
        if (!argTypeUnder(s, &u, maker, pos)) return false;
        out.push_back(u);
        if (a->packIsValue) out.values.push_back(a->packValues[k]);
    }
    for (std::size_t k = 0; k < out.size(); k++) if (out[k] == nullptr || p_.dependentType(out[k])) return false;
    a->pack = out, a->packValues = out.values, a->expandsParam = -1;
    if (a->type != nullptr) a->type = substitute(s, a->type).type;
    return true;
}

const Type *Templates::valueUnder(const Substituted &s, const Type *t) const {
    const Type *u = substitute(s, t).type;
    return u != nullptr && u->isReference() ? u->referent() : u;      // [expr]/5
}

// A reference cast's referent and category as the read records them (Parser::staticCast), collapsed as written ([dcl.ref]/6).
std::pair<const Type *, bool> Templates::castUnder(const Substituted &s, const std::pair<const Type *, bool> &read) const {
    const Type *w = substitute(s, read.second ? p_.types_.rvalueReferenceTo(read.first) : p_.types_.referenceTo(read.first)).type;
    return std::make_pair(w->referent(), w->isRValueReference());
}

// A node's cast as the typed tree records it (Typer::record) and holds it (recordDiffers): the parser's, under `binding`.
std::pair<TypeId, bool> Templates::castRecord(std::uint32_t id, const std::string &binding) const {
    const std::unordered_map<std::uint32_t, std::pair<const Type *, bool> >::const_iterator ref = p_.refCastTypes_.find(id);
    const std::unordered_map<std::uint32_t, const Type *>::const_iterator cast = p_.castTypes_.find(id);
    std::pair<const Type *, bool> c(ref != p_.refCastTypes_.end() ? ref->second.first : cast != p_.castTypes_.end() ? cast->second : nullptr,
                                    ref != p_.refCastTypes_.end() && ref->second.second);
    if (!binding.empty() && c.first != nullptr)
        c = ref != p_.refCastTypes_.end() ? castUnder(bound_.at(binding).s, c) : std::make_pair(substitute(bound_.at(binding).s, c.first).type, false);
    return std::make_pair(TypeTable::id(c.first), c.second);
}

// A dependent decltype's operand the read holds as an expression and notes no syntax for, so its parts are not the replay's.
static bool unnotedOperand(const DepExpr *e) {
    if (e == nullptr) return false;
    if (e->op == DepExpr::SizeofType || e->op == DepExpr::SizeofExpr || e->op == DepExpr::Cast || e->op == DepExpr::CastList ||
        e->op == DepExpr::StaticCast || e->op == DepExpr::QualifiedParamName || e->op == DepExpr::TemplateParam)
        return true;
    for (std::size_t k = 0; k < e->kids.size(); k++) if (unnotedOperand(e->kids[k])) return true;
    return false;
}

// Slice 1 covers a free function template over type parameters whose read body makes no call, member access or object:
// nothing in it waits on [temp.dep.candidate]'s lookup (M8rev M7). Its build is owed; a covered body replays and is counted.
bool Templates::instantiate(std::size_t i, const Binding &b, std::string *why) {
    const Specialization &sp = specializations_[i];
    if (sp.explicitly) return *why = "replayed: an explicit specialization", false;
    for (std::size_t q = 0; q < sp.params.size(); q++) {
        if (sp.params[q].isPack || sp.params[q].isTemplate) return *why = "replayed: a pack or template parameter, bound by name", false;
        const Type *pt = sp.params[q].type != nullptr ? Parser::paramTypeIn(sp.params[q], sp.binding) : nullptr;
        if (pt != nullptr && (pt->unqualified() != p_.types_.get(Kind::Int) || q >= b.values.size()))
            return *why = "replayed: a non-type parameter of a type other than int, bound by name", false;
    }
    for (std::size_t q = 0; q < b.types.size(); q++) {
        if (q < sp.params.size() && sp.params[q].type != nullptr) continue;     // a non-type parameter: its value, in valueShape
        if (!b.types[q].valid()) return *why = "replayed: a type parameter bound to no type", false;
        // A class's locals are built by records keyed on the replay's own nodes (Typer::localBuilt): a class is bound where no
        // object of it is built, copied or destroyed - reached by reference or pointer alone (boundObject, in substShape).
        const Type &bt = *p_.types_.type(b.types[q])->unqualified();
        const Type *to = bt.isReference() && !bt.isRValueReference() ? bt.referent()->unqualified() : nullptr;   // `T &&` from an lvalue
        const std::string plain = bt.isScalar() || (to != nullptr && to->isScalar()) ? std::string() : boundClass(to != nullptr && to->kind() == Kind::Struct ? *to : bt);
        if (!plain.empty()) return *why = "replayed: a type parameter bound to " + plain, false;
    }
    const std::map<std::size_t, syntax::NodeId>::const_iterator read = readBody_.find(sp.start);
    if (read == readBody_.end()) return *why = "replayed: no body read at the declaration", false;
    const std::map<std::size_t, std::size_t>::const_iterator fn = readFnAt_.find(sp.start);
    for (std::size_t q = 0; q < b.types.size() && fn != readFnAt_.end(); q++)
        if (readFns_[fn->second].returns() != nullptr && b.types[q].valid() &&
            p_.types_.type(b.types[q])->unqualified()->isStructOrUnion() &&
            readFns_[fn->second].returns()->unqualified() == p_.types_.templateParam(static_cast<int>(q)))
            return *why = "replayed: a bound class returned by value, which its copy would build", false;
    // Slice 2b: the replay still lays the frame; the Typer types this read under `b` and the lowering takes it (Typer::typeBody).
    Bound made;
    made.read = read->second;
    for (std::size_t q = 0; q < b.types.size(); q++) {
        if (q < sp.params.size() && sp.params[q].type != nullptr) continue;
        made.s.from.push_back(p_.types_.templateParam(static_cast<int>(q)));
        made.s.to.push_back(p_.types_.type(b.types[q]));
    }
    std::vector<syntax::NodeId> requests;
    const std::string shape = substShape(read->second, made.s, &made.calls, &made.members, &made.assigns, &requests);
    if (!shape.empty()) return *why = "replayed: " + shape, false;
    const std::string value = valueShape(read->second, sp, &made.values);
    if (!value.empty()) return *why = "replayed: " + value, false;
    // A trailing decltype's operand the read keeps no syntax for leaves the function other parts than the replay's.
    const std::unordered_map<std::uint32_t, const Type *>::const_iterator ret = p_.declaredReturns_.find(read->second.v);
    int depth = 0;
    bool trailing = false;
    for (std::size_t t = sp.start; t < p_.tokens_.size() && !(depth == 0 && (p_.tokens_[t].is("{") || p_.tokens_[t].is(";"))); t++) {
        depth += p_.tokens_[t].is("(") ? 1 : p_.tokens_[t].is(")") ? -1 : 0;
        trailing = trailing || (depth == 0 && p_.tokens_[t].is("->"));
    }
    if (trailing && ret != p_.declaredReturns_.end() && ret->second != nullptr && unnotedOperand(ret->second->unqualified()->dependentExpr()))
        return *why = "replayed: a trailing 'decltype' over a sizeof, cast or qualified name, which the read notes in other parts", false;
    // The requests in the replay's order: a call's node is noted as its door fires, after its arguments, so the read's node
    // order is the doors' - an argument that is a call is asked, and recorded, before the call it is passed to. `sp` may move.
    std::vector<std::uint32_t> order;
    for (std::size_t q = 0; q < requests.size(); q++) order.push_back(requests[q].v);
    std::sort(order.begin(), order.end());
    for (std::size_t q = 0; q < order.size(); q++) requests[q] = syntax::NodeId(order[q]);
    const Specialization outer = sp;
    for (std::size_t q = 0; q < requests.size(); q++) {
        std::size_t chosen = 0;
        const std::string asked = requestCall(outer, requests[q], made.s, made.calls, &chosen);
        if (!asked.empty()) return *why = "replayed: " + asked, false;
        made.calls[requests[q].v] = made.calls[p_.syntaxTree_.child(requests[q], 0).v] = chosen;
    }
    bound_[outer.symbol] = made;
    return *why = "covered: typed under the binding, the replay's frame kept", false;
}

// Slice 2f (M8rev L2): a value-dependent name in the read is worth what the replay folds it to - a non-type parameter of type
// int its argument, a const int local made from one its initialiser under them; any other mark replays by name.
std::string Templates::valueShape(syntax::NodeId body, const Specialization &sp, std::map<std::uint32_t, std::size_t> *values) const {
    const syntax::Tree &st = p_.syntaxTree_;
    std::map<std::string, int> params;
    for (std::size_t q = 0; q < sp.params.size() && q < sp.values.size(); q++)
        if (sp.params[q].type != nullptr) params[sp.params[q].name] = static_cast<int>(sp.values[q]);
    if (params.empty()) return std::string();
    std::map<std::string, int> consts;        // a const local's initialiser, by its node
    std::map<std::string, int> declared;
    std::vector<syntax::NodeId> marked;
    for (std::vector<syntax::NodeId> work(1, body); !work.empty(); ) {
        const syntax::NodeId id = work.back();
        work.pop_back();
        const syntax::Node n = st.node(id);
        for (std::uint32_t c = 0; c < n.count; c++) work.push_back(st.child(id, c));
        if ((n.marks & syntax::ValueDependent) != 0 || n.kind == syntax::Kind::Name) marked.push_back(id);
        const std::unordered_map<std::uint32_t, Local>::const_iterator l =
            n.kind == syntax::Kind::Var ? p_.declaredLocals_.find(id.v) : p_.declaredLocals_.end();
        if (l == p_.declaredLocals_.end()) continue;
        const Type *t = l->second.type;
        if (t != nullptr && t->unqualified()->isArray()) return "an array local beside a non-type parameter, its bound the read's stand-in";
        if (!l->second.staticName.empty() && st.holds(id, syntax::ValueDependent))
            return "a static local over a non-type parameter, its value the read's stand-in";
        declared[l->second.name]++;
        if (l->second.valueDependent && n.count == 1 && t != nullptr && t->unqualified() == p_.types_.get(Kind::Int))
            consts[l->second.name] = static_cast<int>(st.child(id, 0).v);
    }
    for (std::size_t k = 0; k < marked.size(); k++) {
        const std::string name = st.text(marked[k]);
        long long v = 0;
        // A read of a const local made from a parameter the read left unmarked is the replay's constant as well.
        if ((st.node(marked[k]).marks & syntax::ValueDependent) == 0 && (st.node(marked[k]).kind != syntax::Kind::Name || consts.count(name) == 0)) continue;
        if (st.node(marked[k]).kind != syntax::Kind::Name || declared[name] > 1 || (declared[name] != 0 && params.count(name) != 0) ||
            !foldUnder(marked[k], params, consts, 0, &v))
            return "a value-dependent name the binding does not fold, bound by name";
        if (p_.resolutions_.at(marked[k].v).what == Parser::Resolved::Local) continue;     // read as the replay reads it, from its slot
        (*values)[marked[k].v] = static_cast<std::size_t>(v + 2147483648LL);     // in int, so 0 to 2^32 - 1
    }
    return std::string();
}

// A constant of type int as [expr.const] folds it from `params` and the const locals `consts`; false past int or a shape not here.
bool Templates::foldUnder(syntax::NodeId id, const std::map<std::string, int> &params,
                          const std::map<std::string, int> &consts, int depth, long long *out) const {
    const syntax::Tree &st = p_.syntaxTree_;
    const syntax::Node n = st.node(id);
    const std::string op = st.text(id);
    long long a = 0, b = 0;
    if (depth > 64) return false;
    if (n.kind == syntax::Kind::Name) {
        const std::unordered_map<std::uint32_t, Parser::Resolved>::const_iterator r = p_.resolutions_.find(id.v);
        const bool local = r != p_.resolutions_.end() && r->second.what == Parser::Resolved::Local;     // a const local: its initialiser
        if (r == p_.resolutions_.end() || (r->second.what != Parser::Resolved::Enumerator && !local)) return false;
        const std::map<std::string, int>::const_iterator p = local ? params.end() : params.find(op);
        if (p != params.end()) { *out = p->second; return true; }
        const std::map<std::string, int>::const_iterator c = consts.find(op);
        return c != consts.end() && foldUnder(syntax::NodeId(static_cast<std::uint32_t>(c->second)),
 params, consts, depth + 1, out);
    } else if (n.kind == syntax::Kind::IntLit) {
        const std::unordered_map<std::uint32_t, std::size_t>::const_iterator lit = p_.literalTokens_.find(id.v);
        if (lit == p_.literalTokens_.end()) return false;
        const Token &t = p_.tokens_[lit->second];
        if (t.kind != TokenKind::Num || t.isFloat || t.isChar || t.suffixU || t.suffixL || t.suffixLL || t.value < 0) return false;
        *out = t.value;
    } else if ((n.kind == syntax::Kind::Paren || n.kind == syntax::Kind::Unary) && n.count == 1) {
        if (!foldUnder(st.child(id, 0), params, consts, depth + 1, &a)) return false;
        if (n.kind == syntax::Kind::Paren || op == "+") *out = a;
        else if (op == "-") *out = -a;
        else if (op == "~") *out = ~a;
        else if (op == "!") *out = !a;
        else return false;
    } else if (n.kind == syntax::Kind::Binary && n.count == 2) {
        if (!foldUnder(st.child(id, 0), params, consts, depth + 1, &a) || !foldUnder(st.child(id, 1), params, consts, depth + 1, &b)) return false;
        if ((op == "/" || op == "%") && (b == 0 || (a == INT_MIN && b == -1))) return false;
        if (op == "+") *out = a + b; else if (op == "-") *out = a - b; else if (op == "*") *out = a * b;
        else if (op == "/") *out = a / b; else if (op == "%") *out = a % b;
        else if (op == "&") *out = a & b; else if (op == "|") *out = a | b; else if (op == "^") *out = a ^ b;
        else if (op == "<") *out = a < b; else if (op == ">") *out = a > b; else if (op == "<=") *out = a <= b;
        else if (op == ">=") *out = a >= b; else if (op == "==") *out = a == b; else if (op == "!=") *out = a != b;
        else if (op == "&&") *out = a && b; else if (op == "||") *out = a || b;
        else return false;
    } else {
        return false;
    }
    return *out >= INT_MIN && *out <= INT_MAX;
}

static const char TemplateCallee[] =
    "a qualified or explicit-argument call a function template may answer, which the replay deduces or specializes";

// [temp.nondep]: a call with no type-dependent part is bound at the definition, so the read's record is the replay's - here
// a named function of scalars that is no specialization; the rest replays by name. The callee's use is made in callsUsed.
std::string Templates::nondependentCall(syntax::NodeId call) const {
    const syntax::Tree &st = p_.syntaxTree_;
    for (std::uint32_t c = 0; c < st.node(call).count; c++) {
        if ((st.node(st.child(call, c)).marks & (syntax::TypeDependent | syntax::DependentName)) == 0) continue;
        if (st.node(st.child(call, 0)).kind != syntax::Kind::Name) return "a call through a value or a member with a type-dependent part";
        const std::string written = st.text(st.child(call, 0)), name = written.substr(0, written.find('<'));
        const std::string::size_type cut = name.rfind("::");
        if (fnTemplates_.count(cut == std::string::npos ? name : name.substr(cut + 2)) != 0)
            return TemplateCallee;
        return "a call with a type-dependent argument or a dependent-name callee, resolved at the instantiation";
    }
    const std::unordered_map<std::uint32_t, Parser::Resolved>::const_iterator r = p_.resolutions_.find(call.v);
    if ((st.node(call).marks & (syntax::TypeDependent | syntax::DependentName)) != 0 || st.node(call).count == 0 ||
        st.node(st.child(call, 0)).kind != syntax::Kind::Name || r == p_.resolutions_.end() || r->second.what != Parser::Resolved::Call ||
        r->second.symbol.empty() || r->second.indirect || r->second.hasThis || r->second.type == nullptr || !r->second.type->isFunction())
        return "a call through a value, a member or a class, or one the read did not resolve";
    const Type &f = *r->second.type;
    bool scalars = !f.isVariadicFn() && (f.returns()->isVoid() || f.returns()->isScalar());
    for (std::size_t q = 0; q < f.params().size(); q++) scalars = scalars && f.params()[q]->isScalar();
    if (!scalars) return "a call of a function over a class, a reference or variadic arguments";
    for (std::size_t q = 0; q < specializations_.size(); q++)
        if (specializations_[q].symbol == r->second.symbol) return "a call of a specialization, which the replay asks for";
    return std::string();
}

// [temp.dep.candidate]: an unqualified dependent-name call is resolved at the instantiation over what the definition's lookup
// found and ADL's, which an arithmetic argument has none of ([basic.lookup.argdep]/2), ranked as any call (Overload::bestViable).
std::string Templates::dependentCall(syntax::NodeId call, const Substituted &s, std::size_t *chosen) {
    const syntax::Tree &st = p_.syntaxTree_;
    if (st.node(call).count == 0) return nondependentCall(call);
    const syntax::NodeId callee = st.child(call, 0);
    if (st.node(callee).kind != syntax::Kind::Name || (st.node(callee).marks & syntax::DependentName) == 0 ||
        st.text(callee).find_first_of(":<") != std::string::npos) {
        const std::string why = nondependentCall(call);
        return why == TemplateCallee ? templateCandidates(call, s) : why;
    }
    const std::vector<std::string> found = st.foundSymbols(callee);
    std::vector<std::size_t> set;
    for (std::size_t i = 0; i < found.size(); i++) {
        if (found[i].empty()) return templateCandidates(call, s);
        std::size_t q = 0;
        while (q < p_.lookup_.functions_.size() && p_.lookup_.functions_[q].symbol != found[i]) q++;
        const Signature &f = p_.lookup_.functions_[q < p_.lookup_.functions_.size() ? q : 0];
        bool scalars = q < p_.lookup_.functions_.size() && f.owner.empty() && !f.variadic && !f.deleted &&
                       p_.overload_.leastArguments(f) == f.params.size() && (f.returns->isVoid() || f.returns->isScalar());
        for (std::size_t k = 0; scalars && k < f.params.size(); k++) scalars = f.params[k]->isScalar();
        if (!scalars) return "a dependent-name call of a member, or of a function over a class, a reference, a default or variadic arguments";
        set.push_back(q);
    }
    if (set.empty()) return "a dependent-name call the definition's lookup found nothing for, which ADL alone would answer";
    // The replay looks the name up again where it reads the body: one declared since, or in another scope, is in its set too.
    const std::string last = st.text(callee);
    for (std::unordered_map<std::string, std::vector<std::size_t> >::const_iterator k = p_.lookup_.functionIndex_.begin();
         k != p_.lookup_.functionIndex_.end(); ++k) {
        const bool named = k->first == last || (k->first.size() > last.size() + 2 &&
                                                k->first.compare(k->first.size() - last.size() - 2, std::string::npos, "::" + last) == 0);
        if (named && std::find(k->second.begin(), k->second.end(), set[0]) == k->second.end())
            return "a dependent-name call whose name another scope declares, which the replay's lookup may find";
        if (named && k->second != set) return "a dependent-name call of a name declared again since the definition, or specialized";
    }
    std::vector<std::size_t> viable;
    std::vector<std::vector<Rank> > ranks;
    std::vector<const Type *> given;
    for (std::uint32_t c = 1; c < st.node(call).count; c++) {
        const std::unordered_map<std::uint32_t, Parser::Resolved>::const_iterator r = p_.resolutions_.find(st.child(call, c).v);
        if (st.node(st.child(call, c)).kind != syntax::Kind::Name || r == p_.resolutions_.end() || r->second.what != Parser::Resolved::Local ||
            r->second.type == nullptr)
            return "a dependent-name call over an argument other than a local, which the instantiation does not type here";
        const Type *t = substitute(s, r->second.type).type;
        t = (t->isReference() ? t->referent() : t)->unqualified();
        if (!t->isArithmetic() || t->isEnumeration()) return "a dependent-name call over an argument whose type ADL would search";
        given.push_back(t);
    }
    for (std::size_t k = 0; k < set.size(); k++) {
        const Signature &f = p_.lookup_.functions_[set[k]];
        if (f.params.size() != given.size()) continue;
        std::vector<Rank> rank;
        for (std::size_t q = 0; q < given.size() && (rank.empty() || rank.back() != Rank::None); q++) {
            RankedArgument a;
            a.type = given[q]->id();
            a.lvalue = true;            // a local named is an lvalue, and a scalar parameter takes it by value
            rank.push_back(p_.overload_.rankArgument(a, f.params[q]));
        }
        if (rank.empty() || rank.back() != Rank::None) viable.push_back(set[k]), ranks.push_back(rank);
    }
    const int best = viable.empty() ? -1 : p_.overload_.bestViable(viable, ranks, given, false);
    if (best < 0) return "a dependent-name call with no best candidate, which the replay refuses";
    *chosen = viable[static_cast<std::size_t>(best)];
    return std::string();
}

static const char DeducedCall[] = "a call a function template answers, deduced at the instantiation";

// An argument's value category by its form ([expr.unary.op]/1, [expr.prim.paren]): 1 an lvalue, 0 a prvalue, -1 not decided here.
static int argumentCategory(const syntax::Tree &st, syntax::NodeId a) {
    static const char *const prvalues[] = { "+", "-", "*", "/", "%", "<<", ">>", "<", ">", "<=", ">=", "==", "!=", "&", "|", "^",
                                            "&&", "||" };
    const syntax::Kind k = st.node(a).kind;
    const std::string &op = st.text(a);
    if (k == syntax::Kind::Paren && st.node(a).count == 1) return argumentCategory(st, st.child(a, 0));
    if (k == syntax::Kind::IntLit || k == syntax::Kind::FloatLit || k == syntax::Kind::CharLit) return 0;
    if (k == syntax::Kind::Unary && st.node(a).count == 1) return op == "*" ? 1 : op == "&" || op == "-" || op == "+" || op == "!" || op == "~" ? 0 : -1;
    if (k == syntax::Kind::Member && st.node(a).count == 1)      // `p->m` names an object; `o.m` one where `o` is a name
        return op.compare(0, 2, "->") == 0 || (op.compare(0, 1, ".") == 0 && st.node(st.child(a, 0)).kind == syntax::Kind::Name) ? 1 : -1;
    for (std::size_t q = 0; k == syntax::Kind::Binary && st.node(a).count == 2 && q < sizeof prvalues / sizeof *prvalues; q++)
        if (op == prvalues[q]) return 0;
    return -1;
}

// A call's arguments as deductionArgType gives them, typed under the binding: an lvalue as a reference to its type, a prvalue
// as its type - a local by its record, any other form the typed tree's (Typer::typeUnder); else why not.
std::string Templates::callArgTypes(syntax::NodeId call, const Substituted &s, const std::map<std::uint32_t, std::size_t> *calls,
                                    std::vector<const Type *> *argTypes) {
    const syntax::Tree &st = p_.syntaxTree_;
    for (std::uint32_t c = 1; c < st.node(call).count; c++) {
        const syntax::NodeId a = st.child(call, c);
        const std::unordered_map<std::uint32_t, Parser::Resolved>::const_iterator r = p_.resolutions_.find(a.v);
        const Type *t = nullptr;
        if (st.node(a).kind == syntax::Kind::Name && r != p_.resolutions_.end() && r->second.what == Parser::Resolved::Local && r->second.type != nullptr) {
            t = substitute(s, r->second.type).type;
            t = t->isReference() ? t : p_.types_.referenceTo(t);
        } else if (st.node(a).kind == syntax::Kind::Call) {
            // A call passed is a prvalue of what its record returns: the body's (asked first, its node the lower) or the read's.
            if (calls == nullptr) return DeducedCall;
            const std::map<std::uint32_t, std::size_t>::const_iterator k = calls->find(a.v);
            if (k != calls->end()) t = p_.lookup_.functions_[k->second].returns;
            else if (r != p_.resolutions_.end() && nondependentCall(a).empty() && r->second.what == Parser::Resolved::Call) t = r->second.type->returns();
            if (t == nullptr || t->isReference() || t->unqualified() != t || t->isVoid())
                return "a call a function template may answer over a call that returns a reference, a qualified type or nothing";
        } else {
            // No request's argument holds another but as itself (the case above, ordered by its node), nor any call.
            for (std::vector<syntax::NodeId> work(1, a); !work.empty(); ) {
                const syntax::NodeId id = work.back();
                work.pop_back();
                if (st.node(id).kind == syntax::Kind::Call) return "a call a function template may answer over an argument that holds a call";
                for (std::uint32_t k = 0; k < st.node(id).count; k++) work.push_back(st.child(id, k));
            }
            const int lvalue = argumentCategory(st, a);
            if (lvalue < 0) return "a call a function template may answer over an argument whose value category the read does not decide here";
            Bound under;
            under.s = s;
            bound_[" argument"] = under;
            t = p_.types_.type(Typer(p_).typeUnder(a, " argument"));
            bound_.erase(" argument");
            if (t == nullptr || t->isReference()) return "a call a function template may answer over an argument the typed tree does not type";
            t = lvalue == 1 ? p_.types_.referenceTo(t) : t->unqualified();
        }
        // Every argument's namespaces are searched as the replay's (requestCall: Overload::candidateSet); a class lvalue is ranked
        // there with its converting constructors unasked, and a class prvalue - the temporary it materialises - replays.
        const Type *u = (t->isReference() ? t->referent() : t)->unqualified();
        if (!u->isScalar() && !(u->isStructOrUnion() && t->isReference()))
            return "a call a function template may answer over an array or a class prvalue, which a temporary or a decay would answer";
        argTypes->push_back(t);
    }
    return std::string();
}

// [temp.deduct.call]: the function templates a call reaches, deduced from the bound argument types as the replay's call
// deduces them (deduceTemplateArguments); the specialization is requested once the body's shape is known (requestCall).
std::string Templates::templateCandidates(syntax::NodeId call, const Substituted &s) {
    const syntax::Tree &st = p_.syntaxTree_;
    const std::string written = st.text(st.child(call, 0));
    if (written.find('<') != std::string::npos) return "an explicit-argument call of a function template, which the replay specializes";
    std::vector<const Type *> argTypes;
    const std::string why = callArgTypes(call, s, nullptr, &argTypes);
    if (!why.empty()) return why;      // DeducedCall where an argument is a call: deduced as it is requested
    const std::string::size_type cut = written.rfind("::");
    std::string scope = cut == std::string::npos ? std::string() : written.substr(0, cut), ns;
    if (scope.compare(0, 2, "::") == 0) scope.erase(0, 2);
    const std::vector<TemplateDecl> reached = templatesNamed(cut == std::string::npos ? written : written.substr(cut + 2), scope, &ns);
    for (std::size_t k = 0; k < reached.size(); k++) {
        std::vector<const Type *> binding;
        std::vector<long long> values;
        Packs packs;
        std::string ignored;
        if (deduceTemplateArguments(reached[k], argTypes, &binding, &values, &packs, &ignored)) return DeducedCall;
    }
    return "a call no function template it reaches deduces for, which the replay resolves or refuses";
}

// The specialization a deduced call asks for, requested where and as the replay's call door does (instantiateViableTemplates
// in the instantiation's context), then ranked with the other candidates of the door's index (Overload::bestViable).
std::string Templates::requestCall(const Specialization sp, syntax::NodeId call, const Substituted &s,
                                   const std::map<std::uint32_t, std::size_t> &calls, std::size_t *chosen) {
    const syntax::Tree &st = p_.syntaxTree_;
    const std::string written = st.text(st.child(call, 0));
    const std::string::size_type cut = written.rfind("::");
    const std::string name = cut == std::string::npos ? written : written.substr(cut + 2);
    std::string scope = cut == std::string::npos ? std::string() : written.substr(0, cut), named;
    if (scope.compare(0, 2, "::") == 0) scope.erase(0, 2);
    if (cut == std::string::npos && functionHidesTemplates(name)) return "a call an ordinary function hides the templates of";
    std::vector<const Type *> argTypes;
    const std::string why = callArgTypes(call, s, &calls, &argTypes);
    if (!why.empty()) return why;
    const TemplateDecl &outer = templates_[sp.name];
    std::vector<Shadow> undo;
    bindTemplateParameters(sp.params, sp.binding, sp.values, sp.packs, &undo);
    const std::vector<std::string> wasStack = p_.namespaceStack_;
    if (!outer.ns.empty()) p_.namespaceStack_ = Parser::namespacePathStack(outer.ns);
    const std::string wasKey = p_.instantiationKey_, wasOf = p_.instantiationOf_, wasDisplay = p_.instantiationDisplay_;
    const int wasDepth = p_.instantiationDepth_;
    const bool wasInline = p_.replayingInline_;
    p_.instantiationKey_ = sp.key, p_.instantiationOf_ = sp.name, p_.instantiationDisplay_ = sp.display;
    p_.instantiationDepth_ = sp.depth, p_.replayingInline_ = true;
    const std::vector<TemplateDecl> reached = templatesNamed(name, scope, &named);
    instantiateViableTemplates(reached, argTypes, st.node(st.child(call, 0)).loc);
    const std::string key = p_.templateCallKey(name, named, templates_[name]);
    std::vector<const Type *> given;
    for (std::size_t q = 0; q < argTypes.size(); q++) given.push_back(argTypes[q]->isReference() ? argTypes[q]->referent() : argTypes[q]);
    // The door's set as its resolveOverload looks it up there: the key's, the unnamed namespace's and ADL's ([basic.lookup.argdep]).
    const std::string looked = p_.overload_.lookupName(key, given, nullptr);
    std::vector<std::size_t> merged;
    const std::string unqualified = cut == std::string::npos ? name : std::string();
    const std::vector<std::size_t> *set = memberTemplates_.count(looked) != 0 ? nullptr
                                        : p_.overload_.candidateSet(looked, key, given, nullptr, &merged, unqualified);
    p_.namespaceStack_ = wasStack, p_.replayingInline_ = wasInline;
    p_.instantiationKey_ = wasKey, p_.instantiationOf_ = wasOf, p_.instantiationDisplay_ = wasDisplay, p_.instantiationDepth_ = wasDepth;
    unbindTemplateParameters(undo);
    if (set == nullptr) return "a deduced call whose specialization was not made, which the replay refuses";
    std::vector<std::size_t> viable;
    std::vector<std::vector<Rank> > ranks;
    for (std::size_t k = 0; k < set->size(); k++) {
        const Signature &f = p_.lookup_.functions_[(*set)[k]];
        if (f.params.size() != given.size() || f.variadic) continue;
        // A class candidate is ranked with the rest: a non-const `C &` binds no temporary ([dcl.init.ref]/5), so a scalar
        // reaches it by no converting constructor, nor does a `C` its own class; any other asks one, which the read does not.
        for (std::size_t q = 0; q < f.params.size(); q++) {
            const Type *c = (f.params[q]->isReference() ? f.params[q]->referent() : f.params[q])->unqualified();
            if (c->isStructOrUnion() && c != given[q]->unqualified() &&
                (!f.params[q]->isReference() || f.params[q]->isRValueReference() || f.params[q]->referent()->isConst()))
                return "a deduced call beside a candidate over a class that takes a temporary, whose converting constructors the read does not ask";
        }
        std::vector<Rank> rank;
        bool constructs = false;
        for (std::size_t q = 0; q < given.size() && (rank.empty() || rank.back() != Rank::None); q++) {
            RankedArgument a;
            a.type = given[q]->id();
            a.lvalue = argTypes[q]->isReference();
            a.constructs = [&constructs](const Type *) { return !(constructs = true); };
            rank.push_back(p_.overload_.rankArgument(a, f.params[q]));
        }
        if (constructs) return "a deduced call whose ranking asks a converting constructor, which the read does not ask";
        if (rank.empty() || rank.back() != Rank::None) viable.push_back((*set)[k]), ranks.push_back(rank);
    }
    const int best = viable.empty() ? -1 : p_.overload_.bestViable(viable, ranks, given, false);
    if (best < 0) return "a deduced call with no best candidate, which the replay refuses";
    const Signature &f = p_.lookup_.functions_[viable[static_cast<std::size_t>(best)]];
    // A reference returned is to a scalar (the call an lvalue or an xvalue); a reference parameter binds the lvalue - a class's
    // by `&` alone - directly ([dcl.init.ref]/5: the same type, so no temporary), its address passed as the replay's call does.
    const Type *ret = f.returns->isReference() ? f.returns->referent() : f.returns;
    bool scalars = f.owner.empty() && !f.deleted && p_.overload_.leastArguments(f) == f.params.size() &&
                   (f.returns->isVoid() || ret->isScalar());
    for (std::size_t k = 0; scalars && k < f.params.size(); k++)
        scalars = f.params[k]->isReference() ? argTypes[k]->isReference() && f.params[k]->referent()->unqualified() == given[k]->unqualified() &&
                                                   (given[k]->isScalar() || !f.params[k]->isRValueReference())
                                             : f.params[k]->isScalar();
    if (!scalars) return "a deduced call of a function over a class, a default, or a reference that binds no local as it is";
    *chosen = viable[static_cast<std::size_t>(best)];
    return std::string();
}

// The type a call the instantiation chose records, a member's with `this` first (boundMemberCall); `id` the call's or its callee's node.
const Type *Templates::chosenType(const std::string &binding, std::uint32_t id) const {
    const Bound &b = bound_.at(binding);
    const std::map<std::uint32_t, const Type *>::const_iterator m = b.members.find(id);
    if (m != b.members.end()) return m->second;
    const Signature &f = p_.lookup_.functions_[b.calls.at(id)];
    return p_.types_.functionType(f.returns, f.params, f.variadic);
}

// What the replay's resolution of each call in `body` marks used ([basic.def.odr]/3), made for a body built from the read.
void Templates::callsUsed(syntax::NodeId body, const std::map<std::uint32_t, std::size_t> &calls) {
    for (std::vector<syntax::NodeId> work(1, body); !work.empty(); ) {
        const syntax::NodeId id = work.back();
        work.pop_back();
        for (std::uint32_t c = 0; c < p_.syntaxTree_.node(id).count; c++) work.push_back(p_.syntaxTree_.child(id, c));
        const std::map<std::uint32_t, std::size_t>::const_iterator chosen = calls.find(id.v);
        if (p_.syntaxTree_.node(id).kind == syntax::Kind::Call && chosen != calls.end()) { p_.noteUsed(chosen->second); continue; }
        const std::unordered_map<std::uint32_t, Parser::Resolved>::const_iterator r = p_.resolutions_.find(id.v);
        if (p_.syntaxTree_.node(id).kind != syntax::Kind::Call || r == p_.resolutions_.end() || r->second.what != Parser::Resolved::Call) continue;
        for (std::size_t q = 0; q < p_.lookup_.functions_.size(); q++)
            if (p_.lookup_.functions_[q].symbol == symbolUnder(r->second.symbol)) { p_.noteUsed(q); break; }
    }
}

// What a type parameter bound to `t` reaches: the class or scalar itself, or what an lvalue reference to one refers to.
static const Type *boundTo(const Type *t) { return (t->isReference() ? t->referent() : t)->unqualified(); }

// A struct of the unit, or a class template's specialization the instantiation completed: substShape asks either at the
// member the body reaches - public, a scalar or an array of one, not in a virtual base, an anonymous union or two bases.
std::string Templates::boundClass(const Type &t) const {
    if (t.isArray()) return "an array";
    if (t.isReference()) {
        const Type *r = t.referent()->unqualified();
        const char *to = r->isScalar() ? "a scalar" : r->isArray() ? "an array" : r->isStructOrUnion() ? "a class" : "another type";
        return std::string(t.isRValueReference() ? "an rvalue" : "an lvalue") + " reference to " + to;
    }
    if (t.kind() == Kind::Union) return "a union";
    if (t.isFunction()) return "a function type";
    if (t.kind() != Kind::Struct) return t.isVoid() ? "void" : "a type that is no class";
    if (!t.templateName().empty() && !t.isComplete()) return "a class template's specialization the instantiation has not completed";
    return std::string();
}

// `t`, `*p`, `p[i]` or `(t)` where `t` names a bound class (a reference to one) and `p` a pointer to one: no copy is made of it.
bool Templates::boundObject(syntax::NodeId id, const Substituted &s) const {
    const syntax::Tree &st = p_.syntaxTree_;
    const syntax::Node n = st.node(id);
    if (n.kind == syntax::Kind::Paren && n.count == 1) return boundObject(st.child(id, 0), s);
    const bool deref = (n.kind == syntax::Kind::Unary && st.text(id) == "*" && n.count == 1) || (n.kind == syntax::Kind::Subscript && n.count == 2);
    const syntax::NodeId named = deref ? st.child(id, 0) : id;
    const std::unordered_map<std::uint32_t, Parser::Resolved>::const_iterator r = p_.resolutions_.find(named.v);
    if (st.node(named).kind != syntax::Kind::Name || r == p_.resolutions_.end() || r->second.type == nullptr) return deref;
    const Type *t = substitute(s, r->second.type).type;
    if (t->isReference()) t = t->referent();
    if (deref) t = t->isPointer() ? t->pointee() : nullptr;
    return t != nullptr && t->unqualified()->isStructOrUnion();
}

// `(T)e`, `T(e)` or `static_cast<T>(e)` to an arithmetic type or void, its record the read's under the binding (castRecord),
// over an operand the typed tree types arithmetic: the conversion the lowering makes of any cast ([expr.cast]/4).
std::string Templates::valueCast(syntax::NodeId id, const Substituted &s) {
    const syntax::Tree &st = p_.syntaxTree_;
    const std::string &t = st.text(id);
    const Type *read = p_.castTypes_.at(id.v), *to = substitute(s, read).type;
    const bool named = t.compare(0, 12, "static_cast<") == 0 && t.find('&') == std::string::npos;
    if (st.node(id).count != 1 || !(named || t == read->describe()))
        return "a cast other than a static_cast to a reference, which the read records no type for";
    if (to == nullptr || p_.dependentType(to) || !(to->isVoid() || to->isArithmetic()))
        return "a cast to a type other than an arithmetic type or void under the binding, which the replay converts";
    Bound under;
    under.s = s;
    bound_[" operand"] = under;
    const Type *v = p_.types_.type(Typer(p_).typeUnder(st.child(id, 0), " operand"));
    bound_.erase(" operand");
    if (v == nullptr || !(v->isReference() ? v->referent() : v)->isArithmetic())
        return "a cast over an operand the typed tree does not type arithmetic, which the replay converts";
    if ((v->isReference() ? v->referent() : v)->unqualified() == to->unqualified())
        return "a cast to its operand's own type, which the replay records as its operand";
    return std::string();
}

// `o.m` or `p->m`: its object a name of a bound class (`.`) or of a pointer to one (`->`), or `*p` or `p[i]` of such a
// pointer - that class as the object has it, cv-qualified; else null.
const Type *Templates::boundObjectClass(syntax::NodeId member, const Substituted &s) const {
    const syntax::Tree &st = p_.syntaxTree_;
    const bool arrow = st.text(member).compare(0, 2, "->") == 0;
    if (st.node(member).count != 1) return nullptr;
    syntax::NodeId o = st.child(member, 0);
    while (st.node(o).kind == syntax::Kind::Paren && st.node(o).count == 1) o = st.child(o, 0);
    const syntax::Node on = st.node(o);
    const syntax::NodeId named = !arrow && on.count >= 1 && (on.kind == syntax::Kind::Subscript || (on.kind == syntax::Kind::Unary && st.text(o) == "*"))
                                 ? st.child(o, 0) : o;
    const std::unordered_map<std::uint32_t, Parser::Resolved>::const_iterator r = p_.resolutions_.find(named.v);
    const Type *c = st.node(named).kind == syntax::Kind::Name && r != p_.resolutions_.end() && r->second.type != nullptr
                    ? substitute(s, r->second.type).type : nullptr;
    if (c != nullptr && c->isReference()) c = c->referent();
    if (c != nullptr && (arrow || named.v != o.v)) c = c->isPointer() ? c->pointee() : nullptr;
    for (std::size_t q = 0; c != nullptr && q < s.to.size(); q++)
        if (c->unqualified() == boundTo(s.to[q])) return c;
    return nullptr;
}

// [over.match.call]/3: `o.f(x)` or `p->f(x)` over a bound class, ranked over the class's members of that name as the replay's
// memberCallWith ranks them; `full` the record's function type, `this` first - a base's function on the base, as it converts.
std::string Templates::boundMemberCall(syntax::NodeId call, const Substituted &s, const std::map<std::uint32_t, std::size_t> &calls,
                                       std::size_t *chosen, const Type **full) {
    const syntax::Tree &st = p_.syntaxTree_;
    const syntax::NodeId callee = st.child(call, 0);
    const Type *obj = boundObjectClass(callee, s);
    const Type *plain = obj->unqualified();
    const std::string &t = st.text(callee);
    const std::string name = t.substr(t.compare(0, 2, "->") == 0 ? 2 : 1);
    if (name.empty() || name.find_first_of(":~<> ") != std::string::npos || p_.memberTemplateOwner(plain, name) != nullptr)
        return "a member call on a bound class by a qualified, operator, destructor or template name, which the replay looks up";
    const Type *owner = p_.lookup_.findMemberOwner(plain, name);
    const std::vector<std::size_t> *set = owner != nullptr ? p_.lookup_.overloadsOf(owner->tag() + "::" + name) : nullptr;
    if (set == nullptr || set->empty()) return "a member call on a bound class that names no member function of it";
    if (owner != plain) {
        int paths = 0;
        bool moved = false;     // `this` converted to a base at a nonzero offset: the replay lays a `.bp` slot for it, the read none
        for (std::size_t q = 0; q < plain->bases().size(); q++) {
            const Type::BaseSpec &b = plain->bases()[q];
            if (b.type->unqualified() == owner) paths += b.direct && !b.isVirtual && b.access == Access::Public && !plain->hasVirtualBase() ? 1 : 2;
            moved = moved || (b.type->unqualified() == owner && b.offset != 0);
        }
        if (paths != 1) return "a member call on a bound class of a function its base declares other than as one public, non-virtual direct base";
        if (moved) return "a member call on a bound class of a function a base at a nonzero offset declares, whose conversion the replay lays a slot for";
    }
    std::vector<const Type *> argTypes, given;
    const std::string why = callArgTypes(call, s, &calls, &argTypes);
    if (!why.empty()) return "a member call on a bound class over an argument the read does not type here";
    for (std::size_t q = 0; q < argTypes.size(); q++) {
        given.push_back((argTypes[q]->isReference() ? argTypes[q]->referent() : argTypes[q])->unqualified());
        if (!given.back()->isScalar()) return "a member call on a bound class over a class argument, whose conversions the read does not ask";
    }
    std::vector<std::size_t> viable;
    std::vector<std::vector<Rank> > ranks;
    for (std::size_t k = 0; k < set->size(); k++) {
        const Signature &f = p_.lookup_.functions_[(*set)[k]], &first = p_.lookup_.functions_[(*set)[0]];
        bool scalars = !f.variadic && !f.deleted && f.owner == owner->tag() && p_.overload_.leastArguments(f) == f.params.size() &&
                       f.constThis == first.constThis && f.refThis == RefQual::None && (f.returns->isVoid() || f.returns->isScalar());
        for (std::size_t q = 0; scalars && q < f.params.size(); q++) scalars = f.params[q]->isScalar();
        if (!scalars)
            return "a member call on a bound class beside a candidate over a class, a reference, a default, variadic arguments or another object qualifier";
        if (f.params.size() != given.size()) continue;
        std::vector<Rank> rank;
        for (std::size_t q = 0; q < given.size() && (rank.empty() || rank.back() != Rank::None); q++) {
            RankedArgument a;
            a.type = given[q]->id();
            a.lvalue = argTypes[q]->isReference();
            rank.push_back(p_.overload_.rankArgument(a, f.params[q]));
        }
        if (rank.empty() || rank.back() != Rank::None) viable.push_back((*set)[k]), ranks.push_back(rank);
    }
    const int best = viable.empty() ? -1 : p_.overload_.bestViable(viable, ranks, given, false);
    if (best < 0) return "a member call on a bound class with no best candidate, which the replay refuses";
    const Signature &f = p_.lookup_.functions_[viable[static_cast<std::size_t>(best)]];
    if (f.access != Access::Public) return "a bound class's member function other than public, whose access the replay checks";
    if (f.isStaticMember) return "a bound class's static member function called through its object, which the replay evaluates apart";
    if (f.isVirtual) return "a bound class's virtual member function, which the replay calls through its slot";
    if (obj->isConst() && !f.constThis) return "a bound class's non-const member function on a const object, which the replay refuses";
    std::vector<const Type *> params(1, p_.types_.pointerTo(f.constThis ? p_.types_.withConst(owner) : owner));
    params.insert(params.end(), f.params.begin(), f.params.end());
    *full = p_.types_.functionType(f.returns, params, f.variadic);
    *chosen = viable[static_cast<std::size_t>(best)];
    return std::string();
}

std::string Templates::substShape(syntax::NodeId body, const Substituted &s, std::map<std::uint32_t, std::size_t> *calls,
                                  std::map<std::uint32_t, const Type *> *members, std::map<std::uint32_t, std::pair<std::string, const Type *> > *assigns,
                                  std::vector<syntax::NodeId> *requests) {
    typedef syntax::Kind Kind;
    std::vector<std::uint32_t> assigned;      // the class-typed members an assignment below calls operator= over
    bool classBound = false;
    for (std::size_t q = 0; q < s.to.size(); q++) classBound = classBound || boundTo(s.to[q])->isStructOrUnion();
    // Pass 1 the shapes the slice covers; pass 2 what the read records apart from any replay, refused at parse time (m82g).
    for (int pass = 1; pass <= 2; pass++)
    for (std::vector<syntax::NodeId> work(1, body); !work.empty(); ) {
        const syntax::NodeId id = work.back();
        work.pop_back();
        const syntax::Node n = p_.syntaxTree_.node(id);       // a copy: a reference to `Node` reads as Ast residue
        for (std::uint32_t c = 0; c < n.count; c++) work.push_back(p_.syntaxTree_.child(id, c));
        if ((n.marks & syntax::Unfound) != 0) return "a name no scope declares";
        if (pass == 1 && classBound) {
            // A bound class is reached, never copied: no operator, assignment or return over its object, no local of a class by value.
            const bool op = n.kind == Kind::Binary || n.kind == Kind::Assign || n.kind == Kind::Postfix || n.kind == Kind::Return ||
                            (n.kind == Kind::Unary && p_.syntaxTree_.text(id) != "&");
            for (std::uint32_t c = 0; op && c < n.count; c++)
                if (boundObject(p_.syntaxTree_.child(id, c), s)) return "an operator, an assignment or a return over a class's object";
            const std::unordered_map<std::uint32_t, const Type *>::const_iterator v =
                n.kind == Kind::Var ? p_.declaredTypes_.find(id.v) : p_.declaredTypes_.end();
            const Type *by = v != p_.declaredTypes_.end() ? substitute(s, v->second).type : nullptr;
            // `const B &r = t`, B a base of the bound class at a nonzero offset: the replay's conversion lays a `.bp` slot the read does not.
            const Type *at = by != nullptr && (by->isReference() || by->isPointer()) ? (by->isReference() ? by->referent() : by->pointee())->unqualified() : nullptr;
            for (std::size_t q = 0; at != nullptr && at->isStructOrUnion() && q < s.to.size(); q++)
                for (std::size_t k = 0; boundTo(s.to[q])->isStructOrUnion() && k < boundTo(s.to[q])->bases().size(); k++)
                    if (boundTo(s.to[q])->bases()[k].type->unqualified() == at && boundTo(s.to[q])->bases()[k].offset != 0) return "a reference or pointer to a bound class's base at a nonzero offset, whose conversion the replay lays a slot for";
            while (by != nullptr && by->isArray()) by = by->pointee();
            if (by != nullptr && by->unqualified()->isStructOrUnion()) return "a local or a parameter of a class by value, which its constructor builds";
        }
        if (pass == 1) {
            switch (n.kind) {
            case Kind::Member: {
                // The read records no member of a bound class: the Typer makes it from the class (Typer::boundMember), as the replay's.
                const std::string &t = p_.syntaxTree_.text(id);
                const bool arrow = t.compare(0, 2, "->") == 0;
                const std::string name = t.substr(arrow ? 2 : 1);
                if (p_.resolutions_.count(id.v) != 0 || calls->count(id.v) != 0) continue;     // a member call's callee: boundMemberCall's
                const Type *c = boundObjectClass(id, s);
                if (c == nullptr || name.empty() || name.find_first_of(":~< ") != std::string::npos || c->unqualified()->findMember(name) == nullptr)
                    return "a member that is no data member of a bound class reached by name";
                const Member &m = *c->unqualified()->findMember(name);
                const Type *e = m.type;
                while (e != nullptr && e->isArray()) e = e->pointee();
                if (m.access != Access::Public) return "a bound class's member other than public, whose access the replay checks";
                if (e == nullptr || (!e->isScalar() && std::find(assigned.begin(), assigned.end(), id.v) == assigned.end()))
                    return "a bound class's member other than a scalar or an array of one, which an operator over it would copy";
                if (m.inVirtualBase != nullptr || m.anonUnion != nullptr || m.ambiguous)
                    return "a bound class's member in a virtual base, an anonymous union or two bases, which the replay reaches apart";
                continue;
            }
            case Kind::Assign: {
                // `a.m = b.m` of a class-typed member: the operator= the replay's assignValue calls (copyAssignFor), or its bytes.
                const Type *to[2] = { nullptr, nullptr };
                for (std::uint32_t c = 0; c < 2 && n.count == 2; c++) {
                    const syntax::NodeId o = p_.syntaxTree_.child(id, c);
                    const std::string &t = p_.syntaxTree_.text(o);
                    const Type *k = p_.syntaxTree_.node(o).kind == Kind::Member ? boundObjectClass(o, s) : nullptr;
                    const std::string name = k != nullptr && t.size() > 1 ? t.substr(t.compare(0, 2, "->") == 0 ? 2 : 1) : std::string();
                    const Member *m = k != nullptr && !name.empty() ? k->unqualified()->findMember(name) : nullptr;
                    if (m != nullptr && m->type->unqualified()->isStructOrUnion()) to[c] = k->isConst() ? p_.types_.withConst(m->type) : m->type;
                }
                if (to[0] == nullptr && to[1] == nullptr) continue;
                if (p_.syntaxTree_.text(id) != "=" || to[0] == nullptr || to[1] == nullptr || to[0]->unqualified() != to[1]->unqualified() ||
                    to[0]->isConst())
                    return "an assignment over a bound class's class-typed member other than `a.m = b.m` of one class, which the replay converts";
                const Type *cls = to[0]->unqualified();
                const Signature *op = p_.copyAssignFor(cls, false);     // an lvalue member is copied
                if (op != nullptr && (op->access != Access::Public || op->deleted || op->params.empty()))
                    return "a class-typed member's operator= other than public, which the replay checks";
                if (op != nullptr) (*assigns)[p_.syntaxTree_.child(id, 1).v] = std::make_pair(op->symbol, p_.assignCallType(cls, *op));
                assigned.push_back(p_.syntaxTree_.child(id, 0).v), assigned.push_back(p_.syntaxTree_.child(id, 1).v);
                continue;
            }
            case Kind::IntLit: case Kind::FloatLit: case Kind::CharLit: case Kind::Name: case Kind::Unary: case Kind::Postfix:
            case Kind::Binary: case Kind::Conditional: case Kind::Comma: case Kind::Paren: case Kind::Subscript: case Kind::This:
            case Kind::Block: case Kind::ExprStmt: case Kind::DeclStmt: case Kind::Return: case Kind::If: case Kind::While:
            case Kind::Do: case Kind::For: case Kind::Break: case Kind::Continue: case Kind::Null: case Kind::Var: case Kind::Function:
                continue;
            case Kind::Call: {
                if (nondependentCall(id).empty()) continue;
                std::size_t chosen = 0;
                const syntax::NodeId callee = n.count != 0 ? p_.syntaxTree_.child(id, 0) : syntax::NodeId();
                if (callee.valid() && p_.syntaxTree_.node(callee).kind == Kind::Member && boundObjectClass(callee, s) != nullptr) {
                    const Type *full = nullptr;
                    const std::string why = boundMemberCall(id, s, *calls, &chosen, &full);
                    if (!why.empty()) return why;
                    (*calls)[id.v] = (*calls)[callee.v] = chosen;
                    (*members)[id.v] = (*members)[callee.v] = full;
                    continue;
                }
                const std::string why = dependentCall(id, s, &chosen);
                if (why == DeducedCall) { requests->push_back(id); continue; }
                if (!why.empty()) return why;
                (*calls)[id.v] = (*calls)[p_.syntaxTree_.child(id, 0).v] = chosen;
                continue;
            }
            case Kind::Cast: {
                // A static_cast to a reference names its operand (Parser::staticCast): a name of the referent's type, as std::forward's.
                const std::unordered_map<std::uint32_t, std::pair<const Type *, bool> >::const_iterator c = p_.refCastTypes_.find(id.v);
                const syntax::NodeId o = n.count == 1 ? p_.syntaxTree_.child(id, 0) : syntax::NodeId();
                const std::unordered_map<std::uint32_t, Parser::Resolved>::const_iterator r = o.valid() ? p_.resolutions_.find(o.v) : p_.resolutions_.end();
                if (c == p_.refCastTypes_.end() && p_.castTypes_.count(id.v) != 0) {
                    const std::string why = valueCast(id, s);
                    if (!why.empty()) return why;
                    continue;
                }
                if (p_.syntaxTree_.text(id).compare(0, 12, "static_cast<") != 0 || c == p_.refCastTypes_.end())
                    return "a cast other than a static_cast to a reference, which the read records no type for";
                if (p_.dependentType(castUnder(s, c->second).first)) return "a static_cast to a member type of a class the binding has not made";
                if (p_.syntaxTree_.node(o).kind != Kind::Name || r == p_.resolutions_.end() || r->second.type == nullptr ||
                    valueUnder(s, r->second.type)->unqualified() != castUnder(s, c->second).first->unqualified())
                    return "a static_cast to a reference over another type, which the replay converts";
                continue;
            }
            default:
                return std::string("a '") + syntax::kindName(n.kind) + "' in the body";
            }
        }
        bool asked = false;
        for (std::size_t q = 0; q < requests->size(); q++) asked = asked || (*requests)[q].v == id.v;
        if (calls->count(id.v) != 0 || asked) continue;       // the read's record of a dependent-name call is no record: dependentCall's or requestCall's is
        const std::unordered_map<std::uint32_t, Parser::Resolved>::const_iterator r = p_.resolutions_.find(id.v);
        if (r != p_.resolutions_.end() && r->second.type != nullptr && p_.dependentType(substitute(s, r->second.type).type))
            return std::string("a '") + syntax::kindName(n.kind) + "' the read resolved to a type the binding does not carry";
        if (n.kind == Kind::Unary && p_.syntaxTree_.text(id) == "+" && n.count == 1 &&
            (p_.syntaxTree_.node(p_.syntaxTree_.child(id, 0)).marks & syntax::TypeDependent) != 0)
            return "a unary '+' over a dependent name, which the replay records as its operand";     // unaryNode hands `v` on
        const std::unordered_map<std::uint32_t, const Type *>::const_iterator v = p_.declaredTypes_.find(id.v);
        const Type *declared = v != p_.declaredTypes_.end() ? substitute(s, v->second).type : nullptr;
        if (n.kind == Kind::Var && declared != nullptr && (p_.dependentType(declared) ||     // a member pointer `substituted` keeps as written
            (declared->unqualified()->isMemberPointer() && p_.dependentType(declared->unqualified()->pointee()))))
            return "a 'var' declared with a type the binding does not carry";
    }
    return std::string();
}

static const char *const doorNames[Templates::Doors] = { "function", "class", "member", "member-template", "dependent-base",
                                                         "sfinae-trial", "sfinae-failed", "alias", "constexpr-ahead", "by-substitution" };

std::string Templates::censusLine() const {
    std::string out = "template-census:";
    for (int d = 0; d < Doors; d++) out += std::string(" ") + doorNames[d] + "=" + std::to_string(census_[d]);
    return out;
}

void Templates::countWhy(Door d, const std::string &why) {
    count(d);
    fallbackCensus_[std::string(doorNames[d]) + ": " + why]++;
}

// R0: a jump with no door is no template's (an ordinary class's held bodies); a bind with none is counted as "other".
void Templates::reentered(const char *what, bool standIn, std::size_t site) const {
    if (!substitute_) return;
    const bool bind = std::string(what) == "bind";
    if (door_ == nullptr && !bind) return;
    const std::string door = door_ == nullptr ? "other" : door_;
    const bool read = standIn || door == "read";
    reentries_[std::string(read ? "read " : "instantiation ") + door + ": " + what]++;
    if (!read && bind && site != static_cast<std::size_t>(-1)) reenteredAt_[site].insert(door);
}

void Templates::countDefault(const TemplateParam &p, bool deduced) const {
    if (p.defBegin == 0) return;
    deducedDefaults_ += deduced ? 1 : 0;
    if (defaultMade_) {
        defaultReport_[std::string("made: a type") + (deduced ? ", deduced" : ", the class door's list")]++;
        defaultMade_ = false;
        return;
    }
    fallbackCensus_[std::string("default: ") + (p.isTemplate ? "a template" : p.type == nullptr ? "a type" : "a value") +
                    (deduced ? ", deduced" : ", the class door's list")]++;
}

const Type *Templates::defaultMade(const TemplateDecl &decl, std::size_t i, const std::vector<const Type *> &binding) {
    const TemplateParam &p = decl.params[i];
    const char *why = p.defPattern == nullptr ? (p.defUnread != nullptr ? p.defUnread : "not read at its declaration") : nullptr;
    if (why == nullptr && decl.isMember) why = "a member template's, its class's parameters numbered apart";
    Substituted s;
    for (std::size_t k = 0; why == nullptr && k < i; k++) {
        if (binding[k] == nullptr) why = "a parameter before it unbound";
        s.from.push_back(p_.types_.templateParam(static_cast<int>(k)));
        s.to.push_back(binding[k]);
    }
    const Type *made = nullptr;
    if (why == nullptr) {
        const Substitution r = substitute(s, p.defPattern, At{ p.defBegin });
        made = r.type, why = r.why;
    }
    if (why != nullptr) defaultReport_[std::string("replayed: a type: ") + why]++;
    return why == nullptr ? made : nullptr;
}

bool Templates::defaultHeld(const Type *made, const Type *replayed) {
    if (made == replayed) return true;
    defaultReport_["replayed: a type: differ: made " + made->describe() + ", replayed " +
                   (replayed != nullptr ? replayed->describe() : std::string("nothing"))]++;
    return false;
}

void Templates::countAhead(const std::string &tag, bool constructor) {
    const std::map<std::string, std::string>::const_iterator c = specializationCensus_.find(tag);
    countWhy(ConstexprAhead, std::string(constructor ? "a constexpr constructor" : "a constexpr member function") + ", its class " +
                                 (c == specializationCensus_.end() ? "not in the census" : c->second.compare(0, 4, "made") == 0 ? "made from the read" : "replayed"));
}

// What a kept pattern signature holds beyond types the binding carries: 1 a `decltype`, 2 a value over the parameters.
static int patternHolds(const Type *t, int depth = 0) {
    int held = 0;
    for (; t != nullptr && depth < 64; depth++) {
        if (t->kind() == Kind::DependentExpr) return held | 1;
        if (t->unqualified() != t) { t = t->unqualified(); continue; }
        if (t->kind() == Kind::Function) {
            for (const Type *q : t->params()) held |= patternHolds(q, depth + 1);
            t = t->returns();
            continue;
        }
        if (t->isStructOrUnion()) {
            for (std::size_t i = 0; t->isSpecialization() && i < t->templateArgs().size(); i++) {
                const TemplateArg &a = t->templateArgs()[i];
                if (!a.isType && (a.isParam || a.dependentValue || a.expr != nullptr)) held |= 2;
                if (a.isType) held |= patternHolds(a.type, depth + 1);
                for (const Type *m : a.pack) held |= patternHolds(m, depth + 1);
            }
            return held;
        }
        if (t->kind() == Kind::DependentMember || t->kind() == Kind::PackExpansion || t->isPointer() || t->isReference() || t->isArray()) {
            t = t->pointee();
            continue;
        }
        return held;
    }
    return held;
}

std::string Templates::trialShape(const TemplateDecl &decl, bool defaulted) const {
    if (decl.pattern == nullptr) return "no pattern signature kept";
    const int held = patternHolds(decl.pattern);
    return (held & 1) != 0 ? "a 'decltype'" : (held & 2) != 0 ? "a value over the parameters" : defaulted ? "a default" : "types alone";
}

void Templates::holdBodies(std::size_t mark) {
    if (!checkBodies_) return;
    const std::size_t made = p_.syntaxTree_.size();
    if (bodySeen_.size() < made + 1) bodySeen_.resize(made + 1, 0);
    for (std::size_t i = mark + 1; i <= made; i++) {
        const syntax::NodeId id(static_cast<std::uint32_t>(i));
        const std::uint32_t parts = p_.syntaxTree_.node(id).count, at = p_.syntaxTree_.node(id).loc;
        if (p_.syntaxTree_.node(id).kind != syntax::Kind::Function || p_.syntaxTree_.node(id).dead || bodySeen_[i] != 0) continue;
        // A declaration has no block; a function try block's body is its `try`.
        bool body = false;
        for (std::uint32_t c = 0; c < parts; c++) {
            const syntax::Kind k = p_.syntaxTree_.node(p_.syntaxTree_.child(id, c)).kind;
            body = body || k == syntax::Kind::Block || k == syntax::Kind::Try;
        }
        if (!body) continue;
        bodySeen_[i] = 1;
        // The signature kept at the declaration, against this replay's parameters: one `var` for one.
        std::uint32_t vars = 0;
        while (vars < parts && p_.syntaxTree_.node(p_.syntaxTree_.child(id, vars)).kind == syntax::Kind::Var) vars++;
        for (std::map<std::string, std::vector<TemplateDecl> >::const_iterator t = fnTemplates_.begin(); t != fnTemplates_.end(); ++t)
            for (std::size_t k = 0; k < t->second.size(); k++)
                if (t->second[k].signature.valid() && p_.syntaxTree_.node(t->second[k].signature).loc == at) {
                    signatureTally_[0]++;
                    signatureTally_[1] += p_.syntaxTree_.node(t->second[k].signature).count == vars;
                }
        const std::map<std::uint32_t, syntax::NodeId>::const_iterator held = bodyOnce_.find(at);
        if (held == bodyOnce_.end()) { bodyOnce_[at] = id; bodyTally_[0]++; continue; }
        const int d = p_.syntaxTree_.differs(held->second, id);
        bodyTally_[1 + d]++;
        if (d == 2) bodyShapes_.insert(at);
    }
}

static const char *const readDoorNames[] = { "functions", "class-members", "out-of-line", "partial-members", "aliases", "member-templates" };

// One read's verdict: a body's `function` node with a block made after `mark`, or the reason there is none - noted at token `at`.
void Templates::tallyRead(ReadDoor door, std::size_t mark, std::string why, bool wantsBody, std::size_t at) {
    syntax::NodeId read;
    for (std::size_t i = p_.syntaxTree_.size(); wantsBody && why.empty() && i > mark && !read.valid(); i--) {
        const syntax::NodeId id(static_cast<std::uint32_t>(i));
        if (p_.syntaxTree_.node(id).kind != syntax::Kind::Function) continue;
        for (std::uint32_t c = 0; c < p_.syntaxTree_.node(id).count; c++)
            if (p_.syntaxTree_.node(p_.syntaxTree_.child(id, c)).kind == syntax::Kind::Block) read = id;
    }
    if (wantsBody && why.empty() && !read.valid()) why = "the read made no function with a block";
    if (!why.empty()) {
        const std::string named = why;     // the place's note keeps the names; the census's one reason for every name takes them out
        for (std::size_t a = why.find('\''), b; a != std::string::npos && (b = why.find('\'', a + 1)) != std::string::npos; a = why.find('\'', a + 2))
            why.replace(a + 1, b - a - 1, "");
        declUnread_[door][why.substr(0, 100)]++;
        p_.syntaxTree_.noteUnread(p_.tokens_[at < p_.tokens_.size() ? at : 0].pos, std::string(readDoorNames[door]) + ": " + named.substr(0, 160));
        return;
    }
    if (wantsBody) declRead_[door].push_back(read);
    if (wantsBody && door == ReadMember) memberTemplateRead_.insert(at);
    if (wantsBody && (door == ReadFunction || (substitute_ && (door == ReadClass || door == ReadPartial)))) readBody_[at] = read;
    if (wantsBody) p_.syntaxTree_.noteRead(read);
    else declTally_[door][0]++;
}

void Templates::readDeclaration(const TemplateDecl &decl, ReadDoor door, std::size_t which) {
    // The class doors read with no flag, as the function door (commit 5), and never from inside a read.
    if (door != ReadFunction && p_.patternUses_) return;
    const DoorAt reading(this, "read");
    const bool partial = door == ReadPartial;
    const std::vector<TemplateParam> &params = partial ? decl.partials[which].params : decl.params;
    std::string why;
    std::vector<const Type *> binding;
    for (std::size_t i = 0; i < params.size(); i++) {
        binding.push_back(p_.types_.templateParam(static_cast<int>(i)));
    }
    const std::size_t mark = p_.syntaxTree_.size(), resume = p_.at_;
    bool tallied = false;
    // What the read tallies, as it stood: a read made again (makeWanted) is counted once.
    const bool outermost = !p_.patternUses_;
    std::vector<std::size_t> hadRead;
    std::vector<long> hadTally;
    std::vector<std::map<std::string, int> > hadUnread;
    for (int d = 0; outermost && d < ReadDoors; d++) {
        hadRead.push_back(declRead_[d].size());
        hadTally.push_back(declTally_[d][0]);
        hadUnread.push_back(declUnread_[d]);
    }
    const std::pair<std::size_t, std::size_t> hadNotes = p_.syntaxTree_.noted();
    if (outermost) { wanted_.clear(); held_.clear(); }
    {
        // The read's own layer: its signature, its typedefs and a function specialization it asks for are found in it, and go with it.
        Lookup::Layer layer = p_.lookup_.pushLayer();
        const std::size_t hadSpecializations = specializations_.size();
        const int hadTemps = p_.refTemps_;      // its temporaries' numbers too: a `$chain4` label is the replay's to number
        const std::map<std::string, std::size_t> hadMemberTemplates = memberTemplateMade_;
        const std::map<std::string, std::vector<Owed> > hadOwed = owedBodies_;
        // The pattern is no function: what its read writes - definition, static locals, literals - is a program of its own.
        Program pattern;
        Program *const into = p_.current_, *const wasUnit = p_.unit_;
        pattern.module.data.dialect = into->module.data.dialect;
        struct Back { Parser *p; Program *into, *unit; ~Back() { p->current_ = into; p->unit_ = unit; } } back = { &p_, into, wasUnit };
        if (p_.unit_ == nullptr) p_.unit_ = into;
        const Parser::FunctionState outer = p_.captureFunctionState();
        const std::vector<std::string> wasStack = p_.namespaceStack_;
        // An instantiation leaves its function parameter pack's written name in packs_ (packParameter): `args` there is not this read's.
        const std::map<std::string, PackBinding> wasPacks = p_.packs_;
        p_.packs_.clear();
        const bool wasBody = p_.patternBody_, wasUses = p_.patternUses_;
        // What a class's read sets and a refusal thrown through it leaves set.
        const std::vector<const Type *> wasClasses = p_.classStack_;
        const Type *const wasClass = p_.currentClass_;
        const std::string wasInline = p_.inlineOwner_, wasTag = p_.classInstantiationTag_, wasOf = p_.classInstantiationOf_;
        const std::string wasNamespace = p_.instantiatingNamespace_;
        const auto wasArgs = p_.instantiatingArgs_;
        const auto wasParams = p_.instantiatingParams_;
        const auto wasBinding = p_.instantiatingBinding_;
        const auto wasValues = p_.instantiatingValues_;
        const auto wasHeld = p_.heldForSpecialization_;
        const bool wasDeferring = p_.deferSpecializationBodies_, wasReplaying = p_.replayingInline_, wasInArgs = p_.inTemplateArgs_;
        std::vector<Shadow> undo;
        p_.clearFunctionState();
        Type::nameDependentSizes(true);
        classDoor_ = door != ReadFunction;
        try {
            Parser::Trial trial(&p_);
            const std::vector<long long> values(binding.size(), 0);
            if (door == ReadFunction || door == ReadAlias || door == ReadMember) bindTemplateParameters(params, binding, values, Packs(), &undo);
            p_.patternOnly_ = p_.patternBody_ = p_.patternUses_ = true;
            if (!decl.ns.empty()) p_.namespaceStack_ = Parser::namespacePathStack(decl.ns);
            p_.current_ = &pattern;
            if (door == ReadFunction) {
                p_.at_ = decl.afterParams;
                reentered("jump: a function template's definition");
                struct LogBack { Parser *p; std::vector<FrameEvent> *was; ~LogBack() { p->frameLog_ = was; } } logBack = { &p_, p_.frameLog_ };
                if (substitute_ && outermost) { readFrames_[decl.afterParams].clear(); p_.frameLog_ = &readFrames_[decl.afterParams]; }
                p_.topLevel(pattern);
                if (substitute_ && outermost && !pattern.functions.empty()) keepRead(decl.afterParams, pattern.functions.back());     // the definition: what else the read writes is data
            } else if (door == ReadMember) {
                const std::vector<TemplateArg> args = argumentsFromBinding(params, binding, values, Packs(params.size()));
                const std::string display = specializationKey(decl.isConstructor ? decl.ownerLocal : decl.name, args);
                replayMemberTemplate(decl, display, decl.ownerTag + "::" + display, args, nullptr, false);
            } else if (door == ReadAlias) {
                p_.at_ = decl.aliasAt;
                reentered("jump: an alias's type-id");
                Parser::StorageClass sc;
                Parser::Qualifiers quals;
                const Type *base = p_.specifiers(&sc, &quals);
                p_.declarator(base, true);
            } else {
                // The class over its own parameters, under a tag of this read alone; `C<T>` finds it while the read lasts.
                const Packs packs(params.size());
                const std::vector<TemplateArg> args = argumentsFromBinding(params, binding, values, packs);
                // Keyed as its own members write it: `Box<T, N>` is `Box<$T0,$P1>`, `Counted<Ns...>` is `Counted<{$T0...}>`.
                std::vector<TemplateArg> written = args;
                for (std::size_t i = 0; i < written.size(); i++)
                    if (params[i].isPack) { written[i].pack.assign(1, binding[i]); written[i].expandsParam = static_cast<int>(i); }
                    else if (!written[i].isType) { written[i].isParam = true; written[i].paramIndex = static_cast<int>(i); }
                const std::string tag = specializationKey(decl.name, written);
                std::vector<Parser::PendingBody> bodies;
                const std::string readTag = tag + "$read" + std::to_string(readSerial_++);
                // From its head on, not from its end, so a member's declaration and its replayed definition see one class;
                // a partial's key is its written arguments over its own parameters (`pair<const $T0,$T1>`), none past a pack.
                std::string own = tag;
                if (partial) {
                    std::vector<TemplateArg> written;
                    for (const TemplateDecl::Partial::Arg &w : decl.partials[which].args) {
                        TemplateArg a;
                        a.isType = w.isType; a.type = w.type; a.isParam = w.isParam;
                        a.paramIndex = static_cast<int>(w.param); a.value = w.value;
                        if (w.isPackExpansion) own.clear();
                        written.push_back(a);
                    }
                    if (!own.empty()) own = specializationKey(decl.name, written);
                }
                if (!own.empty()) { readingTag_ = own; readingRead_ = readTag; }
                const Type *made = readClassBody(decl, readTag, partial, partial ? decl.partials[which].bodyAt : decl.afterParams,
                                                 args, params, binding, values, packs, &bodies, &undo);
                if (!own.empty()) readPattern_[own] = made;
                // Slice 3b: its member functions too, the read's tag beside them, before the layer they are declared in pops.
                if (substitute_ && outermost && door != ReadOutOfLine) {
                    const std::size_t key = partial ? decl.partials[which].bodyAt : decl.afterParams;
                    readClass_[key] = made;
                    readOwn_[key] = own;
                    if (partial) readPartials_.insert(made);
                    std::pair<std::string, std::vector<Signature> > &kept = readMembers_[key];
                    kept.first = readTag;
                    kept.second.clear();
                    for (std::size_t f = layer.sizes.functions; f < p_.lookup_.functions_.size(); f++)
                        if (p_.lookup_.functions_[f].owner == readTag) kept.second.push_back(p_.lookup_.functions_[f]);
                    std::vector<std::pair<Parser::PendingBody, std::string> > &held = readBodies_[key];
                    held.clear();
                    for (const Parser::PendingBody &b : bodies)
                        held.push_back(std::make_pair(b, b.which < p_.lookup_.functions_.size() ? p_.lookup_.functions_[b.which].symbol : std::string()));
                    readTypedefs_[key] = memberTypedefs(readTag, decl.name, layer.sizes.typedefs);
                }
                p_.replayingInline_ = true;
                if (door == ReadOutOfLine) {
                    p_.at_ = decl.outOfLine[which].start;
                    reentered("jump: a member defined out of line");
                    p_.topLevel(pattern);
                }
                // Each held member body is a read of its own: one refused leaves the others read.
                for (std::size_t b = 0; door != ReadOutOfLine && b < bodies.size(); b++) {
                    const std::size_t from = p_.syntaxTree_.size();
                    std::string refused;
                    // Its frame steps logged as the replay logs a member's, inside replayInlineBodies' own frame.
                    struct FramesBack { Parser *p; std::map<std::size_t, std::vector<FrameEvent> > *was; ~FramesBack() { p->bodyFrames_ = was; } } framesBack = { &p_, p_.bodyFrames_ };
                    if (substitute_ && outermost) { readFrames_[bodies[b].start].clear(); p_.bodyFrames_ = &readFrames_; }
                    const std::size_t fnsHad = pattern.functions.size();
                    try {
                        Parser::Trial one(&p_);
                        p_.replayInlineBodies(std::vector<Parser::PendingBody>(1, bodies[b]));
                        if (substitute_ && outermost && pattern.functions.size() > fnsHad) keepRead(bodies[b].start, pattern.functions.back());
                    } catch (const SubstitutionFailure &f) {
                        refused = f.why;
                    }
                    p_.current_ = &pattern;
                    tallyRead(door, from, refused, true, bodies[b].start);
                }
                if (door != ReadOutOfLine) readMemberTemplates(readTag, params.size(), &pattern);
                tallied = door != ReadOutOfLine;
            }
        } catch (const SubstitutionFailure &f) {
            why = (door == ReadFunction || door == ReadAlias || door == ReadMember || tallied ? "" : "the class: ") + f.why;
            tallied = false;
        }
        readPattern_.clear();
        readingTag_.clear();
        // A pattern class the read was first to ask for keeps no arguments of the read's: the unit's first asker gives its own.
        for (std::size_t s = 0; s < readShallow_.size(); s++) {
            readIds_[readShallow_[s]] = ReadId{ readShallow_[s]->templateName(), readShallow_[s]->templateArgs() };     // for the class door
            readShallow_[s]->setSpecialization(std::string(), std::vector<TemplateArg>());
            readShallow_[s]->setTemplateNamespace(std::string());
        }
        readShallow_.clear();
        classDoor_ = p_.readLayout_ = false;
        p_.classStack_ = wasClasses;
        p_.currentClass_ = wasClass;
        p_.inlineOwner_ = wasInline;
        p_.classInstantiationTag_ = wasTag;
        p_.classInstantiationOf_ = wasOf;
        p_.instantiatingNamespace_ = wasNamespace;
        p_.instantiatingArgs_ = wasArgs;
        p_.instantiatingParams_ = wasParams;
        p_.instantiatingBinding_ = wasBinding;
        p_.instantiatingValues_ = wasValues;
        p_.heldForSpecialization_ = wasHeld;
        p_.deferSpecializationBodies_ = wasDeferring;
        p_.replayingInline_ = wasReplaying;
        p_.inTemplateArgs_ = wasInArgs;
        Type::nameDependentSizes(wasBody);
        p_.patternBody_ = wasBody;
        p_.patternUses_ = wasUses;
        unbindTemplateParameters(undo);
        p_.packs_ = wasPacks;
        p_.namespaceStack_ = wasStack;
        p_.restoreFunctionState(outer);
        p_.at_ = resume;
        p_.lookup_.popLayer(layer);
        specializations_.erase(specializations_.begin() + static_cast<long>(hadSpecializations), specializations_.end());
        p_.refTemps_ = hadTemps;
        memberTemplateMade_ = hadMemberTemplates;
        owedBodies_ = hadOwed;
        p_.syntaxTree_.kill(mark);
    }
    if (outermost && makeWanted()) {
        for (int d = 0; d < ReadDoors; d++) {
            declRead_[d].resize(hadRead[static_cast<std::size_t>(d)]);
            declTally_[d][0] = hadTally[static_cast<std::size_t>(d)];
            declUnread_[d] = hadUnread[static_cast<std::size_t>(d)];
        }
        p_.syntaxTree_.forget(hadNotes);
        readDeclaration(decl, door, which);
        return;
    }
    const std::size_t at = door == ReadPartial ? decl.partials[which].bodyAt : door == ReadOutOfLine ? decl.outOfLine[which].start :
                           door == ReadAlias ? decl.aliasAt : decl.afterParams;
    if (!tallied) tallyRead(door, mark, why, door != ReadAlias, at);
}

// The member templates registered under `tag` and defined in its class.
std::vector<TemplateDecl> Templates::memberTemplatesUnder(const std::string &tag) const {
    std::vector<TemplateDecl> mine;
    for (std::map<std::string, std::vector<TemplateDecl> >::const_iterator m = memberTemplates_.lower_bound(tag + "::");
         m != memberTemplates_.end() && m->first.compare(0, tag.size() + 2, tag + "::") == 0; ++m)
        for (std::size_t t = 0; t < m->second.size(); t++)
            if (m->second[t].defined && !m->second[t].outOfLineBody) mine.push_back(m->second[t]);
    return mine;
}

void Templates::readMemberTemplatesOf(const std::string &tag) {
    if (p_.patternUses_) return;
    const std::vector<TemplateDecl> mine = memberTemplatesUnder(tag);
    for (std::size_t t = 0; t < mine.size(); t++)
        if (mine[t].classParams.empty()) readDeclaration(mine[t], ReadMember);
}

void Templates::readMemberTemplates(const std::string &readTag, std::size_t outer, Program *pattern) {
    const std::vector<TemplateDecl> mine = memberTemplatesUnder(readTag);
    for (std::size_t t = 0; t < mine.size(); t++) {
        const TemplateDecl &mt = mine[t];
        const std::size_t from = p_.syntaxTree_.size();
        const bool wasBody = p_.memberTemplateBody_;
        std::string refused;
        try {
            Parser::Trial one(&p_);
            std::vector<const Type *> binding;
            for (std::size_t i = 0; i < mt.params.size(); i++) binding.push_back(p_.types_.templateParam(static_cast<int>(outer + i)));
            const std::vector<long long> values(binding.size(), 0);
            const Packs packs(mt.params.size());
            const std::vector<TemplateArg> args = argumentsFromBinding(mt.params, binding, values, packs);
            Parser::BoundParams bound(&p_);
            bindTemplateParameters(mt.params, binding, values, packs, &bound.undo);
            const std::string display = specializationKey(mt.isConstructor ? mt.ownerLocal : mt.name, args);
            replayMemberTemplate(mt, display, mt.ownerTag + "::" + display, args, nullptr, false);
        } catch (const SubstitutionFailure &f) {
            refused = f.why;
        }
        p_.memberTemplateBody_ = wasBody;
        p_.current_ = pattern;
        tallyRead(ReadMember, from, refused, true, mt.afterParams);
    }
}

void Templates::holdDeclarations() {
    if (!checkBodies_) return;
    for (int door = 0; door < ReadDoors; door++)
        for (std::size_t i = 0; i < declRead_[door].size(); i++) {
            const syntax::NodeId read = declRead_[door][i];
            declTally_[door][0]++;
            const std::map<std::uint32_t, syntax::NodeId>::const_iterator held = bodyOnce_.find(p_.syntaxTree_.node(read).loc);
            if (held == bodyOnce_.end()) declTally_[door][4]++;
            else {
                const int d = p_.syntaxTree_.differs(held->second, read);
                declTally_[door][1 + d]++;
                if (door == ReadFunction && d == 2 && declShapes_.insert(p_.syntaxTree_.node(read).loc).second)
                    declShapeTrees_.push_back("read " + p_.syntaxTree_.spelled(read) + "\n-check=templates declaration shape: replayed " + p_.syntaxTree_.spelled(held->second));
            }
        }
}

std::string Templates::bodiesReport() const {
    std::string out = "-check=templates: bodies=" + std::to_string(bodyTally_[0] + bodyTally_[1] + bodyTally_[2] + bodyTally_[3]) +
                      " held=" + std::to_string(bodyTally_[0]) + " same=" + std::to_string(bodyTally_[1]) +
                      " text=" + std::to_string(bodyTally_[2]) + " shape=" + std::to_string(bodyTally_[3]) +
                      " declared=" + std::to_string(signatureTally_[0]) + " signature=" + std::to_string(signatureTally_[1]) + "\n";
    for (std::set<std::uint32_t>::const_iterator i = bodyShapes_.begin(); i != bodyShapes_.end(); ++i)
        out += "-check=templates shape: " + p_.src_.where(*i) + "\n";
    for (std::size_t i = 0; i < builtFailed_.size(); i++) out += "-check=templates failed: " + builtFailed_[i] + "\n";
    for (int door = 0; door < ReadDoors; door++) {
        long unread = 0;
        for (std::map<std::string, int>::const_iterator i = declUnread_[door].begin(); i != declUnread_[door].end(); ++i) unread += i->second;
        // A door the unit never came to prints nothing: the function door's lines are the record's.
        if (door != ReadFunction && declTally_[door][0] + unread == 0) continue;
        const std::string head = door == ReadFunction ? "" : std::string(" ") + readDoorNames[door];
        out += "-check=templates declarations" + (door == ReadFunction ? std::string(": functions=") : head + ": bodies=") +
               std::to_string(declTally_[door][0] + unread) + " read=" + std::to_string(declTally_[door][0]) +
               " unread=" + std::to_string(unread) + " same=" + std::to_string(declTally_[door][1]) + " text=" + std::to_string(declTally_[door][2]) +
               " shape=" + std::to_string(declTally_[door][3]) + " unreplayed=" + std::to_string(declTally_[door][4]) + "\n";
        if (door == ReadFunction) {
            for (std::set<std::uint32_t>::const_iterator i = declShapes_.begin(); i != declShapes_.end(); ++i)
                out += "-check=templates declaration shape: " + p_.src_.where(*i) + "\n";
            for (std::size_t i = 0; i < declShapeTrees_.size(); i++) out += "-check=templates declaration shape: " + declShapeTrees_[i] + "\n";
        }
        for (std::map<std::string, int>::const_iterator i = declUnread_[door].begin(); i != declUnread_[door].end(); ++i)
            out += "-check=templates unread" + head + " (" + std::to_string(i->second) + "): " + i->first + "\n";
    }
    for (std::map<std::string, int>::const_iterator i = substCensus_.begin(); i != substCensus_.end(); ++i)
        out += "-check=templates subst (" + std::to_string(i->second) + "): " + i->first + "\n";
    for (std::map<std::string, int>::const_iterator i = classCensus_.begin(); i != classCensus_.end(); ++i)
        out += "-check=templates class (" + std::to_string(i->second) + "): " + i->first + "\n";
    for (std::map<std::string, int>::const_iterator i = memberCensus_.begin(); i != memberCensus_.end(); ++i)
        out += "-check=templates member (" + std::to_string(i->second) + "): " + i->first + "\n";
    for (std::map<std::string, int>::const_iterator i = frameCensus_.begin(); i != frameCensus_.end(); ++i)
        out += "-check=templates frame (" + std::to_string(i->second) + "): " + i->first + "\n";
    for (std::map<std::string, int>::const_iterator i = shellCensus_.begin(); i != shellCensus_.end(); ++i)
        out += "-check=templates shell (" + std::to_string(i->second) + "): " + i->first + "\n";
    for (std::map<std::string, int>::const_iterator i = madeCensus_.begin(); i != madeCensus_.end(); ++i)
        out += "-check=templates made (" + std::to_string(i->second) + "): " + i->first + "\n";
    return out;
}

static const char *frameStepName(char kind) {
    switch (kind) {
    case 'z': return "the frame's start"; case 'p': return "a nested function's frame"; case 'q': return "the outer frame put back";
    case 'o': return "a synthesised frame"; case 'c': return "the frame it closed into"; case 'd': return "a local";
    case 'a': return "a slot no local names"; case 'f': return "a slot given back"; case 'S': case 'R': return "the sret slot";
    case 'V': return "the variadic save area"; case 'e': return "the frame's size"; case 'b': return "a block";
    }
    return "a step the check does not know";
}

// **Slice 2c**: the frame the read's steps lay under `s` from `start`, the parser's frameAlign_ before it - held against the
// replay's step by step: kind, type (at Made, the kernel's answer a door got) and offset - and in `raised` the widest slot past
// the alignment it started at, outside a nested function: what allocateFrameSlot records. Only the check reads the replay's.
std::string Templates::layFrame(const std::vector<FrameEvent> &read, const std::vector<FrameEvent> &replay, const Substituted &s, int start, int *raised, std::vector<int> *laidOut) {
    struct Frame { int size, align; std::vector<Parser::FreeSlot> free; };
    Frame now = { 0, 16, std::vector<Parser::FreeSlot>() };
    int nested = 0;
    *raised = 0;
    std::vector<Frame> saved;
    std::vector<int> laid(read.size(), 0);
    const bool held = !replay.empty() || read.empty();      // no replay: the frame laid, checked against nothing
    std::size_t k = 0, j = 0;
    for (; k < read.size() && (!held || j < replay.size()); k++) {
        FrameEvent r = read[k];
        if (r.kind == 'R') {     // a dependent class returned by value: the sret slot where the instance returns it indirectly, or no step
            const Type *u = substitute(s, r.type, At{ p_.at_ }).type;
            if (p_.dependentType(u)) return "the return type: a type the binding does not carry";
            if (!u->isStructOrUnion() || !p_.abi_.returnsIndirectly(*u, p_.target_, r.align != 0)) continue;
            r.kind = 'S', r.type = nullptr;
        }
        const FrameEvent &y = held ? replay[j++] : r;
        const std::string step = frameStepName(r.kind);
        if (held && r.kind != y.kind) return "the read's " + step + " where the replay has " + frameStepName(y.kind);
        const Type *t = substitute(s, r.type, At{ p_.at_ }).type;
        if (held && t != y.type) return step + ": typed apart";
        if (t != nullptr && p_.dependentType(t)) return step + ": a type the binding does not carry";
        const Type *stored = t != nullptr && t->isReference() ? p_.types_.pointerTo(t->referent()) : t;
        int at = 0;
        if (r.kind == 'd')
            for (std::size_t i = now.free.size(); i-- > 0 && at == 0;)
                if (now.free[i].stored == stored && now.free[i].align >= r.align) { at = now.free[i].offset; now.free.erase(now.free.begin() + static_cast<long>(i)); }
        if ((r.kind == 'd' && at == 0) || r.kind == 'a') {
            const int align = std::max(objectAlign(stored, p_.target_), r.align);
            now.size = alignTo(now.size + stored->size(p_.target_), align);
            at = now.size;
            if (align > now.align) { now.align = align; if (nested == 0) *raised = align; }
        } else if (r.kind == 'f') {
            // The local this scope gives back: its declaration in this frame, a nested function's steps passed over.
            std::size_t d = k;
            for (int depth = 0; d-- > 0;) {
                if (read[d].kind == 'q') depth++;
                else if (read[d].kind == 'p' && depth-- == 0) d = 0;
                else if (read[d].kind == 'd' && depth == 0 && read[d].serial == r.serial) break;
            }
            if (d >= k || read[d].kind != 'd' || read[d].serial != r.serial) return step + ": a local the read never declared";
            at = laid[d];
            if (at > 0) now.free.push_back(Parser::FreeSlot{ stored, at, objectAlign(stored, p_.target_) });
        } else if (r.kind == 'z' || r.kind == 'p' || r.kind == 'o') {
            at = r.kind == 'o' ? now.size : 0;
            if (r.kind != 'z') saved.push_back(now);
            now.size = 0;
            now.free.clear();
            // The parser resets frameAlign_ for a nested function alone; any other frame starts where the last one left it.
            if (r.kind == 'p') { now.align = 16; nested++; }
            if (held && r.kind == 'z' && y.align != start) return step + ": its alignment, which the replay found apart";
            if (r.kind == 'z') now.align = start;
        } else if (r.kind == 'q' || r.kind == 'c') {
            if (saved.empty()) return step + ": a frame put back that was never saved";
            const int align = now.align;
            now = saved.back();
            if (r.kind == 'q') nested--;
            else now.align = align;
            saved.pop_back();
            at = now.size;
        } else if (r.kind == 'S') {
            at = now.size = alignTo(now.size + 8, 8);
        } else if (r.kind == 'V') {
            at = now.size = alignTo(now.size, 16) + 176;
        } else if (r.kind == 'e') {
            at = alignTo(now.size, 16);
        } else if (r.kind == 'b') {
            if (held && r.block != y.block) return step + ": the block it opens in";     // its offset is where it opens
            at = r.offset;
        } else return step;
        laid[k] = at;
        if (held && at != y.offset) return step + ": its offset";
    }
    if (held && (k != read.size() || j != replay.size())) return "the read's " + std::to_string(read.size()) + " steps against the replay's " + std::to_string(replay.size());
    *laidOut = laid;
    return std::string();
}

// Step 2's end: the function a read made at its declaration, kept by `at` beside its steps - its records, no body.
void Templates::keepRead(std::size_t at, const Function &f) {
    readFnAt_[at] = readFns_.size();
    readFns_.push_back(Function(f.name(), f.returns(), f.params(), p_.oneStatement({}), f.frameSize(), f.isStatic(), f.sretSlot(),
                                f.isVariadic(), f.regSaveSlot(), f.pos()));
    Function &g = readFns_.back();
    g.setHasThis(f.hasThis()); g.setHasLandingPads(f.hasLandingPads()); g.setNoexcept(f.isNoexcept()); g.setExceptionSpec(f.exceptionSpec());
}

// Slice 3b: a covered member body's frame laid from its read's steps, and the shell made from them, held against the replay's.
void Templates::memberFrame(const PendingBody &body, const Substituted &s, const std::vector<FrameEvent> &replayed) {
    Built reads;     // the read's members' constructors against the replay's: apart fails the unit by name
    std::vector<std::string> guards;     // and its guarded members' slots and pads against the replay's
    const std::string symbol = p_.lookup_.functions_[body.which].symbol, stops = membersBuilt(body, symbol, &reads, &guards, s);
    const std::string built = stops.empty() ? builtApart(symbol, reads) : std::string();
    if (!built.empty()) builtFailed_.push_back(p_.src_.where(p_.tokens_[body.start].pos) + ": " + built);
    std::size_t root = p_.definedBodies_.size();
    while (root-- > 0 && !(p_.syntaxFunctions_.count(p_.definedBodies_[root].v) && p_.syntaxFunctions_.at(p_.definedBodies_[root].v) == symbol)) {}
    constantsHeld(symbol, root < p_.definedBodies_.size() ? p_.definedBodies_[root] : syntax::NodeId(), body.start);
    const std::vector<FrameEvent> read = stops.empty() ? guardedFrame(readFrames_[body.start], guards.size()) : readFrames_[body.start];
    if (read.empty() || replayed.empty()) { frameCensus_["member: no steps logged"]++; return; }
    if (wrapInherited(body)) { frameCensus_["member: replayed: a noexcept body wrapped by the calls of the bodies before it"]++; return; }
    int raised = 0;
    std::vector<int> laid;
    const std::string apart = layFrame(read, replayed, s, replayed.front().kind == 'z' ? replayed.front().align : 0, &raised, &laid);
    frameCensus_[apart.empty() ? "member: equal" : "member: differ: " + apart]++;
    const std::string place = p_.src_.where(p_.tokens_[body.start].pos) + ": ";     // a guarded member's frame or pads apart fail it
    if (!apart.empty() && stops.empty() && !guards.empty()) builtFailed_.push_back(place + "a guarded member's frame apart: " + apart);
    if (!apart.empty()) return;
    Specialization sp;
    sp.start = body.start;
    sp.symbol = p_.lookup_.functions_[body.which].symbol;
    const std::vector<Function> &made = p_.current_->functions;
    std::size_t at = made.size();
    while (at-- > 0 && made[at].symbol() != sp.symbol) {}
    if (at >= made.size()) { shellCensus_["member: no function made"]++; return; }
    std::vector<Function> own;
    const std::string fromRead = madeFromRead(sp, s, read, laid, &own);
    memberNamed(body, &own.back());
    if (stops.empty() && !guards.empty()) own.back().setHasLandingPads(true);
    const std::string shell = shellApart(own.back(), made[at]);
    shellCensus_[shell.empty() ? "member: equal" : "member: differ: " + shell]++;
    const std::string side = !shell.empty() ? "its shell" : !fromRead.empty() ? "replayed: " + fromRead : madeApart(own.back(), made[at]);
    madeCensus_[side.empty() ? "member: equal" : "member: differ: " + side]++;
    if (side == "its landing pads" && stops.empty() && !guards.empty()) builtFailed_.push_back(place + "a guarded member's landing pads apart");
}

// **Step 2's shell**: a Function's records, made from the read's steps as laid - a parameter's and a
// local's from its `d` (name, stored type, offset, scope, alignment, serial), the sret slot, the save area and the frame's size -
// and the rest from the function the read made at its declaration, as the replay's definition path sets it for a free function.
std::string Templates::madeFromRead(const Specialization &sp, const Substituted &s, const std::vector<FrameEvent> &read, const std::vector<int> &laid, std::vector<Function> *out) {
    std::vector<Param> params;
    std::vector< ::Local> locals;
    std::vector<int> blocks(1, 0);      // the block table -g reads, from topLevel's outermost block as enterBlock adds to it
    std::vector<std::size_t> opens;
    int depth = 0, size = 0, sret = 0, save = 0;
    for (std::size_t k = 0; k < read.size() && k < laid.size(); k++) {
        const FrameEvent &r = read[k];
        if (r.kind == 'p' || r.kind == 'o') depth++;
        else if (r.kind == 'q' || r.kind == 'c') depth--;
        if (depth != 0) continue;
        if (r.kind == 'b') { blocks.push_back(r.block); opens.resize(blocks.size(), 0); opens.back() = static_cast<std::size_t>(laid[k]); }
        if (r.kind == 'S' || (r.kind == 'R' && laid[k] != 0)) sret = laid[k];
        else if (r.kind == 'V') save = laid[k];
        else if (r.kind == 'e') size = laid[k];
        if (r.kind != 'd') continue;
        const Type *t = substitute(s, r.type, At{ p_.at_ }).type, *stored = t->isReference() ? p_.types_.pointerTo(t->referent()) : t;
        // A member's `this` is declared after its parameters and is its first (definition's paramSlots).
        if (r.param) params.insert(r.name == "this" ? params.begin() : params.end(), Param{ stored, laid[k] });
        locals.push_back(::Local(r.name, stored, laid[k], r.param, std::string(), r.block));
        locals.back().align = std::max(r.align, objectAlign(stored, p_.target_));
        locals.back().serial = r.serial;
        if (t->isReference()) locals.back().declared = t;
    }
    const std::map<std::size_t, std::size_t>::const_iterator at = readFnAt_.find(sp.start);
    const Function *f = at != readFnAt_.end() ? &readFns_[at->second] : nullptr;
    // A reference return is emitted as a pointer (emittedReturn): substituted as written, so `T &&` or `T` with T = U & is U *.
    const std::map<std::size_t, syntax::NodeId>::const_iterator body = readBody_.find(sp.start);
    const std::unordered_map<std::uint32_t, const Type *>::const_iterator written =
        body != readBody_.end() ? p_.declaredReturns_.find(body->second.v) : p_.declaredReturns_.end();
    const Type *as = written != p_.declaredReturns_.end() && written->second != nullptr ? substitute(s, written->second, At{ p_.at_ }).type
                   : f != nullptr ? substitute(s, f->returns(), At{ p_.at_ }).type : nullptr;
    const Type *returns = as != nullptr && as->isReference() ? p_.types_.pointerTo(as->referent()) : as;
    // A covered class's constructor or destructor is named by its class: the program's, which the read class stands for.
    std::string name = f != nullptr ? f->name() : std::string();
    const bool cls = !s.from.empty() && s.from.back()->isStructOrUnion() && s.to.back()->isStructOrUnion();
    if (cls && (name == s.from.back()->tag() || name == "~" + s.from.back()->tag())) name = (name[0] == '~' ? "~" : "") + s.to.back()->tag();
    out->push_back(Function(name, returns, params, p_.oneStatement({}), size, f != nullptr && f->isStatic(), sret,
                            f != nullptr && f->isVariadic(), save, f != nullptr ? f->pos() : 0, locals));
    Function &g = out->back();
    g.setBlocks(blocks, opens);
    g.setSymbol(sp.symbol);
    if (f == nullptr) return "no function read";
    const Type *r = f->returns();
    while (r != nullptr && (r->unqualified() != r || r->isPointer() || r->isReference())) r = r->unqualified() != r ? r->unqualified() : r->isPointer() ? r->pointee() : r->referent();
    if (r != nullptr && r->dependentExpr() != nullptr) return "a decltype return, which the replay works out";
    if (returns == nullptr || p_.dependentType(returns)) return "a return type the binding does not carry";
    if (f->isNoexcept() || !f->exceptionSpec().empty() || f->hasLandingPads()) return "an exception specification or landing pads";
    if (!sp.display.empty() && f->name() == sp.name) g.setDisplayName(Type::displaySpelling(sp.display));
    g.setHasThis(f->hasThis());
    g.setInline(!f->isStatic());       // an instantiation replays inline: vague linkage unless internal
    return std::string();
}

// The shell made from the read held against the replay's function, record for record; serials one for one.
std::string Templates::shellApart(const Function &g, const Function &f) const {
    const std::vector<Param> &params = g.params();
    const std::vector< ::Local> &locals = g.locals();
    if (params.size() != f.params().size()) return "its parameters' count";
    for (std::size_t q = 0; q < params.size(); q++)
        if (params[q].type != f.params()[q].type || params[q].offset != f.params()[q].offset) return "a parameter";
    if (locals.size() != f.locals().size()) return "its locals' count";
    std::map<int, int> serials, back;      // the read numbers its locals as its own; the replay's, one for one
    for (std::size_t q = 0; q < locals.size(); q++) {
        const std::vector< ::Local> &a = locals, &b = f.locals();
        if (a[q].name != b[q].name || a[q].type != b[q].type || a[q].offset != b[q].offset || a[q].isParam != b[q].isParam ||
            a[q].staticName != b[q].staticName) return "a local";
        if (a[q].scope != b[q].scope) return "a local's block";
        if (a[q].align != b[q].align || a[q].declared != b[q].declared) return "a local's alignment or declared type";
        if (!serials.insert(std::make_pair(b[q].serial, a[q].serial)).second || !back.insert(std::make_pair(a[q].serial, b[q].serial)).second)
            return "a local's serial, not one for one with the replay's";
    }
    if (g.sretSlot() != f.sretSlot() || g.regSaveSlot() != f.regSaveSlot()) return "the sret slot or the save area";
    if (g.frameSize() != f.frameSize()) return "the frame's size";
    if (g.blocks() != f.blocks() || g.blockPositions() != f.blockPositions()) return "the block table";
    return std::string();
}

// Step 2's end: the rest of what the replay's definition path sets on the function, held against the one made from the read.
std::string Templates::madeApart(const Function &g, const Function &f) const {
    if (g.name() != f.name()) return "its name";
    if (g.symbol() != f.symbol() || g.alias() != f.alias()) return "its symbol or alias";
    if (g.returns() != f.returns()) return "its return type";
    if (g.isStatic() != f.isStatic() || g.isInline() != f.isInline()) return "its linkage";
    if (g.isVariadic() != f.isVariadic() || g.pos() != f.pos() || g.hasThis() != f.hasThis()) return "its signature's marks";
    if (g.hasDisplayName() != f.hasDisplayName() || g.displayName() != f.displayName()) return "its display name";
    if (g.hasLandingPads() != f.hasLandingPads()) return "its landing pads";
    if (g.isNoexcept() != f.isNoexcept() || g.exceptionSpec() != f.exceptionSpec()) return "its exception specification";
    if (!f.synthesised().empty()) return "a synthesised function";
    return std::string();
}

// What a node folds to on the typed tree (Typer::typeNode): a sizeof's or a case's value, an enumerator's or a folded static
// member's - under a binding, 2f's value first - and an array local's bound; `has` false where the node carries none.
long long Templates::constantOf(const Bound *b, syntax::NodeId id, bool *has) const {
    const Parser &p = p_;
    *has = true;
    if (b != nullptr && b->values.count(id.v) != 0) return static_cast<long long>(b->values.at(id.v)) - 2147483648LL;
    const auto k = p.keywordValues_.find(id.v);
    if (k != p.keywordValues_.end()) return k->second.first;
    const auto c = p.caseFacts_.find(id.v);
    if (c != p.caseFacts_.end() && !c->second.isDefault) return c->second.value;
    const auto r = p.resolutions_.find(id.v);
    if (r != p.resolutions_.end() && (r->second.what == Parser::Resolved::Enumerator || (r->second.what == Parser::Resolved::StaticMember && r->second.serial == 1)))
        return r->second.value;
    const auto l = p.declaredLocals_.find(id.v);
    const Type *t = l != p.declaredLocals_.end() && l->second.type != nullptr ? l->second.type->unqualified() : nullptr;
    if (t != nullptr && b != nullptr) t = substitute(b->s, t).type;
    if (t != nullptr && t->isArray()) return t->length();
    return *has = false, 0;
}

// Work (1): the two bodies walked together, node for node where their shapes agree; the first constant apart, by its text.
std::string Templates::constantsApart(const std::string &symbol, syntax::NodeId read, syntax::NodeId replay) const {
    const syntax::Tree &st = p_.syntaxTree_;
    const std::map<std::string, Bound>::const_iterator b = bound_.find(symbol);
    std::vector<std::pair<syntax::NodeId, syntax::NodeId> > work(1, std::make_pair(read, replay));
    while (!work.empty()) {
        const syntax::NodeId a = work.back().first, y = work.back().second;
        work.pop_back();
        if (st.node(a).kind != st.node(y).kind || st.node(a).count != st.node(y).count) continue;     // a shape apart: the body check's
        bool hasA = false, hasY = false;
        const long long va = constantOf(b != bound_.end() ? &b->second : nullptr, a, &hasA), vy = constantOf(nullptr, y, &hasY);
        if (hasA != hasY || va != vy)
            return "a constant: '" + st.text(a) + "' " + (hasA ? std::to_string(va) : "none") + " for the replay's " + (hasY ? std::to_string(vy) : "none");
        for (std::uint32_t c = 0; c < st.node(a).count; c++) work.push_back(std::make_pair(st.child(a, c), st.child(y, c)));
    }
    return std::string();
}

// The replay's body of `symbol` held against the read's: a difference fails the unit by name, as builtApart does.
void Templates::constantsHeld(const std::string &symbol, syntax::NodeId replay, std::size_t start) {
    const std::map<std::size_t, syntax::NodeId>::const_iterator read = readBody_.find(start);
    if (read == readBody_.end() || !replay.valid() || p_.syntaxTree_.node(read->second).kind != p_.syntaxTree_.node(replay).kind) {
        madeCensus_["constants: no body to hold"]++;
        return;
    }
    const std::string apart = constantsApart(symbol, read->second, replay);
    madeCensus_[apart.empty() ? "constants: equal" : "constants: differ: " + apart]++;
    if (!apart.empty()) builtFailed_.push_back(p_.src_.where(p_.tokens_[start].pos) + ": " + apart);
}

// Step 2's end: the function, its frame's alignment and the unit's records the replay's definition path leaves, made from the
// read alone; false - and replayed - where the read leaves what the made function does not carry or a generator walks the Ast.
bool Templates::madeWithoutReplay(const Specialization &sp, const Substituted &s, int startAlign, int alignHad, std::string *why, const PendingBody *member,
                                  const std::vector<std::string> *guards) {
    if (std::string(p_.target_.name()) == "tms6747") return *why = "a target whose generator may walk the Ast body", false;
    for (const Signature &f : p_.lookup_.functions_)     // slice 3w: its read kept the deleting form pushed beside it
        if (f.symbol == sp.symbol && f.isVirtual && f.name[0] == '~') return *why = "a virtual destructor, whose deleting form the replay emits", false;
    int raised = 0;
    std::vector<int> laid;
    const bool guarded = guards != nullptr && !guards->empty();
    const std::vector<FrameEvent> frame = guarded ? guardedFrame(readFrames_[sp.start], guards->size()) : readFrames_[sp.start];
    *why = layFrame(frame, std::vector<FrameEvent>(), s, startAlign, &raised, &laid);
    if (!why->empty()) return *why = "the frame: " + *why, false;
    std::vector<Function> own;
    if (!(*why = madeFromRead(sp, s, frame, laid, &own)).empty()) return false;
    if (member != nullptr) memberNamed(*member, &own.back());
    if (guarded) own.back().setHasLandingPads(true);
    p_.current_->functions.push_back(std::move(own.back()));
    for (std::size_t q = 0; q < p_.lookup_.functions_.size(); q++)
        if (p_.lookup_.functions_[q].symbol == sp.symbol) p_.lookup_.functions_[q].defined = true;
    if (member == nullptr) p_.discardableInline_.insert(sp.symbol);     // a specialization's member is not discardable
    if (raised > alignHad) p_.frameAligns_[sp.symbol] = raised;
    p_.frameAlign_ = std::max(startAlign, raised);
    unreplayed_.insert(sp.symbol);
    callsUsed(bound_.at(sp.symbol).read, bound_.at(sp.symbol).calls);
    for (std::map<std::uint32_t, std::pair<std::string, const Type *> >::const_iterator a = bound_.at(sp.symbol).assigns.begin();
         a != bound_.at(sp.symbol).assigns.end(); ++a)      // the operator= assignValue marks used
        for (std::size_t q = 0; q < p_.lookup_.functions_.size(); q++)
            if (p_.lookup_.functions_[q].symbol == a->second.first) { p_.noteUsed(q); break; }
    for (std::size_t q = guarded ? guards->size() : 0; q-- > 0;) p_.noteChosen((*guards)[q]);     // the pads', last member first
    return true;
}

// A read-made constructor's frame with the slot each guarded member's pad takes on Itanium (wrapCleanups's exception pointer),
// allocated as assemble does, after the body's steps and before the frame's size; Microsoft's funclets take none.
std::vector<FrameEvent> Templates::guardedFrame(const std::vector<FrameEvent> &read, std::size_t guards) const {
    std::vector<FrameEvent> out = read;
    std::size_t end = out.size();
    while (end-- > 0 && out[end].kind != 'e') {}
    if (end >= out.size() || p_.target_.microsoftNames()) return out;
    const FrameEvent slot = { 'a', p_.types_.pointerTo(p_.types_.get(Kind::Void)), 0, 0, 0, std::string(), false, 0 };
    for (std::size_t q = 0; q < guards; q++) out.insert(out.begin() + static_cast<std::ptrdiff_t>(end), slot);
    return out;
}

// [except.spec]/9's wrap the replay gives a noexcept body whose read made no pads: mayThrow_ is cleared once per replayInlineBodies
// call, not per body, so a call in a body replayed before it wraps this one too. Its read cannot know that order: it replays.
bool Templates::wrapInherited(const PendingBody &body) const {
    const std::map<std::size_t, std::size_t>::const_iterator f = readFnAt_.find(body.start);
    return throwsBefore_.count(body.start) != 0 && !p_.target_.microsoftNames() && f != readFnAt_.end() &&
           readFns_[f->second].isNoexcept() && !readFns_[f->second].hasLandingPads();
}

// Slice 3b, step 2: a covered member body owed no replay, made from its read where its replay would stand in the batch, so its
// frame starts where the last one left frameAlign_ - a constructor and a destructor too (memberNamed); what the read cannot carry replays.
bool Templates::madeMember(const PendingBody &body) {
    if (p_.mayThrow_ > 0) throwsBefore_.insert(body.start);     // asked before every replayed body, as wrapUp will ask at its end
    else throwsBefore_.erase(body.start);
    const std::map<std::size_t, std::pair<std::string, Substituted> >::iterator m = memberMade_.find(body.start);
    if (m == memberMade_.end() || body.which >= p_.lookup_.functions_.size() || p_.lookup_.functions_[body.which].symbol != m->second.first) return false;
    const Substituted s = m->second.second;
    memberMade_.erase(m);
    Specialization sp;
    sp.start = body.start;
    sp.symbol = p_.lookup_.functions_[body.which].symbol;
    const std::map<std::string, int>::const_iterator had = p_.frameAligns_.find(sp.symbol);
    std::string why;
    Built built;     // its members' constructors noted and recorded as the replay's constructMember does
    std::vector<std::string> guards;
    bool cx = p_.constant_.constexprCtorSymbols_.count(sp.symbol) != 0;     // slice 3y: a constexpr member replays by name
    for (std::size_t t = body.start; t < p_.tokens_.size() && !p_.tokens_[t].is("(") && !cx; t++) cx = p_.tokens_[t].is("constexpr");
    why = cx ? "a constexpr member, whose constant the definition's path keeps"
        : wrapInherited(body) ? "a noexcept body wrapped by the calls of the bodies before it" : membersBuilt(body, sp.symbol, &built, &guards, s);
    for (std::size_t q = 0; why.empty() && q < built.size(); q++) {
        p_.noteChosen(built[q].second.first);
        p_.keepMemberBuilt(built[q].first, Parser::MemberBuilt{ built[q].second.first, built[q].second.second, false, false });
    }
    if (why.empty() && madeWithoutReplay(sp, s, p_.frameAlign_, had != p_.frameAligns_.end() ? had->second : 0, &why, &body, &guards)) {
        replayCensus_["member: substituted with no replay"]++;
        return true;
    }
    replayCensus_["member: replayed: " + why]++;
    return false;
}

// A constructor made from the read builds each class-typed member the list leaves as the replay's constructMember does: by its
// class's default constructor - here one taking nothing, public; what is not that replays the body. Keyed as memberBuilt_.
std::string Templates::membersBuilt(const PendingBody &body, const std::string &symbol, Built *out, std::vector<std::string> *guards,
                                    const Substituted &s) const {
    const Type *const read = !s.from.empty() && s.from.back()->isStructOrUnion() ? s.from.back() : nullptr;     // the class read
    const Type *const cls = p_.lookup_.findTypedef(body.tag);
    if (cls == nullptr || body.key != Parser::constructorKey(body.tag)) return std::string();
    for (const Member &m : cls->members()) {
        const Type *mc = m.declaredIn == nullptr ? p_.constructedClass(m) : nullptr;
        if (mc == nullptr) continue;
        if (cls->kind() == Kind::Union || m.anonUnion != nullptr || m.type->isArray() || p_.memberInit_.count(body.tag + "::" + m.name) != 0)
            return "a class-typed member in a union or an array, or with an initialiser of its own";
        const Signature *c = p_.classes_.defaultConstructorOf(mc);
        if (c == nullptr || c->access != Access::Public || c->deleted || !c->params.empty())
            return "a class-typed member whose default constructor takes defaults or is not public";
        // One with a destructor guards the rest as the replay's assemble does: its destructor is the pad's, public and not deleted -
        // a slot the read lays itself where its class was no parameter's (its pads then replay the body: madeFromRead).
        const Signature *d = p_.destructorOf(mc);
        if (d != nullptr && (d->access != Access::Public || d->deleted || mc->hasVirtualBase()))
            return "a class-typed member whose destructor is not public or destroys virtual bases";
        const Member *had = read != nullptr ? read->findMember(m.name) : nullptr;
        if (d != nullptr && (had == nullptr || !p_.memberHasDestructor(had->type))) guards->push_back(d->symbol);
        const std::vector<const Type *> ps(1, p_.types_.pointerTo(mc));
        out->push_back(std::make_pair(symbol + "|" + m.name + "|0", std::make_pair(c->symbol, p_.types_.functionType(p_.types_.get(Kind::Void), ps, false))));
    }
    return std::string();
}

// The replay's records of what builds a body's members (not the keys of a base, `^`, `~` or `$`) held against the read's, one for one.
std::string Templates::builtApart(const std::string &symbol, const Built &own) const {
    const std::string head = symbol + "|";
    std::size_t seen = 0;
    for (std::map<std::string, Parser::MemberBuilt>::const_iterator i = p_.memberBuilt_.lower_bound(head);
         i != p_.memberBuilt_.end() && i->first.compare(0, head.size(), head) == 0; ++i) {
        const char c = i->first.size() > head.size() ? i->first[head.size()] : '^';
        if (c == '^' || c == '~' || c == '$') continue;
        const std::string member = i->first.substr(head.size(), i->first.rfind('|') - head.size());
        std::size_t q = 0;
        while (q < own.size() && own[q].first != i->first) q++;
        if (q == own.size()) return "member '" + member + "' built by a constructor the read records none of";
        if (own[q].second.first != i->second.symbol || own[q].second.second != i->second.signature || i->second.defaulted || i->second.zeroed)
            return "member '" + member + "' built by a constructor other than the replay's";
        seen++;
    }
    return seen == own.size() ? std::string() : "a member built by a constructor the replay records none of";
}

// A covered member's display name under its class's whole name; a constructor's or destructor's C2 or D2 alias on Itanium, as the
// replay's definition path names it (a covered class has no base, so no virtual base splits it).
void Templates::memberNamed(const PendingBody &body, Function *g) const {
    const Type *const cls = p_.lookup_.findTypedef(body.tag);
    if (cls != nullptr) g->setDisplayName(Type::displaySpelling(cls->displayTag() + "::" + g->name()));
    if (cls == nullptr || p_.target_.microsoftNames() || body.which >= p_.lookup_.functions_.size()) return;
    std::string alias, why;
    if (body.key == Parser::destructorKey(body.tag)) itaniumDestructorName(body.tag, cls, false, &alias);
    else if (body.key == Parser::constructorKey(body.tag)) {
        const std::map<std::string, std::string>::const_iterator c2 = p_.constructorC2_.find(g->symbol());
        if (c2 != p_.constructorC2_.end()) alias = c2->second;
        else itaniumConstructorName(body.tag, cls, p_.types_.functionType(p_.types_.get(Kind::Void), p_.lookup_.functions_[body.which].params, false), false, &alias, &why);
    }
    if (!alias.empty()) g->setAlias(alias);
}

std::string Templates::builtReport() const {
    std::string out;
    for (std::map<std::string, int>::const_iterator i = builtCensus_.begin(); i != builtCensus_.end(); ++i)
        out += "-check=templates built (" + std::to_string(i->second) + "): " + i->first + "\n";
    for (std::map<std::string, int>::const_iterator i = replayCensus_.begin(); i != replayCensus_.end(); ++i)
        out += "-check=templates replay (" + std::to_string(i->second) + "): " + i->first + "\n";
    // The measure under -templates=subst: a line a class specialization, then the sum - made + replayed = total, each tag once.
    if (!substitute_) return out;
    long made = 0;
    for (std::map<std::string, std::string>::const_iterator i = specializationCensus_.begin(); i != specializationCensus_.end(); ++i) {
        out += "-check=templates specialization: " + i->second + " | " + i->first + "\n";
        made += i->second.compare(0, 4, "made") == 0 ? 1 : 0;
    }
    const long total = static_cast<long>(specializationCensus_.size());
    out += "-check=templates specializations: total=" + std::to_string(total) + " made=" + std::to_string(made) + " replayed=" +
           std::to_string(total - made) + " again=" + std::to_string(specializationAgain_) + "\n";
    // M8.4's measure (slice 4a): every door's fallback by why, then the unit's one sum; a body door's is its count less those made.
    long defaults = 0;
    for (std::map<std::string, long>::const_iterator i = fallbackCensus_.begin(); i != fallbackCensus_.end(); ++i) {
        out += "-check=templates falling back (" + std::to_string(i->second) + "): " + i->first + "\n";
        defaults += i->first.compare(0, 9, "default: ") == 0 ? i->second : 0;
    }
    for (std::map<std::string, long>::const_iterator i = defaultReport_.begin(); i != defaultReport_.end(); ++i)
        out += "-check=templates default (" + std::to_string(i->second) + "): " + i->first + "\n";
    const std::map<std::string, int>::const_iterator fm = replayCensus_.find("substituted with no replay"),
                                                     mm = replayCensus_.find("member: substituted with no replay");
    const long parts[] = { total - made, census_[FunctionBody] - (fm != replayCensus_.end() ? fm->second : 0),
                           census_[MemberBody] - (mm != replayCensus_.end() ? mm->second : 0), census_[MemberTemplate], census_[SfinaeTrial],
                           census_[ConstexprAhead], census_[AliasId], defaults };
    static const char *const partNames[] = { "classes", "functions", "members", "member-templates", "sfinae-trials", "constexpr-ahead",
                                             "aliases", "defaults" };
    std::string sum;
    long all = 0;
    for (std::size_t i = 0; i < sizeof parts / sizeof parts[0]; i++) all += parts[i], sum += std::string(" ") + partNames[i] + "=" + std::to_string(parts[i]);
    out += "-check=templates falling back=" + std::to_string(all) + sum + "\n";
    // R0's measure (D5, D7): the unit's re-entries under a binding by kind, their sum, and each template re-entered by site.
    long reads = 0, insts = 0, binds = 0;
    for (std::map<std::string, long>::const_iterator i = reentries_.begin(); i != reentries_.end(); ++i) {
        out += "-check=templates reentry (" + std::to_string(i->second) + "): " + i->first + "\n";
        if (i->first.compare(0, 5, "read ") == 0) reads += i->second;
        else insts += i->second;
        binds += i->first.size() > 6 && i->first.compare(i->first.size() - 6, 6, ": bind") == 0 ? i->second : 0;
    }
    out += "-check=templates reentries=" + std::to_string(reads + insts) + " read=" + std::to_string(reads) + " instantiation=" +
           std::to_string(insts) + " binds=" + std::to_string(binds) + " jumps=" + std::to_string(reads + insts - binds) + "\n";
    for (std::map<std::size_t, std::set<std::string> >::const_iterator i = reenteredAt_.begin(); i != reenteredAt_.end(); ++i) {
        const std::string where = p_.src_.where(i->first);
        for (const std::string &door : i->second)
            out += "-check=templates reentered at: " + where.substr(0, where.rfind(':')) + " | " + door + "\n";
    }
    return out;
}

void Templates::instantiatePending() {
    for (bool again = true; again; ) {
        again = false;
        for (std::size_t i = 0; i < specializations_.size(); i++) {
            if (specializations_[i].emitted) continue;
            // **Made where it was asked for, defined only where it was chosen** -
            // deduction instantiates a candidate before it can rank one, and an
            // ordinary function may win. A body skipped now may be wanted later.
            std::vector<Parser::PendingBody> now;
            std::vector<std::size_t> outsideNow;
            if (specializations_[i].isClass) {
                std::vector<Parser::PendingBody> later;
                for (std::size_t b = 0; b < specializations_[i].bodies.size(); b++) {
                    const Parser::PendingBody &body = specializations_[i].bodies[b];
                    // **This overload, not this name.** Every constructor of
                    // a class shares one key, so asking the key replays them
                    // all as soon as any is called.
                    const bool wanted =
                        body.which != Parser::PendingBody::npos() &&
                        body.which < p_.lookup_.functions_.size()
                            ? p_.lookup_.functions_[body.which].used
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
                        if (!memberIsUsed(o.constructor ? Parser::constructorKey(tag) : Parser::destructorKey(tag))) continue;
                        done[k] = true;
                        Parser::PendingBody b;
                        b.tag = tag; b.start = o.start; b.local = o.member; b.which = Parser::PendingBody::npos();
                        b.key = o.constructor ? Parser::constructorKey(tag) : Parser::destructorKey(tag);
                        now.push_back(b);
                        continue;
                    }
                    // **A static data member is defined only where an evaluated
                    // expression named it**, [temp.inst]/3, as clang emits it.
                    if (d.outOfLine[k].isData) {
                        const Type *spec = p_.lookup_.findTypedef(tag);
                        const Type::StaticMember *sm =
                            spec != nullptr ? spec->findStaticMember(d.outOfLine[k].member)
                                            : nullptr;
                        if (sm != nullptr && !p_.usedStaticMembers_.count(sm->symbol)) continue;
                    } else if (!memberIsUsed(tag + "::" + d.outOfLine[k].member)) continue;
                    done[k] = true;
                    outsideNow.push_back(k);
                }
                if (now.empty() && outsideNow.empty()) continue;
            } else {
                const std::vector<std::size_t> *had =
                    p_.lookup_.overloadsOf(specializations_[i].key);
                if (had == nullptr || !p_.lookup_.functions_[(*had)[0]].used) continue;
                specializations_[i].emitted = true;
            }
            again = true;
            // M8.2: a function specialization built from its template's body read at the declaration, under its binding.
            if (!specializations_[i].isClass && substitute_) {
                std::string why;
                Binding b;
                for (std::size_t q = 0; q < specializations_[i].binding.size(); q++) b.types.push_back(TypeTable::id(specializations_[i].binding[q]));
                b.values = specializations_[i].values;
                if (instantiate(i, b, &why)) { count(BySubstitution); continue; }
                substCensus_[why]++;
            }
            std::map<std::size_t, std::vector<FrameEvent> > memberFrames;     // a covered member body's replayed steps, by its start
            const std::map<std::string, Substituted>::const_iterator c = coveredClass_.find(specializations_[i].key);
            if (specializations_[i].isClass && substitute_) {
                for (std::size_t b = 0; b < now.size(); b++) {
                    Bound made;
                    const std::string why = c == coveredClass_.end() ? "replayed: its class replayed: " + classWhy(specializations_[i].key) : memberShape(now[b], c->second, &made);
                    memberCensus_[why]++;
                    if (why.compare(0, 8, "covered:") != 0) continue;
                    bound_[p_.lookup_.functions_[now[b].which].symbol] = made;     // Typer::typeBody
                    // Step 2: made from the read at its place in the batch (madeMember); the replay beside it only under -check=templates.
                    if (checkBodies_) memberFrames[now[b].start], replayCensus_["member: beside a replay, under -check=templates"]++;
                    else memberMade_[now[b].start] = std::make_pair(p_.lookup_.functions_[now[b].which].symbol, c->second);
                }
                if (!outsideNow.empty()) memberCensus_["replayed: a member defined out of line"] += static_cast<int>(outsideNow.size());
            }

            // Copied, not held by reference: replaying may append to the
            // vector and move it.
            const Parser::Specialization sp = specializations_[i];
            TemplateDecl decl = templates_[sp.name];

            std::vector<Shadow> undo;
            const DoorAt bodyDoor(this, sp.isClass ? "member body" : "function body");
            bindTemplateParameters(sp.params, sp.binding, sp.values, sp.packs, &undo);
            const std::string wasKey = p_.instantiationKey_;
            const std::string wasOf = p_.instantiationOf_;
            const std::string wasDisplay = p_.instantiationDisplay_;
            p_.instantiationKey_ = sp.isClass ? std::string() : sp.key;
            p_.instantiationOf_ = sp.isClass ? std::string() : sp.name;
            p_.instantiationDisplay_ = sp.isClass ? std::string() : sp.display;
            // What this body asks for is one deeper than it - and a specialization appended
            // while this round runs is replayed in this same round, so a chain that never
            // settles is one round long; the depth, not the round, is what bounds it.
            const int wasDepth = p_.instantiationDepth_;
            p_.instantiationDepth_ = sp.depth;

            const std::size_t resume = p_.at_;
            // **An instantiated definition has vague linkage** - [basic.link] and [temp.spec]: the
            // same specialization may be produced by every translation unit that uses it, and the
            // linker folds the copies rather than rejecting them.
            const bool wasReplayingInline = p_.replayingInline_;
            p_.replayingInline_ = true;
            // Its bodies read names in the namespace it was declared in, wherever it is used from.
            const std::vector<std::string> wasStack = p_.namespaceStack_;
            if (!decl.ns.empty()) p_.namespaceStack_ = Parser::namespacePathStack(decl.ns);
            const std::size_t bodiesFrom = p_.syntaxTree_.size();
            if (sp.isClass) {
                count(MemberBody, now.size() + outsideNow.size());
                // Its member functions, held here; inlineOwner_ gives the scope.
                struct FramesBack { Parser *p; std::map<std::size_t, std::vector<FrameEvent> > *was; ~FramesBack() { p->bodyFrames_ = was; } } framesBack = { &p_, p_.bodyFrames_ };
                p_.bodyFrames_ = memberFrames.empty() ? nullptr : &memberFrames;
                p_.replayInlineBodies(now);
                p_.bodyFrames_ = framesBack.was;
                for (std::size_t b = 0; b < now.size(); b++)
                    if (memberFrames.count(now[b].start) != 0) memberFrame(now[b], c->second, memberFrames[now[b].start]);
                // And the ones written outside it, which need no owner: the
                // tokens say `Box<T>::get`, so with T bound the ordinary
                // member-definition path reads the qualifier itself.
                for (std::size_t k = 0; k < outsideNow.size(); k++) {
                    p_.at_ = templates_[sp.name].outOfLine[outsideNow[k]].start;
                    reentered("jump: a member defined out of line");
                    p_.topLevel(*p_.current_);
                }
            } else {
                count(FunctionBody);
                p_.at_ = sp.start;
                // Slice 2c, step 1: a covered body's frame laid from its read's steps, held against the one this replay lays.
                const std::map<std::string, Bound>::const_iterator bound = substitute_ ? bound_.find(sp.symbol) : bound_.end();
                std::vector<FrameEvent> replayed;
                struct LogBack { Parser *p; std::vector<FrameEvent> *was; ~LogBack() { p->frameLog_ = was; } } logBack = { &p_, p_.frameLog_ };
                if (bound != bound_.end()) p_.frameLog_ = &replayed;
                const int startAlign = p_.frameAlign_;
                const std::map<std::string, int>::const_iterator had = p_.frameAligns_.find(sp.symbol);
                const int alignHad = had != p_.frameAligns_.end() ? had->second : 0;
                // What the replay does to the unit beyond its function, which a body made from the read must do alike.
                const std::size_t fnsHad = p_.current_->functions.size(), declsHad = p_.lookup_.functions_.size();
                const std::size_t specsHad = specializations_.size(), rootsHad = p_.definedBodies_.size();
                // Step 2's end: a covered body is the read's own, trusted; the replay runs beside it only under -check=templates.
                std::string skip = "beside a replay, under -check=templates";
                if (bound != bound_.end() && !checkBodies_ && madeWithoutReplay(sp, bound->second.s, startAlign, alignHad, &skip)) {
                    replayCensus_["substituted with no replay"]++;
                    p_.frameLog_ = logBack.was;
                } else {
                    if (bound != bound_.end()) replayCensus_[checkBodies_ ? skip : "replayed: " + skip]++;
                    reentered("jump: a function body");
                    p_.topLevel(*p_.current_);
                    p_.frameLog_ = logBack.was;
                    if (bound != bound_.end() && checkBodies_)
                        constantsHeld(sp.symbol, p_.definedBodies_.size() == rootsHad + 1 ? p_.definedBodies_.back() : syntax::NodeId(), sp.start);
                    if (bound != bound_.end()) {
                        int raised = 0;
                        std::vector<int> laid;
                        std::string apart = layFrame(readFrames_[sp.start], replayed, bound->second.s, startAlign, &raised, &laid);
                        const std::map<std::string, int>::const_iterator has = p_.frameAligns_.find(sp.symbol);
                        const int alignHas = has != p_.frameAligns_.end() ? has->second : 0;
                        if (apart.empty() && std::max(alignHad, raised) != alignHas) apart = "the frame's alignment";
                        if (apart.empty() && std::max(startAlign, raised) != p_.frameAlign_) apart = "the alignment the next frame starts at";
                        frameCensus_[apart.empty() ? "equal" : "differ: " + apart]++;
                        const std::vector<Function> &made = p_.current_->functions;
                        std::size_t at = made.size();
                        while (apart.empty() && at-- > 0 && made[at].symbol() != sp.symbol) {}
                        std::vector<Function> own;
                        const std::string fromRead = apart.empty() && at < made.size() ? madeFromRead(sp, bound->second.s, readFrames_[sp.start], laid, &own) : std::string();
                        if (apart.empty() && at < made.size()) {
                            const std::string shell = shellApart(own.back(), made[at]);
                            shellCensus_[shell.empty() ? "equal" : "differ: " + shell]++;
                            std::string side = !shell.empty() ? "its shell" : !fromRead.empty() ? "replayed: " + fromRead : madeApart(own.back(), made[at]);
                            if (side.empty() && (at != fnsHad || made.size() != fnsHad + 1)) side = "the functions it pushed";
                            if (side.empty() && (specializations_.size() != specsHad || p_.lookup_.functions_.size() != declsHad)) side = "a declaration or specialization it made";
                            for (std::size_t q = 0; side.empty() && q < p_.lookup_.functions_.size(); q++)
                                if (p_.lookup_.functions_[q].symbol == sp.symbol && !p_.lookup_.functions_[q].defined) side = "a declaration left undefined";
                            if (side.empty() && !p_.discardableInline_.count(sp.symbol)) side = "its inline definition kept";
                            if (side.empty() && p_.constant_.constexprFns_.count(sp.symbol)) side = "a constexpr function";
                            if (side.empty() && (p_.definedBodies_.size() != rootsHad + 1 || p_.syntaxFunctions_.find(p_.definedBodies_.back().v) == p_.syntaxFunctions_.end() ||
                                               p_.syntaxFunctions_.find(p_.definedBodies_.back().v)->second != sp.symbol)) side = "its body's root";
                            madeCensus_[side.empty() ? "equal" : "differ: " + side]++;
                        }
                    }
                }
            }
            holdBodies(bodiesFrom);
            p_.namespaceStack_ = wasStack;
            p_.replayingInline_ = wasReplayingInline;
            p_.at_ = resume;

            p_.instantiationKey_ = wasKey;
            p_.instantiationOf_ = wasOf;
            p_.instantiationDisplay_ = wasDisplay;
            p_.instantiationDepth_ = wasDepth;
            unbindTemplateParameters(undo);
        }
    }
}

// **A chain of instantiations that never settles** - `f(T t) { return f(&t); }` asks for f<T *>, then f<T **> - is
// refused by name kMaxNesting bodies deep, like cl and clang; it ran 43 s to stack overflow (1.25 review, fix 5).
// Bodies replay after their asker, so a specialization carries its depth; deduction's trial lets "yet" errors out.
void Templates::refuseInstantiationDepth(const std::string &name, int depth, std::size_t pos) const {
    if (depth <= Parser::kMaxNesting) return;
    p_.src_.fail(pos, "'" + name + "' is being instantiated from inside its own instantiations more than " +
                   std::to_string(Parser::kMaxNesting) + " levels deep, and the chain has not ended yet - a "
                   "template that asks for a new specialization of itself each time, with an argument "
                   "that grows, never settles, and the compiler stops here rather than run out of memory");
}

bool Templates::mentionsDeduced(const Type *t) {
    if (t == nullptr) return false;
    if (t->kind() == Kind::Deduced) return true;
    if (t->unqualified() != t) return mentionsDeduced(t->unqualified());
    if (t->isPointer() || t->isArray()) return mentionsDeduced(t->pointee());
    if (t->isReference()) return mentionsDeduced(t->referent());
    return false;
}

// The declared type with `auto` replaced, keeping what surrounds it: `const auto &` as int is `const int &`.
const Type *Templates::substituteDeduced(const Type *t, const Type *with) {
    // The qualifier first: a `const auto` is a const copy of the deduced kind, and asking the kind
    // first answered `with` bare - `const auto e = i;` was an `int` that could be assigned to, and
    // `const auto &c = gc;` an `int &` that could not bind (1.27, 2026-10-21, review of 1.25, item 3).
    if (t->unqualified() != t)
        return p_.types_.withConst(substituteDeduced(t->unqualified(), with));
    if (t->kind() == Kind::Deduced) return with;
    if (t->isPointer()) return p_.types_.pointerTo(substituteDeduced(t->pointee(), with));
    if (t->isReference()) {
        const Type *r = substituteDeduced(t->referent(), with);
        if (r->isReference()) return t->isRValueReference() ? r : p_.types_.referenceTo(r->referent());
        return t->isRValueReference() ? p_.types_.rvalueReferenceTo(r) : p_.types_.referenceTo(r);
    }
    if (t->isArray())
        return p_.types_.arrayOf(substituteDeduced(t->pointee(), with), t->length());
    return t;
}

const Type *Templates::deduceAutoFrom(const Type *declared, const Type *from,
                                   const std::string &name, std::size_t pos) {
    std::vector<const Type *> binding(1, static_cast<const Type *>(nullptr));
    std::string why;
    if (!deduceOne(declared, from, &binding, nullptr, &why) || binding[0] == nullptr)
        p_.src_.fail(pos, "'" + name + "' is declared '" + declared->describe() +
                       "' and its initialiser is '" + from->describe() +
                       "', which does not fit: " + why);
    // **What `auto` itself came out as**, which is not the declarator's type:
    // `auto *p = &i` deduces `int` and declares `int *`.
    p_.lastDeducedAuto_ = binding[0];
    return substituteDeduced(declared, binding[0]);
}

// An lvalue argument travels to deduction as `U &`: an expression's type never carries
// its value category, and a forwarding reference is the one parameter that asks for it.
const Type *Templates::deductionArgType(const Expr &e) const {
    if (const Parser::OverloadArg *oa = p_.overloadArgOf(e))
        p_.src_.fail(oa->pos, "'" + oa->key + "' names " + std::to_string(oa->set.size()) +
                           " functions, and is passed where a template deduces from it - choosing "
                           "one there is not supported yet; initialise a pointer of the type wanted "
                           "and pass that");
    const Type *t = e.type();
    // A string literal is an lvalue, [expr.prim.literal]: `Args &&` deduces `const char (&)[N]` from one (M1).
    const bool literal = dynamic_cast<const StrLit *>(&e) != nullptr && t->isArray();
    if ((isLvalue(e) || literal) && !t->isReference() && !t->isFunction()) return p_.types_.referenceTo(t);
    return t;
}

// `T &&` with T a bare template parameter, or `auto &&` ([dcl.spec.auto]/7 deduces it the same way).
bool Templates::forwardingReference(const Type *pattern) {
    if (!pattern->isRValueReference()) return false;
    const Type *t = pattern->referent();
    return (t->kind() == Kind::TemplateParam || t->kind() == Kind::Deduced) && t->unqualified() == t;
}

// One parameter of the pattern against one argument's type. The pattern still has
// Kind::TemplateParam in it, so "does this position deduce anything" is a question
// about the type and not about a table: one reached here binds.
bool Templates::deduceOne(const Type *pattern, const Type *arg,
                       std::vector<const Type *> *binding,
                       std::vector<long long> *values,
                       std::string *why, Packs *packs, bool nested) const {
    // **Inside a template-id the argument is matched as it is** ([temp.deduct.type]/8): `It<C, V>` against
    // `It<S, const int>` deduces V as `const int`, `It<C, const V>` against it V as `int`. The call-argument rules
    // below (decay, cv dropped) made V `int`, and a map's const_iterator matched no template (1.10, 2026-10-20).
    if (nested) {
        if (pattern->kind() == Kind::TemplateParam || pattern->kind() == Kind::Deduced) {
            const std::size_t i = pattern->kind() == Kind::Deduced
                                      ? 0 : static_cast<std::size_t>(pattern->length());
            if ((*binding)[i] == nullptr) { (*binding)[i] = arg; return true; }
            if ((*binding)[i] != arg) {
                *why = "it is '" + (*binding)[i]->describe() + "' in one argument "
                       "and '" + arg->describe() + "' in another";
                return false;
            }
            return true;
        }
        if (pattern->unqualified() != pattern) {
            if (pattern->isConst() && !arg->isConst()) {
                *why = "'" + arg->describe() + "' is not const where '" +
                       pattern->describe() + "' is";
                return false;
            }
            return deduceOne(pattern->unqualified(), arg->unqualified(), binding, values, why, packs, true);
        }
    }
    // **[temp.deduct.call]/3: `T &&` given an lvalue of type U deduces T as `U &`**, and
    // [dcl.ref]/6 then collapses the parameter to `U &` when the declaration is re-read.
    if (!nested && forwardingReference(pattern) && arg->isReference() && !arg->isRValueReference()) {
        const Type *param = pattern->referent();
        const std::size_t i = param->kind() == Kind::Deduced ? 0 : static_cast<std::size_t>(param->length());
        const Type *deduced = p_.types_.referenceTo(arg->referent());
        if ((*binding)[i] == nullptr) { (*binding)[i] = deduced; return true; }
        if ((*binding)[i] != deduced) {
            *why = "it is '" + (*binding)[i]->describe() + "' in one argument "
                   "and '" + deduced->describe() + "' in another";
            return false;
        }
        return true;
    }
    // **A reference parameter looks *through* itself and keeps the argument's qualifier; everything else decays**:
    // `const T &` given an `int` deduces int. **For `T &` or `auto &` the argument's qualifiers are part of T**
    // ([temp.deduct.call]/3: `T &` given `const int` is T = `const int`; 1.27, review of 1.25, item 3).
    bool keepsQualifiers = false;
    if (nested) {
        // matched as it is - see the top
    } else if (pattern->isReference()) {
        pattern = pattern->referent();
        if (arg->isReference()) arg = arg->referent();
        if (pattern->unqualified() != pattern) {
            pattern = pattern->unqualified();
            arg = arg->unqualified();
        } else {
            keepsQualifiers = true;
        }
    } else {
        arg = p_.decayedArgument(arg);
        if (pattern->unqualified() != pattern) pattern = pattern->unqualified();
    }

    // Kind::Deduced is `auto`, and it is parameter zero of a deduction with
    // one parameter - which is what [dcl.spec.auto] says it is.
    if (pattern->kind() == Kind::TemplateParam ||
        pattern->kind() == Kind::Deduced) {
        const std::size_t i = pattern->kind() == Kind::Deduced
                                  ? 0
                                  : static_cast<std::size_t>(pattern->length());
        const Type *deduced = keepsQualifiers ? arg : arg->unqualified();
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
                    if (!deduceOne(p.pack[k], a.pack[k], binding, values, why, packs, true)) return false;
                continue;
            }
            if (!deduceOne(p.type, a.type, binding, values, why, packs, true)) return false;
        }
        return true;
    }

    // **What a pointer or an array holds is matched as it is**, qualifiers part of T unless the pattern names them
    // (1.27, [temp.deduct.call]/4): `T *` given `const int *` gives `const int`, `const T *` given `int *` `int`,
    // and `T (&)[N]` given `const char[4]` `const char`.
    if (pattern->isPointer() && arg->isPointer()) {
        const Type *pp = pattern->pointee(), *ap = arg->pointee();
        if (pp->unqualified() != pp) { pp = pp->unqualified(); ap = ap->unqualified(); }
        if (pp->kind() == Kind::TemplateParam || pp->kind() == Kind::Deduced)
            return deduceOne(pp, ap, binding, values, why, packs, true);
        // A function pointee is matched as it is too - decayed again it would be a pointer, and
        // `R (*)(A)` below would never see the function.
        return deduceOne(pp, ap, binding, values, why, packs, nested || pp->isFunction());
    }
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
                (*binding)[j] = p_.types_.intType();
                if (values != nullptr) (*values)[j] = arg->length();
            }
            pattern = pattern->pointee();
            arg = arg->pointee();
        }
        if (pattern->isArray() || arg->isArray()) return true;
        if (pattern->unqualified() != pattern) { pattern = pattern->unqualified(); arg = arg->unqualified(); }
        if (pattern->kind() == Kind::TemplateParam || pattern->kind() == Kind::Deduced)
            return deduceOne(pattern, arg, binding, values, why, packs, true);
        return deduceOne(pattern, arg, binding, values, why, packs);
    }
    // **`R (*)(A)` against a function pointer** - [temp.deduct.type]/8's `T(*)()` and `T(T)`: the
    // return type and each parameter, matched as they are (1.27, 2026-10-21, review of 1.25, item 3:
    // `callIt(dbl, 21)` for `R callIt(R (*f)(A), A a)` bound nothing and R "appears in no parameter").
    if (pattern->isFunction() && arg->isFunction())
        return matchFunctionParams(pattern, arg, binding, values, why, packs, true);
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

// **[temp.deduct.call]/4.3: a parameter `V<K>`, `V<K> &` or `V<K> *` deduces from a base**
// of the argument's class when the class itself is no `V` - `q * r` for `Q<4> : V<4>` gives
// K = 4. Only at the parameter's top, and only where exactly one base deduces.
bool Templates::deduceFromArgument(const Type *param, const Type *arg,
                                std::vector<const Type *> *binding,
                                std::vector<long long> *values, std::string *why, Packs *packs) {
    // A function's name arrives as its address; `R (&)(A)` deduces from the function itself.
    if (param->isReference() && param->referent()->isFunction() && arg != nullptr && arg->isPointer() &&
        arg->pointee()->isFunction())
        arg = arg->pointee();
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
        const Type *as = viaPointer ? p_.types_.pointerTo(b) : b;
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
bool Templates::deduceTemplateArguments(const TemplateDecl &decl,
                                     const std::vector<const Type *> &argTypes,
                                     std::vector<const Type *> *binding,
                                     std::vector<long long> *values,
                                     Packs *packs,
                                     std::string *why, std::size_t explicitCount) {
    // [temp.arg.explicit]/3: the first explicitCount are written, not deduced - read into the pattern as the
    // types and values they are, so an argument meets them by conversion ([temp.arg.explicit]/6).
    const std::vector<const Type *> written(binding->begin(), binding->begin() + static_cast<std::ptrdiff_t>(explicitCount));
    const std::vector<long long> writtenValues(values->begin(), values->begin() + static_cast<std::ptrdiff_t>(explicitCount));
    packs->assign(decl.params.size(), std::vector<const Type *>());
    values->assign(decl.params.size(), 0);

    std::vector<const Type *> pattern(decl.params.size());
    std::vector<long long> none(decl.params.size(), 0);
    for (std::size_t i = 0; i < decl.params.size(); i++)
        pattern[i] = i < explicitCount ? written[i] : p_.types_.templateParam(static_cast<int>(i));
    for (std::size_t i = 0; i < explicitCount; i++) none[i] = (*values)[i] = writtenValues[i];
    std::string ignored;
    const Type *fn = p_.readTemplateDeclaration(decl, pattern, none, &ignored);

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
    for (std::size_t i = 0; i < p_.pendingDefaults_.size() && i < fixed; i++)
        if (p_.pendingDefaults_[i] != 0) { least = i; break; }
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
    for (std::size_t i = 0; i < explicitCount; i++) (*binding)[i] = written[i];
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
            else members.push_back(p_.decayedType(a->isReference() ? a->referent() : a));
        }
        (*packs)[decl.params.size() - 1] = members;
    }
    for (std::size_t i = 0; i < binding->size(); i++) {
        // A pack deduced from an expansion was marked in `binding` by its own parameter; the function
        // parameter pack binds through `packs` alone. Neither is a type to leave in the binding.
        if (decl.params[i].isPack) { (*binding)[i] = nullptr; continue; }
        if ((*binding)[i] != nullptr) continue;
        // [temp.deduct]/7: a default that does not substitute makes the deduction fail, and no more.
        bool took = false;
        try {
            Parser::Trial trial(&p_);
            took = p_.defaultTemplateArgument(decl, i, binding, values);
        } catch (const SubstitutionFailure &f) {
            countDefault(decl.params[i], true);
            if (f.unsupported) p_.src_.fail(f.pos, f.why);
            *why = "the default argument of '" + decl.params[i].name + "' does not substitute: " + f.why;
            return false;
        }
        if (took) countDefault(decl.params[i], true);
        if (!took) {
            *why = "'" + decl.params[i].name + "' appears in no parameter, so "
                   "there is nothing in the call to work it out from - write "
                   "the arguments out";
            return false;
        }
    }
    return true;
}

// [temp.arg.nontype]/1: a converted constant expression, which may not narrow. An 8-byte type is
// left alone: the value's own signedness is not kept here.
void Templates::checkNonTypeArgument(const Type *pt, long long v, std::size_t argPos) const {
    if (!(pt->isInteger() && pt->tag().empty() && pt->size(p_.target_) < 8)) return;
    const int bits = pt->kind() == Kind::Bool ? 1 : 8 * pt->size(p_.target_);
    const bool sgn = pt->kind() != Kind::Bool && pt->isSigned(p_.target_);
    const long long lo = sgn ? -(1LL << (bits - 1)) : 0;
    const long long hi = sgn ? (1LL << (bits - 1)) - 1 : (1LL << bits) - 1;
    if (v < lo || v > hi)
        p_.src_.fail(argPos, "the template argument is " + std::to_string(v) +
                          ", which cannot be narrowed to '" + pt->describe() + "'");
}

// Whether a type holds a pattern's `R...` anywhere in a template-id's arguments.
bool Templates::mentionsPackExpansion(const Type *t) {
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

// **[temp.param]/11's second sentence**: a function template's pack may be followed by a
// parameter the call can work out - one a function parameter names - or one with a default.
void Templates::requireDeducibleAfterPack(const TemplateDecl &decl, const Type *fn) {
    const std::size_t from = Parser::afterPack(decl.params);
    if (from == static_cast<std::size_t>(-1)) return;
    for (std::size_t i = from; i < decl.params.size(); i++) {
        if (decl.params[i].defBegin != 0 || decl.params[i].isPack) continue;
        bool named = false;
        if (fn != nullptr && fn->kind() == Kind::Function)
            for (std::size_t k = 0; k < fn->params().size() && !named; k++)
                named = mentionsParam(fn->params()[k], i);
        if (!named)
            p_.src_.fail(decl.params[i].pos, "'" + decl.params[i].name + "' follows the parameter pack '" +
                                          decl.params[from - 1].name + "' and no function parameter "
                                          "names it, so a call could never work it out - a parameter "
                                          "after a pack has to be deducible or defaulted");
    }
}

// **A function type against a function type**, for deduction ([temp.deduct.type]/8's `T(T)`) and for a
// partial specialization alike: the return, then each parameter, a trailing `A...` taking the rest of
// the argument's into its pack - `function<R(Args...)>` is that pattern (M1 L1b). Varargs match varargs.
bool Templates::matchFunctionParams(const Type *pattern, const Type *arg, std::vector<const Type *> *binding,
                                 std::vector<long long> *values, std::string *why, Packs *packs,
                                 bool deduce) const {
    const std::vector<const Type *> &pp = pattern->params(), &ap = arg->params();
    const bool rest = !pp.empty() && pp.back()->kind() == Kind::PackExpansion;
    const std::size_t fixed = rest ? pp.size() - 1 : pp.size();
    if (rest ? ap.size() < fixed : pp.size() != ap.size()) {
        *why = "'" + arg->describe() + "' takes a different number of parameters from '" + pattern->describe() + "'";
        return false;
    }
    if (pattern->isVariadicFn() != arg->isVariadicFn() || pattern->refQualifier() != arg->refQualifier() ||
        pattern->isConstThisFn() != arg->isConstThisFn()) {
        *why = "'" + arg->describe() + "' is not of the form '" + pattern->describe() + "'";
        return false;
    }
    auto one = [&](const Type *p, const Type *a) {
        return deduce ? deduceOne(p, a, binding, values, why, packs, true) : matchPattern(p, a, binding, why, packs);
    };
    if (!one(pattern->returns(), arg->returns())) return false;
    for (std::size_t i = 0; i < fixed; i++)
        if (!one(pp[i], ap[i])) return false;
    if (!rest) return true;
    // The expansion's pattern is the pack itself here; a shaped one, `const A &...`, is not read yet.
    const Type *shape = pp.back()->pointee();
    if (shape == nullptr || shape->kind() != Kind::TemplateParam || packs == nullptr ||
        static_cast<std::size_t>(shape->length()) >= packs->size()) {
        *why = "a parameter pack of that shape in a function type cannot be deduced yet";
        return false;
    }
    const std::size_t at = static_cast<std::size_t>(shape->length());
    PackMembers got(std::vector<const Type *>(ap.begin() + static_cast<std::ptrdiff_t>(fixed), ap.end()));
    if ((*binding)[at] != nullptr && static_cast<const std::vector<const Type *> &>((*packs)[at]) != got) {
        *why = "the parameter pack is deduced two different lists";
        return false;
    }
    (*packs)[at] = got;
    // Deduction marks the pack deduced in the binding, as a template-id's pack does; a partial binds it in packs alone.
    if (deduce) (*binding)[at] = shape;
    return true;
}

// [temp.deduct.type]. A pattern that is a pointer matches a pointer and
// nothing else - there is no conversion here for a mismatch to be forgiven
// by, which is what makes this stricter than deduction from a call.
bool Templates::matchPattern(const Type *pattern, const Type *arg,
                          std::vector<const Type *> *binding,
                          std::string *why, Packs *packs, std::vector<long long> *values) const {
    // **The qualifier is asked about before anything else, and both sides must
    // agree.** `Box<const T>` matches `Box<const int>` and not `Box<int>`; `Box<T>`
    // matches both, binding T to the qualified type where there is one.
    if (pattern->unqualified() != pattern) {
        if (arg->unqualified() == arg) {
            *why = "'" + arg->describe() + "' is not const";
            return false;
        }
        return matchPattern(pattern->unqualified(), arg->unqualified(),
                            binding, why, packs, values);
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
               matchPattern(pattern->pointee(), arg->pointee(), binding, why, packs, values);
    // `T &` and `T &&` are two patterns: `remove_reference<T &&>` must not take an `int &`.
    if (pattern->isReference())
        return arg->isReference() && arg->isRValueReference() == pattern->isRValueReference() &&
               matchPattern(pattern->referent(), arg->referent(), binding, why, packs, values);
    if (pattern->isArray()) {
        if (!arg->isArray()) return false;
        // `T[N]`: N is a parameter, and the argument's length is its value ([temp.deduct.type]/8).
        const int j = pattern->boundParam();
        if (j >= 0 && values != nullptr && static_cast<std::size_t>(j) < values->size() && arg->length() >= 0)
            (*values)[j] = arg->length();
        else if (pattern->length() != arg->length()) return false;
        return matchPattern(pattern->pointee(), arg->pointee(), binding, why, packs, values);
    }
    if (pattern->isMemberPointer())
        return arg->isMemberPointer() &&
               matchPattern(pattern->pointee(), arg->pointee(), binding, why, packs, values) &&
               matchPattern(pattern->enclosing(), arg->enclosing(), binding, why, packs, values);
    if (pattern->isFunction())
        return arg->isFunction() && matchFunctionParams(pattern, arg, binding, nullptr, why, packs, false);

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
void Templates::partialArguments(TemplateDecl::Partial *ps,
                              const std::vector<TemplateParam> &primary) {
    const std::size_t count = primary.size();
    // **A variadic primary is not written a fixed number of arguments**: a pack
    // stands for a list, so the closing angle says where the pattern stops and not
    // the parameter count - which gave "more arguments than parameters" before.
    const bool variadic = !primary.empty() && primary.back().isPack;
    p_.expect("<");
    const bool wasInArgs = p_.inTemplateArgs_;
    const bool wasPattern = p_.patternOnly_;
    p_.inTemplateArgs_ = true;
    p_.patternOnly_ = true;

    std::vector<Shadow> undo;
    std::vector<const Type *> binding(ps->params.size());
    std::vector<long long> values(ps->params.size(), 1);
    for (std::size_t i = 0; i < ps->params.size(); i++)
        binding[i] = ps->params[i].type == nullptr || ps->params[i].isPack
                         ? p_.types_.templateParam(static_cast<int>(i))
                         : ps->params[i].type;
    bindTemplateParameters(ps->params, binding, values,
                           Packs(), &undo);
    // A non-type parameter written as an array bound, `is_array<T[N]>`, is the pattern's N (M1).
    std::map<std::string, int> boundWas;
    for (std::size_t i = 0; i < ps->params.size(); i++)
        if (ps->params[i].type != nullptr && !ps->params[i].isPack) {
            auto had = p_.nonTypePatternParams_.find(ps->params[i].name);
            boundWas[ps->params[i].name] = had == p_.nonTypePatternParams_.end() ? -1 : had->second;
            p_.nonTypePatternParams_[ps->params[i].name] = static_cast<int>(i);
        }

    for (std::size_t i = 0; variadic ? !p_.atClosingAngle() : i < count; i++) {
        if (i > 0) p_.expect(",");
        TemplateDecl::Partial::Arg a;

        // `R...` - this specialization's own pack, standing for everything
        // the fixed arguments before it did not take.
        if (p_.peek().kind == TokenKind::Ident && p_.peekAt(1).is("...")) {
            std::size_t k = ps->params.size();
            for (std::size_t j = 0; j < ps->params.size(); j++)
                if (ps->params[j].isPack && ps->params[j].name == p_.peek().text)
                    k = j;
            if (k < ps->params.size()) {
                a.isType = true;
                a.isPackExpansion = true;
                a.param = k;
                p_.at_ += 2;
                ps->args.push_back(a);
                continue;
            }
        }
        // **A non-type argument that is one of our own parameters is the only
        // shape of one that deduces**, so it is recognised by its tokens
        // before it can be folded into the value it was bound to.
        std::size_t which = ps->params.size();
        if (p_.peek().kind == TokenKind::Ident)
            for (std::size_t k = 0; k < ps->params.size(); k++)
                if (ps->params[k].type != nullptr &&
                    ps->params[k].name == p_.peek().text) which = k;
        if (which < ps->params.size() &&
            (p_.peekAt(1).is(",") || p_.peekAt(1).is(">") || p_.peekAt(1).is(">>"))) {
            a.isType = false;
            a.isParam = true;
            a.param = which;
            p_.at_++;
        } else if (p_.atTypeName()) {
            const std::size_t tpos = p_.peek().pos;
            Parser::StorageClass sc;
            Parser::Qualifiers quals;
            const Type *base = p_.specifiers(&sc, &quals);
            Parser::Declared d = p_.declarator(base, true);
            a.isType = true;
            a.type = d.type;
            // `Box<R...>` inside a partial's argument: matchPattern binds types, and a pack is a list.
            if (mentionsPackExpansion(d.type))
                p_.src_.fail(tpos, "a pack expanded inside a nested template-id in a partial specialization's "
                                "arguments is not supported yet - 'R...' as an argument of its own is");
        } else {
            a.isType = false;
            a.value = p_.constantExpression("a template argument");
        }
        ps->args.push_back(a);
    }
    if (!p_.atClosingAngle())
        p_.src_.fail(p_.peek().pos, "this specialization gives more arguments than "
                              "the template has parameters");
    p_.takeClosingAngle();
    // A pack expansion may only be last: everything after it could never be
    // told apart from a member of it. Same rule, and the same reason, as a
    // pack having to be the last *parameter*.
    for (std::size_t i = 0; i + 1 < ps->args.size(); i++)
        if (ps->args[i].isPackExpansion)
            p_.src_.fail(ps->params.empty() ? 0 : ps->params[0].pos,
                      "a pack expansion has to be the last argument of a "
                      "specialization - anything after it could never be told "
                      "apart from one of its members");

    for (const std::pair<const std::string, int> &w : boundWas) {
        if (w.second < 0) p_.nonTypePatternParams_.erase(w.first);
        else p_.nonTypePatternParams_[w.first] = w.second;
    }
    unbindTemplateParameters(undo);
    p_.inTemplateArgs_ = wasInArgs;
    p_.patternOnly_ = wasPattern;
}

// [temp.class.order], asked the standard's own way: A is at least as specialized as
// B when B's pattern matches A's. A's parameters stand as opaque types while that
// happens, which is exactly what Kind::TemplateParam already is.
bool Templates::atLeastAsSpecialized(const TemplateDecl::Partial &a,
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

bool Templates::moreSpecialized(const TemplateDecl::Partial &a,
                             const TemplateDecl::Partial &b) const {
    return atLeastAsSpecialized(a, b) && !atLeastAsSpecialized(b, a);
}

// Which partial specialization these arguments ask for.
std::size_t Templates::choosePartial(const TemplateDecl &decl,
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
            if (!matchPattern(a.type, flat[i].type, &b, &why, &pk, &v)) ok = false;
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
            p_.src_.fail(pos, "'" + decl.name + "' has two partial "
                           "specializations that fit these arguments and "
                           "neither is more specialized than the other");

    *binding = bindings[best];
    *values = valueSets[best];
    if (packs != nullptr) *packs = packSets[best];
    return fits[best];
}

// `Box<int, 3>` as a type - rung 5.4: replays `struct Box { ... };` with the arguments bound, told the tag.
// The class's tokens from `start` read under a binding: the class made under `tag`, its member bodies held in `bodies`.
const Type *Templates::readClassBody(const TemplateDecl &decl, const std::string &tag, bool partial, std::size_t start,
                                     const std::vector<TemplateArg> &args, const std::vector<TemplateParam> &useParams,
                                     const std::vector<const Type *> &useBinding, const std::vector<long long> &useValues,
                                     const Packs &packs, std::vector<Parser::PendingBody> *bodies, std::vector<Shadow> *kept) {
    const std::size_t resume = p_.at_;
    std::vector<Shadow> mine;
    // A read keeps the parameters bound for the member bodies and takes them back itself, refused or not.
    std::vector<Shadow> &undo = kept != nullptr ? *kept : mine;
    // **A chosen partial binds its own pack, not the primary's.** `L<T, R...>` on
    // `L<int, char, long>` leaves R holding {char, long}, which is what the replayed
    // body has to see - the primary's Ts is not in scope at all.
    bindTemplateParameters(useParams, useBinding, useValues, packs, &undo);
    p_.at_ = start;
    reentered("jump: a class body");
    // **A specialization is not a member of whatever class asked for it.**
    std::vector<const Type *> outerClasses;
    outerClasses.swap(p_.classStack_);
    const Type *outerCurrent = p_.currentClass_;
    p_.currentClass_ = nullptr;
    const std::string outerInline = p_.inlineOwner_;
    p_.inlineOwner_.clear();
    // **The `>>` mark is one slot and this parse can spend it.**
    const std::size_t outerAngle = p_.angleSplit_;

    // **The class being read around this one keeps its own**: a member template recorded after
    // `Vec<N>` in Mat's body would otherwise capture Vec's parameters as Mat's.
    const std::vector<TemplateArg> outerArgs = p_.instantiatingArgs_;
    const std::string outerNamespace = p_.instantiatingNamespace_;
    const auto outerParams = p_.instantiatingParams_;
    const auto outerBinding = p_.instantiatingBinding_;
    const auto outerValues = p_.instantiatingValues_;
    p_.classInstantiationTag_ = tag;
    if (partial) p_.classInstantiationOf_ = decl.name;
    p_.instantiatingArgs_ = args;
    p_.instantiatingNamespace_ = decl.ns;
    p_.instantiatingParams_ = useParams;
    p_.instantiatingBinding_ = useBinding;
    p_.instantiatingValues_ = useValues;
    p_.heldForSpecialization_.clear();
    const bool wasDeferring = p_.deferSpecializationBodies_;
    p_.deferSpecializationBodies_ = true;
    Parser::StorageClass sc;
    Parser::Qualifiers quals;
    // Nor in a class template's: `V<3> N(...)` made V<3> read its own `double s[N]` as the local.
    std::unique_ptr<Parser::LocalsHidden> hidden(new Parser::LocalsHidden(&p_));
    // And its body names things in its own namespace, as a function template's does, wherever it is used from.
    const std::vector<std::string> outerStack = p_.namespaceStack_;
    if (!decl.ns.empty()) p_.namespaceStack_ = Parser::namespacePathStack(decl.ns);
    p_.readLayout_ = kept != nullptr;      // a read's class is laid out over its dependent members and bases
    const Type *made = partial
        ? p_.structOrUnionSpecifier(Kind::Struct, false)
        : p_.specifiers(&sc, &quals);
    p_.readLayout_ = false;
    p_.namespaceStack_ = outerStack;
    p_.angleSplit_ = outerAngle;
    p_.classInstantiationTag_.clear();
    p_.classInstantiationOf_.clear();
    p_.instantiatingArgs_ = outerArgs;
    p_.instantiatingNamespace_ = outerNamespace;
    p_.instantiatingParams_ = outerParams;
    p_.instantiatingBinding_ = outerBinding;
    p_.instantiatingValues_ = outerValues;
    hidden.reset();
    p_.classStack_.swap(outerClasses);
    p_.currentClass_ = outerCurrent;
    p_.inlineOwner_ = outerInline;
    p_.deferSpecializationBodies_ = wasDeferring;
    bodies->swap(p_.heldForSpecialization_);

    if (kept == nullptr) unbindTemplateParameters(undo);
    p_.at_ = resume;
    return made;
}

const Type *Templates::instantiateClass(const TemplateDecl &decl, std::size_t pos) {
    // Before the arguments are read: `A<A<A<...>>>` instantiates from the inside out, through here.
    const Parser::Nesting deep(&p_, pos, "template instantiation");
    std::vector<const Type *> binding;
    std::vector<long long> values;
    std::vector<TemplateArg> args;
    Packs packs;
    p_.templateArguments(decl, &binding, &values, &args, &packs);

    // [temp.alias]/2: an alias template's id is its type-id with the arguments substituted.
    if (decl.isAlias) {
        const std::size_t resume = p_.at_;
        std::vector<Shadow> undo;
        struct Unbind {
            Parser *p; std::vector<Shadow> *u;
            ~Unbind() { p->tmpl_.unbindTemplateParameters(*u); }
        } unbind{ &p_, &undo };
        const DoorAt alias(this, "alias");
        bindTemplateParameters(decl.params, binding, values, packs, &undo);
        count(AliasId);
        p_.at_ = decl.aliasAt;
        reentered("jump: an alias's type-id");
        Parser::StorageClass sc;
        Parser::Qualifiers quals;
        const Type *base = p_.specifiers(&sc, &quals);
        const Type *t = p_.declarator(base, true).type;
        p_.at_ = resume;
        return t;
    }
    const std::string tag = specializationKey(decl.name, args);
    if (p_.patternOnly_ && readPattern_.count(tag) != 0) return readPattern_[tag];     // a class door's read: the class itself
    if (p_.patternOnly_ && !readingTag_.empty() && tag == readingTag_)                // ... and while its body is read
        if (const Type *own = p_.lookup_.findTypedef(readingRead_)) return own;

    // **A pattern's `C<T>`**, the stand-in applied: a shape deduction matches and Itanium spells `T_I...E`.
    if (decl.ttParam >= 0) {
        Type *shallow = p_.types_.structType(Kind::Struct, tag);
        if (!shallow->isSpecialization()) {
            shallow->setSpecialization(decl.name, args);
            if (classDoor_) readShallow_.push_back(shallow);
        }
        p_.lookup_.declareTypeName(tag, shallow);
        return shallow;
    }

    // Reading a pattern, not building a class: `Holder<T>` cannot be laid out, and the mangler
    // and deduction read only its name and arguments. An argument list that mentions no
    // parameter is a real class - `enable_if<2 + 2 == 4, int>`.
    if (p_.patternOnly_ && p_.dependentArgs(args)) {
        Type *shallow = p_.types_.structType(Kind::Struct, tag);
        if (!shallow->isSpecialization()) {
            shallow->setSpecialization(decl.name, args);
            shallow->setTemplateNamespace(decl.ns);
            if (classDoor_) readShallow_.push_back(shallow);
        }
        // Registered so that `Box<T>::get` reads: the declarator's qualified
        // path looks the class up by name, and this is the only name it has.
        // The tag holds a `$` and so cannot collide with anything written.
        p_.lookup_.declareTypeName(tag, shallow);
        return shallow;
    }

    if (const Type *had = p_.lookup_.findTypedef(tag)) {
        asked(tag, pos);
        return had;
    }
    // A class cannot be unmade when the read's layer goes: one the program has not asked for is held incomplete under its tag;
    // what needs its members refuses there by name, and the read is made again with the class made below it (makeWanted).
    if (p_.patternUses_) {
        Type *named = p_.types_.structType(Kind::Struct, tag);
        if (!named->isSpecialization()) {
            named->setSpecialization(decl.name, args);
            named->setTemplateNamespace(decl.ns);
            named->noteClassKey(decl.classKey);
        }
        if (!named->isComplete()) {
            p_.readHeld_.insert(named);
            if (held_.count(named) == 0) held_[named] = Held{ decl, binding, values, args, packs, pos };
        }
        return named;
    }
    return makeClass(decl, tag, binding, values, args, packs, pos);
}

// The class `tag` made for good: its partial chosen, its body read under the arguments, its member bodies owed.
const Type *Templates::makeClass(const TemplateDecl &decl, const std::string &tag, const std::vector<const Type *> &binding, const std::vector<long long> &values,
                                 const std::vector<TemplateArg> &args, const Packs &packs, std::size_t pos) {
    const std::size_t logFrom = seatLog_.size(), fnFrom = p_.lookup_.functions_.size();
    const Parser::ReplayUses replayed(&p_);
    const DoorAt classDoor(this, "class");

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
        Type *shallow = p_.types_.structType(Kind::Struct, tag);
        if (!shallow->isSpecialization()) {
            shallow->setSpecialization(decl.name, args);
            shallow->setTemplateNamespace(decl.ns);
            shallow->noteClassKey(decl.classKey);
        }
        return shallow;
    }

    const std::size_t start = partial ? decl.partials[which].bodyAt : decl.afterParams;
    count(ClassBody);
    // A base named through a parameter, counted: a `:` in the head and a parameter's name before the `{`.
    bool colon = false, dependent = false;
    for (std::size_t t = start; t < p_.tokens_.size() && !p_.tokens_[t].is("{") && !p_.tokens_[t].is(";"); t++) {
        colon = colon || p_.tokens_[t].is(":");
        for (std::size_t q = 0; colon && q < useParams.size(); q++) dependent = dependent || p_.tokens_[t].text == useParams[q].name;
    }
    if (dependent) count(DependentBase);
    std::vector<Parser::PendingBody> bodies;
    Substituted early;
    early.values = useValues, early.packs = partial ? usePacks : packs;
    // An id naming the class itself (`__deque_iterator<T, T &, T *, deque>`): its tag declared first, as the replay declares it.
    Type *ahead = substitute_ && !p_.patternUses_ && !partial && namesItself(start) ? p_.types_.structType(readClass_.at(start)->kind(), tag) : nullptr;
    if (ahead != nullptr && !ahead->isSpecialization()) {     // spelled as the replay's head spells it, for the ids' names
        ahead->setSpecialization(decl.name, args);
        ahead->setTemplateNamespace(decl.ns);
        ahead->noteClassKey(decl.classKey);
    }
    if (ahead != nullptr) readStands(start, ahead, &early);
    const std::string met = substitute_ && !p_.patternUses_ ? idsBefore(start, useParams, useBinding, useValues, pos, early.packs, ahead) : std::string();
    const std::string before = substitute_ && !p_.patternUses_ ? classBefore(start, useParams, useBinding, &early) : std::string();
    const std::size_t typedefsFrom = p_.lookup_.typedefs_.size();
    // Step (1): a class the door takes made from the read, its written members and held bodies too; the replay beside under the check.
    std::string fromRead = before.compare(0, 18, "made from the read") == 0 ? fromReadWhyNot(start, useParams) : "-";
    if (fromRead.empty() && checkBodies_) fromRead = "beside: the replay the program's, under -check=templates";
    Substituted bound;
    bound.values = useValues, bound.packs = early.packs;
    if (ahead != nullptr) readStands(start, ahead, &bound);
    if (fromRead.empty()) classShape(start, useParams, useBinding, &bound);
    const Type *made = fromRead.empty() ? classFromRead(decl, tag, partial, start, args, bound, pos, &bodies, useParams, useBinding, useValues)
                     : readClassBody(decl, tag, partial, start, args, useParams, useBinding, useValues, partial ? usePacks : packs, &bodies, nullptr);
    if (fromRead != "-") replayCensus_["class: " + (fromRead.empty() ? std::string("made from the read with no replay") : fromRead)]++;
    // The measure: one verdict a tag - step (1)'s, else classBefore's or idsBefore's reason, as "before the replay" says it.
    const std::string why = fromRead != "-" ? fromRead : !substitute_ || p_.patternUses_ ? std::string("not under -templates=subst") : met.empty() ? before : met;
    const std::string verdict = fromRead.empty() ? std::string("made from the read with no replay")
                              : fromRead.compare(0, 7, "beside:") == 0 ? std::string("made from the read, the replay beside under -check=templates")
                              : why.compare(0, 9, "replayed:") == 0 ? why : "replayed: " + why;
    if (!specializationCensus_.insert(std::make_pair(tag, verdict)).second) specializationAgain_++;

    if (!made->isStructOrUnion())
        p_.src_.fail(pos, "'" + decl.name + "' is not a class template");
    p_.lookup_.declareTypeName(tag, made);
    // M8.3 slice 3a: the class the read laid, under the binding, held against the one replayed; the replay's is the program's.
    if (substitute_ && !p_.patternUses_) {
        Substituted s;
        s.values = useValues, s.packs = early.packs;
        if (ahead != nullptr) readStands(start, ahead, &s);
        const std::string shape = classShape(start, useParams, useBinding, &s, &early);
        Substituted laidAs = s;
        readStands(start, made, &laidAs);
        std::string held = shape.empty() && checkBodies_ ? classLaid(readClass_.at(start), laidAs, made) : shape;
        if (held == "equal") held = staticsHeld(start, decl, tag, s, made, useParams, useBinding, useValues);
        // Its member functions' symbols, the replay's for the read's: held under the check, kept for its bodies' records either way.
        const std::string members = held.empty() || held == "equal" ? classMembers(start, s, made, tag, fnFrom, &readSymbol_) : held;
        if (held == "equal") held = members;
        classCensus_[!held.empty() ? held : "covered"]++;
        std::string skip;
        if (held.empty() || held == "equal") {      // the read class standing for the replay's, as classMembers holds it
            coveredClass_[tag] = s;
            readStands(start, made, &coveredClass_[tag]);
            skip = classSkip(start, tag, bodies, decl.name, typedefsFrom);
            classCensus_["skip: " + skip]++;
            classCensus_["skip, member bodies: " + skip] += static_cast<int>(bodies.size());
        }
        // Step (1) decides before the replay: what it takes is what the skip takes after it, or the two are apart.
        const bool takes = before.compare(0, 18, "made from the read") == 0;
        classCensus_["before the replay: " + (takes || met.empty() ? before : met)]++;     // what idsBefore met, if it stopped
        if (takes != (skip.compare(0, 18, "made from the read") == 0))
            classCensus_["differ: the class skip before the replay and after it: " + (takes ? skip : before)]++;
        classWhy_[tag] = held.empty() ? "covered" : held;
    }

    Parser::Specialization sp;
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
    // Its member bodies, replayed later, ask from one deeper than the body that asked for the class.
    sp.depth = p_.instantiationDepth_ + 1;
    refuseInstantiationDepth(decl.name, sp.depth, pos);
    specializations_.push_back(sp);
    noteMade(tag, logFrom, fnFrom);
    return made;
}

// A tag the read named over its parameters (`$T0`, `$P1`, `$read3`): a `$` that is no program closure's `$_0` or `$deduced_0`.
static bool overParameters(const std::string &tag) {
    for (std::size_t at = tag.find('$'); at != std::string::npos; at = tag.find('$', at + 1))
        if (tag.compare(at, 2, "$_") != 0 && tag.compare(at, 9, "$deduced_") != 0) return true;
    return false;
}

// The kind of a type a signature names that the binding does not carry, for the class census.
static std::string uncarriedKind(const Type *e, const Type *read) {
    if (e->kind() == Kind::TemplateParam) return "a template parameter";
    if (e->kind() == Kind::DependentExpr) return "a dependent expression's type";
    if (e->isFunction()) return "a function type";
    if (e->kind() == Kind::DependentMember) {
        const Type *o = e->pointee()->unqualified();
        if (o == read || (o->isSpecialization() && o->templateName() == read->templateName())) return "a member type of its own class";
        return o->kind() == Kind::TemplateParam ? "a member type of a type parameter" : "a member type of another class over its parameters";
    }
    if (!e->isStructOrUnion()) return "another kind";
    if (e->dependentLayout()) return "a nested class laid over its parameters";
    if (!overParameters(e->tag())) return "a class template-id over dependent arguments";
    if (e->isSpecialization() && e->templateName() == read->templateName()) return "its own template under other arguments";
    return "a class template-id over its parameters the program had not declared";
}

// A class template-id over the parameters under `s`: the class the program declared before the replay (`early`'s), or null.
const Type *Templates::carriedId(const Substituted &s, const Type *e, const Substituted *early) const {
    if (early != nullptr) {
        for (std::size_t k = 0; k < early->from.size(); k++) if (early->from[k] == e) return early->to[k];
        return nullptr;
    }
    const Substitution declared = substitute(s, e, Declared);
    const Type *real = declared.carried() ? declared.type : nullptr;
    return real == nullptr || !real->isStructOrUnion() || p_.dependentType(real) || readIds_.count(real) != 0 || overParameters(real->tag()) ? nullptr : real;
}

// A class template-id the read named over its parameters: its own, or the one the read's end cleared (readIds_).
bool Templates::readId(const Type *e, std::string *name, std::vector<TemplateArg> *args) const {
    const std::map<const Type *, ReadId>::const_iterator k = readIds_.find(e);
    if (!e->isSpecialization() && k == readIds_.end()) return false;
    if (name != nullptr) *name = e->isSpecialization() ? e->templateName() : k->second.name;
    if (args != nullptr) *args = e->isSpecialization() ? e->templateArgs() : k->second.args;
    return true;
}

// Slice 3s: readId's, a template template parameter's stand-in `$TT<i>` named as the template `s` binds to it (as `templates_[C]`).
bool Templates::idUnder(const Substituted &s, const Type *e, std::string *name, std::vector<TemplateArg> *args) const {
    int tt = -1;
    if (!readId(e, name, args)) return false;
    for (std::size_t k = 0; args != nullptr && k < args->size(); k++) {     // 3t: `Holder<C, int>`, C an argument
        TemplateArg &a = (*args)[k];
        const int i = a.isTemplate && a.type != nullptr && a.type->kind() == Kind::TemplateParam ? static_cast<int>(a.type->length()) : -1;
        if (s.templates.count(i) != 0) a.type = p_.templateNameTypeOf(templates_.at(s.templates.at(i)));
    }
    if (!ttRefName(*name, &tt) || s.templates.count(tt) == 0) return true;
    *name = s.templates.at(tt);
    if (args == nullptr) return true;
    // 3t: the stand-in's list, laid by the bound template's own (`C<int>` over `template <class...> class C`) as templateArguments
    // lays it: each pack's members one argument, a pack parameter takes the rest, a missing one its default (defaultTemplateArgument,
    // the deduced path's, with the ones before it bound); false where a member stays a parameter.
    const TemplateDecl &d = templates_.at(*name);
    std::vector<TemplateArg> flat, out;
    for (std::size_t j = 0; j < args->size(); j++) {
        TemplateArg a = (*args)[j];
        if (!a.isPack) { flat.push_back(a); continue; }
        if (a.isTemplate || a.packIsValue || !argUnder(s, &a, false)) return false;
        for (const Type *m : a.pack) { TemplateArg one; one.type = m; flat.push_back(one); }
    }
    std::size_t k = 0;
    bool open = false;     // 3v: an argument still an id over the parameters (`C<C<T> >`'s inner), which earlyArgs finds pending
    std::vector<const Type *> binding(d.params.size(), nullptr);
    std::vector<long long> values(d.params.size(), 0);
    for (std::size_t q = 0; q < d.params.size(); q++) {
        const TemplateParam &p = d.params[q];
        TemplateArg a;
        if (p.isPack && (p.type != nullptr || p.isTemplate)) return false;
        for (a.isPack = p.isPack; p.isPack && k < flat.size(); k++) {
            if (!flat[k].isType || flat[k].isTemplate) return false;
            a.pack.push_back(substitute(s, flat[k].type).type);
        }
        if (!p.isPack && k < flat.size()) a = flat[k++];
        else if (!p.isPack) {
            if (open || !p_.defaultTemplateArgument(d, q, &binding, &values)) return false;
            countDefault(p, false);
            if (binding[q] == nullptr) return false;
            a.isType = p.type == nullptr, a.isTemplate = p.isTemplate, a.type = binding[q], a.value = values[q];
        }
        if (!a.isPack && (a.isTemplate != p.isTemplate || a.isType != (p.type == nullptr) || a.isParam || a.expr != nullptr || a.dependentValue))
            return false;
        binding[q] = a.isPack || a.isTemplate || !a.isType ? a.type : substitute(s, a.type).type, values[q] = a.isType ? 0 : a.value;
        const bool id = a.isType && binding[q] != nullptr && binding[q]->isStructOrUnion() && readId(binding[q], nullptr, nullptr) &&
                        overParameters(binding[q]->tag());
        open = open || (id && p_.dependentType(binding[q]));
        if (!a.isPack && !a.isTemplate && (binding[q] == nullptr || (p_.dependentType(binding[q]) && !id))) return false;
        out.push_back(a);
    }
    if (k != flat.size()) return false;
    *args = out;
    return true;
}

// Slice 3t: an alias template whose type-id is a class template's id over the alias's type parameters in order (`using A = B<T>;`):
// that template, whose specialization the replay's `C<T>` reaches through the alias; end() for any other alias.
std::map<std::string, TemplateDecl>::const_iterator Templates::aliasedClass(const TemplateDecl &a) const {
    std::size_t t = a.aliasAt + (p_.tokens_[a.aliasAt].is("::") ? 1 : 0);
    while (t + 2 < p_.tokens_.size() && p_.tokens_[t + 1].is("::")) t += 2;
    const std::size_t n = a.params.size();
    const std::map<std::string, TemplateDecl>::const_iterator c = p_.findTemplate(p_.tokens_[t].text);
    if (c == templates_.end() || !c->second.isClass || c->second.isAlias || c->second.ttParam >= 0 || c->first != c->second.name ||
        c->second.params.size() != n || t + 2 * n + 2 >= p_.tokens_.size() || !p_.tokens_[t + 1].is("<") || !p_.tokens_[t + 2 * n + 2].is(";"))
        return templates_.end();
    for (std::size_t q = 0; q < n; q++)
        if (a.params[q].isPack || a.params[q].isTemplate || a.params[q].type != nullptr || c->second.params[q].isPack || c->second.params[q].isTemplate ||
            c->second.params[q].type != nullptr || p_.tokens_[t + 2 + 2 * q].text != a.params[q].name || !p_.tokens_[t + 3 + 2 * q].is(q + 1 < n ? "," : ">"))
            return templates_.end();
    return c;
}

// An id over the parameters the program had not declared, its arguments under `s`: of another template's type parameters alone,
// each argument a type the binding carries (no class the read named over its parameters); false where the door cannot make it.
bool Templates::earlyArgs(const Substituted &s, const Type *e, const Type *read, std::string *name, std::vector<TemplateArg> *args, bool itself,
                          const std::vector<const Type *> *pending, bool base) const {
    std::string written;
    if (!idUnder(s, e, name, args) || !readId(e, &written, nullptr)) return false;
    const std::map<std::string, TemplateDecl>::const_iterator d = templates_.find(*name);
    const bool ownBase = base && !itself && *name == read->templateName();     // 3z: `Tuple<Rest...>`, made reading the head, as the replay
    if (d == templates_.end() || ((*name == read->templateName()) != itself && !ownBase) || d->second.isAlias || d->second.ttParam >= 0) return false;
    // One whose body names the read's own template is made inside the class the replay has begun: the replay's, not before it.
    const std::string own = read->templateName().substr(read->templateName().rfind(':') == std::string::npos ? 0 : read->templateName().rfind(':') + 1);
    std::vector<std::size_t> bodies(itself || ownBase ? 0 : 1, d->second.afterParams);     // the primary's body and each partial's, which makeClass may choose
    for (std::size_t q = 0; !itself && !ownBase && q < d->second.partials.size(); q++) bodies.push_back(d->second.partials[q].bodyAt);
    for (const std::size_t from : bodies)
        for (std::size_t t = from, depth = 0; t < p_.tokens_.size(); t++) {
            if (p_.tokens_[t].text == own) return false;
            if (p_.tokens_[t].is("{")) depth++;
            if ((p_.tokens_[t].is("}") && --depth == 0) || (depth == 0 && p_.tokens_[t].is(";"))) break;
        }
    if (args->size() != d->second.params.size()) return false;
    for (std::size_t k = 0; k < args->size(); k++) {
        TemplateArg &a = (*args)[k];
        const TemplateParam &q = d->second.params[k];
        if (q.type != nullptr && !q.isPack && (argValued(s, &a) || (written != *name && writtenValue(a)))) continue;
        bool carried = !itself && q.isPack && a.isPack && !a.isTemplate && argUnder(s, &a, false);     // `X<Ts...>`: its members, as the replay lists them
        for (std::size_t m = 0; carried && m < a.pack.size(); m++) {
            const Type *u = a.pack[m]->unqualified();
            carried = !u->isFunction() && readIds_.count(u) == 0 && !(u->isStructOrUnion() && overParameters(u->tag()));
        }
        if (carried) continue;
        if (q.isTemplate && !q.isPack && a.isTemplate && a.type->isTemplateName()) continue;     // 3t: a class template, bound or written
        if (q.isPack || q.isTemplate || q.type != nullptr || !a.isType || a.isPack || a.isTemplate || a.expr != nullptr || a.isParam) return false;
        a.type = substitute(s, a.type).type;
        const Type *u = a.type;
        while (u->unqualified() != u || u->isPointer() || u->isReference() || u->isArray())
            u = u->unqualified() != u ? u->unqualified() : u->isReference() ? u->referent() : u->pointee();
        if (pending != nullptr && std::find(pending->begin(), pending->end(), u) != pending->end()) continue;     // made before it
        const Type *of = u->kind() == Kind::DependentMember ? u->pointee()->unqualified() : nullptr;     // `table::iterator`, table made first
        if (of != nullptr && pending != nullptr && std::find(pending->begin(), pending->end(), of) != pending->end()) continue;
        if (p_.dependentType(a.type) || u->isFunction() || readIds_.count(u) != 0 || (u->isStructOrUnion() && overParameters(u->tag()))) return false;
    }
    return true;
}

// An id whose arguments are ids over the parameters (`__tree<pair<const $T0,$T1>, __map_key<$T0,$T1,$T2> >`): each argument's
// carried class mapped in `s`, or the argument wanted at `*at`, innermost first, as the replay makes them reading the id.
void Templates::argsWanted(Substituted *s, const Type *e, const Type *read, const Substituted *early, std::vector<const Type *> *wanted, std::size_t *at) const {
    std::string name;
    std::vector<TemplateArg> args;
    if (!readId(e, &name, &args)) return;
    for (const TemplateArg &a : args) {
        if (!a.isType || a.type == nullptr || a.isPack || a.isTemplate || a.expr != nullptr || a.isParam) continue;
        const Type *u = a.type;
        while (u->unqualified() != u || u->isPointer() || u->isReference() || u->isArray())
            u = u->unqualified() != u ? u->unqualified() : u->isReference() ? u->referent() : u->pointee();
        if (u->kind() == Kind::DependentMember) u = u->pointee()->unqualified();     // slice 3o: `remove_cv<T>::type`, its class first
        if (u == read || !u->isStructOrUnion() || substitute(*s, u).type != u || !readId(u, nullptr, nullptr)
            || (readIds_.count(u) == 0 && !overParameters(u->tag()))) continue;
        argsWanted(s, u, read, early, wanted, at);
        const Type *real = carriedId(*s, u, early);
        std::string n;
        std::vector<TemplateArg> g;
        if (real != nullptr) s->from.push_back(u), s->to.push_back(real);
        else if (wanted != nullptr && earlyArgs(*s, u, read, &n, &g, false, wanted)) {     // ahead of the id, moved from behind it
            const std::vector<const Type *>::iterator from = wanted->begin() + static_cast<std::ptrdiff_t>(*at);
            if (std::find(wanted->begin(), from, u) != from) continue;
            if (std::find(from, wanted->end(), u) != wanted->end()) wanted->erase(std::find(from, wanted->end(), u));
            wanted->insert(wanted->begin() + static_cast<std::ptrdiff_t>((*at)++), u);
        }
    }
}

// Work (2): the ids a class the door takes names over its parameters and the program had not declared, made before it through
// makeClass, as its replay makes them when it reads the body - in the body's order, each declared once; nothing for another class.
// Its answer is what stopped it: the census's reason where the second classBefore, wanting no ids, refuses the class.
std::string Templates::idsBefore(std::size_t start, const std::vector<TemplateParam> &params, const std::vector<const Type *> &binding,
                              const std::vector<long long> &values, std::size_t pos, const Packs &packs, const Type *ahead) {
    Substituted s;
    s.values = values, s.packs = packs;
    if (ahead != nullptr) readStands(start, ahead, &s);     // the class itself, as an id's argument, the tag declared ahead
    std::vector<const Type *> wanted;
    const std::string first = classBefore(start, params, binding, &s, &wanted);
    const std::string why = first.compare(0, 18, "made from the read") != 0 ? first : fromReadWhyNot(start, params);
    if (!why.empty() || wanted.empty()) return why;
    for (const Type *e : wanted) {
        std::string name;
        std::vector<TemplateArg> args;
        if (!earlyArgs(s, e, readClass_.at(start), &name, &args) && !earlyArgs(s, e, readClass_.at(start), &name, &args, false, nullptr, true)) continue;
        bool made = false;
        s.from.push_back(e), s.to.push_back(makeId(name, args, pos, &made));     // an id's argument, as the next one reads it
        if (made) classCensus_["made before the class, as its replay makes it: a class template-id over its parameters"]++;
    }
    return std::string();
}

// An id among the read's typedefs and signatures with the read class itself as an argument: idsBefore makes it once the tag is.
bool Templates::namesItself(std::size_t start) const {
    const std::map<std::size_t, std::vector<std::pair<std::string, const Type *> > >::const_iterator tds = readTypedefs_.find(start);
    const std::map<std::size_t, std::pair<std::string, std::vector<Signature> > >::const_iterator k = readMembers_.find(start);
    if (readClass_.count(start) == 0 || tds == readTypedefs_.end() || k == readMembers_.end()) return false;
    Substituted own;
    readStands(start, nullptr, &own);
    std::vector<const Type *> types;
    for (const std::pair<std::string, const Type *> &td : tds->second) types.push_back(td.second);
    for (const Signature &f : k->second.second) types.push_back(f.returns), types.insert(types.end(), f.params.begin(), f.params.end());
    for (const Type *t : types) {
        std::vector<TemplateArg> args;
        while (t != nullptr && (t->unqualified() != t || t->isPointer() || t->isReference() || t->isArray()))
            t = t->unqualified() != t ? t->unqualified() : t->isReference() ? t->referent() : t->pointee();
        if (t == nullptr || t == readClass_.at(start) || !readId(t, nullptr, &args)) continue;
        for (const TemplateArg &a : args) {
            const Type *u = a.isType && !a.isPack ? a.type : nullptr;
            while (u != nullptr && (u->unqualified() != u || u->isPointer() || u->isReference()))
                u = u->unqualified() != u ? u->unqualified() : u->isReference() ? u->referent() : u->pointee();
            if (u != nullptr && u->isStructOrUnion() && std::find(own.from.begin(), own.from.end(), u) != own.from.end()) return true;
        }
    }
    return false;
}

// `const T` (or volatile) anywhere in `t` with T bound to a reference: the read collapses it, the replay keeps the qualifier.
static bool cvOverReference(const Templates::Substituted &s, const Type *t) {
    if (t == nullptr) return false;
    if (t->unqualified() != t) {
        for (std::size_t k = 0; k < s.from.size(); k++) if (t->unqualified() == s.from[k] && s.to[k]->isReference()) return true;
        return cvOverReference(s, t->unqualified());
    }
    if (t->isPointer() || t->isArray()) return cvOverReference(s, t->pointee());
    if (t->isReference()) return cvOverReference(s, t->referent());
    if (!t->isFunction()) return false;
    for (std::size_t k = 0; k < t->params().size(); k++) if (cvOverReference(s, t->params()[k])) return true;
    return cvOverReference(s, t->returns());
}

// Slice 3a's shape: type parameters bound to scalars, to a class (boundClass's) or to a reference to either, and a read
// class of data members alone, each of a scalar type or a class the read did not name over its parameters.
std::string Templates::classShape(std::size_t start, const std::vector<TemplateParam> &params, const std::vector<const Type *> &binding, Substituted *s,
                                  const Substituted *early, std::vector<const Type *> *wanted) const {
    // Slice 3f: a pack or template parameter its class names in member bodies alone (replayed by name, as instantiate's door
    // asks) is in its tag alone; a partial's scan from past its written arguments, which name it (`is_function<R(A...)>`).
    std::size_t from = start;
    if (p_.tokens_[start].is("{")) {
        while (from > 0 && !p_.tokens_[from].is("struct") && !p_.tokens_[from].is("class") && !p_.tokens_[from].is("union")) from--;
        for (int angle = 0, k = 0; from < start && k < 2; from++) {     // `struct name` to the `<`, then to its `>`
            angle += p_.tokens_[from].is("<") ? 1 : p_.tokens_[from].is(">") ? -1 : p_.tokens_[from].is(">>") ? -2 : 0;
            k += p_.tokens_[from].is("<") && angle == 1 ? 1 : angle <= 0 && k == 1 ? 1 : 0;
        }
    }
    std::vector<char> open;     // the brackets around a token: a pack expanded in a parameter list, `R(Args...)`, is the kernel's list form's
    std::vector<std::size_t> sized;
    for (std::size_t t = from, depth = 0; t < p_.tokens_.size(); t++) {
        const Token &k = p_.tokens_[t], &was = p_.tokens_[t > 0 ? t - 1 : 0];
        if (depth == 1 && k.is(":") && (was.is(")") || was.is("noexcept")))     // a mem-initializer list, replayed with its body
            for (int parens = 0; t + 1 < p_.tokens_.size() && !(parens == 0 && p_.tokens_[t + 1].is("{") &&
                 (p_.tokens_[t].is(")") || p_.tokens_[t].is("}"))); t++)
                parens += p_.tokens_[t + 1].is("(") || p_.tokens_[t + 1].is("{") ? 1 : p_.tokens_[t + 1].is(")") || p_.tokens_[t + 1].is("}") ? -1 : 0;
        const Token &at = p_.tokens_[t], &before = p_.tokens_[t > 0 ? t - 1 : 0];
        if (depth == 1 && at.is("{") && (before.is(")") || before.is("}") || before.is("const") || before.is("volatile") || before.is("override") ||
                                         before.is("noexcept")))
            for (std::size_t inner = 0; t < p_.tokens_.size(); t++) {     // a member function's body, to its `}`
                inner += p_.tokens_[t].is("{");
                if (p_.tokens_[t].is("}") && --inner == 0) break;
            }
        else if (at.is("{")) depth++;
        else if ((at.is("}") && --depth == 0) || (depth == 0 && at.is(";"))) break;
        if (t >= p_.tokens_.size()) break;
        const Token &n = p_.tokens_[t];
        if (t > 0 && p_.tokens_[t - 1].is("operator") && !n.is("(")) continue;     // `operator<`, `operator>>`: no bracket
        if (n.is("(") || n.is("<")) open.push_back(n.text[0]);
        for (int pops = n.is(")") || n.is(">") ? 1 : n.is(">>") ? 2 : 0; pops > 0 && !open.empty() && (n.is(")") || open.back() == '<'); pops--)
            open.pop_back();     // 3q: a `>` inside parentheses compares, `Max<(N > M ? N : M), Rest...>`
        std::size_t e = t + 1;
        while (e < p_.tokens_.size() && (p_.tokens_[e].is("&") || p_.tokens_[e].is("&&") || p_.tokens_[e].is("*"))) e++;
        // ... or in a template-id's arguments, `X<Ts...>`, whose read id the kernel's id step and earlyArgs expand (argUnder).
        const bool expanded = !open.empty() && e < p_.tokens_.size() && p_.tokens_[e].is("...") && (open.back() == '(' || (open.back() == '<' && e == t + 1));
        // Slice 3q: or counted, `sizeof...(Ts)`, where the second scan finds a static's initialiser staticsFromRead reads bound.
        const bool counted = t > start + 2 && t + 1 < p_.tokens_.size() && p_.tokens_[t - 1].is("(") && p_.tokens_[t - 2].is("...") &&
                             p_.tokens_[t - 3].is("sizeof") && p_.tokens_[t + 1].is(")");
        // Slice 3s: a template template parameter's id, `C<T>`, which the read lays over `$TT<i>` and idUnder names as bound.
        bool ttId = t + 1 < p_.tokens_.size() && p_.tokens_[t + 1].is("<") && !(t > 0 && p_.tokens_[t - 1].is("::"));
        // Slice 3t: or an id's whole argument, `Holder<C, int>`, which idUnder names as bound too (templateArgUnder).
        ttId = ttId || (!open.empty() && open.back() == '<' && t + 1 < p_.tokens_.size() && (p_.tokens_[t - 1].is("<") || p_.tokens_[t - 1].is(",")) &&
                        (p_.tokens_[t + 1].is(",") || p_.tokens_[t + 1].is(">") || p_.tokens_[t + 1].is(">>")));
        for (std::size_t q = 0; q < params.size(); q++)
            if ((params[q].isPack || params[q].isTemplate) && !params[q].name.empty() && n.text == params[q].name && !(params[q].isPack && expanded) &&
                !(params[q].isTemplate && !params[q].isPack && ttId)) {
                if (!(params[q].isPack && counted)) return "replayed: a pack or template parameter, bound by name";
                sized.push_back(t);
            }
    }
    // Slice 3e: a non-type parameter named in member bodies, replayed under the real values, or as a bound `[N]`, which the read
    // keeps as the parameter (Type::boundParam) and substituted lays under `s`'s values; anywhere else it replays by name.
    // Slice 3f: or in the primary's own id over its parameters in order (`V<N>`, `Box<T, N>`), the read class (readStands).
    const std::map<std::size_t, std::pair<std::string, std::vector<Signature> > >::const_iterator self = readMembers_.find(start);
    const std::string readTag = self != readMembers_.end() ? self->second.first : std::string();
    std::string ownKey = readTag.substr(0, readTag.find('<')) + "<";
    for (std::size_t q = 0; q < params.size(); q++) ownKey += (q > 0 ? "," : "") + std::string(params[q].type != nullptr ? "$P" : "$T") + std::to_string(q);
    const bool primary = readTag.find('<') != std::string::npos && readTag.compare(0, ownKey.size() + 6, ownKey + ">$read") == 0;
    const std::size_t colons = ownKey.rfind("::", ownKey.find('<')), nameAt = colons == std::string::npos ? 0 : colons + 2;
    const std::string ownName = ownKey.substr(nameAt, ownKey.find('<') - nameAt);
    const auto ownId = [&](std::size_t t) -> std::size_t {     // past the `>` of the own id at `t`, or `t`
        if (!primary || p_.tokens_[t].text != ownName || t + 1 + 2 * params.size() >= p_.tokens_.size() || !p_.tokens_[t + 1].is("<")) return t;
        for (std::size_t q = 0; q < params.size(); q++)
            if (p_.tokens_[t + 2 + 2 * q].text != params[q].name || !p_.tokens_[t + 3 + 2 * q].is(q + 1 < params.size() ? "," : ">")) return t;
        return t + 1 + 2 * params.size();
    };
    const auto idArg = [&](std::size_t t) {     // `N` a whole argument of a class template-id (`V<N>`, `Mx<M, N>`), made by its value
        bool packed = false;     // slice 3ab: `<Ns...>`, a non-type pack the whole list (`<Ns..., 0>` the check finds differ)
        for (std::size_t q = 0; q < params.size() && t > start && t + 2 < p_.tokens_.size(); q++)
            packed = packed || (params[q].isPack && params[q].type != nullptr && p_.tokens_[t].text == params[q].name && p_.tokens_[t - 1].is("<") &&
                                p_.tokens_[t + 1].is("...") && p_.tokens_[t + 2].is(">"));
        const std::size_t whole = t + (packed ? 2 : 1);
        std::size_t lo = t, hi = whole, at = 0;     // 3h: or in one foldBound folds (`V<N + 1>`), which argValued folds as well
        long long v = 0;
        const auto stops = [&](std::size_t k) { return p_.tokens_[k].is(";") || p_.tokens_[k].is("{") || p_.tokens_[k].is("}"); };
        for (int parens = 0; lo > start + 1 && !stops(lo - 1) && (parens > 0 || !(p_.tokens_[lo - 1].is("<") || p_.tokens_[lo - 1].is(","))); lo--)
            parens += (p_.tokens_[lo - 1].is(")") ? 1 : 0) - (p_.tokens_[lo - 1].is("(") ? 1 : 0);
        for (int parens = 0; hi < p_.tokens_.size() && !stops(hi) && (parens > 0 || !(p_.tokens_[hi].is(">") || p_.tokens_[hi].is(","))); hi++)
            parens += (p_.tokens_[hi].is("(") ? 1 : 0) - (p_.tokens_[hi].is(")") ? 1 : 0);
        if (hi >= p_.tokens_.size() || lo <= start + 1 || !(p_.tokens_[lo - 1].is("<") || p_.tokens_[lo - 1].is(",")) || !(p_.tokens_[hi].is(">") || p_.tokens_[hi].is(",")) ||
            (hi != whole || lo != t ? !foldBound(p_.tokens_, &(at = lo), hi, params, s->values, 0, &v) || at != hi || v < 0 : false))
            return false;
        for (std::size_t b = lo - 1, angles = 0; b > start; b--) {
            const Token &x = p_.tokens_[b];
            if (x.is(";") || x.is("{") || x.is("}") || x.is("(") || x.is(")") || x.is("=")) return false;
            if (x.is(">")) angles++;
            else if (x.is("<") && angles > 0) angles--;
            else if (x.is("<")) {
                const std::string &n = p_.tokens_[b - 1].text;
                return !n.empty() && (std::isalpha(static_cast<unsigned char>(n[0])) || n[0] == '_') && n != "template" && n != "operator";
            }
        }
        return false;
    };
    // Slice 3g: or in a data member's first bound (`T a[N + 1];`, in the class's own braces), folded into `s->bounds`.
    const auto boundAt = [&](std::size_t t, std::size_t depth) {
        std::size_t open = t, close = t, at = 0;
        for (; open > start && !p_.tokens_[open].is("["); open--)
            if (p_.tokens_[open].is(";") || p_.tokens_[open].is("{") || p_.tokens_[open].is("}") || p_.tokens_[open].is("]")) return false;
        while (close + 1 < p_.tokens_.size() && !p_.tokens_[close].is("]") && !p_.tokens_[close].is(";")) close++;
        long long v = 0;
        if (depth != 1 || open <= start + 1 || !p_.tokens_[open].is("[") || !p_.tokens_[close].is("]") || p_.tokens_[open - 1].kind != TokenKind::Ident ||
            !(p_.tokens_[close + 1].is(";") || p_.tokens_[close + 1].is(",")) || !foldBound(p_.tokens_, &(at = open + 1), close, params, s->values, 0, &v) ||
            at != close || v <= 0)
            return false;
        for (std::size_t b = open - 1, parens = 0; b > start; b--) {     // a data member's declarator: inside no `(`, no typedef
            const Token &x = p_.tokens_[b];
            if (x.is(";") || x.is("{") || x.is("}")) break;
            if (x.is("typedef") || x.is("static") || (x.is("(") && parens == 0)) return false;
            parens += x.is(")"), parens -= x.is("(");
        }
        s->bounds[p_.tokens_[open - 1].text] = v;
        return true;
    };
    // Slice 3i: or in a static data member's initialiser in the class's own braces (`static const T value = v;`), which
    // staticsFromRead evaluates under the bound values and staticsHeld holds against the replay's.
    const auto staticInit = [&](std::size_t t, std::size_t depth) {
        bool equals = false;
        for (int b = static_cast<int>(t), parens = 0, level = 0, colons = 0; depth == 1 && b > static_cast<int>(start); b--) {
            const Token &x = p_.tokens_[static_cast<std::size_t>(b - 1)];
            if (x.is(";") || x.is("{") || x.is("}") || x.is("typedef") || x.is("using") || x.is("enum")) return false;
            if (x.is(":") && (b < 2 || p_.tokens_[static_cast<std::size_t>(b - 2)].is("public") || p_.tokens_[static_cast<std::size_t>(b - 2)].is("private") ||
                               p_.tokens_[static_cast<std::size_t>(b - 2)].is("protected")))
                return false;
            colons += x.is(":") ? 1 : x.is("?") && colons > 0 ? -1 : 0;     // a `:` no `?` before it matches is no conditional's
            parens += x.is(")") ? 1 : x.is("(") ? -1 : 0;
            if (equals && parens < level) return false;     // a parameter's default argument: `f(int a = N)`
            if (!equals && x.is("=")) equals = true, level = parens;
            if (x.is("static")) return equals && parens == level && colons == 0;
        }
        return false;
    };
    // Slice 3k: or in a `static_assert` in the class's own braces whose condition folds true under the values, as the replay finds.
    const auto assertAt = [&](std::size_t t, std::size_t depth) {
        std::size_t open = t;
        while (depth == 1 && open > start && !p_.tokens_[open].is("static_assert"))
            if (p_.tokens_[--open].is(";") || p_.tokens_[open].is("{") || p_.tokens_[open].is("}")) return false;
        if (!p_.tokens_[open].is("static_assert") || !p_.tokens_[open + 1].is("(")) return false;
        std::size_t comma = open + 2, at = open + 2;
        for (int parens = 1; comma < p_.tokens_.size() && !(parens == 1 && p_.tokens_[comma].is(",")); comma++)
            if (p_.tokens_[comma].is(";") || (parens += p_.tokens_[comma].is("(") ? 1 : p_.tokens_[comma].is(")") ? -1 : 0) == 0) return false;
        long long a = 0, b = 0;
        if (comma >= p_.tokens_.size() || t >= comma || !foldBound(p_.tokens_, &at, comma, params, s->values, 0, &a)) return false;
        if (at == comma) return a != 0;
        const std::string op = p_.tokens_[at++].text;
        if (!foldBound(p_.tokens_, &at, comma, params, s->values, 0, &b) || at != comma) return false;
        return op == "==" ? a == b : op == "!=" ? a != b : op == "<" ? a < b : op == ">" ? a > b : op == "<=" ? a <= b : op == ">=" && a >= b;
    };
    // Slice 3l: or in a member template's declaration, which templatesFromRead reads at its `template` under the values.
    std::vector<std::size_t> heads;
    for (const TemplateDecl *mt : templatesOf(readTag)) heads.push_back(mt->start);
    bool otherEnum = false;     // slice 3m: an unnamed member enumeration naming no parameter, which staticsFromRead reads
    const std::vector<std::size_t> enums = memberEnums(start, &otherEnum);
    const auto inEnum = [&](std::size_t t) {
        for (std::size_t e : enums) {
            std::size_t close = e;
            while (!p_.tokens_[close].is("}")) close++;
            if (t > e && t < close) return true;
        }
        return false;
    };
    // Slice 3n: in a class of no member function an enumerator over a parameter is read under the binding, and nothing reads the read's;
    // 3z: beside one, over a non-type parameter, which marks a body's read of it value-dependent (memberShape replays it); 3aa: a type one too.
    const std::map<std::size_t, std::pair<std::string, std::vector<Signature> > >::const_iterator own = readMembers_.find(start);
    const bool bare = own != readMembers_.end() && own->second.second.empty() && templatesOf(own->second.first).empty();
    const auto ctorAt = [&](std::size_t close) {     // the `)` closing a constructor's parameters: `P(...)` or `P<M, N>(...)`
        std::size_t o = close;
        for (int parens = 0; o > start; o--)
            if ((parens += p_.tokens_[o].is(")") ? 1 : p_.tokens_[o].is("(") ? -1 : 0) == 0) break;
        return o > start + 1 && (p_.tokens_[o - 1].text == ownName || (o > 2 * params.size() + 2 && ownId(o - 2 - 2 * params.size()) == o - 1));
    };
    for (std::size_t t = start, depth = 0, head = 0; t < p_.tokens_.size(); t++) {
        const Token &k = p_.tokens_[t], &was = p_.tokens_[t > 0 ? t - 1 : 0];
        if (depth == 1 && std::find(heads.begin(), heads.end(), t) != heads.end()) head = 1;
        bool body = depth == 1 && k.is("{") && (was.is(")") || was.is("const") || was.is("volatile") || was.is("override") || was.is("noexcept"));
        if (depth == 1 && k.is(":") && was.is(")") && ctorAt(t - 1))     // slice 3y: a constructor's mem-initialisers, its body's head
            for (int parens = 0, braces = 0; t + 1 < p_.tokens_.size() && !body; t++) {
                const Token &w = p_.tokens_[t + 1];
                body = parens == 0 && braces == 0 && w.is("{") && (p_.tokens_[t].is(")") || p_.tokens_[t].is("}"));
                parens += w.is("(") ? 1 : w.is(")") ? -1 : 0, braces += w.is("{") ? 1 : w.is("}") ? -1 : 0;
            }
        if (body)
            for (std::size_t inner = head = 0; t < p_.tokens_.size(); t++) {     // a member function's body, to its `}`
                inner += p_.tokens_[t].is("{");
                if (p_.tokens_[t].is("}") && --inner == 0) break;
            }
        else if (k.is("{")) depth++;
        else if (k.is("}") && --depth == 0) break;
        if (t < p_.tokens_.size() && depth == 1 && (p_.tokens_[t].is(";") || p_.tokens_[t].is("}"))) head = 0;
        if (head != 0) continue;
        if (t < p_.tokens_.size() && ownId(t) != t) { t = ownId(t); continue; }
        if (depth == 1 && t < p_.tokens_.size() && p_.tokens_[t].is("static_assert") && !assertAt(t, depth)) return "replayed: a static_assert";
        if (std::find(sized.begin(), sized.end(), t) != sized.end() && !staticInit(t, depth) && !(bare && inEnum(t)))
            return "replayed: a pack or template parameter, bound by name";
        sized.erase(std::remove(sized.begin(), sized.end(), t), sized.end());
        if (bare && t < p_.tokens_.size() && inEnum(t)) continue;
        for (std::size_t q = 0; q < params.size() && t < p_.tokens_.size() && inEnum(t); q++)
            if (!params[q].name.empty() && p_.tokens_[t].text == params[q].name && params[q].isPack)
                return "replayed: a member enumeration naming a pack parameter, beside a member function";
        if (t < p_.tokens_.size() && inEnum(t)) continue;
        for (std::size_t q = 0; q < params.size() && t < p_.tokens_.size(); q++)
            if (params[q].type != nullptr && !params[q].name.empty() && p_.tokens_[t].text == params[q].name &&
                !(was.is("[") && t + 1 < p_.tokens_.size() && p_.tokens_[t + 1].is("]") && q < s->values.size() && s->values[q] > 0) && !idArg(t) &&
                !boundAt(t, depth) && !staticInit(t, depth) && !assertAt(t, depth))
                return "replayed: a non-type parameter named outside its member bodies";
    }
    if (!sized.empty()) return "replayed: a pack or template parameter, bound by name";     // one the second scan did not reach
    const std::map<std::size_t, const Type *>::const_iterator r = readClass_.find(start);
    if (r == readClass_.end()) return "replayed: no class read at the declaration";
    const Type *read = r->second;
    if (!read->isStructOrUnion() || !read->isComplete()) return "replayed: the read made no complete class";
    for (const Type::BaseSpec &b : read->bases()) {     // one the read laid as the program's own class, by kind for the census
        const bool over = overParameters(b.type->tag()) || readIds_.count(b.type) != 0;     // slice 3o: one over them, mapped below
        const bool first = &b == &read->bases()[0] && b.written == 0 && b.type->polymorphic() && !b.type->hasVirtualBase();     // 3v: its table seeded
        if (b.isVirtual || !b.direct || (!over && !first && (b.type->polymorphic() || b.type->hasVirtualBase())))
            return std::string("replayed: a base: ") + (over ? "a class template-id over its parameters" : b.type->polymorphic() ?
                   "a polymorphic class" : "a virtual base");
    }
    const std::map<const Type *, std::vector<Parser::BaseToLay> >::const_iterator keptBases = p_.readBases_.find(read);
    if (read->dependentBase() && (keptBases == p_.readBases_.end() || !read->bases().empty()))
        return "replayed: a base named through a parameter, which the read laid out as nothing";
    for (std::size_t q = 0; read->dependentBase() && q < keptBases->second.size(); q++)
        if (keptBases->second[q].type == nullptr || !readId(keptBases->second[q].type, nullptr, nullptr))
            return "replayed: a base named through a parameter, which the read laid out as nothing";
    for (std::size_t t = start, depth = 0; read->dependentBase() && t < p_.tokens_.size(); t++) {     // the read skipped each
        if (depth == 1 && p_.tokens_[t].is("using")) return "replayed: a using-declaration beside a base over its parameters";
        depth += p_.tokens_[t].is("{");
        if (p_.tokens_[t].is("}") && --depth == 0) break;
    }
    if (read->hasVptr()) {     // slice 3v: a polymorphic first base's table, each read virtual an override in a slot of it
        const std::map<std::string, std::vector<VSlot> > &tables = p_.classes_.vtables_;
        const std::map<std::string, std::vector<VSlot> >::const_iterator mine = tables.find(readTag),
            theirs = read->bases().empty() || !read->bases()[0].type->polymorphic() ? tables.end() : tables.find(read->bases()[0].type->tag());
        const bool table = read->bases().empty();     // slice 3w: a table of its own, which membersFromRead seeds from the read's
        if (read->hasVirtualBase() || self == readMembers_.end() || mine == tables.end() ||
            (!table && (theirs == tables.end() || mine->second.size() != theirs->second.size())))
            return "replayed: a virtual function";
        for (std::size_t i = 0; !table && i < mine->second.size(); i++)
            if (mine->second[i].pure && !theirs->second[i].pure) return "replayed: a virtual function";
        for (const Signature &f : self->second.second) {     // one the binding may make an override, which the read did not
            if (table) break;
            if (!f.implicit && f.name[0] == '~') return "replayed: a written destructor beside a polymorphic base";
            for (const VSlot &v : theirs->second)
                if (!f.isVirtual && !f.implicit && v.name == f.name) return "replayed: a member function a base's virtual of its name may take";
        }
    }
    for (std::size_t q = 0; q < binding.size() && q < params.size(); q++) {
        std::map<std::string, TemplateDecl>::const_iterator tt = params[q].isTemplate && !params[q].isPack && binding[q] != nullptr &&
            binding[q]->isTemplateName() ? p_.findTemplate(binding[q]->templateName()) : templates_.end();     // 3s: a class template, keyed by its name
        if (tt != templates_.end() && tt->second.isAlias) tt = aliasedClass(tt->second);     // 3t: or an alias naming one, as the replay reads it
        if (tt != templates_.end() && tt->second.isClass && !tt->second.isAlias && tt->second.ttParam < 0 && tt->first == tt->second.name)
            s->templates[static_cast<int>(q)] = tt->first;
        if (params[q].type != nullptr || params[q].isPack || params[q].isTemplate) continue;     // a value, or named nowhere (3f)
        const Type *u = binding[q] != nullptr ? binding[q]->unqualified() : nullptr;
        if (u == nullptr) return "replayed: a type parameter bound to no type";
        const Type *to = u->isReference() ? u->referent()->unqualified() : u;     // `T` bound to `U &`: substituted collapses
        const std::string cls = to->isStructOrUnion() ? boundClass(*to) : std::string(), ref = u->isReference() ? "a reference to " : "";
        const bool pending = to->kind() == Kind::Struct && !to->isComplete();     // completed where a member needs it, as the replay does
        if (to->isStructOrUnion() && ((!cls.empty() && !pending) || overParameters(to->tag())))
            return "replayed: a type parameter bound to " + ref + "a class: " + (cls.empty() ? std::string("one the read named over its parameters") : cls);
        const Type *element = to;     // void, `R(A)` and (3u) an array of scalars, `const char [7]`, substitute as written
        while (element->isArray()) element = element->pointee()->unqualified();
        if (!to->isScalar() && !to->isStructOrUnion() && !to->isVoid() && !to->isFunction() && !(to->isArray() && element->isScalar()))
            return std::string("replayed: a type parameter bound to ") + ref + (to->isReference() ? "a reference" :
                   to->isArray() ? "an array" : "a type that is no scalar, class, reference or array");
        s->from.push_back(p_.types_.templateParam(static_cast<int>(q)));
        s->to.push_back(binding[q]);
    }
    const std::string statics = staticShape(start, *s);
    if (!statics.empty()) return statics;
    std::size_t baseIds = 0;     // slice 3j: `Q : V<I>`, its class made first, as the replay makes it reading the head
    std::vector<const Type *> overBases;     // the kept ids, or (slice 3o) the bases the read laid as ids over its parameters
    for (std::size_t q = 0; read->dependentBase() && q < keptBases->second.size(); q++) overBases.push_back(keptBases->second[q].type);
    for (const Type::BaseSpec &b : read->bases())
        if (overParameters(b.type->tag()) || readIds_.count(b.type) != 0) overBases.push_back(b.type);
    for (const Type *u : overBases) {
        argsWanted(s, u, read, early, wanted, &baseIds);
        const Type *real = carriedId(*s, u, early);
        std::string name;
        std::vector<TemplateArg> args;
        if (real != nullptr && (real->kind() != Kind::Struct || !real->isComplete() || real->polymorphic() || real->hasVirtualBase() || real->isFinal()))
            return "replayed: a base over its parameters: a polymorphic, final, incomplete or union class, or one of a virtual base";
        if (real != nullptr) s->from.push_back(u), s->to.push_back(real);
        else if (wanted != nullptr && earlyArgs(*s, u, read, &name, &args, false, wanted, true))
            wanted->insert(wanted->begin() + static_cast<std::ptrdiff_t>(baseIds++), u);
        else return "replayed: a base: a class template-id over its parameters";
    }
    // A data member's place in the body: its name in the class's own braces before `;`, `,`, `[` or `=`; 0 where none is.
    const auto memberAt = [&](const std::string &n) -> std::size_t {
        for (std::size_t t = start, braces = 0, parens = 0; t + 1 < p_.tokens_.size(); t++) {
            const Token &k = p_.tokens_[t], &next = p_.tokens_[t + 1];
            braces += k.is("{"), parens += k.is("(");
            if ((k.is("}") && --braces == 0) || (braces == 0 && k.is(";"))) break;
            if (k.is(")") && parens > 0) parens--;
            if (braces == 1 && parens == 0 && k.text == n && (next.is(";") || next.is(",") || next.is("[") || next.is("="))) return k.pos;
        }
        return 0;
    };
    // A pointer (at any depth, through arrays) to the read class itself, or to the class readStands takes for it.
    const auto pointsAtOwn = [&](const Type *t) {
        bool pointer = false;
        for (t = t->unqualified(); t->isPointer() || t->isArray(); t = t->pointee()->unqualified()) pointer = pointer || t->isPointer();
        Substituted own;
        readStands(start, nullptr, &own);
        return pointer && std::find(own.from.begin(), own.from.end(), t) != own.from.end();
    };
    const auto ownEnum = [&](const Type *u) {     // slice 3r: a named member enumeration of its own, readStands maps it
        Substituted mapped;
        readStands(start, nullptr, &mapped);
        return std::find(mapped.from.begin(), mapped.from.end(), u) != mapped.from.end();
    };
    std::vector<std::size_t> dataAt;     // the data members that want ids, where the body declares them
    for (const Member &m : read->members()) {
        if (m.isBitField() || m.anonUnion != nullptr) return "replayed: a bit-field or an anonymous union";
        const Type *t = dataUnder(*s, m), *e = t;
        if (pointsAtOwn(m.type)) continue;     // `__hash_node *next`: a pointer, the made class once readStands maps it
        if (ownEnum(m.type->unqualified())) continue;     // slice 3r: `Kind kind`, likewise
        const Type *owner = m.type->unqualified()->kind() == Kind::DependentMember ? m.type->unqualified()->pointee()->unqualified() : nullptr;
        std::string on;     // 3s: `typename C<T>::type t`, its class made first, as the replay makes it reading the member
        std::vector<TemplateArg> oa;
        if (owner != nullptr && wanted != nullptr && p_.dependentType(t) && owner != read && readId(owner, nullptr, nullptr) &&
            (std::find(wanted->begin(), wanted->end(), owner) != wanted->end() || earlyArgs(*s, owner, read, &on, &oa, false, wanted))) {
            if (std::find(wanted->begin(), wanted->end(), owner) == wanted->end()) dataAt.push_back(memberAt(m.name)), wanted->push_back(owner);
            continue;
        }
        int tt = -1;     // 3s: a stand-in id another read laid first stays a specialization of `$TT<i>`, dependent until mapped below
        const Type *bare = t;
        while (bare->isArray() || bare->unqualified() != bare) bare = bare->isArray() ? bare->pointee() : bare->unqualified();
        const bool standIn = bare->isSpecialization() && ttRefName(bare->templateName(), &tt) && s->templates.count(tt) != 0;
        if (p_.dependentType(t) && !standIn) return "replayed: a data member of a type the binding does not carry";
        while (e->isArray()) e = e->pointee();
        if (!m.type->isReference() && e->isReference()) return "replayed: a data member the binding makes a reference";
        const Type *u = e->unqualified();
        const bool over = u->isStructOrUnion() && (readIds_.count(u) != 0 || overParameters(u->tag()));
        std::size_t back = wanted != nullptr ? wanted->size() : 0;
        const std::size_t had = back;
        if (over && wanted != nullptr && std::find(wanted->begin(), wanted->end(), u) != wanted->end()) continue;
        if (over && u != read) argsWanted(s, u, read, early, wanted, &back);
        if (wanted != nullptr && wanted->size() != had) dataAt.push_back(memberAt(m.name));     // where the replay makes them
        const Type *real = over && u != read && readId(u, nullptr, nullptr) ? carriedId(*s, u, early) : nullptr;
        std::string name;     // `X<$T0> m`: the class the program declared under the binding, or one idsBefore makes first
        std::vector<TemplateArg> args;
        if (real != nullptr) s->from.push_back(u), s->to.push_back(real), t = dataUnder(*s, m);
        else if (over) {
            if (wanted == nullptr || u == read || !readId(u, nullptr, nullptr) || !earlyArgs(*s, u, read, &name, &args, false, wanted))
                return "replayed: a data member of a class the read named over its parameters";
            if (wanted->size() == had) dataAt.push_back(memberAt(m.name));
            wanted->push_back(u);
            continue;
        }
        if (e->unqualified()->isEnumeration() && overParameters(e->unqualified()->tag()))
            return "replayed: a data member of a member enumeration the read named over its parameters";
        if (!t->isComplete()) return "replayed: a data member of an incomplete type";
    }
    for (const std::pair<const std::string, long long> &b : s->bounds) {     // each folded bound a data member's the read left open
        bool open = false;
        for (const Member &m : read->members()) open = open || (m.name == b.first && m.type->isArray() && m.type->length() == -1);
        if (!open) return "replayed: a non-type parameter named outside its member bodies";
    }
    const std::map<std::size_t, std::pair<std::string, std::vector<Signature> > >::const_iterator k = readMembers_.find(start);
    if (k == readMembers_.end()) return "replayed: no member functions kept from the read";
    const std::size_t dataIds = wanted != nullptr ? wanted->size() - baseIds : 0;     // the replay makes them in the body's order, not data first
    const std::map<std::size_t, std::vector<std::pair<std::string, const Type *> > >::const_iterator tds = readTypedefs_.find(start);
    std::size_t first = baseIds;     // a typedef's `X<$T0>` or `X<$T0>::m` wanted ahead of the members', as the replay reads the body
    for (std::size_t i = 0; tds != readTypedefs_.end() && i < tds->second.size(); i++) {
        if (cvOverReference(*s, tds->second[i].second)) return "replayed: a typedef's const over a parameter bound to a reference";
        const Type *e = substitute(*s, tds->second[i].second).type;
        while (e->isPointer() || e->isReference() || e->isArray()) e = e->isReference() ? e->referent() : e->pointee();
        e = e->unqualified();
        std::string name;
        std::vector<TemplateArg> args;
        const Type *of = e->kind() == Kind::DependentMember ? e->pointee()->unqualified() : nullptr;
        if (of != nullptr && wanted != nullptr && of != read && readId(of, nullptr, nullptr) && carriedId(*s, of, nullptr) == nullptr
            && earlyArgs(*s, of, read, &name, &args)) {
            wanted->insert(wanted->begin() + static_cast<std::ptrdiff_t>(first++), of);
            continue;
        }
        const bool id = e != read && (overParameters(e->tag()) || readIds_.count(e) != 0) && readId(e, nullptr, nullptr);
        if (id) argsWanted(s, e, read, early, wanted, &first);
        const Type *real = id ? carriedId(*s, e, early) : nullptr;
        if (real != nullptr) s->from.push_back(e), s->to.push_back(real);
        else if (wanted != nullptr && id && earlyArgs(*s, e, read, &name, &args, false, wanted))
            wanted->insert(wanted->begin() + static_cast<std::ptrdiff_t>(first++), e);
    }
    // A signature declared before every data member wanting ids has its ids made ahead of theirs, one after them behind.
    const bool placed = std::find(dataAt.begin(), dataAt.end(), std::size_t(0)) == dataAt.end();
    const std::size_t dataLo = dataAt.empty() ? 0 : *std::min_element(dataAt.begin(), dataAt.end());
    const std::size_t dataHi = dataAt.empty() ? 0 : *std::max_element(dataAt.begin(), dataAt.end());
    std::size_t sigAt = first;
    for (const Signature &f : k->second.second) {
        std::vector<const Type *> types(1, f.returns);     // in the order the replay reads a declaration: its return type first
        types.insert(types.end(), f.params.begin(), f.params.end());
        const bool ahead = dataIds != 0 && placed && f.pos < dataLo, behind = dataIds == 0 || (placed && f.pos > dataHi);
        for (const Type *t : types) {
            if (cvOverReference(*s, t)) return "replayed: a signature's const over a parameter bound to a reference";
            const Type *e = substitute(*s, t).type;
            while (e->isPointer() || e->isReference() || e->isArray()) e = e->isReference() ? e->referent() : e->pointee();
            e = e->unqualified();
            // `X<$T0>::m` whose X<U> the program had not declared: X<U> wanted by idsBefore, the member found once it is made.
            std::string name;
            std::vector<TemplateArg> args;
            const Type *of = e->kind() == Kind::DependentMember ? e->pointee()->unqualified() : nullptr;
            if (of != nullptr && wanted != nullptr && std::find(wanted->begin(), wanted->end(), of) != wanted->end()) continue;
            if (of != nullptr && wanted != nullptr && of != read && readId(of, nullptr, nullptr) && carriedId(*s, of, nullptr) == nullptr
                && earlyArgs(*s, of, read, &name, &args)) {
                if (!ahead && !behind) return "replayed: class template-ids made first for a data member and a member function's signature";
                wanted->insert(ahead ? wanted->begin() + static_cast<std::ptrdiff_t>(sigAt++) : wanted->end(), of);
                continue;
            }
            // A class the read named over its parameters (`initializer_list<$T0>`) stands for the one the program declared under
            // the binding before the replay; one it had not is no specialization once the read ends.
            const bool id = e != read && (overParameters(e->tag()) || readIds_.count(e) != 0) && readId(e, nullptr, nullptr);
            const std::size_t had = wanted != nullptr ? wanted->size() : 0;
            std::size_t back = ahead ? sigAt : had;     // its arguments' ids first, as for a data member's
            if (id) argsWanted(s, e, read, early, wanted, &back);
            if (ahead) sigAt = back;
            if (!ahead && !behind && wanted != nullptr && wanted->size() != had)
                return "replayed: class template-ids made first for a data member and a member function's signature";
            if (id && wanted != nullptr && std::find(wanted->begin(), wanted->end(), e) != wanted->end()) continue;     // 3z: a base made first
            const Type *real = id ? carriedId(*s, e, early) : nullptr;
            const bool own = real == nullptr && id && earlyArgs(*s, e, read, &name, &args, true);
            // Its own template under other arguments: made inside the class where the replay makes it (idsInside), the program's after.
            if (own && early == nullptr) continue;
            if (own) { const Substitution declared = substitute(*s, e, Declared); if (declared.carried()) real = declared.type; }
            if (real != nullptr) s->from.push_back(e), s->to.push_back(real);
            else if (wanted != nullptr && id && earlyArgs(*s, e, read, &name, &args, false, wanted)) {
                if (!ahead && !behind) return "replayed: class template-ids made first for a data member and a member function's signature";
                wanted->insert(ahead ? wanted->begin() + static_cast<std::ptrdiff_t>(sigAt++) : wanted->end(), e);
            }
            else if (e != read && (p_.dependentType(e) || readIds_.count(e) != 0 || (e->isStructOrUnion() && overParameters(e->tag()))))
                return "replayed: a member function's signature over a type the binding does not carry: " + uncarriedKind(e, read);
        }
    }
    return std::string();
}

// The made class's bases, laid by layBases as the parser lays them (classShape's: non-virtual, direct, a vptr only the first's):
// the read's, or (slice 3j) each base over the parameters the read kept (Parser::readBases_) under `s`; `seed` takes its table.
std::vector<Type::BaseSpec> Templates::basesUnder(const Type *read, const Substituted &s, BasesLaid *bases, const std::string &seed) const {
    std::vector<Type::BaseSpec> out = read->bases();
    std::vector<Parser::BaseToLay> order;
    const std::map<const Type *, std::vector<Parser::BaseToLay> >::const_iterator over = p_.readBases_.find(read);
    for (const Type::BaseSpec &b : out) order.push_back(Parser::BaseToLay{ substitute(s, b.type).type, b.access, static_cast<std::size_t>(b.written) });
    for (std::size_t q = 0; out.empty() && read->dependentBase() && over != p_.readBases_.end() && q < over->second.size(); q++)
        order.push_back(Parser::BaseToLay{ substitute(s, over->second[q].type).type, over->second[q].access, over->second[q].written });
    p_.layBases(order, nullptr, 0, seed, "struct", 0, bases);
    for (std::size_t q = 0; q < out.size(); q++) out[q].type = order[q].type, out[q].offset = bases->at[q];     // slice 3o: one over them
    for (std::size_t q = out.empty() ? 0 : order.size(); q < order.size(); q++)
        out.push_back(Type::BaseSpec(order[q].type, bases->at[q], order[q].access, false, true, static_cast<int>(order[q].written)));
    return out;
}

// The read class's data members under `s`, laid as the parser lays a class of no bit-field: its bases by layBases,
// the parser's own path, then its own members from that cursor, past an empty subobject of their type on Itanium.
void Templates::laidFromRead(const Type *read, const Substituted &s, ClassLaidOut *laid) const {
    Parser::BasesLaid bases;
    basesUnder(read, s, &bases);
    long long cursor = (bases.bitCursor + 7) / 8;
    int widest = bases.widest;
    std::size_t own = 0;     // the read's members of its own bases (a kept base over the parameters: none)
    for (const Type::BaseSpec &b : read->bases()) own += b.type->members().size();
    laid->members.swap(bases.members);
    laid->empties.swap(bases.empties);
    laid->endsZero = bases.endsZero, laid->leadsZero = bases.leadsZero;
    for (std::size_t i = own; i < read->members().size(); i++) {
        const Member &m = read->members()[i];
        const Type *t = dataUnder(s, m);
        const Type *slot = t->isReference() ? p_.types_.pointerTo(t->referent()) : t;
        const int a = slot->align(p_.target_);
        long long at = read->kind() == Kind::Union ? 0 : (cursor + a - 1) / a * a;
        const Type *element = slot;
        long long count = 1;
        while (element->isArray()) count *= element->length(), element = element->pointee();
        if (element->unqualified()->isStructOrUnion()) {
            laid->endsZero = element->unqualified()->endsWithZeroSized();
            const int each = element->size(p_.target_);
            for (bool clash = !p_.target_.microsoftNames() && read->kind() != Kind::Union; clash; ) {
                clash = false;
                for (long long k = 0; k < count && !clash; k++) clash = Parser::emptyClash(laid->empties, element, static_cast<int>(at + k * each));
                if (clash) at += a;
            }
            for (long long k = 0; k < count; k++) Parser::addEmpties(laid->empties, element, static_cast<int>(at + k * each));
        }
        cursor = std::max(cursor, at + slot->size(p_.target_));
        widest = std::max(widest, a);
        laid->members.push_back(m);
        laid->members.back().type = t;
        laid->members.back().offset = static_cast<int>(at);
    }
    if (read->polymorphic() && read->bases().empty()) {     // slice 3w: its own vptr in front, shifted as the class tail shifts
        const bool ms = p_.target_.microsoftNames();
        const int vptr = ms ? 8 : p_.pointerBytes(), shift = ms ? 8 : alignTo(vptr, widest);
        for (Member &m : laid->members) m.offset += shift;
        cursor += shift, widest = std::max(widest, vptr);
    }
    laid->totalBits = cursor * 8;
    laid->size = alignTo(static_cast<int>(std::max(cursor, bases.sizeFloor)), widest);
    laid->align = widest;
    if (laid->members.empty() && cursor == 0 && bases.sizeFloor == 0) laid->size = 1, laid->align = 1;
}

static bool nestedAt(const std::vector<std::pair<std::size_t, std::size_t> > &nested, std::size_t t, bool within = false);

// What a class made from the read cannot carry yet, by name: a pack, a member initialiser over a parameter, alignas, final.
std::string Templates::fromReadWhyNot(std::size_t start, const std::vector<TemplateParam> &params) const {
    if (p_.src_.packAt(p_.tokens_[start].pos) != 0) return "replayed: under '#pragma pack'";
    const std::string &readTag = readMembers_.at(start).first;
    for (const Member &m : readClass_.at(start)->members()) {     // slice 3q: one naming no parameter, read where the replay's is
        const std::map<std::string, std::size_t>::const_iterator init = p_.memberInit_.find(readTag + "::" + m.name);
        if (init == p_.memberInit_.end()) continue;
        if (p_.hasConstructors(readTag)) return "replayed: a member initialiser beside a written constructor";
        for (std::size_t t = init->second, depth = 0; t < p_.tokens_.size(); t++) {
            const Token &k = p_.tokens_[t];
            if (depth == 0 && (k.is(";") || k.is(","))) break;
            if (k.is("(") || k.is("[") || k.is("{")) depth++;
            else if ((k.is(")") || k.is("]") || k.is("}")) && depth-- == 0) break;
            for (const TemplateParam &q : params)
                if (!q.name.empty() && k.text == q.name) return "replayed: a member initialiser naming a template parameter";
        }
    }
    // A defaulted special is kept by the read's tag; each held body is a written member's.
    if (p_.defaulted_.count(readTag) != 0) return "replayed: a defaulted special member";
    std::map<std::string, bool> written;
    for (const Signature &f : readMembers_.at(start).second) {
        written[f.symbol] = !f.implicit;
    }
    for (const std::pair<Parser::PendingBody, std::string> &h : readBodies_.at(start))     // a nested class's: its own path's (3v)
        if (!written[h.second] && !nestedAt(nestedByPath(start), h.first.start, true)) return "replayed: a held body of no written member";
    for (std::size_t t = start, depth = 0; t < p_.tokens_.size(); t++) {
        const Token &k = p_.tokens_[t];
        if (k.is("alignas")) return "replayed: alignas";
        if (depth == 0 && k.is("final")) return "replayed: a final class";
        if (k.is("{")) depth++;
        else if (k.is("}") && --depth == 0) break;
    }
    return std::string();
}

// Step (1): the class `tag` made from the read under `s` with no replay - the parser's head for a specialization, then its
// completion tail (completeClass) over the read's members laid under the binding, in the scope a replay reads it in.
const Type *Templates::classFromRead(const TemplateDecl &decl, const std::string &tag, bool partial, std::size_t start,
                                     const std::vector<TemplateArg> &args, const Substituted &s, std::size_t pos,
                                     std::vector<Parser::PendingBody> *bodies,
                                     const std::vector<TemplateParam> &params, const std::vector<const Type *> &binding, const std::vector<long long> &values) {
    const DoorAt madeDoor(this, "made class");     // D1: a class made from the read, and what it reads again
    const Type *read = readClass_.at(start);
    const Kind kind = partial ? Kind::Struct : read->kind();
    Type *type = p_.types_.structType(kind, tag);
    if (type->isComplete()) p_.src_.fail(pos, "struct " + tag + " is defined twice");
    type->setLocalName(tag);
    type->setSpecialization(read->templateName(), args);
    type->setTemplateNamespace(decl.ns);
    p_.lookup_.declareTypeName(tag + "::" + read->templateName(), type);
    type->setDeclaredClass(!partial && decl.classKey);
    type->setBeingDefined();
    const std::string &readTag = readMembers_.at(start).first;
    for (const Member &m : read->members()) {     // slice 3q: its initialiser's tokens, read where the replay records them
        const std::map<std::string, std::size_t>::const_iterator init = p_.memberInit_.find(readTag + "::" + m.name);
        if (init != p_.memberInit_.end()) p_.memberInit_[tag + "::" + m.name] = init->second;
        if (p_.memberInitCopy_.count(readTag + "::" + m.name) != 0) p_.memberInitCopy_.insert(tag + "::" + m.name);
    }
    Parser::BasesLaid laidBases;
    for (const Type::BaseSpec &b : basesUnder(read, s, &laidBases, tag)) type->addBase(b.type, b.offset, b.access, b.isVirtual, b.direct, b.written);
    if (read->polymorphic()) type->setPolymorphic(true);     // slice 3v: a polymorphic base's, as the parser's class tail says,
    type->setPrimaryBase(read->primaryBase());                 // and Itanium's primary base, the one the read's body chose
    p_.lookup_.declareTypeName(p_.tagKey(kind == Kind::Union ? "union" : "struct", tag), type);
    Substituted own = s;     // a pointer to the class itself, or a typedef of it, is one to the class being made
    readStands(start, type, &own);
    for (const std::pair<std::string, const Type *> &td : readTypedefs_.at(start))     // step (2): as the replay declares them
        if (typedefByPath(start, s, td) == 0) p_.lookup_.declareTypeName(tag + "::" + td.first, substitute(own, td.second).type);
    p_.lookup_.declareTypeName(tag, type);
    membersFromRead(start, tag, s, type, bodies, pos);
    staticsFromRead(start, decl, tag, s, type, type, params, binding, values, bodies);
    Substituted laidAs = s;     // slice 3r: its named member enumerations declared now
    readStands(start, type, &laidAs);
    ClassLaidOut laid;
    laidFromRead(read, laidAs, &laid);
    std::vector<const Type *> outerClasses;
    outerClasses.swap(p_.classStack_);
    const Type *outerCurrent = p_.currentClass_;
    p_.currentClass_ = nullptr;
    const bool wasDeferring = p_.deferSpecializationBodies_;
    p_.deferSpecializationBodies_ = true;
    p_.heldForSpecialization_.clear();
    const std::vector<std::string> outerStack = p_.namespaceStack_;
    if (!decl.ns.empty()) p_.namespaceStack_ = Parser::namespacePathStack(decl.ns);
    p_.classStack_.push_back(type);     // the body's scope, as its member templates are declared in it
    templatesFromRead(start, tag, decl, params, binding, values, args, s.packs);
    p_.classStack_.pop_back();
    const Type *made = p_.completeClass(type, tag, true, laid, pos);
    p_.namespaceStack_ = outerStack;
    p_.heldForSpecialization_.clear();
    p_.deferSpecializationBodies_ = wasDeferring;
    p_.currentClass_ = outerCurrent;
    p_.classStack_.swap(outerClasses);
    return made;
}

// A static data member's declarator in the class's own braces: the token after its name and bounds, 0 where none is; `cx`
// whether `constexpr` is written on its declaration, `name` its name's position.
std::size_t Templates::staticAt(std::size_t start, const std::string &n, bool *cx, std::size_t *name) const {
    *cx = false;
    for (std::size_t t = start, braces = 0, parens = 0; t + 1 < p_.tokens_.size(); t++) {
        const Token &k = p_.tokens_[t], &next = p_.tokens_[t + 1];
        braces += k.is("{"), parens += k.is("(");
        if ((k.is("}") && --braces == 0) || (braces == 0 && k.is(";"))) break;
        if (k.is(")") && parens > 0) parens--;
        if (braces != 1 || parens != 0) continue;
        if (k.is(";") || k.is("{") || k.is("}") || k.is(":")) *cx = false;
        *cx = *cx || k.is("constexpr");
        if (k.text != n || !(next.is(";") || next.is(",") || next.is("[") || next.is("=") || next.is("{"))) continue;
        *name = k.pos;
        std::size_t at = t + 1;
        while (at < p_.tokens_.size() && p_.tokens_[at].is("["))
            for (int d = 0; at < p_.tokens_.size(); ) {
                d += p_.tokens_[at].is("[") ? 1 : p_.tokens_[at].is("]") ? -1 : 0;
                at++;
                if (d == 0) break;
            }
        return at;
    }
    return 0;
}

// `typedef X<num, den> n;` in the class's own braces, X its own template and an argument naming one of its static data members:
// the `typedef`'s token, which staticsFromRead reads after them by the class body's path (the replay's place); 0 where none is.
std::size_t Templates::ownTypedefAt(std::size_t start, const std::string &n) const {
    const Type *read = readClass_.at(start);
    const std::size_t colon = read->templateName().rfind(':');
    const std::string own = read->templateName().substr(colon == std::string::npos ? 0 : colon + 1);
    for (std::size_t t = start, braces = 0; t + 3 < p_.tokens_.size(); t++) {
        braces += p_.tokens_[t].is("{");
        if (p_.tokens_[t].is("}") && --braces == 0) break;
        if (braces != 1 || !p_.tokens_[t].is("typedef") || p_.tokens_[t + 1].text != own || !p_.tokens_[t + 2].is("<")) continue;
        bool onStatic = false;
        std::size_t e = t + 3;
        for (int angles = 1; e < p_.tokens_.size() && angles > 0 && !p_.tokens_[e].is(";"); e++) {
            angles += p_.tokens_[e].is("<") ? 1 : p_.tokens_[e].is(">") ? -1 : p_.tokens_[e].is(">>") ? -2 : 0;
            for (const Type::StaticMember &m : read->staticMembers()) onStatic = onStatic || p_.tokens_[e].text == m.name;
        }
        if (onStatic && e + 1 < p_.tokens_.size() && p_.tokens_[e].text == n && p_.tokens_[e + 1].is(";")) return t;
    }
    return 0;
}

// Work (1): what keeps a read static data member from being declared by staticsFromRead under `s`, by name; empty when nothing.
std::string Templates::staticShape(std::size_t start, const Substituted &s) const {
    const Type *read = readClass_.at(start);
    const auto names = [&](const std::string &n) {     // a template, whose use in an initialiser makes a class where it is evaluated
        if (templates_.count(n) != 0) return true;
        for (std::map<std::string, TemplateDecl>::const_iterator i = templates_.begin(); i != templates_.end(); ++i)
            if (i->first.size() > n.size() + 2 && i->first.compare(i->first.size() - n.size() - 2, n.size() + 2, "::" + n) == 0) return true;
        return false;
    };
    const std::size_t colon = read->templateName().rfind(':');
    const std::string own = read->templateName().substr(colon == std::string::npos ? 0 : colon + 1);
    // Another static data member's initialiser, or ownTypedefAt's typedef: staticsFromRead reads each after this one, in order.
    const auto laterInOrder = [&](std::size_t t) {
        std::size_t e = t;
        while (e < p_.tokens_.size() && !p_.tokens_[e].is(";") && !p_.tokens_[e].is("(") && !p_.tokens_[e].is("{") && !p_.tokens_[e].is("=")) e++;
        if (e >= p_.tokens_.size()) return false;
        if (p_.tokens_[t].is("typedef")) {
            while (e < p_.tokens_.size() && !p_.tokens_[e].is(";")) e++;
            return e < p_.tokens_.size() && ownTypedefAt(start, p_.tokens_[e - 1].text) == t;
        }
        bool cx = false;
        std::size_t name = 0;
        return p_.tokens_[e].is("=") && read->findStaticMember(p_.tokens_[e - 1].text) != nullptr &&
               staticAt(start, p_.tokens_[e - 1].text, &cx, &name) == e && !cx;
    };
    // Its ids are made where staticsFromRead evaluates it, after idsBefore's: the body's order while none is named after `k`.
    const auto namedLater = [&](std::size_t k) {
        for (std::size_t depth = 1, t = k + 1; t < p_.tokens_.size(); t++) {
            const Token &w = p_.tokens_[t], &was = p_.tokens_[t - 1];
            if (depth == 1 && w.is("{") && (was.is(")") || was.is("const") || was.is("volatile") || was.is("override") || was.is("noexcept")))
                for (std::size_t inner = 0; t < p_.tokens_.size(); t++) {     // a member function's body, replayed later by both doors
                    inner += p_.tokens_[t].is("{");
                    if (p_.tokens_[t].is("}") && --inner == 0) break;
                }
            else if (w.is("{")) depth++;
            else if (w.is("}") && --depth == 0) break;
            else if (depth == 1 && (w.is("static") || w.is("typedef")) && laterInOrder(t)) {     // made in order where it is read
                while (t < p_.tokens_.size() && !p_.tokens_[t].is(";")) t++;
            }
            else if (w.kind == TokenKind::Ident && names(w.text) && !(w.text == own && t + 1 < p_.tokens_.size() && !p_.tokens_[t + 1].is("<")))     // its constructor
                return true;
        }
        return false;
    };
    for (const Type::StaticMember &m : read->staticMembers()) {
        bool cx = false;
        std::size_t name = 0;
        const std::size_t at = staticAt(start, m.name, &cx, &name);
        if (at == 0) return "replayed: a static data member its class's braces do not declare by name";
        const Type *t = substitute(s, m.type).type, *e = t;
        if (p_.dependentType(t)) return "replayed: a static data member of a type the binding does not carry";
        while (e->isPointer() || e->isReference() || e->isArray()) e = e->isReference() ? e->referent() : e->pointee();
        e = e->unqualified();
        if (e == read || overParameters(e->tag()) || readIds_.count(e) != 0)
            return "replayed: a static data member of a class the read named over its parameters";
        bool named = false;
        std::size_t k = at;
        for (std::size_t depth = 0; k < p_.tokens_.size() && (p_.tokens_[at].is("=") || p_.tokens_[at].is("{")); k++) {
            const Token &w = p_.tokens_[k];
            depth += w.is("(") || w.is("{") || w.is("[");
            if ((w.is(")") || w.is("}") || w.is("]")) && depth > 0 && --depth == 0 && w.is("}")) break;
            if (depth == 0 && (w.is(";") || w.is(","))) break;
            named = named || (w.kind == TokenKind::Ident && names(w.text));
        }
        if (named && namedLater(k)) return "replayed: a static data member's initialiser naming a template, before another template named in its class";
    }
    bool other = false;
    for (const std::size_t e : memberEnums(start, &other)) {     // slice 3n: an enumerator's initialiser naming a template, the same order
        bool named = false;
        std::size_t close = e;
        for (; close < p_.tokens_.size() && !p_.tokens_[close].is("}"); close++) named = named || (p_.tokens_[close].kind == TokenKind::Ident && names(p_.tokens_[close].text));
        if (named && namedLater(close)) return "replayed: a member enumeration naming a template, before another template named in its class";
    }
    return std::string();
}

// Work (1): the read's static data members declared on `owner` by the class body's own path (Parser::declareStaticMember), each at
// its name in the body under the binding and in `scope`'s braces, so an initialiser in the class is evaluated as the replay does.
void Templates::staticsFromRead(std::size_t start, const TemplateDecl &decl, const std::string &tag, const Substituted &s, Type *owner,
                                const Type *scope, const std::vector<TemplateParam> &params, const std::vector<const Type *> &binding,
                                const std::vector<long long> &values, std::vector<Parser::PendingBody> *bodies) {
    const std::vector<Type::StaticMember> &statics = readClass_.at(start)->staticMembers();
    std::vector<std::size_t> typedefs;     // typedefByPath's, among the statics; held apart under the check (`owner` no `scope`)
    for (const std::pair<std::string, const Type *> &td : readTypedefs_.at(start))
        if (typedefByPath(start, s, td) != 0) typedefs.push_back(typedefByPath(start, s, td));
    bool other = false;
    const std::vector<std::size_t> enums = memberEnums(start, &other);     // slice 3m: read in the body's order among the statics
    std::vector<std::pair<std::size_t, std::size_t> > nested;     // slice 3v: read where the class is made, not held apart (no bodies)
    if (bodies != nullptr) nested = nestedByPath(start);
    if (statics.empty() && typedefs.empty() && enums.empty() && nested.empty()) return;
    std::size_t classEnd = start;     // the class's `}`: its own held bodies lie before it
    for (int depth = 0; classEnd < p_.tokens_.size(); classEnd++)
        if ((depth += p_.tokens_[classEnd].is("{") ? 1 : p_.tokens_[classEnd].is("}") ? -1 : 0) == 0 && p_.tokens_[classEnd].is("}")) break;
    std::vector<std::pair<std::size_t, std::size_t> > order;     // each one's token, its static's index (statics.size(): an enum, + 1 a typedef)
    for (std::size_t i = 0; i < statics.size(); i++) {
        bool cx = false;
        std::size_t name = 0;
        order.push_back(std::make_pair(staticAt(start, statics[i].name, &cx, &name), i));
    }
    for (const std::size_t e : enums) order.push_back(std::make_pair(e, statics.size()));
    for (const std::size_t t : typedefs) order.push_back(std::make_pair(t, statics.size() + 1));
    for (const std::pair<std::size_t, std::size_t> &n : nested) order.push_back(std::make_pair(n.first, statics.size() + 2));
    std::sort(order.begin(), order.end());
    const Type *enumScope = owner == scope || enums.empty() ? scope : p_.types_.structType(Kind::Struct, tag + "::$held");
    std::vector<Shadow> undo;
    const DoorAt staticsDoor(this, "made class: staticsFromRead");
    bindTemplateParameters(params, binding, values, s.packs.size() == params.size() ? s.packs : Packs(params.size()), &undo);
    const Parser::LocalsHidden hidden(&p_);
    const std::size_t resume = p_.at_, angle = p_.angleSplit_;
    std::vector<const Type *> outerClasses(1, scope);
    outerClasses.swap(p_.classStack_);
    const Type *outerCurrent = p_.currentClass_;
    p_.currentClass_ = nullptr;
    const std::vector<std::string> outerStack = p_.namespaceStack_;
    if (!decl.ns.empty()) p_.namespaceStack_ = Parser::namespacePathStack(decl.ns);
    for (const std::pair<std::size_t, std::size_t> &o : order) {
        if (o.second == statics.size() + 2) {     // slice 3v: `struct N { ... };` as the class body reads it, its bodies held with the class's
            std::vector<Parser::PendingBody> outerPending;
            outerPending.swap(p_.pendingBodies_);
            const bool wasDeferring = p_.deferSpecializationBodies_;
            const std::string outerInline = p_.inlineOwner_;
            p_.deferSpecializationBodies_ = true, p_.inlineOwner_.clear();
            Parser::StorageClass storage = Parser::StorageNone;
            p_.at_ = o.first;
            reentered("jump: a nested class (nestedByPath)");
            p_.specifiers(&storage);
            p_.deferSpecializationBodies_ = wasDeferring, p_.inlineOwner_ = outerInline;
            std::vector<Parser::PendingBody>::iterator at = bodies->end();     // 3ac: before the class's bodies after its `}`, the replay's order
            while (at != bodies->begin() && (at - 1)->start > o.first && (at - 1)->start < classEnd) --at;
            bodies->insert(at, p_.pendingBodies_.begin(), p_.pendingBodies_.end());
            p_.pendingBodies_.swap(outerPending);
            continue;
        }
        if (o.second == statics.size() + 1) {
            p_.at_ = o.first;
            reentered("jump: a member typedef (typedefByPath)");
            Parser::StorageClass storage = Parser::StorageNone;
            const Parser::Declared td = p_.declarator(p_.specifiers(&storage));
            p_.lookup_.declareTypeName(tag + (owner == scope ? "::" : "::$held::") + td.name, td.type);
            continue;
        }
        if (o.second == statics.size()) {     // `enum { ... }` as the class body reads it, its enumerators `tag::a` (`tag::$held::a`)
            Parser::StorageClass storage = Parser::StorageNone;
            if (enumScope != scope) p_.classStack_.push_back(enumScope), p_.currentClass_ = scope;     // its names found in `scope`
            p_.at_ = o.first;
            reentered("jump: a member enumeration");
            p_.specifiers(&storage);
            p_.classStack_.resize(1), p_.currentClass_ = nullptr;
            continue;
        }
        const Type::StaticMember &m = statics[o.second];
        bool cx = false;
        std::size_t name = 0;
        p_.at_ = staticAt(start, m.name, &cx, &name);
        reentered("jump: a static data member");
        p_.declareStaticMember(tag, owner, Parser::Declared(m.name, substitute(s, m.type).type, name), m.access, m.isVolatile, m.isThreadLocal, cx);
    }
    p_.namespaceStack_ = outerStack;
    p_.currentClass_ = outerCurrent;
    p_.classStack_.swap(outerClasses);
    p_.angleSplit_ = angle;
    p_.at_ = resume;
    unbindTemplateParameters(undo);
}

// Under -check=templates: staticsFromRead's members on a class of their own, in the scope of `made`, held against the replay's.
std::string Templates::staticsHeld(std::size_t start, const TemplateDecl &decl, const std::string &tag, const Substituted &s, const Type *made,
                                   const std::vector<TemplateParam> &params, const std::vector<const Type *> &binding,
                                   const std::vector<long long> &values) {
    Type *mine = p_.types_.anonymousStruct(made->kind());
    const std::size_t enumsFrom = p_.lookup_.enums_.size();
    staticsFromRead(start, decl, tag, s, mine, made, params, binding, values);
    const std::string held = tag + "::$held::";
    for (std::size_t i = enumsFrom; i < p_.lookup_.enums_.size(); i++) {     // slice 3m: each enumerator against the replay's
        const EnumConst &e = p_.lookup_.enums_[i];
        if (e.name.compare(0, held.size(), held) != 0) continue;     // another scope's, declared by what an initialiser made
        const std::unordered_map<std::string, std::size_t>::const_iterator r =
            p_.lookup_.enumIndex_.find(tag + "::" + e.name.substr(std::min(held.size(), e.name.size())));
        if (r == p_.lookup_.enumIndex_.end()) return "differ: a member enumerator the replay does not declare";
        const Type *type = e.type;     // slice 3r: a named one's `tag::$held::E` stands for the replay's `tag::E`, of its kind
        if (type != nullptr && type->isEnumeration() && type->tag().compare(0, held.size(), held) == 0) {
            const Type *theirs = p_.lookup_.findTypedef(p_.tagKey("enum", tag + "::" + type->tag().substr(held.size())));
            if (theirs != nullptr && theirs->kind() == type->kind() && theirs->isScopedEnumeration() == type->isScopedEnumeration()) type = theirs;
        }
        if (p_.lookup_.enums_[r->second].value != e.value || p_.lookup_.enums_[r->second].type != type ||
            p_.lookup_.enums_[r->second].valueDependent != e.valueDependent)
            return "differ: a member enumerator's value or type";
    }
    const std::vector<Type::StaticMember> &a = mine->staticMembers(), &b = made->staticMembers();
    if (a.size() != b.size()) return "differ: the number of static data members";
    for (std::size_t i = 0; i < a.size(); i++) {
        if (a[i].name != b[i].name || a[i].type != b[i].type || a[i].access != b[i].access || a[i].symbol != b[i].symbol)
            return "differ: a static data member's name, type, access or symbol";
        if (a[i].folded != b[i].folded || a[i].value != b[i].value || a[i].isThreadLocal != b[i].isThreadLocal || a[i].isVolatile != b[i].isVolatile)
            return "differ: a static data member's value, thread_local or volatile";
    }
    for (const std::pair<std::string, const Type *> &td : readTypedefs_.at(start))
        if (typedefByPath(start, s, td) != 0 && p_.lookup_.findTypedef(tag + "::$held::" + td.first) != p_.lookup_.findTypedef(tag + "::" + td.first))
            return "differ: a member typedef read by the class body's path";
    return "equal";
}

static std::string retagged(std::string w, const std::string &from, const std::string &to) {
    for (std::size_t at = w.find(from); at != std::string::npos; at = w.find(from, at + to.size())) w.replace(at, from.size(), to);
    return w;
}

// A read member function's name under the real tag; a conversion function's names its target under `s`, as operatorName files it.
std::string Templates::memberName(const Signature &a, const std::string &readTag, const std::string &tag, const Substituted &s) const {
    if (a.name == "operator " + Parser::conversionSpelling(a.returns)) return "operator " + Parser::conversionSpelling(substitute(s, a.returns).type);
    return retagged(a.name, readTag, tag);
}

// A held body's key under the real tag: retagged, or a conversion function's as memberName names it.
std::string Templates::bodyKey(std::size_t start, const std::pair<Parser::PendingBody, std::string> &h, const std::string &tag, const Substituted &s) const {
    const std::pair<std::string, std::vector<Signature> > &r = readMembers_.at(start);
    std::string key = retagged(h.first.key, r.first, tag);
    for (const Signature &a : r.second)
        if (a.symbol == h.second && memberName(a, r.first, tag, s) != retagged(a.name, r.first, tag)) key = tag + "::" + memberName(a, r.first, tag, s);
    return key;
}

// Its own template under other arguments that member `a` names, made through makeClass where the replay makes it reading `a`;
// asked under `ids` (classShape's map) and mapped ahead in `s`, where readStands may take a partial's `X<$T0>` for its own class.
void Templates::idsInside(std::size_t start, const Signature &a, std::size_t pos, Substituted *ids, Substituted *s) {
    std::vector<const Type *> types(1, a.returns);
    types.insert(types.end(), a.params.begin(), a.params.end());
    for (const Type *t : types) {
        const Type *e = substitute(*ids, t).type;
        while (e->isPointer() || e->isReference() || e->isArray()) e = e->isReference() ? e->referent() : e->pointee();
        e = e->unqualified();
        std::string name;
        std::vector<TemplateArg> args;
        if (substitute(*ids, e, Declared).carried() || !earlyArgs(*ids, e, readClass_.at(start), &name, &args, true)) continue;
        const Type *made = makeId(name, args, pos, nullptr);
        classCensus_["made inside the class, as its replay makes it: its own template under other arguments"]++;
        ids->from.push_back(e), ids->to.push_back(made);
        s->from.insert(s->from.begin(), e), s->to.insert(s->to.begin(), made);
    }
}

// Step (1): the read's written member functions declared under the real tag where the replay's class body declares them, and
// its held bodies keyed to them (classSkip's records); the implicit specials are completeClass's.
void Templates::membersFromRead(std::size_t start, const std::string &tag, Substituted s, const Type *made, std::vector<Parser::PendingBody> *bodies,
                                std::size_t pos) {
    Substituted ids = s;
    readStands(start, made, &s);
    const std::pair<std::string, std::vector<Signature> > &r = readMembers_.at(start);
    std::map<std::string, std::size_t> at;     // the read's symbol, its function here
    const bool own = made->polymorphic() && made->bases().empty();     // slice 3w: a table of its own, seeded from the read's
    if (own) p_.classes_.vtables_[tag] = p_.classes_.vtables_[r.first];
    std::vector<VSlot> none;
    for (const Signature &a : r.second) {
        if (a.implicit) continue;
        idsInside(start, a, pos, &ids, &s);
        Signature f = a;
        f.name = memberName(a, r.first, tag, s);
        f.owner = tag;
        f.returns = substitute(s, a.returns).type;
        f.params = substitute(s, a.params).types;
        f.written = substitute(s, a.written).types;
        f.symbol = memberSymbol(a, f.name, f, tag);
        if (p_.defaultArgs_.count(a.symbol) != 0)     // its defaults' tokens, kept by the read's symbol as declareMember keeps them
            p_.defaultArgs_[f.symbol] = p_.defaultArgs_.at(a.symbol), p_.defaultArgNamespace_[f.symbol] = p_.defaultArgNamespace_[a.symbol];
        f.defined = f.used = false;
        if (p_.constant_.constexprCtorSymbols_.count(a.symbol) != 0) p_.constant_.constexprCtorSymbols_.insert(f.symbol);     // 3y: as declared
        const std::string key = f.name == tag ? Parser::constructorKey(tag) : f.name[0] == '~' ? Parser::destructorKey(tag) : tag + "::" + f.name;
        at[a.symbol] = p_.lookup_.functions_.size();
        p_.lookup_.functionIndex_[key].push_back(p_.lookup_.functions_.size());
        p_.lookup_.functions_.push_back(f);
        if (!f.isVirtual) continue;
        const std::vector<VSlot> &readSlots = p_.classes_.vtables_[r.first];     // slice 3v: the slot the read's override took,
        std::vector<VSlot> &slots = p_.classes_.vtables_[tag];                     // in the table layBases seeded from the base
        for (std::size_t i = 0; i < readSlots.size() && i < slots.size(); i++)
            if (readSlots[i].symbol == a.symbol) slots[i].symbol = f.symbol, slots[i].pure = false, slots[i].returns = f.returns;
    }
    for (VSlot &v : own ? p_.classes_.vtables_[tag] : none) {     // each slot's signature under `s`, a deleting destructor this tag's
        v.params = substitute(s, v.params).types;
        if (v.returns != nullptr) v.returns = substitute(s, v.returns).type;
        if (v.name == "~$deleting" || (v.name == "~" && p_.target_.microsoftNames())) v.symbol = p_.linkage_.deletingDestructorSymbol(tag);
    }
    for (const std::pair<Parser::PendingBody, std::string> &h : readBodies_.at(start)) {
        if (nestedAt(nestedByPath(start), h.first.start, true)) continue;     // staticsFromRead's, by the nested class's path
        bodies->push_back(h.first);
        bodies->back().key = bodyKey(start, h, tag, s);
        bodies->back().tag = retagged(h.first.tag, r.first, tag);
        bodies->back().which = at.at(h.second);
    }
}

// The member templates the body of class `tag` declared at its own level, in the body's order (each one's `template`).
std::vector<const TemplateDecl *> Templates::templatesOf(const std::string &tag) const {
    std::vector<const TemplateDecl *> out;
    for (std::map<std::string, std::vector<TemplateDecl> >::const_iterator m = memberTemplates_.lower_bound(tag + "::");
         m != memberTemplates_.end() && m->first.compare(0, tag.size() + 2, tag + "::") == 0; ++m)
        for (const TemplateDecl &t : m->second) if (t.ownerTag == tag) out.push_back(&t);
    std::sort(out.begin(), out.end(), [](const TemplateDecl *a, const TemplateDecl *b) { return a->start < b->start; });
    return out;
}

// Work (1): the read's member templates declared under `tag` by the class body's own path (Parser::memberTemplate), each at its
// `template` in the body's order, under the binding the replay's body records (instantiatingParams_), the class's parameters bound.
void Templates::templatesFromRead(std::size_t start, const std::string &tag, const TemplateDecl &decl, const std::vector<TemplateParam> &params,
                                  const std::vector<const Type *> &binding, const std::vector<long long> &values, const std::vector<TemplateArg> &args,
                                  const Packs &packs) {
    std::vector<std::pair<std::size_t, std::pair<std::string, Access> > > at;
    for (const TemplateDecl *mt : templatesOf(readMembers_.at(start).first))
        at.push_back(std::make_pair(mt->start, std::make_pair(mt->ownerLocal, mt->memberAccess)));
    if (at.empty()) return;
    std::vector<Shadow> undo;
    const DoorAt templatesDoor(this, "made class: templatesFromRead");
    bindTemplateParameters(params, binding, values, packs.empty() ? Packs(params.size()) : packs, &undo);
    const Parser::LocalsHidden hidden(&p_);
    const std::size_t resume = p_.at_, angle = p_.angleSplit_;
    const std::string inlineOwner = p_.inlineOwner_, outerNamespace = p_.instantiatingNamespace_;
    const std::vector<TemplateArg> outerArgs = p_.instantiatingArgs_;
    const auto outerParams = p_.instantiatingParams_;
    const auto outerBinding = p_.instantiatingBinding_;
    const auto outerValues = p_.instantiatingValues_;
    p_.inlineOwner_.clear();
    p_.instantiatingArgs_ = args;
    p_.instantiatingNamespace_ = decl.ns;
    p_.instantiatingParams_ = params;
    p_.instantiatingBinding_ = binding;
    p_.instantiatingValues_ = values;
    for (const std::pair<std::size_t, std::pair<std::string, Access> > &a : at) {
        p_.at_ = a.first;
        reentered("jump: a member template's declaration");
        p_.memberTemplate(tag, a.second.first, a.second.second);
    }
    p_.instantiatingArgs_ = outerArgs;
    p_.instantiatingNamespace_ = outerNamespace;
    p_.instantiatingParams_ = outerParams;
    p_.instantiatingBinding_ = outerBinding;
    p_.instantiatingValues_ = outerValues;
    p_.inlineOwner_ = inlineOwner;
    p_.angleSplit_ = angle;
    p_.at_ = resume;
    unbindTemplateParameters(undo);
}

// The read's member templates held against those declared under `tag`, each as the replay records it; empty when they agree.
std::string Templates::templatesHeld(const std::string &readTag, const Substituted &s, const std::string &tag) const {
    const std::vector<const TemplateDecl *> mine = templatesOf(readTag), theirs = templatesOf(tag);
    if (mine.size() != theirs.size()) return "differ: the number of member templates";
    for (std::size_t i = 0; i < mine.size(); i++) {
        const TemplateDecl &a = *mine[i], &b = *theirs[i];
        if (a.start != b.start || a.afterParams != b.afterParams || (a.isConstructor ? Parser::localOf(tag) : a.name) != b.name ||
            a.isConstructor != b.isConstructor || a.isExplicit != b.isExplicit || a.staticMember != b.staticMember || a.defined != b.defined ||
            a.memberAccess != b.memberAccess || a.params.size() != b.params.size())
            return "differ: a member template's place, name or kind";
        if (a.classParams.size() != b.classParams.size() || a.classBinding.size() != b.classBinding.size() ||
            (s.values.empty() ? a.classValues : s.values) != b.classValues)     // the read's are stand-ins; `s` carries the binding's
            return "differ: a member template's class parameters";
        for (std::size_t k = 0; k < a.classBinding.size(); k++) {     // a pack's is the class's own binding, its members below
            const bool value = k < a.classParams.size() && a.classParams[k].type != nullptr && a.classBinding[k] != nullptr &&
                               a.classBinding[k]->kind() == Kind::TemplateParam;     // the read's pattern for N, its value held above
            if (k < b.classParams.size() && b.classParams[k].isPack) continue;
            const std::map<int, std::string>::const_iterator tt = s.templates.find(static_cast<int>(k));     // a template template one's
            if (k < a.classParams.size() && a.classParams[k].isTemplate && tt != s.templates.end()) {
                if (p_.templateNameTypeOf(templates_.at(tt->second)) != b.classBinding[k]) return "differ: a member template's class binding";
                continue;
            }
            if (value ? b.classBinding[k] != nullptr && b.classBinding[k]->kind() == Kind::TemplateParam : substitute(s, a.classBinding[k]).type != b.classBinding[k])
                return "differ: a member template's class binding";
        }
        for (std::size_t k = 0; k < b.classPacks.size() && k < b.classParams.size(); k++)     // what templatesFromRead binds
            if (b.classParams[k].isPack && (k >= s.packs.size() || static_cast<const std::vector<const Type *> &>(s.packs[k]) !=
                static_cast<const std::vector<const Type *> &>(b.classPacks[k]) || s.packs[k].values != b.classPacks[k].values))
                return "differ: a member template's class pack";
    }
    return std::string();
}

// The read's members laid under `s` (laidFromRead), held against `made`, the replay's.
std::string Templates::classLaid(const Type *read, const Substituted &s, const Type *made) const {
    if (!made->isComplete()) return "differ: the replay's class is incomplete";
    if (made->hasVptr() && !made->bases().empty() && !made->bases()[0].type->polymorphic()) return "differ: the replay's class has a vptr of its own";
    Parser::BasesLaid laidBases;
    const std::vector<Type::BaseSpec> mineBases = basesUnder(read, s, &laidBases);
    if (made->bases().size() != mineBases.size()) return "differ: the number of bases";
    if (made->primaryBase() != read->primaryBase() || made->polymorphic() != read->polymorphic()) return "differ: the primary base or polymorphism";
    for (std::size_t i = 0; i < made->bases().size(); i++) {
        const Type::BaseSpec &a = mineBases[i], &b = made->bases()[i];
        if (a.type != b.type || a.offset != b.offset || a.access != b.access || a.isVirtual != b.isVirtual || a.direct != b.direct || a.written != b.written)
            return "differ: a base's class, offset or access";
    }
    ClassLaidOut laid;
    laidFromRead(read, s, &laid);
    const bool noData = laid.members.empty() && laid.totalBits == 0;     // completeClass's data size and cl's flags from `laid`
    if (made->dataSize() != (noData ? 0 : laid.size) && made->dataSize() != static_cast<int>((laid.totalBits + 7) / 8)) return "differ: the data size";
    if (made->endsWithZeroSized() != (noData || laid.endsZero) || made->leadsWithZeroSized() != (noData || laid.leadsZero))
        return "differ: a zero-sized subobject at an end";
    const std::vector<Member> &mine = laid.members;
    const int size = laid.size, align = laid.align;
    const std::vector<Member> &theirs = made->members();
    if (mine.size() != theirs.size()) return "differ: the number of data members";
    for (std::size_t i = 0; i < mine.size(); i++) {
        if (mine[i].name != theirs[i].name) return "differ: a data member's name";
        if (mine[i].type != theirs[i].type) return "differ: a data member's type: " + mine[i].type->describe() + " for " + theirs[i].type->describe();
        if (mine[i].offset != theirs[i].offset) return "differ: a data member's offset";
        if (mine[i].access != theirs[i].access || mine[i].isMutable != theirs[i].isMutable || mine[i].isVolatile != theirs[i].isVolatile)
            return "differ: a data member's access, mutable or volatile";
    }
    if (size != made->size(p_.target_)) return "differ: the size";
    if (align != made->align(p_.target_)) return "differ: the alignment";
    return "equal";
}

// Slice 3b: the read's member functions under `s`, the read class standing for `made`, held against the replay's under `tag`.
std::string Templates::classMembers(std::size_t start, Substituted s, const Type *made, const std::string &tag, std::size_t fnFrom,
                                    std::map<std::string, std::string> *symbols) const {
    const std::map<std::size_t, std::pair<std::string, std::vector<Signature> > >::const_iterator r = readMembers_.find(start);
    if (r == readMembers_.end()) return "differ: no member functions kept from the read";
    // A data member of class type or a base over the parameters (slice 3o): completeClass's under the binding, not the read's.
    bool specials = !readClass_.at(start)->dependentBase();
    for (const Type::BaseSpec &b : readClass_.at(start)->bases()) specials = specials && !overParameters(b.type->tag()) && readIds_.count(b.type) == 0;
    for (const Member &m : readClass_.at(start)->members()) {
        const Type *e = substitute(s, m.type).type;
        while (e->isArray()) e = e->pointee();
        specials = specials && !e->unqualified()->isStructOrUnion();
    }
    readStands(start, made, &s);
    std::vector<const Signature *> theirs, mine;
    for (std::size_t f = fnFrom; f < p_.lookup_.functions_.size(); f++)
        if (p_.lookup_.functions_[f].owner == tag && (specials || !p_.lookup_.functions_[f].implicit)) theirs.push_back(&p_.lookup_.functions_[f]);
    for (const Signature &a : r->second.second) if (specials || !a.implicit) mine.push_back(&a);
    const std::string &readTag = r->second.first;
    if (mine.size() != theirs.size()) return "differ: the number of member functions";
    for (std::size_t i = 0; i < mine.size(); i++) {
        const Signature &a = *mine[i], &b = *theirs[i];
        const std::string name = memberName(a, readTag, tag, s);
        if (name != b.name) return "differ: a member function's name: " + name + " for " + b.name;
        if (substitute(s, a.returns).type != b.returns || substitute(s, a.params).types.size() != b.params.size()) return "differ: a member function's return type or arity";
        if (substitute(s, a.params).types != b.params) return "differ: a member function's parameter type";
        if (a.variadic != b.variadic || a.constThis != b.constThis || a.access != b.access || a.isVirtual != b.isVirtual || a.isExplicit != b.isExplicit ||
            a.isStaticMember != b.isStaticMember || a.isNoexcept != b.isNoexcept || a.inlineBody != b.inlineBody || a.refThis != b.refThis)
            return "differ: a member function's qualifiers, access or kind";
        if (a.implicit != b.implicit) return "differ: which special members are implicit";
        if (memberSymbol(a, name, s, tag) != b.symbol) return "differ: a member function's symbol";
        const bool defaults = p_.defaultArgs_.count(a.symbol) != 0;
        if (defaults != (p_.defaultArgs_.count(b.symbol) != 0) || (defaults && (p_.defaultArgs_.at(a.symbol) != p_.defaultArgs_.at(b.symbol) ||
            p_.defaultArgNamespace_.at(a.symbol) != p_.defaultArgNamespace_.at(b.symbol))))
            return "differ: a member function's default arguments or the scope they are read in";
        (*symbols)[a.symbol] = b.symbol;
    }
    const std::string table = vtableHeld(readTag, tag, *symbols);
    if (!table.empty()) return table;
    const std::string templates = templatesHeld(readTag, s, tag);
    if (!templates.empty()) return templates;
    return "equal";
}

// Slice 3v: `tag`'s table held against the read's - each slot's name, purity and finality, its function the read's under `symbols`,
// a destructor's slot the class's own (registerDestructor's), an inherited entry the same; empty when they agree.
std::string Templates::vtableHeld(const std::string &readTag, const std::string &tag, const std::map<std::string, std::string> &symbols) const {
    const std::map<std::string, std::vector<VSlot> > &tables = p_.classes_.vtables_;
    const std::map<std::string, std::vector<VSlot> >::const_iterator mine = tables.find(readTag), theirs = tables.find(tag);
    const std::size_t n = mine == tables.end() ? 0 : mine->second.size();
    if (n != (theirs == tables.end() ? 0 : theirs->second.size())) return "differ: the number of vtable slots";
    const Signature *dtor = n == 0 ? nullptr : p_.destructorOf(p_.lookup_.findTypedef(tag));
    for (std::size_t i = 0; i < n; i++) {
        const VSlot &a = mine->second[i], &b = theirs->second[i];
        const std::map<std::string, std::string>::const_iterator to = symbols.find(a.symbol);
        const bool deleting = a.name == "~$deleting" || (a.name == "~" && p_.target_.microsoftNames());
        const std::string want = to != symbols.end() ? to->second : deleting ? p_.linkage_.deletingDestructorSymbol(tag)
                               : a.name == "~" && dtor != nullptr ? dtor->symbol : a.symbol;
        if (a.name != b.name || a.pure != b.pure || a.isFinal != b.isFinal || want != b.symbol) return "differ: a vtable slot: " + b.name;
    }
    return std::string();
}

// A read member function's name for the real tag from its signature under `s`, as the parser and Classes spell it on each ABI.
std::string Templates::memberSymbol(const Signature &a, const std::string &name, const Substituted &s, const std::string &tag) const {
    Signature under = a;
    under.params = substitute(s, a.params).types, under.returns = substitute(s, a.returns).type, under.written = substitute(s, a.written).types;
    return memberSymbol(a, name, under, tag);
}

// The same from `under`, the read's signature already under the binding (membersFromRead's, the types it declared).
std::string Templates::memberSymbol(const Signature &a, const std::string &name, const Signature &under, const std::string &tag) const {
    {
        const std::vector<const Type *> &params = under.params;
        const Type *fn = p_.types_.functionType(under.returns, params, a.variadic), *cls = p_.lookup_.findTypedef(tag);
        std::string symbol, why;
        if (!p_.target_.microsoftNames()) {
            if (name == tag) itaniumConstructorName(tag, cls, fn, true, &symbol, &why);
            else if (name[0] == '~') itaniumDestructorName(tag, cls, true, &symbol);
            else symbol = p_.linkage_.memberSymbol(tag, name, fn, a.access, a.constThis, a.refThis, a.pos, a.isVirtual, a.isStaticMember);
        } else {
            // The written parameters under the binding stand where manglingType reads them; Classes names an implicit one public.
            const std::vector<const Type *> keptWritten = p_.writtenParams_, keptFor = p_.writtenFor_;
            p_.writtenParams_ = under.written;
            p_.writtenFor_ = a.written.empty() ? std::vector<const Type *>() : params;
            const char code = a.isVirtual ? 'U' : a.implicit ? 'Q' : a.access == Access::Public ? 'Q' : a.access == Access::Protected ? 'I' : 'A';
            if (name == tag) microsoftConstructorName(tag, cls, p_.linkage_.manglingType(fn), code, &symbol, &why);
            else if (name[0] == '~') symbol = microsoftDestructorName(tag, cls, code);
            else if (a.implicit && name == "operator=") microsoftCopyAssignName(tag, cls, fn, 'Q', &symbol, &why);
            else symbol = p_.linkage_.memberSymbol(tag, name, fn, a.access, a.constThis, a.refThis, a.pos, a.isVirtual, a.isStaticMember);
            p_.writtenParams_ = keptWritten;
            p_.writtenFor_ = keptFor;
        }
        return symbol;
    }
}

// What a class body declares at its own level beyond data members and member functions, by name; empty when nothing.
std::string Templates::classBeyond(std::size_t start) const {
    bool other = false;
    const std::vector<std::size_t> enums = memberEnums(start, &other);
    if (other) return "replayed: a member enumeration with a base or a declarator, or a named one not ended by its braces";
    const std::map<std::size_t, std::pair<std::string, std::vector<Signature> > >::const_iterator own = readMembers_.find(start);
    bool written = own == readMembers_.end() || !templatesOf(own->second.first).empty();     // slice 3r: a named one's type, mapped late
    for (std::size_t f = 0; own != readMembers_.end() && f < own->second.second.size(); f++) written = written || !own->second.second[f].implicit;
    for (const std::size_t e : enums)
        if (written && p_.tokens_[e + 1].kind == TokenKind::Ident) return "replayed: a member enumeration with a name, beside a member function";
    const std::vector<std::pair<std::size_t, std::size_t> > nested = nestedByPath(start);
    std::size_t t = start;
    while (t < p_.tokens_.size() && !p_.tokens_[t].is("{") && !p_.tokens_[t].is(";")) t++;
    for (int depth = 0; t < p_.tokens_.size(); t++) {
        const Token &k = p_.tokens_[t];
        if (k.is("{") || k.is("(") || k.is("[")) depth++;
        else if ((k.is("}") || k.is(")") || k.is("]")) && --depth == 0) break;
        else if (depth != 1) continue;
        else if (k.is("using") && !(p_.tokens_[t + 1].kind == TokenKind::Ident && p_.tokens_[t + 2].is("=")))
            return "replayed: a using-declaration";     // a member typedef or alias is the read's, readTypedefs_
        else if (k.is("enum") || nestedAt(nested, t)) continue;     // memberEnums' and nestedByPath's, read by staticsFromRead
        else if (k.is("struct") || k.is("class") || k.is("union")) return "replayed: a nested or elaborated class";
        else if (k.is("friend")) return "replayed: a friend";
        else if (k.is("template"))     // a member function template: its parameter list passed, its body at depth 2
            for (int angle = 0; t + 1 < p_.tokens_.size(); ) {
                const Token &a = p_.tokens_[++t];
                angle += a.is("<") ? 1 : a.is(">") ? -1 : a.is(">>") ? -2 : 0;
                if (angle <= 0) break;
            }
    }     // a static_assert is classShape's: kept when its condition folds true under the values
    return std::string();
}

// Slice 3m: each `enum { a = 15, b = is_same<T, int>::value };` (3r: or `enum E { ... };`) in the class's braces, by its `enum`, which
// staticsFromRead reads as the replay does; `*other` where any other member enumeration is written (3n: a template named too).
std::vector<std::size_t> Templates::memberEnums(std::size_t start, bool *other) const {
    std::vector<std::size_t> at;
    *other = false;
    std::size_t t = start;
    while (t < p_.tokens_.size() && !p_.tokens_[t].is("{") && !p_.tokens_[t].is(";")) t++;
    for (int depth = 0; t + 2 < p_.tokens_.size(); t++) {
        const Token &k = p_.tokens_[t], &was = p_.tokens_[t - 1];
        if (k.is("{") || k.is("(") || k.is("[")) depth++;
        else if ((k.is("}") || k.is(")") || k.is("]")) && --depth == 0) break;
        if (depth != 1 || !k.is("enum")) continue;
        const bool named = p_.tokens_[t + 1].kind == TokenKind::Ident && p_.tokens_[t + 2].is("{");     // slice 3r: `enum Kind { ... };`
        std::size_t close = t + (named ? 3 : 2);
        while (close + 1 < p_.tokens_.size() && !p_.tokens_[close].is("}") && !p_.tokens_[close].is("{") && !p_.tokens_[close].is(";"))
            close++;
        if ((was.is(";") || was.is("{") || was.is("}") || was.is(":")) && (named || p_.tokens_[t + 1].is("{")) && p_.tokens_[close].is("}") &&
            p_.tokens_[close + 1].is(";"))
            at.push_back(t);
        else *other = true;
    }
    return at;
}

// Slice 3v: each `struct N { ... };` staticsFromRead reads by the class body's path, as the replay reads it, by its key and `}`: in a struct
// with no access label before it and no member template after it, N named nowhere else, no template in it (3ac: its members defined
// out of line, its static data members and enumerations are the same path's).
std::vector<std::pair<std::size_t, std::size_t> > Templates::nestedByPath(std::size_t start) const {
    std::vector<std::pair<std::size_t, std::size_t> > at;
    std::size_t open = start, end = start;
    while (open < p_.tokens_.size() && !p_.tokens_[open].is("{") && !p_.tokens_[open].is(";")) open++;
    if (!p_.tokens_[start].is("struct") || open >= p_.tokens_.size() || !p_.tokens_[open].is("{")) return at;
    for (int depth = 0; end < p_.tokens_.size(); end++)
        if ((depth += p_.tokens_[end].is("{") ? 1 : p_.tokens_[end].is("}") ? -1 : 0) == 0 && end > open) break;
    bool label = false;
    for (std::size_t t = open + 1; t < end; t++) {
        const Token &k = p_.tokens_[t];
        if (k.is("{") || k.is("(") || k.is("[")) {     // a member function's body or an initialiser: not this level
            for (int depth = 0; t < end; t++)
                if ((depth += p_.tokens_[t].is("{") || p_.tokens_[t].is("(") || p_.tokens_[t].is("[") ? 1 :
                              p_.tokens_[t].is("}") || p_.tokens_[t].is(")") || p_.tokens_[t].is("]") ? -1 : 0) == 0) break;
            continue;
        }
        label = label || k.is("public") || k.is("private") || k.is("protected");
        if (!(k.is("struct") || k.is("class")) || label || p_.tokens_[t + 1].kind != TokenKind::Ident || !p_.tokens_[t + 2].is("{")) continue;
        std::size_t close = t + 2;
        for (int inner = 0; close < end; close++)
            if ((inner += p_.tokens_[close].is("{") ? 1 : p_.tokens_[close].is("}") ? -1 : 0) == 0) break;
        bool plain = close + 1 < end && p_.tokens_[close + 1].is(";");
        for (std::size_t i = t + 3; i < close && plain; i++) {
            const Token &n = p_.tokens_[i];
            plain = !(n.is("template") || n.is("friend") || n.is("virtual") || n.is("union") || n.is("using") || n.is("default") ||
                      n.is("delete") || n.is("constexpr") ||
                      ((n.is("struct") || n.is("class")) && !(p_.tokens_[i + 1].kind == TokenKind::Ident && p_.tokens_[i + 2].is("{"))));
        }
        for (std::size_t i = open + 1; i < end && plain; i++) plain = (i > t && i <= close) || p_.tokens_[i].text != p_.tokens_[t + 1].text;
        for (std::size_t i = close + 2; i < end && plain; i++) plain = !p_.tokens_[i].is("template");     // 3ac: a function after it, held in place
        if (plain) at.push_back(std::make_pair(t, close));
        t = close;
    }
    return at;
}

// Whether token `t` is the key of one of nestedByPath's classes, or (within) inside one's braces.
static bool nestedAt(const std::vector<std::pair<std::size_t, std::size_t> > &nested, std::size_t t, bool within) {
    for (const std::pair<std::size_t, std::size_t> &n : nested)
        if (within ? t > n.first && t < n.second : t == n.first) return true;
    return false;
}

// Step (1)'s door, asked before any replay: the shape, nothing beyond data members and member functions, the read's records kept.
std::string Templates::classBefore(std::size_t start, const std::vector<TemplateParam> &params, const std::vector<const Type *> &binding,
                                   Substituted *early, std::vector<const Type *> *wanted) const {
    Substituted &s = *early;
    // The body's member beyond first: under one, the ids idsBefore would make are not made, so a signature's refusal hides it.
    const std::string shape = classShape(start, params, binding, &s, nullptr, wanted), outside = classBeyond(start), beyond = outside.empty() ? shape : outside;
    if (!beyond.empty()) return beyond;
    const std::map<std::size_t, std::vector<std::pair<Parser::PendingBody, std::string> > >::const_iterator r = readBodies_.find(start);
    if (r == readBodies_.end() || readTypedefs_.count(start) == 0) return "replayed: no held member bodies kept from the read";
    const std::string carried = typedefsCarried(start, s, wanted);
    if (!carried.empty()) return carried;
    for (const Signature &f : readMembers_.at(start).second)
        if (!f.implicit || !r->second.empty()) return "made from the read: data members, member functions and held bodies";
    return "made from the read: data members and implicit special members alone";
}

// Step (2): each of the read's member typedefs and aliases names, under `s`, a type the binding carries; else why not.
std::string Templates::typedefsCarried(std::size_t start, const Substituted &s, std::vector<const Type *> *wanted) const {
    for (const std::pair<std::string, const Type *> &td : readTypedefs_.at(start))
        if (!typedefCarried(start, s, td.second, wanted) && typedefByPath(start, s, td) == 0)     // else read by staticsFromRead
            return "replayed: a member typedef or alias over a type the binding does not carry";
    return std::string();
}

// One read typedef's type under `s`: one the binding carries, or one whose class classShape wanted made first.
bool Templates::typedefCarried(std::size_t start, const Substituted &s, const Type *read, const std::vector<const Type *> *wanted) const {
    const Type *e = substitute(s, read).type;
    while (e->isPointer() || e->isReference() || e->isArray()) e = e->isReference() ? e->referent() : e->pointee();
    e = e->unqualified();
    const Type *of = e->kind() == Kind::DependentMember ? e->pointee()->unqualified() : e;
    if (wanted != nullptr && std::find(wanted->begin(), wanted->end(), of) != wanted->end()) return true;
    return e == readClass_.at(start) || !(p_.dependentType(e) || readIds_.count(e) != 0 || (e->isStructOrUnion() && overParameters(e->tag())));
}

// Slice 3u: a typedef staticsFromRead reads by the class body's path, as the replay reads it: ownTypedefAt's, or, in a class of no
// written member function, one `s`'s binding alone does not carry written `typedef ... n;` with one declarator; else 0.
std::size_t Templates::typedefByPath(std::size_t start, const Substituted &s, const std::pair<std::string, const Type *> &td) const {
    const std::size_t own = ownTypedefAt(start, td.first);
    Substituted bare;     // the same answer at the door, in staticsFromRead and in the skip, whatever ids each one's `s` maps
    bare.values = s.values, bare.packs = s.packs, bare.templates = s.templates;
    const std::map<std::size_t, std::pair<std::string, std::vector<Signature> > >::const_iterator m = readMembers_.find(start);
    if (own != 0 || typedefCarried(start, bare, td.second, nullptr) || m == readMembers_.end() || !templatesOf(m->second.first).empty()) return own;
    for (const Signature &f : m->second.second)
        if (!f.implicit) return 0;
    for (std::size_t t = start, braces = 0; t + 1 < p_.tokens_.size(); t++) {
        braces += p_.tokens_[t].is("{");
        if (p_.tokens_[t].is("}") && --braces == 0) break;
        if (braces != 1 || !p_.tokens_[t].is("typedef")) continue;
        std::size_t e = t + 1;
        bool one = true;
        for (int angles = 0; e < p_.tokens_.size() && !p_.tokens_[e].is(";"); e++) {
            const Token &k = p_.tokens_[e];
            angles += k.is("<") ? 1 : k.is(">") ? -1 : k.is(">>") ? -2 : 0;
            one = one && !k.is("(") && !k.is("{") && !(angles == 0 && k.is(","));
        }
        if (one && e < p_.tokens_.size() && p_.tokens_[e - 1].text == td.first) return t;
    }
    return 0;
}

// Step 4's shape: a covered class made from the read lacks what its body declares beyond data members and member functions,
// and its held member bodies are the read's records under the real tag, each one's function the replay's by readSymbol_.
std::string Templates::classSkip(std::size_t start, const std::string &tag, const std::vector<Parser::PendingBody> &bodies,
                                 const std::string &name, std::size_t typedefsFrom) const {
    const std::string beyond = classBeyond(start);
    if (!beyond.empty()) return beyond;
    const std::map<std::size_t, std::vector<std::pair<Parser::PendingBody, std::string> > >::const_iterator r = readBodies_.find(start);
    const std::map<std::size_t, std::pair<std::string, std::vector<Signature> > >::const_iterator m = readMembers_.find(start);
    if (r == readBodies_.end() || m == readMembers_.end() || readTypedefs_.count(start) == 0) return "differ: no held member bodies kept from the read";
    std::vector<std::pair<Parser::PendingBody, std::string> > reads;     // a nested class's, read by its path, are the replay's (3v)
    std::vector<Parser::PendingBody> replays;
    for (const std::pair<Parser::PendingBody, std::string> &h : r->second) if (!nestedAt(nestedByPath(start), h.first.start, true)) reads.push_back(h);
    for (const Parser::PendingBody &b : bodies) if (!nestedAt(nestedByPath(start), b.start, true)) replays.push_back(b);
    if (reads.size() != replays.size()) return "differ: the number of held member bodies";
    for (std::size_t i = 0; i < replays.size(); i++) {
        const Parser::PendingBody &a = reads[i].first, &b = replays[i];
        const std::string key = bodyKey(start, reads[i], tag, coveredClass_.at(tag)), owner = retagged(a.tag, m->second.first, tag);
        if (a.start != b.start || a.freeFunction != b.freeFunction || key != b.key || owner != b.tag || a.local != b.local)
            return "differ: a held member body's place, key, class or name";
        const std::map<std::string, std::string>::const_iterator f = readSymbol_.find(reads[i].second);
        if (b.which >= p_.lookup_.functions_.size() || f == readSymbol_.end() || f->second != p_.lookup_.functions_[b.which].symbol)
            return "differ: a held member body's function";
    }
    // Step (2): each member typedef or alias the read's under the binding, as the replay declared it under the real tag.
    const std::string carried = typedefsCarried(start, coveredClass_.at(tag));
    if (!carried.empty()) return carried;
    const std::vector<std::pair<std::string, const Type *> > &mine = readTypedefs_.at(start), theirs = memberTypedefs(tag, name, typedefsFrom);
    if (mine.size() != theirs.size()) return "differ: the number of member typedefs and aliases";
    for (std::size_t i = 0; i < mine.size(); i++)
        if (mine[i].first != theirs[i].first || (typedefByPath(start, coveredClass_.at(tag), mine[i]) == 0 && substitute(coveredClass_.at(tag), mine[i].second).type != theirs[i].second))
            return "differ: a member typedef's or alias's name or type";
    return mine.empty() ? "made from the read: data members, member functions and held bodies alone"
                        : "made from the read: member typedefs or aliases beside them";
}

// The member typedefs and aliases declared under `owner` since `from`, in order: not its injected name, not a nested class.
std::vector<std::pair<std::string, const Type *> > Templates::memberTypedefs(const std::string &owner, const std::string &injected, std::size_t from) const {
    std::vector<std::pair<std::string, const Type *> > out;
    for (std::size_t i = from; i < p_.lookup_.typedefs_.size(); i++) {
        const std::string &key = p_.lookup_.typedefs_[i].name;
        if (key.compare(0, owner.size() + 2, owner + "::") != 0) continue;
        const std::string own = key.substr(owner.size() + 2);
        const Type *t = p_.lookup_.typedefs_[i].type;
        if (own == injected || own.find("::") != std::string::npos || ((t->isStructOrUnion() || t->isEnumeration()) && t->tag() == key)) continue;
        out.push_back(std::make_pair(own, t));
    }
    return out;
}

// Why a class's member bodies replay: its door's reason, or that no door under the binding made it.
std::string Templates::classWhy(const std::string &tag) const {
    const std::map<std::string, std::string>::const_iterator w = classWhy_.find(tag);
    return w == classWhy_.end() ? "not made at the class door" : w->second;
}

// The first n functions in the order the program would have declared them: a seated one just before the first declared after its ask.
std::vector<std::size_t> Templates::seatedOrder(std::size_t n) const {
    std::vector<std::pair<std::pair<std::size_t, int>, std::size_t> > keyed;
    for (std::size_t i = 0; i < n; i++) {
        const std::map<std::size_t, std::size_t>::const_iterator s = fnSeat_.find(i);
        keyed.push_back(std::make_pair(s == fnSeat_.end() ? std::make_pair(i, 1) : std::make_pair(s->second, 0), i));
    }
    std::sort(keyed.begin(), keyed.end());
    std::vector<std::size_t> order;
    for (std::size_t i = 0; i < keyed.size(); i++) order.push_back(keyed[i].second);
    return order;
}

// Made while a held class is: entered in the log, and seated by its key with what its own making logged before it.
// Step 2's census: a held member body of a covered class, the function path's shape test under the class's binding; `this` and
// the class's own members wait for the made class, so a body reaching them replays by name.
std::string Templates::memberShape(const PendingBody &body, const Substituted &s, Bound *made) {
    const std::map<std::size_t, syntax::NodeId>::const_iterator read = readBody_.find(body.start);
    if (read == readBody_.end()) return "replayed: no body read at the declaration";
    if (body.freeFunction) return "replayed: a friend defined in its class";
    if (body.which >= p_.lookup_.functions_.size()) return "replayed: no overload of its own to key the binding by";
    for (std::vector<syntax::NodeId> work(1, read->second); !work.empty(); ) {
        const syntax::NodeId id = work.back();
        work.pop_back();
        const syntax::Node n = p_.syntaxTree_.node(id);
        for (std::uint32_t c = 0; c < n.count; c++) work.push_back(p_.syntaxTree_.child(id, c));
        const std::unordered_map<std::uint32_t, Parser::Resolved>::const_iterator r = p_.resolutions_.find(id.v);
        if (r != p_.resolutions_.end() && r->second.what == Parser::Resolved::StaticMember) return "replayed: a static member of its own class";
        if (r != p_.resolutions_.end() && (overRead(s, r->second.owner) || overRead(s, r->second.type))) return "replayed: a member of a class the read named over its parameter";
        if (n.kind == syntax::Kind::Conditional) return "replayed: a 'conditional', whose type the read may hold apart from its replay";
        if ((n.marks & syntax::ValueDependent) != 0) return "replayed: a non-type parameter's value, which the read held as its stand-in";
    }
    const std::vector<FrameEvent> &steps = readFrames_[body.start];
    for (std::size_t k = 0; k < steps.size(); k++)
        if (steps[k].kind == 'd' && overRead(s, steps[k].type)) return "replayed: a local of a class the read named over its parameter";
    made->read = read->second;
    made->s = s;
    std::vector<syntax::NodeId> requests;
    const std::string shape = substShape(read->second, s, &made->calls, &made->members, &made->assigns, &requests);
    if (!shape.empty()) return "replayed: " + shape;
    if (!requests.empty()) return "replayed: a function template's call, which the class's binding would ask as the outer layer";
    return "covered: typed under the class's binding beside the replay";
}

// The read class standing for `made`, last in `s` (offsetUnder); before it the class the program declared under the read's own tag
// (`inner<$T0>`, made inside `outer`'s read before `inner<int>`'s), which the read's records may name in its place - a partial's
// own key, never the primary's id over its parameters (`pair<$T0,$T1>` in `pair<const T1, T2>` is another class).
void Templates::readStands(std::size_t start, const Type *made, Substituted *s) const {
    const std::map<std::size_t, std::pair<std::string, std::vector<Signature> > >::const_iterator k = readMembers_.find(start);
    const std::map<std::size_t, std::string>::const_iterator own = readOwn_.find(start);
    const Type *apart = own == readOwn_.end() || own->second.empty() ? nullptr : p_.lookup_.findTypedef(own->second);
    if (apart != nullptr && apart != readClass_.at(start) && apart->isStructOrUnion()) s->from.push_back(apart), s->to.push_back(made);
    s->from.push_back(readClass_.at(start));
    s->to.push_back(made);
    bool apartEnum = false;     // slice 3r: each named member enumeration the made class's, once staticsFromRead declared it
    for (const std::size_t e : memberEnums(start, &apartEnum)) {
        const Type *mine = nullptr;     // the read's, by a data member of it or (3aa) a member function's return or parameter
        const std::string end = "::" + p_.tokens_[e + 1].text;
        std::vector<const Type *> carried;
        for (const Member &m : readClass_.at(start)->members()) carried.push_back(m.type);
        for (std::size_t f = 0; k != readMembers_.end() && f < k->second.second.size(); f++) {
            carried.push_back(k->second.second[f].returns);
            carried.insert(carried.end(), k->second.second[f].params.begin(), k->second.second[f].params.end());
        }
        for (const Type *c : carried) {
            const Type *u = c == nullptr ? nullptr : c->unqualified();
            if (u == nullptr) continue;
            if (p_.tokens_[e + 1].kind == TokenKind::Ident && u->isEnumeration() && u->tag().size() > end.size() &&
                u->tag().compare(u->tag().size() - end.size(), end.size(), end) == 0) mine = u;
        }
        const Type *theirs = made == nullptr || mine == nullptr ? nullptr : p_.lookup_.findTypedef(p_.tagKey("enum", made->tag() + "::" + p_.tokens_[e + 1].text));
        if (mine != nullptr && (made == nullptr || theirs != nullptr)) s->from.push_back(mine), s->to.push_back(theirs);
    }
}

// A specialization or a class nested in one the map does not carry (`Tree<$T0>::It`; `inner<int>` read inside `outer`'s read).
bool Templates::overRead(const Substituted &s, const Type *t) const {
    while (t != nullptr && (t->unqualified() != t || t->isPointer() || t->isReference() || t->isArray()))
        t = t->unqualified() != t ? t->unqualified() : t->isReference() ? t->referent() : t->pointee();
    return t != nullptr && t->isStructOrUnion() && substitute(s, t).type == t && t->tag().find('<') != std::string::npos;
}

const std::string &Templates::symbolUnder(const std::string &symbol) const {
    const std::map<std::string, std::string>::const_iterator m = readSymbol_.find(symbol);
    return m == readSymbol_.end() ? symbol : m->second;
}

// A data member of the read class, named `m`, `.m` or `->m`, at its offset in the class `s` puts in its place; else `offset`.
// Named alone, its record has no owner: its own class, the read class last in a covered class's map.
int Templates::offsetUnder(const Substituted &s, const Type *owner, const std::string &spelled, int offset) const {
    if (owner == nullptr && !s.from.empty() && s.from.back()->isStructOrUnion()) owner = s.from.back();
    const Type *to = owner != nullptr ? substitute(s, owner->unqualified()).type->unqualified() : nullptr;
    const std::string name = spelled.substr(spelled.compare(0, 2, "->") == 0 ? 2 : spelled.compare(0, 1, ".") == 0 ? 1 : 0);
    if (to == nullptr || to == owner->unqualified() || !to->isStructOrUnion() || to->findMember(name) == nullptr) return offset;
    return to->findMember(name)->offset;
}

void Templates::noteMade(const std::string &key, std::size_t from, std::size_t fnFrom) {
    if (!making_) return;
    const std::size_t fnTo = p_.lookup_.functions_.size();
    seatLog_.push_back(Seat{ specializations_.size() - 1, std::string(), fnFrom > fnTo ? fnTo : fnFrom, fnTo });
    seat_[key].assign(seatLog_.begin() + static_cast<long>(from), seatLog_.end());
}

// The program's ask for what a held class's making made: seated last, as its replay would have made it there.
void Templates::asked(const std::string &key, std::size_t pos) {
    if (p_.patternUses_) return;
    const std::map<std::string, std::vector<Seat> >::iterator seated = seat_.find(key);
    if (seated == seat_.end()) return;
    if (making_) { seatLog_.push_back(Seat{ 0, key, 0, 0 }); return; }
    const std::size_t at = p_.lookup_.functions_.size();
    std::vector<Seat> log;
    log.swap(seated->second);
    seat_.erase(seated);
    for (std::size_t i = 0; i < log.size(); i++) {
        if (!log[i].key.empty()) { asked(log[i].key, pos); continue; }
        for (std::size_t f = log[i].fnFrom; f < log[i].fnTo; f++) fnSeat_.insert(std::make_pair(f, at));
        if (specializations_[log[i].index].emitted) continue;
        Specialization sp = specializations_[log[i].index];
        specializations_[log[i].index].emitted = true;
        specializations_[log[i].index].name.clear();
        specializations_[log[i].index].key.clear();
        specializations_[log[i].index].bodies.clear();
        seat_.erase(sp.key);
        sp.pos = pos;
        sp.depth = p_.instantiationDepth_ + 1;
        specializations_.push_back(sp);
    }
}

// Each class the read just ended needed the members of, made here; true if one was completed, and the read is worth making again.
bool Templates::makeWanted() {
    std::vector<const Type *> want;
    want.swap(wanted_);
    // Inside a class still being defined the class may name it, incomplete: left held, refused by name as before.
    if (!p_.classStack_.empty() || p_.currentClass_ != nullptr) want.clear();
    bool made = false;
    const bool was = making_;
    for (std::size_t i = 0; i < want.size(); i++) {
        const std::map<const Type *, Held>::iterator h = held_.find(want[i]);
        if (h == held_.end() || want[i]->isComplete()) continue;
        const Held it = h->second;
        held_.erase(h);
        const std::string tag = specializationKey(it.decl.name, it.args);
        if (p_.lookup_.findTypedef(tag) != nullptr) continue;
        making_ = true;
        makeClass(it.decl, tag, it.binding, it.values, it.args, it.packs, it.pos);
        making_ = was;
        made = made || want[i]->isComplete();
    }
    held_.clear();
    if (!making_) seatLog_.clear();
    return made;
}

// One member template's body replayed as the specialization `display` of `key`, its parameters already bound.
void Templates::replayMemberTemplate(const TemplateDecl &mt, const std::string &display, const std::string &key,
                                     const std::vector<TemplateArg> &args, const Type *patternFn, bool declOnly) {
    const bool wasInst = p_.memberTemplateInst_;
    const std::string wasOf = p_.memberTemplateOf_;
    const std::string wasName = p_.memberTemplateName_;
    const std::vector<TemplateArg> wasArgs = p_.memberTemplateArgs_;
    const Type *wasPattern = p_.memberTemplatePattern_;
    p_.memberTemplateInst_ = true;
    p_.memberTemplateOf_ = mt.name;
    p_.memberTemplateName_ = display;
    p_.memberTemplateArgs_ = args;
    p_.memberTemplateAccess_ = mt.memberAccess;
    p_.memberTemplatePattern_ = patternFn;

    std::vector<Parser::PendingBody> one(1);
    one[0].tag = mt.ownerTag;
    one[0].start = mt.afterParams;
    one[0].local = mt.ownerLocal.empty() ? mt.ownerTag : mt.ownerLocal;
    one[0].key = key;
    one[0].which = Parser::PendingBody::npos();
    // Defined at namespace scope: replayed as written there, `C::f` read in the namespace it was written in.
    if (mt.outOfLineBody) { one[0].freeFunction = true; one[0].tag.clear(); }
    struct Restore {
        Parser *p; bool inst; std::string of, name; std::vector<TemplateArg> args; const Type *pattern;
        bool statik, declOnly; std::vector<std::string> ns;
        ~Restore() {
            p->memberTemplateInst_ = inst; p->memberTemplateOf_ = of; p->memberTemplateName_ = name;
            p->memberTemplateArgs_ = args; p->memberTemplatePattern_ = pattern;
            p->memberTemplateStatic_ = statik; p->memberTemplateDeclOnly_ = declOnly; p->namespaceStack_ = ns;
        }
    } restore{ &p_, wasInst, wasOf, wasName, wasArgs, wasPattern, p_.memberTemplateStatic_,
               p_.memberTemplateDeclOnly_, p_.namespaceStack_ };
    p_.memberTemplateStatic_ = mt.staticMember;
    p_.memberTemplateDeclOnly_ = declOnly;
    if (mt.outOfLineBody) p_.namespaceStack_ = p_.namespacePathStack(mt.ns);
    p_.replayInlineBodies(one);
}

// The specialization these arguments ask for, made if it is new.
const Parser::Signature *Templates::instantiateMemberTemplate(
    const TemplateDecl &mt, const std::vector<const Type *> &binding,
    const std::vector<long long> &values,
    const std::vector<TemplateArg> &args, std::size_t pos) {
    const DoorAt memberTemplateDoor(this, "member template");
    // head<3>; a constructor's is spelled from the class's name, CMatrix<3>, and keyed apart from it.
    const std::string display =
        specializationKey(mt.isConstructor ? mt.ownerLocal : mt.name, args);
    const std::string key = mt.ownerTag + "::" + display;
    // Two templates of one name may each be instantiated at K = 3: the cache is per template.
    const std::string made = key + "@" + std::to_string(mt.declaredAt != 0 ? mt.declaredAt : mt.afterParams);
    if (memberTemplateMade_.count(made) != 0) return &p_.lookup_.functions_[memberTemplateMade_[made]];
    const std::size_t before = p_.lookup_.functions_.size();
    const Parser::ReplayUses replayed(&p_);
    for (std::size_t i = 0; i < binding.size(); i++)     // over a pattern's parameter, it is the pattern's still
        if (binding[i] != nullptr && p_.dependentType(binding[i])) p_.patternUses_ = replayed.was;
    // Its body is replayed into the unit at once, which no layer takes back.
    if (replayed.was && !p_.patternUses_) throw SubstitutionFailure("a member template specialization the program has not asked for yet");
    if (!mt.defined && mt.declaredAt == 0)
        p_.src_.fail(pos, "'" + mt.ownerTag + "::" + mt.name + "' is declared but "
                       "not defined, so there is nothing to instantiate");
    // [temp.point]: a call before the definition declares the specialization; the definition replays its body.
    const bool declOnly = !mt.defined;
    if (declOnly)
        owedBodies_[mt.ownerTag + "::" + mt.name + "@" + std::to_string(mt.declaredAt)].push_back(
            Owed{ binding, values, args, pos });

    Parser::BoundParams bound(&p_);
    std::vector<Shadow> &undo = bound.undo;
    // The class's parameters first (N), then the member's own (M) - two layers,
    // the whole reason a member template records the class's binding.
    if (!mt.classParams.empty())
        bindTemplateParameters(mt.classParams, mt.classBinding, mt.classValues, mt.classPacks, &undo);
    // A member template's own pack is carried in its argument list.
    Packs packs(mt.params.size());
    for (std::size_t i = 0; i < args.size() && i < packs.size(); i++)
        if (args[i].isPack) { packs[i] = args[i].pack; packs[i].values = args[i].packValues; }
    bindTemplateParameters(mt.params, binding, values, packs, &undo);

    // **The pattern the Itanium name is spelled from** is the declaration's read (ParserType's, or the
    // out-of-line definition's): the member's own parameters as themselves, the class's bound - not read again.
    const Type *patternFn = mt.pattern;
    if (patternFn == nullptr) p_.src_.fail(pos, "'" + mt.ownerTag + "::" + mt.name + "' has no pattern signature kept at its declaration");

    bool ownPack = false;
    for (std::size_t i = 0; i < mt.params.size(); i++) ownPack = ownPack || mt.params[i].isPack;
    countWhy(MemberTemplate, mt.isConstructor ? "a constructor" : ownPack ? "an own pack"
                             : memberTemplateRead_.count(mt.afterParams) == 0 ? "a body the read did not read"
                             : mt.classParams.empty() ? "one layer, a body read" : "two layers, a body read");
    const std::size_t bodiesFrom = p_.syntaxTree_.size();
    replayMemberTemplate(mt, display, key, args, patternFn, declOnly);
    holdBodies(bodiesFrom);
    bound.unbindNow();

    // The function this replay declared under the key - the first one past `before`.
    const std::vector<std::size_t> *had = p_.lookup_.overloadsOf(key);
    if (had == nullptr) return nullptr;
    for (std::size_t i = 0; i < had->size(); i++)
        if ((*had)[i] >= before) {
            memberTemplateMade_[made] = (*had)[i];
            if (mt.isExplicit) p_.lookup_.functions_[(*had)[i]].isExplicit = true;
            return &p_.lookup_.functions_[(*had)[i]];
        }
    return &p_.lookup_.functions_[(*had)[0]];
}

// The specializations a call declared before this definition: each body replayed now, under the name it has.
void Templates::definedOutOfLine(const TemplateDecl &mt) {
    const std::map<std::string, std::vector<Owed> >::iterator o =
        owedBodies_.find(mt.ownerTag + "::" + mt.name + "@" + std::to_string(mt.declaredAt));
    if (o == owedBodies_.end()) return;
    const std::vector<Owed> owed = o->second;
    owedBodies_.erase(o);
    const std::size_t resume = p_.at_;
    for (const Owed &w : owed) {
        const std::string made = mt.ownerTag + "::" +
            specializationKey(mt.isConstructor ? mt.ownerLocal : mt.name, w.args) + "@" + std::to_string(mt.declaredAt);
        const std::map<std::string, std::size_t>::iterator had = memberTemplateMade_.find(made);
        const bool cached = had != memberTemplateMade_.end();
        const std::size_t was = cached ? had->second : 0;
        if (cached) memberTemplateMade_.erase(had);
        instantiateMemberTemplate(mt, w.binding, w.values, w.args, w.pos);
        if (cached) memberTemplateMade_[made] = was;
    }
    p_.at_ = resume;
}

// **Function templates as overload-resolution candidates.**
void Templates::instantiateViableTemplates(const std::string &name,
                                        const std::vector<const Type *> &argTypes,
                                        std::size_t pos) {
    std::map<std::string, std::vector<TemplateDecl> >::const_iterator it =
        fnTemplates_.find(name);
    if (it == fnTemplates_.end()) return;
    const std::vector<TemplateDecl> overloads = it->second;
    instantiateViableTemplates(overloads, argTypes, pos);
}

std::vector<TemplateDecl> Templates::templatesNamed(const std::string &name, const std::string &scope,
                                                         std::string *ns) const {
    const std::string::size_type cut = name.rfind("::");
    const std::string q = cut == std::string::npos ? std::string() : name.substr(0, cut + 2);
    const std::string bare = cut == std::string::npos ? name : name.substr(cut + 2);
    ns->clear();
    std::map<std::string, std::vector<TemplateDecl> >::const_iterator it = fnTemplates_.find(bare);
    if (it == fnTemplates_.end()) return std::vector<TemplateDecl>();
    std::string here = scope.empty() ? p_.namespacePrefix() : scope + "::";
    if (!scope.empty()) {
        std::vector<TemplateDecl> got;
        for (const TemplateDecl &d : it->second)
            if (d.ns == here) got.push_back(d);
        if (!got.empty()) *ns = here;
        return got.empty() ? it->second : got;
    }
    for (;;) {
        std::vector<TemplateDecl> got;
        for (const TemplateDecl &d : it->second)
            if (d.ns == here + q) got.push_back(d);
        if (!got.empty()) { *ns = here + q == "" ? "::" : here + q; return got; }
        if (here.empty()) break;
        const std::string::size_type up = here.rfind("::", here.size() - 3);
        here = up == std::string::npos ? std::string() : here.substr(0, up + 2);
    }
    return it->second;
}

// [basic.lookup.unqual]/1 for a call: an ordinary function in an enclosing namespace hides the templates of
// a namespace no enclosing one is - `copy(r)` in `mir` is `mir::copy`, not `std::copy` (M1, MirWebs.cpp).
bool Templates::functionHidesTemplates(const std::string &name) const {
    std::string ns;
    templatesNamed(name, std::string(), &ns);
    if (!ns.empty()) return false;
    const std::string key = p_.lookup_.qualifyForLookup(name, &sema::Lookup::hasFunctionNamed);
    if (key == name) return false;
    if (const std::vector<std::size_t> *set = p_.lookup_.overloadsOf(key))
        for (std::size_t i : *set)
            if (!p_.lookup_.functions_[i].fromTemplate) return true;
    return false;
}

void Templates::instantiateViableTemplates(const std::vector<TemplateDecl> &overloads,
                                        const std::vector<const Type *> &argTypes, std::size_t pos) {
    for (std::size_t k = 0; k < overloads.size(); k++) {
        const TemplateDecl &decl = overloads[k];
        const DoorAt trialDoor(this, "SFINAE trial");
        std::vector<const Type *> binding;
        std::vector<long long> values;
        Packs packs;
        std::string why;
        const long defaultsBefore = deducedDefaults_;
        if (!deduceTemplateArguments(decl, argTypes, &binding, &values, &packs,
                                     &why))
            continue;
        const std::vector<TemplateArg> args = argumentsFromBinding(decl.params, binding, values, packs);
        countWhy(SfinaeTrial, "a function template, " + trialShape(decl, deducedDefaults_ != defaultsBefore));
        try {
            Parser::Trial trial(&p_);
            p_.instantiate(decl, binding, values, args, pos, packs);
        } catch (const SubstitutionFailure &f) {
            count(SfinaeFailed);
            // This template does not apply; the others still might - unless
            // what stopped it was a feature this compiler has not built, which
            // is not [temp.deduct]/8's business and must not be swallowed.
            if (f.unsupported) p_.src_.fail(f.pos, f.why);
        }
    }
}

// **A member function template as an overload-resolution candidate.**
void Templates::instantiateViableMemberTemplates(
    const Type *cls, const std::string &name,
    const std::vector<const Type *> &argTypes, std::size_t pos) {
    if (cls == nullptr || cls->tag().empty()) return;
    std::map<std::string, std::vector<TemplateDecl> >::const_iterator it =
        memberTemplates_.find(cls->tag() + "::" + name);
    if (it == memberTemplates_.end()) return;
    const std::vector<TemplateDecl> all = it->second;
    for (std::size_t t = 0; t < all.size(); t++) instantiateViableMemberTemplate(all[t], argTypes, pos);
}

void Templates::instantiateViableMemberTemplatesUnder(
    const std::string &key, const std::vector<const Type *> &argTypes, std::size_t pos) {
    std::map<std::string, std::vector<TemplateDecl> >::const_iterator it =
        memberTemplates_.find(key);
    if (it == memberTemplates_.end()) return;
    const std::vector<TemplateDecl> all = it->second;
    for (std::size_t t = 0; t < all.size(); t++) instantiateViableMemberTemplate(all[t], argTypes, pos);
}

void Templates::instantiateConstructorTemplates(const Type *cls,
                                             const std::vector<const Type *> &argTypes,
                                             std::size_t pos) {
    if (cls == nullptr || !cls->isStructOrUnion() || cls->tag().empty()) return;
    instantiateViableMemberTemplatesUnder(Parser::constructorKey(cls->unqualified()->tag()), argTypes, pos);
}

void Templates::instantiateConstructorTemplates(const Type *cls, const std::vector<ExprPtr> &args,
                                             std::size_t pos) {
    std::vector<const Type *> argTypes;
    for (std::size_t i = 0; i < args.size(); i++) argTypes.push_back(deductionArgType(*args[i]));
    instantiateConstructorTemplates(cls, argTypes, pos);
}

// One member template as a candidate: deduced, and instantiated where that works.
void Templates::instantiateViableMemberTemplate(const TemplateDecl &mt,
                                             const std::vector<const Type *> &argTypes,
                                             std::size_t pos) {

    std::vector<const Type *> binding;
    std::vector<long long> values;
    Packs packs;
    std::string why;
    const DoorAt trialDoor(this, "SFINAE trial");
    Parser::BoundParams bound(&p_);
    if (!mt.classParams.empty())
        bindTemplateParameters(mt.classParams, mt.classBinding, mt.classValues, mt.classPacks, &bound.undo);
    const long defaultsBefore = deducedDefaults_;
    const bool ok = deduceTemplateArguments(mt, argTypes, &binding, &values,
                                            &packs, &why);
    bound.unbindNow();
    if (!ok) return;
    const bool defaulted = deducedDefaults_ != defaultsBefore;

    const std::vector<TemplateArg> args = argumentsFromBinding(mt.params, binding, values, packs);
    const bool wasBody = p_.memberTemplateBody_;
    p_.memberTemplateBody_ = false;
    const std::size_t before = p_.lookup_.functions_.size();
    std::vector<std::size_t> uses;
    std::vector<std::size_t> *const wasUses = p_.candidateUses_;
    p_.candidateUses_ = &uses;
    countWhy(SfinaeTrial, "a member template, " + trialShape(mt, defaulted));
    try {
        Parser::Trial trial(&p_);
        const Signature *made = instantiateMemberTemplate(mt, binding, values, args, pos);
        p_.candidateUses_ = wasUses;
        if (made != nullptr) {
            const std::size_t at = static_cast<std::size_t>(made - &p_.lookup_.functions_[0]);
            p_.candidateSpecializations_.push_back(at);
            p_.lookup_.functions_[at].deferredUses.insert(p_.lookup_.functions_[at].deferredUses.end(), uses.begin(), uses.end());
            if (p_.lookup_.functions_[at].used) p_.noteUsed(at);      // already chosen elsewhere: its uses stand
        }
    } catch (const SubstitutionFailure &f) {
        p_.candidateUses_ = wasUses;
        count(SfinaeFailed);
        // This one does not apply - unless it was a refusal, which is a fact about the compiler
        // rather than about this candidate, or **the body failed**, which [temp.deduct]/8 does not
        // forgive: the candidate was formed, and dropping it now leaves a call to nothing.
        if (f.unsupported) p_.src_.fail(f.pos, f.why);
        if (p_.memberTemplateBody_) {
            const std::string display = specializationKey(mt.isConstructor ? mt.ownerLocal : mt.name, args);
            const std::string why = f.why + " - in the instantiation of '" + mt.ownerTag + "::" +
                                    display + "', asked for at " + p_.src_.where(pos);
            // **Its signature stands as a candidate**; the body's error waits for it to be chosen,
            // which pruneUnchosenCandidates asks - a pair<pair<A, B>, C> ranks pair(const pair<U1, U2> &).
            const std::vector<std::size_t> *had = p_.lookup_.overloadsOf(mt.ownerTag + "::" + display);
            std::size_t found = static_cast<std::size_t>(-1);
            for (std::size_t i = 0; had != nullptr && i < had->size(); i++)
                if ((*had)[i] >= before) { found = (*had)[i]; break; }
            if (found == static_cast<std::size_t>(-1)) p_.src_.fail(f.pos, why);
            p_.lookup_.functions_[found].bodyFailure = why;
            p_.lookup_.functions_[found].bodyFailurePos = f.pos;
            memberTemplateMade_[mt.ownerTag + "::" + display + "@" + std::to_string(mt.afterParams)] = found;
            p_.candidateSpecializations_.push_back(found);
        }
    }
    p_.memberTemplateBody_ = wasBody;
}

}   // namespace sema
