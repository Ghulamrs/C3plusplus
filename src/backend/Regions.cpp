#include "Regions.h"

#include <algorithm>

#include "../Source.h"

void Regions::markLine(std::size_t pos) {
    if (lines_ == nullptr) return;

    if (pos == 0) return;
    Source::Place at = lines_->locate(pos);
    std::size_t before = emittedSize();
    emitLoc(at.file + 1, at.line, at.column);

    notCode_ += emittedSize() - before;
}

std::size_t Regions::openingBrace(std::size_t pos) const {
    if (lines_ == nullptr) return pos;
    const std::string &text = lines_->text();
    int depth = 0;
    for (std::size_t at = pos; at < text.size(); at++) {
        const char c = text[at];
        if (c == '(') depth++;
        else if (c == ')') depth--;
        else if (c == ';' && depth == 0) return pos;
        else if (c == '{' && depth == 0) return at;
    }
    return pos;
}

void Regions::resetBlocks(const std::vector<int> &parents) {
    blocks_.clear();
    blockState_.clear();
    marks_.clear();
    notCode_ = 0;
    for (std::size_t i = 0; i < parents.size(); i++) {
        DwarfBlock b;
        b.parent = parents[i];
        blocks_.push_back(b);
    }
}

void Regions::openBlock(int scope) {
    if (lines_ == nullptr || scope <= 0) return;
    if (static_cast<std::size_t>(scope) >= blocks_.size()) return;
    std::size_t before = emittedSize();
    blocks_[scope].begin = label("blk.b", scope);
    blocks_[scope].funclet = funcletNo_;
    defineLabel(blocks_[scope].begin);
    notCode_ += emittedSize() - before;
    Mark m;
    m.size = emittedSize();
    m.notCode = notCode_;
    marks_.push_back(m);
}

void Regions::markBlock(int scope, bool close) {
    if (lines_ == nullptr || scope <= 0 || static_cast<std::size_t>(scope) >= blocks_.size()) return;
    if (blockState_.size() < blocks_.size()) blockState_.resize(blocks_.size(), 0);
    if (!close && blockState_[scope] == 0) { openBlock(scope); blockState_[scope] = 1; }
    else if (close && blockState_[scope] == 1 && blocks_[scope].funclet == funcletNo_) { closeBlock(scope); blockState_[scope] = 2; }
}

void Regions::closeOpenBlocks() {
    for (std::size_t s = blockState_.size(); s-- > 1; )
        if (blockState_[s] == 1 && blocks_[s].funclet == funcletNo_) markBlock(static_cast<int>(s), true);
}

void Regions::closeBlock(int scope) {
    if (lines_ == nullptr || scope <= 0) return;
    if (static_cast<std::size_t>(scope) >= blocks_.size()) return;
    if (marks_.empty()) return;
    Mark m = marks_.back();
    marks_.pop_back();
    bool code = (emittedSize() - m.size) > (notCode_ - m.notCode);

    std::size_t before = emittedSize();
    blocks_[scope].end = label("blk.e", scope);
    defineLabel(blocks_[scope].end);
    notCode_ += emittedSize() - before;

    if (!code) {
        blocks_[scope].begin.clear();
        blocks_[scope].end.clear();
    }
}


// **The shape is the same on both targets, so it lives here.**
void Regions::openRegion(const std::string &begin,
                        const std::vector<std::string> &types,
                        const std::vector<int> &indices) {
    if (!open_.empty()) {
        OpenRegion &outer = open_.back();
        CallSite piece;
        piece.begin = outer.start;
        piece.end = begin;
        piece.at = outer.at;
        outer.closed.push_back(piece);
    }
    OpenRegion mine;
    mine.start = begin;
    mine.at = ++labelOrder_;
    mine.types = types;
    mine.indices = indices;
    open_.push_back(mine);
}

std::vector<Regions::CallSite> Regions::closeRegion(const std::string &end, const std::string &resume,
                                                  const std::set<std::string> &exits,
                                                  const std::set<std::string> &defines) {
    OpenRegion mine = open_.back();
    open_.pop_back();
    CallSite last;
    last.begin = mine.start;
    last.end = end;
    last.at = mine.at;
    mine.closed.push_back(last);
    // A pad jumped into ran by the jump, so its region hands over too; the regions outside it do not.
    std::set<std::string> left = mine.innerExits;
    left.insert(exits.begin(), exits.end());
    const bool any = !left.empty();
    for (const std::string &l : defines) left.erase(l);
    for (CallSite &piece : mine.closed) piece.handsOver = any;
    if (!open_.empty()) {
        open_.back().start = resume;
        open_.back().at = ++labelOrder_;
        open_.back().innerExits.insert(left.begin(), left.end());
    }
    return mine.closed;
}
// **One table, two spellings.** Every byte below was measured against clang for
// both Itanium targets and was written out twice by hand until now; what differs
// is in LsdaSpelling, where the next reader can see the whole of it.
std::string Regions::lsdaTable(const LsdaSpelling &sp, const std::string &symbol,
                              std::vector<std::string> &types) const {
    const std::string L = sp.label;
    const std::string ex = L + "exception." + symbol;
    const std::string ttbase = L + "ttbase." + symbol;
    const std::string ttref = L + "ttbaseref." + symbol;
    const std::string cstBegin = L + "cst.begin." + symbol;
    const std::string cstEnd = L + "cst.end." + symbol;
    const std::string fnBegin = L + "func.begin." + symbol;
    const std::string fnEnd = L + "func.end." + symbol;

    std::string o;
    o += std::string("  ") + sp.section + "\n";
    o += "  .p2align 2\n";
    // A label that is not a temporary, where the linker cuts sections at symbols:
    // with only temporaries the second table in a file was never reached.
    if (sp.atomSymbol) o += "GCC_except_table." + symbol + ":\n";
    o += ex + ":\n";
    o += "  .byte 255\n";                 // LPStart omitted: pads are function-relative
    o += "  .byte 155\n";                 // the type table is indirect, pc-relative
    o += "  .uleb128 " + ttbase + "-" + ttref + "\n";
    o += ttref + ":\n";
    o += "  .byte 1\n";                   // the call-site table is uleb128
    o += "  .uleb128 " + cstEnd + "-" + cstBegin + "\n";
    o += cstBegin + ":\n";

    // **Every call in the function is a row, the ones outside a try
    // included**: a miss makes libc++abi call terminate.
    int action = 1;
    std::string at = fnBegin;
    // A terminate row catches anything, under a type index past the
    // parser's; its pad ends the program.
    int catchAll = 0;
    for (std::size_t i = 0; i < callSites().size(); i++)
        for (std::size_t k = 0; k < callSites()[i].indices.size(); k++)
            if (callSites()[i].indices[k] > catchAll) catchAll = callSites()[i].indices[k];
    catchAll++;
    // **A `throw(T...)` list is a filter**: its types go into the type table past every other index, and
    // the list into the specification table after ttbase, which a negative filter counts into from 1.
    std::vector<std::string> specs;
    std::vector<std::vector<int> > specIndices;
    std::vector<std::string> specTypes;
    int nextIndex = catchAll;
    for (const CallSite &c : callSites())
        for (const std::string &t : c.types) {
            if (t.compare(0, 5, "$spec") != 0) continue;
            if (std::find(specs.begin(), specs.end(), t) != specs.end()) continue;
            specs.push_back(t);
            specIndices.push_back(std::vector<int>());
            for (std::size_t from = 6; from <= t.size(); ) {
                std::size_t to = t.find(',', from);
                if (to == std::string::npos) to = t.size();
                if (to > from) {
                    specIndices.back().push_back(++nextIndex);
                    specTypes.push_back(t.substr(from, to - from));
                }
                from = to + 1;
            }
        }
    auto filterOf = [&](const std::string &t) {
        int offset = 0;
        for (std::size_t i = 0; i < specs.size() && specs[i] != t; i++)
            offset += static_cast<int>(specIndices[i].size()) + 1;
        return -(offset + 1);
    };
    // **Sorted by address, which is safe now and was not before.**
    std::vector<CallSite> rows = callSites();
    for (std::size_t i = 1; i < rows.size(); i++)
        for (std::size_t j = i; j > 0 && rows[j - 1].at > rows[j].at; j--) {
            CallSite t = rows[j - 1]; rows[j - 1] = rows[j]; rows[j] = t;
        }
    for (std::size_t i = 0; i < rows.size(); i++) {
        const CallSite &c = rows[i];
        o += "  .uleb128 " + at + "-" + fnBegin + "\n";
        o += "  .uleb128 " + c.begin + "-" + at + "\n";
        o += "  .byte 0\n";
        o += "  .byte 0\n";
        o += "  .uleb128 " + c.begin + "-" + fnBegin + "\n";
        o += "  .uleb128 " + c.end + "-" + c.begin + "\n";
        o += "  .uleb128 " + c.pad + "-" + fnBegin + "\n";
        // No handler at all is a *cleanup*.
        o += "  .uleb128 " + std::to_string(c.types.empty() && !c.terminate ? 0 : action) + "\n";
        // **A `try` inside a cleanup region carries one record more.**
        action += 2 * static_cast<int>(c.types.size() + (c.cleanup ? 1 : 0) + (c.terminate ? 1 : 0));
        at = c.end;
    }
    o += "  .uleb128 " + at + "-" + fnBegin + "\n";
    o += "  .uleb128 " + fnEnd + "-" + at + "\n";
    o += "  .byte 0\n";
    o += "  .byte 0\n";
    o += cstEnd + ":\n";

    // The action table: a type index and the offset to the next record, 0
    // saying there is no next, so the chain ends and the exception goes on
    // unwinding. **The index is the parser's, not this table's.**
    types.clear();
    if (!specTypes.empty()) types.resize(specTypes.size() + static_cast<std::size_t>(catchAll));
    for (std::size_t i = 0; i < specTypes.size(); i++)
        types[static_cast<std::size_t>(catchAll) + i] = specTypes[i];
    for (std::size_t i = 0; i < rows.size(); i++) {
        const CallSite &c = rows[i];
        if (c.terminate) {
            if (types.size() < static_cast<std::size_t>(catchAll)) types.resize(static_cast<std::size_t>(catchAll));
            types[static_cast<std::size_t>(catchAll) - 1] = std::string();
            o += "  .byte " + std::to_string(catchAll) + "\n";
            o += "  .byte 0\n";
            continue;
        }
        for (std::size_t k = 0; k < c.types.size(); k++) {
            if (c.types[k].compare(0, 5, "$spec") == 0) {
                // One byte, as long as the specification table stays under 64.
                o += "  .byte " + std::to_string(128 + filterOf(c.types[k])) + "\n";
                const bool more = k + 1 < c.types.size() || c.cleanup;
                o += "  .byte " + std::string(more ? "1" : "0") + "\n";
                continue;
            }
            const std::size_t at = k < c.indices.size()
                                       ? static_cast<std::size_t>(c.indices[k])
                                       : types.size() + 1;
            if (types.size() < at) types.resize(at);
            types[at - 1] = c.types[k];
            o += "  .byte " + std::to_string(at) + "\n";
            const bool more = k + 1 < c.types.size() || c.cleanup;
            o += "  .byte " + std::string(more ? "1" : "0") + "\n";
        }
        // Filter 0 is "cleanup": not a handler, but a reason to stop here in
        // phase 2 and run the pad. It ends the chain.
        if (c.cleanup && !c.types.empty()) {
            o += "  .byte 0\n";
            o += "  .byte 0\n";
        }
    }
    o += "  .p2align 2\n";
    // **Written backwards**: index 1 is the entry just before Lttbase.
    for (std::size_t i = types.size(); i-- > 0; ) {
        const std::string here = L + "ti." + symbol + "." + std::to_string(i);
        o += here + ":\n";
        if (types[i].empty()) o += "  .long 0\n";          // catch (...)
        else o += "  .long " + std::string(sp.typePrefix) + types[i] +
                  sp.typeSuffix + "-" + here + "\n";
    }
    o += ttbase + ":\n";
    for (std::size_t i = 0; i < specs.size(); i++) {
        for (int ix : specIndices[i]) o += "  .uleb128 " + std::to_string(ix) + "\n";
        o += "  .byte 0\n";
    }
    o += "  .p2align 2\n";
    return o;
}
