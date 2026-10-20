// [over.over]/1 with two candidates that each take a different function of the argument's set:
// both are exact matches, so the call is ambiguous. clang: "call to 'g' is ambiguous" (7:14).
int h(int x) { return x; }
double h(double x) { return x; }
void g(int (*)(int)) {}
void g(double (*)(double)) {}
int main() { g(h); return 0; }
