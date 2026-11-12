#pragma once

#include "uisim/Entry.h"
#include "uisim/Rules.h"
#include "uisim/Season.h"
#include "uisim/Sim.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace uisim {

// The player's career over a season (M10.5): the championships, the rating of every race,
// penalty points on the drivers' licences with the suspension (A3.3.1 b), and a save file
// written between the races. Entry indices here always mean the season's entry list; a race
// with suspended drivers runs with fewer cars (see careerRaceEntries).

struct CareerPenaltyPoints {
    int entry = 0;
    int points = 0;
};

// One finished race, as shown on the season screen.
struct CareerRace {
    std::string name;
    std::vector<int> podium;    // entries in places 1 to 3
    std::vector<int> suspended; // entries that sat this race out
    std::vector<CareerPenaltyPoints> penaltyPoints; // imposed in this race, in order of the verdicts
    // The player's rating (Rating.h).
    double overall = 0.0;
    double fairness = 0.0;
    double consistency = 0.0;
    double raceQuality = 0.0;
    double safety = 0.0;
    int incidents = 0;
    int decided = 0;
    int overturned = 0; // verdicts set aside on review before the next race (M11.5)
};

struct Career {
    std::string season;     // name of the calendar
    std::uint64_t seed = 1; // season seed (see seasonRaceSeed)
    int nextRace = 0;       // index into the calendar
    Championship championship;
    std::vector<int> penaltyPoints; // per entry: on the licence now
    // Per entry and penalty point the number of the race it came from: points expire after as many
    // races as the calendar has, the sim's twelve months (A3.3.1 b, M11.6).
    std::vector<std::vector<int>> pointRaces;
    int racesDriven = 0; // over all seasons of this career
    std::vector<int> suspended;     // entries that sit out the next race
    std::vector<CareerRace> races;  // finished races
};

// Penalty points that go with a penalty for this kind of incident (Penalty Guidelines, data/rules).
int penaltyPointsFor(Incident::Type type, const RaceRules& rules);

Career newCareer(const SeasonCalendar& calendar, const std::vector<Entry>& entries, std::uint64_t seed);
bool careerFinished(const Career& career, const SeasonCalendar& calendar);

// The player's rating over the season: mean of the race ratings (0..100), 0 before the first race.
double careerRating(const Career& career);

// Season entry index of every car in the next race, in the order of the race's entry list. Since
// M11.6 that is every entry: a reserve drives the car of a suspended driver.
std::vector<int> careerRaceEntries(const Career& career);

// The reserve driver of a team, for the car of the suspended `regular` (B1, Changes of Driver):
// a little slower, and what he scores counts for the team only.
Entry reserveDriver(const Entry& regular);

// The career after its last race, ready for the next season on the same calendar: fresh
// championships and the next seed; penalty points and a pending suspension carry over.
Career nextSeason(const Career& career, const SeasonCalendar& calendar, const std::vector<Entry>& entries);

// Builds the next race of the career (track, weather, laps and seed as in makeSeasonRace),
// with reserve drivers in the cars of the suspended drivers.
Sim makeCareerRace(const Career& career, const SeasonCalendar& calendar, const std::filesystem::path& dataDir,
                   const std::vector<Entry>& entries, const RaceRules& rules);

// Books a finished race built by makeCareerRace: championship points, the rating, penalty points
// for every penalty the player imposed (none with "no action" or a warning), then the
// suspensions for the following race (at the limit; the limit is then taken off the licence).
void finishCareerRace(Career& career, const SeasonCalendar& calendar, const Sim& sim, const std::vector<Entry>& entries,
                      const RaceRules& rules);

// Save file (JSON), written between races. Loading checks that the file fits the calendar
// and the entry list; both throw std::runtime_error with the file name on failure.
void saveCareer(const std::filesystem::path& path, const Career& career);
Career loadCareer(const std::filesystem::path& path, const SeasonCalendar& calendar, const std::vector<Entry>& entries);

} // namespace uisim
