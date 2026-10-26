#pragma once

// **The lowering of the typed tree to the SSA IR** (M3b.1, doc-cxx3/M3-IR.md 3.8), built with no Parser in reach: what
// it reads is the typed tree with its record, the TypeTable for layout, the Target and the program's data - this
// header includes nothing of src/parser/, and that is the gate M3b.0 fixes. M3b.1 fills the class in.

#include "../sema/Typing.h"
#include "../Type.h"

struct Program;

namespace ir {

class Lowering {
public:
    Lowering(const sema::TypedTree &tree, TypeTable &types, const Target &target, Program &program);
    const sema::TypedTree &tree() const { return tree_; }
    TypeTable &types() const { return types_; }
    const Target &target() const { return target_; }
    Program &program() const { return program_; }
private:
    const sema::TypedTree &tree_;
    TypeTable &types_;
    const Target &target_;
    Program &program_;
};

}   // namespace ir
