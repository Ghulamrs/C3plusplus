// [ofstream.cons]/4 and [ifstream.cons]/4, C++11: a stream opened by a std::string name; and [support.start.term],
// std::atexit from <cstdlib> - both what the compiler's own Driver.cpp writes (M1).
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
static void bye() { std::printf("bye\n"); }
int main() {
    std::atexit(bye);
    const std::string name = std::string("fstream-string-name") + ".tmp";
    {
        std::ofstream out(name, std::ios::binary);
        out << "written " << 42;
    }
    std::ifstream in(name);
    std::string word;
    int n = 0;
    in >> word >> n;
    in.close();
    std::ofstream again;
    again.open(name);
    again.close();
    std::remove(name.c_str());
    std::printf("%s %d %d\n", word.c_str(), n, (int)in.is_open());
    return 0;
}
