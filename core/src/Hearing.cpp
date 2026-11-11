#include "uisim/Hearing.h"

#include "uisim/Rating.h"
#include "uisim/Rng.h"

#include <algorithm>
#include <cmath>

namespace uisim {
namespace {

constexpr double kLateBrakingMetres = 30.0; // attacker at full commitment: this much later on the brakes
constexpr double kDefenderMoveMetres = 2.5; // defender at full commitment: this far across in the braking zone
constexpr double kHonestSpread = 0.5;       // an honest claim stays within this share of the tolerance
constexpr double kLieMin = 1.5;             // a lie is this many tolerances off, at least
constexpr double kLieMax = 3.0;

enum class Favour { Lower, Higher };

// Which way a claim helps the driver making it.
Favour favourable(EvidenceKind kind, bool attacker, Incident::Type type) {
    switch (kind) {
    case EvidenceKind::TurnInGap:
    case EvidenceKind::LateBraking:
        return attacker ? Favour::Lower : Favour::Higher;
    case EvidenceKind::DefenderMove:
        return attacker ? Favour::Higher : Favour::Lower;
    case EvidenceKind::Measure:
        return type == Incident::Type::UnsafeRelease ? Favour::Higher : Favour::Lower; // a bigger gap was safer
    case EvidenceKind::Breach:
        break;
    }
    return Favour::Lower;
}

std::vector<Evidence> evidenceOf(const Incident& incident) {
    using Type = Incident::Type;
    switch (incident.type) {
    case Type::Collision:
        return {{EvidenceKind::TurnInGap, incident.turnInGap},
                {EvidenceKind::LateBraking, kLateBrakingMetres * incident.attackerPush},
                {EvidenceKind::DefenderMove, kDefenderMoveMetres * incident.defenderPush}};
    case Type::PitLaneSpeeding:
    case Type::UnsafeRelease:
    case Type::TrackLimits:
    case Type::FalseStart:
    case Type::GridPosition:
        return {{EvidenceKind::Measure, incident.measure}};
    case Type::Accident:
        return {};
    default:
        return {{EvidenceKind::Breach, 1.0}}; // flags, blue flags, tyre rules, closed pit entry
    }
}

} // namespace

double evidenceTolerance(EvidenceKind kind, double value) {
    switch (kind) {
    case EvidenceKind::TurnInGap:
        return 1.0;
    case EvidenceKind::LateBraking:
        return 5.0;
    case EvidenceKind::DefenderMove:
        return 0.5;
    case EvidenceKind::Measure:
        return 0.15 * std::abs(value);
    case EvidenceKind::Breach:
        return 0.5;
    }
    return 0.0;
}

bool contradictsEvidence(const Hearing& hearing, const Claim& claim) {
    for (const Evidence& evidence : hearing.evidence) {
        if (evidence.kind == claim.kind) {
            return std::abs(claim.value - evidence.value) > evidenceTolerance(evidence.kind, evidence.value);
        }
    }
    return false; // nothing recorded about it
}

Hearing makeHearing(const Sim& sim, const Incident& incident) {
    Hearing hearing;
    hearing.incidentId = incident.id;
    hearing.evidence = evidenceOf(incident);
    if (hearing.evidence.empty()) {
        return hearing;
    }
    const ExpectedVerdict expected = expectedVerdict(incident);
    const bool twoCars = incident.type == Incident::Type::Collision;
    const int cars[2] = {incident.car, incident.otherCar};
    for (int side = 0; side < (twoCars ? 2 : 1); ++side) {
        const int car = cars[side];
        // Its own stream per race, incident and car: asking twice gives the same answer.
        Pcg32 rng(sim.seed() * 1000003u + static_cast<std::uint64_t>(incident.id) * 101u + static_cast<std::uint64_t>(side), 141u);
        const double honesty = std::clamp(sim.entries()[car].honesty, 0.0, 1.0);
        const bool atFault = expected.car == car && expected.penalty != Penalty::None;
        const bool lies = atFault && rng.uniform() >= honesty; // only someone with something to lose bends the story
        Statement statement;
        statement.car = car;
        for (const Evidence& evidence : hearing.evidence) {
            const double tolerance = evidenceTolerance(evidence.kind, evidence.value);
            Claim claim;
            claim.kind = evidence.kind;
            if (evidence.kind == EvidenceKind::Breach) {
                claim.value = lies ? 0.0 : 1.0; // "there was no flag" / "we did nothing wrong"
            } else if (lies) {
                const double shift = tolerance * rng.uniform(kLieMin, kLieMax);
                const bool lower = favourable(evidence.kind, side == 0, incident.type) == Favour::Lower;
                claim.value = lower ? evidence.value - shift : evidence.value + shift;
                if (claim.value < 0.0) {
                    claim.value = 0.0; // nothing can be less than nothing: this claim may then still fit
                }
            } else {
                claim.value = std::max(0.0, evidence.value + tolerance * kHonestSpread * rng.uniform(-1.0, 1.0));
            }
            statement.claims.push_back(claim);
        }
        hearing.statements.push_back(statement);
    }
    return hearing;
}

} // namespace uisim
