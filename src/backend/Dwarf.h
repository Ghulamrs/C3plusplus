#pragma once

#include "../Ast.h"
#include "../Type.h"

#include <string>
#include <vector>

struct DwarfBlock {
    int parent;
    std::string begin;
    std::string end;
    // Which funclet of the function the block's code is in - 0 for the function's own (1.29): a Microsoft
    // handler is walked as a funclet, a procedure of its own in `.text$x`, and its blocks are described there.
    int funclet = 0;
};

struct DwarfFunction {
    std::string name;
    std::string begin;
    std::string end;
    int file;
    int line;
    bool external;
    const Type *returns;
    const std::vector<Local> *locals;

    std::vector<DwarfBlock> blocks;

    // For CodeView (CodeView.h), and spelled as a table names them: the symbol,
    // the labels after the prologue and after the last instruction, whether the
    // code is a COMDAT, and the frame slot's displacement from RBP (rbp + bias - offset).
    std::string symbol;
    std::string prologEnd;
    std::string codeEnd;
    bool mergeable = false;
    int frameBias = 0;
    // A realigned frame's base register as DWARF numbers it (rbx is 3), records at `frameBias - offset` from it; -1 the frame pointer, `-offset`.
    int frameBaseReg = -1;
    // **A handler funclet, described as cl describes one** (1.29): an S_LPROC32 of its own, `` `f'::`1'::catch$0 ``, typeless, over the
    // parent's frame - `locals` and `blocks` are the parent's, `funclet` says which of its funclets this is (DwarfBlock::funclet). It
    // follows its parent in the list, and a mergeable parent's funclets share its `.debug$S` (CodeView.cpp).
    int funclet = 0;
};

struct DwarfGlobal {
    std::string name;
    std::string symbol;
    const Type *type;
    bool external;
};

struct DwarfSpelling {
    const char *abbrev;
    const char *info;

    const char *line;

    bool offsetsAreLabels;
    int frameBaseReg;
    const char *symbolPrefix;
};

extern const DwarfSpelling kElfDwarf;
extern const DwarfSpelling kMachODwarf;

void writeDwarf(std::string &out, const DwarfSpelling &sp, const Target &target,
                const std::string &file, const std::string &compDir,
                const std::vector<DwarfFunction> &fns,
                const std::vector<DwarfGlobal> &globals, Dialect dialect = Dialect::Cxx11);
