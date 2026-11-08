#pragma once

namespace uisim {

// Rule parameters used by the sim. Defaults match data/rules/fia-f1-2026.json;
// the mechanics are simplified, see docs/DESIGN.md chapter 3.
struct RaceRules {
    double yellowSpeedFactor = 0.95;       // speed in a single-yellow marshal sector
    double doubleYellowSpeedFactor = 0.80; // speed in a double-yellow marshal sector
    double vscSpeedFraction = 0.60;        // VSC: every car drives this share of the reference speed
    double safetyCarSpeedFraction = 0.55;  // Safety Car pace
    double safetyCarCatchUpFraction = 0.85; // field closing up to the Safety Car queue
    double redFlagSpeedFraction = 0.40;    // slow return to the pit lane / line
    bool redFlagFreeTyreChange = true;     // tyres may be changed during a suspension
    double pitLaneSpeedKmh = 80.0;
    double stopAndGoSeconds = 10.0;
    double unservedDriveThroughSeconds = 20.0; // added if not served before the end / imposed afterwards
    double unservedStopAndGoSeconds = 30.0;
};

} // namespace uisim
