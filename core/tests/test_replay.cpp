// M4: incidents with ground truth and the replay recording.

#include "uisim/DataDir.h"
#include "uisim/DataLoader.h"
#include "uisim/Sim.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>

using namespace uisim;

namespace {

Sim makeRace(std::uint64_t seed) {
    const std::filesystem::path data(UISIM_DATA_DIR);
    RaceConfig config;
    config.laps = 50;
    config.seed = seed;
    return Sim(loadTrack(data / "tracks" / "testring.json"), loadEntries(data / "entries" / "fictional-grid.json"), config);
}

void runToEnd(Sim& sim) {
    for (long steps = 0; !sim.finished() && steps < 2'000'000; ++steps) {
        sim.step();
    }
    REQUIRE(sim.finished());
}

} // namespace

TEST_CASE("Collisions are recorded with a ground truth") {
    int collisions = 0;
    for (std::uint64_t seed = 1; seed <= 5; ++seed) {
        Sim sim = makeRace(seed);
        runToEnd(sim);
        double previousTime = 0.0;
        for (std::size_t k = 0; k < sim.incidents().size(); ++k) {
            const Incident& incident = sim.incidents()[k];
            CHECK(incident.id == static_cast<int>(k) + 1);
            CHECK(incident.time >= previousTime);
            const bool single = incident.type == Incident::Type::Accident || incident.type == Incident::Type::PitLaneSpeeding;
            CHECK(single == (incident.car == incident.otherCar));
            CHECK(incident.blame >= 0.0);
            CHECK(incident.blame <= 1.0);
            previousTime = incident.time;
            if (incident.type == Incident::Type::Collision) {
                ++collisions;
            }
        }
        const auto soloAccidents = std::count_if(sim.accidents().begin(), sim.accidents().end(),
            [](const Accident& a) { return !a.fromCollision; });
        const auto pitLane = std::count_if(sim.incidents().begin(), sim.incidents().end(), [](const Incident& v) {
            return v.type == Incident::Type::PitLaneSpeeding || v.type == Incident::Type::UnsafeRelease;
        });
        CHECK(sim.stats().collisions + sim.stats().flagViolations + sim.stats().blueFlagsIgnored + soloAccidents + pitLane ==
              static_cast<long long>(sim.incidents().size()));
    }
    INFO("collisions in 5 races: ", collisions);
    CHECK(collisions >= 5);  // something to investigate in a typical race
    CHECK(collisions <= 50); // but not carnage
}

TEST_CASE("Replay records every tick and matches the live state") {
    Sim sim = makeRace(2);
    long steps = 0;
    for (; steps < 6000; ++steps) {
        sim.step();
    }
    const auto& frames = sim.replayFrames();
    REQUIRE(frames.size() == static_cast<std::size_t>(steps) + 1); // plus the grid frame
    CHECK(frames.front().time == 0.0);
    CHECK(frames.back().time == doctest::Approx(sim.snapshot().time));

    // Last frame equals the current snapshot.
    for (const CarState& car : sim.snapshot().cars) {
        const ReplayCar& sample = sim.replayCar(frames.size() - 1, car.entryIndex);
        CHECK(sample.distance == car.distance);
        CHECK(sample.speed == doctest::Approx(car.speed).epsilon(1e-6));
    }

    // Frame lookup.
    CHECK(sim.replayFrameAt(-5.0) == 0);
    CHECK(sim.replayFrameAt(1e9) == frames.size() - 1);
    const std::size_t mid = sim.replayFrameAt(123.456);
    CHECK(frames[mid].time <= 123.456);
    CHECK(frames[mid + 1].time > 123.456);
}

TEST_CASE("Replay shows the cars at the incident location") {
    Sim sim = makeRace(1);
    runToEnd(sim);
    REQUIRE_FALSE(sim.incidents().empty());
    for (const Incident& incident : sim.incidents()) {
        const std::size_t frame = sim.replayFrameAt(incident.time);
        const double a = sim.replayCar(frame, incident.car).distance;
        const double b = sim.replayCar(frame, incident.otherCar).distance;
        CHECK(a == doctest::Approx(incident.distance)); // recorded in the same tick
        // Together on the road (also when lapping: then a full lap apart in total distance).
        const double road = std::min(sim.track().wrap(a - b), sim.track().wrap(b - a));
        // Blue flags: logged after the lapped car held the faster one up for a while (within ~1 s).
        const double limit = incident.type == Incident::Type::IgnoringBlueFlags ? 100.0
                           : incident.type == Incident::Type::UnsafeRelease    ? 30.0 // release window in the pit lane
                                                                               : 20.0;
        CHECK(road < limit);
    }
}
