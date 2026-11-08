// M7.7: the sim does not depend on the testring; key properties on every track.

#include "uisim/DataDir.h"
#include "uisim/DataLoader.h"
#include "uisim/Sim.h"

#include <doctest/doctest.h>

#include <filesystem>
#include <string>

using namespace uisim;

namespace {

const char* const kTracks[] = {"testring.json", "valle-alta.json"};

Sim makeRace(const std::string& track, std::uint64_t seed) {
    const std::filesystem::path data(UISIM_DATA_DIR);
    RaceConfig config;
    config.laps = 50;
    config.seed = seed;
    config.rules = loadRules(data / "rules" / "fia-f1-2026.json");
    return Sim(loadTrack(data / "tracks" / track), loadEntries(data / "entries" / "fictional-grid.json"), config);
}

void runToEnd(Sim& sim) {
    for (long steps = 0; !sim.finished() && steps < 2'000'000; ++steps) {
        sim.step();
    }
    REQUIRE(sim.finished());
}

} // namespace

TEST_CASE("Every track closes, has overtake lines, tyres and a pit straight") {
    for (const char* name : kTracks) {
        INFO(std::string(name));
        const Track track = loadTrack(std::filesystem::path(UISIM_DATA_DIR) / "tracks" / name);
        CHECK(track.closureError() < 0.03 * track.length()); // as in test_track.cpp
        CHECK(track.overtakeDetectionLine() >= 0.0);
        CHECK(track.overtakeActivationLine() >= 0.0);
        CHECK(track.compounds()[0].pace > track.compounds()[2].pace);
        CHECK(track.segments().front().length > 300.0); // pit exit lies on the main straight
        bool trackLimits = false;
        for (const TrackSegment& seg : track.segments()) {
            trackLimits = trackLimits || seg.trackLimits;
        }
        CHECK(trackLimits);
    }
}

TEST_CASE("Races on every track are deterministic and plausible") {
    for (const char* name : kTracks) {
        INFO(std::string(name));
        Sim a = makeRace(name, 7);
        Sim b = makeRace(name, 7);
        runToEnd(a);
        runToEnd(b);
        CHECK(a.snapshot().time == b.snapshot().time);
        CHECK(a.stats().overtakes == b.stats().overtakes);

        double overtakes = 0.0;
        constexpr int kRaces = 5;
        for (std::uint64_t seed = 1; seed <= kRaces; ++seed) {
            Sim sim = makeRace(name, seed);
            runToEnd(sim);
            overtakes += sim.stats().overtakes;
            for (const CarState& car : sim.snapshot().cars) {
                if (car.finished) {
                    CHECK(car.pitStops >= 1);
                    CHECK(car.compoundsUsed >= 2); // B6.3.6
                }
            }
        }
        INFO("average overtakes ", overtakes / kRaces);
        CHECK(overtakes / kRaces >= 20.0);
        CHECK(overtakes / kRaces <= 50.0);
    }
}
