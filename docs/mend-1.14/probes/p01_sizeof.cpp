// sizeof(T) in enable_if, sizeof(expr), comparisons and arithmetic on non-type params.
template <bool B, class T = void> struct enable_if {};
template <class T> struct enable_if<true, T> { typedef T type; };
template <class T> typename enable_if<sizeof(T) == 4, int>::type f1(T) { return 1; }
template <class T> typename enable_if<sizeof(T) != 4, long>::type f2(T) { return 2; }
template <class T> typename enable_if<(sizeof(T) > 2), int>::type f3(T) { return 3; }
template <class T> typename enable_if<(sizeof(T) < 2), int>::type f4(T) { return 4; }
template <class T> typename enable_if<sizeof(T) <= 8, int>::type f5(T) { return 5; }
template <class T> typename enable_if<(sizeof(T) >= 8), int>::type f6(T) { return 6; }
template <class T> typename enable_if<sizeof(T) * 2 == 8, int>::type f7(T) { return 7; }
template <class T> typename enable_if<sizeof(T) + 4 == 8, int>::type f8(T) { return 8; }
template <class T> typename enable_if<sizeof(T) - 4 == 0, int>::type f9(T) { return 9; }
template <class T> typename enable_if<sizeof(T) / 2 == 2, int>::type f10(T) { return 10; }
template <class T> typename enable_if<sizeof(T) % 4 == 0, int>::type f11(T) { return 11; }
template <class T> typename enable_if<(sizeof(T) & 4) != 0, int>::type f12(T) { return 12; }
template <class T> typename enable_if<(sizeof(T) | 4) == 4, int>::type f13(T) { return 13; }
template <class T> typename enable_if<(sizeof(T) ^ 4) == 0, int>::type f14(T) { return 14; }
template <class T> typename enable_if<(sizeof(T) << 1) == 8, int>::type f15(T) { return 15; }
template <class T> typename enable_if<(sizeof(T) >> 1) == 2, int>::type f16(T) { return 16; }
template <class T> typename enable_if<sizeof(T) == 4 && sizeof(T) != 2, int>::type f17(T) { return 17; }
template <class T> typename enable_if<sizeof(T) == 4 || sizeof(T) == 2, int>::type f18(T) { return 18; }
template <class T> typename enable_if<!(sizeof(T) == 2), int>::type f19(T) { return 19; }
template <class T> typename enable_if<(-sizeof(T) + 8) == 4, int>::type f20(T) { return 20; }
template <class T> typename enable_if<(~sizeof(T) & 1) == 1, int>::type f21(T) { return 21; }
template <class T> typename enable_if<sizeof(T) == 4 ? true : false, int>::type f22(T) { return 22; }
template <class T> typename enable_if<sizeof(T *) == 8, int>::type f23(T) { return 23; }
template <class T> typename enable_if<sizeof(T[2]) == 8, int>::type f24(T) { return 24; }
template <class T> typename enable_if<sizeof(T) == sizeof(int), int>::type f25(T) { return 25; }
template <class T> typename enable_if<sizeof(T(1)) == 4, int>::type f26(T) { return 26; }
template <class T, int N> typename enable_if<N == 3, int>::type f27(T) { return 27; }
template <class T, int N> typename enable_if<(N + 1 == 4), int>::type f28(T) { return 28; }
template <class T, int N> typename enable_if<(N > 2), int>::type f29(T) { return 29; }
template <class T, int N, int M> typename enable_if<N * M == 6, int>::type f30(T) { return 30; }
template <class T> typename enable_if<sizeof(T) == 4u, int>::type f31(T) { return 31; }
template <class T> typename enable_if<sizeof(T) == 4L, int>::type f32(T) { return 32; }
template <class T> typename enable_if<sizeof(T) == 4ULL, int>::type f33(T) { return 33; }
template <class T> typename enable_if<sizeof(T) == 'a' - 93, int>::type f34(T) { return 34; }
template <class T> typename enable_if<sizeof(T) == (int)4.0, int>::type f35(T) { return 35; }
template <class T> typename enable_if<sizeof(T) == 4, T>::type f36(T) { return T(); }
template <class T> typename enable_if<sizeof(T) == 4, T *>::type f37(T) { return 0; }
int main() {
    int i = 0; double d = 0; char c = 0;
    f1(i); f2(d); f3(i); f4(c); f5(i); f6(d); f7(i); f8(i); f9(i); f10(i); f11(i); f12(i); f13(i); f14(i);
    f15(i); f16(i); f17(i); f18(i); f19(i); f20(i); f21(i); f22(i); f23(i); f24(i); f25(i); f26(i);
    f27<int, 3>(i); f28<int, 3>(i); f29<int, 3>(i); f30<int, 2, 3>(i); f31(i); f32(i); f33(i); f34(i); f35(i);
    f36(i); f37(i);
    return 0;
}
