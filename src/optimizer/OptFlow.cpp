#include "OptFlow.h"

namespace opt {

// A jcc falls through and jmp does not; ret leaves, and jmp through a
// register goes where nothing here can follow.
Control controlOf(const MInst &i) {
    Control c;
    const Opcode::Kind kind = i.m.row().kind;
    if (kind == Opcode::Ret) { c.ends = true; c.falls = false; c.returns = true; return c; }
    if (kind == Opcode::Call) { c.calls = true; return c; }
    if (kind != Opcode::Jmp && kind != Opcode::Jcc) return c;
    c.ends = true;
    c.falls = kind != Opcode::Jmp;
    if (i.a.kind == MOperand::Label) c.target = i.a.text.str();
    return c;
}

}
