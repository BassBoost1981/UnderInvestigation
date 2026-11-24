// Qualifying session (M13.3): three segments with elimination; its result is the grid of the race.

#include "uisim/DataDir.h"
#include "uisim/DataLoader.h"
#include "uisim/Sim.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <numeric>

using namespace uisim;

namespace {

std::filesystem::path dataDir() { return std::filesystem::path(UISIM_DATA_DIR); }

// Without random accidents unless asked for (M13.4): the 13.3 tests check a session in which every car can run.
Sim makeQualifying(std::uint64_t seed, const std::filesystem::path& track = dataDir() / "tracks" / "testring.json",
                   double accidents = 0.0) {
    RaceConfig config;
    config.seed = seed;
    config.qualifying = true;
    config.accidentChanceScale = accidents;
    config.followUpChanceScale = accidents;
    return Sim(loadTrack(track), loadEntries(dataDir() / "entries" / "fictional-grid.json"), config);
}

// Steps until the session is over; `each` sees the snapshot after every step.
template <typename Each> void runSession(Sim& sim, Each each) {
    const long maxSteps = 3L * 3600 * 20; // three hours: far beyond the hour a session takes
    for (long steps = 0; !sim.finished() && steps < maxSteps; ++steps) {
        sim.step();
        each(sim.snapshot());
    }
    REQUIRE(sim.finished());
}
void runSession(Sim& sim) {
    runSession(sim, [](const SimSnapshot&) {});
}

// Best timed lap of an entry in a segment (0-based), 0 = none.
double bestTime(const Sim& sim, int entry, int segment) {
    double best = 0.0;
    for (const QualifyingLap& lap : sim.qualifyingLaps()) {
        if (lap.car == entry && lap.segment == segment && lap.valid && (best == 0.0 || lap.time < best)) {
            best = lap.time;
        }
    }
    return best;
}

} // namespace

TEST_CASE("Qualifying starts with every car in its garage") {
    const Sim sim = makeQualifying(1);
    const SimSnapshot& snap = sim.snapshot();
    CHECK(snap.qualifying);
    CHECK(snap.qualifyingSegment == 1);
    CHECK(snap.segmentTimeLeft == doctest::Approx(18.0 * 60.0));
    CHECK_FALSE(snap.finished);
    REQUIRE(snap.cars.size() == 20);
    for (const CarState& car : snap.cars) {
        CHECK(car.inPitLane);
        CHECK(car.speed == 0.0);
        CHECK(car.qualifyingPhase == QualifyingPhase::Garage);
        CHECK(car.qualifyingTime == 0.0);
        CHECK(car.eliminatedIn == 0);
    }
}

TEST_CASE("Same seed produces an identical qualifying") {
    Sim a = makeQualifying(7);
    Sim b = makeQualifying(7);
    runSession(a);
    runSession(b);
    CHECK(a.snapshot().time == b.snapshot().time);
    CHECK(a.qualifyingOrder() == b.qualifyingOrder());
    REQUIRE(a.qualifyingLaps().size() == b.qualifyingLaps().size());
    for (std::size_t k = 0; k < a.qualifyingLaps().size(); ++k) {
        CHECK(a.qualifyingLaps()[k].car == b.qualifyingLaps()[k].car);
        CHECK(a.qualifyingLaps()[k].time == b.qualifyingLaps()[k].time);
        CHECK(a.qualifyingLaps()[k].setAt == b.qualifyingLaps()[k].setAt);
    }
    Sim other = makeQualifying(8);
    runSession(other);
    CHECK(other.qualifyingLaps().front().time != a.qualifyingLaps().front().time);
}

TEST_CASE("Three segments: five cars go out after each of the first two, every car sets a time") {
    for (std::uint64_t seed = 1; seed <= 5; ++seed) {
        CAPTURE(seed);
        Sim sim = makeQualifying(seed);
        runSession(sim);
        const SimSnapshot& snap = sim.snapshot();
        CHECK(snap.qualifyingSegment == 3);
        CHECK(snap.segmentTimeLeft == 0.0);
        REQUIRE(snap.cars.size() == 20);
        CHECK(sim.qualifyingOrder().size() == 20);
        for (int r = 0; r < 20; ++r) {
            const CarState& car = snap.cars[static_cast<std::size_t>(r)];
            CAPTURE(r);
            CHECK(car.position == r + 1);
            CHECK(sim.qualifyingOrder()[static_cast<std::size_t>(r)] == car.entryIndex);
            // B2.4.2 / B2.4.3: places 1 to 10 from Q3, 11 to 15 went out in Q2, 16 to 20 in Q1.
            const int segment = r < 10 ? 2 : r < 15 ? 1 : 0;
            CHECK(car.eliminatedIn == (r < 10 ? 0 : segment + 1));
            CHECK(bestTime(sim, car.entryIndex, 0) > 0.0); // in the dry every car sets a time
            CHECK(car.qualifyingTime > 0.0);
            CHECK(car.qualifyingTime == bestTime(sim, car.entryIndex, segment));
            for (int later = segment + 1; later < 3; ++later) {
                CHECK(bestTime(sim, car.entryIndex, later) == 0.0); // out: no more laps
            }
            if (r > 0 && r != 10 && r != 15) {
                CHECK(car.qualifyingTime >= snap.cars[static_cast<std::size_t>(r - 1)].qualifyingTime);
            }
            CHECK(car.qualifyingPhase == QualifyingPhase::Garage);
            CHECK(car.inPitLane);
        }
    }
}

TEST_CASE("Eliminated cars stay in their garages") {
    Sim sim = makeQualifying(3);
    std::vector<double> parkedAt(20, 0.0);
    std::vector<bool> out(20, false);
    int checked = 0;
    runSession(sim, [&](const SimSnapshot& snap) {
        for (const CarState& car : snap.cars) {
            const std::size_t entry = static_cast<std::size_t>(car.entryIndex);
            if (car.eliminatedIn == 0) {
                continue;
            }
            if (!out[entry]) {
                out[entry] = true;
                parkedAt[entry] = car.distance;
            }
            ++checked;
            CHECK(car.distance == parkedAt[entry]);
            CHECK(car.speed == 0.0);
            CHECK(car.qualifyingPhase == QualifyingPhase::Garage);
        }
    });
    CHECK(std::count(out.begin(), out.end(), true) == 10);
    CHECK(checked > 0);
}

TEST_CASE("A lap begun before the time is up counts, none begins after it") {
    int completedAfterTheTime = 0;
    for (std::uint64_t seed = 1; seed <= 3; ++seed) {
        CAPTURE(seed);
        Sim sim = makeQualifying(seed);
        std::vector<double> timeUp(3, 0.0); // session time at which the clock of each segment reached zero
        runSession(sim, [&](const SimSnapshot& snap) {
            double& at = timeUp[static_cast<std::size_t>(snap.qualifyingSegment - 1)];
            if (snap.segmentTimeLeft == 0.0 && at == 0.0) {
                at = snap.time;
            }
        });
        for (const QualifyingLap& lap : sim.qualifyingLaps()) {
            const double end = timeUp[static_cast<std::size_t>(lap.segment)];
            CHECK(lap.setAt - lap.time < end); // B4.1.2 c: begun in time
            completedAfterTheTime += lap.setAt > end ? 1 : 0;
        }
    }
    CHECK(completedAfterTheTime > 0);
}

TEST_CASE("The qualifying order follows the pace, but is not the pace order") {
    const auto entries = loadEntries(dataDir() / "entries" / "fictional-grid.json");
    const int n = static_cast<int>(entries.size());
    std::vector<int> byPace(static_cast<std::size_t>(n));
    std::iota(byPace.begin(), byPace.end(), 0);
    std::stable_sort(byPace.begin(), byPace.end(), [&](int a, int b) {
        return entries[a].teamPace * entries[a].skill > entries[b].teamPace * entries[b].skill;
    });
    std::vector<int> paceRank(static_cast<std::size_t>(n));
    for (int r = 0; r < n; ++r) {
        paceRank[static_cast<std::size_t>(byPace[static_cast<std::size_t>(r)])] = r;
    }
    std::vector<int> poleSitters;
    std::vector<int> firstOrder;
    bool ordersDiffer = false;
    double correlationSum = 0.0;
    const int seeds = 10;
    for (std::uint64_t seed = 1; seed <= static_cast<std::uint64_t>(seeds); ++seed) {
        CAPTURE(seed);
        Sim sim = makeQualifying(seed);
        runSession(sim);
        const std::vector<int> order = sim.qualifyingOrder();
        double squares = 0.0; // Spearman rank correlation
        for (int r = 0; r < n; ++r) {
            const double d = r - paceRank[static_cast<std::size_t>(order[static_cast<std::size_t>(r)])];
            squares += d * d;
        }
        const double correlation = 1.0 - 6.0 * squares / (n * (n * n - 1.0));
        CHECK(correlation > 0.8);
        CHECK(order != byPace);
        correlationSum += correlation;
        if (std::find(poleSitters.begin(), poleSitters.end(), order.front()) == poleSitters.end()) {
            poleSitters.push_back(order.front());
        }
        if (firstOrder.empty()) {
            firstOrder = order;
        }
        ordersDiffer = ordersDiffer || order != firstOrder;
    }
    MESSAGE("mean rank correlation " << correlationSum / seeds << ", " << poleSitters.size() << " different pole sitters");
    CHECK(correlationSum / seeds > 0.9);
    CHECK(ordersDiffer);
    CHECK(poleSitters.size() >= 2); // the two fastest are level on pace
}

TEST_CASE("Flying laps are free: slow cars move over, nobody races, nothing from the race leaks in") {
    for (std::uint64_t seed = 1; seed <= 5; ++seed) {
        CAPTURE(seed);
        Sim sim = makeQualifying(seed);
        const double lapLength = sim.track().length();
        long flyingTicks = 0;
        long blockedTicks = 0; // on a flying lap, right behind another car on a flying lap
        runSession(sim, [&](const SimSnapshot& snap) {
            for (const CarState& car : snap.cars) {
                if (car.qualifyingPhase != QualifyingPhase::FlyingLap) {
                    continue;
                }
                ++flyingTicks;
                for (const CarState& other : snap.cars) {
                    const double ahead = std::fmod(other.trackPosition - car.trackPosition + lapLength, lapLength);
                    if (&other != &car && other.qualifyingPhase == QualifyingPhase::FlyingLap && !other.inPitLane && ahead < 12.0) {
                        ++blockedTicks;
                        break;
                    }
                }
            }
        });
        // Two runs per car and segment: 20 + 15 + 10 cars.
        const std::size_t laps = sim.qualifyingLaps().size();
        MESSAGE("seed " << seed << ": " << laps << " timed laps, blocked " << blockedTicks << " of " << flyingTicks << " flying ticks");
        CHECK(laps >= 2 * 45);
        CHECK(blockedTicks < flyingTicks / 100);
        CHECK(sim.stats().overtakeAttempts == 0);
        CHECK(sim.stats().collisions == 0);
        CHECK(sim.stats().accidents == 0);
        CHECK(sim.stats().spins == 0);
        CHECK(sim.stats().blueFlagsIgnored == 0);
        CHECK(sim.stats().flagViolations == 0);
        CHECK(sim.stats().trackLimitOffences == 0);
        CHECK(sim.incidents().empty()); // no accidents here; incidents in qualifying come with 13.5
        for (const CarState& car : sim.snapshot().cars) {
            CHECK_FALSE(car.overtakeActive);
            CHECK_FALSE(car.blueFlag);
            CHECK(car.pitStops == 0);
        }
    }
}

TEST_CASE("The race starts in the order of the qualifying") {
    Sim qualifying = makeQualifying(1);
    runSession(qualifying);
    RaceConfig config{50, 1};
    config.grid = qualifying.qualifyingOrder();
    const Sim race(loadTrack(dataDir() / "tracks" / "testring.json"), loadEntries(dataDir() / "entries" / "fictional-grid.json"), config);
    for (const CarState& car : race.snapshot().cars) {
        CHECK(config.grid[static_cast<std::size_t>(car.gridPosition - 1)] == car.entryIndex);
    }
}

TEST_CASE("Qualifying runs on every track") {
    std::vector<std::filesystem::path> tracks;
    for (const std::filesystem::path& dir : {dataDir() / "tracks", dataDir() / "tracks" / "private"}) {
        if (!std::filesystem::exists(dir)) {
            continue; // the private tracks are not part of the repository
        }
        for (const auto& file : std::filesystem::directory_iterator(dir)) {
            if (file.path().extension() == ".json") {
                tracks.push_back(file.path());
            }
        }
    }
    std::sort(tracks.begin(), tracks.end());
    for (const std::filesystem::path& track : tracks) {
        CAPTURE(track.filename().string());
        Sim sim = makeQualifying(1, track);
        runSession(sim);
        for (const CarState& car : sim.snapshot().cars) {
            CHECK(car.qualifyingTime > 0.0);
        }
        CHECK(sim.qualifyingLaps().size() >= 2 * 45 - 5); // nearly every car gets both runs in
    }
}

// ---- Flags in the session (M13.4) ----

namespace {

void runSeconds(Sim& sim, double seconds) {
    const double until = sim.snapshot().time + seconds;
    while (sim.snapshot().time < until && !sim.finished()) {
        sim.step();
    }
}

void send(Sim& sim, Command::Type type) {
    Command c;
    c.type = type;
    sim.command(c);
    sim.step();
}

// A car on a flying lap out on the track, -1 = none.
int flyingCar(const Sim& sim) {
    for (const CarState& car : sim.snapshot().cars) {
        if (car.qualifyingPhase == QualifyingPhase::FlyingLap && !car.inPitLane && !car.spinning) {
            return car.entryIndex;
        }
    }
    return -1;
}

// Runs until a car is on a flying lap and crashes it there; returns the car.
int crashFlyingCar(Sim& sim, AccidentType type = AccidentType::Crash) {
    int car = -1;
    for (int k = 0; k < 20 * 600 && car < 0; ++k) {
        sim.step();
        car = flyingCar(sim);
    }
    REQUIRE(car >= 0);
    Command c;
    c.type = Command::Type::InjectAccident;
    c.car = car;
    c.accident = type;
    sim.command(c);
    sim.step();
    return car;
}

const CarState& stateOf(const Sim& sim, int entry) {
    for (const CarState& car : sim.snapshot().cars) {
        if (car.entryIndex == entry) {
            return car;
        }
    }
    FAIL("no such entry");
    return sim.snapshot().cars.front();
}

} // namespace

TEST_CASE("Under the red flag the clock stops, everyone returns, nobody begins a flying lap") {
    Sim sim = makeQualifying(2);
    runSeconds(sim, 240.0);
    const int victim = crashFlyingCar(sim);
    send(sim, Command::Type::RedFlag);
    const double clock = sim.snapshot().segmentTimeLeft;
    const double redAt = sim.snapshot().time;
    CHECK(clock > 0.0);
    CHECK(sim.snapshot().state == RaceState::RedFlag);
    const std::size_t lapsBefore = sim.qualifyingLaps().size();
    for (int k = 0; k < 20 * 400; ++k) { // 400 s: every car is back
        sim.step();
        CHECK(sim.snapshot().segmentTimeLeft == clock);
        for (const CarState& car : sim.snapshot().cars) {
            CHECK(car.qualifyingPhase != QualifyingPhase::FlyingLap);
        }
    }
    CHECK(sim.qualifyingLaps().size() == lapsBefore); // the laps under way did not count
    for (const CarState& car : sim.snapshot().cars) {
        if (car.retired) {
            continue;
        }
        CHECK(car.inPitLane);
        CHECK(car.speed == 0.0); // in the garage or at the closed pit exit
    }
    CHECK(stateOf(sim, victim).retired);

    // The restart: the segment goes on with the time that was left, and the cars go out again.
    send(sim, Command::Type::Restart);
    CHECK(sim.snapshot().state == RaceState::Green);
    CHECK(sim.snapshot().segmentTimeLeft == doctest::Approx(clock - Sim::kDt));
    runSeconds(sim, clock + 300.0);
    CHECK(sim.snapshot().qualifyingSegment == 2);
    bool lapAfterRestart = false;
    for (const QualifyingLap& lap : sim.qualifyingLaps()) {
        if (lap.segment == 0 && lap.setAt > redAt) {
            lapAfterRestart = true;
            CHECK(lap.setAt - lap.time > redAt + 400.0); // begun after the restart
        }
        const bool beforeTheCrash = lap.car != victim || lap.setAt < redAt;
        CHECK(beforeTheCrash); // B4.3.2: the crashed car takes no further part
    }
    CHECK(lapAfterRestart);
    runSession(sim);
    CHECK(stateOf(sim, victim).eliminatedIn == 1); // no time: out after Q1
}

TEST_CASE("A run cut short by the red flag is given back") {
    // Red flag early in Q1, while most cars are on their first run: after the restart everyone still gets
    // two runs' worth of timed laps into Q1.
    Sim sim = makeQualifying(4);
    runSeconds(sim, 200.0);
    send(sim, Command::Type::RedFlag);
    runSeconds(sim, 300.0);
    send(sim, Command::Type::Restart);
    runSession(sim);
    std::vector<int> runsInQ1(20, 0);
    for (const QualifyingLap& lap : sim.qualifyingLaps()) {
        if (lap.segment == 0) {
            ++runsInQ1[static_cast<std::size_t>(lap.car)];
        }
    }
    for (int entry = 0; entry < 20; ++entry) {
        CAPTURE(entry);
        CHECK(runsInQ1[static_cast<std::size_t>(entry)] >= 2);
    }
}

TEST_CASE("The break waits for the restart when the red flag is out") {
    Sim sim = makeQualifying(1);
    while (sim.snapshot().qualifyingSegment == 1 && sim.snapshot().segmentTimeLeft > 0.0) {
        sim.step();
    }
    runSeconds(sim, 200.0); // the laps under way are completed, the break begins
    send(sim, Command::Type::RedFlag);
    runSeconds(sim, 300.0);
    CHECK(sim.snapshot().qualifyingSegment == 1);
    send(sim, Command::Type::Restart);
    runSeconds(sim, 120.0);
    CHECK(sim.snapshot().qualifyingSegment == 2);
    runSession(sim);
}

TEST_CASE("A lap through double yellow is deleted, through single yellow it counts and is marked") {
    for (const Flag flag : {Flag::DoubleYellow, Flag::Yellow}) {
        CAPTURE(static_cast<int>(flag));
        Sim sim = makeQualifying(3);
        runSeconds(sim, 300.0);
        const double from = sim.snapshot().time;
        for (int sector = 0; sector < sim.track().marshalSectorCount(); sector += 5) {
            Command c;
            c.type = Command::Type::SetSectorFlag;
            c.sector = sector;
            c.flag = flag;
            sim.command(c);
        }
        runSeconds(sim, 240.0);
        const double to = sim.snapshot().time;
        for (int sector = 0; sector < sim.track().marshalSectorCount(); sector += 5) {
            Command c;
            c.type = Command::Type::SetSectorFlag;
            c.sector = sector;
            c.flag = Flag::Green;
            sim.command(c);
        }
        runSeconds(sim, 400.0);
        int flagged = 0;
        int clean = 0;
        for (const QualifyingLap& lap : sim.qualifyingLaps()) {
            const bool throughFlag = lap.setAt - lap.time > from && lap.setAt < to; // the whole lap under the flags
            const bool afterFlag = lap.setAt - lap.time > to + 1.0;
            if (throughFlag) {
                ++flagged;
                CHECK(lap.valid == (flag == Flag::Yellow));
                CHECK(lap.throughYellow == (flag == Flag::Yellow));
            } else if (afterFlag || lap.setAt < from) {
                ++clean;
                CHECK(lap.valid);
                CHECK_FALSE(lap.throughYellow);
            }
        }
        CHECK(flagged > 0);
        CHECK(clean > 0);
        runSession(sim);
    }
}

TEST_CASE("An accident site without a flag raises the risk as in the race") {
    auto followUps = [](bool doubleYellow) {
        int total = 0;
        for (std::uint64_t seed = 1; seed <= 30; ++seed) {
            RaceConfig config;
            config.seed = seed;
            config.qualifying = true;
            config.accidentChanceScale = 0.0; // only the follow-ups of the one placed crash
            Sim sim(loadTrack(dataDir() / "tracks" / "testring.json"), loadEntries(dataDir() / "entries" / "fictional-grid.json"),
                    config);
            runSeconds(sim, 300.0);
            crashFlyingCar(sim);
            if (doubleYellow) {
                Command c;
                c.type = Command::Type::SetSectorFlag;
                c.sector = sim.snapshot().hazards.front().marshalSector;
                c.flag = Flag::DoubleYellow;
                sim.command(c);
            }
            runSeconds(sim, 600.0);
            total += sim.stats().followUpAccidents;
        }
        return total;
    };
    const int none = followUps(false);
    const int doubleYellow = followUps(true);
    MESSAGE("follow-up accidents in 30 sessions: no flag " << none << ", double yellow " << doubleYellow);
    CHECK(none >= 3);
    CHECK(doubleYellow * 3 < none);
}

TEST_CASE("Sessions with accidents and nobody at the flags still finish") {
    int accidents = 0;
    int sessionsWithAccident = 0;
    for (std::uint64_t seed = 1; seed <= 20; ++seed) {
        CAPTURE(seed);
        Sim sim = makeQualifying(seed, dataDir() / "tracks" / "testring.json", 1.0);
        runSession(sim);
        accidents += sim.stats().accidents;
        sessionsWithAccident += sim.stats().accidents > 0 ? 1 : 0;
        CHECK(sim.snapshot().qualifyingSegment == 3); // every break ended: the track was cleared in it
        CHECK(sim.qualifyingOrder().size() == 20);
    }
    MESSAGE(accidents << " cars out by accidents in 20 sessions, " << sessionsWithAccident << " sessions with one");
    CHECK(sessionsWithAccident >= 4);
    CHECK(sessionsWithAccident <= 16);
}

TEST_CASE("No Safety Car, VSC or closed pit entry in a session") {
    Sim sim = makeQualifying(1);
    runSeconds(sim, 120.0);
    send(sim, Command::Type::DeploySafetyCar);
    CHECK_FALSE(sim.snapshot().safetyCarOnTrack);
    send(sim, Command::Type::DeployVirtualSafetyCar);
    CHECK(sim.snapshot().state == RaceState::Green);
    send(sim, Command::Type::ClosePitEntry);
    CHECK_FALSE(sim.snapshot().pitEntryClosed);
    send(sim, Command::Type::SafetyCarStart);
    CHECK(sim.snapshot().state == RaceState::Green);
    Command garage; // an accident in the garage is not possible
    garage.type = Command::Type::InjectAccident;
    garage.car = sim.snapshot().cars.back().entryIndex;
    for (const CarState& car : sim.snapshot().cars) {
        if (car.qualifyingPhase == QualifyingPhase::Garage) {
            garage.car = car.entryIndex;
        }
    }
    sim.command(garage);
    sim.step();
    CHECK(sim.accidents().empty());
}
