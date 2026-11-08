// M8.3: breaking the two-compound rule (B6.3.6) and closing the pit entry (B1.6.4).

#include "uisim/DataDir.h"
#include "uisim/DataLoader.h"
#include "uisim/Rating.h"
#include "uisim/Sim.h"

#include <doctest/doctest.h>

#include <filesystem>
#include <map>

using namespace uisim;

namespace {

Sim makeRace(std::uint64_t seed, double compoundRuleMissChance) {
    const std::filesystem::path data(UISIM_DATA_DIR);
    RaceConfig config;
    config.laps = 50;
    config.seed = seed;
    config.rules = loadRules(data / "rules" / "fia-f1-2026.json");
    config.compoundRuleMissChance = compoundRuleMissChance;
    return Sim(loadTrack(data / "tracks" / "testring.json"), loadEntries(data / "entries" / "fictional-grid.json"),
               config);
}

void runToEnd(Sim& sim) {
    for (long steps = 0; !sim.finished() && steps < 2'000'000; ++steps) {
        sim.step();
    }
    REQUIRE(sim.finished());
}

void runUntilLeaderLap(Sim& sim, int lap) {
    for (long steps = 0; sim.snapshot().leaderLap < lap && !sim.finished() && steps < 2'000'000; ++steps) {
        sim.step();
    }
    REQUIRE(sim.snapshot().leaderLap >= lap);
}

Command simple(Command::Type type) {
    Command c;
    c.type = type;
    return c;
}

std::map<int, int> incidentsPerCar(const Sim& sim, Incident::Type type) {
    std::map<int, int> count;
    for (const Incident& incident : sim.incidents()) {
        if (incident.type == type) {
            CHECK(incident.car == incident.otherCar);
            ++count[incident.car];
        }
    }
    return count;
}

} // namespace

TEST_CASE("Two-compound rule: no strategy error, no breach") {
    for (std::uint64_t seed = 1; seed <= 5; ++seed) {
        Sim sim = makeRace(seed, 0.0);
        runToEnd(sim);
        for (const CarState& car : sim.snapshot().cars) {
            if (car.finished) {
                CHECK(car.compoundsUsed >= 2);
            }
        }
        CHECK(incidentsPerCar(sim, Incident::Type::CompoundRule).empty());
    }
}

TEST_CASE("Two-compound rule: every breach is one incident, disqualification is the ground truth") {
    int breaches = 0;
    for (std::uint64_t seed = 1; seed <= 5; ++seed) {
        Sim sim = makeRace(seed, 1.0); // every team overlooks the rule
        runToEnd(sim);
        auto perCar = incidentsPerCar(sim, Incident::Type::CompoundRule);
        for (const CarState& car : sim.snapshot().cars) {
            if (car.finished) {
                CHECK(perCar[car.entryIndex] == (car.compoundsUsed < 2 ? 1 : 0));
            }
        }
        for (const Incident& incident : sim.incidents()) {
            if (incident.type == Incident::Type::CompoundRule) {
                ++breaches;
                CHECK(incident.measure == 1.0);
                const ExpectedVerdict expected = expectedVerdict(incident);
                CHECK(expected.car == incident.car);
                CHECK(expected.penalty == Penalty::Disqualification);
            }
        }
    }
    INFO("breaches in 5 races: ", breaches);
    CHECK(breaches >= 5);
}

TEST_CASE("Disqualification removes the car from the results") {
    Sim sim = makeRace(1, 1.0);
    runToEnd(sim);
    const Incident* breach = nullptr;
    for (const Incident& incident : sim.incidents()) {
        if (incident.type == Incident::Type::CompoundRule) {
            breach = &incident;
            break;
        }
    }
    REQUIRE(breach != nullptr);
    const int car = breach->car;
    Command verdict;
    verdict.type = Command::Type::IssueVerdict;
    verdict.incidentId = breach->id;
    verdict.car = car;
    verdict.penalty = Penalty::Disqualification;
    sim.command(verdict);
    sim.step();
    const auto& cars = sim.snapshot().cars;
    CHECK(cars.back().entryIndex == car);
    CHECK(cars.back().disqualified);
    for (std::size_t k = 0; k + 1 < cars.size(); ++k) {
        CHECK_FALSE(cars[k].disqualified);
    }
}

TEST_CASE("Closed pit entry: stops wait, entering anyway is an incident (stop-and-go)") {
    int offences = 0;
    int stopsWhileClosed = 0;
    for (std::uint64_t seed = 1; seed <= 6; ++seed) {
        Sim sim = makeRace(seed, 0.0);
        runUntilLeaderLap(sim, 10);
        sim.command(simple(Command::Type::ClosePitEntry));
        sim.step();
        CHECK(sim.snapshot().pitEntryClosed);
        std::map<int, int> stopsAtClose;
        std::map<int, bool> inPitAtClose;
        for (const CarState& car : sim.snapshot().cars) {
            stopsAtClose[car.entryIndex] = car.pitStops;
            inPitAtClose[car.entryIndex] = car.inPitLane;
        }
        const auto before = sim.incidents().size();
        runUntilLeaderLap(sim, 25);
        sim.command(simple(Command::Type::OpenPitEntry));
        sim.step();
        CHECK_FALSE(sim.snapshot().pitEntryClosed);

        std::map<int, int> entered; // closed-entry incidents per car during the closure
        for (std::size_t k = before; k < sim.incidents().size(); ++k) {
            const Incident& incident = sim.incidents()[k];
            if (incident.type == Incident::Type::PitEntryClosed) {
                ++entered[incident.car];
                ++offences;
                CHECK(expectedVerdict(incident).penalty == Penalty::StopAndGo);
            }
        }
        for (const CarState& car : sim.snapshot().cars) {
            const int stops = car.pitStops - stopsAtClose[car.entryIndex];
            stopsWhileClosed += stops;
            // Only a car that was already in the pit lane or entered against the closure may stop.
            CHECK(stops <= entered[car.entryIndex] + (inPitAtClose[car.entryIndex] ? 1 : 0));
        }
        runToEnd(sim);
        for (const CarState& car : sim.snapshot().cars) {
            if (car.finished) {
                CHECK(car.compoundsUsed >= 2); // the delayed stops still happen
            }
        }
    }
    INFO("offences ", offences, ", stops while closed ", stopsWhileClosed);
    CHECK(offences >= 1); // rare, but it happens
    CHECK(offences <= 30);
}
