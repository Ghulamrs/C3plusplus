// `return {};` of a class with a copy of its own, and std::nullptr_t forwarded through a `T &&` (M3b.7 round 5b).
#include <cstdio>
#include <memory>
#include <utility>
#include <vector>

struct Node {
    explicit Node(std::unique_ptr<int> v);
    std::unique_ptr<int> v_;
};
Node::Node(std::unique_ptr<int> v) : v_(std::move(v)) {}

template <class T, class... A>
std::unique_ptr<T> owned(A &&...args) { return std::unique_ptr<T>(new T(std::forward<A>(args)...)); }

static std::vector<int> evens(int n) {
    if (n < 0) return {};
    std::vector<int> v;
    for (int i = 0; i < n; i++)
        if (i % 2 == 0) v.push_back(i);
    return v;
}

int main() {
    std::unique_ptr<Node> a = owned<Node>(nullptr);
    std::printf("%d\n", a->v_ == nullptr ? 1 : 0);
    std::vector<int> none = evens(-1);
    std::vector<int> some = evens(7);
    std::printf("%d %d %d\n", (int)none.size(), (int)some.size(), some[3]);
    return 0;
}
