// <thread>, <mutex> and <atomic>: four real threads draining one work list - the shape of cxx3's own -j pool -
// over CreateThread and SRW locks on Windows, pthreads on Linux and macOS, and one thread on the C6000.
#include <atomic>
#include <mutex>
#include <thread>
#include <vector>
#include <cstdio>
static std::mutex &lockOf() { static std::mutex m; return m; }
struct Counter { std::atomic<int> hits{0}; };
int main() {
    const unsigned n = std::thread::hardware_concurrency();
    std::atomic<std::size_t> next{0};
    std::atomic<bool> ok{true};
    std::mutex say;
    long total = 0;
    Counter c;
    std::vector<int> work(1000);
    for (std::size_t i = 0; i < work.size(); i++) work[i] = (int)i;
    {
        std::vector<std::thread> pool;
        for (unsigned t = 0; t < 4; t++)
            pool.emplace_back([&] {
                for (;;) {
                    const std::size_t i = next.fetch_add(1);
                    if (i >= work.size()) return;
                    c.hits++;
                    std::lock_guard<std::mutex> hold(say);
                    total += work[i];
                }
            });
        for (std::thread &t : pool) t.join();
    }
    { std::lock_guard<std::mutex> h(lockOf()); }
    std::unique_lock<std::mutex> u(say);
    int expected = 5;
    bool swapped = c.hits.compare_exchange_strong(expected, 7);
    printf("%ld %d %d %d %d %d\n", total, c.hits.load(), (int)ok.load(), n >= 1, (int)u.owns_lock(), (int)swapped);
    return 0;
}
