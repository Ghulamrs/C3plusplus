// [thread.thread.this]: this_thread::yield and sleep_for, which <thread> named and did not define - found
// writing a probe that waits for a debugger, compiled by cxx3. sleep_for sleeps at least what it is given.
#include <chrono>
#include <cstdio>
#include <thread>
int main() {
    std::this_thread::yield();
    const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    std::this_thread::sleep_for(std::chrono::microseconds(1500));
    std::this_thread::sleep_for(std::chrono::seconds(0));
    const long long took = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();
    std::printf("slept at least 31 ms: %d\n", took >= 31 ? 1 : 0);
    return 0;
}
