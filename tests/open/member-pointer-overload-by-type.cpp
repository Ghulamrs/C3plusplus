// [over.over]: `&S::f` names an overload set, and the target type picks one -
// here by its ref-qualifier, `int (S::*)() &` taking `f() &` and `int (S::*)() &&`
// taking `f() &&`. cxx1 refuses an `&S::f` that names more than one function
// ("which one this is cannot be told from the use alone"), whatever the target
// says; the refusal is older than ref-qualifiers, which only add one more way
// two overloads can differ by nothing a call's arguments show. Found 2026-10-18.
extern "C" int printf(const char *, ...);
struct S {
    int v;
    int f() & { return v; }
    int f() && { return v * 10; }
};
int main() {
    int (S::*lv)() & = &S::f;
    int (S::*rv)() && = &S::f;
    S s;
    s.v = 4;
    printf("%d %d\n", (s.*lv)(), (static_cast<S &&>(s).*rv)());
    return 0;
}
