#pragma once

#include <string>
#include <vector>

namespace masm { struct Instruction; }

// **The compiler's door to the encoder library** (M4.2, doc-cxx3/M4-PLAN.md): the MASM text the spelling wrote for
// x86_64-windows, assembled in process by src/encoder into the object `-c` names - the same assembler the project's
// masm is a command line over, so the object is byte-equal with `-S` + masm. `asmName` is what the .s would have been called.
class CoffEmitter {
public:
    // `structured` (M4.4) holds the instructions the spelling handed over as data, each standing where the text reads `\001k`.
    static bool assemble(const std::string &text, const std::string &asmName, const std::string &object,
                         std::vector<std::string> &errors, const std::vector<masm::Instruction> *structured = nullptr);
    // One line counting what went as data, what as a directive, and the instruction lines still in the text, by mnemonic.
    static std::string census(const std::string &text, const std::vector<masm::Instruction> *structured);
};
