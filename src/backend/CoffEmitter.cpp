#include "CoffEmitter.h"

#include "../encoder/Assembler.h"

#include <map>

// One assembly over the text, as masm's command line runs one over a file: the passes until the layout settles,
// the CodeView from the .FILE and .LINE directives the spelling wrote under -g, the object written. Errors come
// back as the library words them, each naming the line of the text it stopped at.
bool CoffEmitter::assemble(const std::string &text, const std::string &asmName, const std::string &object,
                           std::vector<std::string> &errors, const std::vector<masm::Instruction> *structured) {
    masm::Assembler assembler(asmName, object, false);
    const bool ok = assembler.runText(text, structured);
    errors = assembler.errors();
    return ok;
}

// **What took which door** (M4.4's record): every indented line of the text is a `\001k`, a directive (a dot, or a
// data word - DB, DD, ALIGN, .LINE), or an instruction the text still carries; the last are counted by mnemonic.
std::string CoffEmitter::census(const std::string &text, const std::vector<masm::Instruction> *structured) {
    std::map<std::string, int> textual;
    int directives = 0;
    std::size_t at = 0;
    while (at < text.size()) {
        std::size_t nl = text.find('\n', at);
        if (nl == std::string::npos) nl = text.size();
        if (nl > at + 2 && text[at] == ' ' && text[at + 1] == ' ' && text[at + 2] != '\001') {
            std::size_t sp = text.find(' ', at + 2);
            std::string word = text.substr(at + 2, (sp == std::string::npos ? nl : sp) - at - 2);
            static const char *const kData[] = { "DB", "DW", "DD", "DQ", "ALIGN", nullptr };
            bool data = word[0] == '.';
            for (const char *const *d = kData; *d != nullptr && !data; d++) data = word == *d;
            if (data) directives++;
            else textual[word]++;
        }
        at = nl + 1;
    }
    std::string out = "coff: " + std::to_string(structured == nullptr ? 0 : structured->size()) + " instructions as data, " +
                      std::to_string(directives) + " directive lines";
    int n = 0;
    for (std::map<std::string, int>::const_iterator i = textual.begin(); i != textual.end(); ++i) n += i->second;
    out += ", " + std::to_string(n) + " instruction lines as text";
    for (std::map<std::string, int>::const_iterator i = textual.begin(); i != textual.end(); ++i)
        out += " " + i->first + "=" + std::to_string(i->second);
    return out;
}
