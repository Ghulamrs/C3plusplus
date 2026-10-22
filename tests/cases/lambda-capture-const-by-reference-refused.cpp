// A const object captured by reference stays const: writing through it is refused, as clang refuses it.
int main() {
    const int c = 4;
    auto f = [&]() mutable { c = 5; };
    f();
    return 0;
}
