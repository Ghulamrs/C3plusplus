// sema::Templates - see Templates.h.

#include "Templates.h"
#include "../parser/Parser.h"
#include "../parser/ParserInternal.h"
#include "../Source.h"

#include <algorithm>

namespace sema {

// A type as a specialization's key spells it: describe() writes an enumeration as the int it is
// held in, and `Box<Rank>` would be `Box<int>` - one class for two (M1, `std::vector<Rank>`).
static std::string keySpelling(const Type *t) {
    if (t->unqualified() != t)
        return t->isPointer() ? keySpelling(t->unqualified()) + " const" : "const " + keySpelling(t->unqualified());
    if (t->isEnumeration()) return "enum " + t->enumTag();
    if (t->isPointer() && !t->pointee()->isFunction()) return keySpelling(t->pointee()) + " *";
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
            auto had = p_.templates_.find(p.name);
            if (had != p_.templates_.end()) { s.hadTemplate = true; s.hadDecl = had->second; }
            const Type *b = i < binding.size() ? binding[i] : nullptr;
            if (b != nullptr && b->kind() == Kind::TemplateParam) {
                TemplateDecl stand;
                stand.name = "$TT" + std::to_string(b->length());
                stand.params = p.inner;
                stand.isClass = true;
                stand.ttParam = static_cast<int>(b->length());
                p_.templates_[p.name] = stand;
            } else if (b != nullptr && b->isTemplateName() && p_.findTemplate(b->templateName()) != p_.templates_.end()) {
                p_.templates_[p.name] = p_.findTemplate(b->templateName())->second;
            } else {
                p_.src_.fail(p.pos, "'" + p.name + "' is a template template parameter and this binds "
                                 "it to no template");
            }
            undo->push_back(s);
            continue;
        }
        if (s.isType) {
            auto it = p_.typedefIndex_.find(p.name);
            if (it != p_.typedefIndex_.end()) { s.had = true; s.was = it->second; }
            p_.typedefIndex_[p.name] = p_.typedefs_.size();
            p_.typedefs_.push_back(TypedefName{ p.name, binding[i] });
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
            auto en = p_.enumIndex_.find(p.name);
            if (en != p_.enumIndex_.end()) { ev.had = true; ev.was = en->second; }
            p_.enumIndex_[p.name] = p_.enums_.size();
            p_.enums_.push_back(EnumConst{ p.name, values[i] });
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
            auto it = p_.enumIndex_.find(p.name);
            if (it != p_.enumIndex_.end()) { s.had = true; s.was = it->second; }
            p_.enumIndex_[p.name] = p_.enums_.size();
            EnumConst e{ p.name, values[i] };
            // [temp.param]/5: a non-type parameter is a prvalue of its declared type - an enumeration,
            // `unsigned`, `unsigned long long` - and not an int.
            const Type *pt = Parser::paramTypeIn(p, binding);
            if (pt != nullptr && pt->isInteger()) e.type = pt->unqualified();
            e.paramType = pt;
            p_.enums_.push_back(e);
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
            if (s.hadTemplate) p_.templates_[s.name] = s.hadDecl;
            else               p_.templates_.erase(s.name);
            continue;
        }
        if (s.isType) {
            if (s.had) p_.typedefIndex_[s.name] = s.was;
            else       p_.typedefIndex_.erase(s.name);
            if (--p_.boundTypeParams_[s.name] <= 0) p_.boundTypeParams_.erase(s.name);
        } else if (s.isParamRef) {
            if (s.hadParamRef) p_.nonTypePatternParams_[s.name] = s.paramRefWas;
            else               p_.nonTypePatternParams_.erase(s.name);
        } else {
            if (s.had) p_.enumIndex_[s.name] = s.was;
            else       p_.enumIndex_.erase(s.name);
            if (--p_.boundValueParams_[s.name] <= 0) p_.boundValueParams_.erase(s.name);
        }
    }
}

// What a template template parameter is bound to by a specialization: its template's stand-in, or
// - while two patterns are ordered against each other - the other pattern's own parameter.
const Type *Templates::templateOf(const Type *spec) const {
    int tt;
    if (ttRefName(spec->templateName(), &tt)) return p_.types_.templateParam(tt);
    return p_.templateNameTypeOf(p_.templates_.find(spec->templateName())->second);
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
        else if (args[i].type->kind() == Kind::TemplateParam)
            key += "$T" + std::to_string(args[i].type->length());
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
        if (p_.functions_[(*set)[k]].used) return true;
    return false;
}

void Templates::instantiatePending() {
    for (bool again = true; again; ) {
        again = false;
        for (std::size_t i = 0; i < p_.specializations_.size(); i++) {
            if (p_.specializations_[i].emitted) continue;
            // **Made where it was asked for, defined only where it was chosen** -
            // deduction instantiates a candidate before it can rank one, and an
            // ordinary function may win. A body skipped now may be wanted later.
            std::vector<Parser::PendingBody> now;
            std::vector<std::size_t> outsideNow;
            if (p_.specializations_[i].isClass) {
                std::vector<Parser::PendingBody> later;
                for (std::size_t b = 0; b < p_.specializations_[i].bodies.size(); b++) {
                    const Parser::PendingBody &body = p_.specializations_[i].bodies[b];
                    // **This overload, not this name.** Every constructor of
                    // a class shares one key, so asking the key replays them
                    // all as soon as any is called.
                    const bool wanted =
                        body.which != Parser::PendingBody::npos() &&
                        body.which < p_.functions_.size()
                            ? p_.functions_[body.which].used
                            : memberIsUsed(body.key);
                    (wanted ? now : later).push_back(body);
                }
                p_.specializations_[i].bodies = later;

                // **Looked up fresh, because the list can still be growing.** An
                // out-of-line definition may be written further down the file than
                // the use that asked for the class.
                const TemplateDecl &d = p_.templates_[p_.specializations_[i].name];
                std::vector<bool> &done = p_.specializations_[i].outsideDone;
                done.resize(d.outOfLine.size(), false);
                for (std::size_t k = 0; k < d.outOfLine.size(); k++) {
                    if (done[k] || p_.specializations_[i].fromPartial) continue;
                    const TemplateDecl::OutOfLine &o = d.outOfLine[k];
                    // The class the member is in: the specialization, or a class nested in it.
                    const std::string tag = p_.specializations_[i].key + (o.nested.empty() ? "" : "::" + o.nested);
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
                    p_.lookup_.overloadsOf(p_.specializations_[i].key);
                if (had == nullptr || !p_.functions_[(*had)[0]].used) continue;
                p_.specializations_[i].emitted = true;
            }
            again = true;

            // Copied, not held by reference: replaying may append to the
            // vector and move it.
            const Parser::Specialization sp = p_.specializations_[i];
            TemplateDecl decl = p_.templates_[sp.name];

            std::vector<Shadow> undo;
            bindTemplateParameters(sp.params, sp.binding, sp.values, sp.packs, &undo);
            const std::string wasKey = p_.instantiationKey_;
            const std::string wasOf = p_.instantiationOf_;
            p_.instantiationKey_ = sp.isClass ? std::string() : sp.key;
            p_.instantiationOf_ = sp.isClass ? std::string() : sp.name;
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
            if (sp.isClass) {
                // Its member functions, held here; inlineOwner_ gives the scope.
                p_.replayInlineBodies(now);
                // And the ones written outside it, which need no owner: the
                // tokens say `Box<T>::get`, so with T bound the ordinary
                // member-definition path reads the qualifier itself.
                for (std::size_t k = 0; k < outsideNow.size(); k++) {
                    p_.at_ = p_.templates_[sp.name].outOfLine[outsideNow[k]].start;
                    p_.topLevel(*p_.current_);
                }
            } else {
                p_.at_ = sp.start;
                p_.topLevel(*p_.current_);
            }
            p_.namespaceStack_ = wasStack;
            p_.replayingInline_ = wasReplayingInline;
            p_.at_ = resume;

            p_.instantiationKey_ = wasKey;
            p_.instantiationOf_ = wasOf;
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
            if (f.unsupported) p_.src_.fail(f.pos, f.why);
            *why = "the default argument of '" + decl.params[i].name + "' does not substitute: " + f.why;
            return false;
        }
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
        bindTemplateParameters(decl.params, binding, values, packs, &undo);
        p_.at_ = decl.aliasAt;
        Parser::StorageClass sc;
        Parser::Qualifiers quals;
        const Type *base = p_.specifiers(&sc, &quals);
        const Type *t = p_.declarator(base, true).type;
        p_.at_ = resume;
        return t;
    }
    const std::string tag = specializationKey(decl.name, args);

    // **A pattern's `C<T>`**, the stand-in applied: a shape deduction matches and Itanium spells `T_I...E`.
    if (decl.ttParam >= 0) {
        Type *shallow = p_.types_.structType(Kind::Struct, tag);
        if (!shallow->isSpecialization()) shallow->setSpecialization(decl.name, args);
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
        }
        // Registered so that `Box<T>::get` reads: the declarator's qualified
        // path looks the class up by name, and this is the only name it has.
        // The tag holds a `$` and so cannot collide with anything written.
        p_.lookup_.declareTypeName(tag, shallow);
        return shallow;
    }

    if (const Type *had = p_.lookup_.findTypedef(tag)) return had;

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

    const std::size_t resume = p_.at_;
    std::vector<Shadow> undo;
    // **A chosen partial binds its own pack, not the primary's.** `L<T, R...>` on
    // `L<int, char, long>` leaves R holding {char, long}, which is what the replayed
    // body has to see - the primary's Ts is not in scope at all.
    bindTemplateParameters(useParams, useBinding, useValues,
                           partial ? usePacks : packs, &undo);

    p_.at_ = partial ? decl.partials[which].bodyAt : decl.afterParams;
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
    const Type *made = partial
        ? p_.structOrUnionSpecifier(Kind::Struct, false)
        : p_.specifiers(&sc, &quals);
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
    std::vector<Parser::PendingBody> bodies;
    bodies.swap(p_.heldForSpecialization_);

    unbindTemplateParameters(undo);
    p_.at_ = resume;

    if (!made->isStructOrUnion())
        p_.src_.fail(pos, "'" + decl.name + "' is not a class template");
    p_.lookup_.declareTypeName(tag, made);

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
    p_.specializations_.push_back(sp);
    return made;
}

// The specialization these arguments ask for, made if it is new.
const Parser::Signature *Templates::instantiateMemberTemplate(
    const TemplateDecl &mt, const std::vector<const Type *> &binding,
    const std::vector<long long> &values,
    const std::vector<TemplateArg> &args, std::size_t pos) {
    // head<3>; a constructor's is spelled from the class's name, CMatrix<3>, and keyed apart from it.
    const std::string display =
        specializationKey(mt.isConstructor ? mt.ownerLocal : mt.name, args);
    const std::string key = mt.ownerTag + "::" + display;
    // Two templates of one name may each be instantiated at K = 3: the cache is per template.
    const std::string made = key + "@" + std::to_string(mt.afterParams);
    if (p_.memberTemplateMade_.count(made) != 0) return &p_.functions_[p_.memberTemplateMade_[made]];
    const std::size_t before = p_.functions_.size();
    if (!mt.defined)
        p_.src_.fail(pos, "'" + mt.ownerTag + "::" + mt.name + "' is declared but "
                       "not defined, so there is nothing to instantiate");

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

    // **The pattern the Itanium name is spelled from**: the member's own parameters as
    // references to themselves, the class's already bound to its arguments above.
    std::vector<const Type *> pattern(mt.params.size());
    for (std::size_t i = 0; i < mt.params.size(); i++)
        pattern[i] = p_.types_.templateParam(static_cast<int>(i));
    std::string patternName;
    const Type *patternFn = p_.readTemplateDeclaration(mt, pattern, values, &patternName);

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
    struct Restore {
        Parser *p; bool inst; std::string of, name; std::vector<TemplateArg> args; const Type *pattern;
        ~Restore() {
            p->memberTemplateInst_ = inst; p->memberTemplateOf_ = of; p->memberTemplateName_ = name;
            p->memberTemplateArgs_ = args; p->memberTemplatePattern_ = pattern;
        }
    } restore{ &p_, wasInst, wasOf, wasName, wasArgs, wasPattern };
    p_.replayInlineBodies(one);
    bound.unbindNow();

    // The function this replay declared under the key - the first one past `before`.
    const std::vector<std::size_t> *had = p_.lookup_.overloadsOf(key);
    if (had == nullptr) return nullptr;
    for (std::size_t i = 0; i < had->size(); i++)
        if ((*had)[i] >= before) {
            p_.memberTemplateMade_[made] = (*had)[i];
            if (mt.isExplicit) p_.functions_[(*had)[i]].isExplicit = true;
            return &p_.functions_[(*had)[i]];
        }
    return &p_.functions_[(*had)[0]];
}

// **Function templates as overload-resolution candidates.**
void Templates::instantiateViableTemplates(const std::string &name,
                                        const std::vector<const Type *> &argTypes,
                                        std::size_t pos) {
    std::map<std::string, std::vector<TemplateDecl> >::const_iterator it =
        p_.fnTemplates_.find(name);
    if (it == p_.fnTemplates_.end()) return;
    const std::vector<TemplateDecl> overloads = it->second;
    instantiateViableTemplates(overloads, argTypes, pos);
}

std::vector<TemplateDecl> Templates::templatesNamed(const std::string &name, const std::string &scope,
                                                         std::string *ns) const {
    const std::string::size_type cut = name.rfind("::");
    const std::string q = cut == std::string::npos ? std::string() : name.substr(0, cut + 2);
    const std::string bare = cut == std::string::npos ? name : name.substr(cut + 2);
    ns->clear();
    std::map<std::string, std::vector<TemplateDecl> >::const_iterator it = p_.fnTemplates_.find(bare);
    if (it == p_.fnTemplates_.end()) return std::vector<TemplateDecl>();
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
            if (!p_.functions_[i].fromTemplate) return true;
    return false;
}

void Templates::instantiateViableTemplates(const std::vector<TemplateDecl> &overloads,
                                        const std::vector<const Type *> &argTypes, std::size_t pos) {
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
            Parser::Trial trial(&p_);
            p_.instantiate(decl, binding, values, args, pos, packs);
        } catch (const SubstitutionFailure &f) {
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
        p_.memberTemplates_.find(cls->tag() + "::" + name);
    if (it == p_.memberTemplates_.end()) return;
    const std::vector<TemplateDecl> all = it->second;
    for (std::size_t t = 0; t < all.size(); t++) instantiateViableMemberTemplate(all[t], argTypes, pos);
}

void Templates::instantiateViableMemberTemplatesUnder(
    const std::string &key, const std::vector<const Type *> &argTypes, std::size_t pos) {
    std::map<std::string, std::vector<TemplateDecl> >::const_iterator it =
        p_.memberTemplates_.find(key);
    if (it == p_.memberTemplates_.end()) return;
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
    Parser::BoundParams bound(&p_);
    if (!mt.classParams.empty())
        bindTemplateParameters(mt.classParams, mt.classBinding, mt.classValues, mt.classPacks, &bound.undo);
    const bool ok = deduceTemplateArguments(mt, argTypes, &binding, &values,
                                            &packs, &why);
    bound.unbindNow();
    if (!ok) return;

    const std::vector<TemplateArg> args = argumentsFromBinding(mt.params, binding, values, packs);
    const bool wasBody = p_.memberTemplateBody_;
    p_.memberTemplateBody_ = false;
    const std::size_t before = p_.functions_.size();
    std::vector<std::size_t> uses;
    std::vector<std::size_t> *const wasUses = p_.candidateUses_;
    p_.candidateUses_ = &uses;
    try {
        Parser::Trial trial(&p_);
        const Signature *made = instantiateMemberTemplate(mt, binding, values, args, pos);
        p_.candidateUses_ = wasUses;
        if (made != nullptr) {
            const std::size_t at = static_cast<std::size_t>(made - &p_.functions_[0]);
            p_.candidateSpecializations_.push_back(at);
            p_.functions_[at].deferredUses.insert(p_.functions_[at].deferredUses.end(), uses.begin(), uses.end());
            if (p_.functions_[at].used) p_.noteUsed(at);      // already chosen elsewhere: its uses stand
        }
    } catch (const SubstitutionFailure &f) {
        p_.candidateUses_ = wasUses;
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
            p_.functions_[found].bodyFailure = why;
            p_.functions_[found].bodyFailurePos = f.pos;
            p_.memberTemplateMade_[mt.ownerTag + "::" + display + "@" + std::to_string(mt.afterParams)] = found;
            p_.candidateSpecializations_.push_back(found);
        }
    }
    p_.memberTemplateBody_ = wasBody;
}

}   // namespace sema
