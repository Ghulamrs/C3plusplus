#pragma once

// **Sema's typing of the syntax tree** - M2.10, doc-cxx3/M2-PLAN.md. The typed tree is an arena like the syntax tree's,
// a node per syntax node with the type Sema gives it; the subset typed so far is scalars, operators, locals and calls,
// and a node outside it is typed null with its parts still typed. -check=typed holds it against the parser's Ast.h.

#include "../syntax/Syntax.h"
#include "../Type.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

class Parser;
class Source;
struct Program;

namespace sema {

struct TypedId {
    std::uint32_t v;
    TypedId() : v(0) {}
    explicit TypedId(std::uint32_t value) : v(value) {}
    bool valid() const { return v != 0; }
};

struct TypedNode {
    syntax::NodeId syntax;
    const Type *type;           // null where the subset does not reach
    std::uint32_t first, count;
    syntax::NodeId decl;        // a name's: the `var` node it names, none for one at file scope
};

class TypedTree {
public:
    TypedTree() { nodes_.push_back(TypedNode()); }
    TypedId add(syntax::NodeId syn, const Type *type, const std::vector<TypedId> &children,
                syntax::NodeId decl = syntax::NodeId());
    const TypedNode &node(TypedId id) const { return nodes_[id.v]; }
    TypedId child(TypedId id, std::uint32_t i) const { return children_[nodes_[id.v].first + i]; }
    std::size_t size() const { return nodes_.size() - 1; }
private:
    std::vector<TypedNode> nodes_;
    std::vector<TypedId> children_;
};

class Typer {
public:
    explicit Typer(Parser &p) : p_(p) {}
    // One function's syntax, typed; its parameters and locals are its scopes.
    TypedId typeFunction(syntax::NodeId fn);
    const TypedTree &tree() const { return tree_; }
    // **-check=typed**: every root function typed, each typed expression held against the type the parser gave the
    // Ast.h node carrying the same syntax - the innermost such node, before any conversion wrapped it.
    static std::string check(Parser &p, const Program &program, const Source &src, int *differ);
private:
    TypedId node(syntax::NodeId id);
    const Type *expression(syntax::NodeId id, const std::vector<const Type *> &parts);
    const Type *literal(syntax::NodeId id);
    const Type *named(const std::string &name, syntax::NodeId *decl);
    struct Declared { const Type *type; syntax::NodeId decl; };
    Parser &p_;
    TypedTree tree_;
    std::vector<std::unordered_map<std::string, Declared> > scopes_;
    bool localOnly_ = false;    // in a function whose other names are a class's or a namespace's: those are not typed yet
};

}   // namespace sema
