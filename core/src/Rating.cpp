#include "uisim/Rating.h"

#include <algorithm>
#include <cmath>

namespace uisim {
namespace {

constexpr double kScorePerSeverityStep = 0.34; // one step off (e.g. 5 s instead of 10 s) costs a third
constexpr double kSimilarBlame = 0.15;          // incidents this close in blame should be judged alike
constexpr double kAcceptedNeutralisation = 0.05; // share of the race under VSC/SC/red that costs nothing
constexpr double kAfterRaceDecisionCost = 0.1;
constexpr double kFullReactionTime = 20.0; // seconds after the accident: radio delay plus a quick reaction
constexpr double kZeroReactionTime = 120.0;
constexpr double kFollowUpCost = 0.15;
constexpr double kHeavyRainCostPer10Min = 0.5; // safety lost per 10 minutes of racing in standing water

int severity(Penalty penalty) { return static_cast<int>(penalty); }

double score(const ExpectedVerdict& expected, int givenCar, Penalty given) {
    const int party = severity(given) > 0 ? givenCar : -1; // "no action" has no party
    const int level = party < 0 ? 0 : severity(given);
    const int expectedLevel = severity(expected.penalty);
    if (expected.car < 0) {
        return std::max(0.0, 1.0 - kScorePerSeverityStep * level);
    }
    if (party == expected.car) {
        return std::max(0.0, 1.0 - kScorePerSeverityStep * std::abs(level - expectedLevel));
    }
    if (party < 0) {
        return std::max(0.0, 1.0 - kScorePerSeverityStep * expectedLevel); // let off too lightly
    }
    return 0.0; // the wrong driver was penalised
}

// Which side a verdict penalised: 1 = incident.car, 2 = incident.otherCar, 0 = no one.
int role(const Incident& incident, int car, Penalty penalty) {
    if (severity(penalty) == 0 || car < 0) {
        return 0;
    }
    return car == incident.car ? 1 : car == incident.otherCar ? 2 : 3;
}

} // namespace

ExpectedVerdict expectedVerdict(const Incident& incident) {
    using Type = Incident::Type;
    switch (incident.type) {
    case Type::Collision:
        if (incident.blame >= 0.8) {
            return {incident.car, Penalty::TenSeconds};
        }
        if (incident.blame >= 0.6) {
            return {incident.car, Penalty::FiveSeconds};
        }
        if (incident.blame <= 0.2) {
            return {incident.otherCar, Penalty::FiveSeconds};
        }
        return {-1, Penalty::None}; // racing incident
    case Type::OvertakeUnderYellow:
    case Type::OvertakeUnderVsc:
    case Type::OvertakeUnderSafetyCar:
        return {incident.car, Penalty::TenSeconds};
    case Type::OvertakeUnderRedFlag:
        return {incident.car, Penalty::DriveThrough};
    case Type::IgnoringBlueFlags:
        return {incident.car, Penalty::FiveSeconds};
    case Type::OvertakeBeforeRestartLine:
        return {incident.car, Penalty::FiveSeconds};
    case Type::Accident:
        return {-1, Penalty::None};
    case Type::PitLaneSpeeding: { // Penalty Guidelines tariff (data/rules: pit_speeding_*)
        const RaceRules rules;
        if (incident.measure > rules.pitSpeedingStopAndGoAboveKmh) {
            return {incident.car, Penalty::StopAndGo};
        }
        return {incident.car, incident.measure >= rules.pitSpeedingDriveThroughFromKmh ? Penalty::DriveThrough
                                                                                      : Penalty::FiveSeconds};
    }
    case Type::TrackLimits: // Penalty Guidelines: 5 s for the offence after the black-and-white flag and each further one
        return {incident.car, Penalty::FiveSeconds};
    case Type::CompoundRule: // B6.3.6
        return {incident.car, Penalty::Disqualification};
    case Type::PitEntryClosed: // B1.6.4 b (data/rules: pit_entry_closed_penalty)
        return {incident.car, RaceRules{}.pitEntryClosedPenalty};
    case Type::WetTyreRule: // B6.3.7 (data/rules: wet_start_tyre_change_penalty)
        return {incident.car, RaceRules{}.wetStartTyreChangePenalty};
    case Type::UnsafeRelease: // 5 s (unsafe_release_penalty_seconds), 10 s if it nearly ended in contact
        return {incident.car, incident.measure < 10.0 ? Penalty::TenSeconds : Penalty::FiveSeconds};
    }
    return {};
}

RaceRating rateRace(const Sim& sim) {
    RaceRating rating;
    const auto& incidents = sim.incidents();
    double total = 0.0;
    int rated = 0;
    for (const Incident& incident : incidents) {
        IncidentRating r;
        r.incidentId = incident.id;
        r.expected = expectedVerdict(incident);
        const auto verdict = std::find_if(sim.verdicts().begin(), sim.verdicts().end(),
            [&](const Verdict& v) { return v.incidentId == incident.id; });
        if (verdict != sim.verdicts().end()) {
            r.decided = true;
            r.givenCar = verdict->car;
            r.givenPenalty = verdict->penalty;
            ++rating.decided;
        }
        r.score = score(r.expected, r.givenCar, r.givenPenalty);
        // An accident without contact only counts when the stewards decided on it.
        if (incident.type != Incident::Type::Accident || r.decided) {
            total += r.score;
            ++rated;
        }
        rating.incidents.push_back(r);
    }
    if (rated > 0) {
        rating.fairness = total / static_cast<double>(rated);
    }

    // Consistency over decided pairs of the same kind with similar blame.
    int pairs = 0;
    int consistent = 0;
    for (std::size_t a = 0; a < incidents.size(); ++a) {
        for (std::size_t b = a + 1; b < incidents.size(); ++b) {
            const IncidentRating& ra = rating.incidents[a];
            const IncidentRating& rb = rating.incidents[b];
            if (!ra.decided || !rb.decided || incidents[a].type != incidents[b].type ||
                std::abs(incidents[a].blame - incidents[b].blame) > kSimilarBlame) {
                continue;
            }
            ++pairs;
            // Verdicts may differ only as much as the truth justifies (a threshold can lie between them).
            auto difference = [&](int carA, Penalty penA, int carB, Penalty penB) {
                const int sides = role(incidents[a], carA, penA) == role(incidents[b], carB, penB) ? 0 : 2;
                return sides + std::abs(severity(penA) - severity(penB));
            };
            const int given = difference(ra.givenCar, ra.givenPenalty, rb.givenCar, rb.givenPenalty);
            const int deserved = difference(ra.expected.car, ra.expected.penalty, rb.expected.car, rb.expected.penalty);
            if (given <= deserved + 1) {
                ++consistent;
            }
        }
    }
    if (pairs > 0) {
        rating.consistency = static_cast<double>(consistent) / pairs;
    }

    // Race quality: too much neutralisation is boring, decisions after the flag annoy everyone.
    const double raceTime = sim.finishTime() > 0.0 ? sim.finishTime() : sim.snapshot().time;
    const double share = raceTime > 0.0 ? sim.neutralisedTime() / raceTime : 0.0;
    double quality = 1.0 - std::max(0.0, share - kAcceptedNeutralisation) * 3.0;
    for (const Verdict& verdict : sim.verdicts()) {
        if (verdict.afterRace) {
            quality -= kAfterRaceDecisionCost;
        }
    }
    rating.raceQuality = std::clamp(quality, 0.0, 1.0);

    // Safety: how fast each accident site got a sufficient flag, minus follow-up accidents.
    double reaction = 0.0;
    for (const Accident& accident : sim.accidents()) {
        if (accident.causedBy != 0) {
            ++rating.followUpAccidents;
        }
        if (accident.type == AccidentType::Spin) {
            continue; // a spun car drives on: nothing to protect
        }
        ++rating.accidentSites;
        if (accident.protectedAfter >= 0.0) {
            reaction += std::clamp((kZeroReactionTime - accident.protectedAfter) / (kZeroReactionTime - kFullReactionTime),
                                   0.0, 1.0);
        }
    }
    const double reactionScore = rating.accidentSites > 0 ? reaction / rating.accidentSites : 1.0;
    // Racing on in standing water instead of neutralising (M9.4).
    rating.heavyRainSeconds = sim.stats().heavyRainUnneutralised;
    rating.safety = std::clamp(reactionScore - kFollowUpCost * rating.followUpAccidents -
                                   kHeavyRainCostPer10Min * rating.heavyRainSeconds / 600.0,
                               0.0, 1.0);

    rating.overall = 100.0 * (0.35 * rating.fairness + 0.15 * rating.consistency + 0.3 * rating.safety +
                              0.2 * rating.raceQuality);
    return rating;
}

} // namespace uisim
