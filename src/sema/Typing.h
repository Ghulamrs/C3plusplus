#pragma once

// **Sema's typing of the syntax tree** - M2.10, doc-cxx3/M2-PLAN.md. The typed tree is an arena like the syntax tree's,
// a node per syntax node with the type Sema gives it and, from M3b.0, the record the parser made where it resolved the
// node (M3-IR 3.8); a node outside the subset is typed null with its parts still typed. -check=typed holds it to Ast.h.

#include "../Lexer.h"
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

// **The kinds of thing a node resolves to** (M2.16), one enum for the parser's record and the typed tree's copy, so
// the two cannot drift: Function is a function named as a value, Call one reached by a call, Callee the function on a
// call's callee node, TypeName the class a temporary's callee names, Value a type the parser gave a form naming no entity.
struct ResolutionKind {
    enum What { None, Local, Global, Function, Call, Callee, Member, Enumerator, StaticMember, This, Temporary,
                TypeName, Value };
};

// **The record on the typed tree** (M3b.0, M3-IR 3.8): Parser::Resolved field for field, its types as TypeId handles
// (R6.3), copied by the Typer from the record the parser made where it resolved the node, in the scope it had then.
struct Resolution : ResolutionKind {
    What what;
    TypeId type;            // as declared: a reference keeps its `&`, a function is its function type
    TypeId owner;           // a member's class, a member function's
    std::string symbol;     // a global's, a function's or a static member's linkage name; a local's static name
    int serial;             // a local's serial; a member's offset
    long long value;        // an enumerator's, a folded static member's
    bool isConst, isThreadLocal;    // a global's
    bool hasThis;           // a call's: its function type's first parameter is the object's address
    bool indirect;          // a call's: through a value - a pointer or a vtable slot
    std::string name;       // a global's or a function's lookup name, which its Var is named by
    Resolution() : what(None), serial(-1), value(0), isConst(false), isThreadLocal(false), hasThis(false), indirect(false) {}
};

// What a sizeof, alignof or noexcept folded to, or an integer or character literal's value, with its type.
struct Folded {
    long long value;
    TypeId type;
    bool has;
    Folded() : value(0), has(false) {}
};

struct TypedNode {
    syntax::NodeId syntax;
    const Type *type;           // null where the subset does not reach
    std::uint32_t first, count;
    syntax::NodeId decl;        // a name's: the `var` node it names, none for one at file scope
    // **The parser's facts for this node, copied as it was typed** (M3b.0): its record (an index into
    // TypedTree::resolution, 0 none), a cast's named type, what it folded to, and a literal's token (0 none).
    std::uint32_t resolved;
    TypeId castType;            // a cast's type as named; a static_cast or const_cast to a reference keeps its `&`
    bool castXvalue;            // such a reference cast's operand is an xvalue: `static_cast<T &&>`
    Folded folded;
    std::uint32_t literal;
    std::uint8_t initForm;      // a `var`'s initialiser form: 0 none, 1 `= e` or `(e)`, 2 a braced list, 3 empty braces
    std::uint8_t forParts;      // a `for`'s parts present: 1 init, 2 condition, 4 step, 8 recorded at all
};

class TypedTree {
public:
    TypedTree() { nodes_.push_back(TypedNode()); resolutions_.push_back(Resolution()); literals_.push_back(Token()); labels_.push_back(std::string()); }
    TypedId add(syntax::NodeId syn, const Type *type, const std::vector<TypedId> &children,
                syntax::NodeId decl = syntax::NodeId());
    const TypedNode &node(TypedId id) const { return nodes_[id.v]; }
    TypedId child(TypedId id, std::uint32_t i) const { return children_[nodes_[id.v].first + i]; }
    std::size_t size() const { return nodes_.size() - 1; }
    // The record and the other facts of node `id`, each set once as the Typer types it (M3b.0).
    void setResolution(TypedId id, const Resolution &r);
    void setCast(TypedId id, TypeId to, bool xvalue);
    void setFolded(TypedId id, long long value, TypeId type);
    void setInitForm(TypedId id, int form) { nodes_[id.v].initForm = static_cast<std::uint8_t>(form); }
    void setForParts(TypedId id, int parts) { nodes_[id.v].forParts = static_cast<std::uint8_t>(parts | 8); }
    // A string literal's token comes with the label the program's data holds its bytes under (M3b.1: the lowering's `addr`).
    void setLiteral(TypedId id, const Token &token, const std::string &label = std::string());
    const Resolution &resolution(TypedId id) const { return resolutions_[nodes_[id.v].resolved]; }
    // The token a literal node was read from; `node(id).literal` says whether there is one, and 0 gives an End token.
    const Token &literal(TypedId id) const { return literals_[nodes_[id.v].literal]; }
    const std::string &stringLabel(TypedId id) const { return labels_[nodes_[id.v].literal]; }
    std::size_t resolutionCount() const { return resolutions_.size() - 1; }
private:
    std::vector<TypedNode> nodes_;
    std::vector<TypedId> children_;
    std::vector<Resolution> resolutions_;    // index 0 is the none record
    std::vector<Token> literals_;            // index 0 is no token
    std::vector<std::string> labels_;        // beside literals_: a string literal's label, else empty
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
    // Facts of a function's declaration the lowering is told (M3b.1): its return type as written, and whether its body is a try.
    TypeId declaredReturn(syntax::NodeId fn) const;
    bool wrappedBody(const std::string &symbol) const;
    // A body read in a class's, a namespace's or a template's scope (Parser::scopedFunctions_), which the adapter left alone too.
    int scopedBody(syntax::NodeId fn) const;
    bool packBody(const std::string &symbol) const;
    std::string typeInfoSymbol(const Type *t) const;   // the type_info a catch of t names, Sema's; empty where none can be made
    bool constructed(const Type *cls) const;          // a class some constructor builds, written or implicit
    bool assigned(const Type *cls) const;             // a class with an operator= of its own, written or implicit
    bool defaultInit(const Type *cls, const std::string &member) const;   // the member has an initialiser of its own
    bool guardedStatic(const std::string &symbol) const;  // a static local built at run time, under its guard   // a pack was expanded in the body: one node stands for its members   // Parser::scopedFunctions_'s bits, 0 for a body at file scope
    // Whether the function of this symbol is a template specialization (Signature::fromTemplate): its body is a replay.
    bool fromTemplate(const std::string &symbol) const;
    // The census's counts as the report spells them, "functions=N bodies=K synthesised=M", and the kinds behind M.
    static std::string censusLine(const Census &c);
    static std::string censusKinds(const Census &c);
private:
    TypedId node(syntax::NodeId id);
    // The parser's record, cast type, folded value and literal token for syntax node `id`, copied onto typed node `t`.
    void record(syntax::NodeId id, TypedId t);
    // -check=typed's half for the copy: what on typed node `t` is not the parser's, or nothing.
    static std::string recordDiffers(const Parser &p, const TypedTree &tree, TypedId t);
    const Type *expression(syntax::NodeId id, const std::vector<const Type *> &parts);
    const Type *literal(syntax::NodeId id);
    const Type *named(syntax::NodeId id, syntax::NodeId *decl);
    const Type *resolvedType(syntax::NodeId id, syntax::Kind kind) const;
    const Type *recordedValue(syntax::NodeId id);
    int recorded_ = 0;      // nodes typed from a Value record, the parser's own answer: not independent agreement
    // Where a node's type came from, if not the typer's own rule: 'e' an entity's record, 'v' a Value record (C3).
    std::unordered_map<std::uint32_t, char> source_;
    struct Declared { const Type *type; syntax::NodeId decl; };
    Parser &p_;
    TypedTree tree_;
    std::vector<std::unordered_map<std::string, Declared> > scopes_;
};

}   // namespace sema
