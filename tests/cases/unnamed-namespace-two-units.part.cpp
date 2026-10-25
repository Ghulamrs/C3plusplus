// The second unit of unnamed-namespace-two-units.cpp: the same three names, its own objects.
namespace {
unsigned count = 40;
int table[2] = { 5, 6 };
const char *label = "part";
}
namespace ns { namespace { int level = 1; } }

int partTotal() { return static_cast<int>(count) + table[1] + ns::level + (label[0] == 'p'); }
