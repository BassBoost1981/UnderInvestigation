#pragma once

#include "uisim/Snapshot.h"

#include <vector>

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
    // M9 (from package 9.1)
    bool wetTyresVoidCompoundRule = true;              // B6.3.6
    Penalty wetStartTyreChangePenalty = Penalty::StopAndGo; // B6.3.7
    int safetyCarStartLapsDeductedOffset = 1;          // B5.10.7: race shortened by SC laps minus this
    bool safetyCarStartStandingRestart = true;         // B5.10.8 / B5.10.9
    bool trackDeclaredWetInRace = false;               // no race declaration in Section B
    bool rainLightsWithWetTyres = true;                // B1.5.5 a
    double rainHazardPrecipitationProbability = 0.4;   // B1.5.11
    // M10 (from package 10.1)
    std::vector<int> racePoints = {25, 18, 15, 12, 10, 8, 6, 4, 2, 1};             // A2.2.1 d: leader at 75 % or more
    std::vector<int> racePointsFrom50Percent = {19, 14, 12, 10, 8, 6, 4, 3, 2, 1}; // A2.2.1 c
    std::vector<int> racePointsFrom25Percent = {13, 10, 8, 6, 5, 4, 3, 2, 1};      // A2.2.1 b
    std::vector<int> racePointsFromTwoLaps = {6, 4, 3, 2, 1};                      // A2.2.1 a
    int racePointsMinGreenLaps = 2;             // A2.2.1: laps without SC or VSC before any points
    double classifiedMinDistanceFraction = 0.9; // B2.5.5 b
    double raceDistanceKm = 305.0;              // B2.5.2: least number of laps exceeding this
    double raceDistanceShortKm = 260.0;         // B2.5.2 b: one named street circuit
    int startLights = 5;                        // B5.7.1
    double startLightIntervalSeconds = 1.0;     // B5.7.1 b i
    double falseStartPenaltySeconds = 5.0;      // B5.11, Penalty Guidelines
    double falseStartSeriousPenaltySeconds = 10.0; // sim: positions gained through the breach
    int startRepeatLapsDeducted = 1;            // B5.8, B5.9: per aborted start or extra formation lap
    bool standingRestartAfterRed = true;        // B5.15.4
    int penaltyPointsSuspensionAt = 12;         // A3.3.1 b: suspended for the next Competition
    int penaltyPointsValidMonths = 12;          // A3.3.1 b
    int penaltyPointsCollision = 2;             // Penalty Guidelines (0 to 3)
    int penaltyPointsYellowFlag = 3;            // Penalty Guidelines (maximum)
    int penaltyPointsOvertakingNeutralised = 2; // Penalty Guidelines: behind the SC or under VSC
};

} // namespace uisim
