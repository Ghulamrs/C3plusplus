// A long double literal is accepted where its value is exactly a double, on the target whose long double is
// wider, however many digits it takes to write: 18446744073709551616.0L is 2^64, twenty significant digits,
// and the test that asked overflowed a 64-bit accumulator on them and refused it - which stopped cxx3
// compiling its own ParserInit.cpp for x86_64-linux (M1). The digits are a number of any length now.
#include <cstdio>

static const long double values[] = {
    18446744073709551616.0L,
    1180591620717411303424.0L,
    9903520314283041099681366016.0L,
    0.000000000000000000000000000000000000000000000000000000652530446799852452671029410925654755570116425806896654775863645546972324459748622722289612685386828161426819860935211181640625L,
    0.00000000000000000000000000000000000000000000000000000000000186690458335834251214321921613403727217707565061635013993033498443690965021061503235518344693771408508708997686653674463741481304168701171875L,
};

int main() {
    for (unsigned i = 0; i < sizeof values / sizeof values[0]; i++)
        std::printf("%.17g\n", (double)values[i]);
    std::printf("%d\n", values[0] == 18446744073709551616.0L);
    return 0;
}
