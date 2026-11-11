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
    config.stallChance = 0.0;
    config.formationLap = false; // these tests are about the lights and the grid; the formation lap has its own below
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

// ---- M11.3: formation lap, a car immobilised on the grid, aborted start, extra formation lap

namespace {

// A race with the formation lap, run until the cars stand on the grid again (the lights phase).
Sim onTheGridAfterFormation(RaceConfig config) {
    config.formationLap = true;
    Sim sim = makeRace(config);
    for (long steps = 0; sim.snapshot().formationLap && steps < 200'000; ++steps) {
        sim.step();
    }
    REQUIRE_FALSE(sim.snapshot().formationLap);
    REQUIRE(sim.snapshot().gridStart);
    return sim;
}

std::vector<int> stalledCars(const Sim& sim) {
    std::vector<int> stalled;
    for (const CarState& car : sim.snapshot().cars) {
        if (car.stalled) {
            stalled.push_back(car.entryIndex);
        }
    }
    return stalled;
}

// The first seed from `from` on whose start has exactly one car immobilised on the grid.
Sim gridWithOneStall(std::uint64_t from, int laps, std::uint64_t* seedOut = nullptr) {
    for (std::uint64_t seed = from; seed < from + 200; ++seed) {
        RaceConfig config = cleanConfig(seed, laps);
        config.stallChance = 0.05;
        Sim sim = onTheGridAfterFormation(config);
        if (stalledCars(sim).size() == 1) {
            if (seedOut) {
                *seedOut = seed;
            }
            return sim;
        }
    }
    FAIL("no start with exactly one immobilised car");
    return onTheGridAfterFormation(cleanConfig(from, laps));
}

const CarState& carOf(const Sim& sim, int entry) {
    for (const CarState& car : sim.snapshot().cars) {
        if (car.entryIndex == entry) {
            return car;
        }
    }
    FAIL("entry not found");
    return sim.snapshot().cars.front();
}

} // namespace

TEST_CASE("Formation lap: every car is back in its grid slot and the lap does not count (B5.6)") {
    RaceConfig config = cleanConfig(2, 6);
    config.formationLap = true;
    Sim sim = makeRace(config);
    REQUIRE(sim.snapshot().formationLap);
    std::map<int, double> slot;
    for (const CarState& car : sim.snapshot().cars) {
        slot[car.entryIndex] = car.distance;
    }
    bool moved = false;
    for (long steps = 0; sim.snapshot().formationLap && steps < 200'000; ++steps) {
        sim.step();
        CHECK(sim.snapshot().state == RaceState::SafetyCar); // its rules, without the car
        CHECK_FALSE(sim.snapshot().safetyCarOnTrack);
        for (const CarState& car : sim.snapshot().cars) {
            moved = moved || car.speed > 0.0;
            CHECK_FALSE(car.inPitLane);
        }
    }
    CHECK(moved);
    CHECK(sim.snapshot().time > 60.0); // a lap at reduced pace
    CHECK(sim.snapshot().gridStart);
    for (const CarState& car : sim.snapshot().cars) {
        CHECK(std::abs(car.distance - slot[car.entryIndex]) < 0.5); // its own slot, as at time 0 (within half a metre)
        CHECK(car.lapsCompleted == 0);
        CHECK(car.speed == 0.0);
    }
    CHECK(sim.incidents().empty());
    CHECK(sim.stats().overtakes == 0);
    runThroughStandingStart(sim);
    for (long steps = 0; !sim.finished() && steps < 2'000'000; ++steps) {
        sim.step();
    }
    REQUIRE(sim.finished());
    CHECK(sim.snapshot().cars.front().lapsCompleted == 6); // six racing laps
    CHECK(sim.snapshot().totalLaps == 6);
}

TEST_CASE("Formation lap: the same seed gives the same race, without it the start is as before") {
    RaceConfig config;
    config.laps = 4;
    config.seed = 9;
    Sim a = makeRace(config);
    Sim b = makeRace(config);
    for (long steps = 0; !a.finished() && steps < 2'000'000; ++steps) {
        a.step();
        b.step();
    }
    REQUIRE(a.finished());
    REQUIRE(b.finished());
    for (std::size_t k = 0; k < a.snapshot().cars.size(); ++k) {
        CHECK(a.snapshot().cars[k].entryIndex == b.snapshot().cars[k].entryIndex);
        CHECK(a.snapshot().cars[k].lastLineTime == b.snapshot().cars[k].lastLineTime);
    }
    config.formationLap = false;
    const Sim plain = makeRace(config);
    CHECK_FALSE(plain.snapshot().formationLap);
    CHECK(plain.snapshot().gridStart); // straight to the lights
    CHECK(plain.snapshot().state == RaceState::Green);
}

TEST_CASE("A car immobilised on the grid: the lights wait, an extra formation lap sends it to the back (B5.7, B5.9)") {
    Sim sim = gridWithOneStall(1, 8);
    const int stalled = stalledCars(sim).front();
    CHECK(sim.stats().stalls == 1);
    CHECK(sim.snapshot().totalLaps == 8);
    for (int k = 0; k < 20 * 8; ++k) { // eight seconds: race control has time to decide
        sim.step();
        CHECK(sim.snapshot().startLights == 0);
    }
    sim.command(command(Command::Type::ExtraFormationLap));
    sim.step();
    CHECK(sim.snapshot().formationLap);
    CHECK(sim.snapshot().totalLaps == 7); // one lap less
    CHECK(sim.stats().startRepeats == 1);
    CHECK(stalledCars(sim).empty());
    for (long steps = 0; sim.snapshot().formationLap && steps < 200'000; ++steps) {
        sim.step();
    }
    REQUIRE(sim.snapshot().gridStart);
    // Behind the last row now (stand-in for the pit lane start).
    for (const CarState& car : sim.snapshot().cars) {
        if (car.entryIndex != stalled) {
            CHECK(car.distance > carOf(sim, stalled).distance + 5.0);
        }
    }
    runThroughStandingStart(sim);
    CHECK(sim.stats().stallCrashes == 0);
    for (long steps = 0; !sim.finished() && steps < 2'000'000; ++steps) {
        sim.step();
    }
    REQUIRE(sim.finished());
    CHECK(sim.snapshot().cars.front().lapsCompleted == 7);
    CHECK(carOf(sim, stalled).lapsCompleted >= 6); // it raced
}

TEST_CASE("An aborted start begins again after a wait and costs a lap (B5.8)") {
    Sim sim = gridWithOneStall(1, 8);
    const int stalled = stalledCars(sim).front();
    sim.command(command(Command::Type::AbortStart));
    sim.step();
    const double called = sim.snapshot().time;
    CHECK(sim.snapshot().formationLap);
    CHECK(sim.snapshot().totalLaps == 7);
    for (int k = 0; k < 20 * 50; ++k) { // fifty seconds later nobody has moved yet
        sim.step();
    }
    for (const CarState& car : sim.snapshot().cars) {
        CHECK(car.speed == 0.0);
    }
    for (long steps = 0; sim.snapshot().formationLap && steps < 200'000; ++steps) {
        sim.step();
    }
    CHECK(sim.snapshot().time - called > 120.0); // the wait and another formation lap
    runThroughStandingStart(sim);
    CHECK(sim.snapshot().totalLaps == 7);
    CHECK_FALSE(carOf(sim, stalled).retired);
    // Once the race is on, the start cannot be called off any more.
    sim.command(command(Command::Type::AbortStart));
    sim.command(command(Command::Type::ExtraFormationLap));
    sim.step();
    CHECK_FALSE(sim.snapshot().formationLap);
    CHECK(sim.snapshot().totalLaps == 7);
    CHECK(sim.stats().startRepeats == 1);
}

TEST_CASE("An immobilised car left on the grid is in the way: sometimes it is hit (B5.7)") {
    int stalls = 0;
    int crashes = 0;
    int stoodStill = 0;
    for (std::uint64_t seed = 1; seed <= 80; ++seed) {
        RaceConfig config = cleanConfig(seed, 3);
        config.stallChance = 0.05;
        Sim sim = onTheGridAfterFormation(config);
        const std::vector<int> stalled = stalledCars(sim);
        if (stalled.empty()) {
            continue;
        }
        runThroughStandingStart(sim); // nobody calls the start off
        for (int k = 0; k < 20 * 5; ++k) {
            sim.step();
        }
        for (int entry : stalled) {
            const CarState& car = carOf(sim, entry);
            stoodStill += car.speed == 0.0 ? 1 : 0; // five seconds after the signal it still stands
        }
        stalls += sim.stats().stalls;
        crashes += sim.stats().stallCrashes;
    }
    INFO("immobilised cars ", stalls, ", hit ", crashes, ", still standing after 5 s ", stoodStill);
    REQUIRE(stalls >= 20);
    CHECK(stoodStill == stalls);
    CHECK(crashes > 0);          // a real risk ...
    CHECK(crashes * 2 < stalls); // ... but not the rule
}
