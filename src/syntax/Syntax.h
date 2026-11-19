#pragma once

// **The syntax tree, with handles** - M2 step 2, doc-cxx3/M2-PLAN.md and design section 2. One arena per
// translation unit; a node is {kind, children range, payload, location}, numbered in creation order, so two
// runs make the same tree. The parser records a node beside each Ast.h node it returns; nothing reads it yet.

#include "SyntaxId.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

class Source;

namespace syntax {

enum class Kind : std::uint8_t {
    IntLit, FloatLit, CharLit, StringLit, WordLit, Name, This,
    Unary, Postfix, Binary, Assign, Conditional, Comma,
    Call, Subscript, Member, Cast, Sizeof, Paren, New, Delete, Throw, Lambda, Other,
    // statements
    Block, ExprStmt, DeclStmt, Return, If, While, Do, For, Switch, Case, Default,
    Break, Continue, Goto, Label, Try, Catch, Null,
    Typedef,    // a typedef or alias-declaration in a block that defines no class or enumeration
    // declarations
    Var, Function,
    MemInit     // a constructor's mem-initialiser: its text the member or base it names, its parts the arguments
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
    bool pattern;           // a template's signature kept at its declaration (M8.1): dead to every reader of the tree, and held
    std::uint32_t member;   // 1 + the pack member a pattern's copy was read for, 0 outside an expansion
    std::uint8_t marks;     // a read's name (M8.1 row 5, M8rev M7): what waits for the arguments, and a written `typename`
};

// TypeDependent: the type waits for the arguments; DependentName: looked up again at the instantiation ([temp.dep]/1,
// an unqualified callee with a type-dependent argument, or a name in a dependent scope); Unfound: no declaration (M8rev M1).
const std::uint8_t TypeDependent = 1, Typename = 2, Unfound = 4,
                   DependentName = 8;

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
    // The live roots made after `from` located in [lo, hi] are dead: a template's arguments, folded, are no expression.
    void drop(std::size_t from, std::size_t lo, std::size_t hi);
    // The live roots made after `from` are pack member m's: they share the pattern's place, and are not a rewound read.
    void markMember(std::size_t from, std::size_t m);
    // A node whose parent died is a root again, for the live node that records the same tokens to adopt (M2.15).
    void release(NodeId id) { nodes_[id.v].hasParent = false; }
    // A subtree a kill took that is the tree's after all: a lambda's replayed body made inside a discarded reading.
    void revive(NodeId id);
    // A subtree read with a template's parameters standing for themselves: kept for the count, and no root of the tree.
    void markPattern(NodeId id);
    // Two subtrees held against each other (M8.1): 0 the same, 1 a text differs under the same kinds, 2 a kind or a count.
    int differs(NodeId a, NodeId b) const;
    // Every live root - a node no other node holds - in creation order, as an S-expression a line.
    // Every live root, in creation order.
    std::vector<NodeId> roots() const;
    void dump(std::string &out, const Source &src) const;
    // A template body a read declined (M8.1): the dump names it after the tree, where and why.
    void noteUnread(std::size_t pos, const std::string &why) { unread_.push_back(std::make_pair(pos, why)); }
    // A template body read at its declaration (M8.1): dead to every reader, and shown once by the dump after the tree.
    void noteRead(NodeId id) { read_.push_back(id); }
    // What both lists held, and a read made again taking its notes back to that.
    std::pair<std::size_t, std::size_t> noted() const { return std::make_pair(read_.size(), unread_.size()); }
    void forget(const std::pair<std::size_t, std::size_t> &n) { read_.erase(read_.begin() + static_cast<long>(n.first), read_.end()); unread_.erase(unread_.begin() + static_cast<long>(n.second), unread_.end()); }
    void mark(NodeId id, std::uint8_t marks) { nodes_[id.v].marks |= marks; }
    std::string spelled(NodeId id) const { std::string out; dumpNode(out, id); return out; }
    // [temp.dep.candidate] (M8rev M7): a dependent-name callee's functions as unqualified lookup found them at the
    // definition - each its spelling for the dump and its symbol, empty for a template, for the instantiation.
    void noteFound(NodeId id, const std::string &spelling, const std::string &symbol) {
        found_[id.v].push_back(std::make_pair(strings_.intern(spelling), strings_.intern(symbol)));
    }
    std::vector<std::string> foundSymbols(NodeId id) const;
private:
    void dumpNode(std::string &out, NodeId id) const;
    std::unordered_map<std::uint32_t, std::vector<std::pair<std::uint32_t, std::uint32_t> > > found_;
    std::vector<Node> nodes_;
    std::vector<std::pair<std::size_t, std::string> > unread_;
    std::vector<NodeId> read_;
    std::vector<NodeId> children_;
    StringTable strings_;
};

}   // namespace syntax
