// syntax::Tree - see Syntax.h.

#include "Syntax.h"
#include "../Source.h"

namespace syntax {

const char *kindName(Kind k) {
    switch (k) {
        case Kind::IntLit: return "int";
        case Kind::FloatLit: return "float";
        case Kind::CharLit: return "char";
        case Kind::StringLit: return "string";
        case Kind::Name: return "name";
        case Kind::This: return "this";
        case Kind::WordLit: return "literal";
        case Kind::Unary: return "unary";
        case Kind::Postfix: return "postfix";
        case Kind::Binary: return "binary";
        case Kind::Assign: return "assign";
        case Kind::Conditional: return "conditional";
        case Kind::Comma: return "comma";
        case Kind::Call: return "call";
        case Kind::Subscript: return "subscript";
        case Kind::Member: return "member";
        case Kind::Cast: return "cast";
        case Kind::Sizeof: return "sizeof";
        case Kind::Paren: return "paren";
        case Kind::New: return "new";
        case Kind::Delete: return "delete";
        case Kind::Throw: return "throw";
        case Kind::Lambda: return "lambda";
        case Kind::Other: return "other";
        case Kind::Block: return "block";
        case Kind::ExprStmt: return "expr";
        case Kind::DeclStmt: return "decl";
        case Kind::Return: return "return";
        case Kind::If: return "if";
        case Kind::While: return "while";
        case Kind::Do: return "do";
        case Kind::For: return "for";
        case Kind::Switch: return "switch";
        case Kind::Case: return "case";
        case Kind::Default: return "default";
        case Kind::Break: return "break";
        case Kind::Continue: return "continue";
        case Kind::Goto: return "goto";
        case Kind::Label: return "label";
        case Kind::Try: return "try";
        case Kind::Catch: return "catch";
        case Kind::Null: return "null";
        case Kind::Var: return "var";
        case Kind::Function: return "function";
    }
    return "?";
}

std::uint32_t StringTable::intern(const std::string &s) {
    std::unordered_map<std::string, std::uint32_t>::const_iterator it = index_.find(s);
    if (it != index_.end()) return it->second;
    const std::uint32_t i = static_cast<std::uint32_t>(strings_.size());
    strings_.push_back(s);
    index_[s] = i;
    return i;
}

NodeId Tree::add(Kind kind, const std::string &text, std::size_t pos, const std::vector<NodeId> &children) {
    Node n;
    n.kind = kind;
    n.first = static_cast<std::uint32_t>(children_.size());
    n.count = 0;
    n.text = strings_.intern(text);
    n.loc = static_cast<std::uint32_t>(pos);
    n.hasParent = false;
    n.dead = false;
    for (std::size_t i = 0; i < children.size(); i++) {
        if (!children[i].valid()) continue;     // an operand the grammar did not record is left out
        children_.push_back(children[i]);
        nodes_[children[i].v].hasParent = true;
        n.count++;
    }
    nodes_.push_back(n);
    return NodeId(static_cast<std::uint32_t>(nodes_.size() - 1));
}

// A string's quotes are the dump's own, so a quote or a backslash in the spelling is written escaped.
static void quoted(std::string &out, const std::string &s) {
    out += '"';
    for (std::size_t i = 0; i < s.size(); i++) {
        if (s[i] == '"' || s[i] == '\\') out += '\\';
        if (s[i] == '\n') { out += "\\n"; continue; }
        out += s[i];
    }
    out += '"';
}

void Tree::dumpNode(std::string &out, NodeId id) const {
    const Node &n = nodes_[id.v];
    out += '(';
    out += kindName(n.kind);
    if (n.text != 0) { out += ' '; quoted(out, strings_.at(n.text)); }
    for (std::uint32_t i = 0; i < n.count; i++) {
        out += ' ';
        dumpNode(out, children_[n.first + i]);
    }
    out += ')';
}

std::vector<NodeId> Tree::adopt(std::size_t from, std::size_t lo, std::size_t hi) {
    std::vector<NodeId> out;
    std::unordered_map<std::uint32_t, std::size_t> at;     // location -> its place in `out`
    for (std::size_t i = from + 1; i < nodes_.size(); i++) {
        const Node &n = nodes_[i];
        if (n.hasParent || n.dead || n.loc < lo || n.loc > hi) continue;
        std::unordered_map<std::uint32_t, std::size_t>::const_iterator seen = at.find(n.loc);
        if (seen != at.end()) {
            nodes_[out[seen->second].v].dead = true;
            out[seen->second] = NodeId(static_cast<std::uint32_t>(i));
            continue;
        }
        at[n.loc] = out.size();
        out.push_back(NodeId(static_cast<std::uint32_t>(i)));
    }
    return out;
}

void Tree::kill(std::size_t from) {
    for (std::size_t i = from + 1; i < nodes_.size(); i++) nodes_[i].dead = true;
}

std::vector<NodeId> Tree::roots() const {
    std::vector<NodeId> out;
    for (std::size_t i = 1; i < nodes_.size(); i++)
        if (!nodes_[i].hasParent && !nodes_[i].dead) out.push_back(NodeId(static_cast<std::uint32_t>(i)));
    return out;
}

void Tree::dump(std::string &out, const Source &src) const {
    for (std::size_t i = 1; i < nodes_.size(); i++) {
        if (nodes_[i].hasParent || nodes_[i].dead) continue;
        out += src.where(nodes_[i].loc);
        out += ": ";
        dumpNode(out, NodeId(static_cast<std::uint32_t>(i)));
        out += '\n';
    }
}

}   // namespace syntax
