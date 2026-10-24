#pragma once

// A syntax node's handle, apart from the tree so that Ast.h can carry one (M2 step 2, doc-cxx3/M2-PLAN.md).

#include <cstdint>

namespace syntax {

// 0 is none, so an Ast.h node the grammar did not record carries no syntax.
struct ExprId {
    std::uint32_t v;
    ExprId() : v(0) {}
    explicit ExprId(std::uint32_t value) : v(value) {}
    bool valid() const { return v != 0; }
};

}   // namespace syntax
