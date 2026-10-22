// The other half of member-template-body-unchosen: where the constructor
// template is the one chosen, its body is instantiated, and an error in it is
// reported - after overload resolution, naming where it was asked for.
template <class T> struct W {
    int v;
    template <class U> W(const U &u) : v(u.missing) {}
};
int main() {
    W<int> w(3);
    return w.v;
}
