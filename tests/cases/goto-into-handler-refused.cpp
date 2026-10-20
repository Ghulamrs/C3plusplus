// [except.handle]/2: a jump may leave a handler and never enter one. clang: "cannot jump from this
// goto statement to its label" (5:12), the jump bypassing the handler's initialisation.
int f(int k) {
    if (k)
        goto in;
    try { throw 1; } catch (int) { in: return 2; }
    return 0;
}
int main() { return f(0); }
