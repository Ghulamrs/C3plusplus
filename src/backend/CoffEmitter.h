#pragma once

#include <string>
#include <vector>

// **The compiler's door to the encoder library** (M4.2, doc-cxx3/M4-PLAN.md): the MASM text the spelling wrote for
// x86_64-windows, assembled in process by src/encoder into the object `-c` names - the same assembler the project's
// masm is a command line over, so the object is byte-equal with `-S` + masm. `asmName` is what the .s would have been called.
class CoffEmitter {
public:
    static bool assemble(const std::string &text, const std::string &asmName, const std::string &object,
                         std::vector<std::string> &errors);
};
