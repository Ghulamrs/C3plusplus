#include "CoffEmitter.h"

#include "../encoder/Assembler.h"

// One assembly over the text, as masm's command line runs one over a file: the passes until the layout settles,
// the CodeView from the .FILE and .LINE directives the spelling wrote under -g, the object written. Errors come
// back as the library words them, each naming the line of the text it stopped at.
bool CoffEmitter::assemble(const std::string &text, const std::string &asmName, const std::string &object,
                           std::vector<std::string> &errors) {
    masm::Assembler assembler(asmName, object, false);
    const bool ok = assembler.runText(text);
    errors = assembler.errors();
    return ok;
}
