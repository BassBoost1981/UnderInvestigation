// M7.5: pit lane offences (speeding, unsafe release) with a ground truth.

#include "uisim/DataDir.h"
#include "uisim/DataLoader.h"
#include "uisim/Rating.h"
#include "uisim/Sim.h"

#include <doctest/doctest.h>

#include <filesystem>

using namespace uisim;

TEST_CASE("Speeding and unsafe releases happen and carry a fitting ground truth") {
    const std::filesystem::path data(UISIM_DATA_DIR);
    int speeding = 0;
    int unsafe = 0;
    for (std::uint64_t seed = 1; seed <= 20; ++seed) {
        RaceConfig config;
        config.laps = 50;
        config.seed = seed;
        config.rules = loadRules(data / "rules" / "fia-f1-2026.json");
        Sim sim(loadTrack(data / "tracks" / "testring.json"), loadEntries(data / "entries" / "fictional-grid.json"), config);
        for (long steps = 0; !sim.finished() && steps < 2'000'000; ++steps) {
            sim.step();
        }
        for (const Incident& incident : sim.incidents()) {
            const ExpectedVerdict expected = expectedVerdict(incident);
            if (incident.type == Incident::Type::PitLaneSpeeding) {
                ++speeding;
                CHECK(incident.car == incident.otherCar);
                CHECK(incident.measure >= 1.0);
                CHECK(incident.measure <= 20.0);
                CHECK(expected.car == incident.car);
                // Penalty Guidelines tariff: < 6 km/h 5 s, 6-15 drive-through, > 15 stop-and-go.
                const Penalty tariff = incident.measure > config.rules.pitSpeedingStopAndGoAboveKmh ? Penalty::StopAndGo
                    : incident.measure >= config.rules.pitSpeedingDriveThroughFromKmh             ? Penalty::DriveThrough
                                                                                                   : Penalty::FiveSeconds;
                CHECK(expected.penalty == tariff);
            } else if (incident.type == Incident::Type::UnsafeRelease) {
                ++unsafe;
                CHECK(incident.car != incident.otherCar);
                CHECK(incident.measure > 0.0);
                CHECK(incident.measure < 30.0);
                CHECK(expected.car == incident.car);
                CHECK((expected.penalty == Penalty::FiveSeconds || expected.penalty == Penalty::TenSeconds));
            }
        }
    }
    INFO("in 20 races: speeding ", speeding, ", unsafe releases ", unsafe);
    CHECK(speeding >= 3);
    CHECK(unsafe >= 2);
}
