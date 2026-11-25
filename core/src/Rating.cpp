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
constexpr double kReviewScore = 0.5;    // a verdict scoring less than this is clearly wrong (right of review)
constexpr double kOverturnedCost = 5.0; // points of the overall rating per verdict set aside
constexpr double kGridPlacesPerStep = 3.0; // grid drops: this many places too many or too few cost one step

// Steps of severity. The race penalties count in their order; the session verdicts (M13.5) sit on the same
// scale above a reprimand: deleting the lap like 5 s, a grid drop like 10 s.
int severity(Penalty penalty) {
    switch (penalty) {
    case Penalty::DeleteLap:
        return 2;
    case Penalty::GridDrop:
        return 3;
    default:
        return static_cast<int>(penalty);
    }
}

double score(const ExpectedVerdict& expected, int givenCar, Penalty given, int givenPlaces) {
    const int party = severity(given) > 0 ? givenCar : -1; // "no action" has no party
    const int level = party < 0 ? 0 : severity(given);
    const int expectedLevel = severity(expected.penalty);
    if (expected.car < 0) {
        return std::max(0.0, 1.0 - kScorePerSeverityStep * level);
    }
    if (party == expected.car) {
        double steps = std::abs(level - expectedLevel);
        if (given == Penalty::GridDrop && expected.penalty == Penalty::GridDrop) {
            steps += std::abs(givenPlaces - expected.places) / kGridPlacesPerStep;
        }
        return std::max(0.0, 1.0 - kScorePerSeverityStep * steps);
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

// A jump start earlier than this is a clear advantage: 10 s instead of 5 s (sim, B5.11).
constexpr double kFalseStartSerious = 0.2;
// Qualifying (M13.5, sim): held up this long is a grid drop, this long a reprimand, less no action.
constexpr double kImpedingGridDropSeconds = 2.0;
constexpr double kImpedingReprimandSeconds = 0.8;
// Single yellow on a timed lap: more than this share of the free pace (in %) is not slowing down.
constexpr double kYellowLiftedEnough = 97.0;

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
    case Type::FalseStart: // B5.11 (data/rules: false_start_penalty_seconds, false_start_serious_penalty_seconds)
        return {incident.car, incident.measure > kFalseStartSerious ? Penalty::TenSeconds : Penalty::FiveSeconds};
    case Type::GridPosition: // B5.11: outside the grid box
        return {incident.car, Penalty::FiveSeconds};
    case Type::UnsafeRelease: // 5 s (unsafe_release_penalty_seconds), 10 s if it nearly ended in contact
        return {incident.car, incident.measure < 10.0 ? Penalty::TenSeconds : Penalty::FiveSeconds};
    case Type::Impeding: // B4.1.1: places as the stewards see fit (data/rules: qualifying_impeding_grid_drop)
        if (incident.measure >= kImpedingGridDropSeconds) {
            return {incident.car, Penalty::GridDrop, RaceRules{}.qualifyingImpedingGridDrop};
        }
        return incident.measure >= kImpedingReprimandSeconds ? ExpectedVerdict{incident.car, Penalty::Warning}
                                                              : ExpectedVerdict{};
    case Type::LapTrackLimits: // B1.9.4: the lap time is deleted
        return {incident.car, Penalty::DeleteLap};
    case Type::YellowFlagLap: // B1.8.4: a driver must slow down for a single yellow; did not lift enough: lap deleted
        return incident.measure > kYellowLiftedEnough ? ExpectedVerdict{incident.car, Penalty::DeleteLap} : ExpectedVerdict{};
    }
    return {};
}

namespace {

// One incident of the rated weekend with the session it happened in.
struct Rated {
    const Incident* incident;
    const Sim* session;
};

// The rating of a race, or of a weekend when `qualifying` is given (M13.6): the incidents and accident
// sites of both sessions count, race quality comes from the race alone.
RaceRating rate(const Sim* qualifying, const Sim& sim) {
    RaceRating rating;
    std::vector<Rated> incidents;
    for (const Sim* session : {qualifying, &sim}) {
        if (session) {
            for (const Incident& incident : session->incidents()) {
                incidents.push_back({&incident, session});
            }
        }
    }
    double total = 0.0;
    int rated = 0;
    for (const Rated& entry : incidents) {
        const Incident& incident = *entry.incident;
        const std::vector<Verdict>& verdicts = entry.session->verdicts();
        IncidentRating r;
        r.incidentId = incident.id;
        r.qualifying = entry.session == qualifying;
        r.expected = expectedVerdict(incident);
        const auto verdict = std::find_if(verdicts.begin(), verdicts.end(),
            [&](const Verdict& v) { return v.incidentId == incident.id; });
        if (verdict != verdicts.end()) {
            r.decided = true;
            r.givenCar = verdict->car;
            r.givenPenalty = verdict->penalty;
            r.givenPlaces = verdict->places;
            ++rating.decided;
        }
        r.score = score(r.expected, r.givenCar, r.givenPenalty, r.givenPlaces);
        r.heard = incident.heard;
        r.overturned = r.decided && !r.heard && r.score < kReviewScore;
        rating.overturned += r.overturned ? 1 : 0;
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
            const Incident& ia = *incidents[a].incident;
            const Incident& ib = *incidents[b].incident;
            const IncidentRating& ra = rating.incidents[a];
            const IncidentRating& rb = rating.incidents[b];
            if (!ra.decided || !rb.decided || ia.type != ib.type || std::abs(ia.blame - ib.blame) > kSimilarBlame) {
                continue;
            }
            ++pairs;
            // Verdicts may differ only as much as the truth justifies (a threshold can lie between them).
            auto difference = [&](int carA, Penalty penA, int carB, Penalty penB) {
                const int sides = role(ia, carA, penA) == role(ib, carB, penB) ? 0 : 2;
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
        if (verdict.afterRace && !sim.snapshot().qualifying) { // after a session the stewards have time (M13.5)
            quality -= kAfterRaceDecisionCost;
        }
    }
    rating.raceQuality = std::clamp(quality, 0.0, 1.0);

    // Safety: how fast each accident site got a sufficient flag, minus follow-up accidents.
    double reaction = 0.0;
    for (const Sim* session : {qualifying, &sim}) {
        if (!session) {
            continue;
        }
        for (const Accident& accident : session->accidents()) {
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
    }
    const double reactionScore = rating.accidentSites > 0 ? reaction / rating.accidentSites : 1.0;
    // Racing on in standing water instead of neutralising (M9.4).
    rating.heavyRainSeconds = sim.stats().heavyRainUnneutralised;
    rating.safety = std::clamp(reactionScore - kFollowUpCost * rating.followUpAccidents -
                                   kHeavyRainCostPer10Min * rating.heavyRainSeconds / 600.0,
                               0.0, 1.0);

    rating.overall = 100.0 * (0.35 * rating.fairness + 0.15 * rating.consistency + 0.3 * rating.safety +
                              0.2 * rating.raceQuality);
    rating.overall = std::max(0.0, rating.overall - kOverturnedCost * rating.overturned);
    return rating;
}

} // namespace

RaceRating rateRace(const Sim& sim) { return rate(nullptr, sim); }

RaceRating rateWeekend(const Sim& qualifying, const Sim& race) { return rate(&qualifying, race); }

} // namespace uisim
