// Defect D3: decltype of an expression over T in a leading return type is refused by a message naming
// no feature - "'template parameter' and 'template parameter' cannot be combined like that".
template <class T> decltype(T() + T()) h1(T) { return T(); }
int main() { return h1(1); }
