// -keep-going=2 stops at the second error and says that is why: the third is never reached.
int a() { return undeclared_1; }
int b() { return undeclared_2; }
int c() { return undeclared_3; }
