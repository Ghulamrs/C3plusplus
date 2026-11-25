// cerr is tied to cout ([narrow.stream.objects]/4): what cout holds is written before cerr writes, so with both
// streams in one file the lines keep program order even when stdout is fully buffered
#include <iostream>

int main() {
    std::cout << "one\n";
    std::cerr << "two\n";
    std::cout << "three " << 3 << "\n";
    std::cerr << "four " << 4 << '\n';
    std::cout << "five\n";
    return 0;
}
