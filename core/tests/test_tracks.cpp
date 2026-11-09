// M7.7 / M9.6: the sim does not depend on the testring; key properties on every track.
// Public: the testring. Private (data/tracks/private, gitignored): the real circuits, if present.

#include "uisim/DataDir.h"
#include "uisim/DataLoader.h"
#include "uisim/Sim.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

using namespace uisim;

namespace {

const std::filesystem::path kData(UISIM_DATA_DIR);

std::vector<std::filesystem::path> privateTracks() {
    std::vector<std::filesystem::path> tracks;
    const std::filesystem::path dir = kData / "tracks" / "private";
    if (std::filesystem::is_directory(dir)) {
        for (const auto& entry : std::filesystem::directory_iterator(dir)) {
            if (entry.is_regular_file() && entry.path().extension() == ".json") {
                tracks.push_back(entry.path());
            }
        }
    }
    std::sort(tracks.begin(), tracks.end());
    return tracks;
}

Sim makeRace(const std::filesystem::path& track, std::uint64_t seed) {
    RaceConfig config;
    config.laps = 50;
    config.seed = seed;
    config.rules = loadRules(kData / "rules" / "fia-f1-2026.json");
    return Sim(loadTrack(track), loadEntries(kData / "entries" / "fictional-grid.json"), config);
}

void runToEnd(Sim& sim) {
    for (long steps = 0; !sim.finished() && steps < 2'000'000; ++steps) {
        sim.step();
    }
    REQUIRE(sim.finished());
}

void checkTrackData(const Track& track) {
    CHECK(track.closureError() < 0.03 * track.length()); // as in test_track.cpp
    CHECK(track.overtakeDetectionLine() >= 0.0);
    CHECK(track.overtakeActivationLine() >= 0.0);
    CHECK(track.compounds()[0].pace > track.compounds()[2].pace);
    bool trackLimits = false;
    for (const TrackSegment& seg : track.segments()) {
        trackLimits = trackLimits || seg.trackLimits;
    }
    CHECK(trackLimits);
}

} // namespace

TEST_CASE("Testring: data, determinism and plausible races") {
    const std::filesystem::path path = kData / "tracks" / "testring.json";
    const Track track = loadTrack(path);
    checkTrackData(track);
    CHECK(track.segments().front().length > 300.0); // pit exit lies on the main straight

    Sim a = makeRace(path, 7);
    Sim b = makeRace(path, 7);
    runToEnd(a);
    runToEnd(b);
    CHECK(a.snapshot().time == b.snapshot().time);
    CHECK(a.stats().overtakes == b.stats().overtakes);

    double overtakes = 0.0;
    constexpr int kRaces = 5;
    for (std::uint64_t seed = 1; seed <= kRaces; ++seed) {
        Sim sim = makeRace(path, seed);
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

TEST_CASE("Real circuits (private, only if present): data and a full race on each") {
    const auto tracks = privateTracks();
    if (tracks.empty()) {
        MESSAGE("no private tracks in data/tracks/private: skipped");
        return;
    }
    for (const auto& path : tracks) {
        INFO(path.filename().string());
        const Track track = loadTrack(path);
        checkTrackData(track);
        CHECK(track.length() > 3000.0);
        CHECK(track.length() < 7500.0);
        Sim sim = makeRace(path, 1);
        runToEnd(sim);
        CHECK(sim.stats().overtakes > 0);
        int finishers = 0;
        for (const CarState& car : sim.snapshot().cars) {
            finishers += car.finished ? 1 : 0;
            if (car.finished) {
                CHECK(car.pitStops >= 1);
            }
        }
        CHECK(finishers >= 10);
    }
    // Determinism on one of them.
    Sim a = makeRace(tracks.front(), 7);
    Sim b = makeRace(tracks.front(), 7);
    runToEnd(a);
    runToEnd(b);
    CHECK(a.snapshot().time == b.snapshot().time);
}
