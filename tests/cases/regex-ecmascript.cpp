// <regex>: the ECMAScript subset cxx3's own arm64 peephole asks of it - its patterns, a search, lazy and counted
// repetition, a negated class, alternation - and regex_error for a pattern that does not parse.
#include <regex>
#include <string>
#include <cstdio>
static void show(const char *what, const std::string &t, const std::regex &re) {
    std::smatch m;
    if (!std::regex_match(t, m, re)) { printf("%s: no\n", what); return; }
    printf("%s:", what);
    for (std::size_t i = 1; i < m.size(); i++) printf(" [%s]", m[i].matched ? m[i].str().c_str() : "-");
    printf("\n");
}
int main() {
    const std::regex kPush(R"(^str ([xd])(\d+), \[sp, #-16\]!$)");
    const std::regex kSlot(R"(^(ldur|ldursw|ldurb|ldursb|ldurh|ldursh|stur|sturb|sturh) ([wxds])(\d+|zr), \[x29, #-(\d+)\]$)");
    const std::regex kMovAny(R"(^(mov|fmov) ([xwds])(\d+), (?:#-?\d+|[xwds]\d+|[xw]zr)$)");
    const std::regex kBaseAt(R"(^(ldr|ldrsw|ldrb|ldrsb|ldrh|ldrsh|str|strb|strh) ([wxds](?:\d+|zr)), \[x(\d+)\]$)");
    const std::regex kCset(R"(^cset ([xw])(\d+), (\w+)$)");
    const std::regex kSp(R"(\bsp\b)");
    const std::regex kX9(R"(\b[wx]9\b)");
    show("push", "str x19, [sp, #-16]!", kPush);
    show("push2", "str x19, [sp, #-32]!", kPush);
    show("slot", "ldursw x3, [x29, #-24]", kSlot);
    show("slotzr", "stur wzr, [x29, #-8]", kSlot);
    show("movany", "mov x3, #-17", kMovAny);
    show("movany2", "fmov d1, d2", kMovAny);
    show("movany3", "mov w4, wzr", kMovAny);
    show("baseat", "strb wzr, [x12]", kBaseAt);
    show("cset", "cset w0, ne", kCset);
    printf("%d %d %d %d\n", (int)std::regex_search(std::string("add sp, sp, #16"), kSp), (int)std::regex_search(std::string("add spx, x1"), kSp),
           (int)std::regex_search(std::string("ldr w9, [x0]"), kX9), (int)std::regex_search(std::string("ldr w19, [x0]"), kX9));
    std::smatch m;
    std::string text = "key = value ; other=42";
    std::regex kv(R"((\w+)\s*=\s*(\w+))");
    bool found = std::regex_search(text, m, kv);
    printf("%d %s %s %d %d\n", (int)found, m[1].str().c_str(), m[2].str().c_str(), (int)m.position(0), (int)m.length(0));
    std::regex lazy("a(b+?)(b*)c"), rep("x{2,3}y"), cls("[^a-c]+"), alt("(cat|category)s?");
    std::smatch l;
    std::regex_match(std::string("abbbc"), l, lazy);
    printf("%s|%s %d %d %d %d %d\n", l[1].str().c_str(), l[2].str().c_str(), (int)std::regex_match(std::string("xxy"), rep),
           (int)std::regex_match(std::string("xxxxy"), rep), (int)std::regex_match(std::string("xyz"), cls),
           (int)std::regex_match(std::string("categorys"), alt), (int)(m[1] == "key"));
    try { std::regex broken("(a"); printf("accepted\n"); } catch (const std::regex_error &) { printf("regex_error\n"); }
    return 0;
}
