// The statements M2.9 records, and the keyword forms: every control statement, a label and its goto, try and
// catch, declarations local and in a condition, sizeof, new, delete, a named cast, throw and a lambda.
struct B { virtual ~B() {} };
struct D : B { int k; };
int g(int);
int h(int n, B *b) {
    int s = 0, t[3] = {1, 2, 3};
    for (int i = 0; i < n; i++) { if (i % 2) continue; s += t[i % 3]; }
    while (s > 100) s /= 2;
    do { s++; } while (s < 3);
    switch (n) { case 1: s = sizeof(int) + sizeof s; break; default: s = static_cast<int>(2.5); }
    if (D *d = dynamic_cast<D *>(b)) s += d->k; else s--;
    int *p = new int(7); s += *p; delete p;
    auto f = [&s](int x) { return x + s; };
    try { if (s < 0) throw s; } catch (int e) { s = e; }
    again: if (s > 1000) goto again;
    return f(s) + g(s);
}
