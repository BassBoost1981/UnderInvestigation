#pragma once

#include "uisim/Snapshot.h"

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
    double overtakeDetectionGapSeconds = 1.0; // B7.2: set per event by the FIA (sim assumption)
    int blueFlagMarshalSectors = 3; // a lapped car must let the faster car by within this many sectors
    // M7 (from package 7.1; used by the tyre, pit stop and track limits packages)
    int minDryCompounds = 2;                    // B6.3.6, otherwise disqualification
    double twoCompoundNotRestartedSeconds = 30.0; // B6.3.6: instead of disqualification if the race is not restarted
    bool timePenaltyServedAtNextStop = true;    // B1.9.6 b
    double pitSpeedingDriveThroughFromKmh = 6.0; // Penalty Guidelines: below 5 s, from here drive-through
    double pitSpeedingStopAndGoAboveKmh = 15.0;  // Penalty Guidelines: above this stop-and-go
    double unsafeReleasePenaltySeconds = 5.0;    // Penalty Guidelines (B1.6.2 a)
    int trackLimitsBlackWhiteAt = 3;   // Penalty Guidelines: offence that brings the black-and-white flag
    int trackLimitsPenaltyFrom = 4;    // first offence that is penalised
    double trackLimitsPenaltySeconds = 5.0;
    // M8 (from package 8.1)
    bool lappedCarsMayOvertake = true;        // B5.13.4 c, on the Race Director's message
    int safetyCarInLapsAfterLappedCars = 1;   // B5.13.5 b: SC returns at the end of the following lap
    Penalty pitEntryClosedPenalty = Penalty::StopAndGo; // B1.6.4 b
};

} // namespace uisim
