// Incidents and verdicts in a qualifying session (M13.5): impeding, track limits and single yellow on a timed
// lap; deleting the lap and grid drops; the starting grid after all verdicts (B2.5.4 b).

#include "uisim/DataDir.h"
#include "uisim/DataLoader.h"
#include "uisim/Hearing.h"
#include "uisim/Rating.h"
#include "uisim/Sim.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <iterator>
#include <set>

using namespace uisim;

namespace {

std::filesystem::path dataDir() { return std::filesystem::path(UISIM_DATA_DIR); }

RaceConfig sessionConfig(std::uint64_t seed) {
    RaceConfig config;
    config.seed = seed;
    config.qualifying = true;
    config.accidentChanceScale = 0.0; // no cars lost: every car has a time
    config.followUpChanceScale = 0.0;
    return config;
}

Sim makeSession(const RaceConfig& config) {
    return Sim(loadTrack(dataDir() / "tracks" / "testring.json"), loadEntries(dataDir() / "entries" / "fictional-grid.json"),
               config);
}

constexpr long kMaxSteps = 3L * 3600 * 20;

void runToEnd(Sim& sim) {
    for (long steps = 0; !sim.finished() && steps < kMaxSteps; ++steps) {
        sim.step();
    }
    REQUIRE(sim.finished());
}

// A finished session with plenty of incidents to hang verdicts on.
Sim finishedSession(std::uint64_t seed) {
    RaceConfig config = sessionConfig(seed);
    config.impedingChance = 0.5;
    Sim sim = makeSession(config);
    runToEnd(sim);
    REQUIRE(sim.incidents().size() >= 6);
    return sim;
}

void verdict(Sim& sim, int incidentId, int car, Penalty penalty, int places = 0) {
    Command c;
    c.type = Command::Type::IssueVerdict;
    c.incidentId = incidentId;
    c.car = car;
    c.penalty = penalty;
    c.places = places;
    sim.command(c);
    sim.step();
}

const CarState& stateOf(const Sim& sim, int car) {
    return *std::find_if(sim.snapshot().cars.begin(), sim.snapshot().cars.end(),
                         [&](const CarState& cs) { return cs.entryIndex == car; });
}

// Every other car keeps its order relative to the rest.
void checkOthersInOrder(const std::vector<int>& before, const std::vector<int>& after, const std::set<int>& moved) {
    std::vector<int> a;
    std::vector<int> b;
    std::copy_if(before.begin(), before.end(), std::back_inserter(a), [&](int i) { return moved.count(i) == 0; });
    std::copy_if(after.begin(), after.end(), std::back_inserter(b), [&](int i) { return moved.count(i) == 0; });
    CHECK(a == b);
}

int slotOf(const std::vector<int>& grid, int car) {
    return static_cast<int>(std::find(grid.begin(), grid.end(), car) - grid.begin());
}

} // namespace

TEST_CASE("Every qualifying incident has an expected verdict and evidence for a hearing") {
    std::set<Incident::Type> seen;
    std::set<Penalty> expectedPenalties;
    int yellowLifted = 0;
    int yellowNotLifted = 0;
    for (std::uint64_t seed = 1; seed <= 4; ++seed) {
        RaceConfig config = sessionConfig(seed);
        config.impedingChance = 0.2;
        config.lapTrackLimitsScale = 3.0;
        config.yellowIgnoreChance = 0.5;
        Sim sim = makeSession(config);
        // Single yellow in one marshal sector for the whole of Q1: every timed lap there passes it.
        Command yellow;
        yellow.type = Command::Type::SetSectorFlag;
        yellow.sector = 3;
        yellow.flag = Flag::Yellow;
        sim.command(yellow);
        bool cleared = false;
        for (long steps = 0; !sim.finished() && steps < kMaxSteps; ++steps) {
            if (!cleared && sim.snapshot().qualifyingSegment == 2) {
                Command green = yellow;
                green.flag = Flag::Green;
                sim.command(green);
                cleared = true;
            }
            sim.step();
        }
        REQUIRE(sim.finished());
        for (const Incident& incident : sim.incidents()) {
            seen.insert(incident.type);
            const ExpectedVerdict expected = expectedVerdict(incident);
            expectedPenalties.insert(expected.penalty);
            CHECK((expected.car == -1 || expected.car == incident.car));
            const Hearing hearing = makeHearing(sim, incident);
            CHECK_FALSE(hearing.evidence.empty());
            REQUIRE(hearing.statements.size() == 1); // only the car under investigation is heard
            CHECK(hearing.statements[0].car == incident.car);
            switch (incident.type) {
            case Incident::Type::Impeding:
                CHECK(incident.otherCar != incident.car);
                CHECK(incident.measure >= 0.3);
                CHECK((expected.penalty == Penalty::GridDrop || expected.penalty == Penalty::Warning ||
                       expected.penalty == Penalty::None));
                if (expected.penalty == Penalty::GridDrop) {
                    CHECK(expected.places == 3);
                }
                break;
            case Incident::Type::LapTrackLimits:
                REQUIRE(incident.lap >= 0);
                CHECK(sim.qualifyingLaps()[static_cast<std::size_t>(incident.lap)].car == incident.car);
                CHECK(sim.qualifyingLaps()[static_cast<std::size_t>(incident.lap)].trackLimits);
                CHECK(expected.penalty == Penalty::DeleteLap);
                break;
            case Incident::Type::YellowFlagLap:
                REQUIRE(incident.lap >= 0);
                CHECK(sim.qualifyingLaps()[static_cast<std::size_t>(incident.lap)].throughYellow);
                CHECK(sim.qualifyingLaps()[static_cast<std::size_t>(incident.lap)].segment == 0);
                (expected.penalty == Penalty::DeleteLap ? yellowNotLifted : yellowLifted) += 1;
                break;
            default:
                FAIL("an incident of the race in a session: type " << static_cast<int>(incident.type));
            }
        }
    }
    CHECK(seen.count(Incident::Type::Impeding) == 1);
    CHECK(seen.count(Incident::Type::LapTrackLimits) == 1);
    CHECK(seen.count(Incident::Type::YellowFlagLap) == 1);
    CHECK(expectedPenalties.count(Penalty::GridDrop) == 1);
    CHECK(expectedPenalties.count(Penalty::DeleteLap) == 1);
    CHECK(yellowLifted > 0);    // honest drivers through yellow: no action
    CHECK(yellowNotLifted > 0); // and the ones who kept their pace
    MESSAGE("yellow laps: lifted " << yellowLifted << ", not lifted " << yellowNotLifted);
}

TEST_CASE("A deleted lap does not count for the result of the segment") {
    // A track-limits lap in Q3 deleted after the session: the car's Q3 time and its place follow from the other laps.
    for (std::uint64_t seed = 1; seed <= 20; ++seed) {
        RaceConfig config = sessionConfig(seed);
        config.lapTrackLimitsScale = 5.0;
        Sim sim = makeSession(config);
        runToEnd(sim);
        const auto incident = std::find_if(sim.incidents().begin(), sim.incidents().end(), [&](const Incident& i) {
            const QualifyingLap& lap = sim.qualifyingLaps()[static_cast<std::size_t>(i.lap)];
            return i.type == Incident::Type::LapTrackLimits && lap.segment == 2 && stateOf(sim, i.car).qualifyingTime == lap.time;
        });
        if (incident == sim.incidents().end()) {
            continue;
        }
        const int car = incident->car;
        const QualifyingLap deletedLap = sim.qualifyingLaps()[static_cast<std::size_t>(incident->lap)];
        REQUIRE(deletedLap.valid); // and the car's best in Q3
        verdict(sim, incident->id, car, Penalty::DeleteLap);
        CHECK_FALSE(sim.qualifyingLaps()[static_cast<std::size_t>(incident->lap)].valid);
        double best = 0.0;
        for (const QualifyingLap& lap : sim.qualifyingLaps()) {
            if (lap.car == car && lap.segment == 2 && lap.valid && (best == 0.0 || lap.time < best)) {
                best = lap.time;
            }
        }
        CHECK(best != deletedLap.time);
        CHECK(stateOf(sim, car).qualifyingTime == best);
        // The Q3 cars are ordered by their remaining Q3 times; a car without one comes behind those with one.
        for (std::size_t k = 1; k < 10; ++k) {
            const double ahead = sim.snapshot().cars[k - 1].qualifyingTime;
            const double here = sim.snapshot().cars[k].qualifyingTime;
            CHECK((here == 0.0 || (ahead > 0.0 && ahead <= here)));
        }
        CHECK(sim.qualifyingOrder() == sim.startingGrid()); // no grid drops
        return;
    }
    FAIL("no track-limits lap in Q3 in 20 seeds");
}

TEST_CASE("A lap deleted during its segment counts no longer; an elimination already made stands") {
    for (std::uint64_t seed = 1; seed <= 20; ++seed) {
        RaceConfig config = sessionConfig(seed);
        config.lapTrackLimitsScale = 5.0;
        Sim sim = makeSession(config);
        // During Q1: the first track-limits incident.
        int during = 0;
        for (long steps = 0; !sim.finished() && steps < kMaxSteps && during == 0; ++steps) {
            sim.step();
            for (const Incident& i : sim.incidents()) {
                if (i.type == Incident::Type::LapTrackLimits) {
                    during = i.id;
                }
            }
        }
        if (during == 0 || sim.snapshot().qualifyingSegment != 1) {
            continue;
        }
        const Incident first = sim.incidents()[static_cast<std::size_t>(during - 1)];
        verdict(sim, during, first.car, Penalty::DeleteLap);
        CHECK(stateOf(sim, first.car).qualifyingTime != sim.qualifyingLaps()[static_cast<std::size_t>(first.lap)].time);
        // In Q2: a Q1 track-limits lap of a car that went through. Deleting it now does not take the car out.
        while (!sim.finished() && sim.snapshot().qualifyingSegment < 2) {
            sim.step();
        }
        const auto late = std::find_if(sim.incidents().begin(), sim.incidents().end(), [&](const Incident& i) {
            return i.type == Incident::Type::LapTrackLimits && i.id != during &&
                   sim.qualifyingLaps()[static_cast<std::size_t>(i.lap)].segment == 0 && stateOf(sim, i.car).eliminatedIn == 0;
        });
        if (late == sim.incidents().end()) {
            continue;
        }
        const int car = late->car;
        verdict(sim, late->id, car, Penalty::DeleteLap);
        CHECK_FALSE(sim.qualifyingLaps()[static_cast<std::size_t>(late->lap)].valid);
        CHECK(stateOf(sim, car).eliminatedIn == 0);
        runToEnd(sim);
        CHECK(stateOf(sim, car).eliminatedIn != 1);
        return;
    }
    FAIL("no seed with track-limits laps in Q1 judged during and after it");
}

TEST_CASE("A grid drop moves exactly this car (B2.5.4 b)") {
    SUBCASE("Three places") {
        Sim sim = finishedSession(1);
        const std::vector<int> order = sim.qualifyingOrder();
        const int car = order[4]; // fifth
        verdict(sim, 1, car, Penalty::GridDrop, 3);
        const std::vector<int> grid = sim.startingGrid();
        CHECK(slotOf(grid, car) == 7);
        checkOthersInOrder(order, grid, {car});
        CHECK(sim.qualifyingOrder() == order); // the classification itself is unchanged
    }
    SUBCASE("Past the end of the field: last") {
        Sim sim = finishedSession(2);
        const std::vector<int> order = sim.qualifyingOrder();
        const int car = order[18];
        verdict(sim, 1, car, Penalty::GridDrop, 5);
        const std::vector<int> grid = sim.startingGrid();
        CHECK(slotOf(grid, car) == 19);
        checkOthersInOrder(order, grid, {car});
    }
    SUBCASE("Two drops on one car are added up") {
        Sim sim = finishedSession(3);
        const std::vector<int> order = sim.qualifyingOrder();
        const int car = order[2];
        verdict(sim, 1, car, Penalty::GridDrop, 2);
        verdict(sim, 2, car, Penalty::GridDrop, 3);
        CHECK(slotOf(sim.startingGrid(), car) == 7);
    }
    SUBCASE("Two on one slot: the slower qualifier keeps it, the other goes directly ahead") {
        Sim sim = finishedSession(4);
        const std::vector<int> order = sim.qualifyingOrder();
        const int faster = order[1]; // second, three places: slot 5
        const int slower = order[3]; // fourth, one place: slot 5
        verdict(sim, 1, faster, Penalty::GridDrop, 3);
        verdict(sim, 2, slower, Penalty::GridDrop, 1);
        const std::vector<int> grid = sim.startingGrid();
        CHECK(slotOf(grid, slower) == 4);
        CHECK(slotOf(grid, faster) == 3);
        checkOthersInOrder(order, grid, {faster, slower});
    }
    SUBCASE("More than 15 places: behind all others") {
        Sim sim = finishedSession(5);
        const std::vector<int> order = sim.qualifyingOrder();
        verdict(sim, 1, order[0], Penalty::GridDrop, 10);
        verdict(sim, 2, order[0], Penalty::GridDrop, 10);
        verdict(sim, 3, order[6], Penalty::GridDrop, 16);
        const std::vector<int> grid = sim.startingGrid();
        CHECK(grid[18] == order[0]); // the faster qualifier first among them
        CHECK(grid[19] == order[6]);
        checkOthersInOrder(order, grid, {order[0], order[6]});
    }
}

TEST_CASE("Verdicts after the qualifying still change the grid, after the race start no longer") {
    Sim sim = finishedSession(6);
    const std::vector<int> order = sim.qualifyingOrder();
    verdict(sim, 1, order[0], Penalty::GridDrop, 3);
    REQUIRE(sim.verdicts().size() == 1);
    CHECK(sim.verdicts()[0].afterRace);
    const std::vector<int> grid = sim.startingGrid();
    CHECK(slotOf(grid, order[0]) == 3);

    // The race starts from that grid.
    RaceConfig raceConfig;
    raceConfig.seed = 6;
    raceConfig.laps = 3;
    raceConfig.grid = grid;
    sim.lockGrid();
    const Sim race(sim.track(), sim.entries(), raceConfig);
    for (const CarState& cs : race.snapshot().cars) {
        CHECK(cs.gridPosition == slotOf(grid, cs.entryIndex) + 1);
    }

    // From now on the session takes no verdicts: neither the grid nor the decisions change.
    verdict(sim, 2, order[1], Penalty::GridDrop, 5);
    verdict(sim, 3, order[2], Penalty::DeleteLap);
    CHECK(sim.verdicts().size() == 1);
    CHECK(sim.startingGrid() == grid);
}

TEST_CASE("A session takes only its own verdicts, a race only its own") {
    Sim sim = finishedSession(7);
    verdict(sim, 1, 0, Penalty::FiveSeconds);
    verdict(sim, 2, 0, Penalty::DriveThrough);
    verdict(sim, 3, 0, Penalty::GridDrop, 0); // a grid drop needs places
    CHECK(sim.verdicts().empty());
    verdict(sim, 4, 0, Penalty::Warning);
    verdict(sim, 5, -1, Penalty::None);
    CHECK(sim.verdicts().size() == 2);

    RaceConfig raceConfig;
    raceConfig.laps = 3;
    raceConfig.accidentChanceScale = 0.0;
    Sim race(sim.track(), sim.entries(), raceConfig);
    Command accident;
    accident.type = Command::Type::InjectAccident;
    accident.car = 3;
    accident.accident = AccidentType::Spin;
    bool sent = false;
    for (int k = 0; k < 20 * 600 && race.incidents().empty(); ++k) {
        if (!sent && race.snapshot().raceStarted && race.snapshot().cars[0].lapsCompleted >= 1) {
            race.command(accident);
            sent = true;
        }
        race.step();
    }
    REQUIRE_FALSE(race.incidents().empty());
    verdict(race, 1, 3, Penalty::DeleteLap);
    verdict(race, 1, 3, Penalty::GridDrop, 3);
    CHECK(race.verdicts().empty());
}

TEST_CASE("Qualifying verdicts are rated like race verdicts; decisions after the session cost nothing") {
    Sim sim = finishedSession(8);
    for (const Incident& incident : sim.incidents()) {
        const ExpectedVerdict expected = expectedVerdict(incident);
        verdict(sim, incident.id, expected.car, expected.penalty, expected.places);
    }
    RaceRating rating = rateRace(sim);
    CHECK(rating.decided == static_cast<int>(sim.incidents().size()));
    CHECK(rating.fairness == doctest::Approx(1.0));
    CHECK(rating.overturned == 0);
    CHECK(rating.raceQuality == doctest::Approx(1.0)); // every verdict came after the session

    // An impeding grid drop with three places too many is a step off; so is a reprimand instead.
    Sim other = finishedSession(8);
    const auto drop = std::find_if(other.incidents().begin(), other.incidents().end(), [](const Incident& i) {
        return expectedVerdict(i).penalty == Penalty::GridDrop;
    });
    REQUIRE(drop != other.incidents().end());
    verdict(other, drop->id, drop->car, Penalty::GridDrop, 6);
    rating = rateRace(other);
    CHECK(rating.incidents[static_cast<std::size_t>(drop->id - 1)].score == doctest::Approx(0.66));
    CHECK(rating.incidents[static_cast<std::size_t>(drop->id - 1)].givenPlaces == 6);
}
