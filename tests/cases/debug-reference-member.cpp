// A reference in -g's records is a pointer's size and names what it refers to: written as a base type
// with the referent's size, a closure capturing a 300-byte object by reference overran DWARF's one-byte
// size and the assembler refused the file. run.sh builds this case with -g as well, and runs it.
extern "C" int printf(const char *, ...);
struct Big { char b[300]; };
int sum(Big &r, Big &&q, const int &k) { return r.b[0] + q.b[1] + k; }
int main() {
    Big a = {}, b = {};
    a.b[0] = 1;
    b.b[1] = 2;
    auto get = [&]() { return a.b[0] + b.b[1]; };
    int k = 3;
    printf("%d %d\n", get(), sum(a, static_cast<Big &&>(b), k));
    return 0;
}
