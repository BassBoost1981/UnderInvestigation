#pragma once

#include "uisim/Sim.h"

#include <vector>

namespace uisim {

// A hearing on an incident (M11.4): what the recording shows, and what the drivers say.
// The evidence is always true. A statement is made of claims about the same quantities; a
// driver who is at fault and not honest shifts his claims in his own favour, further than the
// evidence allows. There are no free texts here: the display turns kinds and values into sentences.

enum class EvidenceKind {
    TurnInGap,    // collision: metres the attacker was behind when both turned in (0 = alongside)
    LateBraking,  // collision: metres the attacker braked later than on his laps before
    DefenderMove, // collision: metres the defender moved across in the braking zone
    Measure,      // what was measured: km/h over the pit limit, metres, seconds, a count (by incident type)
    Breach        // 1 = the recording shows the breach (flags, tyres, pit entry)
};

struct Evidence {
    EvidenceKind kind = EvidenceKind::Measure;
    double value = 0.0;
};

struct Claim {
    EvidenceKind kind = EvidenceKind::Measure;
    double value = 0.0;
};

struct Statement {
    int car = 0; // entry index
    std::vector<Claim> claims;
};

struct Hearing {
    int incidentId = 0;
    std::vector<Evidence> evidence;
    std::vector<Statement> statements; // the car first, then the other car where there is one
};

// How far a claim may be from the evidence and still agree with it (memory is not a recording).
double evidenceTolerance(EvidenceKind kind, double value);

// True if the claim is further from the hearing's evidence of the same kind than the tolerance.
bool contradictsEvidence(const Hearing& hearing, const Claim& claim);

// The hearing for an incident of this race. The same race gives the same hearing; asking does
// not change the race. Incidents nobody is to blame for (accidents without contact) have no statements.
Hearing makeHearing(const Sim& sim, const Incident& incident);

} // namespace uisim
