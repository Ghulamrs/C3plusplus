/* A function try block: C has no exceptions, and 'try' is an identifier. */
int main(void) try { return 0; } catch (int e) { return e; }
