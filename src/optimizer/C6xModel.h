#pragma once

// The C6000 machine model the text passes share: a parsed line, the scheduler's view of an instruction, an execute
// packet being filled, and the readers of the ISA - delay slots, units, cross paths - written once in C6xSched.cpp
// and read by the software pipeliner in C6xPipe.cpp.

#include <cstdint>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace c6x {

enum { UL = 1, US = 2, UD = 4, UM = 8 };   // the units, as a bitmask

// Every edit of the text - a line copied, or assigned over another - counted, for liveness to tell it is stale.
extern thread_local std::uint64_t textEdits;

struct LineFields {
    std::string raw;
    bool instr = false;
    bool verbatim = false;              // scheduled already: printed as it stands, never rescheduled
    std::string pred;                   // "A1" or "!A1", read as A1
    std::string mnem;
    std::vector<std::string> ops;
    std::uint64_t liveOut = ~0ull;      // the registers live at the end of this line's block
    std::uint64_t liveIn = ~0ull;       // and at its start
};

// A line of the text; an assignment over one, or a copy of one, is an edit, which is how a vector of them is edited.
struct Line : LineFields {
    Line() = default;
    Line(Line &&) = default;
    Line(const Line &o) : LineFields(o) { textEdits++; }
    Line &operator=(const Line &o) { LineFields::operator=(o); textEdits++; return *this; }
    Line &operator=(Line &&o) { LineFields::operator=(std::move(o)); textEdits++; return *this; }
};

// One instruction as the scheduler sees it: what it reads and writes, its
// latency, the units it has a form on, its side and cross path, its memory
// access, and the edges from the earlier instructions it must follow.
struct Node {
    Line line;
    std::uint64_t reads = 0, writes = 0;
    std::uint64_t stepped = 0;          // the address register a `*R++` or `*R--` access writes, in E1 rather than at its latency
    int lat = 1;
    int late = 0;                       // the cycles after issue its sources are still read, and its unit held
    unsigned units = 0;                 // a bitmask of UL, US, UD, UM; 0 for a form the table does not know
    int side = -1;                      // 0 A, 1 B, -1 either (a branch to a label)
    int cross = -1;                     // the cross path used, by the side that reads: 0 1X, 1 2X, -1 none
    bool mem = false, store = false;
    int tpath = -1;                     // a load's or store's data path: the data register's side
    std::string memBase; long memOff = 0; int memSize = 0; bool memFrame = false;
    bool branch = false, call = false;
    std::vector<std::pair<int, int> > preds;   // (earlier node, the least distance in cycles)
    int issue = -1, height = 0;
};

// The packet being filled at one cycle: the units taken on each side, the
// cross paths, the data paths, the registers written and the branch.
struct Packet {
    bool unit[2][4] = { { false, false, false, false }, { false, false, false, false } };
    bool held[2][4] = { { false, false, false, false }, { false, false, false, false } };   // by an earlier cycle's DP instruction
    bool cross[2] = { false, false }, tpath[2] = { false, false };
    std::uint64_t writes = 0;
    bool branch = false, alone = false;
    int count = 0;
    std::vector<const Node *> members;
};

// What a scheduled block is written from: a packet's member by its place in the block, or idle cycles (node -1).
struct Row { int node; int nops; bool par; };
struct Pending { std::uint64_t writes; int lands; };   // a write in flight where a block ends, for the one fallen into

bool startsWith(const std::string &s, const char *p);
bool endsWith(const std::string &s, const char *p);
int delaySlots(const std::string &m);
int lateReads(const std::string &m);
Line parse(const std::string &raw);
Line make(const std::string &mnem, const std::string &a, const std::string &b = "", const std::string &c = "");
Line rebuilt(const std::string &mnem, const std::vector<std::string> &ops, const std::string &pred = "");
void registersIn(const std::string &op, std::vector<std::string> &out);
bool isNameChar(char c);
bool isStore(const std::string &m);
bool isLoad(const std::string &m);
char sideOf(const std::string &r);
int sideIndex(const std::string &r);
std::uint64_t bitOf(const std::string &r);
std::uint64_t maskOf(const std::vector<std::string> &regs);
void readsAndWrites(const Line &l, std::vector<std::string> &reads, std::vector<std::string> &writes);
bool has(const std::vector<std::string> &v, const std::string &r);
bool isNumber(const std::string &s);
bool isLabel(const Line &l);
std::string labelName(const Line &l);
bool isBranch(const std::string &m);
bool blockEnd(const Line &l);
bool isCall(const Line &l, const std::set<std::string> &labels);
bool renameable(const Line &l);
bool isCallTo(const std::string &m, const std::string &target, const std::set<std::string> &labels);
bool isReturnTo(const std::string &m, const std::string &target, const std::set<std::string> &labels);
// What liveness reads of a line or a stream's entry: the blocks, the flow and the registers, by each form's reader.
struct LiveFacts { bool instr = false, label = false, kept = false, predicated = false; std::string name, mnem, target; std::uint64_t reads = 0, writes = 0; };
void liveness(const std::vector<LiveFacts> &v, std::vector<std::uint64_t> &liveIn, std::vector<std::uint64_t> &liveOut);
LiveFacts liveFacts(const Line &l);
void computeLiveness(std::vector<Line> &v);
// Under CXX1_C6X_MIR=1 the lines' liveness held against the stream's (C6xPack.cpp): `c6x-live` names a line that differs.
void checkLiveness(const std::vector<Line> &v);
// The liveness of this text, recomputed first where an edit since makes it stale - unless a LivenessHeld is open.
std::uint64_t liveOutAt(const std::vector<Line> &v, std::size_t i);
std::uint64_t liveInAt(const std::vector<Line> &v, std::size_t i);
// A pass that vouches its edits only shrink liveness reads it as computed; CPP11_C6XLIVECHECK=1 checks that it does.
struct LivenessHeld {
    LivenessHeld(const std::vector<Line> &v, const char *pass);
    ~LivenessHeld();
    const char *outer;
};
int accessSize(const std::string &m);
std::string renamed(const std::string &op, const std::string &from, const std::string &to);
int crossings(const Line &l);
unsigned unitsFor(const Line &l);
Node makeNode(const Line &l, const std::set<std::string> &labels);
// The call's registers and the cross-path positions, read by the table's readers on the stream too (C6xPack.cpp).
std::uint64_t callReads();
std::uint64_t callWrites();
bool mayCross(const std::string &m, std::size_t o);
bool pairMayCross(const std::string &m);
bool fits(const Packet &p, const Node &n);
void add(Packet &p, const Node &n);
bool passThrough(const Line &l, const std::set<std::string> &named);
bool passThroughName(const std::string &name, const std::set<std::string> &named);
bool assignUnits(const std::vector<const Node *> &m, std::size_t k, bool taken[2][4]);
// One block list-scheduled into rows, and the wait where a block is empty: read by the text pass and by C6xPack.cpp.
std::vector<Pending> scheduleBlock(std::vector<Node> &nodes, std::vector<Row> &rows, const std::vector<Pending> &in, bool carry);
void waitOut(std::vector<Pending> &pend, std::vector<Row> &rows);
std::string steppedRegister(const Line &l, long &by);
std::string pairOf(const std::string &lo, const std::string &hi);
std::string partner(const std::string &r);
bool legalForm(Line &r);

// The software pipeliner (C6xPipe.cpp): the innermost counted loops of the text rewritten as verbatim lines.
void pipelineLoops(std::vector<Line> &v);

// What the loop passes (reuseEntryLoads to widenFills, hoistConstantPairs) read and edit: `v` as lines, a line blanked
// or set, a preheader inserted, the blanks dropped, liveness asked. The text's own here; a stream's in C6xPack.cpp.
struct LoopText {
    std::vector<Line> own, &v;
    LoopText() : v(own) {}
    explicit LoopText(std::vector<Line> &lines) : v(lines) {}
    LoopText(const LoopText &) = delete;
    LoopText &operator=(const LoopText &) = delete;
    virtual ~LoopText() {}
    virtual void blank(std::size_t k) { v[k] = parse(""); }
    virtual void set(std::size_t k, const Line &l) { v[k] = l; }
    virtual std::size_t apply(std::size_t h, const std::vector<Line> &pre, std::size_t i);   // the back edge's new place
    virtual void compact();
    virtual std::uint64_t liveIn(std::size_t k) { return liveInAt(v, k); }
    virtual std::uint64_t liveOut(std::size_t k) { return liveOutAt(v, k); }
};
void reuseEntryLoads(LoopText &t);
void hoistInvariants(LoopText &t);
void inductionPointers(LoopText &t);
void widenFills(LoopText &t);
void hoistConstantPairs(LoopText &t);

}   // namespace c6x
