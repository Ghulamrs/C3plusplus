// The C6000's MIR (C6xMir.h): its operands read from and spelled as TI's assembly, and the printer of a stream.

#include "C6xMir.h"
#include "C6xModel.h"

#include <cctype>
#include <ostream>

namespace c6x {

std::string regName(int r) { return (r < 32 ? "A" : "B") + std::to_string(r & 31); }

int regNumber(const std::string &s) {
    if (s.size() < 2 || s.size() > 3 || (s[0] != 'A' && s[0] != 'B')) return kNoReg;
    int n = 0;
    for (std::size_t i = 1; i < s.size(); i++) {
        if (!std::isdigit(static_cast<unsigned char>(s[i]))) return kNoReg;
        n = n * 10 + (s[i] - '0');
    }
    if (n > 31 || (s.size() == 3 && s[1] == '0')) return kNoReg;
    return n + (s[0] == 'B' ? 32 : 0);
}

namespace {

bool number(const std::string &s, long long &v) {
    const std::size_t from = !s.empty() && s[0] == '-' ? 1 : 0;
    if (s.size() == from || s.size() - from > 18) return false;
    v = 0;
    for (std::size_t i = from; i < s.size(); i++) {
        if (!std::isdigit(static_cast<unsigned char>(s[i]))) return false;
        v = v * 10 + (s[i] - '0');
    }
    if (from != 0) v = -v;
    return true;
}

// `*`, a sign or a step, the base, a step after it, then `(n)`, `[n]` or a register in either.
bool memory(const std::string &s, MOperand &o) {
    std::size_t i = 1;
    o.mode = MOperand::Base;
    if (s.compare(i, 2, "++") == 0) { o.mode = MOperand::PreInc; i += 2; }
    else if (s.compare(i, 2, "--") == 0) { o.mode = MOperand::PreDec; i += 2; }
    else if (s.compare(i, 1, "+") == 0) { o.mode = MOperand::Plus; i += 1; }
    else if (s.compare(i, 1, "-") == 0) { o.mode = MOperand::Minus; i += 1; }
    std::size_t e = i;
    while (e < s.size() && std::isalnum(static_cast<unsigned char>(s[e]))) e++;
    const int base = regNumber(s.substr(i, e - i));
    if (base == kNoReg) return false;
    o.reg = static_cast<std::uint8_t>(base);
    if (o.mode == MOperand::Base && s.compare(e, 2, "++") == 0) { o.mode = MOperand::PostInc; e += 2; }
    else if (o.mode == MOperand::Base && s.compare(e, 2, "--") == 0) { o.mode = MOperand::PostDec; e += 2; }
    if (e == s.size()) return true;
    const char close = s[e] == '(' ? ')' : s[e] == '[' ? ']' : 0;
    if (close == 0 || s[s.size() - 1] != close) return false;
    const std::string in = s.substr(e + 1, s.size() - e - 2);
    o.hasOffset = true;
    o.scaled = close == ']';
    const int idx = regNumber(in);
    if (idx != kNoReg) { o.index = static_cast<std::uint8_t>(idx); return true; }
    return number(in, o.value);
}

}   // namespace

MOperand::MOperand(const char *s) { set(s); }
MOperand::MOperand(const std::string &s) { set(s); }

void MOperand::set(const std::string &s) {
    const std::size_t colon = s.find(':');
    const int r = regNumber(s);
    if (r != kNoReg) { kind = Register; reg = static_cast<std::uint8_t>(r); }
    else if (number(s, value)) kind = Immediate;
    else if (!s.empty() && s[0] == '*' && memory(s, *this)) kind = Memory;
    else if (colon != std::string::npos && regNumber(s.substr(0, colon)) != kNoReg && regNumber(s.substr(colon + 1)) != kNoReg) {
        kind = Pair;
        hi = static_cast<std::uint8_t>(regNumber(s.substr(0, colon)));
        reg = static_cast<std::uint8_t>(regNumber(s.substr(colon + 1)));
    }
    if (kind != None && str() == s) return;
    *this = MOperand();
    kind = Symbol;
    text = opt::StrId(s);
}

std::string MOperand::str() const {
    switch (kind) {
    case None: return std::string();
    case Register: return regName(reg);
    case Pair: return regName(hi) + ":" + regName(reg);
    case Immediate: return std::to_string(value);
    case Symbol: return text.str();
    case Memory: break;
    }
    static const char *const before[] = { "", "+", "-", "++", "--", "", "" };
    std::string s = std::string("*") + before[mode] + regName(reg);
    if (mode == PostInc) s += "++";
    if (mode == PostDec) s += "--";
    if (!hasOffset) return s;
    return s + (scaled ? "[" : "(") + (index != kNoReg ? regName(index) : std::to_string(value)) + (scaled ? "]" : ")");
}

MInst::MInst(const char *mnem, const MOperand &a, const MOperand &b, const MOperand &c, const MOperand &d) : m(mnem) {
    op[0] = a; op[1] = b; op[2] = c; op[3] = d;
    while (operands < 4 && op[operands].kind != MOperand::None) operands++;
}

int MInst::slots() const { return kind == Instruction ? delaySlots(m.str()) : 0; }

int MInst::side() const {
    for (int k = operands; k-- > 0; )
        if (op[k].kind == MOperand::Register || op[k].kind == MOperand::Pair) return sideOfReg(op[k].reg);
    return -1;
}

std::string MInst::str() const {
    if (kind == Verbatim) return m.str();
    if (kind == Label) return m.str() + ":\n";
    std::string s = "\t";
    if (pred != kNoReg) s += std::string(predNot ? "[!" : "[") + regName(pred) + "]\t";
    s += m.str();
    for (int k = 0; k < operands; k++) s += (k == 0 ? "\t" : ", ") + op[k].str();
    return s + "\n";
}

void read(const std::string &text, MStream &to) {
    for (std::size_t at = 0; at < text.size(); ) {
        const std::size_t nl = text.find('\n', at);
        const std::size_t end = nl == std::string::npos ? text.size() : nl + 1;
        const std::string raw = text.substr(at, end - at);
        at = end;
        MInst i;
        i.kind = MInst::Verbatim;
        i.m = opt::StrId(raw);
        const std::size_t n = raw.size();
        if (n >= 3 && raw[0] != '\t' && raw[n - 2] == ':' && raw[n - 1] == '\n') {
            i.kind = MInst::Label;
            i.m = opt::StrId(raw.substr(0, n - 2));
        } else if (n >= 3 && raw[0] == '\t' && raw[n - 1] == '\n') {
            // C6xSched's reading of a line (parse), then each operand's own.
            const Line l = parse(raw.substr(0, n - 1));
            if (!l.mnem.empty() && l.ops.size() <= 4) {
                MInst x;
                x.m = opt::StrId(l.mnem);
                if (!l.pred.empty()) {
                    x.predNot = l.pred[0] == '!';
                    x.pred = static_cast<std::uint8_t>(regNumber(l.pred.substr(x.predNot ? 1 : 0)));
                }
                for (std::size_t k = 0; k < l.ops.size(); k++) x.op[x.operands++] = MOperand(l.ops[k]);
                if (x.str() == raw) i = x;
            }
        }
        to.push_back(i);
    }
}

void write(const MStream &s, std::ostream &out) {
    for (std::size_t k = 0; k < s.size(); k++) out << s[k].str();
}

Census census(const MStream &s) {
    Census c;
    for (std::size_t k = 0; k < s.size(); k++) {
        const MInst &i = s[k];
        if (i.kind == MInst::Label) { c.labels++; continue; }
        if (i.kind == MInst::Verbatim) { c.verbatim++; continue; }
        c.instructions++;
        const bool jumps = isBranch(i.m.str()) || startsWith(i.m.str(), "CALL");
        for (int n = 0; n < i.operands; n++)
            if (i.op[n].kind == MOperand::Symbol && !(jumps && n == 0)) c.opaque++;
    }
    return c;
}

}   // namespace c6x
