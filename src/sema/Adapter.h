#pragma once

// **The adapter: a typed tree turned into the Ast.h the backends read** - M2.11, doc-cxx3/M2-PLAN.md - with the
// parser's lowerings, reading from it only what it decides of layout (frame slots, scopes, string labels). A function
// that reaches past the subset is answered null, with the reason, and keeps the parser's body.

#include "Entities.h"
#include "Typing.h"
#include "../Ast.h"

#include <string>
#include <unordered_set>

class Parser;

namespace sema {

struct Local;

class Adapter {
public:
    Adapter(Parser &p, const Typer &typer) : p_(p), typer_(typer) {}
    // The body of the function `fn` (a `function` node typed into `typed`), or null with the reason in *why.
    StmtPtr body(TypedId typed, const Function &fn, std::string *why);
    // **-front=typed and -check=adapted**: every root function the typed tree reaches, adapted. With `replace` its body
    // is the adapter's; without, each is held against the parser's and the report says how many agree.
    static std::string run(Parser &p, Program &program, bool replace, int *differ);
private:
    struct Outside { std::string why; explicit Outside(const std::string &w) : why(w) {} };
    StmtPtr stmt(TypedId id);
    StmtPtr declaration(TypedId id);
    StmtPtr forStatement(TypedId id);
    std::size_t declaratorEnd(TypedId var, int *bounds) const;
    void arrayInitialiser(TypedId var, const Local &l, std::vector<StmtPtr> &body);
    ExprPtr expr(TypedId id);
    ExprPtr name(TypedId id);
    ExprPtr member(TypedId id);
    ExprPtr classValue(ExprPtr e, const Type *to);
    ExprPtr bind(const Type *ref, ExprPtr init);
    ExprPtr unary(TypedId id);
    ExprPtr binary(TypedId id, const std::string &op, TypedId l, TypedId r);
    ExprPtr assign(TypedId id);
    ExprPtr call(TypedId id);
    ExprPtr memberCall(TypedId id, TypedId head, std::vector<ExprPtr> args);
    ExprPtr finishCall(TypedId at, const std::string &name, const Signature &f, const std::vector<const Type *> &params,
                       std::vector<ExprPtr> args, bool hasThis, ExprPtr callee = ExprPtr());
    ExprPtr pointerCall(TypedId id, TypedId head, std::vector<ExprPtr> args);
    ExprPtr condition(TypedId id);
    ExprPtr arithmetic(const std::string &op, ExprPtr a, ExprPtr b);
    ExprPtr pointerAdd(ExprPtr ptr, ExprPtr n);
    ExprPtr pointerSub(ExprPtr l, ExprPtr r);
    ExprPtr convert(ExprPtr e, const Type *to, bool cast = false);
    ExprPtr promote(ExprPtr e);
    ExprPtr decay(ExprPtr e);
    const syntax::Node &syn(TypedId id) const;
    static void outside(const std::string &why);
    static bool plainClass(Parser &p, const Type *t);
    static void plainLocal(Parser &p, const Local &l);
    Parser &p_;
    const Typer &typer_;
    const Type *returns_ = nullptr;
    bool scopedName(const std::string &name);
    bool scopedBuilt_ = false;
    std::unordered_set<std::string> scoped_, globalFunctions_;
    std::string fnSymbol_;
};

}   // namespace sema
