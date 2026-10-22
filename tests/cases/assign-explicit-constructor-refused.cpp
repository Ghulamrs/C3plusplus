// An explicit constructor takes no part in the implicit copy assignment's conversion.
struct Ex {
    int n;
    Ex() : n(1) {}
    explicit Ex(int k) : n(k) {}
};
int main() {
    Ex e;
    e = 5;
    return e.n;
}
