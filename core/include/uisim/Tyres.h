#pragma once

#include <array>

namespace uisim {

enum class Compound { Soft, Medium, Hard };

// Performance of one dry compound (per track, data/tracks/*.json "tyres").
struct CompoundSpec {
    double pace = 1.0;            // speed factor on a new set
    double wearPerLap = 0.0;      // speed factor lost per lap of age
    int cliffLap = 99;            // from this age on the set drops off quickly
    double cliffLossPerLap = 0.0; // extra loss per lap beyond the cliff
};

using CompoundSpecs = std::array<CompoundSpec, 3>; // indexed by Compound

// Speed factor of a set of `compound` that has done `age` laps.
inline double tyreFactor(const CompoundSpecs& specs, Compound compound, double age) {
    const CompoundSpec& s = specs[static_cast<int>(compound)];
    const double beyondCliff = age > s.cliffLap ? age - s.cliffLap : 0.0;
    return s.pace * (1.0 - s.wearPerLap * age - s.cliffLossPerLap * beyondCliff);
}

} // namespace uisim
