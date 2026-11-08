#include "uisim/DataDir.h"
#include "uisim/DataLoader.h"
#include "uisim/Rng.h"
#include "uisim/Track.h"

#include <doctest/doctest.h>

#include <cmath>
#include <set>
#include <stdexcept>

using namespace uisim;

namespace {

std::filesystem::path dataDir() { return std::filesystem::path(UISIM_DATA_DIR); }

TrackSegment segment(double length, double speed, int sector) {
    TrackSegment s;
    s.length = length;
    s.speed = speed;
    s.sector = sector;
    return s;
}

} // namespace

TEST_CASE("Testring loads with expected layout") {
    const Track track = loadTrack(dataDir() / "tracks" / "testring.json");
    CHECK(track.name() == "Testring Nordhafen");
    CHECK(track.segments().size() == 16);
    CHECK(track.length() == doctest::Approx(5600.0));
    CHECK(track.sectorStart(0) == 0.0);
    CHECK(track.sectorStart(1) == doctest::Approx(2150.0));
    CHECK(track.sectorStart(2) == doctest::Approx(4330.0));
}

TEST_CASE("Segment lookup and wrapping") {
    const Track track = loadTrack(dataDir() / "tracks" / "testring.json");
    CHECK(track.segmentIndexAt(0.0) == 0);
    CHECK(track.segmentIndexAt(899.9) == 0);
    CHECK(track.segmentIndexAt(900.0) == 1);
    CHECK(track.segmentIndexAt(5599.0) == 15);
    CHECK(track.wrap(-5.0) == doctest::Approx(5595.0));
    CHECK(track.wrap(2 * 5600.0 + 10.0) == doctest::Approx(10.0));
}

TEST_CASE("Testring geometry closes and is consistent") {
    const Track track = loadTrack(dataDir() / "tracks" / "testring.json");
    INFO("closure error ", track.closureError(), " m");
    CHECK(track.closureError() < 0.03 * track.length()); // turn data nearly closes the loop

    const TrackPose start = track.pose(0.0);
    CHECK(start.x == doctest::Approx(0.0));
    CHECK(start.y == doctest::Approx(0.0));
    const TrackPose wrapped = track.pose(track.length());
    CHECK(wrapped.x == doctest::Approx(start.x));
    CHECK(wrapped.y == doctest::Approx(start.y));

    // Consecutive samples ~kSampleSpacing apart, also across the line.
    const auto& line = track.centerline();
    for (std::size_t i = 0; i < line.size(); ++i) {
        const TrackPose& a = line[i];
        const TrackPose& b = line[(i + 1) % line.size()];
        CHECK(std::hypot(b.x - a.x, b.y - a.y) == doctest::Approx(Track::kSampleSpacing).epsilon(0.15));
    }

    // Lateral offset is perpendicular and positive to the left (start heading +x => left = +y).
    const TrackPose left = track.pose(0.0, 4.0);
    CHECK(left.x == doctest::Approx(0.0));
    CHECK(left.y == doctest::Approx(4.0));
}

TEST_CASE("Track rejects invalid data") {
    CHECK_THROWS_AS(Track("empty", {}), std::invalid_argument);
    CHECK_THROWS_AS(Track("skips sector 2", {segment(100, 50, 0), segment(100, 50, 2)}),
                    std::invalid_argument);
    CHECK_THROWS_AS(Track("only two sectors", {segment(100, 50, 0), segment(100, 50, 1)}),
                    std::invalid_argument);
    CHECK_THROWS_AS(Track("zero length", {segment(0, 50, 0), segment(100, 50, 1), segment(100, 50, 2)}),
                    std::invalid_argument);
    CHECK_NOTHROW(Track("ok", {segment(100, 50, 0), segment(100, 50, 1), segment(100, 50, 2)}));
}

TEST_CASE("Loader reports missing files") {
    CHECK_THROWS_AS(loadTrack(dataDir() / "tracks" / "does-not-exist.json"), std::runtime_error);
    CHECK_THROWS_AS(loadEntries(dataDir() / "entries" / "does-not-exist.json"), std::runtime_error);
}

TEST_CASE("Fictional grid has 20 unique entries") {
    const auto entries = loadEntries(dataDir() / "entries" / "fictional-grid.json");
    CHECK(entries.size() == 20);
    std::set<int> numbers;
    std::set<std::string> codes;
    for (const Entry& e : entries) {
        numbers.insert(e.number);
        codes.insert(e.code);
    }
    CHECK(numbers.size() == 20);
    CHECK(codes.size() == 20);
}

TEST_CASE("Pcg32 is deterministic and in range") {
    Pcg32 a(7), b(7), c(8);
    bool differs = false;
    for (int i = 0; i < 1000; ++i) {
        const double va = a.uniform();
        CHECK(va == b.uniform());
        CHECK(va >= 0.0);
        CHECK(va < 1.0);
        differs = differs || va != c.uniform();
    }
    CHECK(differs);
}
