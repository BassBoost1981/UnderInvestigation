// M9.3: intermediate and wet tyres, and when the teams change to them.

#include "uisim/DataDir.h"
#include "uisim/DataLoader.h"
#include "uisim/Sim.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <map>

using namespace uisim;

namespace {

const std::filesystem::path kData(UISIM_DATA_DIR);

Sim makeRace(std::uint64_t seed, const WeatherPlan& weather, int laps) {
    RaceConfig config;
    config.laps = laps;
    config.seed = seed;
    config.rules = loadRules(kData / "rules" / "fia-f1-2026.json");
    config.weather = weather;
    config.compoundRuleMissChance = 0.0;
    return Sim(loadTrack(kData / "tracks" / "testring.json"), loadEntries(kData / "entries" / "fictional-grid.json"), config);
}

WeatherPlan plan(std::vector<WeatherPoint> points) {
    WeatherPlan p;
    p.name = "test";
    p.points = std::move(points);
    return p;
}

int incidentsOf(const Sim& sim, Incident::Type type) {
    return static_cast<int>(std::count_if(sim.incidents().begin(), sim.incidents().end(),
        [&](const Incident& incident) { return incident.type == type; }));
}

} // namespace

TEST_CASE("Rain tyres: the right tyre for the wetness") {
    const CompoundSpecs& c = loadTrack(kData / "tracks" / "testring.json").compounds();
    const CompoundSpec& slick = c[static_cast<int>(Compound::Medium)];
    const CompoundSpec& inter = c[static_cast<int>(Compound::Intermediate)];
    const CompoundSpec& wet = c[static_cast<int>(Compound::Wet)];
    CHECK(wetSpeedFactor(slick, 0.0) > wetSpeedFactor(inter, 0.0)); // dry: slicks
    CHECK(wetSpeedFactor(inter, 0.6) > wetSpeedFactor(slick, 0.6)); // damp: intermediates
    CHECK(wetSpeedFactor(inter, 0.6) > wetSpeedFactor(wet, 0.6));
    CHECK(wetSpeedFactor(wet, 1.0) > wetSpeedFactor(inter, 1.0));   // soaked: wets
    CHECK(inter.dryWearFactor > 0.0);                               // rain tyres overheat when dry
    CHECK(slick.dryWearFactor == 0.0);
}

TEST_CASE("Rain tyres: on a damp track the field changes to intermediates") {
    Sim sim = makeRace(1, plan({{0.0, 0.0}, {300.0, 0.0}, {360.0, 0.5}}), 30);
    for (long steps = 0; sim.snapshot().leaderLap < 15 && steps < 2'000'000; ++steps) {
        sim.step();
    }
    int onInters = 0;
    int running = 0;
    for (const CarState& car : sim.snapshot().cars) {
        if (!car.retired && !car.finished) {
            ++running;
            onInters += car.compound == Compound::Intermediate ? 1 : 0;
        }
    }
    INFO(onInters, " of ", running, " on intermediates at lap 15");
    CHECK(onInters >= running * 8 / 10);
}

TEST_CASE("Rain tyres: wets in the downpour, slicks again once it has dried; no two-compound incident") {
    int switchedToRain = 0;
    int backOnSlicks = 0;
    int finishers = 0;
    std::vector<double> firstRainStop;
    for (std::uint64_t seed = 1; seed <= 4; ++seed) {
        Sim sim = makeRace(seed, plan({{0.0, 0.0}, {300.0, 0.0}, {360.0, 1.0}, {1200.0, 1.0}, {1260.0, 0.0}}), 45);
        std::map<int, double> firstRain;
        for (long steps = 0; !sim.finished() && steps < 2'000'000; ++steps) {
            sim.step();
            for (const CarState& car : sim.snapshot().cars) {
                if (isWetCompound(car.compound) && firstRain.count(car.entryIndex) == 0) {
                    firstRain[car.entryIndex] = sim.snapshot().time;
                }
            }
        }
        for (const CarState& car : sim.snapshot().cars) {
            if (!car.finished) {
                continue;
            }
            ++finishers;
            switchedToRain += static_cast<int>(firstRain.count(car.entryIndex));
            backOnSlicks += isWetCompound(car.compound) ? 0 : 1;
        }
        if (seed == 1) {
            for (const auto& [entry, time] : firstRain) {
                firstRainStop.push_back(time);
            }
        }
        CHECK(incidentsOf(sim, Incident::Type::CompoundRule) == 0);
    }
    INFO("finishers ", finishers, ", changed to rain tyres ", switchedToRain, ", on slicks at the end ", backOnSlicks);
    CHECK(switchedToRain >= finishers * 9 / 10);
    CHECK(backOnSlicks >= finishers * 8 / 10);
    // Not everyone in the same lap: some teams gamble, some hesitate.
    REQUIRE(firstRainStop.size() >= 2);
    const auto [earliest, latest] = std::minmax_element(firstRainStop.begin(), firstRainStop.end());
    CHECK(*latest - *earliest > 60.0);
}

TEST_CASE("Rain tyres: never used in a dry race") {
    Sim sim = makeRace(2, WeatherPlan{}, 30);
    for (long steps = 0; !sim.finished() && steps < 2'000'000; ++steps) {
        sim.step();
        if (steps % 400 == 0) {
            for (const CarState& car : sim.snapshot().cars) {
                CHECK_FALSE(isWetCompound(car.compound));
            }
        }
    }
}

TEST_CASE("Rain tyres: a wet start puts the field on rain tyres from the grid") {
    Sim sim = makeRace(3, plan({{0.0, 1.0}}), 10);
    for (const CarState& car : sim.snapshot().cars) {
        CHECK(isWetCompound(car.compound));
    }
}
