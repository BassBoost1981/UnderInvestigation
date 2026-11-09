// M8.2: lapped cars may overtake under the Safety Car (B5.13.4 c), SC in a lap later (B5.13.5 b).

#include "uisim/DataDir.h"
#include "uisim/DataLoader.h"
#include "uisim/Sim.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <map>
#include <set>
#include <vector>

using namespace uisim;

namespace {

Sim makeRace(std::uint64_t seed, bool autoRelease = false) {
    const std::filesystem::path data(UISIM_DATA_DIR);
    RaceConfig config;
    config.laps = 50;
    config.seed = seed;
    config.rules = loadRules(data / "rules" / "fia-f1-2026.json");
    config.ruleBreakChance = 0.0;     // only the released cars may pass
    config.autoReleaseLappedCars = autoRelease;
    config.accidentChanceScale = 0.0; // no stranded cars in the queue
    return Sim(loadTrack(data / "tracks" / "testring.json"), loadEntries(data / "entries" / "fictional-grid.json"),
               config);
}

Command simple(Command::Type type) {
    Command c;
    c.type = type;
    return c;
}

void runUntilLeaderLap(Sim& sim, int lap) {
    for (long steps = 0; sim.snapshot().leaderLap < lap && !sim.finished() && steps < 2'000'000; ++steps) {
        sim.step();
    }
    REQUIRE(sim.snapshot().leaderLap >= lap);
}

struct Outcome {
    int released = 0;           // cars that had to pass
    int rejoinedInTime = 0;     // of those, in the queue at the restart
    int orderErrors = 0;        // a released car ahead of a lead-lap car in the queue
    int lapsUntilGreen = 0;     // leader laps from the release to the restart
    bool inWithoutRecall = false;
    int violations = 0;
    int lappedAtRelease = 0;    // cars with lapsDown > 0 when the message is sent
    std::vector<int> finishOrder;
};

// SC at lap 40, release at lap 42, then no further command: the SC comes in by itself.
Outcome runScenario(std::uint64_t seed) {
    Sim sim = makeRace(seed);
    runUntilLeaderLap(sim, 40);
    sim.command(simple(Command::Type::DeploySafetyCar));
    runUntilLeaderLap(sim, 42);
    REQUIRE(sim.snapshot().state == RaceState::SafetyCar);

    std::map<int, int> stops;
    std::set<int> leadLap;
    for (const CarState& car : sim.snapshot().cars) {
        stops[car.entryIndex] = car.pitStops;
    }
    sim.command(simple(Command::Type::LappedCarsMayOvertake));
    sim.step();
    Outcome out;
    const SimSnapshot& atRelease = sim.snapshot();
    CHECK(atRelease.lappedCarsReleased);
    std::set<int> released;
    for (const CarState& car : atRelease.cars) {
        out.lappedAtRelease += car.lapsDown > 0 && !car.retired ? 1 : 0;
        if (car.unlapping) {
            released.insert(car.entryIndex);
        } else if (car.lapsDown == 0 && !car.retired && !car.inPitLane) {
            leadLap.insert(car.entryIndex);
        }
    }
    out.released = static_cast<int>(released.size());
    const int releaseLap = atRelease.leaderLap;

    SimSnapshot lastUnderSc = atRelease;
    for (long steps = 0; sim.snapshot().state == RaceState::SafetyCar && steps < 200'000; ++steps) {
        lastUnderSc = sim.snapshot();
        out.inWithoutRecall = out.inWithoutRecall || lastUnderSc.safetyCarInThisLap;
        sim.step();
    }
    REQUIRE(sim.snapshot().state == RaceState::Green);
    out.lapsUntilGreen = sim.snapshot().leaderLap - releaseLap;

    // The queue at the restart: cars behind the SC, front to back, while the gaps stay small.
    std::vector<std::pair<double, int>> behind;
    for (const CarState& car : lastUnderSc.cars) {
        if (!car.retired && !car.finished && !car.inPitLane && car.pitStops == stops[car.entryIndex]) {
            behind.push_back({sim.track().wrap(lastUnderSc.safetyCarDistance - car.distance), car.entryIndex});
        }
    }
    std::sort(behind.begin(), behind.end());
    bool releasedSeen = false;
    for (std::size_t k = 0; k < behind.size(); ++k) {
        if (k > 0 && behind[k].first - behind[k - 1].first > 60.0) {
            break; // end of the queue
        }
        const int entry = behind[k].second;
        if (released.count(entry) != 0) {
            releasedSeen = true;
            ++out.rejoinedInTime;
        } else if (leadLap.count(entry) != 0 && releasedSeen) {
            ++out.orderErrors;
        }
    }

    for (long steps = 0; !sim.finished() && steps < 2'000'000; ++steps) {
        sim.step();
    }
    out.violations = sim.stats().flagViolations;
    for (const CarState& car : sim.snapshot().cars) {
        out.finishOrder.push_back(car.entryIndex);
    }
    return out;
}

} // namespace

TEST_CASE("Lapped cars under the SC: they pass, rejoin the back, the SC comes in a lap later") {
    int released = 0;
    int rejoined = 0;
    constexpr int kSeeds = 8;
    for (std::uint64_t seed = 1; seed <= kSeeds; ++seed) {
        const Outcome out = runScenario(seed);
        INFO("seed ", seed, ": lapped ", out.lappedAtRelease, ", released ", out.released, ", in the queue at the restart ",
             out.rejoinedInTime, ", leader laps until green ", out.lapsUntilGreen);
        CHECK(out.orderErrors == 0);    // lead-lap cars ahead of every released car that made it
        CHECK(out.violations == 0);     // passing on the SC's green light is no offence
        CHECK(out.inWithoutRecall);     // B5.13.5 b: no "SC in this lap" command needed
        CHECK(out.lapsUntilGreen >= 1); // end of the following lap, not at once
        CHECK(out.lapsUntilGreen <= 2);
        released += out.released;
        rejoined += out.rejoinedInTime;
    }
    INFO("released ", released, ", rejoined in time ", rejoined);
    CHECK(released >= kSeeds); // at lap 42 of 50 there are lapped cars in the queue
    CHECK(rejoined >= released / 3); // the SC does not wait (B5.13.5 b); 10 of 18 make it today
}

TEST_CASE("Lapped cars under the SC: deterministic") {
    const Outcome a = runScenario(3);
    const Outcome b = runScenario(3);
    CHECK(a.finishOrder == b.finishOrder);
    CHECK(a.released == b.released);
}

TEST_CASE("Lapped cars may overtake: ignored outside a Safety Car period") {
    Sim sim = makeRace(1);
    runUntilLeaderLap(sim, 25);
    sim.command(simple(Command::Type::LappedCarsMayOvertake));
    sim.step();
    CHECK_FALSE(sim.snapshot().lappedCarsReleased);
    for (const CarState& car : sim.snapshot().cars) {
        CHECK_FALSE(car.unlapping);
    }
}

TEST_CASE("Lapped cars under the SC: released automatically if the player does nothing (M9.5)") {
    int released = 0;
    int inWithoutCommand = 0;
    for (std::uint64_t seed = 1; seed <= 4; ++seed) {
        Sim sim = makeRace(seed, true);
        runUntilLeaderLap(sim, 40);
        sim.command(simple(Command::Type::DeploySafetyCar));
        bool anyUnlapping = false;
        for (long steps = 0; sim.snapshot().leaderLap < 46 && !sim.finished() && steps < 2'000'000; ++steps) {
            sim.step();
            for (const CarState& car : sim.snapshot().cars) {
                anyUnlapping = anyUnlapping || car.unlapping;
            }
            if (sim.snapshot().state == RaceState::Green) {
                break; // the SC came in by itself
            }
        }
        released += anyUnlapping ? 1 : 0;
        inWithoutCommand += sim.snapshot().state == RaceState::Green ? 1 : 0;
        CHECK(sim.stats().flagViolations == 0);
    }
    CHECK(released == 4);
    CHECK(inWithoutCommand == 4); // B5.13.5 b after the release, no "in this lap" command
}

TEST_CASE("Lapped cars under the SC: no automatic release without lapped cars in the queue") {
    Sim sim = makeRace(1, true);
    runUntilLeaderLap(sim, 3); // nobody lapped yet
    sim.command(simple(Command::Type::DeploySafetyCar));
    for (long steps = 0; sim.snapshot().leaderLap < 7 && steps < 2'000'000; ++steps) {
        sim.step();
    }
    CHECK(sim.snapshot().state == RaceState::SafetyCar); // stays out until race control calls it in
    CHECK_FALSE(sim.snapshot().lappedCarsReleased);
}
