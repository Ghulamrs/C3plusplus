#include "OptStrings.h"

#include <atomic>
#include <map>
#include <mutex>
#include <stdexcept>
#include <unordered_map>

namespace opt {

namespace {
// **The registry a StrId indexes**: written under a lock, read with none, as the opcode table's is (OptTable.cpp) - a
// slot published once its string is complete, in chunks of 4,096 allocated as they fill. Id 0 is the empty text.
constexpr unsigned kChunk = 4096, kChunks = 4096;
std::atomic<std::atomic<const std::string *> *> gChunks[kChunks];
std::mutex gIntern;
std::map<std::string, unsigned> &gByName() { static std::map<std::string, unsigned> m; return m; }
unsigned gCount = 1;            // id 0 is the empty text and has no slot

unsigned internLocked(const std::string &s) {
    const auto it = gByName().find(s);
    if (it != gByName().end()) return it->second;
    const unsigned id = gCount++;
    if (id / kChunk >= kChunks) throw std::length_error("opt::StrId: more operand texts than the registry holds");
    std::atomic<const std::string *> *chunk = gChunks[id / kChunk].load(std::memory_order_acquire);
    if (chunk == nullptr) {
        chunk = new std::atomic<const std::string *>[kChunk]();
        gChunks[id / kChunk].store(chunk, std::memory_order_release);
    }
    chunk[id % kChunk].store(new std::string(s), std::memory_order_release);
    gByName()[s] = id;
    return id;
}

unsigned intern(const std::string &s) {
    if (s.empty()) return 0;
    thread_local std::unordered_map<std::string, unsigned> seen;
    const auto hit = seen.find(s);
    if (hit != seen.end()) return hit->second;
    std::lock_guard<std::mutex> hold(gIntern);
    const unsigned id = internLocked(s);
    seen[s] = id;
    return id;
}

const std::string &emptyText() { static const std::string e; return e; }
}

StrId::StrId(const std::string &s) : id_(intern(s)) {}
StrId::StrId(const char *s) : id_(intern(s)) {}
const std::string &StrId::str() const {
    if (id_ == 0) return emptyText();
    return *gChunks[id_ / kChunk].load(std::memory_order_acquire)[id_ % kChunk].load(std::memory_order_acquire);
}

}
