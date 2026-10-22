// std::function's primary template is declared only for a function type ([func.wrap.func]).
#include <functional>
int main() {
    std::function<int> f;
    return 0;
}
