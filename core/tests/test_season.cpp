// M10.4: season in the core: race distance (B2.5.2), points (A2.2.1), classification (B2.5.5 b),
// championships with the tie rule (A2.1), a reproducible season.

#include "uisim/DataDir.h"
#include "uisim/DataLoader.h"
#include "uisim/Season.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <numeric>
#include <vector>

using namespace uisim;

namespace {

const std::filesystem::path kData(UISIM_DATA_DIR);

std::vector<Entry> fourEntries() {
    std::vector<Entry> entries(4);
    entries[0].name = "A One";
    entries[0].team = "Alpha";
    entries[1].name = "A Two";
    entries[1].team = "Alpha";
    entries[2].name = "B One";
    entries[2].team = "Beta";
    entries[3].name = "B Two";
    entries[3].team = "Beta";
    return entries;
}

// A result with the entries in finishing order, full points; `classified` cars from the front.
RaceResult resultOf(const std::vector<int>& order, int classified) {
    const RaceRules rules;
    RaceResult result;
    for (int k = 0; k < static_cast<int>(order.size()); ++k) {
        RaceResultLine line;
        line.entry = order[k];
        line.classified = k < classified;
        line.position = line.classified ? k + 1 : 0;
        line.points = line.classified ? rules.racePoints[k] : 0;
        result.lines.push_back(line);
    }
    return result;
}

SeasonCalendar shortCalendar() {
    SeasonCalendar calendar;
    calendar.name = "Test season";
    for (int k = 0; k < 3; ++k) {
        SeasonRace race;
        race.name = "Race " + std::to_string(k + 1);
        race.track = "testring.json";
        race.laps = 6;
        calendar.races.push_back(race);
    }
    calendar.races[1].weather = "shower.json";
    return calendar;
}

Championship runSeason(const SeasonCalendar& calendar, std::uint64_t seed, std::vector<int>* winners = nullptr) {
    const std::vector<Entry> entries = loadEntries(kData / "entries" / "fictional-grid.json");
    const RaceRules rules = loadRules(kData / "rules" / "fia-f1-2026.json");
    Championship championship = newChampionship(entries);
    for (int k = 0; k < static_cast<int>(calendar.races.size()); ++k) {
        Sim sim = makeSeasonRace(calendar, k, seed, kData, entries, rules);
        for (long steps = 0; !sim.finished() && steps < 2'000'000; ++steps) {
            sim.step();
        }
        REQUIRE(sim.finished());
        const RaceResult result = classifyRace(sim, rules);
        if (winners) {
            winners->push_back(result.lines.front().entry);
        }
        addResult(championship, entries, result);
    }
    return championship;
}

} // namespace

TEST_CASE("Race distance: the least number of laps that exceeds 305 km (B2.5.2)") {
    const RaceRules rules;
    CHECK(raceLaps(5000.0, rules, false) == 62);  // 61 laps are exactly 305 km: not more than the distance
    CHECK(raceLaps(5793.0, rules, false) == 53);  // 307.0 km
    CHECK(raceLaps(7004.0, rules, false) == 44);  // 308.2 km
    CHECK(raceLaps(3337.0, rules, true) == 78);   // short distance: 260.3 km
    CHECK(raceLaps(3337.0, rules, false) == 92);
    CHECK_THROWS(raceLaps(0.0, rules, false));
    for (double length : {3337.0, 4400.0, 5793.0, 7004.0}) {
        const int laps = raceLaps(length, rules, false);
        CHECK(laps * length > 305000.0);
        CHECK((laps - 1) * length <= 305000.0);
    }
}

TEST_CASE("Points table by the distance the leader completed (A2.2.1)") {
    const RaceRules rules;
    constexpr int kScheduled = 60;
    CHECK(racePointsTable(rules, 1, kScheduled, 1).empty());           // fewer than two laps
    CHECK(racePointsTable(rules, 30, kScheduled, 1).empty());          // not two laps under green
    CHECK(racePointsTable(rules, 2, kScheduled, 2) == rules.racePointsFromTwoLaps);
    CHECK(racePointsTable(rules, 14, kScheduled, 14) == rules.racePointsFromTwoLaps);
    CHECK(racePointsTable(rules, 15, kScheduled, 15) == rules.racePointsFrom25Percent); // exactly 25 %
    CHECK(racePointsTable(rules, 29, kScheduled, 29) == rules.racePointsFrom25Percent);
    CHECK(racePointsTable(rules, 30, kScheduled, 30) == rules.racePointsFrom50Percent); // exactly 50 %
    CHECK(racePointsTable(rules, 44, kScheduled, 44) == rules.racePointsFrom50Percent);
    CHECK(racePointsTable(rules, 45, kScheduled, 45) == rules.racePoints);              // exactly 75 %
    CHECK(racePointsTable(rules, 60, kScheduled, 60) == rules.racePoints);
    CHECK(racePointsTable(rules, 60, kScheduled, 60).front() == 25);
    CHECK(racePointsTable(rules, 30, 0, 30).empty());
}

TEST_CASE("Championship by hand: points, both cars score for the team, ties by places (A2.1)") {
    const std::vector<Entry> entries = fourEntries();
    Championship championship = newChampionship(entries);
    REQUIRE(championship.drivers.size() == 4);
    REQUIRE(championship.teams.size() == 2);
    CHECK(championship.teams[0].name == "Alpha");

    addResult(championship, entries, resultOf({0, 2, 1, 3}, 4)); // A1 25, B1 18, A2 15, B2 12
    CHECK(championship.drivers[0].points == 25);
    CHECK(championship.drivers[2].points == 18);
    CHECK(championship.teams[0].points == 40); // 25 + 15
    CHECK(championship.teams[1].points == 30); // 18 + 12
    CHECK(standings(championship.drivers) == std::vector<int>{0, 2, 1, 3});

    addResult(championship, entries, resultOf({2, 0, 3, 1}, 4)); // B1 25, A1 18, B2 15, A2 12
    // A One and B One both have 43 points, one win and one second place: the earlier line stays ahead.
    CHECK(championship.drivers[0].points == 43);
    CHECK(championship.drivers[2].points == 43);
    CHECK(standings(championship.drivers) == std::vector<int>{0, 2, 1, 3});
    CHECK(championship.teams[0].points == 70);
    CHECK(championship.teams[1].points == 70);

    // Race 3: B One wins; A One third. Race 4: A One wins, B One third, the last car is not classified.
    addResult(championship, entries, resultOf({2, 1, 0, 3}, 4)); // B1 25, A2 18, A1 15, B2 12
    addResult(championship, entries, resultOf({0, 3, 2, 1}, 3)); // A1 25, B2 18, B1 15, A2 -
    CHECK(championship.racesRun == 4);
    CHECK(championship.drivers[0].points == 83); // 25 + 18 + 15 + 25
    CHECK(championship.drivers[2].points == 83); // 18 + 25 + 25 + 15
    CHECK(championship.drivers[1].points == 45); // 15 + 12 + 18 + 0
    CHECK(championship.drivers[3].points == 57); // 12 + 15 + 12 + 18
    // Equal on points and on wins (2 each) and on second places (1 each); third places 1 each too:
    CHECK(championship.drivers[0].places[0] == 2);
    CHECK(championship.drivers[2].places[0] == 2);
    CHECK(standings(championship.drivers) == std::vector<int>{0, 2, 3, 1});
    CHECK(championship.drivers[1].places[3] == 1); // fourth once; the unclassified race counts for no place
    CHECK(std::accumulate(championship.drivers[1].places.begin(), championship.drivers[1].places.end(), 0) == 3);

    // The tie rule decides: same points, more wins first; equal wins, more second places first.
    std::vector<ChampionshipLine> lines(3);
    lines[0].points = 50;
    lines[0].places = {1, 0, 5};
    lines[1].points = 50;
    lines[1].places = {2, 0, 0};
    lines[2].points = 50;
    lines[2].places = {1, 3, 0};
    CHECK(standings(lines) == std::vector<int>{1, 2, 0});
    lines[0].points = 51;
    CHECK(standings(lines) == std::vector<int>{0, 1, 2});
}

TEST_CASE("A finished race is classified and scored (B2.5.5 b, A2.2.1)") {
    const std::vector<Entry> entries = loadEntries(kData / "entries" / "fictional-grid.json");
    const RaceRules rules = loadRules(kData / "rules" / "fia-f1-2026.json");
    SeasonCalendar calendar = shortCalendar();
    calendar.races[0].laps = 20;
    Sim sim = makeSeasonRace(calendar, 0, 4, kData, entries, rules);
    CHECK(sim.snapshot().totalLaps == 20);
    for (long steps = 0; !sim.finished() && steps < 2'000'000; ++steps) {
        sim.step();
    }
    REQUIRE(sim.finished());
    const RaceResult result = classifyRace(sim, rules);
    REQUIRE(result.lines.size() == entries.size());
    CHECK(result.lines.front().points == 25);
    int position = 0;
    int total = 0;
    for (std::size_t k = 0; k < result.lines.size(); ++k) {
        const RaceResultLine& line = result.lines[k];
        const CarState& car = sim.snapshot().cars[k];
        CHECK(line.entry == car.entryIndex);
        CHECK(line.classified == (!car.disqualified && car.lapsCompleted >= 18)); // 90 % of 20 laps
        if (line.classified) {
            CHECK(line.position == ++position);
            CHECK(line.points == (position <= 10 ? rules.racePoints[position - 1] : 0));
        } else {
            CHECK(line.position == 0);
            CHECK(line.points == 0);
        }
        total += line.points;
    }
    CHECK(position >= 10);
    CHECK(total == 101); // 25 + 18 + 15 + 12 + 10 + 8 + 6 + 4 + 2 + 1
}

TEST_CASE("The same seed gives the same season, the races of a season differ") {
    const SeasonCalendar calendar = shortCalendar();
    std::vector<int> winners;
    const Championship a = runSeason(calendar, 11, &winners);
    const Championship b = runSeason(calendar, 11);
    const Championship c = runSeason(calendar, 12);
    REQUIRE(a.racesRun == 3);
    bool different = false;
    int points = 0;
    for (std::size_t k = 0; k < a.drivers.size(); ++k) {
        CHECK(a.drivers[k].points == b.drivers[k].points);
        CHECK(a.drivers[k].places == b.drivers[k].places);
        different = different || a.drivers[k].points != c.drivers[k].points;
        points += a.drivers[k].points;
    }
    CHECK(different); // another seed, another season
    CHECK(points == 303);
    int teamPoints = 0;
    for (const ChampionshipLine& team : a.teams) {
        teamPoints += team.points;
    }
    CHECK(teamPoints == points);
    CHECK(seasonRaceSeed(11, 0) != seasonRaceSeed(11, 1));
    CHECK(seasonRaceSeed(11, 2) != seasonRaceSeed(12, 2));
    CHECK(standings(a.drivers).front() == standings(b.drivers).front());
    REQUIRE(winners.size() == 3);
}

TEST_CASE("The example calendar loads; its races get their laps from the race distance") {
    const SeasonCalendar calendar = loadSeason(kData / "seasons" / "example.json");
    const RaceRules rules = loadRules(kData / "rules" / "fia-f1-2026.json");
    const std::vector<Entry> entries = loadEntries(kData / "entries" / "fictional-grid.json");
    CHECK_FALSE(calendar.name.empty());
    REQUIRE(calendar.races.size() >= 3);
    bool sawShort = false;
    bool sawWeather = false;
    for (int k = 0; k < static_cast<int>(calendar.races.size()); ++k) {
        const SeasonRace& race = calendar.races[k];
        CHECK_FALSE(race.name.empty());
        const Sim sim = makeSeasonRace(calendar, k, 1, kData, entries, rules);
        const double km = sim.snapshot().totalLaps * sim.track().length() / 1000.0;
        const double wanted = race.shortDistance ? 260.0 : 305.0;
        CHECK(km > wanted);
        CHECK(km - sim.track().length() / 1000.0 <= wanted);
        sawShort = sawShort || race.shortDistance;
        sawWeather = sawWeather || !race.weather.empty();
    }
    CHECK(sawShort);
    CHECK(sawWeather);
    CHECK_THROWS(makeSeasonRace(calendar, static_cast<int>(calendar.races.size()), 1, kData, entries, rules));
    CHECK_THROWS(loadSeason(kData / "seasons" / "missing.json"));
}

TEST_CASE("Form of the day: per race, per name, the same for both cars of a team (M11.2)") {
    const std::vector<Entry> entries = loadEntries(kData / "entries" / "fictional-grid.json");
    const Track track = loadTrack(kData / "tracks" / "testring.json");
    const std::vector<Entry> a = seasonRaceEntries(entries, 5, 0, track, false);
    const std::vector<Entry> again = seasonRaceEntries(entries, 5, 0, track, false);
    const std::vector<Entry> nextRace = seasonRaceEntries(entries, 5, 1, track, false);
    const std::vector<Entry> wet = seasonRaceEntries(entries, 5, 0, track, true);
    REQUIRE(a.size() == entries.size());
    bool raceDiffers = false;
    bool rainDiffers = false;
    for (std::size_t k = 0; k < entries.size(); ++k) {
        CHECK(a[k].name == entries[k].name);
        CHECK(a[k].teamPace == again[k].teamPace); // reproducible
        CHECK(a[k].skill == again[k].skill);
        // A few places, not through the field: within 2 % of the entry list.
        CHECK(a[k].teamPace / entries[k].teamPace == doctest::Approx(1.0).epsilon(0.02));
        CHECK(a[k].skill / entries[k].skill == doctest::Approx(1.0).epsilon(0.02));
        raceDiffers = raceDiffers || a[k].teamPace != nextRace[k].teamPace;
        CHECK(wet[k].teamPace == a[k].teamPace); // rain is the driver's matter
        rainDiffers = rainDiffers || wet[k].skill != a[k].skill;
        for (std::size_t other = 0; other < k; ++other) {
            if (entries[other].team == entries[k].team) { // one car, one form
                CHECK(a[k].teamPace / entries[k].teamPace == doctest::Approx(a[other].teamPace / entries[other].teamPace));
            }
        }
    }
    CHECK(raceDiffers);
    CHECK(rainDiffers);
    // A smaller field (a suspended driver) leaves everyone else as they were.
    std::vector<Entry> fewer = entries;
    fewer.erase(fewer.begin() + 3);
    const std::vector<Entry> b = seasonRaceEntries(fewer, 5, 0, track, false);
    for (std::size_t k = 0; k < b.size(); ++k) {
        const std::size_t from = k < 3 ? k : k + 1;
        CHECK(b[k].teamPace == a[from].teamPace);
        CHECK(b[k].skill == a[from].skill);
    }
}

TEST_CASE("Private season (only if present): several winners, nobody wins more than half, most drivers score (M11.2)") {
    const std::filesystem::path file = kData / "seasons" / "private" / "season-2026.json";
    if (!std::filesystem::exists(file)) {
        MESSAGE("no private season in data/seasons/private: skipped");
        return;
    }
    const SeasonCalendar calendar = loadSeason(file);
    const int races = static_cast<int>(calendar.races.size());
    int winnersTotal = 0;
    constexpr int kSeeds = 3;
    for (std::uint64_t seed = 1; seed <= kSeeds; ++seed) {
        std::vector<int> winners;
        const Championship championship = runSeason(calendar, seed, &winners);
        std::vector<int> wins(championship.drivers.size(), 0);
        for (int winner : winners) {
            ++wins[winner];
        }
        const int different = static_cast<int>(std::count_if(wins.begin(), wins.end(), [](int w) { return w > 0; }));
        const int most = *std::max_element(wins.begin(), wins.end());
        const int scorers = static_cast<int>(std::count_if(championship.drivers.begin(), championship.drivers.end(),
            [](const ChampionshipLine& driver) { return driver.points > 0; }));
        INFO("seed ", seed, ": ", different, " winners, most wins ", most, ", ", scorers, " drivers with points");
        CHECK(different >= 5);
        CHECK(most * 5 <= races * 3); // nobody runs away with it: at most 60 % of the wins
        CHECK(scorers >= 14);
        winnersTotal += different;
    }
    CHECK(winnersTotal >= 6 * kSeeds); // six winners a season on average
}
