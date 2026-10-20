// [except.handle]/2, the `try` block half - and a `switch` is a jump too, so a case label inside a
// `try` its switch is outside of is refused the same way. clang refuses both.
int f(int k) {
    switch (k) {
    case 0:
        try {
    case 1:
            return 1;
        } catch (int) { }
    }
    return 0;
}
int main() { return f(0); }
