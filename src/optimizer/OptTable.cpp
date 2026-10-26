#include "OptTable.h"

#include <atomic>
#include <cstring>
#include <deque>
#include <map>
#include <mutex>
#include <stdexcept>
#include <unordered_map>

namespace opt {

namespace {

using K = Opcode;
constexpr unsigned OPT_OPCODE_FLAGS_ABBREVIATED;

// **The table**, one row per line of OPT_OPCODES in OptTable.h - the same list the mn:: enumerators are made from, so
// a row's index and its enumerator cannot drift apart.
#define OPT_ROW(id, att, kind, flags, width, cc, masm, mw) {att, K::kind, flags, width, cc, masm, mw},
const Opcode kTable[] = { OPT_OPCODES(OPT_ROW) };
#undef OPT_ROW
// Quirks the table records as it found them, each a candidate for a later measured change (M3a.3): `addq`/`subq` name
// a width but are Unknown, `sal`/`rol`/`ror` are shifts by flag and opaque by kind, `testb/l/q` are not kExplicit where
// `test` is; and a row the MASM spelling alone knew (`addb`, `incq`, `nop` ...) is Unknown with no flags, as it was unlisted.

// The families a prefix names, for a mnemonic not listed above.
const Opcode kMovFamily = {"mov*", K::Unknown, E | W, 0, kNoCond, nullptr, 0};
const Opcode kSetFamily = {"set*", K::Unknown, E | W, 0, kNoCond, nullptr, 0};
const Opcode kSetcc = {"setcc", K::Setcc, E | W, 0, kNoCond, nullptr, 0};
const Opcode kJcc = {"jcc", K::Jcc, 0, 0, kNoCond, nullptr, 0};
const Opcode kCvtFamily = {"cvt*", K::Move, M | C, 0, kNoCond, nullptr, 0};
const Opcode kX87Family = {"f*", K::X87, N, 0, kNoCond, nullptr, 0};
const Opcode kUnknown = {"?", K::Unknown, 0, 0, kNoCond, nullptr, 0};

bool starts(const std::string &m, const char *p) { return m.compare(0, std::strlen(p), p) == 0; }

// The conditions jcc and setcc spell, each with its opposite, in Cond's order.
struct CondName { const char *cc; Cond inv; };
const CondName kCondNames[kConds] = {
    {"e", CcNe}, {"ne", CcE}, {"z", CcNz}, {"nz", CcZ}, {"l", CcGe}, {"ge", CcL},
    {"le", CcG}, {"g", CcLe}, {"b", CcAe}, {"ae", CcB}, {"be", CcA}, {"a", CcBe},
    {"s", CcNs}, {"ns", CcS}, {"p", CcNp}, {"np", CcP}, {"o", CcNo}, {"no", CcO},
};

// The spelling of a condition, for the family fallback on a mnemonic the table does not list.
Cond condNamed(const std::string &m) {
    std::string rest;
    if (m.size() > 1 && m[0] == 'j' && m != "jmp") rest = m.substr(1);
    else if (starts(m, "set")) rest = m.substr(3);
    else return kNoCond;
    for (int c = 0; c < kConds; ++c) if (rest == kCondNames[c].cc) return static_cast<Cond>(c);
    return kNoCond;
}

}

const Opcode &opcodeOf(const std::string &m) {
    // Built once by a static's initialiser, which C++11 makes thread-safe; filled lazily, two jobs raced (review P4).
    static const std::map<std::string, const Opcode *> index = [] {
        std::map<std::string, const Opcode *> made;
        for (const Opcode &o : kTable) made[o.name] = &o;
        return made;
    }();
    const auto it = index.find(m);
    if (it != index.end()) return *it->second;
    if (condNamed(m) != kNoCond) return m[0] == 'j' ? kJcc : kSetcc;
    if (starts(m, "mov")) return kMovFamily;
    if (starts(m, "set")) return kSetFamily;
    if (starts(m, "cvt")) return kCvtFamily;
    if (m[0] == 'f') return kX87Family;
    return kUnknown;
}

Cond inverse(Cond cc) { return cc == kNoCond ? kNoCond : kCondNames[cc].inv; }

// The jcc and setcc rows by condition, found once in the table.
namespace {
struct CondRows { OpId jcc[kConds], setcc[kConds]; };
const CondRows &condRows() {
    static const CondRows rows = [] {
        CondRows r;
        for (unsigned i = 0; i < sizeof(kTable) / sizeof(kTable[0]); ++i) {
            const Opcode &o = kTable[i];
            if (o.cc == kNoCond) continue;
            (o.kind == K::Jcc ? r.jcc : r.setcc)[o.cc] = OpId(static_cast<mn::Mn>(i + 1));
        }
        return r;
    }();
    return rows;
}
}
OpId jcc(Cond cc) { return condRows().jcc[cc]; }
OpId setcc(Cond cc) { return condRows().setcc[cc]; }

// **The registry an OpId indexes**: written once per new mnemonic under a lock, read with no lock - a slot is
// published after its record is complete - since the passes ask a mnemonic's name at every turn.
namespace {
struct Mnemonic {
    std::string name;
    const Opcode *row;
    bool listed;
    Mnemonic(const std::string &n, const Opcode *r, bool l) : name(n), row(r), listed(l) {}
};
constexpr unsigned kMaxMnemonics = 1u << 16;
std::atomic<const Mnemonic *> gSlots[kMaxMnemonics];
std::mutex gIntern;
std::map<std::string, unsigned> &gByName() { static std::map<std::string, unsigned> m; return m; }
std::deque<Mnemonic> &gRecords() { static std::deque<Mnemonic> d; return d; }   // the owner: no element moves (R6)
unsigned gCount = 0;

unsigned publish(const std::string &m, const Opcode *row, bool listed) {
    if (gCount >= kMaxMnemonics) throw std::length_error("opt::OpId: more mnemonics than the registry holds");
    const unsigned id = gCount++;
    gRecords().emplace_back(m, row, listed);
    gSlots[id].store(&gRecords().back(), std::memory_order_release);
    gByName()[m] = id;
    return id;
}

unsigned internLocked(const std::string &m) {
    static const bool seeded = [] {
        publish(std::string(), &kUnknown, true);
        for (const Opcode &o : kTable) publish(o.name, &o, true);
        return true;
    }();
    (void)seeded;
    const auto it = gByName().find(m);
    if (it != gByName().end()) return it->second;
    return publish(m, &opcodeOf(m), false);
}

unsigned intern(const std::string &m) {
    thread_local std::unordered_map<std::string, unsigned> seen;
    const auto hit = seen.find(m);
    if (hit != seen.end()) return hit->second;
    std::lock_guard<std::mutex> hold(gIntern);
    const unsigned id = internLocked(m);
    seen[m] = id;
    return id;
}

const Mnemonic &at(unsigned id) {
    const Mnemonic *m = gSlots[id].load(std::memory_order_acquire);
    if (m == nullptr) {         // read before anything was interned: the rows are seeded first
        std::lock_guard<std::mutex> hold(gIntern);
        internLocked(std::string());
        m = gSlots[id].load(std::memory_order_acquire);
    }
    return *m;
}
}

OpId::OpId(const std::string &m) : id_(intern(m)) {}
const std::string &OpId::name() const { return at(id_).name; }
const Opcode &OpId::row() const { return *at(id_).row; }
bool OpId::listed() const { return at(id_).listed; }

}
