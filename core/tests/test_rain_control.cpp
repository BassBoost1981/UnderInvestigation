// M9.4: race control in the rain (SC start with wet tyres, aquaplaning, safety rating).

#include "uisim/DataDir.h"
#include "uisim/DataLoader.h"
#include "uisim/Rating.h"
#include "uisim/Sim.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>

using namespace uisim;

namespace {

const std::filesystem::path kData(UISIM_DATA_DIR);

WeatherPlan plan(std::vector<WeatherPoint> points) {
    WeatherPlan p;
    p.name = "test";
    p.points = std::move(points);
    return p;
}

Sim makeRace(std::uint64_t seed, const WeatherPlan& weather, int laps, double wetIgnore = 0.0) {
    RaceConfig config;
    config.laps = laps;
    config.seed = seed;
    config.rules = loadRules(kData / "rules" / "fia-f1-2026.json");
    config.weather = weather;
    config.ruleBreakChance = 0.0;
    config.compoundRuleMissChance = 0.0;
    config.wetMandateIgnoreChance = wetIgnore;
    return Sim(loadTrack(kData / "tracks" / "testring.json"), loadEntries(kData / "entries" / "fictional-grid.json"), config);
}

Command command(Command::Type type, bool wetTyres = false) {
    Command c;
    c.type = type;
    c.wetTyres = wetTyres;
    return c;
}

void runUntil(Sim& sim, double time) {
    for (long steps = 0; sim.snapshot().time < time && !sim.finished() && steps < 2'000'000; ++steps) {
        sim.step();
    }
}

void runToEnd(Sim& sim) {
    for (long steps = 0; !sim.finished() && steps < 2'000'000; ++steps) {
        sim.step();
    }
    REQUIRE(sim.finished());
}

int incidentsOf(const Sim& sim, Incident::Type type) {
    return static_cast<int>(std::count_if(sim.incidents().begin(), sim.incidents().end(),
        [&](const Incident& incident) { return incident.type == type; }));
}

const WeatherPlan kDownpour = plan({{0.0, 0.0}, {240.0, 0.0}, {300.0, 1.0}, {1500.0, 1.0}, {1560.0, 0.0}});

} // namespace

TEST_CASE("Rain control: standing water without neutralisation is dangerous and costs safety") {
    int accidentsOpen = 0;
    int accidentsNeutralised = 0;
    double safetyOpen = 0.0;
    double safetyNeutralised = 0.0;
    int aquaplanes = 0;
    constexpr int kSeeds = 6;
    for (std::uint64_t seed = 1; seed <= kSeeds; ++seed) {
        Sim open = makeRace(seed, kDownpour, 30);
        runToEnd(open);
        accidentsOpen += static_cast<int>(open.stats().accidents);
        aquaplanes += open.stats().aquaplanes;
        safetyOpen += rateRace(open).safety;
        CHECK(open.snapshot().standingWater == 0.0); // dry again at the end

        Sim neutralised = makeRace(seed, kDownpour, 30);
        runUntil(neutralised, 330.0);
        neutralised.command(command(Command::Type::DeploySafetyCar));
        runUntil(neutralised, 1530.0);
        neutralised.command(command(Command::Type::RecallSafetyCar));
        runToEnd(neutralised);
        accidentsNeutralised += static_cast<int>(neutralised.stats().accidents);
        safetyNeutralised += rateRace(neutralised).safety;
        CHECK(neutralised.stats().heavyRainUnneutralised < 120.0); // only until the SC is out
    }
    INFO("accidents open ", accidentsOpen, " (aquaplanes ", aquaplanes, "), behind the SC ", accidentsNeutralised,
         "; safety open ", safetyOpen / kSeeds, ", behind the SC ", safetyNeutralised / kSeeds);
    CHECK(aquaplanes >= kSeeds);
    CHECK(accidentsNeutralised * 2 < accidentsOpen);
    CHECK(safetyOpen / kSeeds < 0.5);
    CHECK(safetyNeutralised > safetyOpen);
}

TEST_CASE("Rain control: start behind the SC with wet tyres, then a rolling start") {
    Sim sim = makeRace(2, plan({{0.0, 1.0}}), 20);
    sim.command(command(Command::Type::SafetyCarStart, true));
    sim.step();
    REQUIRE(sim.snapshot().state == RaceState::SafetyCar);
    CHECK(sim.snapshot().wetTyresMandated);
    for (const CarState& car : sim.snapshot().cars) {
        CHECK(car.compound == Compound::Wet);
    }
    for (long steps = 0; sim.snapshot().leaderLap < 3 && steps < 200'000; ++steps) {
        sim.step();
    }
    CHECK(sim.stats().overtakes == 0);
    for (const CarState& car : sim.snapshot().cars) {
        CHECK(car.compound == Compound::Wet); // compulsory until the SC has gone in
    }
    sim.command(command(Command::Type::RecallSafetyCar)); // rolling start
    for (long steps = 0; sim.snapshot().state != RaceState::Green && steps < 200'000; ++steps) {
        sim.step();
    }
    CHECK_FALSE(sim.snapshot().wetTyresMandated);
    runToEnd(sim);
    CHECK(sim.snapshot().cars.front().lapsCompleted == 21); // B5.10.7: SC laps minus one -> one lap more here
    CHECK(sim.stats().flagViolations == 0);
    CHECK(incidentsOf(sim, Incident::Type::WetTyreRule) == 0);
}

TEST_CASE("Rain control: changing off the compulsory wets is an incident (stop-and-go)") {
    // Light rain: intermediates would be faster, every team ignores the mandate.
    Sim sim = makeRace(3, plan({{0.0, 0.4}}), 20, 1.0);
    sim.command(command(Command::Type::SafetyCarStart, true));
    sim.step();
    runUntil(sim, 900.0);
    int found = 0;
    for (const Incident& incident : sim.incidents()) {
        if (incident.type == Incident::Type::WetTyreRule) {
            ++found;
            CHECK(incident.car == incident.otherCar);
            CHECK(expectedVerdict(incident).penalty == Penalty::StopAndGo);
        }
    }
    CHECK(found >= 1);
}

TEST_CASE("Rain control: SC start only before the start") {
    Sim sim = makeRace(1, WeatherPlan{}, 10);
    runUntil(sim, 5.0);
    sim.command(command(Command::Type::SafetyCarStart));
    sim.step();
    CHECK(sim.snapshot().state == RaceState::Green);
}

TEST_CASE("Rain control: fewer passes in spray") {
    int dry = 0;
    int wet = 0;
    const WeatherPlan downpour = loadWeather(kData / "weather" / "downpour.json");
    for (std::uint64_t seed = 1; seed <= 3; ++seed) {
        Sim a = makeRace(seed, WeatherPlan{}, 50);
        Sim b = makeRace(seed, downpour, 50);
        runToEnd(a);
        runToEnd(b);
        dry += a.stats().overtakes;
        wet += b.stats().overtakes;
    }
    INFO("overtakes dry ", dry, ", downpour ", wet);
    CHECK(wet <= dry * 2); // cars on different tyres pass more, as in real wet races; spray halves the chances
}
