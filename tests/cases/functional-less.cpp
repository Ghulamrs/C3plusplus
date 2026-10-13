// <functional> (L09): the comparison, arithmetic and logical function objects, called directly,
// through a template that takes one as a parameter, and handed to std::sort. clang prints the same.
#include <functional>
#include <algorithm>
#include <vector>
#include <cstdio>

template <class Op> int fold(const int *first, const int *last, int start, Op op) {
    int acc = start;
    for (; first != last; ++first) acc = op(acc, *first);
    return acc;
}

int main() {
    std::less<int> lt;
    std::greater<int> gt;
    std::printf("%d %d %d %d\n", lt(1, 2), gt(1, 2), std::equal_to<int>()(3, 3), std::not_equal_to<int>()(3, 3));
    std::printf("%d %d\n", std::less_equal<double>()(2.5, 2.5), std::greater_equal<int>()(1, 2));
    std::vector<int> v;
    v.push_back(4); v.push_back(1); v.push_back(3); v.push_back(2);
    std::printf("%d %d\n", fold(&v[0], &v[0] + v.size(), 0, std::plus<int>()),
                fold(&v[0], &v[0] + v.size(), 1, std::multiplies<int>()));
    std::printf("%d %d %d %d\n", std::minus<int>()(9, 4), std::divides<int>()(9, 4),
                std::modulus<int>()(9, 4), std::negate<int>()(5));
    std::printf("%d %d %d\n", std::logical_and<bool>()(true, false), std::logical_or<bool>()(true, false),
                std::logical_not<bool>()(false));
    std::sort(v.begin(), v.end(), std::greater<int>());
    for (std::size_t i = 0; i < v.size(); i++) std::printf("%d ", v[i]);
    std::printf("\n");
    return 0;
}
