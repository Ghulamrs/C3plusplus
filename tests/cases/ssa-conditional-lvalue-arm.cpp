// A const local copy-initialised from `p ? *p : T()` - an lvalue on one arm, a temporary on the other.
#include <cstdio>
#include <string>
#include <vector>

struct Member { std::vector<int> pack; std::string name; };
typedef std::vector<Member> Packs;

struct Deducer {
    int calls;
    Deducer();
    bool one(const int *pattern, const int *arg, std::vector<const int *> *binding, std::vector<long long> *values,
             std::string *why, Packs *packs, bool nested = false) const;
    bool from(const int *param, const int *arg, std::vector<const int *> *binding, std::vector<long long> *values,
              std::string *why, Packs *packs);
};

Deducer::Deducer() : calls(0) {}

bool Deducer::one(const int *pattern, const int *arg, std::vector<const int *> *binding, std::vector<long long> *values,
                  std::string *why, Packs *packs, bool nested) const {
    std::printf("one: pattern %d arg %d binding %d values %d packs %d nested %d\n", *pattern, *arg,
                static_cast<int>(binding->size()), values ? static_cast<int>(values->size()) : -1,
                packs ? static_cast<int>(packs->size()) : -1, nested ? 1 : 0);
    if ((*binding)[0] == nullptr) { (*binding)[0] = arg; }
    if (values) values->push_back(*arg);
    if (packs) { Member m; m.pack.push_back(*arg); m.name = "added"; packs->push_back(m); }
    if (*pattern != *arg) { *why = "they differ"; return false; }
    return true;
}

bool Deducer::from(const int *param, const int *arg, std::vector<const int *> *binding, std::vector<long long> *values,
                   std::string *why, Packs *packs) {
    calls++;
    const std::vector<const int *> before = *binding;
    const std::vector<long long> beforeValues = values ? *values : std::vector<long long>();
    const Packs beforePacks = packs ? *packs : Packs();
    if (one(param, arg, binding, values, why, packs)) return true;
    if (packs) *packs = beforePacks;
    std::vector<const int *> tryBinding = before;
    std::vector<long long> tryValues = beforeValues;
    Packs tryPacks = beforePacks;
    std::string ignored;
    if (!one(arg, arg, &tryBinding, values ? &tryValues : nullptr, &ignored, packs ? &tryPacks : nullptr)) return false;
    *binding = tryBinding;
    if (values) *values = tryValues;
    if (packs) *packs = tryPacks;
    return true;
}

int main() {
    Deducer d;
    int three = 3, four = 4;
    for (int round = 0; round < 4; round++) {
        std::vector<const int *> binding(2);
        std::vector<long long> values;
        values.push_back(7);
        Packs packs;
        Member m; m.name = "first"; m.pack.push_back(1);
        packs.push_back(m);
        std::string why;
        const bool same = d.from(&three, round % 2 ? &four : &three, &binding, round & 2 ? &values : nullptr, &why,
                                 round & 2 ? &packs : nullptr);
        std::printf("round %d: %d why '%s' binding %d values %d packs %d last '%s'\n", round, same ? 1 : 0, why.c_str(),
                    binding[0] ? *binding[0] : -1, static_cast<int>(values.size()), static_cast<int>(packs.size()),
                    packs.back().name.c_str());
    }
    std::printf("calls %d\n", d.calls);
    return 0;
}
