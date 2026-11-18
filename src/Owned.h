#pragma once

#include <memory>
#include <utility>

// R6.4: the one helper an owned object is made by, and so the one `new` the memory rule (R6) leaves standing.
template <class T, class... A>
std::unique_ptr<T> make_owned(A &&...args) { return std::unique_ptr<T>(new T(std::forward<A>(args)...)); }
