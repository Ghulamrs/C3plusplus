/* A reference declared as a local. */
int main(void) { int x = 3; int &r = x; r = 5; return x; }
