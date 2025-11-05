#include "bitonic_count.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

constexpr size_t DurationSeconds = 10;

enum class ETestType {
    OwnAtomic,
    SharedAtomic,
    Bitonic
};

unsigned next_power_of_two(unsigned value) {
    if (value <= 2U) return 2U;
    --value;
    value |= value >> 1;
    value |= value >> 2;
    value |= value >> 4;
    value |= value >> 8;
    value |= value >> 16;
    return value + 1;
}

size_t Bench(int numThreads, ETestType testType) {
    if (numThreads < 1) numThreads = 1;

    const unsigned width = next_power_of_two(static_cast<unsigned>(numThreads));
    Bitonic bitonic(width);

    std::atomic<size_t> atomicCounter{0};

    std::atomic<size_t> ownAtomicSum{0};

    // avoid having start/stop on the same cache line
    volatile char padding[64];

    std::atomic<bool> start{false};
    std::atomic<bool> stop{false};

    std::vector<std::thread> threads;
    threads.reserve(static_cast<size_t>(numThreads));

    for (int t = 0; t < numThreads; ++t) {
        threads.emplace_back([t, width, testType, &bitonic, &atomicCounter, &ownAtomicSum, &start, &stop]() {
            std::atomic<size_t> ownAtomic{0};
            const unsigned input = static_cast<unsigned>(t) & (width - 1);

            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }

            while (!stop.load(std::memory_order_relaxed)) {
                switch (testType) {
                case ETestType::OwnAtomic: {
                    ownAtomic.fetch_add(1, std::memory_order_relaxed);
                    break;
                }
                case ETestType::SharedAtomic: {
                    atomicCounter.fetch_add(1, std::memory_order_relaxed);
                    break;
                }
                case ETestType::Bitonic: {
                    (void)bitonic.get_count(input);
                    break;
                }
                }
            }

            if (testType == ETestType::OwnAtomic) {
                ownAtomicSum.fetch_add(ownAtomic.load(std::memory_order_relaxed), std::memory_order_relaxed);
            }
        });
    }

    start.store(true, std::memory_order_release);
    std::this_thread::sleep_for(std::chrono::seconds(DurationSeconds));

    stop.store(true, std::memory_order_relaxed);

    for (auto &th : threads) th.join();

    switch (testType) {
    case ETestType::OwnAtomic:
        return ownAtomicSum.load(std::memory_order_relaxed);
    case ETestType::SharedAtomic:
        return atomicCounter.load(std::memory_order_relaxed);
    case ETestType::Bitonic:
        return bitonic.get_count(0) - 1;
    }
}

int main(int argc, char** argv) {
    if (argc < 2) {
        return 1;
    }
    int numThreads = std::atoi(argv[1]);

    bool useBitonic = (argc >= 3 && std::string(argv[2]) == "-b");
    bool useOwnAtomic = (argc >= 3 && std::string(argv[2]) == "-o");

    ETestType test_type = ETestType::SharedAtomic;
    if (useBitonic) {
        test_type = ETestType::Bitonic;
    } else if (useOwnAtomic) {
        test_type = ETestType::OwnAtomic;
    }

    constexpr size_t run_count = 5;

    std::vector<size_t> results;
    results.reserve(run_count);

    for (size_t i = 0; i < run_count; ++i) {
        size_t result = Bench(numThreads, test_type);
        results.emplace_back(result);
    }

    std::sort(results.begin(), results.end());

    auto result = results[results.size() / 2];

    std::cout << (result / DurationSeconds) << '\n';
    return 0;
}
