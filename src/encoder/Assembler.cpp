#include "Assembler.h"

#include <fstream>

namespace masm {

Assembler::Assembler(const std::string &in, const std::string &out, bool debug) : input(in), output(out)
{
    unit.debug = debug;
}

const std::vector<std::string> &Assembler::errors() const
{
    return unit.errors;
}

bool Assembler::run()
{
    std::ifstream f(input.c_str());
    if (!f) {
        unit.errors.push_back("cannot open " + input);
        return false;
    }
    std::vector<std::string> lines;
    std::string text;
    while (std::getline(f, text)) {
        if (!text.empty() && text[text.size() - 1] == '\r')
            text.erase(text.size() - 1);
        lines.push_back(text);
    }
    return assemble(lines);
}

// The text as a compiler holds it before writing a .s: split at its line ends, a CR before one dropped.
bool Assembler::runText(const std::string &text)
{
    std::vector<std::string> lines;
    size_t at = 0;
    while (at < text.size()) {
        size_t nl = text.find('\n', at);
        if (nl == std::string::npos) nl = text.size();
        size_t end = nl;
        if (end > at && text[end - 1] == '\r') end--;
        lines.push_back(text.substr(at, end - at));
        at = nl + 1;
    }
    return assemble(lines);
}

// **Passes until the layout settles**: the first sees a forward jump as short and a forward label as untyped, the
// next knows every label from the pass before, one more confirms nothing moved; a jump only grows, so this ends.
bool Assembler::assemble(const std::vector<std::string> &lines)
{
    std::vector<Token> tokens;
    for (int pass = 1; ; pass++) {
        unit.begin_pass(pass);
        target.begin_pass(pass);
        for (size_t i = 0; i < lines.size() && !target.finished(); i++) {
            unit.line = (int)i + 1;
            std::string err;
            if (!split_line(lines[i], tokens, err)) {
                unit.error(err);
                continue;
            }
            if (!tokens.empty()) {
                const int sec = unit.current;
                const unsigned long at = sec >= 0 ? (unsigned long)unit.sections[sec].bytes.size() : 0;
                target.statement(unit, tokens);
                unit.note_line(sec, at);
            }
        }
        if (!target.finished())
            unit.error("END expected");
        unit.resolve(target);
        if (!target.again() && !unit.moved())
            break;
        if (pass >= 200) {
            unit.error("the layout did not settle in 200 passes");
            break;
        }
    }
    if (!unit.errors.empty())
        return false;
    std::string err;
    if (!codeview(unit, input, output, err)) {
        unit.errors.push_back(err);
        return false;
    }
    if (!writer.write(unit, output, err)) {
        unit.errors.push_back(err);
        return false;
    }
    return true;
}

}
