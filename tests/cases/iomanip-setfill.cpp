// <iomanip> setfill (L05): the fill character pads what setw widens, numbers and strings alike,
// and stays until changed - [std.manip] makes it sticky where the width is not; fill() reads and
// sets it. clang prints the same lines.
#include <iostream>
#include <iomanip>
#include <sstream>

int main() {
    std::cout << '[' << std::setw(5) << 42 << "]\n";
    std::cout << '[' << std::setfill('0') << std::setw(5) << 42 << "]\n";
    std::cout << '[' << std::setw(4) << 7 << '|' << std::setw(6) << "ab" << "]\n";
    std::cout << '[' << std::setfill('*') << std::setw(3) << 12345 << "]\n";
    char was = std::cout.fill('.');
    std::cout << '[' << std::setw(4) << "x" << "] " << was << "\n";
    std::ostringstream os;
    os << std::setfill('-') << std::setw(6) << 3.5;
    std::cout << os.str() << ' ' << os.fill() << "\n";
    std::cout << std::setfill(' ');
    return 0;
}
