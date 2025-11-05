#include "bitonic_count.h"
#include <atomic>
#include <iostream>
#include <thread>
#include <vector>

int main() {
    constexpr unsigned W = 8;
    constexpr unsigned NTHREADS = 64;

    Bitonic net(W);

    std::vector<std::thread> ts;
    ts.reserve(NTHREADS);

    for (unsigned t = 0; t < NTHREADS; ++t) {
        ts.emplace_back([&, t]{
            // In a real counting network you’d stack multiple layers to get full ranks.
            // Here we just demonstrate balanced distribution modulo W.
            unsigned in = t % W; // pick an input wire
            net.get_count(in);
        });
    }
    for (auto& th : ts) th.join();

    std::cout << "Output distribution over " << W << " wires:\n";
    for (unsigned i = 0; i < W; ++i) {
        std::cout << i << ": " << net.get_bucket_value(i) << "\n";
    }

    std::cout << "Final count: " << (net.get_count(0) - 1) << std::endl;
    return 0;
}