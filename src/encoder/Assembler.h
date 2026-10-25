#pragma once

#include "AsmUnit.h"
#include "Coff.h"
#include "X64.h"

namespace masm {

// **One assembly, from a file or from text in memory**: the passes until the layout settles, the CodeView, the
// object written where `output` says. `input` names the .asm for messages and S_OBJNAME's neighbour; a compiler
// hands its own text to `runText` and the name it would have given the .asm.
class Assembler {
public:
    Assembler(const std::string &input, const std::string &output, bool debug);
    bool run();
    // The text, with a line reading `\001k` standing for structured[k], an Instruction handed over already parsed (M4.4).
    bool runText(const std::string &text, const std::vector<Instruction> *structured = 0);
    const std::vector<std::string> &errors() const;
    void fail(const std::string &why) { unit.errors.push_back(why); }

private:
    bool assemble(const std::vector<std::string> &lines, const std::vector<Instruction> *structured);
    std::string input;
    std::string output;
    X64Target target;
    CoffWriter writer;
    Unit unit;

    Assembler(const Assembler &);
    Assembler &operator=(const Assembler &);
};

}
