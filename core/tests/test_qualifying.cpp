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

Sim makeQualifying(std::uint64_t seed, const std::filesystem::path& track = dataDir() / "tracks" / "testring.json") {
    RaceConfig config;
    config.seed = seed;
    config.qualifying = true;
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
        CHECK(sim.incidents().empty()); // incidents in qualifying come with 13.5
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
