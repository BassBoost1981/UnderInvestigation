// M5 acceptance: penalties applied in the race, rating after the race (docs/DESIGN.md 8).

#include "uisim/DataDir.h"
#include "uisim/DataLoader.h"
#include "uisim/Rating.h"
#include "uisim/Sim.h"

#include <doctest/doctest.h>

#include <functional>

using namespace uisim;

namespace {

Sim makeRace(std::uint64_t seed, int laps = 20) {
    const std::filesystem::path data(UISIM_DATA_DIR);
    RaceConfig config;
    config.laps = laps;
    config.seed = seed;
    // Penalty tests compare two races; random accidents would make one of them diverge.
    config.accidentChanceScale = 0.0;
    config.followUpChanceScale = 0.0;
    return Sim(loadTrack(data / "tracks" / "testring.json"), loadEntries(data / "entries" / "fictional-grid.json"), config);
}

Command verdict(int incidentId, int car, Penalty penalty) {
    Command c;
    c.type = Command::Type::IssueVerdict;
    c.incidentId = incidentId;
    c.car = car;
    c.penalty = penalty;
    return c;
}

// Runs to the end; `onNewIncident` may issue commands as incidents appear.
void runToEnd(Sim& sim, const std::function<void(Sim&, const Incident&)>& onNewIncident = {}) {
    std::size_t seen = 0;
    for (long steps = 0; !sim.finished() && steps < 2'000'000; ++steps) {
        sim.step();
        while (onNewIncident && seen < sim.incidents().size()) {
            onNewIncident(sim, sim.incidents()[seen++]);
        }
    }
    REQUIRE(sim.finished());
}

const CarState& stateOf(const Sim& sim, int entry) {
    for (const CarState& car : sim.snapshot().cars) {
        if (car.entryIndex == entry) {
            return car;
        }
    }
    FAIL("entry not found");
    return sim.snapshot().cars.front();
}

// Race time lost by `entry` through a penalty given at the first incident.
double timeLost(Penalty penalty, int entry, bool* sawPitLane = nullptr) {
    Sim clean = makeRace(1);
    runToEnd(clean);
    Sim penalised = makeRace(1);
    bool pit = false;
    std::size_t seen = 0;
    for (long steps = 0; !penalised.finished() && steps < 2'000'000; ++steps) {
        penalised.step();
        if (seen == 0 && !penalised.incidents().empty()) {
            penalised.command(verdict(penalised.incidents().front().id, entry, penalty));
            seen = 1;
        }
        for (const CarState& car : penalised.snapshot().cars) {
            if (car.entryIndex == entry && car.inPitLane) {
                pit = true;
                CHECK(car.lateral < -1.0);
            }
        }
    }
    REQUIRE(penalised.finished());
    if (sawPitLane) {
        *sawPitLane = pit;
    }
    CHECK(stateOf(penalised, entry).pendingPenalty == Penalty::None);
    return stateOf(penalised, entry).lastLineTime - stateOf(clean, entry).lastLineTime;
}

} // namespace

TEST_CASE("Time penalty changes the final classification") {
    Sim clean = makeRace(1);
    runToEnd(clean);
    const int winner = clean.snapshot().cars[0].entryIndex;
    const double margin = clean.snapshot().cars[1].gapToLeader;
    REQUIRE(margin < 10.0);

    Sim sim = makeRace(1);
    runToEnd(sim, [&](Sim& s, const Incident& incident) {
        if (incident.id == 1) {
            s.command(verdict(1, winner, Penalty::TenSeconds));
        }
    });
    REQUIRE_FALSE(sim.incidents().empty());
    // B1.9.6 b: served at the next stop before any work, so nothing is left on the race time.
    CHECK(stateOf(sim, winner).timePenalty == 0.0);
    CHECK(stateOf(sim, winner).pitStops >= 1);
    const double lost = stateOf(sim, winner).lastLineTime - stateOf(clean, winner).lastLineTime;
    INFO("time lost by serving 10 s: ", lost);
    CHECK(lost > 7.0);
    CHECK(lost < 18.0); // plus the traffic it rejoins in
}

TEST_CASE("A time penalty after the last stop is added to the race time") {
    Sim sim = makeRace(1);
    for (long steps = 0; !sim.finished() && sim.snapshot().leaderLap < sim.snapshot().totalLaps - 2; ++steps) {
        sim.step();
    }
    REQUIRE_FALSE(sim.incidents().empty());
    const int entry = sim.snapshot().cars[0].entryIndex; // leader: no stop left this late
    sim.command(verdict(sim.incidents().front().id, entry, Penalty::FiveSeconds));
    runToEnd(sim);
    CHECK(stateOf(sim, entry).timePenalty == 5.0); // B1.9.6 b ii
}

TEST_CASE("Drive-through is served in the pit lane and costs about 20 s") {
    Sim probe = makeRace(1);
    runToEnd(probe);
    const int entry = probe.snapshot().cars[5].entryIndex;
    bool pit = false;
    const double lost = timeLost(Penalty::DriveThrough, entry, &pit);
    INFO("drive-through cost ", lost, " s");
    CHECK(pit);
    CHECK(lost > 12.0);
    CHECK(lost < 30.0);

    const double lostStopGo = timeLost(Penalty::StopAndGo, entry);
    INFO("stop-and-go cost ", lostStopGo, " s");
    CHECK(lostStopGo - lost > 6.0);
    CHECK(lostStopGo - lost < 19.0); // 10 s stationary plus stopping, pulling away and traffic
}

TEST_CASE("Penalties after the flag become time; one verdict per incident") {
    Sim sim = makeRace(1);
    runToEnd(sim);
    REQUIRE(sim.incidents().size() >= 2);
    const int entry = sim.incidents()[0].car;
    sim.command(verdict(1, entry, Penalty::DriveThrough));
    sim.command(verdict(1, entry, Penalty::TenSeconds)); // second decision on #1: ignored
    sim.step();
    CHECK(stateOf(sim, entry).timePenalty == 20.0);
    REQUIRE(sim.verdicts().size() == 1);
    CHECK(sim.verdicts()[0].afterRace);
    CHECK(rateRace(sim).raceQuality < 1.0);
}

TEST_CASE("Rating: verdicts matching the ground truth score full marks") {
    Sim sim = makeRace(1, 50);
    runToEnd(sim, [](Sim& s, const Incident& incident) {
        const ExpectedVerdict expected = expectedVerdict(incident);
        s.command(verdict(incident.id, expected.car, expected.penalty));
    });
    const RaceRating rating = rateRace(sim);
    REQUIRE_FALSE(rating.incidents.empty());
    CHECK(rating.decided == static_cast<int>(rating.incidents.size()));
    CHECK(rating.fairness == doctest::Approx(1.0));
    CHECK(rating.consistency == doctest::Approx(1.0));
    CHECK(rating.raceQuality == doctest::Approx(1.0));
    CHECK(rating.overall == doctest::Approx(100.0));
}

TEST_CASE("Rating: penalising the wrong driver is punished") {
    Sim sim = makeRace(1, 50);
    runToEnd(sim, [](Sim& s, const Incident& incident) {
        // Always 10 s for the defender, whatever happened.
        s.command(verdict(incident.id, incident.otherCar, Penalty::TenSeconds));
    });
    const RaceRating rating = rateRace(sim);
    INFO("fairness ", rating.fairness, ", overall ", rating.overall);
    CHECK(rating.fairness < 0.4);
    CHECK(rating.overall < 75.0);
}

TEST_CASE("Rating: long neutralisation lowers race quality") {
    Sim sim = makeRace(1);
    for (long steps = 0; steps < 6000; ++steps) {
        sim.step();
    }
    Command vsc;
    vsc.type = Command::Type::DeployVirtualSafetyCar;
    sim.command(vsc);
    runToEnd(sim); // VSC never ends
    const RaceRating rating = rateRace(sim);
    INFO("race quality ", rating.raceQuality);
    CHECK(rating.raceQuality < 0.2);
}
