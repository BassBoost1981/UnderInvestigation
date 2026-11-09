#pragma once

#include "uisim/Sim.h"

#include <vector>

namespace uisim {

// What the stewards should have decided, derived from the hidden ground truth.
// Simplified stewarding practice; to be refined from published decisions (DESIGN.md 3).
struct ExpectedVerdict {
    int car = -1; // entry index, -1 = no further action (racing incident)
    Penalty penalty = Penalty::None;
};
ExpectedVerdict expectedVerdict(const Incident& incident);

struct IncidentRating {
    int incidentId = 0;
    ExpectedVerdict expected;
    bool decided = false;
    int givenCar = -1;
    Penalty givenPenalty = Penalty::None;
    double score = 0.0; // 0..1
};

// Race director rating (DESIGN.md 7).
struct RaceRating {
    double fairness = 1.0;    // verdicts vs ground truth, undecided incidents count as "no action" (undecided accidents without contact not at all)
    double consistency = 1.0; // similar incidents got similar verdicts
    double raceQuality = 1.0; // little neutralisation, no decisions after the flag
    double safety = 1.0;      // accident sites protected quickly, no follow-up accidents
    double overall = 100.0;   // 0..100: 35 % fairness, 15 % consistency, 30 % safety, 20 % race quality
    int accidentSites = 0;
    int followUpAccidents = 0;
    double heavyRainSeconds = 0.0; // racing on in standing water, not neutralised (M9.4)
    int decided = 0;
    std::vector<IncidentRating> incidents;
};
RaceRating rateRace(const Sim& sim);

} // namespace uisim
