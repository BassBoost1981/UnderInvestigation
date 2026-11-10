// M10.3: standing start with lights (B5.7.1), start incidents (B5.11), standing start after
// formation laps behind the Safety Car (B5.10.8) and after a suspension (B5.15.4).

#include "uisim/DataDir.h"
#include "uisim/DataLoader.h"
#include "uisim/Rating.h"
#include "uisim/Sim.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <vector>

using namespace uisim;

namespace {

RaceConfig cleanConfig(std::uint64_t seed, int laps = 10) {
    RaceConfig config;
    config.laps = laps;
    config.seed = seed;
    config.jumpStartChance = 0.0;
    config.gridPositionErrorChance = 0.0;
    config.poorStartChance = 0.0;
    config.ruleBreakChance = 0.0; // nobody overtakes behind the Safety Car on purpose
    return config;
}

Sim makeRace(const RaceConfig& config) {
    const std::filesystem::path data(UISIM_DATA_DIR);
    return Sim(loadTrack(data / "tracks" / "testring.json"), loadEntries(data / "entries" / "fictional-grid.json"), config);
}

Command command(Command::Type type, bool standing = false) {
    Command c;
    c.type = type;
    c.standing = standing;
    return c;
}

int countIncidents(const Sim& sim, Incident::Type type) {
    return static_cast<int>(std::count_if(sim.incidents().begin(), sim.incidents().end(),
        [&](const Incident& incident) { return incident.type == type; }));
}

// Entry indices in road order, front first, of the cars still racing.
std::vector<int> roadOrder(const Sim& sim) {
    std::vector<int> order;
    for (const CarState& car : sim.snapshot().cars) { // sorted by position
        if (!car.retired && !car.inPitLane) {
            order.push_back(car.entryIndex);
        }
    }
    return order;
}

// Runs through a standing start that is forming or counting down. Checks the grid and the
// lights and returns the race time of the start signal.
double runThroughStandingStart(Sim& sim) {
    int lights = 0;
    bool sawAllLights = false;
    for (long steps = 0; sim.snapshot().gridStart && steps < 200'000; ++steps) {
        const SimSnapshot& s = sim.snapshot();
        CHECK(s.startLights >= lights); // one after the other, never back
        lights = s.startLights;
        sawAllLights = sawAllLights || lights == 5;
        if (lights > 0) { // the lights only start once every car has stopped
            for (const CarState& car : s.cars) {
                if (!car.retired && !car.inPitLane) {
                    CHECK(car.speed == 0.0);
                }
            }
        }
        sim.step();
    }
    CHECK(sawAllLights);
    CHECK_FALSE(sim.snapshot().gridStart);
    CHECK(sim.snapshot().startLights == 0); // lights out
    CHECK(sim.snapshot().state == RaceState::Green);
    return sim.snapshot().time;
}

} // namespace

TEST_CASE("Standing start: cars wait on the grid until the five lights go out") {
    for (std::uint64_t seed = 1; seed <= 5; ++seed) {
        Sim sim = makeRace(cleanConfig(seed));
        REQUIRE(sim.snapshot().gridStart);
        const double signal = runThroughStandingStart(sim);
        INFO("start signal at ", signal);
        CHECK(signal >= 5.5); // five lights one second apart, then 0.5 to 3 s
        CHECK(signal <= 8.1);
        CHECK(sim.incidents().empty());
        for (int k = 0; k < 20; ++k) { // one second later everyone is away
            sim.step();
        }
        for (const CarState& car : sim.snapshot().cars) {
            CHECK(car.speed > 0.0);
        }
    }
}

TEST_CASE("Standing start: the same seed gives the same start") {
    RaceConfig config;
    config.laps = 5;
    config.seed = 7;
    Sim a = makeRace(config);
    Sim b = makeRace(config);
    for (int k = 0; k < 1200; ++k) {
        a.step();
        b.step();
        REQUIRE(a.snapshot().startLights == b.snapshot().startLights);
    }
    for (std::size_t k = 0; k < a.snapshot().cars.size(); ++k) {
        CHECK(a.snapshot().cars[k].entryIndex == b.snapshot().cars[k].entryIndex);
        CHECK(a.snapshot().cars[k].distance == b.snapshot().cars[k].distance);
    }
}

TEST_CASE("Standing start: a jump start is exactly one incident with the right penalty") {
    RaceConfig config = cleanConfig(3);
    config.jumpStartChance = 1.0; // every driver goes early
    Sim sim = makeRace(config);
    bool movedBeforeSignal = false;
    for (long steps = 0; sim.snapshot().gridStart && steps < 200'000; ++steps) {
        for (const CarState& car : sim.snapshot().cars) {
            movedBeforeSignal = movedBeforeSignal || car.speed > 0.0;
        }
        CHECK(sim.incidents().empty()); // judged at the signal
        sim.step();
    }
    CHECK(movedBeforeSignal);
    std::map<int, int> perCar;
    bool sawFive = false;
    bool sawTen = false;
    for (const Incident& incident : sim.incidents()) {
        REQUIRE(incident.type == Incident::Type::FalseStart);
        CHECK(incident.car == incident.otherCar);
        ++perCar[incident.car];
        CHECK(incident.measure >= 0.05);
        CHECK(incident.measure <= 0.4);
        const ExpectedVerdict expected = expectedVerdict(incident);
        CHECK(expected.car == incident.car);
        CHECK(expected.penalty == (incident.measure > 0.2 ? Penalty::TenSeconds : Penalty::FiveSeconds)); // B5.11, guidelines
        sawFive = sawFive || expected.penalty == Penalty::FiveSeconds;
        sawTen = sawTen || expected.penalty == Penalty::TenSeconds;
    }
    CHECK(perCar.size() == sim.snapshot().cars.size());
    for (const auto& [car, count] : perCar) {
        CHECK(count == 1);
    }
    CHECK(sawFive);
    CHECK(sawTen);
    for (int k = 0; k < 2000; ++k) { // nothing is added later
        sim.step();
    }
    CHECK(countIncidents(sim, Incident::Type::FalseStart) == static_cast<int>(perCar.size()));
}

TEST_CASE("Standing start: a car outside its grid box is one incident, 5 s") {
    RaceConfig config = cleanConfig(4);
    config.gridPositionErrorChance = 1.0;
    Sim sim = makeRace(config);
    runThroughStandingStart(sim);
    CHECK(countIncidents(sim, Incident::Type::GridPosition) == static_cast<int>(sim.snapshot().cars.size()));
    for (const Incident& incident : sim.incidents()) {
        REQUIRE(incident.type == Incident::Type::GridPosition);
        CHECK(incident.measure >= 0.5);
        CHECK(incident.measure <= 1.5);
        CHECK(expectedVerdict(incident).penalty == Penalty::FiveSeconds);
    }
}

TEST_CASE("Standing start: start incidents are rare, position changes in lap 1 are plausible") {
    int falseStarts = 0;
    int gridErrors = 0;
    int collisions = 0; // contact on the first lap
    double moved = 0.0;
    int cars = 0;
    constexpr int kSeeds = 40;
    for (std::uint64_t seed = 1; seed <= kSeeds; ++seed) {
        RaceConfig config;
        config.laps = 3;
        config.seed = seed;
        config.accidentChanceScale = 0.0;
        Sim sim = makeRace(config);
        for (long steps = 0; sim.snapshot().leaderLap < 2 && steps < 200'000; ++steps) {
            sim.step();
        }
        falseStarts += countIncidents(sim, Incident::Type::FalseStart);
        gridErrors += countIncidents(sim, Incident::Type::GridPosition);
        collisions += countIncidents(sim, Incident::Type::Collision);
        int position = 0;
        for (const CarState& car : sim.snapshot().cars) {
            ++position;
            moved += std::abs(position - car.gridPosition);
            ++cars;
        }
    }
    INFO("false starts ", falseStarts, ", grid errors ", gridErrors, " in ", kSeeds, " starts; places changed per car ",
         moved / cars, ", first-lap collisions ", collisions);
    CHECK(falseStarts >= 1); // 1 % per car and start: about 8 expected
    CHECK(falseStarts <= 20);
    CHECK(gridErrors <= 12); // 0.5 %: about 4 expected
    CHECK(collisions >= kSeeds / 8); // the packed field touches now and then ...
    CHECK(collisions <= kSeeds * 2); // ... but not at every start
    CHECK(moved / cars > 0.3); // the order is shuffled a little ...
    CHECK(moved / cars < 3.0); // ... but the grid still counts
}

TEST_CASE("Standing start after formation laps behind the Safety Car (B5.10.8)") {
    Sim sim = makeRace(cleanConfig(2, 8));
    sim.command(command(Command::Type::SafetyCarStart));
    sim.step();
    CHECK_FALSE(sim.snapshot().gridStart); // no lights: off behind the SC
    CHECK(sim.snapshot().state == RaceState::SafetyCar);
    CHECK(sim.snapshot().totalLaps == 9); // B5.10.7
    for (long steps = 0; sim.snapshot().leaderLap < 2 && steps < 200'000; ++steps) {
        sim.step();
    }
    sim.command(command(Command::Type::RecallSafetyCar, true)); // "STANDING START"
    for (long steps = 0; !sim.snapshot().gridStart && steps < 200'000; ++steps) {
        sim.step();
        CHECK(sim.snapshot().state == RaceState::SafetyCar);
    }
    REQUIRE(sim.snapshot().gridStart);
    CHECK_FALSE(sim.snapshot().safetyCarOnTrack);
    const std::vector<int> queue = roadOrder(sim); // the queue as the Safety Car goes in
    // Until every car stands in its slot no light comes on.
    for (long steps = 0; sim.snapshot().startLights == 0 && sim.snapshot().gridStart && steps < 200'000; ++steps) {
        sim.step();
    }
    REQUIRE(sim.snapshot().startLights > 0);
    // The grid is that queue, 8 m apart (cars that were in the pit lane line up behind it).
    std::map<int, double> distance;
    for (const CarState& car : sim.snapshot().cars) {
        distance[car.entryIndex] = car.distance;
    }
    for (std::size_t k = 1; k < queue.size(); ++k) {
        CHECK(distance[queue[k - 1]] - distance[queue[k]] == doctest::Approx(8.0).epsilon(0.05));
    }
    runThroughStandingStart(sim);
    CHECK(sim.snapshot().state == RaceState::Green);
    for (long steps = 0; !sim.finished() && steps < 2'000'000; ++steps) {
        sim.step();
    }
    REQUIRE(sim.finished());
    CHECK(countIncidents(sim, Incident::Type::OvertakeUnderSafetyCar) == 0);
    CHECK(countIncidents(sim, Incident::Type::OvertakeBeforeRestartLine) == 0); // racing from the lights
    CHECK(sim.snapshot().cars.front().lapsCompleted == 9);
}

TEST_CASE("A plain recall after formation laps is still a rolling start (B5.10.9)") {
    Sim sim = makeRace(cleanConfig(2, 8));
    sim.command(command(Command::Type::SafetyCarStart));
    for (long steps = 0; sim.snapshot().leaderLap < 2 && steps < 200'000; ++steps) {
        sim.step();
    }
    sim.command(command(Command::Type::RecallSafetyCar));
    for (long steps = 0; sim.snapshot().state != RaceState::Green && steps < 200'000; ++steps) {
        sim.step();
        CHECK_FALSE(sim.snapshot().gridStart);
    }
    CHECK(sim.snapshot().state == RaceState::Green);
}

TEST_CASE("Standing start resumption after a red flag (B5.15.4)") {
    RaceConfig config = cleanConfig(5, 12);
    config.accidentChanceScale = 0.0;
    Sim sim = makeRace(config);
    for (long steps = 0; sim.snapshot().leaderLap < 4 && steps < 200'000; ++steps) {
        sim.step();
    }
    sim.command(command(Command::Type::RedFlag));
    for (int k = 0; k < 20 * 240; ++k) { // everyone back at the line
        sim.step();
    }
    REQUIRE(sim.snapshot().state == RaceState::RedFlag);
    sim.command(command(Command::Type::Restart, true));
    sim.step();
    CHECK(sim.snapshot().state == RaceState::SafetyCar);
    for (long steps = 0; !sim.snapshot().gridStart && steps < 200'000; ++steps) {
        sim.step();
    }
    REQUIRE(sim.snapshot().gridStart);
    const double signal = runThroughStandingStart(sim);
    CHECK(signal > 0.0);
    CHECK(sim.snapshot().totalLaps == 12);
    for (long steps = 0; !sim.finished() && steps < 2'000'000; ++steps) {
        sim.step();
    }
    REQUIRE(sim.finished());
    CHECK(countIncidents(sim, Incident::Type::OvertakeUnderSafetyCar) == 0);
    CHECK(sim.snapshot().cars.front().lapsCompleted == 12);
}

TEST_CASE("A red flag during a standing start calls it off") {
    Sim sim = makeRace(cleanConfig(6, 8));
    sim.command(command(Command::Type::SafetyCarStart));
    for (long steps = 0; sim.snapshot().leaderLap < 2 && steps < 200'000; ++steps) {
        sim.step();
    }
    sim.command(command(Command::Type::RecallSafetyCar, true));
    for (long steps = 0; !sim.snapshot().gridStart && steps < 200'000; ++steps) {
        sim.step();
    }
    REQUIRE(sim.snapshot().gridStart);
    sim.command(command(Command::Type::RedFlag));
    sim.step();
    CHECK_FALSE(sim.snapshot().gridStart);
    CHECK(sim.snapshot().state == RaceState::RedFlag);
}
