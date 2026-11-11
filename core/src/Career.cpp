#include "uisim/Career.h"

#include "uisim/Rating.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <stdexcept>

namespace uisim {
namespace {

using nlohmann::json;

constexpr int kSaveFormat = 1;

// path.string() throws on Windows for non-ASCII paths; go through UTF-8.
std::string displayName(const std::filesystem::path& path) {
    const std::u8string utf8 = path.u8string();
    return std::string(utf8.begin(), utf8.end());
}

json lineToJson(const ChampionshipLine& line) {
    return {{"name", line.name}, {"points", line.points}, {"places", line.places}};
}

ChampionshipLine lineFromJson(const json& j) {
    ChampionshipLine line;
    line.name = j.at("name").get<std::string>();
    line.points = j.at("points").get<int>();
    line.places = j.at("places").get<std::vector<int>>();
    return line;
}

bool contains(const std::vector<int>& values, int value) {
    return std::find(values.begin(), values.end(), value) != values.end();
}

} // namespace

int penaltyPointsFor(Incident::Type type, const RaceRules& rules) {
    using Type = Incident::Type;
    switch (type) {
    case Type::Collision:
        return rules.penaltyPointsCollision;
    case Type::OvertakeUnderYellow:
        return rules.penaltyPointsYellowFlag;
    case Type::OvertakeUnderVsc:
    case Type::OvertakeUnderSafetyCar:
    case Type::OvertakeBeforeRestartLine:
        return rules.penaltyPointsOvertakingNeutralised;
    case Type::OvertakeUnderRedFlag:
        return rules.penaltyPointsRedFlag;
    default:
        return 0; // pit lane, track limits, tyres, start, blue flags: no penalty points
    }
}

Career newCareer(const SeasonCalendar& calendar, const std::vector<Entry>& entries, std::uint64_t seed) {
    Career career;
    career.season = calendar.name;
    career.seed = seed;
    career.championship = newChampionship(entries);
    career.penaltyPoints.assign(entries.size(), 0);
    return career;
}

bool careerFinished(const Career& career, const SeasonCalendar& calendar) {
    return career.nextRace >= static_cast<int>(calendar.races.size());
}

double careerRating(const Career& career) {
    if (career.races.empty()) {
        return 0.0;
    }
    double sum = 0.0;
    for (const CareerRace& race : career.races) {
        sum += race.overall;
    }
    return sum / static_cast<double>(career.races.size());
}

std::vector<int> careerRaceEntries(const Career& career) {
    std::vector<int> entries;
    for (int entry = 0; entry < static_cast<int>(career.penaltyPoints.size()); ++entry) {
        if (!contains(career.suspended, entry)) {
            entries.push_back(entry);
        }
    }
    return entries;
}

Sim makeCareerRace(const Career& career, const SeasonCalendar& calendar, const std::filesystem::path& dataDir,
                   const std::vector<Entry>& entries, const RaceRules& rules) {
    if (careerFinished(career, calendar)) {
        throw std::out_of_range("The season is over");
    }
    std::vector<Entry> racing;
    for (int entry : careerRaceEntries(career)) {
        racing.push_back(entries.at(entry));
    }
    return makeSeasonRace(calendar, career.nextRace, career.seed, dataDir, racing, rules);
}

void finishCareerRace(Career& career, const SeasonCalendar& calendar, const Sim& sim, const std::vector<Entry>& entries,
                      const RaceRules& rules) {
    if (careerFinished(career, calendar)) {
        throw std::logic_error("The season is over");
    }
    if (!sim.finished()) {
        throw std::logic_error("The race has not finished");
    }
    const std::vector<int> racing = careerRaceEntries(career);
    if (sim.entries().size() != racing.size()) {
        throw std::logic_error("The race does not belong to this career");
    }
    CareerRace race;
    race.name = calendar.races[career.nextRace].name;
    race.suspended = career.suspended;

    RaceResult result = classifyRace(sim, rules);
    for (RaceResultLine& line : result.lines) {
        line.entry = racing[line.entry]; // race entry index -> season entry index
        if (line.classified && line.position <= 3) {
            race.podium.push_back(line.entry);
        }
    }
    addResult(career.championship, entries, result);

    const RaceRating rating = rateRace(sim);
    race.overall = rating.overall;
    race.fairness = rating.fairness;
    race.consistency = rating.consistency;
    race.raceQuality = rating.raceQuality;
    race.safety = rating.safety;
    race.incidents = static_cast<int>(rating.incidents.size());
    race.decided = rating.decided;
    race.overturned = rating.overturned;

    // A3.3.1 b: penalty points go with a penalty other than a reprimand; here per kind of incident.
    // A verdict set aside on review counts as the stewards should have decided.
    for (std::size_t k = 0; k < rating.incidents.size(); ++k) {
        const IncidentRating& decision = rating.incidents[k];
        if (!decision.decided) {
            continue;
        }
        const int car = decision.overturned ? decision.expected.car : decision.givenCar;
        const Penalty penalty = decision.overturned ? decision.expected.penalty : decision.givenPenalty;
        if (penalty == Penalty::None || penalty == Penalty::Warning || car < 0 || car >= static_cast<int>(racing.size())) {
            continue;
        }
        const int points = penaltyPointsFor(sim.incidents()[k].type, rules);
        if (points > 0) {
            const int entry = racing[car];
            career.penaltyPoints[entry] += points;
            race.penaltyPoints.push_back({entry, points});
        }
    }
    // At the limit: suspended for the next race, the limit is then taken off the licence.
    career.suspended.clear();
    for (int entry = 0; entry < static_cast<int>(career.penaltyPoints.size()); ++entry) {
        if (career.penaltyPoints[entry] >= rules.penaltyPointsSuspensionAt) {
            career.suspended.push_back(entry);
            career.penaltyPoints[entry] -= rules.penaltyPointsSuspensionAt;
        }
    }
    career.races.push_back(race);
    ++career.nextRace;
}

void saveCareer(const std::filesystem::path& path, const Career& career) {
    json root;
    root["format"] = kSaveFormat;
    root["season"] = career.season;
    root["seed"] = career.seed;
    root["next_race"] = career.nextRace;
    root["races_run"] = career.championship.racesRun;
    json drivers = json::array();
    for (int entry = 0; entry < static_cast<int>(career.championship.drivers.size()); ++entry) {
        json driver = lineToJson(career.championship.drivers[entry]);
        driver["penalty_points"] = career.penaltyPoints.at(entry);
        driver["suspended"] = contains(career.suspended, entry);
        drivers.push_back(driver);
    }
    root["drivers"] = drivers;
    json teams = json::array();
    for (const ChampionshipLine& team : career.championship.teams) {
        teams.push_back(lineToJson(team));
    }
    root["teams"] = teams;
    json races = json::array();
    for (const CareerRace& race : career.races) {
        json points = json::array();
        for (const CareerPenaltyPoints& p : race.penaltyPoints) {
            points.push_back({{"entry", p.entry}, {"points", p.points}});
        }
        races.push_back({{"name", race.name},
                         {"podium", race.podium},
                         {"suspended", race.suspended},
                         {"penalty_points", points},
                         {"overall", race.overall},
                         {"fairness", race.fairness},
                         {"consistency", race.consistency},
                         {"race_quality", race.raceQuality},
                         {"safety", race.safety},
                         {"incidents", race.incidents},
                         {"decided", race.decided},
                         {"overturned", race.overturned}});
    }
    root["races"] = races;
    std::ofstream file(path);
    file << root.dump(2) << "\n";
    file.flush();
    if (!file) {
        throw std::runtime_error("Cannot write " + displayName(path));
    }
}

Career loadCareer(const std::filesystem::path& path, const SeasonCalendar& calendar, const std::vector<Entry>& entries) {
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("Cannot open " + displayName(path));
    }
    const std::string where = "Bad save file " + displayName(path) + ": ";
    try {
        const json root = json::parse(file);
        if (root.at("format").get<int>() != kSaveFormat) {
            throw std::runtime_error(where + "unknown format");
        }
        Career career;
        career.season = root.at("season").get<std::string>();
        if (career.season != calendar.name) {
            throw std::runtime_error(where + "it belongs to the season '" + career.season + "'");
        }
        career.seed = root.at("seed").get<std::uint64_t>();
        career.nextRace = root.at("next_race").get<int>();
        career.championship.racesRun = root.at("races_run").get<int>();
        const json& drivers = root.at("drivers");
        if (drivers.size() != entries.size()) {
            throw std::runtime_error(where + "the entry list has changed");
        }
        for (int entry = 0; entry < static_cast<int>(drivers.size()); ++entry) {
            const json& driver = drivers[entry];
            ChampionshipLine line = lineFromJson(driver);
            if (line.name != entries[entry].name || line.places.size() != entries.size()) {
                throw std::runtime_error(where + "the entry list has changed");
            }
            career.championship.drivers.push_back(line);
            career.penaltyPoints.push_back(driver.at("penalty_points").get<int>());
            if (driver.at("suspended").get<bool>()) {
                career.suspended.push_back(entry);
            }
        }
        for (const json& team : root.at("teams")) {
            career.championship.teams.push_back(lineFromJson(team));
        }
        for (const json& r : root.at("races")) {
            CareerRace race;
            race.name = r.at("name").get<std::string>();
            race.podium = r.at("podium").get<std::vector<int>>();
            race.suspended = r.at("suspended").get<std::vector<int>>();
            for (const json& p : r.at("penalty_points")) {
                race.penaltyPoints.push_back({p.at("entry").get<int>(), p.at("points").get<int>()});
            }
            race.overall = r.at("overall").get<double>();
            race.fairness = r.at("fairness").get<double>();
            race.consistency = r.at("consistency").get<double>();
            race.raceQuality = r.at("race_quality").get<double>();
            race.safety = r.at("safety").get<double>();
            race.incidents = r.at("incidents").get<int>();
            race.decided = r.at("decided").get<int>();
            race.overturned = r.value("overturned", 0);
            career.races.push_back(race);
        }
        if (career.nextRace < 0 || career.nextRace > static_cast<int>(calendar.races.size()) ||
            static_cast<int>(career.races.size()) != career.nextRace) {
            throw std::runtime_error(where + "the races do not fit the calendar");
        }
        return career;
    } catch (const json::exception& e) {
        throw std::runtime_error(where + e.what());
    }
}

} // namespace uisim
