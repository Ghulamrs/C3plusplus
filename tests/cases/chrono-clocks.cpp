// <chrono> and <ratio>: a steady clock that does not go back, durations converted between periods - implicitly
// where nothing is lost, by duration_cast where it is - and the system clock's time_t. <cerrno> beside them.
#include <chrono>
#include <cerrno>
#include <cstdio>
int main() {
    using Clock = std::chrono::steady_clock;
    Clock::time_point a = Clock::now();
    volatile long s = 0;
    for (long i = 0; i < 2000000; i++) s += i;
    Clock::time_point b = Clock::now();
    double ms = std::chrono::duration<double, std::milli>(b - a).count();
    std::chrono::milliseconds two(2);
    std::chrono::microseconds us = two;
    std::chrono::seconds whole = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::milliseconds(3500));
    std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
    time_t t = std::chrono::system_clock::to_time_t(now);
    errno = 0;
    printf("%d %lld %lld %d %d %d %d\n", ms >= 0.0, (long long)us.count(), (long long)whole.count(),
           (two + two).count() == 4, two < std::chrono::milliseconds(3), t > 1700000000, errno);
    return 0;
}
