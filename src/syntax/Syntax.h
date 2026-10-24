#pragma once

// **The syntax tree, with handles** - M2 step 2, doc-cxx3/M2-PLAN.md and design section 2. One arena per
// translation unit; a node is {kind, children range, payload, location}, numbered in creation order, so two
// runs make the same tree. The parser records a node beside each Ast.h node it returns; nothing reads it yet.

#include "SyntaxId.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

class Source;

namespace syntax {

enum class Kind : std::uint8_t {
    IntLit, FloatLit, CharLit, StringLit, Name, This,
    Unary, Postfix, Binary, Assign, Conditional, Comma,
    Call, Subscript, Member, Cast, Sizeof, Paren, New, Delete, Throw, Lambda, Other,
    // statements
    Block, ExprStmt, DeclStmt, Return, If, While, Do, For, Switch, Case, Default,
    Break, Continue, Goto, Label, Try, Null,
    // declarations
    Var, Function
};

// The name of a kind, as the dump spells it.
const char *kindName(Kind k);

// Identifiers, literal spellings and operator spellings, each kept once: a node's payload is an index here.
class StringTable {
public:
    StringTable() { strings_.push_back(std::string()); index_[std::string()] = 0; }
    std::uint32_t intern(const std::string &s);
    const std::string &at(std::uint32_t i) const { return strings_[i]; }
private:
    std::vector<std::string> strings_;
    std::unordered_map<std::string, std::uint32_t> index_;
};

struct Node {
    Kind kind;
    std::uint32_t first;    // children_[first, first + count)
    std::uint32_t count;
    std::uint32_t text;     // a StringTable index: the spelling, the name or the operator
    std::uint32_t loc;      // the source offset, Source's position cut to 32 bits
    bool hasParent;
    bool dead;              // made by a trial that rewound: its tokens are read again, and these nodes are not the tree's
};

class Tree {
public:
    Tree() { nodes_.push_back(Node()); }   // handle 0 is none
    NodeId add(Kind kind, const std::string &text, std::size_t pos,
               const std::vector<NodeId> &children = std::vector<NodeId>());
    const Node &node(NodeId id) const { return nodes_[id.v]; }
    NodeId child(NodeId id, std::uint32_t i) const { return children_[nodes_[id.v].first + i]; }
    const std::string &text(NodeId id) const { return strings_.at(nodes_[id.v].text); }
    std::size_t size() const { return nodes_.size() - 1; }
    // The live roots made after node `from` located in [lo, hi], in creation order - a statement's parts; a rewound read's earlier root at a location dies.
    std::vector<NodeId> adopt(std::size_t from, std::size_t lo, std::size_t hi);
    // Every node made after `from` is dead: a trial rewound past them.
    void kill(std::size_t from);
    // Every live root - a node no other node holds - in creation order, as an S-expression a line.
    // Every live root, in creation order.
    std::vector<NodeId> roots() const;
    void dump(std::string &out, const Source &src) const;
private:
    void dumpNode(std::string &out, NodeId id) const;
    std::vector<Node> nodes_;
    std::vector<NodeId> children_;
    StringTable strings_;
};

}   // namespace syntax
