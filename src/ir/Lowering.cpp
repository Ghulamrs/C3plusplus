// ir::Lowering - see Lowering.h.

#include "Lowering.h"

namespace ir {

Lowering::Lowering(const sema::TypedTree &tree, TypeTable &types, const Target &target, Program &program)
    : tree_(tree), types_(types), target_(target), program_(program) {}

}   // namespace ir
