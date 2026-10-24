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
        case Kind::Other: return "other";
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

ExprId Tree::add(Kind kind, const std::string &text, std::size_t pos, const std::vector<ExprId> &children) {
    Node n;
    n.kind = kind;
    n.first = static_cast<std::uint32_t>(children_.size());
    n.count = 0;
    n.text = strings_.intern(text);
    n.loc = static_cast<std::uint32_t>(pos);
    n.hasParent = false;
    for (std::size_t i = 0; i < children.size(); i++) {
        if (!children[i].valid()) continue;     // an operand the grammar did not record is left out
        children_.push_back(children[i]);
        nodes_[children[i].v].hasParent = true;
        n.count++;
    }
    nodes_.push_back(n);
    return ExprId(static_cast<std::uint32_t>(nodes_.size() - 1));
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

void Tree::dumpNode(std::string &out, ExprId id) const {
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

void Tree::dump(std::string &out, const Source &src) const {
    for (std::size_t i = 1; i < nodes_.size(); i++) {
        if (nodes_[i].hasParent) continue;
        out += src.where(nodes_[i].loc);
        out += ": ";
        dumpNode(out, ExprId(static_cast<std::uint32_t>(i)));
        out += '\n';
    }
}

}   // namespace syntax
