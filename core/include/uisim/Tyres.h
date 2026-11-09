#pragma once

#include <array>

namespace uisim {

// Soft, medium and hard are the dry specifications; intermediate and wet are for rain (M9.3).
enum class Compound { Soft, Medium, Hard, Intermediate, Wet };

inline bool isWetCompound(Compound c) { return c == Compound::Intermediate || c == Compound::Wet; }

// Performance of one compound (per track, data/tracks/*.json "tyres").
struct CompoundSpec {
    double pace = 1.0;            // speed factor on a new set
    double wearPerLap = 0.0;      // speed factor lost per lap of age
    int cliffLap = 99;            // from this age on the set drops off quickly
    double cliffLossPerLap = 0.0; // extra loss per lap beyond the cliff
    double wetGripLoss = 0.25;    // grip lost on a soaked racing line (M9.3)
    double dryWearFactor = 0.0;   // extra age per lap on a dry line (rain tyres overheat), 0 for slicks
};

using CompoundSpecs = std::array<CompoundSpec, 5>; // indexed by Compound

// Speed factor of a set of `compound` that has done `age` laps.
inline double tyreFactor(const CompoundSpecs& specs, Compound compound, double age) {
    const CompoundSpec& s = specs[static_cast<int>(compound)];
    const double beyondCliff = age > s.cliffLap ? age - s.cliffLap : 0.0;
    return s.pace * (1.0 - s.wearPerLap * age - s.cliffLossPerLap * beyondCliff);
}

// Speed of a new set on a racing line of the given wetness (0..1), relative; the team's choice.
inline double wetSpeedFactor(const CompoundSpec& s, double wetness) {
    return s.pace * (1.0 - s.wetGripLoss * wetness);
}

// Defaults for the rain tyres when a track has no data for them.
inline CompoundSpec defaultIntermediate() { return {0.94, 0.0010, 25, 0.004, 0.10, 2.0}; }
inline CompoundSpec defaultWet() { return {0.90, 0.0008, 30, 0.004, 0.05, 4.0}; }

} // namespace uisim
