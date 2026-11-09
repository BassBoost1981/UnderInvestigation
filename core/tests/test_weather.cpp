// M9.2: weather in the core (rain, wetness, grip, errors, spray).

#include "uisim/DataDir.h"
#include "uisim/DataLoader.h"
#include "uisim/Sim.h"

#include <doctest/doctest.h>

#include <filesystem>

using namespace uisim;

namespace {

const std::filesystem::path kData(UISIM_DATA_DIR);

WeatherPlan constantRain(double rain) {
    WeatherPlan plan;
    plan.name = "test";
    plan.points = {{0.0, rain}};
    return plan;
}

Sim makeRace(std::uint64_t seed, const WeatherPlan& weather, int laps = 20) {
    RaceConfig config;
    config.laps = laps;
    config.seed = seed;
    config.rules = loadRules(kData / "rules" / "fia-f1-2026.json");
    config.weather = weather;
    return Sim(loadTrack(kData / "tracks" / "testring.json"), loadEntries(kData / "entries" / "fictional-grid.json"), config);
}

void runToEnd(Sim& sim) {
    for (long steps = 0; !sim.finished() && steps < 2'000'000; ++steps) {
        sim.step();
    }
    REQUIRE(sim.finished());
}

void runUntil(Sim& sim, double time) {
    for (long steps = 0; sim.snapshot().time < time && steps < 2'000'000; ++steps) {
        sim.step();
    }
}

} // namespace

TEST_CASE("Weather data loads and interpolates") {
    const WeatherPlan shower = loadWeather(kData / "weather" / "shower.json");
    CHECK(shower.points.size() == 5);
    CHECK(rainAt(shower, 10.0 * 60.0) == 0.0);
    CHECK(rainAt(shower, 17.5 * 60.0) == doctest::Approx(0.3));
    CHECK(rainAt(shower, 30.0 * 60.0) == doctest::Approx(0.6));
    CHECK(rainAt(shower, 90.0 * 60.0) == 0.0);
    CHECK(rainAt(shower, 30.0 * 60.0, 0.0, 2.0) == 1.0); // clamped
    CHECK(rainAt(WeatherPlan{}, 100.0) == 0.0);
    const WeatherPlan downpour = loadWeather(kData / "weather" / "downpour.json");
    CHECK(rainAt(downpour, 20.0 * 60.0) == 1.0);
}

TEST_CASE("Weather: a dry race stays dry") {
    Sim sim = makeRace(1, WeatherPlan{}, 10);
    for (long steps = 0; !sim.finished() && steps < 2'000'000; ++steps) {
        sim.step();
        if (steps % 500 == 0) {
            CHECK(sim.snapshot().rain == 0.0);
            CHECK(sim.snapshot().lineWetness == 0.0);
            CHECK(sim.snapshot().trackWetness == 0.0);
        }
    }
}

TEST_CASE("Weather: rain makes the race slower and more dangerous") {
    double dryTime = 0.0;
    double wetTime = 0.0;
    int dryAccidents = 0;
    int wetAccidents = 0;
    for (std::uint64_t seed = 1; seed <= 8; ++seed) {
        Sim dry = makeRace(seed, WeatherPlan{});
        Sim wet = makeRace(seed, constantRain(0.8));
        runToEnd(dry);
        runToEnd(wet);
        dryTime += dry.snapshot().cars.front().lastLineTime;
        wetTime += wet.snapshot().cars.front().lastLineTime;
        dryAccidents += static_cast<int>(dry.accidents().size());
        wetAccidents += static_cast<int>(wet.accidents().size());
    }
    INFO("race time dry ", dryTime, " s, wet ", wetTime, " s; accidents dry ", dryAccidents, ", wet ", wetAccidents);
    CHECK(wetTime > 1.08 * dryTime); // on rain tyres since M9.3 (wet start), about 10 % slower
    CHECK(wetAccidents >= dryAccidents * 3 / 2 + 1);
}

TEST_CASE("Weather: the racing line dries first") {
    WeatherPlan plan;
    plan.name = "ten minutes of rain";
    plan.points = {{0.0, 1.0}, {600.0, 1.0}, {601.0, 0.0}};
    Sim sim = makeRace(1, plan, 30);
    runUntil(sim, 600.0);
    CHECK(sim.snapshot().lineWetness > 0.95);
    CHECK(sim.snapshot().trackWetness > 0.95);
    runUntil(sim, 601.0 + 450.0);
    CHECK(sim.snapshot().rain == 0.0);
    CHECK(sim.snapshot().lineWetness == doctest::Approx(0.5).epsilon(0.1));
    CHECK(sim.snapshot().trackWetness > sim.snapshot().lineWetness);
    runUntil(sim, 601.0 + 950.0);
    CHECK(sim.snapshot().lineWetness == 0.0); // about 15 minutes
    CHECK(sim.snapshot().trackWetness > 0.3); // off the line still damp
}

TEST_CASE("Weather: deterministic per seed, varied between seeds") {
    WeatherPlan shower = loadWeather(kData / "weather" / "shower.json");
    shower.variation = 1.0;
    Sim a = makeRace(3, shower);
    Sim b = makeRace(3, shower);
    runToEnd(a);
    runToEnd(b);
    REQUIRE(a.snapshot().cars.size() == b.snapshot().cars.size());
    for (std::size_t k = 0; k < a.snapshot().cars.size(); ++k) {
        CHECK(a.snapshot().cars[k].entryIndex == b.snapshot().cars[k].entryIndex);
        CHECK(a.snapshot().cars[k].lastLineTime == b.snapshot().cars[k].lastLineTime);
    }
    // Another seed shifts the shower: at some moment the rain differs.
    Sim c = makeRace(4, shower);
    Sim d = makeRace(5, shower);
    bool differs = false;
    for (double t = 900.0; t <= 1500.0 && !differs; t += 60.0) {
        runUntil(c, t);
        runUntil(d, t);
        differs = c.snapshot().rain != d.snapshot().rain;
    }
    CHECK(differs);
}
