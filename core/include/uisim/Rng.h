#pragma once

#include <cstdint>

namespace uisim {

// PCG32 (pcg-random.org). Own implementation instead of <random> because the
// standard distributions are not guaranteed to produce identical sequences
// across compilers - the sim must be deterministic for a given seed.
class Pcg32 {
public:
    explicit Pcg32(std::uint64_t seed, std::uint64_t stream = 54u)
        : state_(0u), inc_((stream << 1u) | 1u) {
        next();
        state_ += seed;
        next();
    }

    std::uint32_t next() {
        const std::uint64_t old = state_;
        state_ = old * 6364136223846793005ULL + inc_;
        const auto xorshifted = static_cast<std::uint32_t>(((old >> 18u) ^ old) >> 27u);
        const auto rot = static_cast<std::uint32_t>(old >> 59u);
        return (xorshifted >> rot) | (xorshifted << ((32u - rot) & 31u));
    }

    // Uniform in [0, 1).
    double uniform() { return next() * (1.0 / 4294967296.0); }

    // Uniform in [lo, hi).
    double uniform(double lo, double hi) { return lo + (hi - lo) * uniform(); }

private:
    std::uint64_t state_;
    std::uint64_t inc_;
};

} // namespace uisim
