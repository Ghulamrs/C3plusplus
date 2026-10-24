#pragma once

// A syntax node's handle - an expression's, a statement's or a declaration's - apart from the tree so that Ast.h can carry one (M2 step 2, doc-cxx3/M2-PLAN.md).

#include <cstdint>

namespace syntax {

// 0 is none, so an Ast.h node the grammar did not record carries no syntax.
struct NodeId {
    std::uint32_t v;
    NodeId() : v(0) {}
    explicit NodeId(std::uint32_t value) : v(value) {}
    bool valid() const { return v != 0; }
};

}   // namespace syntax
