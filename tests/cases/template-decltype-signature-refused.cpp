// A leading decltype over a template parameter in a function template's signature: the Itanium name
// spells the expression (DT...E), which this compiler does not write yet. It was refused by a message
// naming no feature - "'template parameter' and 'template parameter' cannot be combined like that".
template <class T> decltype(T() + T()) h1(T) { return T(); }
int main() { return h1(1); }
