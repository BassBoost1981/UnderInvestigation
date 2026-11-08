// M7.3: tyre compounds and wear.

#include "uisim/DataDir.h"
#include "uisim/DataLoader.h"
#include "uisim/Sim.h"
#include "uisim/Tyres.h"

#include <doctest/doctest.h>

#include <filesystem>

using namespace uisim;

namespace {

Track testring() { return loadTrack(std::filesystem::path(UISIM_DATA_DIR) / "tracks" / "testring.json"); }

} // namespace

TEST_CASE("Track data provides three dry compounds") {
    const CompoundSpecs& c = testring().compounds();
    CHECK(c[0].pace > c[1].pace);       // soft quickest on a new set
    CHECK(c[1].pace > c[2].pace);
    CHECK(c[0].wearPerLap > c[2].wearPerLap);
    CHECK(c[0].cliffLap < c[1].cliffLap);
    CHECK(c[1].cliffLap < c[2].cliffLap);
}

TEST_CASE("A soft stint starts faster and fades sooner than a hard one") {
    const CompoundSpecs& c = testring().compounds();
    CHECK(tyreFactor(c, Compound::Soft, 0) > tyreFactor(c, Compound::Hard, 0));
    CHECK(tyreFactor(c, Compound::Soft, 25) < tyreFactor(c, Compound::Hard, 25));
    const double softLoss = tyreFactor(c, Compound::Soft, 0) - tyreFactor(c, Compound::Soft, 20);
    const double hardLoss = tyreFactor(c, Compound::Hard, 0) - tyreFactor(c, Compound::Hard, 20);
    CHECK(softLoss > hardLoss);
    // The cliff: losing more per lap after it than before.
    const CompoundSpec& s = c[static_cast<int>(Compound::Soft)];
    const double before = tyreFactor(c, Compound::Soft, s.cliffLap - 1) - tyreFactor(c, Compound::Soft, s.cliffLap);
    const double after = tyreFactor(c, Compound::Soft, s.cliffLap + 1) - tyreFactor(c, Compound::Soft, s.cliffLap + 2);
    CHECK(after > 2.0 * before);
}

Sim makeRace(std::uint64_t seed, int laps = 50) {
    RaceConfig config;
    config.laps = laps;
    config.seed = seed;
    config.accidentChanceScale = 0.0;
    return Sim(testring(), loadEntries(std::filesystem::path(UISIM_DATA_DIR) / "entries" / "fictional-grid.json"), config);
}

TEST_CASE("Every finisher stops and uses two dry compounds (B6.3.6)") {
    for (std::uint64_t seed = 1; seed <= 5; ++seed) {
        Sim sim = makeRace(seed);
        const int softStarter = sim.snapshot().cars.back().entryIndex; // last on the grid starts on soft
        int softStopLap = -1;
        for (long steps = 0; !sim.finished() && steps < 2'000'000; ++steps) {
            sim.step();
            for (const CarState& car : sim.snapshot().cars) {
                if (car.entryIndex == softStarter && car.pitStops > 0 && softStopLap < 0) {
                    softStopLap = car.lapsCompleted;
                }
            }
        }
        const int cliff = sim.track().compounds()[static_cast<int>(Compound::Soft)].cliffLap;
        CHECK(softStopLap > 0);
        CHECK(softStopLap <= cliff); // stops before the soft drops off
        for (const CarState& car : sim.snapshot().cars) {
            if (car.finished) {
                CHECK(car.pitStops >= 1);
                CHECK(car.compoundsUsed >= 2);
                CHECK(car.tyreAge < car.lapsCompleted);
            }
        }
    }
}

TEST_CASE("Teams take a cheap stop under the Safety Car") {
    // Same races with and without a Safety Car in the middle; count stops while it is out.
    int withSc = 0;
    int without = 0;
    for (std::uint64_t seed = 1; seed <= 5; ++seed) {
        for (bool safetyCar : {true, false}) {
            Sim sim = makeRace(seed);
            for (long s = 0; s < static_cast<long>(1200.0 / Sim::kDt); ++s) {
                sim.step();
            }
            if (safetyCar) {
                Command c;
                c.type = Command::Type::DeploySafetyCar;
                sim.command(c);
            }
            int before = 0;
            for (const CarState& car : sim.snapshot().cars) {
                before += car.pitStops;
            }
            for (long s = 0; s < static_cast<long>(150.0 / Sim::kDt); ++s) {
                sim.step();
            }
            int after = 0;
            for (const CarState& car : sim.snapshot().cars) {
                after += car.pitStops;
            }
            (safetyCar ? withSc : without) += after - before;
        }
    }
    INFO("stops in 150 s: with Safety Car ", withSc, ", without ", without);
    CHECK(withSc > 2 * without);
}
