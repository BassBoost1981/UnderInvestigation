#include "uisim/DataDir.h"
#include "uisim/DataLoader.h"
#include "uisim/Sim.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <stdexcept>

using namespace uisim;

namespace {

std::filesystem::path dataDir() { return std::filesystem::path(UISIM_DATA_DIR); }

Sim makeRace(std::uint64_t seed, int laps = 50) {
    return Sim(loadTrack(dataDir() / "tracks" / "testring.json"),
               loadEntries(dataDir() / "entries" / "fictional-grid.json"),
               RaceConfig{laps, seed});
}

void runToEnd(Sim& sim) {
    // Guard against a stuck sim: 10 minutes per lap is far beyond any real pace.
    const long maxSteps = static_cast<long>(sim.snapshot().totalLaps) * 600 * 20;
    for (long steps = 0; !sim.finished() && steps < maxSteps; ++steps) {
        sim.step();
    }
    REQUIRE(sim.finished());
}

#ifdef NDEBUG
constexpr bool kOptimizedBuild = true;
#else
constexpr bool kOptimizedBuild = false;
#endif

} // namespace

TEST_CASE("Sim rejects invalid configuration") {
    const Track track = loadTrack(dataDir() / "tracks" / "testring.json");
    const auto entries = loadEntries(dataDir() / "entries" / "fictional-grid.json");
    CHECK_THROWS_AS(Sim(track, {}, RaceConfig{50, 1}), std::invalid_argument);
    CHECK_THROWS_AS(Sim(track, entries, RaceConfig{0, 1}), std::invalid_argument);
}

TEST_CASE("Cars start behind the line in grid order") {
    const Sim sim = makeRace(42);
    const auto& cars = sim.snapshot().cars;
    REQUIRE(cars.size() == 20);
    for (std::size_t r = 0; r < cars.size(); ++r) {
        CHECK(cars[r].distance < 0.0);
        CHECK(cars[r].position == cars[r].gridPosition);
        if (r > 0) {
            CHECK(cars[r].distance < cars[r - 1].distance);
        }
    }
}

TEST_CASE("Same seed produces an identical race") {
    Sim a = makeRace(42);
    Sim b = makeRace(42);
    runToEnd(a);
    runToEnd(b);
    const auto& sa = a.snapshot();
    const auto& sb = b.snapshot();
    CHECK(sa.time == sb.time);
    CHECK(a.stats().overtakes == b.stats().overtakes);
    CHECK(a.stats().driverErrors == b.stats().driverErrors);
    REQUIRE(sa.cars.size() == sb.cars.size());
    for (std::size_t r = 0; r < sa.cars.size(); ++r) {
        CHECK(sa.cars[r].entryIndex == sb.cars[r].entryIndex);
        CHECK(sa.cars[r].distance == sb.cars[r].distance);
        CHECK(sa.cars[r].bestLapTime == sb.cars[r].bestLapTime);
        CHECK(sa.cars[r].lastLineTime == sb.cars[r].lastLineTime);
    }
}

TEST_CASE("Different seeds produce different races") {
    Sim a = makeRace(1);
    Sim b = makeRace(2);
    runToEnd(a);
    runToEnd(b);
    bool differs = a.snapshot().time != b.snapshot().time;
    for (std::size_t r = 0; r < a.snapshot().cars.size(); ++r) {
        differs = differs || a.snapshot().cars[r].entryIndex != b.snapshot().cars[r].entryIndex;
    }
    CHECK(differs);
}

TEST_CASE("50-lap race result is plausible") {
    Sim sim = makeRace(42);
    runToEnd(sim);
    const auto& snap = sim.snapshot();
    REQUIRE(snap.cars.size() == 20);

    const CarState& winner = snap.cars.front();
    CHECK(winner.lapsCompleted == 50);
    CHECK(winner.lastLineTime > 50 * 80.0);
    CHECK(winner.lastLineTime < 50 * 110.0);

    double fastest = 1e9;
    double slowest = 0.0;
    for (std::size_t r = 0; r < snap.cars.size(); ++r) {
        const CarState& car = snap.cars[r];
        CHECK(car.position == static_cast<int>(r) + 1);
        CHECK((car.finished || car.retired));
        if (car.retired) {
            continue; // accidents (M6): retired cars have no meaningful lap times or gaps
        }
        CHECK(car.lapsCompleted >= 48);
        fastest = std::min(fastest, car.bestLapTime);
        slowest = std::max(slowest, car.bestLapTime);
        // Timing consistency: on the lead lap the gap grows with position.
        if (r > 0 && car.lapsDown == 0 && snap.cars[r - 1].lapsDown == 0) {
            CHECK(car.gapToLeader >= snap.cars[r - 1].gapToLeader);
        }
    }
    INFO("fastest best lap ", fastest, " s, slowest best lap ", slowest, " s");
    CHECK(fastest > 80.0);
    CHECK(fastest < 105.0);
    CHECK(slowest - fastest > 0.3); // drivers differ
    CHECK(slowest - fastest < 6.0); // but the field is competitive
    CHECK(sim.stats().overtakes > 0);
}

TEST_CASE("Overtaking level is race-like over several seeds") {
    // Tuning target (2026-11-07): 20-50 overtakes per 50-lap race on average, and the
    // result must not just reproduce the grid. Single races may fall outside the range.
    constexpr int kRaces = 5;
    int overtakes = 0;
    double displacement = 0.0;
    for (int seed = 1; seed <= kRaces; ++seed) {
        Sim sim = makeRace(static_cast<std::uint64_t>(seed));
        runToEnd(sim);
        overtakes += sim.stats().overtakes;
        for (const CarState& car : sim.snapshot().cars) {
            displacement += std::abs(car.position - car.gridPosition);
        }
    }
    const double averageOvertakes = overtakes / static_cast<double>(kRaces);
    const double averageDisplacement = displacement / (kRaces * 20.0);
    INFO("average overtakes ", averageOvertakes, ", average |grid - finish| ", averageDisplacement);
    CHECK(averageOvertakes >= 20.0);
    CHECK(averageOvertakes <= 50.0);
    CHECK(averageDisplacement >= 1.0);
}

TEST_CASE("50 laps simulate in under 2 seconds" * doctest::skip(!kOptimizedBuild)) {
    const auto start = std::chrono::steady_clock::now();
    Sim sim = makeRace(42);
    runToEnd(sim);
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    INFO("simulation took ", seconds, " s");
    CHECK(seconds < 2.0);
}
