#include "uisim/Season.h"

#include "uisim/DataLoader.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>

namespace uisim {

int raceLaps(double lapLengthMetres, const RaceRules& rules, bool shortDistance) {
    if (lapLengthMetres <= 0.0) {
        throw std::invalid_argument("Lap length must be positive");
    }
    const double distance = (shortDistance ? rules.raceDistanceShortKm : rules.raceDistanceKm) * 1000.0;
    return static_cast<int>(std::floor(distance / lapLengthMetres)) + 1; // must exceed the distance
}

std::vector<int> racePointsTable(const RaceRules& rules, int leaderLaps, int scheduledLaps, int greenLaps) {
    if (leaderLaps < rules.racePointsMinGreenLaps || greenLaps < rules.racePointsMinGreenLaps || scheduledLaps <= 0) {
        return {};
    }
    // Whole numbers: 4 * laps against the scheduled laps avoids rounding at the 25/50/75 % marks.
    const int quarters = 4 * leaderLaps;
    if (quarters >= 3 * scheduledLaps) {
        return rules.racePoints;
    }
    if (quarters >= 2 * scheduledLaps) {
        return rules.racePointsFrom50Percent;
    }
    if (quarters >= scheduledLaps) {
        return rules.racePointsFrom25Percent;
    }
    return rules.racePointsFromTwoLaps;
}

RaceResult classifyRace(const Sim& sim, const RaceRules& rules) {
    const SimSnapshot& snapshot = sim.snapshot();
    RaceResult result;
    if (snapshot.cars.empty()) {
        return result;
    }
    const int winnerLaps = snapshot.cars.front().lapsCompleted;
    const int minLaps = static_cast<int>(std::floor(rules.classifiedMinDistanceFraction * winnerLaps));
    const std::vector<int> table = racePointsTable(rules, winnerLaps, snapshot.totalLaps, winnerLaps);
    int position = 0;
    for (const CarState& car : snapshot.cars) { // timing order
        RaceResultLine line;
        line.entry = car.entryIndex;
        line.classified = !car.disqualified && car.lapsCompleted >= minLaps;
        if (line.classified) {
            line.position = ++position;
            if (position <= static_cast<int>(table.size())) {
                line.points = table[position - 1];
            }
        }
        result.lines.push_back(line);
    }
    return result;
}

Championship newChampionship(const std::vector<Entry>& entries) {
    Championship championship;
    for (const Entry& entry : entries) {
        ChampionshipLine driver;
        driver.name = entry.name;
        driver.places.assign(entries.size(), 0);
        championship.drivers.push_back(driver);
        const bool known = std::any_of(championship.teams.begin(), championship.teams.end(),
            [&](const ChampionshipLine& team) { return team.name == entry.team; });
        if (!known) {
            ChampionshipLine team;
            team.name = entry.team;
            team.places.assign(entries.size(), 0);
            championship.teams.push_back(team);
        }
    }
    return championship;
}

void addResult(Championship& championship, const std::vector<Entry>& entries, const RaceResult& result) {
    for (const RaceResultLine& line : result.lines) {
        if (line.entry < 0 || line.entry >= static_cast<int>(championship.drivers.size())) {
            throw std::out_of_range("Race result names an unknown entry");
        }
        ChampionshipLine& driver = championship.drivers[line.entry];
        const auto team = std::find_if(championship.teams.begin(), championship.teams.end(),
            [&](const ChampionshipLine& t) { return t.name == entries[line.entry].team; });
        driver.points += line.points;
        if (team != championship.teams.end()) {
            team->points += line.points; // both cars of a team score (A2.1 b)
        }
        if (line.classified && line.position >= 1 && line.position <= static_cast<int>(driver.places.size())) {
            ++driver.places[line.position - 1];
            if (team != championship.teams.end()) {
                ++team->places[line.position - 1];
            }
        }
    }
    ++championship.racesRun;
}

std::vector<int> standings(const std::vector<ChampionshipLine>& lines) {
    std::vector<int> order(lines.size());
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
        if (lines[a].points != lines[b].points) {
            return lines[a].points > lines[b].points;
        }
        return lines[a].places > lines[b].places; // more first places, then second places, ...
    });
    return order;
}

std::uint64_t seasonRaceSeed(std::uint64_t seasonSeed, int index) {
    return seasonSeed * 1000u + static_cast<std::uint64_t>(index) + 1u;
}

Sim makeSeasonRace(const SeasonCalendar& calendar, int index, std::uint64_t seasonSeed, const std::filesystem::path& dataDir,
                   const std::vector<Entry>& entries, const RaceRules& rules) {
    if (index < 0 || index >= static_cast<int>(calendar.races.size())) {
        throw std::out_of_range("Season has no race " + std::to_string(index));
    }
    const SeasonRace& race = calendar.races[index];
    Track track = loadTrack(dataDir / "tracks" / race.track);
    RaceConfig config;
    config.rules = rules;
    config.seed = seasonRaceSeed(seasonSeed, index);
    config.laps = race.laps > 0 ? race.laps : raceLaps(track.length(), rules, race.shortDistance);
    if (!race.weather.empty()) {
        config.weather = loadWeather(dataDir / "weather" / race.weather);
    }
    return Sim(std::move(track), entries, config);
}

} // namespace uisim
