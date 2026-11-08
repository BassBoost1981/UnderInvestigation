// M7.6: track limits, counted by race control.

#include "uisim/DataDir.h"
#include "uisim/DataLoader.h"
#include "uisim/Rating.h"
#include "uisim/Sim.h"

#include <doctest/doctest.h>

#include <filesystem>
#include <map>

using namespace uisim;

TEST_CASE("Track limits: strikes, black-and-white flag, then incidents") {
    const std::filesystem::path data(UISIM_DATA_DIR);
    int offences = 0;
    int incidents = 0;
    constexpr int kRaces = 20;
    for (std::uint64_t seed = 1; seed <= kRaces; ++seed) {
        RaceConfig config;
        config.laps = 50;
        config.seed = seed;
        config.rules = loadRules(data / "rules" / "fia-f1-2026.json");
        Sim sim(loadTrack(data / "tracks" / "testring.json"), loadEntries(data / "entries" / "fictional-grid.json"), config);
        std::map<int, int> perCar; // track-limits incidents per entry
        for (long steps = 0; !sim.finished() && steps < 2'000'000; ++steps) {
            sim.step();
        }
        for (const Incident& incident : sim.incidents()) {
            if (incident.type != Incident::Type::TrackLimits) {
                continue;
            }
            ++incidents;
            ++perCar[incident.car];
            CHECK(incident.car == incident.otherCar);
            CHECK(incident.measure >= config.rules.trackLimitsPenaltyFrom); // never before the warning
            CHECK(expectedVerdict(incident).penalty == Penalty::FiveSeconds);
        }
        for (const CarState& car : sim.snapshot().cars) {
            // One incident for every offence from the penalised one on.
            const int expected = std::max(0, car.trackLimitStrikes - config.rules.trackLimitsPenaltyFrom + 1);
            CHECK(perCar[car.entryIndex] == expected);
            CHECK(car.blackWhiteFlag == (car.trackLimitStrikes >= config.rules.trackLimitsBlackWhiteAt));
        }
        offences += sim.stats().trackLimitOffences;
    }
    INFO("in ", kRaces, " races: offences ", offences, ", incidents ", incidents);
    CHECK(offences >= 3 * kRaces);  // a few per race
    CHECK(offences <= 20 * kRaces);
    CHECK(incidents >= 1);           // but a penalty stays rare
    CHECK(incidents <= 2 * kRaces);
}
