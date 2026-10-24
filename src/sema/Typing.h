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

// **The census of what the typed tree covers** (M2.15): every function the backends emit, split into those whose body the
// grammar read - each with a syntax root, found by symbol - and those the compiler wrote, counted by kind.
struct Census {
    std::vector<std::pair<syntax::NodeId, std::size_t> > bodies;    // a root and the index of the function it defined
    std::vector<std::pair<std::string, int> > synthesised;          // kind, count
    int emitted = 0;
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
    static Census census(const Parser &p, const Program &program);
    // The census's counts as the report spells them, "functions=N bodies=K synthesised=M", and the kinds behind M.
    static std::string censusLine(const Census &c);
    static std::string censusKinds(const Census &c);
private:
    TypedId node(syntax::NodeId id);
    const Type *expression(syntax::NodeId id, const std::vector<const Type *> &parts);
    const Type *literal(syntax::NodeId id);
    const Type *named(syntax::NodeId id, syntax::NodeId *decl);
    const Type *resolvedType(syntax::NodeId id, syntax::Kind kind) const;
    struct Declared { const Type *type; syntax::NodeId decl; };
    Parser &p_;
    TypedTree tree_;
    std::vector<std::unordered_map<std::string, Declared> > scopes_;
};

}   // namespace sema
