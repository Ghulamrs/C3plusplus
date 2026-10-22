// A local's address is not an address constant, [expr.const]/4.
int main() {
    int x = 1;
    constexpr int *p = &x;
    return *p;
}
