#pragma once

// **sema::Templates, out of the parser in place** - M2 step 1, doc-cxx3/M2-PLAN.md. Its functions moved here
// whole; they still read the parser's state through p_, and the state follows in a later commit.

#include "Entities.h"
#include "../Ast.h"
#include "../Lexer.h"
#include "../Type.h"

#include <cstddef>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

class Parser;

namespace sema {

class Templates {
public:
    explicit Templates(Parser &p) : p_(p) {}

    // Try every function template named `name` against these argument types,
    // instantiating each that deduces - a substitution failure just drops that
    // one.
    void instantiateViableTemplates(const std::string &name,
                                    const std::vector<const Type *> &argTypes,
                                    std::size_t pos);
    void instantiateViableTemplates(const std::vector<TemplateDecl> &overloads,
                                    const std::vector<const Type *> &argTypes, std::size_t pos);
    // The function templates a call of `name` names, [basic.lookup.qual] and [basic.lookup.unqual]:
    // the nearest namespace declaring one - or the one a qualifier names - in `*ns`; else every one.
    // A `scope` is the namespace a qualifier resolved to, absolute: `m` for `m::twice(2)`.
    bool functionHidesTemplates(const std::string &name) const;
    std::vector<TemplateDecl> templatesNamed(const std::string &name, const std::string &scope,
                                             std::string *ns) const;
    // The same, for a member function template of `cls`.
    void instantiateViableMemberTemplates(const Type *cls,
                                          const std::string &name,
                                          const std::vector<const Type *> &argTypes,
                                          std::size_t pos);
    // The same by the table's own key - "CMatrix<3,3>::CMatrix<3,3>" for a constructor template.
    void instantiateViableMemberTemplatesUnder(const std::string &key,
                                               const std::vector<const Type *> &argTypes,
                                               std::size_t pos);
    // Every constructor template of `cls` that deduces from these argument types,
    // instantiated so the constructor set that is then resolved holds it.
    void instantiateConstructorTemplates(const Type *cls,
                                         const std::vector<const Type *> &argTypes,
                                         std::size_t pos);
    void instantiateConstructorTemplates(const Type *cls,
                                         const std::vector<ExprPtr> &args, std::size_t pos);
    const Signature *instantiateMemberTemplate(
        const TemplateDecl &mt, const std::vector<const Type *> &binding,
        const std::vector<long long> &values,
        const std::vector<TemplateArg> &args, std::size_t pos);
    // `Box<int, 3>` where a type was expected.
    const Type *instantiateClass(const TemplateDecl &decl, std::size_t pos);
    void instantiatePending();
    // Past kMaxNesting bodies deep, a specialization asked for from inside another's body is refused.
    void refuseInstantiationDepth(const std::string &name, int depth, std::size_t pos) const;
    bool memberIsUsed(const std::string &key) const;
    // "twice<int>" - the table key and what the diagnostics call it.
    std::string specializationKey(const std::string &name,
                                  const std::vector<TemplateArg> &args) const;
    // `packs` is indexed like the others and read only for a parameter that
    // is one; everything else ignores it.
    void bindTemplateParameters(const std::vector<TemplateParam> &params,
                                const std::vector<const Type *> &binding,
                                const std::vector<long long> &values,
                                const Packs &packs,
                                std::vector<Shadow> *undo);
    // [temp.deduct.type] rather than [temp.deduct.call]: this matches a template
    // *argument* against a pattern, where nothing decays and nothing may differ.
    // Deduction from a call is looser, because a conversion may still bridge it.
    bool matchPattern(const Type *pattern, const Type *arg,
                      std::vector<const Type *> *binding,
                      std::string *why, Packs *packs = nullptr, std::vector<long long> *values = nullptr) const;
    bool matchFunctionParams(const Type *pattern, const Type *arg, std::vector<const Type *> *binding,
                             std::vector<long long> *values, std::string *why, Packs *packs, bool deduce) const;
    // The partial specialization these arguments ask for, or npos for none.
    // Refuses by name where two match and neither is more specialized.
    std::size_t choosePartial(const TemplateDecl &decl,
                              const std::vector<TemplateArg> &args,
                              std::vector<const Type *> *binding,
                              std::vector<long long> *values,
                              Packs *packs,
                              std::size_t pos);
    // [temp.class.order]: whether one pattern is at least as specialized as
    // another, which is asked by matching each against the other.
    bool atLeastAsSpecialized(const TemplateDecl::Partial &a,
                              const TemplateDecl::Partial &b) const;
    bool moreSpecialized(const TemplateDecl::Partial &a,
                         const TemplateDecl::Partial &b) const;
    static bool mentionsPackExpansion(const Type *t);
    // `Box<T *>` after `template <class T> struct` - the argument list of a
    // partial specialization, read as patterns.
    void partialArguments(TemplateDecl::Partial *ps,
                          const std::vector<TemplateParam> &primary);
    void unbindTemplateParameters(const std::vector<Shadow> &undo);
    // Deduction: the arguments worked out from the call rather than written.
    // Answers false and fills `why` when some parameter cannot be deduced.
    bool deduceTemplateArguments(const TemplateDecl &decl,
                                 const std::vector<const Type *> &argTypes,
                                 std::vector<const Type *> *binding,
                                 std::vector<long long> *values,
                                 Packs *packs,
                                 std::string *why, std::size_t explicitCount = 0);
    // `values` receives a deduced non-type argument (the 3 in V<3>); null where only type parameters occur.
    // `nested`: inside a template-id an argument is matched as it is - no decay, and a cv-qualifier the pattern
    // does not write is deduced ([temp.deduct.type]/8: `It<C, V>` against `It<S, const int>` is V = const int).
    bool deduceOne(const Type *pattern, const Type *arg,
                   std::vector<const Type *> *binding,
                   std::vector<long long> *values, std::string *why,
                   Packs *packs = nullptr, bool nested = false) const;
    // An argument's type for deduction: an lvalue of type U goes as `U &` ([temp.deduct.call]/3).
    const Type *deductionArgType(const Expr &e) const;
    // Is this pattern parameter `T &&` with T a bare template parameter - a forwarding reference?
    static bool forwardingReference(const Type *pattern);
    void checkNonTypeArgument(const Type *pt, long long v, std::size_t argPos) const;
    // ---- Rung 7.1: `auto` ----
    // [dcl.spec.auto] deduces a variable's `auto` **as if by template argument deduction
    // from a call**, so deduceOne does the work and Kind::Deduced stands in.
    static bool mentionsDeduced(const Type *t);
    const Type *substituteDeduced(const Type *t, const Type *with);
    // The same deduction against a type already in hand, for a `for` whose
    // initialiser is built rather than written.
    const Type *deduceAutoFrom(const Type *declared, const Type *from,
                               const std::string &name, std::size_t pos);
    // The template a specialization is of, as a binding: its stand-in, or a pattern's own parameter.
    const Type *templateOf(const Type *spec) const;
    // [temp.arg.template]/3 as clang applies it under -std=c++11: whether a template with
    // parameters `arg` can take the argument list the parameter's own list `param` forms.
    static bool templateTemplateMatches(const std::vector<TemplateParam> &param,
                                        const std::vector<TemplateParam> &arg, std::string *why,
                                        std::size_t *at);
    // The argument list a deduction's binding amounts to, one TemplateArg per parameter.
    std::vector<TemplateArg> argumentsFromBinding(const std::vector<TemplateParam> &params,
                                                  const std::vector<const Type *> &binding,
                                                  const std::vector<long long> &values,
                                                  const Packs &packs) const;
    void requireDeducibleAfterPack(const TemplateDecl &decl, const Type *fn);
    void instantiateViableMemberTemplate(const TemplateDecl &mt,
                                         const std::vector<const Type *> &argTypes, std::size_t pos);
    bool deduceFromArgument(const Type *param, const Type *arg, std::vector<const Type *> *binding,
                            std::vector<long long> *values, std::string *why, Packs *packs = nullptr);

private:
    Parser &p_;
};

}   // namespace sema
