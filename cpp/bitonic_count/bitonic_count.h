// quick and dirty

#pragma once

#include <atomic>
#include <cassert>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <vector>

inline bool is_power_of_two(unsigned w) {
    return w >= 2 && (w & (w - 1)) == 0;
}

/** Alternates 0,1,0,1,... in a thread-safe way. */
class alignas(64) Balancer {
public:
    Balancer() = default;
    Balancer(const Balancer&) = delete;
    Balancer& operator=(const Balancer&) = delete;

    Balancer(Balancer&& other) noexcept {
        toggle_.store(other.toggle_.load(std::memory_order_relaxed), std::memory_order_relaxed);
    }
    Balancer& operator=(Balancer&& other) noexcept {
        if (this != &other) {
            toggle_.store(other.toggle_.load(std::memory_order_relaxed), std::memory_order_relaxed);
        }
        return *this;
    }

    int traverse() noexcept {
        // Relaxed is fine: we only need atomic fetch-add for uniqueness & parity.
        return toggle_.fetch_add(1, std::memory_order_relaxed) & 1;
    }

private:
    std::atomic<uint64_t> toggle_{0};
};

/**
 * A width-W merger built from two width/2 mergers and a final layer of W/2 balancers.
 * Width must be a power of two and >= 2.
 */
class Merger {
public:
    explicit Merger(unsigned width) : width_(width) {
        if (!is_power_of_two(width_))
            throw std::invalid_argument("Merger width must be power of two and >= 2");

        // Final layer of W/2 balancers
        layer_.resize(width_ / 2);

        if (width_ > 2) {
            half_[0] = std::make_unique<Merger>(width_ / 2);
            half_[1] = std::make_unique<Merger>(width_ / 2);
        }
    }

    // Route an input wire index (0..W-1) to an output wire index (0..W-1).
    int traverse(unsigned input) noexcept {
        // TODO: check input < width

        // Base case: W == 2 → single balancer picks lane 0/1.
        if (width_ == 2) {
            return layer_[0].traverse(); // already 0 or 1
        }

        // Recursive descent into one of the two half mergers.
        const unsigned nextInput = input >> 1;     // input / 2
        const unsigned parity   = input & 1;       // input % 2
        const unsigned leftSize = width_ >> 1;

        unsigned halfIdx;
        if (input < leftSize) {
            halfIdx = parity;          // left half uses half[parity]
        } else {
            halfIdx = 1U - parity;     // right half uses the opposite
        }

        unsigned subOut = static_cast<unsigned>(half_[halfIdx]->traverse(nextInput)); // 0..(W/2 - 1)

        // Final layer: choose even/odd lane via layer[subOut].
        int lane = layer_[subOut].traverse(); // 0 or 1
        return static_cast<int>((subOut << 1) + static_cast<unsigned>(lane)); // 2*subOut + lane
    }

    unsigned width() const noexcept { return width_; }

private:
    unsigned width_;
    std::unique_ptr<Merger> half_[2]; // null when width_ == 2
    std::vector<Balancer> layer_;     // size = width_/2
};

class alignas(64) AtomicCounter {
public:
    AtomicCounter() = default;
    AtomicCounter(const AtomicCounter&) = delete;
    AtomicCounter& operator=(const AtomicCounter&) = delete;

    AtomicCounter(AtomicCounter&& other) noexcept {
        count_.store(other.count_.load(std::memory_order_relaxed), std::memory_order_relaxed);
    }

    AtomicCounter& operator=(AtomicCounter&& other) noexcept {
        if (this != &other) {
            count_.store(other.count_.load(std::memory_order_relaxed), std::memory_order_relaxed);
        }
        return *this;
    }

    uint64_t increment() {
        return count_.fetch_add(1UL, std::memory_order_relaxed) + 1;
    }

    uint64_t get() {
        return count_.load(std::memory_order_relaxed);
    }

private:
    std::atomic<uint64_t> count_{0};
};

/**
 * A width-W bitonic network: two width/2 bitonic networks feeding a width-W Merger.
 * Width must be a power of two and >= 2.
 */
class Bitonic {
public:
    explicit Bitonic(unsigned width) : width_(width) {
        if (!is_power_of_two(width_))
            throw std::invalid_argument("Bitonic width must be power of two and >= 2");

        if (width_ == 2) {
            base_ = std::make_unique<Balancer>();
        } else {
            half_[0] = std::make_unique<Bitonic>(width_ / 2);
            half_[1] = std::make_unique<Bitonic>(width_ / 2);
            merger_  = std::make_unique<Merger>(width_);
        }

        counters_.resize(width_);
    }

    // Route an input wire index (0..W-1) to an output wire index (0..W-1).
    int traverse(unsigned input) noexcept {
        // TODO: check input < width

        if (width_ == 2) {
            return base_->traverse(); // 0 or 1
        }

        const unsigned halfW = width_ >> 1;

        // First pass through the appropriate half bitonic network.
        unsigned out;
        if (input < halfW) {
            out = static_cast<unsigned>(half_[0]->traverse(input));
        } else {
            out = static_cast<unsigned>(half_[1]->traverse(input - halfW)) + halfW;
        }

        // Then merge both halves.
        return merger_->traverse(out);
    }

    uint64_t get_count(unsigned input) {
        int index = traverse(input);
        uint64_t value = counters_[index].increment();
        return (value - 1) * width_ + index + 1;
    }

    uint64_t get_bucket_value(size_t index) {
        return counters_[index].get();
    }

    unsigned width() const noexcept { return width_; }

private:
    unsigned width_;
    std::unique_ptr<Balancer> base_;     // only when width_ == 2
    std::unique_ptr<Bitonic>  half_[2];  // null when width_ == 2
    std::unique_ptr<Merger>   merger_;   // null when width_ == 2

    std::vector<AtomicCounter> counters_;
};
